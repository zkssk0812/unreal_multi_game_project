#include "ServerCore/CorePch.h"

#include <set>

#include "TestFramework.h"
#include "WorldTestUtil.h"

// GameWorld 기본: 준비, 이동, 스테미나, 구르기, 스냅샷 (psd.md M0·M1)

using namespace WorldTest;

TEST_CASE(World_Setup_CreatesEntities)
{
	GameWorld world = MakeWorld(2);
	CHECK(world.Entities()[0].kind == EntityKind::Thief && world.Entities()[0].id == 1);	// 플레이어가 1번부터
	CHECK(world.Entities()[1].ownerPlayerId == B);
	CHECK_EQ(CountKind(world, EntityKind::Guard), size_t{ 3 });			// N-02
	CHECK_EQ(CountKind(world, EntityKind::Safe), size_t{ 1 });			// O-01
	CHECK_EQ(CountKind(world, EntityKind::ReturnPoint), size_t{ 5 });	// O-21
	CHECK(CountKind(world, EntityKind::Box) > 10 && CountKind(world, EntityKind::Box) <= GameData::BoxCount);
	CHECK_EQ(CountKind(world, EntityKind::FieldItem), GameData::FieldItemStart);	// I-14
	CHECK_EQ(world.TreasureValue(), uint32_t{ 2000 });

	// 지운 오브젝트(숨는 곳·창고 문·탈출구)는 없다: 종류는 v3 목록뿐
	for (const Entity& e : world.Entities())
		CHECK(static_cast<int>(e.kind) <= static_cast<int>(EntityKind::Smoke));
}

TEST_CASE(World_ThievesStartInDifferentRandomRooms)
{
	// 의적 셋은 방 안 무작위 칸, 서로 다른 방에서 시작 (A-26). 판마다 자리가 바뀜
	const MapData& map = DefaultMap();
	std::set<std::pair<float, float>> firstSpots;
	for (uint32_t seed = 1; seed <= 30; ++seed)
	{
		GameWorld world = MakeWorld(3, false, {}, seed);
		std::set<int> rooms;
		for (uint64_t id : { A, B, C })
		{
			const Entity* thief = world.ThiefOf(id);
			CHECK(thief != nullptr && OnRoomCell(map, thief->pos));
			if (thief)
				rooms.insert(RoomOf(map, thief->pos));
		}
		CHECK_EQ(rooms.size(), size_t{ 3 });
		firstSpots.insert({ world.ThiefOf(A)->pos.x, world.ThiefOf(A)->pos.y });
	}
	CHECK(firstSpots.size() >= 20);
}

TEST_CASE(World_ThiefStartsWithLoadout)
{
	GameWorld world = MakeWorld(3, false, { {}, {}, { GameData::Perk::Vest, GameData::Perk::DeathSave } });
	const Entity* first = world.ThiefOf(A);
	const Entity* third = world.ThiefOf(C);
	CHECK(first && third);
	if (!first || !third)
		return;
	CHECK_EQ(first->hp, 100);
	CHECK(first->stamina == GameData::StaminaMax);
	CHECK(first->weapons[0].id == WeaponId::Baton && first->weapons[1].id == WeaponId::None);	// 삼단봉만 (D-07)
	CHECK(first->skill == SkillId::Taser && third->skill == SkillId::Cloak);					// 캐릭터 고유 (A-06)
	CHECK_EQ(first->shield, 0);
	CHECK_EQ(third->shield, 20);		// 방탄조끼 (K-01)
	CHECK(third->deathSaveLeft);		// 가시회생 (K-02)
	CHECK_EQ(third->name, std::string("p2"));
}

TEST_CASE(World_Move_UsesClientPosition)
{
	GameWorld world = MakeWorld(1);
	Teleport(world, A, { 123, 456, 7 }, 45.0f, MoveFlag::Moving);
	const Entity* thief = world.ThiefOf(A);
	CHECK(thief->pos.x == 123 && thief->pos.y == 456 && thief->pos.z == 7);
	CHECK(thief->yaw == 45.0f);			// 시선 = 커서 방향 (CT-09)
	CHECK((thief->state & StateBit::Running) == 0);
}

