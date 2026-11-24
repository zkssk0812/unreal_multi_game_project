#include "ServerCore/CorePch.h"
#include "Content/Game/GameWorld.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "Content/GameData.h"
#include "Content/Game/PathFinder.h"
#include "Content/Game/Sight.h"
#include "Content/Game/WorldMath.h"
#include "Content/Perf.h"

// 경비원 AI (docs/psd.md M3, 명세 §12). GameWorld의 멤버 함수를 파일만 나눠 둔 것.
//
// 행동 트리 (docs/경비원AI_행동트리.md, 2026-11-25에 FSM에서 바꿈). 매 틱 위에서부터 다시 평가:
//   ? 경비원
//     → 수리(HP 0, 5초 무적·정지)  → 경직(섬광·테이저·제트팩·감전)  → 깨어남(할 일만 정하고 아래로)
//     → 전투(놓침 / 돌기·쏘기·움직이기)  → 발견(보이면 추격 + 동료 호출)  → 탐색  → 순찰
//   하던 일(경로·탐색 지점·둘러보기)은 GuardBrain(블랙보드)에, brain.state는 SC_GUARD_ALERT로 클라에 간다.
//   틀은 GuardBT.h, 트리는 아래 BuildGuardTree.
//
// 범위 (기획 그림): 탐지 범위 반지름 15m, 시야각 정면 90°(좌우 45°), 공격 범위 정면 60°(좌우 30°)
// 몸은 초당 90°로만 돈다 (N-04): 타겟이 시야 안이어도 공격 범위 밖이면 돌아서 맞춘 뒤 쏜다 → 옆·뒤로 돌면 피할 수 있다.
//
// 개선 (docs/경비원AI_공격탐색_개선안.md, 항목마다 GuardFeatures로 끌 수 있음. 기본 수치는 그대로)
//   A-2 물러나며 돌아서기: 정면 60° 밖에서 맞거나(지금 타겟 포함) 4m 안의 타겟이 정면 밖이면,
//       타겟 쪽으로 90°/s로 돌면서 대각선 뒤로 달린다. 정면에 들어오면 바로 쏘고, 정면에 둔 채 4m 넘게 벌어지거나 2초 지나면 끝
//   탐색: S-1 도망 방향 1초 앞(최대 3m)부터 · S-3 첫 지점 한 바퀴(4초) · S-4 주변 3~7m 최대 2곳 좌우 60° 훑기 ·
//         S-5 발견하면 20m 안 동료가 탐색하러 옴 · S-6 경직·수리 뒤 최근 6초 안에 본 곳부터
//   순찰: P-1 오래 안 간 곳 우선(3명 공유 기록) · P-2 동료와 겹치지 않기 · P-3 연결 2개 지점은 통과 ·
//         P-4 왼쪽 → 오른쪽 45° 훑기(문에서는 방 안쪽, 가운데로 돌아오지 않음) · P-5 금고·반납 지점 방 가중치 · P-6 가끔 뒤돌아보기 · P-7 탐색 뒤 30초 경계 순찰
//
// 타겟 규칙 (N-13~N-16)
//   처음 발견한 플레이어를 유지 · 동시에 보이면 가까운 쪽 · 다른 플레이어가 공격하면 그 사람으로 바꾸고,
//   공격 때문에 바뀐 타겟은 시야에서 벗어나기 전까지 고정 · 은신·연막 안·쓰러짐·리스폰 무적은 감지 못 함

namespace Content {

using namespace WorldMath;

namespace {

AlertLevel AlertOf(GuardState state)
{
	switch (state)
	{
	case GuardState::Chase: return AlertLevel::Chase;
	case GuardState::Attack: return AlertLevel::Attack;
	case GuardState::Search: return AlertLevel::Search;
	case GuardState::Repairing: return AlertLevel::Repairing;
	case GuardState::Stunned: return AlertLevel::Stunned;
	default: return AlertLevel::Patrol;
	}
}

// 경비원이 감지할 수 있는 플레이어인가 (N-12, T-31)
bool Detectable(const Entity& thief)
{
	constexpr uint32_t hidden = StateBit::Downed | StateBit::LeftGame | StateBit::Cloaked | StateBit::InSmoke | StateBit::Invulnerable;
	return thief.IsThief() && (thief.state & hidden) == 0;
}

} // namespace

// ───────────────── 매 틱 ─────────────────

void GameWorld::UpdateGuards(uint32_t dtMs)
{
	if (!_aiEnabled || _map == nullptr)
		return;
	const auto begin = std::chrono::steady_clock::now();
	_sightTests = 0;

	for (GuardBrain& brain : _brains)
		if (Entity* guard = Find(brain.guardId))
			ThinkGuard(*guard, brain, dtMs);

	Perf::RecordAI(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now() - begin).count()), _sightTests);
}

void GameWorld::UpdateGuardTimers(Entity& guard)
{
	// AI를 꺼도 수리·경직 시간은 흐른다 (규칙 시험)
	if ((guard.state & StateBit::Repairing) != 0 && _nowMs >= guard.repairUntilMs)
	{
		guard.hp = GameData::GuardMaxHp;		// 수리 끝 → HP 200 (N-06)
		ClearBits(guard.state, StateBit::Repairing | StateBit::Invulnerable);
	}
	if ((guard.state & StateBit::Stunned) != 0 && _nowMs >= guard.stunUntilMs)
		ClearBits(guard.state, StateBit::Stunned);
}

void GameWorld::ThinkGuard(Entity& guard, GuardBrain& brain, uint32_t dtMs)
{
	GuardCtx ctx{ *this, guard, brain, dtMs };
	GuardTree().Tick(ctx);
}

// ───────────────── 행동 트리 (docs/경비원AI_행동트리.md) ─────────────────
//
// ? 경비원 (매 틱 위에서부터 다시 평가)
//   → 수리   → 경직   → 깨어남(할 일만 정하고 같은 틱에 아래로)   → 전투   → 발견   → 탐색   → 순찰
// 하던 일(경로·탐색 지점·둘러보기)은 GuardBrain에 남아 있고, brain.state가 SC_GUARD_ALERT로 클라에 간다.

