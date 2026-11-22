#include "ServerCore/CorePch.h"

#include <cmath>

#include "Content/Game/PathFinder.h"
#include "Content/Game/Sight.h"
#include "Content/Game/WorldMath.h"
#include "TestFramework.h"
#include "WorldTestUtil.h"

// 경비원 AI (psd.md M3, 명세 §12): 경로(A*), 시야(3D), 상태 전환, 사격, 느린 회전, 타겟 규칙.
// 실제 맵을 쓴다. 북쪽 전시실 Gallery1은 낮은 벤치(높이 44) 말고는 트인 곳이라 "보이는" 시험을 여기서 한다.

using namespace WorldTest;

namespace {

size_t ShotsBy(const std::vector<WorldEvent>& events, uint16_t shooter)
{
	return static_cast<size_t>(std::count_if(events.begin(), events.end(), [shooter](const WorldEvent& e) {
		const auto* fire = std::get_if<Msg::Fire>(&e);
		return fire != nullptr && fire->shooter == shooter;
	}));
}


const Vec3 GuardSpot{ 900.0f, -4200.0f, 0.0f };		// Gallery1 남쪽 가운데
const Vec3 FrontOfGuard{ 900.0f, -3400.0f, 0.0f };	// GuardSpot에서 +Y로 8m (yaw 90이면 정면, 두 벤치 사이)
const Vec3 FarSpot{ 900.0f, -2200.0f, 0.0f };		// GuardSpot에서 +Y로 20m (탐지 범위 15m 밖, 복도)

// GuardSpot에서 yaw 90(+Y)을 기준으로 deg만큼 돌아간 방향, 8m
Vec3 AtAngle(float deg)
{
	const float r = (90.0f + deg) * 3.14159265f / 180.0f;
	return { GuardSpot.x + std::cos(r) * 800.0f, GuardSpot.y + std::sin(r) * 800.0f, 0.0f };
}

// AI를 켠 세계. 경비원 0번만 GuardSpot에 +Y를 보고 서 있고 나머지·상자는 치운다
GameWorld GuardArena(int players = 1)
{
	GameWorld world = MakeWorld(players, true);
	ClearArena(world);
	for (int i = 0; i < players; ++i)
		Teleport(world, A + static_cast<uint64_t>(i), FarAway);
	PlaceGuard(world, GuardSpot, 90.0f);
	world.TakeEvents();
	return world;
}

GuardState StateOf(const GameWorld& world, uint16_t guard) { return world.BrainOf(guard)->state; }

template <class T, class Pred>
bool Has(const std::vector<WorldEvent>& events, Pred pred)
{
	for (const WorldEvent& e : events)
		if (const T* m = std::get_if<T>(&e); m != nullptr && pred(*m))
			return true;
	return false;
}

} // namespace

// ───────────────── 경로 (A*) ─────────────────

TEST_CASE(Path_GoesAroundWalls_EverySegmentWalkable)
{
	const MapData& map = DefaultMap();
	const Vec3 from = map.guardSpawns[0].pos;		// 서쪽 경비실
	const Vec3 to = FarAway;						// 동쪽 객실 GuestRoom7
	std::vector<Vec3> path;
	PathStats stats;
	CHECK(FindPath(map.nav, from, to, path, &stats));
	CHECK(!path.empty() && stats.expanded > 0);

	// 다듬어진 경로의 각 구간은 직선으로 걸을 수 있어야 하고(벽을 뚫지 않음), 끝은 목적지
	Vec3 prev = from;
	float length = 0.0f;
	for (const Vec3& p : path)
	{
		CHECK(LineWalkable(map.nav, prev, p));
		length += std::sqrt((p.x - prev.x) * (p.x - prev.x) + (p.y - prev.y) * (p.y - prev.y));
		prev = p;
	}
	CHECK(std::fabs(path.back().x - to.x) < 1.0f && std::fabs(path.back().y - to.y) < 1.0f);
	const float straight = std::sqrt((to.x - from.x) * (to.x - from.x) + (to.y - from.y) * (to.y - from.y));
	CHECK(length >= straight && length < straight * 3.0f);		// 돌아가지만 터무니없이 길지는 않다
	CHECK(path.size() < 40);									// 다듬기: 칸마다 점이 생기지 않는다
}

