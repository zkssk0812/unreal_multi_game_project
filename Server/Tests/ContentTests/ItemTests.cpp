#include "ServerCore/CorePch.h"

#include <cmath>
#include <set>

#include "TestFramework.h"
#include "WorldTestUtil.h"

// 상자·소비 아이템·필드 아이템 (psd.md M5)

using namespace WorldTest;

namespace {

GameWorld Arena(int players = 2)
{
	GameWorld world = MakeWorld(players);
	ClearArena(world);
	Teleport(world, A, HallW, 0.0f);
	if (players > 1)
		Teleport(world, B, HallE, 180.0f);
	world.TakeEvents();
	return world;
}

bool BoxAt(const GameWorld& world, const Vec3& pos)
{
	for (const Entity& e : world.Entities())
		if (e.kind == EntityKind::Box && !e.removed && e.pos.x == pos.x && e.pos.y == pos.y)
			return true;
	return false;
}

} // namespace

// ───────────────── 무작위 배치 (Q-38, A-05) ─────────────────

TEST_CASE(Place_SafeAndBoxes_RandomRoomCells)
{
	// 판마다(씨앗마다) 금고·상자 자리가 바뀌고, 언제나 방 안의 걸을 수 있는 칸. 금고는 현관(시작 방) 제외, 복도에는 없음
	const MapData& map = DefaultMap();
	std::set<int> safeRooms;
	std::set<std::pair<float, float>> firstBoxSpots;
	for (uint32_t seed = 1; seed <= 40; ++seed)
	{
		GameWorld world = MakeWorld(3, false, {}, seed);
		std::set<int> thiefRooms;
		for (const Entity& e : world.Entities())
			if (e.kind == EntityKind::Thief)
				thiefRooms.insert(RoomOf(map, e.pos));
		for (const Entity& e : world.Entities())
		{
			if (e.kind != EntityKind::Safe && e.kind != EntityKind::Box)
				continue;
			const int room = RoomOf(map, e.pos);
			CHECK(room >= 0 && map.rooms[static_cast<size_t>(room)].returnPoint);	// 방 안, 복도 아님
			CHECK(map.nav.IsWalkable(e.pos) && map.nav.Connected(map.thiefSpawns[0].pos, e.pos));
			if (e.kind == EntityKind::Safe)
			{
				CHECK(!thiefRooms.contains(room));		// 의적이 시작하는 방에는 없음 (A-05)
				CHECK_EQ(static_cast<int>(e.safeRoom), room);
				CHECK(e.interactPos.x == e.pos.x && e.interactPos.y == e.pos.y);
				safeRooms.insert(room);
			}
		}
		const Entity* box = world.Find(Nth(world, EntityKind::Box));
		CHECK(box != nullptr);
		if (box)
			firstBoxSpots.insert({ box->pos.x, box->pos.y });
		CHECK_EQ(CountKind(world, EntityKind::Box), GameData::BoxCount);
	}
	CHECK(safeRooms.size() >= 10);			// 40판이면 여러 방에 고루 나옴
	CHECK(firstBoxSpots.size() >= 30);
}

TEST_CASE(Place_ObjectsKeep1mApart)
{
	// 금고·상자·필드 아이템·반납 지점은 서로 1m 이상 떨어져 놓인다 (A-04)
	GameWorld world = MakeWorld(3, false, {}, 77);
	std::vector<Vec3> spots;
	for (const Entity& e : world.Entities())
		if (e.kind == EntityKind::Safe || e.kind == EntityKind::Box || e.kind == EntityKind::FieldItem)
			spots.push_back(e.pos);
	for (size_t i = 0; i < spots.size(); ++i)
		for (size_t j = i + 1; j < spots.size(); ++j)
			CHECK(std::hypot(spots[i].x - spots[j].x, spots[i].y - spots[j].y) >= GameData::ItemSpacingCm);
}

// ───────────────── 상자 ─────────────────

TEST_CASE(Item_Box_Damage20Accumulates_ThenRespawnsSameSpot)
{
	GameWorld world = Arena(1);
	const Vec3 spot = Offset(HallW, 100, 0);
	const uint16_t box = world.AddNow(EntityKind::Box, spot);
	Attack(world, A, 0.0f);
	CHECK(world.Find(box) != nullptr && world.Find(box)->hp == 10);
	Run(world, 500);
	Attack(world, A, 0.0f);
	CHECK(world.Find(box) == nullptr);					// 누적 20 → 부서짐 (O-11, A-02)
	Run(world, 29000);
	CHECK(!BoxAt(world, spot));
	Run(world, 31000);
	CHECK(BoxAt(world, spot));							// 30~60초 뒤 같은 자리 (O-14)
}