const Bt::Node<GuardCtx>& GameWorld::GuardTree()
{
	static const std::unique_ptr<Bt::Node<GuardCtx>> tree = BuildGuardTree();
	return *tree;
}

std::unique_ptr<Bt::Node<GuardCtx>> GameWorld::BuildGuardTree()
{
	using N = Bt::Node<GuardCtx>;
	using Bt::Status;
	using C = GuardCtx;

	// ── 수리: HP 0 → 5초 제자리 (무적) ──
	auto repair = N::Sequence("수리",
		N::If("HP 0으로 수리 중", [](C& c) { return (c.guard.state & StateBit::Repairing) != 0; }),
		N::Do("제자리에서 기다림", [](C& c) {
			if (c.brain.state != GuardState::Repairing)
				c.world.SetGuardState(c.guard, c.brain, GuardState::Repairing);
			return Status::Running;
		}));

	// ── 경직: 타겟·도망 방향은 기억한다 ──
	auto stun = N::Sequence("경직",
		N::If("경직 중", [](C& c) { return (c.guard.state & StateBit::Stunned) != 0; }),
		N::Do("기다림 (타겟 기억)", [](C& c) {
			if (c.brain.state != GuardState::Stunned)
			{
				const uint16_t keep = c.brain.target;
				const bool locked = c.brain.lockedByAttack;
				const Vec3 vel = c.brain.seenVel;
				c.world.SetGuardState(c.guard, c.brain, GuardState::Stunned);
				c.brain.target = keep;				// 경직이 풀리면 같은 타겟을 다시 본다
				c.brain.lockedByAttack = locked;
				c.brain.seenVel = vel;				// 도망 방향 예측에 쓴다 (S-1·S-6)
			}
			return Status::Running;
		}));

	// ── 깨어남: 수리·경직이 끝난 틱. 할 일만 정하고 실패를 돌려줘서, 같은 틱에 아래 가지(전투·탐색)가 이어서 움직인다 ──
	auto wake = N::Sequence("깨어남",
		N::If("방금 수리·경직이 끝남", [](C& c) {
			return c.brain.state == GuardState::Repairing || c.brain.state == GuardState::Stunned;
		}),
		N::Select("다시 할 일",
			N::Do("기억하던 타겟이 보이면 추격", [](C& c) {
				Entity* old = c.world.Find(c.brain.target);
				if (old == nullptr || !Detectable(*old) || !c.world.CanSee(c.guard, *old))
					return Status::Failure;
				const bool locked = c.brain.lockedByAttack;
				c.world.SetGuardState(c.guard, c.brain, GuardState::Chase, old->id);
				c.brain.lockedByAttack = locked;
				return Status::Success;
			}),
			N::Do("6초 안에 본 곳부터 탐색 (S-6)", [](C& c) {
				GameWorld& w = c.world;
				if (!w._guardFeatures.stunMemory || c.brain.lastSeenMs == 0 || w._nowMs - c.brain.lastSeenMs > GameData::StunMemoryMs)
					return Status::Failure;
				w.StartSearch(c.guard, c.brain, c.brain.lastSeenPos, true);
				return Status::Success;
			}),
			N::Do("제자리부터 탐색", [](C& c) {
				c.world.StartSearch(c.guard, c.brain, c.guard.pos, false);
				return Status::Success;
			})),
		N::Do("아래 가지로 이어서", [](C&) { return Status::Failure; }));

	// ── 전투: 놓침 ──
	auto lost = N::Sequence("놓침",
		N::If("쓰러짐·은신·연막, 또는 3초 못 봄", [](C& c) {
			return !c.valid || (!c.visible && c.world._nowMs - c.brain.lastSeenMs > GameData::ChaseLoseMs);
		}),
		N::Select("다음 타겟",
			N::Do("다른 플레이어가 보이면 그쪽 추격", [](C& c) {
				Entity* other = c.world.NearestVisibleThief(c.guard);
				if (other == nullptr)
					return Status::Failure;
				c.world.SetGuardState(c.guard, c.brain, GuardState::Chase, other->id);
				c.world.NoteTargetPos(c.brain, other->pos);
				return Status::Running;
			}),
			N::Do("마지막으로 본 곳 탐색", [](C& c) {
				c.world.StartSearch(c.guard, c.brain, c.brain.lastSeenPos, true);
				return Status::Running;
			})));

	// ── 전투: 쏘기 ──
	auto shoot = N::Select("쏘기",
		N::Sequence("정면 60° 안",
			N::If("공격 범위 안", [](C& c) { return c.visible && c.inCone; }),
			N::Do("0.1초마다 사격", [](C& c) {
				// 멈추지 않고 아래에서 계속 쫓아간다 (12.2 이동, A-18)
				GameWorld& w = c.world;
				if (c.brain.state != GuardState::Attack)
				{
					const bool locked = c.brain.lockedByAttack;
					w.SetGuardState(c.guard, c.brain, GuardState::Attack, c.target->id);
					c.brain.lockedByAttack = locked;
				}
				c.guard.state |= StateBit::Attacking;
				if (w._nowMs >= c.guard.nextGunMs)
				{
					c.guard.nextGunMs = w._nowMs + GameData::GuardGunEveryMs;
					w.GuardShoot(c.guard, *c.target);
				}
				return Status::Success;
			})),
		N::Do("추격 알림", [](C& c) {
			if (c.visible && c.brain.state != GuardState::Chase)
			{
				const bool locked = c.brain.lockedByAttack;
				c.world.SetGuardState(c.guard, c.brain, GuardState::Chase, c.target->id);
				c.brain.lockedByAttack = locked;
			}
			return Status::Success;
		}));

	// ── 전투: 움직이기 ──
	auto move = N::Select("움직이기",
		N::Sequence("물러나며 돌아서기 (A-2)",
			N::If("4m 안 정면 밖·옆뒤에서 맞음", [](C& c) {
				return c.world._guardFeatures.evade && c.world._nowMs < c.brain.evadeUntilMs;
			}),
			N::Do("대각선 뒤로 달림", [](C& c) {
				// 타겟(안 보이면 짐작한 위치) 쪽으로 돌면서. 경로를 따르지 않으므로 끝나면 경로를 새로 찾는다
				GameWorld& w = c.world;
				const Vec3 threat = c.visible ? c.target->pos : w.GuessTargetPos(c.brain);
				if (!c.visible)
					w.TurnToward(c.guard, YawTo(c.guard.pos, threat), c.dtMs);
				w.EvadeStep(c.guard, c.brain, threat, GameData::GuardRunSpeed * c.slow, c.dtMs);
				c.brain.path.clear();
				return Status::Running;
			})),
		N::Sequence("붙음",
			N::If("보이고 몸이 닿음", [](C& c) {
				return c.visible && Dist2D(c.guard.pos, c.target->pos) <= GameData::BodyRadiusCm * 2.0f;
			}),
			N::Do("더 다가가지 않음", [](C&) { return Status::Running; })),
		N::Do("타겟·마지막으로 본 곳으로 달림", [](C& c) {
			// 보이면 지금 위치로, 안 보이면 마지막으로 본 곳으로 (경로는 0.5초마다).
			// 갈 수 없거나, 마지막으로 본 곳에 왔는데 없으면 탐색
			GameWorld& w = c.world;
			GuardBrain& brain = c.brain;
			const Vec3 goal = c.visible ? c.target->pos : brain.lastSeenPos;
			if ((brain.path.empty() || w._nowMs >= brain.repathAtMs) && !w.SetGoal(c.guard, brain, goal))
			{
				w.StartSearch(c.guard, brain, brain.lastSeenPos, true);
				return Status::Running;
			}
			const bool arrived = w.FollowPath(c.guard, brain, GameData::GuardRunSpeed * c.slow, c.dtMs);
			if (c.visible)
			{
				// 이번 틱에 너무 많이 가서 몸이 겹쳤으면 닿는 거리까지 물러난다
				const float contact = GameData::BodyRadiusCm * 2.0f;
				const float dx = c.guard.pos.x - c.target->pos.x, dy = c.guard.pos.y - c.target->pos.y;
				const float d = std::sqrt(dx * dx + dy * dy);
				if (d < contact && d > 0.001f)
				{
					c.guard.pos.x = c.target->pos.x + dx / d * contact;
					c.guard.pos.y = c.target->pos.y + dy / d * contact;
				}
			}
			if (arrived && !c.visible)
			{
				w.StartSearch(c.guard, brain, c.guard.pos, true);
				return Status::Running;
			}
			if (!c.visible && brain.pathIndex < brain.path.size())
				w.TurnToward(c.guard, YawTo(c.guard.pos, brain.path[brain.pathIndex]), c.dtMs);
			return Status::Running;
		}));

	// ── 전투 ──
	auto fight = N::Sequence("싸움",
		N::Do("타겟 쪽으로 돌기", [](C& c) {
			if (!c.visible)
				return Status::Success;
			GameWorld& w = c.world;
			const float toTarget = YawTo(c.guard.pos, c.target->pos);
			w.TurnToward(c.guard, toTarget, c.dtMs);	// 초당 90°
			const float dist = Dist2D(c.guard.pos, c.target->pos);
			c.inCone = std::fabs(AngleDiff(toTarget, c.guard.yaw)) <= GameData::GuardAttackHalfAngleDeg &&
				dist <= GameData::GuardSightRangeCm;
			if (w._guardFeatures.evade)
			{
				// A-2: 가까운(4m) 타겟이 정면 밖이면 물러나며 돌아서기 시작·연장. 정면에 두고 4m 넘게 벌어지면 끝
				if (!c.inCone && dist <= GameData::EvadeRangeCm)
					c.brain.evadeUntilMs = w._nowMs + GameData::EvadeMs;
				else if (c.inCone && dist > GameData::EvadeRangeCm)
					c.brain.evadeUntilMs = 0;
			}
			return Status::Success;
		}),
		std::move(shoot),
		std::move(move));

	auto combat = N::Sequence("전투",
		N::If("타겟이 있음", [](C& c) { return c.brain.state == GuardState::Chase || c.brain.state == GuardState::Attack; }),
		N::Do("타겟 확인", [](C& c) {
			c.target = c.world.Find(c.brain.target);
			c.valid = c.target != nullptr && Detectable(*c.target);
			c.visible = c.valid && c.world.CanSee(c.guard, *c.target);
			if (c.visible)
				c.world.NoteTargetPos(c.brain, c.target->pos);
			else
				c.brain.lockedByAttack = false;		// 시야에서 벗어나면 고정이 풀림 (N-16)
			return Status::Success;
		}),
		N::Select("전투 행동", std::move(lost), std::move(fight)));

	// ── 발견: 순찰·탐색 중 보이면 추격 ──
	auto spot = N::Sequence("발견",
		N::If("플레이어가 보임", [](C& c) { c.seen = c.world.NearestVisibleThief(c.guard); return c.seen != nullptr; }),
		N::Do("추격 시작 + 동료 호출 (S-5)", [](C& c) {
			c.world.SetGuardState(c.guard, c.brain, GuardState::Chase, c.seen->id);
			c.world.NoteTargetPos(c.brain, c.seen->pos);
			c.world.CallAllies(c.guard, c.seen->pos);
			return Status::Running;
		}));

	// ── 탐색 ──
	auto search = N::Sequence("탐색",
		N::If("탐색 중", [](C& c) { return c.brain.state == GuardState::Search; }),
		N::Do("탐색 지점 둘러보기", [](C& c) { c.world.SearchStep(c.guard, c.brain, c.slow, c.dtMs); return Status::Running; }));

	// ── 순찰 ──
	auto patrol = N::Sequence("순찰",
		N::Do("둘러보기 마저 하고 다음 지점 고르기", [](C& c) {
			GameWorld& w = c.world;
			if (!c.brain.looking)
				return Status::Success;
			if (!w.UpdateLook(c.guard, c.brain, c.dtMs))
				return Status::Running;
			w.PickPatrolNode(c.guard, c.brain);		// P-1·P-2·P-5·P-7
			c.brain.looking = false;
			return Status::Success;
		}),
		N::Do("걸어가기, 도착하면 둘러보기", [](C& c) {
			GameWorld& w = c.world;
			GuardBrain& brain = c.brain;
			if (w._map->patrolNodes.empty())
				return Status::Running;
			if (brain.path.empty() && !w.SetGoal(c.guard, brain, w._map->patrolNodes[brain.patrolNode]))
			{
				brain.looking = true;
				w.StartSpin(brain, GameData::PatrolLookMs);
				return Status::Running;
			}
			if (w.FollowPath(c.guard, brain, GameData::GuardWalkSpeed * c.slow, c.dtMs))
			{
				brain.path.clear();
				w.ArrivePatrolNode(c.guard, brain);		// P-3 통과 · P-4 훑기 · P-6 돌아보기
			}
			else if (brain.pathIndex < brain.path.size())
			{
				w.TurnToward(c.guard, YawTo(c.guard.pos, brain.path[brain.pathIndex]), c.dtMs);
			}
			return Status::Running;
		}));

	return N::Sequence("경비원",
		N::Do("틱 준비", [](C& c) {
			ClearBits(c.guard.state, StateBit::Running | StateBit::Attacking);	// 이번 틱에 달리거나 쏘면 다시 켠다
			c.slow = (c.guard.state & StateBit::Slowed) != 0 ? GameData::StickySlow : 1.0f;
			return Status::Success;
		}),
		N::Select("할 일 (위에서부터)",
			std::move(repair), std::move(stun), std::move(wake), std::move(combat),
			std::move(spot), std::move(search), std::move(patrol)));
}