TEST_CASE(Path_SameResultTwice_ScratchReused)
{
	// 스레드 버퍼를 "세대 번호"로 재사용해도 지난 탐색의 값이 섞이지 않아야 한다
	const MapData& map = DefaultMap();
	std::vector<Vec3> first, second, other;
	CHECK(FindPath(map.nav, map.guardSpawns[0].pos, FarAway, first));
	CHECK(FindPath(map.nav, map.guardSpawns[2].pos, map.thiefSpawns[0].pos, other));
	CHECK(FindPath(map.nav, map.guardSpawns[0].pos, FarAway, second));
	CHECK_EQ(first.size(), second.size());
	for (size_t i = 0; i < first.size() && i < second.size(); ++i)
		CHECK(first[i].x == second[i].x && first[i].y == second[i].y);
}

TEST_CASE(Path_SeparatedRegions_FailFast)
{
	// 가운데 세로 벽으로 나뉜 작은 격자: 구역 번호가 있으면 탐색 없이 실패, 없으면 다 뒤진 뒤 실패
	NavGrid nav;
	nav.cellSize = 50.0f;
	nav.rows = 6;
	nav.cols = 7;
	nav.originX = 300.0f;
	nav.originY = 0.0f;
	nav.cells.assign(6, std::string("...#..."));
	const Vec3 left = nav.ToWorld(2, 1), right = nav.ToWorld(2, 5);
	std::vector<Vec3> path;
	PathStats stats;

	CHECK(!FindPath(nav, left, right, path, &stats));		// 구역 번호 없음
	CHECK(stats.expanded > 0);

	nav.LabelRegions();
	CHECK(!nav.Connected(left, right));
	CHECK(!FindPath(nav, left, right, path, &stats));
	CHECK_EQ(stats.expanded, 0u);							// 바로 실패
	CHECK(path.empty());
	CHECK(FindPath(nav, left, nav.ToWorld(4, 0), path));	// 같은 구역은 찾음
}

TEST_CASE(Map_PatrolNodeKinds_Loaded)
{
	// 순찰 지점 종류 (P-4 문에서 방 안쪽 보기, P-5 중요한 방): 문 37개, 방 안 80개, 나머지 복도
	const MapData& map = DefaultMap();
	CHECK_EQ(map.patrolKinds.size(), map.patrolNodes.size());
	CHECK_EQ(map.patrolRooms.size(), map.patrolNodes.size());
	size_t doors = 0, rooms = 0;
	for (size_t i = 0; i < map.patrolKinds.size(); ++i)
	{
		if (map.patrolKinds[i] == PatrolNodeKind::Door) ++doors;
		if (map.patrolKinds[i] != PatrolNodeKind::Room) continue;
		++rooms;
		CHECK(map.patrolRooms[i] >= 0 && map.rooms[static_cast<size_t>(map.patrolRooms[i])].Contains(map.patrolNodes[i]));
	}
	CHECK_EQ(doors, size_t{ 37 });
	CHECK_EQ(rooms, size_t{ 80 });
}

// ───────────────── 시야 (3D) ─────────────────

TEST_CASE(Sight_WallBlocks_ButNotFromAbove)
{
	// Gallery1 동쪽 벽: x 1880~1920, y −3420~−2380, 높이 0~400
	const MapData& map = DefaultMap();
	CHECK(!HasLineOfSight(map, { 1700.0f, -3000.0f, 160.0f }, { 2100.0f, -3000.0f, 160.0f }));	// 벽을 가로지름
	CHECK(HasLineOfSight(map, { 1700.0f, -3000.0f, 450.0f }, { 2100.0f, -3000.0f, 450.0f }));	// 벽 위(400보다 높이)
	CHECK(HasLineOfSight(map, { GuardSpot.x, GuardSpot.y, 160.0f }, { FrontOfGuard.x, FrontOfGuard.y, 100.0f }));	// 트인 곳
}

TEST_CASE(Sight_LowObstacleAndHeight)
{
	// 맵을 복사해 Gallery1의 빈 곳에 조리대(높이 95) 하나를 놓고 시험한다
	MapData map = DefaultMap();
	MapCollider counter;
	counter.xmin = 1250.0f; counter.xmax = 1350.0f;
	counter.ymin = -3950.0f; counter.ymax = -3850.0f;
	counter.zMin = 0.0f; counter.zMax = 95.0f;
	counter.blocksSight = false;
	map.colliders.push_back(counter);

	const Vec3 eye{ 1000.0f, -3900.0f, 160.0f }, body{ 1600.0f, -3900.0f, 100.0f };	// 조리대를 가로지르는 선분
	CHECK(HasLineOfSight(map, eye, body));				// 시야를 막지 않는 충돌체는 검사하지 않음
	map.colliders.back().blocksSight = true;
	CHECK(HasLineOfSight(map, eye, body));				// 막는 충돌체라도 낮으면, 그 위를 지나는 선분(높이 125~135)은 보임
	map.colliders.back().zMax = 300.0f;
	CHECK(!HasLineOfSight(map, eye, body));				// 벽 높이면 가려짐
}