TEST_CASE(Item_Box_Interact3s_CancelResets)
{
	GameWorld world = Arena(1);
	const uint16_t box = world.AddNow(EntityKind::Box, Offset(HallW, 100, 0));
	CHECK(!Act(world, A, ActionType::InteractBegin, box).has_value());
	Run(world, 2000);
	Act(world, A, ActionType::InteractCancel);
	Act(world, A, ActionType::InteractBegin, box);
	Run(world, 2500);
	CHECK(world.Find(box) != nullptr);					// 처음부터 다시 (O-11)
	Run(world, 550);
	CHECK(world.Find(box) == nullptr);
}

TEST_CASE(Item_Interact_StopsWhenHit)
{
	// 금고·상자를 여는 중에 공격받으면 멈춤 (IN-05). 금고는 진행도가 남고, 상자는 처음부터
	GameWorld world = Arena(2);
	const uint16_t box = world.AddNow(EntityKind::Box, Offset(HallW, -100, 0));	// A의 서쪽 (B의 총알 길을 막지 않게)
	CHECK(!Act(world, A, ActionType::InteractBegin, box).has_value());
	Run(world, 2000);
	Give(world, B, WeaponId::Pistol);
	Attack(world, B, 180.0f);			// HallE에서 서쪽(A)으로 한 발
	CHECK(world.ThiefOf(A)->hp < 100);
	CHECK(world.ThiefOf(A)->interactTarget == 0 && (world.ThiefOf(A)->state & StateBit::Interacting) == 0);
	Run(world, 1500);
	CHECK(world.Find(box) != nullptr);					// 멈췄으니 3초가 지나도 안 부서짐
}

TEST_CASE(Item_Box_DropsAtMostOneItem)
{
	GameWorld world = Arena(1);
	size_t items = 0, empty = 0;
	for (int i = 0; i < 30; ++i)		// 30 × 0.55초 < 20초: 필드 아이템 생성·상자 재생성과 겹치지 않게
	{
		const size_t before = world.Entities().size();
		const uint16_t box = world.AddNow(EntityKind::Box, Offset(HallW, 100, 0));
		world.Find(box)->hp = 1;
		Attack(world, A, 0.0f);
		Run(world, 500);
		const size_t after = world.Entities().size();
		CHECK(after + 1 >= before && after <= before + 1);	// 상자 하나가 아이템 0~1개로
		(after == before + 1 ? items : empty)++;
		// 나온 아이템을 치운다 (다음 상자 판정과 섞이지 않게)
		for (const Entity& e : world.Entities())
			if (e.kind != EntityKind::Thief && e.kind != EntityKind::Guard && e.pos.x == Offset(HallW, 100, 0).x)
				world.Find(e.id)->pos = Parking;
	}
	CHECK(items > 0 && empty > 0);						// 확률표 (O-12): 아이템도 "없음"도 나온다
}

// ───────────────── 소비 아이템 ─────────────────

TEST_CASE(Item_Consumable_OneOnly_SwapLeavesOld)
{
	GameWorld world = Arena(1);
	world.ThiefOf(A)->consumable = ConsumableId::Smoke;
	const uint16_t kit = world.AddNow(EntityKind::ConsumablePickup, Offset(HallW, 100, 0), static_cast<uint16_t>(ConsumableId::HealKit));
	CHECK(!Act(world, A, ActionType::InteractBegin, kit).has_value());
	CHECK(world.ThiefOf(A)->consumable == ConsumableId::HealKit);
	const uint16_t left = Nth(world, EntityKind::ConsumablePickup);
	CHECK(left != 0 && world.Find(left)->typeId == static_cast<uint16_t>(ConsumableId::Smoke));	// A-03
}

TEST_CASE(Item_HealKit_FullHpRejected_Heals30)
{
	GameWorld world = Arena(1);
	world.ThiefOf(A)->consumable = ConsumableId::HealKit;
	const auto full = Act(world, A, ActionType::UseConsumable);
	CHECK(full && *full == RejectReason::HpFull);
	world.ThiefOf(A)->hp = 50;
	CHECK(!Act(world, A, ActionType::UseConsumable).has_value());
	CHECK_EQ(world.ThiefOf(A)->hp, 80);
	CHECK(world.ThiefOf(A)->consumable == ConsumableId::None);
	const auto none = Act(world, A, ActionType::UseConsumable);
	CHECK(none && *none == RejectReason::NotAvailable);		// 없으면 아무 일도 없음 (CT-08)
}