void GameWorld::SearchStep(Entity& guard, GuardBrain& brain, float slow, uint32_t dtMs)
{
	// 탐색 지점을 차례로: 걸어가서 첫 지점은 한 바퀴(S-3), 주변 지점(S-4)은 좌우 훑기
	if (!brain.looking)
	{
		const bool there = Dist2D(guard.pos, brain.searchPos) < 50.0f;
		if (there || (brain.path.empty() && !SetGoal(guard, brain, brain.searchPos)) ||
			FollowPath(guard, brain, GameData::GuardWalkSpeed * slow, dtMs))
		{
			brain.looking = true;
			brain.path.clear();
			if (brain.searchIndex == 0)
				StartSpin(brain, _guardFeatures.fullSpin ? GameData::SearchSpinMs : GameData::SearchLookMs);
			else
				StartSweep(brain, guard.yaw, GameData::SearchSweepDeg, true);	// S-4: 왼쪽 → 오른쪽 → 가운데
		}
		else if (brain.pathIndex < brain.path.size())
		{
			TurnToward(guard, YawTo(guard.pos, brain.path[brain.pathIndex]), dtMs);
		}
		return;
	}
	if (!UpdateLook(guard, brain, dtMs))
		return;
	if (++brain.searchIndex < brain.searchSpots.size())
	{
		brain.searchPos = brain.searchSpots[brain.searchIndex];
		brain.looking = false;
		brain.path.clear();
		return;
	}
	const Vec3 missed = brain.searchSpots.empty() ? guard.pos : brain.searchSpots.front();
	StartPatrol(guard, brain);		// 없다 → 가까운 순찰 지점부터 다시
	if (_guardFeatures.alertPatrol)
	{
		brain.alertUntilMs = _nowMs + GameData::AlertPatrolMs;	// P-7: 30초 동안 놓친 곳 근처를 경계 순찰
		brain.alertCenter = missed;
	}
}