TEST_CASE(AI_CanSee_ConeRangeAndWall)
{
	GameWorld world = GuardArena();
	const uint16_t guard = Nth(world, EntityKind::Guard);
	Teleport(world, A, FrontOfGuard);
	CHECK(world.CanSee(*world.Find(guard), *world.ThiefOf(A)));			// 정면 8m
	Teleport(world, A, AtAngle(40.0f));
	CHECK(world.CanSee(*world.Find(guard), *world.ThiefOf(A)));			// 시야각 90° 안 (좌우 45°)
	Teleport(world, A, AtAngle(60.0f));
	CHECK(!world.CanSee(*world.Find(guard), *world.ThiefOf(A)));
	Teleport(world, A, FarSpot);
	CHECK(!world.CanSee(*world.Find(guard), *world.ThiefOf(A)));		// 탐지 범위 15m 밖

	world.Find(guard)->pos = { 1700.0f, -3000.0f, 0.0f };				// Gallery1 동쪽 벽 이쪽, 도둑은 벽 너머 Study2
	world.Find(guard)->yaw = 0.0f;
	Teleport(world, A, { 2100.0f, -3000.0f, 0.0f });
	CHECK(!world.CanSee(*world.Find(guard), *world.ThiefOf(A)));
}

TEST_CASE(AI_Patrol_WalksAt70Percent)
{
	GameWorld world = MakeWorld(1, true);
	const uint16_t guard = Nth(world, EntityKind::Guard);
	const Vec3 start = world.Find(guard)->pos;
	Run(world, 3000);
	const Vec3 now = world.Find(guard)->pos;
	const float moved = std::hypot(now.x - start.x, now.y - start.y);
	CHECK(moved > 100.0f && moved <= 3.0f * GameData::GuardWalkSpeed + 1.0f);	// 걷기 2.1m/s (N-03)
	CHECK(StateOf(world, guard) == GuardState::Patrol);
	CHECK(DefaultMap().nav.IsWalkable(now));
}

TEST_CASE(AI_Disabled_GuardsStayStill)
{
	GameWorld world = MakeWorld(1);
	const uint16_t guard = Nth(world, EntityKind::Guard);
	const Vec3 start = world.Find(guard)->pos;
	Run(world, 2000);
	CHECK(world.Find(guard)->pos.x == start.x && world.Find(guard)->pos.y == start.y);
}

TEST_CASE(AI_SeesThiefInFront_SpraysTenPerSecond)
{
	using namespace Content::WorldMath;
	GameWorld world = GuardArena();
	const uint16_t guard = Nth(world, EntityKind::Guard);
	Teleport(world, A, FrontOfGuard);
	world.ThiefOf(A)->hp = 1000;
	std::vector<WorldEvent> events;
	CHECK(RunUntil(world, 500, [&] { return StateOf(world, guard) == GuardState::Attack; }, &events));
	CHECK(Has<Msg::GuardAlert>(events, [&](const Msg::GuardAlert& a) { return a.guard == guard && a.target == world.ThiefOf(A)->id; }));

	// 1초에 10발 (12.2)
	const int before = world.ThiefOf(A)->hp;
	const auto oneSecond = RunCollect(world, 1000);
	size_t shots = 0;
	for (const WorldEvent& e : oneSecond)
		if (const auto* fire = std::get_if<Msg::Fire>(&e); fire && fire->shooter == guard)
			++shots;
	CHECK_EQ(shots, size_t{ 10 });
	const int lost = before - world.ThiefOf(A)->hp;
	CHECK(lost % 10 == 0 && lost <= 100);				// 맞은 발마다 10, 최대 100
	CHECK((world.Find(guard)->state & StateBit::Attacking) != 0);

	// 난사: 발마다 타겟 방향 ±30° 안, 방향이 서로 다르고, 빗나가는 발과 맞는 발이 섞임 (Q-07)
	const auto more = RunCollect(world, 5000);
	size_t hits = 0, misses = 0;
	float minDiff = 999.0f, maxDiff = -999.0f;
	for (const WorldEvent& e : more)
	{
		const auto* fire = std::get_if<Msg::Fire>(&e);
		if (fire == nullptr || fire->shooter != guard)
			continue;
		const float diff = AngleDiff(fire->yaw, YawTo(fire->origin, world.ThiefOf(A)->pos));
		CHECK(std::fabs(diff) <= GameData::GuardSprayHalfAngleDeg + 0.01f);
		minDiff = std::min(minDiff, diff);
		maxDiff = std::max(maxDiff, diff);
		(fire->hitEntity == world.ThiefOf(A)->id ? hits : misses)++;
	}
	CHECK(maxDiff - minDiff > 20.0f);					// 한 방향으로만 쏘지 않음
	CHECK(hits > 0 && misses > 0);
}