TEST_CASE(Item_Smoke_MarksPlayersInside_Expires10s)
{
	GameWorld world = Arena();
	Teleport(world, B, Offset(HallW, 300, 0));
	world.ThiefOf(A)->consumable = ConsumableId::Smoke;
	Act(world, A, ActionType::UseConsumable);
	Run(world, 50);
	CHECK((world.ThiefOf(A)->state & StateBit::InSmoke) != 0);
	CHECK((world.ThiefOf(B)->state & StateBit::InSmoke) != 0);	// 반지름 5m (I-32)
	Run(world, 10000);
	CHECK_EQ(CountKind(world, EntityKind::Smoke), size_t{ 0 });
	CHECK((world.ThiefOf(B)->state & StateBit::InSmoke) == 0);
}

TEST_CASE(Item_Flashbang_FlashesPlayers_StunsGuard_NotThrower)
{
	GameWorld world = Arena();
	const Vec3 land = Offset(HallW, 600, 0);
	Teleport(world, B, Offset(land, 0, 200));
	const uint16_t guard = PlaceGuard(world, Offset(land, 0, -200), 90.0f);
	world.ThiefOf(A)->consumable = ConsumableId::Flashbang;
	Act(world, A, ActionType::UseConsumable, 0, 0.0f, land);
	Run(world, 1100);
	CHECK((world.ThiefOf(B)->state & StateBit::Flashed) != 0);		// 3초 (I-33)
	CHECK((world.Find(guard)->state & StateBit::Stunned) != 0);	// 2초
	CHECK((world.ThiefOf(A)->state & StateBit::Flashed) == 0);
	Run(world, 3000);
	CHECK((world.ThiefOf(B)->state & StateBit::Flashed) == 0);
}

TEST_CASE(Item_Jetpack_PushesPlayerOnce_StunsGuard)
{
	GameWorld world = Arena();
	Teleport(world, B, Offset(HallW, 50, 0));
	const uint16_t guard = PlaceGuard(world, Offset(HallW, 0, 60), 0.0f);
	world.ThiefOf(A)->consumable = ConsumableId::Jetpack;
	CHECK(!Act(world, A, ActionType::UseConsumable).has_value());
	const auto events = RunCollect(world, 500);
	const auto push = First<Msg::Knockback>(events);
	CHECK(Count<Msg::Knockback>(events) == 1 && push && push->target == world.ThiefOf(B)->id && push->distance == 300.0f);
	CHECK((world.Find(guard)->state & StateBit::Stunned) != 0);		// 1초 (I-34)
	const auto locked = Act(world, A, ActionType::AttackStart);
	CHECK(!locked.has_value());										// 공격 입력은 받되 제트팩 중엔 쏘지 않음
	Run(world, 3000);
	CHECK((world.ThiefOf(A)->state & StateBit::Jetpack) == 0);
}

// ───────────────── 필드 아이템 ─────────────────

TEST_CASE(Item_FieldPotion_NotTakenAtFullHp_HealsOnTouch)
{
	GameWorld world = Arena(1);
	const uint16_t potion = world.AddNow(EntityKind::FieldItem, Offset(HallW, 20, 0), static_cast<uint16_t>(FieldItemId::PotionM));
	Run(world, 100);
	CHECK(world.Find(potion) != nullptr);					// HP 100이면 먹지 않음 (I-16)
	world.ThiefOf(A)->hp = 50;
	Run(world, 50);
	CHECK(world.Find(potion) == nullptr);
	CHECK_EQ(world.ThiefOf(A)->hp, 80);						// 중 +30
	world.ThiefOf(A)->hp = 90;
	world.AddNow(EntityKind::FieldItem, Offset(HallW, 20, 0), static_cast<uint16_t>(FieldItemId::PotionL));
	Run(world, 50);
	CHECK_EQ(world.ThiefOf(A)->hp, 100);					// 100을 넘지 않음
}

TEST_CASE(Item_FieldItems_SpawnEvery20s_Max20)
{
	GameWorld world = MakeWorld(1);
	Teleport(world, A, Parking);
	const size_t start = CountKind(world, EntityKind::FieldItem);
	Run(world, 20000);
	CHECK_EQ(CountKind(world, EntityKind::FieldItem), start + 1);	// 20초마다 1개 (I-14)
	Run(world, 20000 * 20);
	CHECK(CountKind(world, EntityKind::FieldItem) <= GameData::FieldItemMax);
}