// ───────────────── 사격·피격 ─────────────────

void GameWorld::GuardShoot(Entity& guard, Entity& target)
{
	// 기관총 난사: 타겟 방향 ±30° 안의 무작위 방향으로 한 발. 처음 맞은 대상(플레이어·상자) 하나에 10.
	// 그래서 타겟이 빗나가고 옆 사람이 맞을 수 있다. 벽에 막힘, 다른 경비원은 통과 (12.2, Q-07)
	std::uniform_real_distribution<float> spread(-GameData::GuardSprayHalfAngleDeg, GameData::GuardSprayHalfAngleDeg);
	const float yaw = AngleDiff(YawTo(guard.pos, target.pos) + spread(_rng), 0.0f);
	const HitResult hit = RayHit(guard, yaw, GameData::GuardSightRangeCm, true);
	_events.push_back(Msg::Fire{ guard.id, WeaponId::None, guard.pos, yaw, hit.target != nullptr ? hit.target->id : uint16_t{ 0 } });
	if (hit.target != nullptr)
		ApplyDamage(*hit.target, &guard, WeaponId::None, GameData::GuardGunDamage, 0);
}

void GameWorld::OnGuardAttacked(Entity& guard, Entity& attacker)
{
	// N-15: 타겟이 아닌 플레이어가 공격하면 즉시 그 사람으로. N-16: 공격 때문에 바뀐 타겟은 고정
	// A-2: 정면 60° 밖에서 맞으면 물러나며 돌아서기. 지금 타겟이 때려도 위치를 알고 반응한다
	GuardBrain* brain = BrainFor(guard.id);
	if (brain == nullptr || (guard.state & StateBit::Repairing) != 0)
		return;
	const bool stunned = (guard.state & StateBit::Stunned) != 0;
	const bool fromOutsideCone = std::fabs(AngleDiff(YawTo(guard.pos, attacker.pos), guard.yaw)) > GameData::GuardAttackHalfAngleDeg;
	if (brain->target == attacker.id)
	{
		if (!_guardFeatures.evade)
			return;						// 개선 전: 지금 타겟이 때리면 반응 없음
		NoteTargetPos(*brain, attacker.pos);
		if (stunned)
			return;
		if (brain->state != GuardState::Chase && brain->state != GuardState::Attack)
		{
			SetGuardState(guard, *brain, GuardState::Chase, attacker.id);
			CallAllies(guard, attacker.pos);	// S-5
		}
		if (fromOutsideCone)
			brain->evadeUntilMs = _nowMs + GameData::EvadeMs;
		return;
	}
	if (brain->lockedByAttack)
		return;			// 이미 공격자에게 고정된 타겟이 시야 안에 있음
	const bool wasCalm = brain->state == GuardState::Patrol || brain->state == GuardState::Search;
	if (stunned)
	{
		brain->target = attacker.id;	// 경직이 풀리면 그 사람부터 본다
		brain->lockedByAttack = true;
		brain->velSampleMs = 0;
		NoteTargetPos(*brain, attacker.pos);
		return;
	}
	SetGuardState(guard, *brain, GuardState::Chase, attacker.id);
	brain->lockedByAttack = true;
	NoteTargetPos(*brain, attacker.pos);
	if (_guardFeatures.evade && fromOutsideCone)
		brain->evadeUntilMs = _nowMs + GameData::EvadeMs;
	if (wasCalm)
		CallAllies(guard, attacker.pos);		// S-5
}

