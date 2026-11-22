#include "ServerCore/CorePch.h"

#include <cmath>
#include <cstdio>
#include <deque>
#include <random>
#include <set>

#include "Content/Game/PathFinder.h"
#include "Content/Game/Sight.h"
#include "Content/Game/WorldMath.h"
#include "TestFramework.h"
#include "WorldTestUtil.h"

// 경비원 AI 개선 효과 측정 (docs/경비원AI_개선_검증.md).
// 같은 상황을 "개선 전"(GuardFeatures::None)과 "개선 후"(GuardFeatures::All)로 돌려 숫자를 비교한다.
// 플레이어는 정해진 규칙으로 움직이는 봇이고, 시간은 서버 틱(50ms)으로 흘린다. 씨앗이 고정이라 매번 같은 결과가 나온다.
//
//   ContentTests.exe Bench          ← 측정만 따로 돌리기 (결과는 [측정] 줄)
//
// Bench_ 시험은 오래 걸리므로 이름으로 고를 때만 돈다 (TestFramework.cpp).

using namespace WorldTest;
using namespace Content::WorldMath;

namespace {

constexpr float TickStepCm = 600.0f * 0.05f;		// 플레이어 달리기 6m/s의 한 틱 거리
constexpr float TrailCm = 500.0f;					// ⑤ 경비원 뒤를 따라가는 거리

struct Mode
{
	const char* name;
	GuardFeatures features;
};
const Mode Modes[] = { { "개선 전", GuardFeatures::None() }, { "개선 후", GuardFeatures::All() } };

template <class... Args>
void Report(std::format_string<Args...> fmt, Args&&... args)
{
	std::printf("    [측정] %s\n", std::format(fmt, std::forward<Args>(args)...).c_str());
}

GameWorld BenchWorld(int players, uint32_t seed, const GuardFeatures& features)
{
	GameWorld world = MakeWorld(players, true, {}, seed);
	world.SetGuardFeatures(features);
	return world;
}

// 봇 이동: goal 쪽으로 최대 step만큼. 벽이면 X·Y 한쪽으로만 미끄러진다. 바라보는 방향은 face
void MoveBot(GameWorld& world, uint64_t player, const Vec3& goal, float step, float face)
{
	const NavGrid& nav = DefaultMap().nav;
	const Vec3 from = world.ThiefOf(player)->pos;
	const float dx = goal.x - from.x, dy = goal.y - from.y;
	const float d = std::sqrt(dx * dx + dy * dy);
	Vec3 to = d <= step ? Vec3{ goal.x, goal.y, from.z } : Vec3{ from.x + dx / d * step, from.y + dy / d * step, from.z };
	if (!nav.IsWalkable(to))
	{
		const Vec3 onlyX{ to.x, from.y, from.z }, onlyY{ from.x, to.y, from.z };
		to = nav.IsWalkable(onlyX) ? onlyX : nav.IsWalkable(onlyY) ? onlyY : from;
	}
	Teleport(world, player, to, face, static_cast<uint8_t>(MoveFlag::Moving | MoveFlag::Running));
}

bool Sees(const Vec3& eyeFeet, const Vec3& bodyFeet)
{
	const Vec3 eye{ eyeFeet.x, eyeFeet.y, eyeFeet.z + GameData::GuardEyeCm };
	const Vec3 body{ bodyFeet.x, bodyFeet.y, bodyFeet.z + GameData::ThiefBodyCm };
	return HasLineOfSight(DefaultMap(), eye, body);
}

bool Chasing(const GameWorld& world, uint16_t guard, uint16_t target)
{
	const GuardBrain* brain = world.BrainOf(guard);
	return (brain->state == GuardState::Chase || brain->state == GuardState::Attack) && brain->target == target;
}

Vec3 RandomAround(std::mt19937& rng, const Vec3& center, float minCm, float maxCm)
{
	std::uniform_real_distribution<float> angle(-180.0f, 180.0f), dist(minCm, maxCm);
	return Ahead(center, angle(rng), dist(rng));
}

// ─────────────── ① 등 뒤 돌며 때리기 (A-2) ───────────────
// 삼단봉(사거리 2m)을 든 봇이 경비원 등 뒤 1.5m를 달리기 속도(6m/s)로 계속 따라 돌며 때린다 (최적으로 움직이는 플레이어).
// 경비원이 봇에게 준 대미지와, 봇이 경비원 공격 범위(정면 60°) 안에 있던 시간 비율을 잰다.

struct CircleResult
{
	double damageToPlayer = 0;	// 경비원 기관총이 봇에게 준 대미지
	double hitsOnGuard = 0;		// 봇의 삼단봉이 경비원을 때린 횟수
	double inConePct = 0;		// 봇이 경비원 정면 60° 안에 있던 시간 비율
};

CircleResult CircleTrial(const GuardFeatures& features, uint32_t seed, float startDeg)
{
	const Vec3 arena{ 400.0f, -3400.0f, 0.0f };		// Gallery1 서쪽 가운데: 사방으로 4m 넘게 트임
	GameWorld world = BenchWorld(1, seed, features);
	ClearArena(world);
	const uint16_t guard = PlaceGuard(world, arena, 90.0f);
	const uint16_t me = world.ThiefOf(A)->id;
	world.ThiefOf(A)->hp = 1000000;
	Give(world, A, WeaponId::Baton);
	Teleport(world, A, Ahead(arena, 270.0f + startDeg, 150.0f), YawTo(Ahead(arena, 270.0f + startDeg, 150.0f), arena));
	Act(world, A, ActionType::AttackStart, 0, world.ThiefOf(A)->yaw);
	world.TakeEvents();

	CircleResult r;
	int inCone = 0;
	constexpr int Ticks = 300;		// 15초
	for (int t = 0; t < Ticks; ++t)
	{
		const Entity& g = *world.Find(guard);
		const Vec3 behind = Ahead(g.pos, g.yaw + 180.0f, 150.0f);
		MoveBot(world, A, behind, TickStepCm, 0.0f);
		Teleport(world, A, world.ThiefOf(A)->pos, YawTo(world.ThiefOf(A)->pos, world.Find(guard)->pos), static_cast<uint8_t>(MoveFlag::Moving | MoveFlag::Running));
		world.Update(0.05f);
		for (const WorldEvent& e : world.TakeEvents())
		{
			if (const auto* dmg = std::get_if<Msg::Damage>(&e))
			{
				if (dmg->target == me && dmg->attacker == guard) r.damageToPlayer += dmg->amount;
				if (dmg->target == guard && dmg->attacker == me) r.hitsOnGuard += 1;
			}
		}
		const Entity& g2 = *world.Find(guard);
		if (std::fabs(AngleDiff(YawTo(g2.pos, world.ThiefOf(A)->pos), g2.yaw)) <= GameData::GuardAttackHalfAngleDeg)
			++inCone;
	}
	r.inConePct = 100.0 * inCone / Ticks;
	return r;
}

// ─────────────── ② 쫓기다 숨기 (S-1·S-3·S-4) ───────────────
// 경비원이 앞에서 봇을 발견 → 봇이 달려서(6m/s) 길을 따라 14m 안쪽의, 경비원 자리와 처음 자리에서 안 보이는
// 곳(모퉁이 너머·옆 방)으로 가서 멈춘다. 경비원이 한 번 놓쳐서 탐색을 시작한 시도만 세고, 그중 25초 안에 다시 찾은 비율을 잰다.

struct HideCase
{
	Vec3 guard, start, hide;
	std::vector<Vec3> route;	// start → hide 걸어갈 길
};

std::vector<HideCase> HideCases(size_t count, uint32_t seed, float guardMin, float guardMax)
{
	const MapData& map = DefaultMap();
	std::mt19937 rng(seed);
	std::vector<HideCase> cases;
	std::uniform_int_distribution<size_t> pick(0, map.patrolNodes.size() - 1);
	for (int attempt = 0; attempt < 50000 && cases.size() < count; ++attempt)
	{
		const Vec3 start = map.patrolNodes[pick(rng)];
		const Vec3 hide = RandomAround(rng, start, 400.0f, 900.0f);
		const Vec3 guard = RandomAround(rng, start, guardMin, guardMax);
		if (!map.nav.IsWalkable(hide) || !map.nav.IsWalkable(guard) || !LineWalkable(map.nav, guard, start) ||
			!Sees(guard, start) || Sees(guard, hide) || Sees(start, hide))
			continue;
		std::vector<Vec3> route;
		if (!FindPath(map.nav, start, hide, route) || route.empty())
			continue;
		float length = 0.0f;
		Vec3 prev = start;
		for (const Vec3& p : route)
		{
			length += Dist2D(prev, p);
			prev = p;
		}
		if (length > 1400.0f)
			continue;
		cases.push_back({ guard, start, hide, route });
	}
	return cases;
}

struct SearchResult
{
	int tried = 0;			// 경비원이 놓치고 탐색을 시작한 시도
	int found = 0;			// 그중 다시 찾음
	double foundSec = 0;	// 다시 찾은 시도의 평균 걸린 시간 (봇이 숨은 뒤부터)
};

// 봇이 숨은 뒤 maxSec 안에 경비원이 다시 보면(추격) 찾은 것
void RunHide(GameWorld& world, uint16_t guard, const std::vector<Vec3>& route, uint32_t maxSec, SearchResult& r)
{
	const uint16_t me = world.ThiefOf(A)->id;
	bool hidden = false, searched = false;
	int hiddenTick = 0;
	size_t next = 0;
	for (int t = 0; t < static_cast<int>(maxSec) * 20 + 200; ++t)
	{
		if (!hidden)
		{
			// 길을 따라 한 틱 거리만큼 (꺾이는 점을 넘으면 남은 거리로 다음 점 쪽으로)
			float budget = TickStepCm;
			Vec3 pos = world.ThiefOf(A)->pos;
			while (budget > 0.0f && next < route.size())
			{
				const float d = Dist2D(pos, route[next]);
				if (d <= budget)
				{
					pos = { route[next].x, route[next].y, pos.z };
					budget -= d;
					++next;
				}
				else
				{
					pos = { pos.x + (route[next].x - pos.x) / d * budget, pos.y + (route[next].y - pos.y) / d * budget, pos.z };
					budget = 0.0f;
				}
			}
			const float face = next < route.size() ? YawTo(pos, route[next]) : world.ThiefOf(A)->yaw;
			Teleport(world, A, pos, face, static_cast<uint8_t>(MoveFlag::Moving | MoveFlag::Running));
			if (next >= route.size())
			{
				hidden = true;
				hiddenTick = t;
			}
		}
		world.Update(0.05f);
		world.TakeEvents();
		if (world.BrainOf(guard)->state == GuardState::Search)
			searched = true;
		if (hidden && searched && Chasing(world, guard, me))
		{
			++r.tried;
			++r.found;
			r.foundSec += (t - hiddenTick) * 0.05;
			return;
		}
		if (hidden && t - hiddenTick > static_cast<int>(maxSec) * 20)
			break;
	}
	if (searched)
		++r.tried;
}

SearchResult HideTrial(const GuardFeatures& features, const std::vector<HideCase>& cases)
{
	SearchResult r;
	uint32_t seed = 500;
	for (const HideCase& c : cases)
	{
		GameWorld world = BenchWorld(1, ++seed, features);
		ClearArena(world);
		const uint16_t guard = PlaceGuard(world, c.guard, YawTo(c.guard, c.start));
		world.ThiefOf(A)->hp = 1000000;
		Teleport(world, A, c.start, YawTo(c.start, c.guard));
		Run(world, 500);										// 발견됨
		world.TakeEvents();
		RunHide(world, guard, c.route, 25, r);
	}
	return r;
}

// ─────────────── ③ 테이저로 경직시키고 도망 (S-6, 탐색 전체) ───────────────
// 2m 앞에서 테이저(경직 2초)를 맞히고 경비원 눈에 안 보이는 곳으로 달려 숨는다. 경직이 풀린 뒤 20초 안에 다시 찾은 비율.

SearchResult StunTrial(const GuardFeatures& features, const std::vector<HideCase>& cases)
{
	SearchResult r;
	uint32_t seed = 900;
	for (const HideCase& c : cases)
	{
		GameWorld world = BenchWorld(1, ++seed, features);
		ClearArena(world);
		const uint16_t guard = PlaceGuard(world, c.guard, YawTo(c.guard, c.start));
		world.ThiefOf(A)->hp = 1000000;
		Teleport(world, A, c.start, YawTo(c.start, c.guard));
		Give(world, A, WeaponId::Taser);
		Attack(world, A, YawTo(c.start, c.guard));			// 경직 2초 (N-08)
		world.TakeEvents();
		if ((world.Find(guard)->state & StateBit::Stunned) == 0)
			continue;
		SearchResult one;
		RunHide(world, guard, c.route, 20, one);
		++r.tried;			// 경직 뒤에는 늘 놓친 상태에서 시작하므로 모두 센다
		r.found += one.found;
		r.foundSec += one.foundSec;
	}
	return r;
}

// ─────────────── ④ 순찰 범위 (P-1~P-5) ───────────────
// 경비원 3명이 5분 동안 순찰만 한다 (플레이어는 맵 밖). 들른 순찰 지점·방의 비율과 멈춰 있던 시간 비율.

struct PatrolResult
{
	double nodesPct = 0, roomsPct = 0, worstRoomsPct = 100, stoppedPct = 0;
};

PatrolResult PatrolTrial(const GuardFeatures& features, int seeds)
{
	const MapData& map = DefaultMap();
	std::set<int> patrolRooms;				// 순찰 지점이 있는 방 (방 안 지점 기준)
	for (size_t i = 0; i < map.patrolNodes.size() && i < map.patrolKinds.size(); ++i)
		if (map.patrolKinds[i] == PatrolNodeKind::Room && map.patrolRooms[i] >= 0)
			patrolRooms.insert(map.patrolRooms[i]);

	PatrolResult r;
	for (int s = 0; s < seeds; ++s)
	{
		GameWorld world = BenchWorld(1, 2000 + static_cast<uint32_t>(s), features);
		Teleport(world, A, Parking);
		std::vector<uint16_t> guards;
		for (size_t n = 0; n < 3; ++n)
			guards.push_back(Nth(world, EntityKind::Guard, n));
		std::vector<Vec3> last;
		for (uint16_t g : guards)
			last.push_back(world.Find(g)->pos);
		std::vector<bool> node(map.patrolNodes.size(), false);
		std::set<int> rooms;
		long stopped = 0, total = 0;
		for (int t = 0; t < 6000; ++t)		// 5분
		{
			world.Update(0.05f);
			world.TakeEvents();
			for (size_t i = 0; i < guards.size(); ++i)
			{
				const Vec3 p = world.Find(guards[i])->pos;
				if (Dist2D(p, last[i]) < 1.0f) ++stopped;
				++total;
				last[i] = p;
				for (size_t n = 0; n < map.patrolNodes.size(); ++n)
					if (!node[n] && Dist2D(p, map.patrolNodes[n]) < 100.0f) node[n] = true;
				const int room = RoomOf(map, p);
				if (patrolRooms.contains(room)) rooms.insert(room);
			}
		}
		const double nodesPct = 100.0 * std::count(node.begin(), node.end(), true) / node.size();
		const double roomsPct = 100.0 * rooms.size() / patrolRooms.size();
		r.nodesPct += nodesPct / seeds;
		r.roomsPct += roomsPct / seeds;
		r.worstRoomsPct = std::min(r.worstRoomsPct, roomsPct);
		r.stoppedPct += 100.0 * stopped / total / seeds;
	}
	return r;
}

// ─────────────── ⑤ 순찰 경비원 뒤따라가기 (P-6) ───────────────
// 봇이 순찰하는 경비원이 지나간 길을 5m 뒤에서 그대로 따라간다. 60초 안에 들킨 비율과 들키기까지 걸린 시간.

struct TrailResult
{
	int tried = 0, caught = 0;
	double caughtSec = 0;
};

TrailResult TrailTrial(const GuardFeatures& features, int trials)
{
	TrailResult r;
	for (int s = 0; s < trials; ++s)
	{
		GameWorld world = BenchWorld(1, 3000 + static_cast<uint32_t>(s), features);
		const size_t lead = static_cast<size_t>(s) % 3;
		for (size_t n = 0; n < 3; ++n)
			if (n != lead) world.Find(Nth(world, EntityKind::Guard, n))->pos = Parking;
		const uint16_t guard = Nth(world, EntityKind::Guard, lead);
		const uint16_t me = world.ThiefOf(A)->id;
		world.ThiefOf(A)->hp = 1000000;
		Teleport(world, A, Parking);
		std::deque<Vec3> trail;
		bool following = false;
		++r.tried;
		for (int t = 0; t < 1200; ++t)		// 60초
		{
			const Vec3 g = world.Find(guard)->pos;
			trail.push_back(g);
			// 경비원에게서 5m 넘게 떨어진, 가장 최근에 경비원이 지나간 자리
			const Vec3* spot = nullptr;
			for (auto it = trail.rbegin(); it != trail.rend(); ++it)
				if (Dist2D(*it, g) >= TrailCm) { spot = &*it; break; }
			if (spot != nullptr)
			{
				following = true;
				Teleport(world, A, *spot, YawTo(*spot, g), MoveFlag::Moving);
			}
			while (trail.size() > 400) trail.pop_front();
			world.Update(0.05f);
			world.TakeEvents();
			if (following && Chasing(world, guard, me))
			{
				++r.caught;
				r.caughtSec += t * 0.05;
				break;
			}
		}
	}
	return r;
}

// ─────────────── ⑥ 동료 호출 (S-5) ───────────────
// 경비원 하나가 플레이어를 발견했을 때, 그 플레이어를 직접 볼 수 없는 10~19m 거리의 다른 경비원이 20초 안에 합류(플레이어를 봄)한 비율.

struct CallResult
{
	int tried = 0, joined = 0;
	double joinedSec = 0;
};

CallResult CallTrial(const GuardFeatures& features)
{
	const MapData& map = DefaultMap();
	const Vec3 spotter{ 900.0f, -4200.0f, 0.0f }, player{ 900.0f, -3400.0f, 0.0f };	// Gallery1
	CallResult r;
	uint32_t seed = 4000;
	for (size_t n = 0; n < map.patrolNodes.size(); ++n)
	{
		const Vec3 helper = map.patrolNodes[n];
		const float d = Dist2D(helper, player);
		if (d < 1000.0f || d > 1900.0f || !map.nav.Connected(helper, player) || Sees(helper, player))
			continue;
		GameWorld world = BenchWorld(1, ++seed, features);
		ClearArena(world);
		PlaceGuard(world, spotter, 90.0f, 0);
		const uint16_t other = PlaceGuard(world, helper, 0.0f, 1);
		world.ThiefOf(A)->hp = 1000000;
		Teleport(world, A, player, -90.0f);
		++r.tried;
		for (int t = 0; t < 400; ++t)		// 20초
		{
			world.Update(0.05f);
			world.TakeEvents();
			if (Chasing(world, other, world.ThiefOf(A)->id))
			{
				++r.joined;
				r.joinedSec += t * 0.05;
				break;
			}
		}
	}
	return r;
}

double Pct(int part, int whole) { return whole == 0 ? 0.0 : 100.0 * part / whole; }
double Avg(double sum, int count) { return count == 0 ? 0.0 : sum / count; }

} // namespace