TEST_CASE(AI_Shots_PassThroughBoxes)
{
	// 경비원 기관총은 플레이어에게만 맞음: 사이에 있는 상자는 통과하고 부서지지 않음 (N-09)
	GameWorld world = GuardArena();
	const uint16_t guard = Nth(world, EntityKind::Guard);
	const uint16_t box = world.AddNow(EntityKind::Box, Offset(GuardSpot, 0, 300));		// 경비원과 타겟 사이
	Teleport(world, A, FrontOfGuard);
	world.ThiefOf(A)->hp = 100000;
	const auto events = RunCollect(world, 3000);
	CHECK(ShotsBy(events, guard) > 0);
	CHECK(world.Find(box) != nullptr && world.Find(box)->hp == GameData::BoxHp);
	CHECK(world.ThiefOf(A)->hp < 100000);
}

TEST_CASE(AI_ShootsWhileMoving_ChasesTarget)
{
	GameWorld world = GuardArena();
	const uint16_t guard = Nth(world, EntityKind::Guard);
	Teleport(world, A, FrontOfGuard);
	world.ThiefOf(A)->hp = 1000;
	CHECK(RunUntil(world, 500, [&] { return StateOf(world, guard) == GuardState::Attack; }));
	const float before = Content::WorldMath::Dist2D(world.Find(guard)->pos, world.ThiefOf(A)->pos);
	const auto events = RunCollect(world, 300);
	CHECK(Content::WorldMath::Dist2D(world.Find(guard)->pos, world.ThiefOf(A)->pos) < before);	// 쏘면서 다가감 (12.2 이동)
	CHECK(ShotsBy(events, guard) > 0);
	CHECK((world.Find(guard)->state & StateBit::Attacking) != 0);
	CHECK((world.Find(guard)->state & StateBit::Running) != 0);
	Run(world, 3000);
	CHECK(Content::WorldMath::Dist2D(world.Find(guard)->pos, world.ThiefOf(A)->pos) >= GameData::BodyRadiusCm * 2.0f - 1.0f);	// 겹치지 않음
}

TEST_CASE(AI_TargetOutsideAttackCone_TurnsSlowlyFirst)
{
	GameWorld world = GuardArena();
	const uint16_t guard = Nth(world, EntityKind::Guard);
	Teleport(world, A, AtAngle(42.0f));			// 시야 안(45°), 공격 범위 밖(30°)
	world.ThiefOf(A)->hp = 1000;
	Run(world, 100);
	CHECK_EQ(world.ThiefOf(A)->hp, 1000);		// 아직 못 쏨: 초당 90°로만 돈다 (N-04)
	CHECK(StateOf(world, guard) == GuardState::Chase);
	const auto events = RunCollect(world, 400);
	CHECK(ShotsBy(events, guard) > 0);			// 돌아서 범위에 넣은 뒤 사격 (난사라 맞는지는 운)
}

TEST_CASE(AI_TurnRate_90DegPerSecond)
{
	GameWorld world = GuardArena();
	const uint16_t guard = Nth(world, EntityKind::Guard);
	Teleport(world, A, AtAngle(40.0f));
	Run(world, 50);								// 발견 → 추격
	const float startYaw = world.Find(guard)->yaw;
	Run(world, 100);
	CHECK(std::fabs(world.Find(guard)->yaw - startYaw) <= 9.0f + 0.01f);		// 0.1초에 최대 9°
}

TEST_CASE(AI_CloakedOrInSmoke_NotDetected)
{
	GameWorld world = GuardArena();
	const uint16_t guard = Nth(world, EntityKind::Guard);
	world.ThiefOf(A)->state |= StateBit::Cloaked;
	world.ThiefOf(A)->skillActiveUntilMs = 60000;
	Teleport(world, A, FrontOfGuard);
	Run(world, 300);
	CHECK(StateOf(world, guard) == GuardState::Patrol);		// 은신 (N-12)

	GameWorld smoky = GuardArena();
	smoky.ThiefOf(A)->consumable = ConsumableId::Smoke;
	Teleport(smoky, A, FrontOfGuard);
	Act(smoky, A, ActionType::UseConsumable);
	Run(smoky, 300);
	CHECK(StateOf(smoky, Nth(smoky, EntityKind::Guard)) == GuardState::Patrol);	// 연막 안 (I-32)
}