// ───────────────── 탐색 (S-1·S-3·S-4·S-5) ─────────────────

void GameWorld::StartSearch(Entity& guard, GuardBrain& brain, const Vec3& center, bool useFlee)
{
	// S-1: 놓친 플레이어가 움직이던 방향으로 1초 앞(최대 3m)부터. 벽에 막히면 놓친 곳 그대로
	const Vec3 vel = brain.seenVel;
	SetGuardState(guard, brain, GuardState::Search);
	Vec3 first = center;
	Vec3 flee{};
	bool hasFlee = false;
	const float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
	if (_guardFeatures.predictFlee && useFlee && speed >= GameData::PredictMinSpeedCm)
	{
		flee = { vel.x / speed, vel.y / speed, 0.0f };
		hasFlee = true;
		const float ahead = std::min(speed * GameData::PredictAheadMs / 1000.0f, GameData::PredictMaxCm);
		const Vec3 guess{ center.x + flee.x * ahead, center.y + flee.y * ahead, center.z };
		if (_map->nav.IsWalkable(guess) && LineWalkable(_map->nav, center, guess))
			first = guess;
	}
	brain.searchSpots.assign(1, first);
	brain.searchIndex = 0;
	brain.searchPos = first;
	if (_guardFeatures.extraSpots)
		AddSearchSpots(brain, first, hasFlee ? &flee : nullptr);
}

void GameWorld::AddSearchSpots(GuardBrain& brain, const Vec3& from, const Vec3* flee)
{
	// S-4: 첫 지점에서 3~7m 떨어진 걸을 수 있는 곳 중 최대 2곳. 도망 방향 쪽, 첫 지점에서 안 보이는 곳(모퉁이 너머)을 먼저.
	// 실제로 걸어서 직선 거리의 2배 안에 닿는 곳만 (경로 찾기는 최대 8번)
	struct Candidate { Vec3 pos; float score; };
	std::vector<Candidate> candidates;
	std::uniform_real_distribution<float> jitter(0.0f, 0.3f);
	const Vec3 eye{ from.x, from.y, from.z + GameData::GuardEyeCm };
	for (int a = 0; a < 12; ++a)
	{
		const float yaw = static_cast<float>(a) * 30.0f;
		for (const float dist : { GameData::SearchSpotMinCm, (GameData::SearchSpotMinCm + GameData::SearchSpotMaxCm) * 0.5f, GameData::SearchSpotMaxCm })
		{
			const Vec3 p = Ahead(from, yaw, dist);
			if (!_map->nav.IsWalkable(p) || !_map->nav.Connected(from, p))
				continue;
			float score = jitter(_rng);
			if (flee != nullptr)
			{
				const float r = yaw * Pi / 180.0f;
				score += 1.0f + std::cos(r) * flee->x + std::sin(r) * flee->y;		// 도망 방향과 같을수록 (0~2)
			}
			const Vec3 body{ p.x, p.y, p.z + GameData::ThiefBodyCm };
			if (!SeesThrough(eye, body, &_sightTests))
				score += 1.0f;															// 첫 지점에서 안 보이는 곳
			candidates.push_back({ p, score });
		}
	}
	std::sort(candidates.begin(), candidates.end(), [](const Candidate& l, const Candidate& r) { return l.score > r.score; });

	std::vector<Vec3> path;
	size_t tries = 0;
	for (const Candidate& c : candidates)
	{
		if (brain.searchSpots.size() > GameData::SearchExtraSpots || tries >= GameData::SearchPathTries)
			break;
		bool tooClose = false;
		for (const Vec3& s : brain.searchSpots)
			tooClose = tooClose || Dist2D(s, c.pos) < GameData::SearchSpotGapCm;
		if (tooClose)
			continue;
		++tries;
		if (!FindPath(_map->nav, from, c.pos, path) || path.empty())
			continue;
		float length = 0.0f;
		Vec3 prev = from;
		for (const Vec3& p : path)
		{
			length += Dist2D(prev, p);
			prev = p;
		}
		if (length <= Dist2D(from, c.pos) * GameData::SearchDetourMax)
			brain.searchSpots.push_back(c.pos);
	}
}

void GameWorld::CallAllies(const Entity& caller, const Vec3& where)
{
	// S-5: 20m 안에서 순찰·탐색 중인 동료(전투·경직·수리 중 제외)가 그 위치로 탐색하러 온다
	if (!_guardFeatures.callAllies)
		return;
	for (GuardBrain& other : _brains)
	{
		if (other.guardId == caller.id || (other.state != GuardState::Patrol && other.state != GuardState::Search))
			continue;
		Entity* ally = Find(other.guardId);
		if (ally == nullptr || (ally->state & (StateBit::Stunned | StateBit::Repairing)) != 0 ||
			Dist2D(ally->pos, caller.pos) > GameData::AllyCallRangeCm)
			continue;
		StartSearch(*ally, other, where, false);
	}
}

// ───────────────── 순찰 (P-1~P-7) ─────────────────

