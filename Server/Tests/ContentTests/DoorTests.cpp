#include "ServerCore/CorePch.h"

#include <cmath>
#include <set>

#include "Content/Game/Sight.h"
#include "Content/Game/WorldMath.h"
#include "TestFramework.h"
#include "WorldTestUtil.h"

// 문 (클라 2026-11-24): 방 사이 문 37곳, F로 즉시 열고 닫기, 문간에 누가 있으면 닫지 않음,
// 닫힌 문은 벽처럼 시야·공격을 막음(레일건은 뚫음), 경비원은 열고 지나가며 열어 둠

using namespace WorldTest;
using namespace Content::WorldMath;

namespace {

// 문 하나와 그 양쪽 자리. 양쪽은 문 가운데에서 지나가는 방향으로 ±dist, 걸을 수 있고 문 말고는 막는 것이 없다
struct DoorSpot
{
	size_t index = 0;
	uint16_t id = 0;
	Vec3 center, sideA, sideB;
	float yawAtoB = 0.0f;
};

uint16_t DoorId(const GameWorld& world, size_t index)
{
	for (const Entity& e : world.Entities())
		if (e.kind == EntityKind::Door && e.typeId == index)
			return e.id;
	return 0;
}

DoorSpot FindOpenAirDoor(const GameWorld& world, float dist)
{
	const MapData& map = DefaultMap();
	for (size_t i = 0; i < map.doors.size(); ++i)
	{
		const MapDoor& d = map.doors[i];
		const Vec3 a = Ahead(d.center, d.yaw + 180.0f, dist);
		const Vec3 b = Ahead(d.center, d.yaw, dist);
		const Vec3 chestA{ a.x, a.y, GameData::ShotHeightCm }, chestB{ b.x, b.y, GameData::ShotHeightCm };
		if (map.nav.IsWalkable(a) && map.nav.IsWalkable(b) && HasLineOfSight(map, chestA, chestB))
			return { i, DoorId(world, i), d.center, a, b, YawTo(a, b) };
	}
	return {};
}

bool IsOpen(GameWorld& world, uint16_t door) { return (world.Find(door)->state & StateBit::Opened) != 0; }

GameWorld DoorWorld(int players = 2, bool ai = false)
{
	GameWorld world = MakeWorld(players, ai);
	ClearArena(world);
	for (int i = 0; i < players; ++i)
		Teleport(world, A + static_cast<uint64_t>(i), FarAway);
	world.TakeEvents();
	return world;
}

} // namespace

TEST_CASE(Door_Setup_AllInteriorDoorsClosed)
{
	GameWorld world = DoorWorld();
	const MapData& map = DefaultMap();
	CHECK_EQ(map.doors.size(), size_t{ 37 });						// 안쪽 문 37 (바깥 잠긴 문 2곳은 벽)
	CHECK_EQ(CountKind(world, EntityKind::Door), map.doors.size());
	std::set<uint16_t> indexes;
	for (const Entity& e : world.Entities())
	{
		if (e.kind != EntityKind::Door)
			continue;
		CHECK((e.state & StateBit::Opened) == 0);					// 처음엔 모두 닫힘
		CHECK(e.yaw == 0.0f || e.yaw == 90.0f);
		indexes.insert(e.typeId);
	}
	CHECK_EQ(indexes.size(), map.doors.size());						// typeId = 문 번호, 겹치지 않음
}

TEST_CASE(Door_F_TogglesImmediately_SentInSnapshot)
{
	GameWorld world = DoorWorld();
	const DoorSpot door = FindOpenAirDoor(world, 100.0f);
	CHECK(door.id != 0);
	Teleport(world, A, door.sideA);
	world.MakeSnapshot(1, 0);
	CHECK(!Act(world, A, ActionType::InteractBegin, door.id).has_value());
	CHECK(IsOpen(world, door.id));
	CHECK((world.ThiefOf(A)->state & StateBit::Interacting) == 0);	// 시간 걸리는 상호작용이 아님
	const auto snapshot = world.MakeSnapshot(2, 0);
	bool sent = false;
	for (const auto& s : snapshot.entities)
		if (s.entityId == door.id)
			sent = (s.state & StateBit::Opened) != 0;
	CHECK(sent);													// 바뀐 틱의 스냅샷에 실림

	CHECK(!Act(world, A, ActionType::InteractBegin, door.id).has_value());
	CHECK(!IsOpen(world, door.id));								// 다시 F: 닫힘

	Teleport(world, A, Ahead(door.center, door.yawAtoB + 180.0f, 200.0f));
	const auto tooFar = Act(world, A, ActionType::InteractBegin, door.id);
	CHECK(tooFar && *tooFar == RejectReason::OutOfRange);			// 150cm 밖
}

