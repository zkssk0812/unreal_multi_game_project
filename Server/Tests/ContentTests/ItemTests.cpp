#include "ServerCore/CorePch.h"

#include <cmath>
#include <map>
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

// ───────────────── 상자 드롭 비율 (클라 2026-11-22) ─────────────────

// 같은 종류 상자 n개를 삼단봉으로 하나씩 부수고, 나온 것을 (종류, typeId)별로 센다
static std::map<std::pair<EntityKind, uint16_t>, int> BreakBoxes(BoxType type, int n)
{
	GameWorld world = Arena(1);
	const Vec3 spot = Offset(HallW, 100, 0);
	std::map<std::pair<EntityKind, uint16_t>, int> drops;
	for (int i = 0; i < n; ++i)
	{
		world.AddNow(EntityKind::Box, spot, static_cast<uint16_t>(type));
		world.TakeEvents();
		Attack(world, A, 0.0f);
		Run(world, 500);
		Attack(world, A, 0.0f);									// 삼단봉 10 × 2 = 상자 HP 20
		for (const WorldEvent& event : RunCollect(world, 500))
			if (const auto* spawn = std::get_if<Msg::EntitySpawn>(&event))
				for (const Msg::EntityInfo& info : spawn->entities)
					if (info.kind != EntityKind::Box && info.kind != EntityKind::FieldItem)	// 다시 생긴 상자·20초 회복약 제외
						++drops[{ info.kind, info.typeId }];
		for (const Entity& e : world.Entities())				// 나온 것은 치운다 (다음 상자 판정에 끼지 않게)
			if (e.kind != EntityKind::Thief && e.kind != EntityKind::Guard && e.kind != EntityKind::ReturnPoint)
				world.Find(e.id)->removed = true;
	}
	return drops;
}

static int CountOf(const std::map<std::pair<EntityKind, uint16_t>, int>& drops, EntityKind kind)
{
	int n = 0;
	for (const auto& [key, count] : drops)
		if (key.first == kind)
			n += count;
	return n;
}

TEST_CASE(Box_WeaponBox_Weapon80_Consumable20_PistolTaserCommon_RailgunFlameRare)
{
	// 무기 상자 600개: 무기 80% · 소비 아이템 20%, 그 밖에는 안 나옴. 권총·테이저건 각 20%, 레일건·화염방사기 각 5% (클라 2026-11-24)
	constexpr int boxes = 600;
	auto drops = BreakBoxes(BoxType::Weapon, boxes);
	const int weapons = CountOf(drops, EntityKind::WeaponPickup);
	const int consumables = CountOf(drops, EntityKind::ConsumablePickup);
	CHECK_EQ(weapons + consumables, boxes);						// 빈 상자 없음, 환금 아이템 없음
	CHECK_EQ(CountOf(drops, EntityKind::Valuable), 0);
	CHECK(consumables > boxes * 0.14 && consumables < boxes * 0.26);
	CHECK_EQ((drops[{ EntityKind::WeaponPickup, static_cast<uint16_t>(WeaponId::Baton) }]), 0);
	const auto byWeapon = [&](WeaponId w) { return drops[{ EntityKind::WeaponPickup, static_cast<uint16_t>(w) }]; };
	const int common = byWeapon(WeaponId::Pistol) + byWeapon(WeaponId::Taser);
	const int rare = byWeapon(WeaponId::Railgun) + byWeapon(WeaponId::Flamethrower);
	CHECK(common > 3 * rare);									// 기대값 40% 대 10%
	CHECK(rare > 0);
}

TEST_CASE(Box_ValuableBox_OnlyValuables_Common60_Advanced30_Rare10)
{
	constexpr int boxes = 600;
	auto drops = BreakBoxes(BoxType::Valuable, boxes);
	const auto quality = [&](ValuableQuality q) { return drops[{ EntityKind::Valuable, static_cast<uint16_t>(q) }]; };
	CHECK_EQ(CountOf(drops, EntityKind::Valuable), boxes);		// 항상 환금 아이템 하나
	CHECK_EQ(CountOf(drops, EntityKind::WeaponPickup) + CountOf(drops, EntityKind::ConsumablePickup), 0);
	CHECK(quality(ValuableQuality::Common) > boxes * 0.52 && quality(ValuableQuality::Common) < boxes * 0.68);
	CHECK(quality(ValuableQuality::Advanced) > boxes * 0.22 && quality(ValuableQuality::Advanced) < boxes * 0.38);
	CHECK(quality(ValuableQuality::Rare) > boxes * 0.05 && quality(ValuableQuality::Rare) < boxes * 0.15);
	CHECK_EQ(quality(ValuableQuality::Unique), 0);				// 보물은 금고에서만
}

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
		size_t valuableBoxes = 0;
		for (const Entity& e : world.Entities())
			if (e.kind == EntityKind::Box)
			{
				CHECK(e.typeId == static_cast<uint16_t>(BoxType::Weapon) || e.typeId == static_cast<uint16_t>(BoxType::Valuable));
				valuableBoxes += e.typeId == static_cast<uint16_t>(BoxType::Valuable);
			}
		CHECK_EQ(valuableBoxes, GameData::ValuableBoxCount);	// 무기 상자 14 : 환금 상자 6 (클라 2026-11-24)
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
	const uint16_t box = world.AddNow(EntityKind::Box, spot, static_cast<uint16_t>(BoxType::Valuable));
	Attack(world, A, 0.0f);
	CHECK(world.Find(box) != nullptr && world.Find(box)->hp == 10);
	Run(world, 500);
	Attack(world, A, 0.0f);
	CHECK(world.Find(box) == nullptr);					// 누적 20 → 부서짐 (O-11, A-02)
	Run(world, 29000);
	CHECK(!BoxAt(world, spot));
	Run(world, 31000);
	CHECK(BoxAt(world, spot));							// 30~60초 뒤 같은 자리 (O-14)
	for (const Entity& e : world.Entities())
		if (e.kind == EntityKind::Box && e.pos.x == spot.x && e.pos.y == spot.y)
			CHECK_EQ(e.typeId, static_cast<uint16_t>(BoxType::Valuable));	// 같은 종류로
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

TEST_CASE(Item_Box_AlwaysDropsExactlyOneItem)
{
	// 두 종류 모두 빈 상자는 없고 아이템은 딱 하나 (클라 2026-11-24)
	GameWorld world = Arena(1);
	for (int i = 0; i < 30; ++i)		// 30 × 0.55초 < 20초: 필드 아이템 생성·상자 재생성과 겹치지 않게
	{
		const size_t before = world.Entities().size();
		const BoxType type = i % 2 == 0 ? BoxType::Weapon : BoxType::Valuable;
		const uint16_t box = world.AddNow(EntityKind::Box, Offset(HallW, 100, 0), static_cast<uint16_t>(type));
		world.Find(box)->hp = 1;
		Attack(world, A, 0.0f);
		Run(world, 500);
		CHECK_EQ(world.Entities().size(), before + 1);	// 상자 하나가 아이템 하나로
		// 나온 아이템을 치운다 (다음 상자 판정과 섞이지 않게)
		for (const Entity& e : world.Entities())
			if (e.kind != EntityKind::Thief && e.kind != EntityKind::Guard && e.pos.x == Offset(HallW, 100, 0).x)
				world.Find(e.id)->pos = Parking;
	}
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