void GameWorld::ArrivePatrolNode(Entity& guard, GuardBrain& brain)
{
	const uint16_t node = brain.patrolNode;
	if (node < _patrolVisitMs.size())
		_patrolVisitMs[node] = _nowMs;		// P-1: 3명이 함께 쓰는 방문 기록

	// P-3: 복도 중간처럼 연결이 2개뿐인 지나가는 지점은 멈추지 않는다
	if (_guardFeatures.passThrough && node < _patrolLinks.size() && _patrolLinks[node].size() == 2)
	{
		PickPatrolNode(guard, brain);
		return;
	}

	brain.looking = true;
	if (!_guardFeatures.sweepLook)
	{
		StartSpin(brain, GameData::PatrolLookMs);		// 개선 전: 한쪽으로 1초 (90°)
	}
	else
	{
		// P-4: 좌우 45° 훑기 (2초). 문 지점이면 방 안쪽을 가운데로
		float center = guard.yaw;
		if (node < _map->patrolKinds.size() && _map->patrolKinds[node] == PatrolNodeKind::Door && node < _patrolLinks.size())
		{
			const uint16_t* room = nullptr;
			for (const uint16_t& n : _patrolLinks[node])
			{
				if (n >= _map->patrolKinds.size() || _map->patrolKinds[n] != PatrolNodeKind::Room)
					continue;
				if (room == nullptr || *room == brain.prevPatrolNode)
					room = &n;
			}
			if (room != nullptr)
				center = YawTo(_map->patrolNodes[node], _map->patrolNodes[*room]);
		}
		// 가운데로 돌아오지 않고 한쪽에서 끝낸다: 다음 지점 쪽으로 돌아서면서 등 뒤를 한 번 스친다
		// (가운데로 돌아오면 늘 앞을 보고 끝나서 뒤따라오는 플레이어를 못 봄, 경비원AI_개선_검증.md 5장)
		StartSweep(brain, center, GameData::PatrolSweepDeg, false);
	}

	// P-6: 가끔 왔던 쪽을 돌아본다 (경계 순찰 중이면 더 자주)
	if (_guardFeatures.lookBack && brain.prevPatrolNode < _map->patrolNodes.size())
	{
		const bool alert = _guardFeatures.alertPatrol && _nowMs < brain.alertUntilMs;
		std::uniform_real_distribution<float> roll(0.0f, 1.0f);
		if (roll(_rng) < (alert ? GameData::AlertLookBackChance : GameData::LookBackChance))
		{
			if (brain.lookYaws.empty())
				brain.waitUntilMs = _nowMs;		// 한 바퀴 대신 돌아보기만
			brain.lookYaws.push_back(YawTo(_map->patrolNodes[node], _map->patrolNodes[brain.prevPatrolNode]));
			brain.waitUntilMs += static_cast<uint32_t>(180.0f / GameData::GuardTurnDegPerSec * 1000.0f) + 500;
		}
	}
}

void GameWorld::PickPatrolNode(Entity& guard, GuardBrain& brain)
{
	// 다음 지점: 이어진 지점 중에서. 방금 온 곳은 막다른 길일 때만
	std::vector<uint16_t> next;
	if (brain.patrolNode < _patrolLinks.size())
		for (const uint16_t n : _patrolLinks[brain.patrolNode])
			if (n != brain.prevPatrolNode)
				next.push_back(n);
	std::erase_if(next, [&](uint16_t n) { return !_map->nav.Connected(guard.pos, _map->patrolNodes[n]); });
	if (next.empty() && brain.prevPatrolNode < _map->patrolNodes.size())
		next.push_back(brain.prevPatrolNode);
	brain.prevPatrolNode = brain.patrolNode;
	if (next.empty())
		return;

	const GuardFeatures& f = _guardFeatures;
	const bool alert = f.alertPatrol && _nowMs < brain.alertUntilMs;
	if (!f.leastVisited && !f.spreadOut && !f.importantRooms && !alert)
	{
		brain.patrolNode = next[std::uniform_int_distribution<size_t>(0, next.size() - 1)(_rng)];	// 개선 전: 완전 무작위
		return;
	}

	// 점수가 가장 높은 곳: 오래 안 간 곳(P-1) + 무작위 폭 + 중요한 방(P-5) + 경계 순찰(P-7) − 동료와 겹침(P-2)
	std::vector<int16_t> importantRooms;
	if (f.importantRooms)
	{
		for (const Entity& e : _entities)
		{
			if (e.removed) continue;
			if (e.kind == EntityKind::Safe) importantRooms.push_back(static_cast<int16_t>(e.safeRoom));
			if (e.kind == EntityKind::ReturnPoint) importantRooms.push_back(static_cast<int16_t>(e.typeId));
		}
	}
	std::uniform_real_distribution<float> jitter(0.0f, static_cast<float>(GameData::PatrolJitterMs));
	const Vec3& here = _map->patrolNodes[brain.patrolNode];
	float best = -1e30f;
	uint16_t pick = next.front();
	for (const uint16_t n : next)
	{
		const Vec3& p = _map->patrolNodes[n];
		uint32_t visited = n < _patrolVisitMs.size() ? _patrolVisitMs[n] : 0;
		float penalty = 0.0f;
		if (f.spreadOut)
		{
			for (const GuardBrain& other : _brains)
			{
				if (other.guardId == brain.guardId) continue;
				if (other.state == GuardState::Patrol && other.patrolNode == n)
					visited = _nowMs;										// 동료가 가는 곳은 방금 다녀간 것으로
				const Entity* ally = Find(other.guardId);
				if (ally != nullptr && Dist2D(ally->pos, here) <= GameData::AllySpreadCm &&
					std::fabs(AngleDiff(YawTo(here, p), YawTo(here, ally->pos))) <= GameData::AllySpreadDeg)
					penalty = static_cast<float>(GameData::AllySpreadPenaltyMs);	// 가까운 동료 쪽은 뒤로 미룸
			}
		}
		float score = (f.leastVisited ? static_cast<float>(_nowMs - std::min(visited, _nowMs)) : 0.0f) + jitter(_rng) - penalty;
		if (f.importantRooms && n < _map->patrolRooms.size() && _map->patrolRooms[n] >= 0 &&
			std::find(importantRooms.begin(), importantRooms.end(), _map->patrolRooms[n]) != importantRooms.end())
			score += static_cast<float>(GameData::ImportantRoomBonusMs);
		if (alert && Dist2D(p, brain.alertCenter) <= GameData::AlertRadiusCm)
			score += static_cast<float>(GameData::AlertBonusMs);
		if (score > best)
		{
			best = score;
			pick = n;
		}
	}
	brain.patrolNode = pick;
}