TEST_CASE(Door_Close_RejectedWhileSomeoneInDoorway)
{
	GameWorld world = DoorWorld();
	const DoorSpot door = FindOpenAirDoor(world, 100.0f);
	Teleport(world, A, door.sideA);
	Act(world, A, ActionType::InteractBegin, door.id);
	Teleport(world, B, door.center);								// 문간에 서 있음
	const auto busy = Act(world, A, ActionType::InteractBegin, door.id);
	CHECK(busy && *busy == RejectReason::TargetBusy);
	CHECK(IsOpen(world, door.id));
	const uint16_t guard = PlaceGuard(world, door.center, 0.0f);	// 경비원도 마찬가지
	Teleport(world, B, FarAway);
	CHECK(Act(world, A, ActionType::InteractBegin, door.id).has_value());
	world.Find(guard)->pos = Parking;
	CHECK(!Act(world, A, ActionType::InteractBegin, door.id).has_value());
	CHECK(!IsOpen(world, door.id));
}

TEST_CASE(Door_Closed_BlocksShots_RailgunPassesThrough)
{
	GameWorld world = DoorWorld();
	const DoorSpot door = FindOpenAirDoor(world, 200.0f);
	Teleport(world, A, door.sideA, door.yawAtoB);
	Teleport(world, B, door.sideB);
	Give(world, A, WeaponId::Pistol);
	Attack(world, A, door.yawAtoB);
	CHECK_EQ(world.ThiefOf(B)->hp, 100);							// 닫힌 문에 막힘

	Teleport(world, A, Ahead(door.center, door.yawAtoB + 180.0f, 100.0f), door.yawAtoB);
	Act(world, A, ActionType::InteractBegin, door.id);				// 열고
	Teleport(world, A, door.sideA, door.yawAtoB);
	Run(world, 1000);
	Attack(world, A, door.yawAtoB);
	CHECK(world.ThiefOf(B)->hp < 100);								// 열린 문은 지나감

	Run(world, 3000);
	Teleport(world, A, Ahead(door.center, door.yawAtoB + 180.0f, 100.0f), door.yawAtoB);
	Act(world, A, ActionType::InteractBegin, door.id);				// 다시 닫고
	CHECK(!IsOpen(world, door.id));
	Teleport(world, A, door.sideA, door.yawAtoB);
	world.ThiefOf(B)->hp = 100;
	Give(world, A, WeaponId::Railgun);
	Attack(world, A, door.yawAtoB);
	Run(world, 2100);
	CHECK(world.ThiefOf(B)->hp < 100);								// 레일건은 벽처럼 문도 뚫음 (A-11)
}

TEST_CASE(Door_Closed_BlocksGuardSight)
{
	GameWorld world = DoorWorld(1, false);
	const DoorSpot door = FindOpenAirDoor(world, 300.0f);
	const uint16_t guard = PlaceGuard(world, door.sideA, door.yawAtoB);
	Teleport(world, A, door.sideB);
	CHECK(!world.CanSee(*world.Find(guard), *world.ThiefOf(A)));	// 닫힌 문 너머는 안 보임
	world.Find(guard)->pos = Parking;
	Teleport(world, A, Ahead(door.center, door.yawAtoB, 100.0f));
	Act(world, A, ActionType::InteractBegin, door.id);
	Teleport(world, A, door.sideB);
	world.Find(guard)->pos = door.sideA;
	CHECK(world.CanSee(*world.Find(guard), *world.ThiefOf(A)));	// 열면 보임
}

TEST_CASE(Door_GuardsOpenDoorsOnTheirWay_NeverWalkThroughClosed_LeaveOpen)
{
	// 경비원 3명이 60초 순찰: 문간(문짝 자리)에 선 경비원의 문은 언제나 열려 있고, 한 번 열린 문은 닫히지 않는다
	GameWorld world = MakeWorld(1, true);
	Teleport(world, A, Parking);									// 플레이어는 맵 밖 (쫓지 않게)
	const MapData& map = DefaultMap();
	std::vector<bool> everOpen(map.doors.size(), false);
	size_t walkedThrough = 0;
	for (int t = 0; t < 1200; ++t)
	{
		world.Update(0.05f);
		world.TakeEvents();
		for (const Entity& e : world.Entities())
		{
			if (e.kind == EntityKind::Door)
			{
				const bool open = (e.state & StateBit::Opened) != 0;
				CHECK(open || !everOpen[e.typeId]);					// 경비원은 닫지 않음
				everOpen[e.typeId] = everOpen[e.typeId] || open;
			}
		}
		for (const Entity& e : world.Entities())
		{
			if (!e.IsGuard())
				continue;
			for (size_t i = 0; i < map.doors.size(); ++i)
			{
				const MapCollider& p = map.doors[i].panel;
				if (e.pos.x >= p.xmin && e.pos.x <= p.xmax && e.pos.y >= p.ymin && e.pos.y <= p.ymax)
				{
					++walkedThrough;
					CHECK(everOpen[i]);									// 닫힌 문을 그냥 지나가지 않음
				}
			}
		}
	}
	CHECK(walkedThrough > 0);											// 60초 동안 문을 지나가긴 함
}