TEST_CASE(AI_AttackedByOther_SwitchesTarget_AndLocks)
{
	GameWorld world = GuardArena(2);
	const uint16_t guard = Nth(world, EntityKind::Guard);
	Teleport(world, A, FrontOfGuard);
	world.ThiefOf(A)->hp = 1000;
	RunUntil(world, 500, [&] { return StateOf(world, guard) == GuardState::Attack; });
	CHECK_EQ(world.BrainOf(guard)->target, world.ThiefOf(A)->id);

	// B가 시야 안 옆쪽(약 10°, 6m)에서 권총으로 쏜다 → 즉시 B로 (N-15), 고정 (N-16)
	const Vec3 bPos = Offset(GuardSpot, 100, 600);
	const float toGuard = std::atan2(GuardSpot.y - bPos.y, GuardSpot.x - bPos.x) * 180.0f / 3.14159265f;
	Teleport(world, B, bPos, toGuard);
	world.ThiefOf(B)->hp = 1000;
	Give(world, B, WeaponId::Pistol);
	Attack(world, B, toGuard);
	CHECK_EQ(world.BrainOf(guard)->target, world.ThiefOf(B)->id);
	CHECK(world.BrainOf(guard)->lockedByAttack);

	// 고정된 동안 A가 쏴도 바뀌지 않는다
	Give(world, A, WeaponId::Pistol);
	Attack(world, A, -90.0f);
	CHECK_EQ(world.BrainOf(guard)->target, world.ThiefOf(B)->id);
}

TEST_CASE(AI_LosesTarget_Searches_ThenPatrols)
{
	GameWorld world = GuardArena();
	const uint16_t guard = Nth(world, EntityKind::Guard);
	Teleport(world, A, FrontOfGuard);
	world.ThiefOf(A)->hp = 1000;
	RunUntil(world, 500, [&] { return StateOf(world, guard) == GuardState::Attack; });
	Teleport(world, A, FarAway);
	CHECK(RunUntil(world, 4000, [&] { return StateOf(world, guard) == GuardState::Search; }));	// 3초 못 보면 놓침
	CHECK(RunUntil(world, 15000, [&] { return StateOf(world, guard) == GuardState::Patrol; }));
}

TEST_CASE(AI_Repairing_StopsFiveSeconds)
{
	GameWorld world = GuardArena();
	const uint16_t guard = Nth(world, EntityKind::Guard);
	world.Find(guard)->hp = 10;
	Teleport(world, A, Offset(GuardSpot, 0, 150), -90.0f);
	world.ThiefOf(A)->hp = 1000;
	Attack(world, A, -90.0f);								// 삼단봉 10 → HP 0 → 수리
	Run(world, 50);
	CHECK(StateOf(world, guard) == GuardState::Repairing);
	const int hp = world.ThiefOf(A)->hp;
	Run(world, 4500);
	CHECK_EQ(world.ThiefOf(A)->hp, hp);						// 수리 중에는 쏘지 않음
	Run(world, 600);
	CHECK_EQ(world.Find(guard)->hp, 200);
	CHECK(StateOf(world, guard) != GuardState::Repairing);
}

TEST_CASE(AI_Stunned_DoesNothing)
{
	GameWorld world = GuardArena();
	const uint16_t guard = Nth(world, EntityKind::Guard);
	Teleport(world, A, Offset(GuardSpot, 0, 200), -90.0f);
	world.ThiefOf(A)->hp = 1000;
	Give(world, A, WeaponId::Taser);
	Attack(world, A, -90.0f);								// 2초 경직 (N-08)
	CHECK_EQ(ShotsBy(RunCollect(world, 1500), guard), size_t{ 0 });	// 경직 중에는 안 쏨
	CHECK_EQ(world.ThiefOf(A)->hp, 1000);
	CHECK(StateOf(world, guard) == GuardState::Stunned);
	std::vector<WorldEvent> events;
	CHECK(RunUntil(world, 1500, [&] { return ShotsBy(events, guard) > 0; }, &events));	// 풀리면 다시 쏜다
}