// ───────────────── 둘러보기 ─────────────────

void GameWorld::StartSpin(GuardBrain& brain, uint32_t ms)
{
	brain.lookYaws.clear();				// 비어 있으면 한쪽으로 계속 돈다 (90°/s × ms)
	brain.lookIndex = 0;
	brain.waitUntilMs = _nowMs + ms;
}

void GameWorld::StartSweep(GuardBrain& brain, float center, float halfDeg, bool backToCenter)
{
	// 왼쪽 → 오른쪽 (→ 가운데). 90°/s로 3 × halfDeg (가운데까지면 4 × halfDeg) 만큼 돈다
	brain.lookYaws = { AngleDiff(center + halfDeg, 0.0f), AngleDiff(center - halfDeg, 0.0f) };
	if (backToCenter)
		brain.lookYaws.push_back(AngleDiff(center, 0.0f));
	brain.lookIndex = 0;
	const float turnDeg = halfDeg * (backToCenter ? 4.0f : 3.0f);
	brain.waitUntilMs = _nowMs + static_cast<uint32_t>(turnDeg / GameData::GuardTurnDegPerSec * 1000.0f) + 500;
}

bool GameWorld::UpdateLook(Entity& guard, GuardBrain& brain, uint32_t dtMs)
{
	// 끝났으면 true
	if (brain.lookYaws.empty())
	{
		LookAround(guard, dtMs);
		return _nowMs >= brain.waitUntilMs;
	}
	if (brain.lookIndex < brain.lookYaws.size())
	{
		TurnToward(guard, brain.lookYaws[brain.lookIndex], dtMs);
		if (std::fabs(AngleDiff(brain.lookYaws[brain.lookIndex], guard.yaw)) < 0.5f)
			++brain.lookIndex;
	}
	return brain.lookIndex >= brain.lookYaws.size() || _nowMs >= brain.waitUntilMs;
}

void GameWorld::NoteTargetPos(GuardBrain& brain, const Vec3& pos)
{
	// 두 관측(본 위치·맞은 위치) 사이의 이동으로 속도를 짐작한다. 반씩 섞어 튀는 값을 줄임
	if (brain.velSampleMs != 0 && _nowMs > brain.velSampleMs && _nowMs - brain.velSampleMs <= GameData::TargetVelGapMs)
	{
		const float dt = static_cast<float>(_nowMs - brain.velSampleMs) / 1000.0f;
		float vx = (pos.x - brain.velSamplePos.x) / dt, vy = (pos.y - brain.velSamplePos.y) / dt;
		const float speed = std::sqrt(vx * vx + vy * vy);
		if (speed > GameData::TargetVelMaxCm)
		{
			vx *= GameData::TargetVelMaxCm / speed;
			vy *= GameData::TargetVelMaxCm / speed;
		}
		brain.seenVel = { (brain.seenVel.x + vx) * 0.5f, (brain.seenVel.y + vy) * 0.5f, 0.0f };
	}
	else if (brain.velSampleMs == 0 || _nowMs - brain.velSampleMs > GameData::TargetVelGapMs)
	{
		brain.seenVel = {};
	}
	if (_nowMs != brain.velSampleMs || brain.velSampleMs == 0)
	{
		brain.velSamplePos = pos;
		brain.velSampleMs = _nowMs;
	}
	brain.lastSeenPos = pos;
	brain.lastSeenMs = _nowMs;
}

Vec3 GameWorld::GuessTargetPos(const GuardBrain& brain) const
{
	const float sec = std::min(static_cast<float>(_nowMs - brain.lastSeenMs), 1000.0f) / 1000.0f;
	return { brain.lastSeenPos.x + brain.seenVel.x * sec, brain.lastSeenPos.y + brain.seenVel.y * sec, brain.lastSeenPos.z };
}

void GameWorld::EvadeStep(Entity& guard, const GuardBrain& brain, const Vec3& threat, float speed, uint32_t dtMs)
{
	// A-2: 타겟에게서 멀어지는 방향을, 타겟이 경비원 주위를 도는 쪽으로 45° 비튼 대각선으로 달린다.
	// 같은 쪽으로 비켜서면 타겟이 경비원 등 뒤에 붙어 있기 어렵다 (상대 각속도가 줄어듦, 개선안 2.1).
	// 막히면 반대쪽 대각선 → 바로 뒤 → 그래도 막히면 제자리에서 돌기만 한다
	const float away = YawTo(threat, guard.pos);
	const float rx = threat.x - guard.pos.x, ry = threat.y - guard.pos.y;
	const float r = std::max(std::sqrt(rx * rx + ry * ry), 1.0f);
	const float circling = (rx * brain.seenVel.y - ry * brain.seenVel.x) / r;	// 반시계(+)·시계(−)로 도는 속도
	const float side = circling >= 0.0f ? 1.0f : -1.0f;
	const float bend = GameData::EvadeDiagonalDeg;
	float tries[3] = { away - bend * side, away + bend * side, away };
	if (std::fabs(circling) < GameData::EvadeSideMinSpeed)
	{
		tries[0] = away;
		tries[1] = away - bend;
		tries[2] = away + bend;
	}
	const float step = speed * static_cast<float>(dtMs) / 1000.0f;
	for (const float yaw : tries)
	{
		const Vec3 to = Ahead(guard.pos, yaw, step);
		if (!_map->nav.IsWalkable(to) || !_map->nav.IsWalkable(Ahead(guard.pos, yaw, step + GameData::EvadeLookAheadCm)))
			continue;
		OpenDoorsOnWay(guard.pos, Ahead(guard.pos, yaw, step + GameData::GuardDoorLookAheadCm));
		guard.pos.x = to.x;
		guard.pos.y = to.y;
		guard.state |= StateBit::Running;
		return;
	}
}

// ───────────────── 도우미 ─────────────────