TEST_CASE(Bench_CircleBehind_A2)
{
	for (const Mode& mode : Modes)
	{
		CircleResult sum;
		constexpr int Trials = 8;
		for (int i = 0; i < Trials; ++i)
		{
			const CircleResult one = CircleTrial(mode.features, 100 + static_cast<uint32_t>(i), 45.0f * static_cast<float>(i));
			sum.damageToPlayer += one.damageToPlayer / Trials;
			sum.hitsOnGuard += one.hitsOnGuard / Trials;
			sum.inConePct += one.inConePct / Trials;
		}
		Report("① 등 뒤 돌며 때리기 {} | 15초 동안 경비원이 준 대미지 {:.0f}, 봇의 타격 {:.1f}회, 봇이 공격 범위 안에 있던 시간 {:.1f}%",
			mode.name, sum.damageToPlayer, sum.hitsOnGuard, sum.inConePct);
	}
}

TEST_CASE(Bench_HideAfterChase_Search)
{
	const auto cases = HideCases(40, 77, 500.0f, 800.0f);
	CHECK(cases.size() >= 30);
	for (const Mode& mode : Modes)
	{
		const SearchResult r = HideTrial(mode.features, cases);
		Report("② 쫓기다 숨기 {} | 탐색까지 간 시도 {}회 중 다시 찾음 {}회 ({:.0f}%), 찾는 데 평균 {:.1f}초",
			mode.name, r.tried, r.found, Pct(r.found, r.tried), Avg(r.foundSec, r.found));
	}
}