TEST_CASE(World_Running_DrainsStaminaThenExhausted)
{
	GameWorld world = MakeWorld(1);
	Teleport(world, A, HallW, 0.0f, MoveFlag::Moving | MoveFlag::Running);
	CHECK((world.ThiefOf(A)->state & StateBit::Running) != 0);
	world.Update(5.0f);					// 초당 10 × 5초
	CHECK(world.ThiefOf(A)->stamina == 50.0f);
	world.Update(6.0f);					// 다 쓰면 탈진, 걷기로
	CHECK(world.ThiefOf(A)->stamina == 0.0f);
	CHECK((world.ThiefOf(A)->state & StateBit::Exhausted) != 0);
	Teleport(world, A, HallW, 0.0f, MoveFlag::Moving | MoveFlag::Running);
	CHECK((world.ThiefOf(A)->state & StateBit::Running) == 0);	// 탈진 중 달리기 불가 (P-03)

	world.Update(10.0f);				// 탈진 회복 +5/s
	CHECK(world.ThiefOf(A)->stamina == 50.0f);
	world.Update(10.0f);
	CHECK((world.ThiefOf(A)->state & StateBit::Exhausted) == 0);	// 최대치까지 차면 풀림
}

TEST_CASE(World_Dodge_CostsStamina_InvulnerableHalfSecond)
{
	GameWorld world = MakeWorld(1);
	CHECK(!Act(world, A, ActionType::Dodge).has_value());
	CHECK(world.ThiefOf(A)->stamina == 80.0f);
	CHECK((world.ThiefOf(A)->state & StateBit::Dodging) != 0);
	const auto again = Act(world, A, ActionType::Dodge);
	CHECK(again && *again == RejectReason::Cooldown);
	Run(world, 500);
	CHECK((world.ThiefOf(A)->state & StateBit::Dodging) == 0);
}

TEST_CASE(World_Snapshot_DynamicEveryTick_ObjectsOnlyWhenChanged)
{
	GameWorld world = MakeWorld(3);
	Msg::Snapshot first = world.MakeSnapshot(1, 1000);
	CHECK_EQ(first.entities.size(), size_t{ 6 });		// 플레이어 3 + 경비원 3
	CHECK(first.entities[0].weapon == WeaponId::Baton && first.entities[0].hp == 100);

	// 상자가 맞아 HP가 바뀌면 그 틱에만 들어간다
	ClearArena(world);
	const uint16_t box = world.AddNow(EntityKind::Box, Offset(HallW, 100, 0));
	world.TakeEvents();
	Teleport(world, A, HallW);
	Attack(world, A, 0.0f);
	Msg::Snapshot hit = world.MakeSnapshot(2, 1000);
	CHECK_EQ(hit.entities.size(), size_t{ 7 });
	CHECK(hit.entities.back().entityId == box && hit.entities.back().hp == 10);
	CHECK_EQ(world.MakeSnapshot(3, 1000).entities.size(), size_t{ 6 });
}

TEST_CASE(World_LeftThief_IgnoresInput_DropsValuables)
{
	GameWorld world = MakeWorld(2);
	world.ThiefOf(B)->carried.push_back({ ValuableQuality::Unique, 2000 });
	world.MarkLeft(B);
	Teleport(world, B, { 999, 999, 0 });
	CHECK(world.ThiefOf(B)->pos.x != 999);
	const auto action = Act(world, B, ActionType::Dodge);
	CHECK(action && *action == RejectReason::InvalidState);
	CHECK(world.AnyThiefActive());
	CHECK_EQ(CountKind(world, EntityKind::Valuable), size_t{ 1 });	// 보물은 사라지지 않는다 (I-24)
	const auto results = world.MakeResults();
	CHECK(results.size() == 2 && !results[0].leftEarly && results[1].leftEarly);
}

TEST_CASE(World_Status_SentOnlyWhenChanged)
{
	GameWorld world = MakeWorld(2);
	CHECK_EQ(world.TakeStatuses().size(), size_t{ 2 });		// 처음 한 번은 모두
	CHECK(world.TakeStatuses().empty());					// 바뀐 것이 없음
	world.ThiefOf(A)->hp = 70;
	const auto changed = world.TakeStatuses();
	CHECK(changed.size() == 1 && changed[0].first == A && changed[0].second.hp == 70);
}