void GameWorld::SetGuardState(Entity& guard, GuardBrain& brain, GuardState state, uint16_t target)
{
	if (target != brain.target)
	{
		brain.lockedByAttack = false;
		brain.velSampleMs = 0;			// 다른 사람의 속도는 새로 잰다
		brain.seenVel = {};
	}
	if (state != GuardState::Chase && state != GuardState::Attack)
		brain.evadeUntilMs = 0;
	brain.state = state;
	brain.target = target;
	brain.path.clear();
	brain.pathIndex = 0;
	brain.looking = false;
	brain.repathAtMs = 0;

	// 클라에 보이는 경계 수준이 바뀔 때만 SC_GUARD_ALERT
	const AlertLevel level = AlertOf(state);
	const uint16_t alertTarget = (level == AlertLevel::Chase || level == AlertLevel::Attack) ? target : 0;
	if (level != brain.alertLevel || alertTarget != brain.alertTarget)
	{
		brain.alertLevel = level;
		brain.alertTarget = alertTarget;
		_events.push_back(Msg::GuardAlert{ guard.id, level, alertTarget });
	}
}

bool GameWorld::CanSee(const Entity& guard, const Entity& thief)
{
	// ① 거리(3D) 15m ② 시선 방향 좌우 45° ③ 눈 → 플레이어 가슴 사이에 벽이 없나 (싼 검사부터)
	const Vec3 eye{ guard.pos.x, guard.pos.y, guard.pos.z + GameData::GuardEyeCm };
	const Vec3 body{ thief.pos.x, thief.pos.y, thief.pos.z + GameData::ThiefBodyCm };
	const float dx = body.x - eye.x, dy = body.y - eye.y, dz = body.z - eye.z;
	if (dx * dx + dy * dy + dz * dz > GameData::GuardSightRangeCm * GameData::GuardSightRangeCm)
		return false;
	if (dx * dx + dy * dy > 1.0f && std::fabs(AngleDiff(YawTo(eye, body), guard.yaw)) > GameData::GuardSightHalfAngleDeg)
		return false;
	return SeesThrough(eye, body, &_sightTests);		// 벽·닫힌 문
}

Entity* GameWorld::NearestVisibleThief(Entity& guard)
{
	Entity* best = nullptr;
	float bestDist = 0.0f;
	for (Entity& e : _entities)
	{
		if (!Detectable(e))
			continue;
		const float d = Dist2D(guard.pos, e.pos);
		if ((best == nullptr || d < bestDist) && CanSee(guard, e))
		{
			best = &e;
			bestDist = d;
		}
	}
	return best;
}

bool GameWorld::SetGoal(Entity& guard, GuardBrain& brain, const Vec3& goal)
{
	PathStats stats;
	const bool found = FindPath(_map->nav, guard.pos, goal, brain.path, &stats);
	Perf::RecordPath(stats.micros, stats.expanded);
	brain.pathIndex = 0;
	brain.goal = goal;
	brain.repathAtMs = _nowMs + GameData::RepathMs;
	return found && !brain.path.empty();
}

bool GameWorld::FollowPath(Entity& guard, GuardBrain& brain, float speed, uint32_t dtMs)
{
	// 이번 틱에 갈 수 있는 거리만큼 경로의 점들을 차례로 지나간다. 몸 방향은 TurnToward가 천천히 돌린다
	float budget = speed * static_cast<float>(dtMs) / 1000.0f;

	// 이번 틱에 갈 거리 + 1m 앞까지 경로에 닫힌 문이 있으면 열고 지나간다 (클라 2026-11-24, 문은 열어 둠)
	if (!_doorOpen.empty())
	{
		float ahead = budget + GameData::GuardDoorLookAheadCm;
		Vec3 from = guard.pos;
		for (size_t i = brain.pathIndex; i < brain.path.size() && ahead > 0.0f; ++i)
		{
			const Vec3& to = brain.path[i];
			OpenDoorsOnWay(from, to);
			ahead -= Dist2D(from, to);
			from = to;
		}
	}

	while (budget > 0.0f && brain.pathIndex < brain.path.size())
	{
		const Vec3& next = brain.path[brain.pathIndex];
		const float dx = next.x - guard.pos.x, dy = next.y - guard.pos.y;
		const float length = std::sqrt(dx * dx + dy * dy);
		if (length <= budget)
		{
			guard.pos.x = next.x;
			guard.pos.y = next.y;
			budget -= length;
			++brain.pathIndex;
		}
		else
		{
			guard.pos.x += dx / length * budget;
			guard.pos.y += dy / length * budget;
			budget = 0.0f;
		}
	}
	if (speed > GameData::GuardWalkSpeed)
		guard.state |= StateBit::Running;
	return brain.pathIndex >= brain.path.size();
}

void GameWorld::TurnToward(Entity& guard, float yaw, uint32_t dtMs)
{
	const float maxStep = GameData::GuardTurnDegPerSec * static_cast<float>(dtMs) / 1000.0f;
	const float diff = AngleDiff(yaw, guard.yaw);
	const float step = std::clamp(diff, -maxStep, maxStep);
	guard.yaw = AngleDiff(guard.yaw + step, 0.0f);
}

void GameWorld::LookAround(Entity& guard, uint32_t dtMs)
{
	guard.yaw = AngleDiff(guard.yaw + GameData::GuardTurnDegPerSec * static_cast<float>(dtMs) / 1000.0f, 0.0f);
}

void GameWorld::StartPatrol(Entity& guard, GuardBrain& brain)
{
	SetGuardState(guard, brain, GuardState::Patrol);
	float best = -1.0f;
	for (size_t i = 0; i < _map->patrolNodes.size(); ++i)
	{
		if (!_map->nav.Connected(guard.pos, _map->patrolNodes[i]))
			continue;
		const float d = Dist2D(guard.pos, _map->patrolNodes[i]);
		if (best < 0.0f || d < best)
		{
			best = d;
			brain.patrolNode = static_cast<uint16_t>(i);
		}
	}
	brain.prevPatrolNode = 0xFFFF;
}

GuardBrain* GameWorld::BrainFor(uint16_t guardId)
{
	for (GuardBrain& brain : _brains)
		if (brain.guardId == guardId)
			return &brain;
	return nullptr;
}

const GuardBrain* GameWorld::BrainOf(uint16_t guardId) const
{
	for (const GuardBrain& brain : _brains)
		if (brain.guardId == guardId)
			return &brain;
	return nullptr;
}

} // namespace Content