TEST_CASE(Bench_StunThenHide_S6)
{
	const auto cases = HideCases(40, 99, 180.0f, 200.0f);
	CHECK(cases.size() >= 30);
	for (const Mode& mode : Modes)
	{
		const SearchResult r = StunTrial(mode.features, cases);
		Report("③ 경직시키고 숨기 {} | {}회 중 다시 찾음 {}회 ({:.0f}%), 찾는 데 평균 {:.1f}초",
			mode.name, r.tried, r.found, Pct(r.found, r.tried), Avg(r.foundSec, r.found));
	}
}

TEST_CASE(Bench_PatrolCoverage_P)
{
	for (const Mode& mode : Modes)
	{
		const PatrolResult r = PatrolTrial(mode.features, 6);
		Report("④ 5분 순찰 {} | 들른 순찰 지점 {:.0f}%, 들른 방 {:.0f}% (가장 적을 때 {:.0f}%), 멈춰 있던 시간 {:.0f}%",
			mode.name, r.nodesPct, r.roomsPct, r.worstRoomsPct, r.stoppedPct);
	}
}

TEST_CASE(Bench_TrailBehindGuard_P6)
{
	for (const Mode& mode : Modes)
	{
		const TrailResult r = TrailTrial(mode.features, 12);
		Report("⑤ 경비원 뒤따라가기 {} | {}회 중 60초 안에 들킴 {}회 ({:.0f}%), 들키기까지 평균 {:.1f}초",
			mode.name, r.tried, r.caught, Pct(r.caught, r.tried), Avg(r.caughtSec, r.caught));
	}
}

TEST_CASE(Bench_AllyCall_S5)
{
	for (const Mode& mode : Modes)
	{
		const CallResult r = CallTrial(mode.features);
		Report("⑥ 동료 호출 {} | 10~19m 떨어진 동료 {}명 중 20초 안에 합류 {}명 ({:.0f}%), 합류까지 평균 {:.1f}초",
			mode.name, r.tried, r.joined, Pct(r.joined, r.tried), Avg(r.joinedSec, r.joined));
	}
}
