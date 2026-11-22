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
//   순찰 ──플레이어가 보임──→ 추격 ──공격 범위(정면 60°) 안──→ 사격(멈춰서 0.1초마다)
//    ↑                        │ ↑────────── 범위 밖으로 벗어남 ─┘
//    └── 탐색(2초 둘러봄) ←── 3초 못 봄
//   HP 0 → 자가 수리 5초(무적·정지) → 탐색 / 경직(섬광·테이저·제트팩·감전) → 풀리면 탐색
//
// 범위 (기획 그림): 탐지 범위 반지름 15m, 시야각 정면 90°(좌우 45°), 공격 범위 정면 60°(좌우 30°)
// 몸은 초당 90°로만 돈다 (N-04): 타겟이 시야 안이어도 공격 범위 밖이면 돌아서 맞춘 뒤 쏜다 → 옆·뒤로 돌면 피할 수 있다.
//
// 개선 (docs/경비원AI_공격탐색_개선안.md, 항목마다 GuardFeatures로 끌 수 있음. 기본 수치는 그대로)
//   A-2 물러나며 돌아서기: 정면 60° 밖에서 맞거나(지금 타겟 포함) 4m 안의 타겟이 정면 밖이면,
//       타겟 쪽으로 90°/s로 돌면서 대각선 뒤로 달린다. 정면에 들어오면 바로 쏘고, 정면에 둔 채 4m 넘게 벌어지거나 2초 지나면 끝
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
	ClearBits(guard.state, StateBit::Running | StateBit::Attacking);	// 이번 틱에 달리거나 쏘면 다시 켠다

	// 수리·경직: 아무것도 못 한다. 끝나면 그 자리를 둘러본다
	if ((guard.state & StateBit::Repairing) != 0)
	{
		if (brain.state != GuardState::Repairing)
			SetGuardState(guard, brain, GuardState::Repairing);
		return;
	}
	if ((guard.state & StateBit::Stunned) != 0)
	{
		if (brain.state != GuardState::Stunned)
		{
			const uint16_t keep = brain.target;
			const bool locked = brain.lockedByAttack;
			SetGuardState(guard, brain, GuardState::Stunned);
			brain.target = keep;				// 경직이 풀리면 같은 타겟을 다시 본다
			brain.lockedByAttack = locked;
		}
		return;
	}
	if (brain.state == GuardState::Repairing || brain.state == GuardState::Stunned)
	{
		const uint16_t keep = brain.target;
		Entity* old = Find(keep);
		if (old != nullptr && Detectable(*old) && CanSee(guard, *old))
		{
			const bool locked = brain.lockedByAttack;
			SetGuardState(guard, brain, GuardState::Chase, keep);
			brain.lockedByAttack = locked;
		}
		else
		{
			brain.searchPos = guard.pos;
			SetGuardState(guard, brain, GuardState::Search);
		}
	}

	const float slow = (guard.state & StateBit::Slowed) != 0 ? GameData::StickySlow : 1.0f;	// 끈끈이 위 −50%

	// ── 쫓는 중: 타겟 유지 ──
	if (brain.state == GuardState::Chase || brain.state == GuardState::Attack)
	{
		Entity* target = Find(brain.target);
		const bool valid = target != nullptr && Detectable(*target);
		const bool visible = valid && CanSee(guard, *target);
		if (visible)
			NoteTargetPos(brain, target->pos);
		else
			brain.lockedByAttack = false;		// 시야에서 벗어나면 고정이 풀림 (N-16)

		if (!valid || (!visible && _nowMs - brain.lastSeenMs > GameData::ChaseLoseMs))
		{
			// 놓침(3초) 또는 타겟이 쓰러짐·은신·연막 → 다른 사람이 보이면 그쪽, 아니면 마지막으로 본 곳 탐색
			if (Entity* other = NearestVisibleThief(guard))
			{
				SetGuardState(guard, brain, GuardState::Chase, other->id);
				NoteTargetPos(brain, other->pos);
				return;
			}
			brain.searchPos = brain.lastSeenPos;
			return SetGuardState(guard, brain, GuardState::Search);
		}

		if (visible)
		{
			const float toTarget = YawTo(guard.pos, target->pos);
			TurnToward(guard, toTarget, dtMs);	// 초당 90°
			const float dist = Dist2D(guard.pos, target->pos);
			const bool inAttackCone = std::fabs(AngleDiff(toTarget, guard.yaw)) <= GameData::GuardAttackHalfAngleDeg &&
				dist <= GameData::GuardSightRangeCm;
			if (_guardFeatures.evade)
			{
				// A-2: 가까운(4m) 타겟이 정면 밖이면 물러나며 돌아서기 시작·연장. 정면에 두고 4m 넘게 벌어지면 끝
				if (!inAttackCone && dist <= GameData::EvadeRangeCm)
					brain.evadeUntilMs = _nowMs + GameData::EvadeMs;
				else if (inAttackCone && dist > GameData::EvadeRangeCm)
					brain.evadeUntilMs = 0;
			}
			if (inAttackCone)
			{
				// 공격 범위 안: 0.1초마다 단일 사격하고, 멈추지 않고 아래에서 계속 쫓아간다 (12.2 이동, A-18)
				if (brain.state != GuardState::Attack)
				{
					const bool locked = brain.lockedByAttack;
					SetGuardState(guard, brain, GuardState::Attack, target->id);
					brain.lockedByAttack = locked;
				}
				guard.state |= StateBit::Attacking;
				if (_nowMs >= guard.nextGunMs)
				{
					guard.nextGunMs = _nowMs + GameData::GuardGunEveryMs;
					GuardShoot(guard, *target);
				}
			}
			else if (brain.state != GuardState::Chase)
			{
				const bool locked = brain.lockedByAttack;
				SetGuardState(guard, brain, GuardState::Chase, target->id);
				brain.lockedByAttack = locked;
			}
		}

		// A-2 물러나며 돌아서기: 다가가는 대신 타겟(안 보이면 짐작한 위치) 쪽으로 돌면서 대각선 뒤로 달린다.
		// 정면 60°에 들어오면 위에서 이미 쐈다. 경로를 따르지 않으므로 끝나면 경로를 새로 찾는다
		if (_guardFeatures.evade && _nowMs < brain.evadeUntilMs)
		{
			const Vec3 threat = visible ? target->pos : GuessTargetPos(brain);
			if (!visible)
				TurnToward(guard, YawTo(guard.pos, threat), dtMs);
			EvadeStep(guard, brain, threat, GameData::GuardRunSpeed * slow, dtMs);
			brain.path.clear();
			return;
		}

		// 쏘는 중이든 아니든: 보이면 지금 위치로, 안 보이면 마지막으로 본 곳으로 달린다 (경로는 0.5초마다)
		if (visible && Dist2D(guard.pos, target->pos) <= GameData::BodyRadiusCm * 2.0f)
			return;		// 몸이 닿았으면 더 다가가지 않음 (겹치지 않게)
		const Vec3 goal = visible ? target->pos : brain.lastSeenPos;
		if ((brain.path.empty() || _nowMs >= brain.repathAtMs) && !SetGoal(guard, brain, goal))
		{
			brain.searchPos = brain.lastSeenPos;
			return SetGuardState(guard, brain, GuardState::Search);	// 갈 수 없는 곳
		}
		const bool arrived = FollowPath(guard, brain, GameData::GuardRunSpeed * slow, dtMs);
		if (visible)
		{
			// 이번 틱에 너무 많이 가서 몸이 겹쳤으면 닿는 거리까지 물러난다
			const float contact = GameData::BodyRadiusCm * 2.0f;
			const float dx = guard.pos.x - target->pos.x, dy = guard.pos.y - target->pos.y;
			const float d = std::sqrt(dx * dx + dy * dy);
			if (d < contact && d > 0.001f)
			{
				guard.pos.x = target->pos.x + dx / d * contact;
				guard.pos.y = target->pos.y + dy / d * contact;
			}
		}
		if (arrived && !visible)
		{
			brain.searchPos = guard.pos;
			SetGuardState(guard, brain, GuardState::Search);		// 마지막으로 본 곳에 왔는데 없음
		}
		if (!visible && brain.pathIndex < brain.path.size())
			TurnToward(guard, YawTo(guard.pos, brain.path[brain.pathIndex]), dtMs);
		return;
	}

	// ── 순찰·탐색: 보이면 추격 ──
	if (Entity* seen = NearestVisibleThief(guard))
	{
		SetGuardState(guard, brain, GuardState::Chase, seen->id);
		NoteTargetPos(brain, seen->pos);
		return;
	}

	if (brain.state == GuardState::Search)
	{
		if (!brain.looking)
		{
			const bool there = Dist2D(guard.pos, brain.searchPos) < 50.0f;
			if (there || (brain.path.empty() && !SetGoal(guard, brain, brain.searchPos)) ||
				FollowPath(guard, brain, GameData::GuardWalkSpeed * slow, dtMs))
			{
				brain.looking = true;
				brain.waitUntilMs = _nowMs + GameData::SearchLookMs;
				brain.path.clear();
			}
			else if (brain.pathIndex < brain.path.size())
			{
				TurnToward(guard, YawTo(guard.pos, brain.path[brain.pathIndex]), dtMs);
			}
			return;
		}
		LookAround(guard, dtMs);
		if (_nowMs >= brain.waitUntilMs)
			StartPatrol(guard, brain);		// 없다 → 가까운 순찰 지점부터 다시
		return;
	}

	// 순찰
	if (brain.looking)
	{
		LookAround(guard, dtMs);
		if (_nowMs < brain.waitUntilMs)
			return;
		// 갈림길에서 다음 지점: 이어진 지점 중 무작위, 방금 온 곳은 막다른 길일 때만
		std::vector<uint16_t> next;
		for (const MapPatrolEdge& edge : _map->patrolEdges)
		{
			if (edge.a == brain.patrolNode && edge.b != brain.prevPatrolNode) next.push_back(edge.b);
			if (edge.b == brain.patrolNode && edge.a != brain.prevPatrolNode) next.push_back(edge.a);
		}
		std::erase_if(next, [&](uint16_t n) { return !_map->nav.Connected(guard.pos, _map->patrolNodes[n]); });
		if (next.empty() && brain.prevPatrolNode < _map->patrolNodes.size())
			next.push_back(brain.prevPatrolNode);
		brain.prevPatrolNode = brain.patrolNode;
		if (!next.empty())
			brain.patrolNode = next[std::uniform_int_distribution<size_t>(0, next.size() - 1)(_rng)];
		brain.looking = false;
	}
	if (_map->patrolNodes.empty())
		return;
	if (brain.path.empty() && !SetGoal(guard, brain, _map->patrolNodes[brain.patrolNode]))
	{
		brain.looking = true;
		brain.waitUntilMs = _nowMs + GameData::PatrolLookMs;
		return;
	}
	if (FollowPath(guard, brain, GameData::GuardWalkSpeed * slow, dtMs))
	{
		brain.looking = true;
		brain.waitUntilMs = _nowMs + GameData::PatrolLookMs;
		brain.path.clear();
	}
	else if (brain.pathIndex < brain.path.size())
	{
		TurnToward(guard, YawTo(guard.pos, brain.path[brain.pathIndex]), dtMs);
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
			SetGuardState(guard, *brain, GuardState::Chase, attacker.id);
		if (fromOutsideCone)
			brain->evadeUntilMs = _nowMs + GameData::EvadeMs;
		return;
	}
	if (brain->lockedByAttack)
		return;			// 이미 공격자에게 고정된 타겟이 시야 안에 있음
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
	return _map == nullptr || HasLineOfSight(*_map, eye, body, &_sightTests);
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
