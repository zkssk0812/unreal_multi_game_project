#include "ServerCore/CorePch.h"

#include "TestFramework.h"
#include "WorldTestUtil.h"

// 무기 (psd.md M4): 무기 칸 규칙 W-01~W-10, 판정 모양, 레일건, 수류탄, 지뢰

using namespace WorldTest;

namespace {

GameWorld Arena(int players = 2)
{
	GameWorld world = MakeWorld(players);
	ClearArena(world);
	Teleport(world, A, HallW, 0.0f);
	if (players > 1)
		Teleport(world, B, HallE, 180.0f);		// A 앞 10m
	world.TakeEvents();
	return world;
}

uint16_t DropWeapon(GameWorld& world, WeaponId weapon, Vec3 pos)
{
	return world.AddNow(EntityKind::WeaponPickup, pos, static_cast<uint16_t>(weapon), GameData::WeaponOf(weapon)->ammo);
}

} // namespace

// ───────────────── 무기 칸 ─────────────────

TEST_CASE(Weapon_PickUp_FillsEmptySlot)
{
	GameWorld world = Arena(1);
	const uint16_t pistol = DropWeapon(world, WeaponId::Pistol, Offset(HallW, 200, 0));
	CHECK(!Act(world, A, ActionType::InteractBegin, pistol).has_value());		// 3m 안이면 즉시 (IN-03)
	const Entity* a = world.ThiefOf(A);
	CHECK(a->weapons[0].id == WeaponId::Baton && a->weapons[1].id == WeaponId::Pistol && a->weapons[1].ammo == 12);
	CHECK(world.Find(pistol) == nullptr);
}

TEST_CASE(Weapon_PickUp_TooFar_OutOfRange)
{
	GameWorld world = Arena(1);
	const uint16_t pistol = DropWeapon(world, WeaponId::Pistol, Offset(HallW, 400, 0));
	const auto reject = Act(world, A, ActionType::InteractBegin, pistol);
	CHECK(reject && *reject == RejectReason::OutOfRange);
}

TEST_CASE(Weapon_Full_ReplacesSlot1_OldStaysOnGround)
{
	GameWorld world = Arena(1);
	Give(world, A, WeaponId::Sword, 0);
	Give(world, A, WeaponId::Pistol, 1);
	const uint16_t railgun = DropWeapon(world, WeaponId::Railgun, Offset(HallW, 100, 0));
	CHECK(!Act(world, A, ActionType::InteractBegin, railgun).has_value());
	const Entity* a = world.ThiefOf(A);
	CHECK(a->weapons[0].id == WeaponId::Railgun && a->weapons[1].id == WeaponId::Pistol);		// W-03
	const uint16_t left = Nth(world, EntityKind::WeaponPickup);
	CHECK(left != 0 && world.Find(left)->typeId == static_cast<uint16_t>(WeaponId::Sword));	// 원래 무기1은 그 자리에
	CHECK(world.Find(left)->pos.x == Offset(HallW, 100, 0).x);
}

TEST_CASE(Weapon_Full_ReplacedBaton_Vanishes)
{
	GameWorld world = Arena(1);
	Give(world, A, WeaponId::Pistol, 1);
	const uint16_t sword = DropWeapon(world, WeaponId::Sword, Offset(HallW, 100, 0));
	Act(world, A, ActionType::InteractBegin, sword);
	CHECK(world.ThiefOf(A)->weapons[0].id == WeaponId::Sword);
	CHECK_EQ(CountKind(world, EntityKind::WeaponPickup), size_t{ 0 });	// 삼단봉은 땅에 남지 않음 (W-10)
}

TEST_CASE(Weapon_OutOfAmmo_BatonFillsSlot)
{
	GameWorld world = Arena();
	Give(world, A, WeaponId::Sword, 0);
	Give(world, A, WeaponId::Taser, 1);		// 1발
	Attack(world, A, 0.0f);
	const Entity* a = world.ThiefOf(A);
	CHECK(a->weapons[0].id == WeaponId::Sword && a->weapons[1].id == WeaponId::Baton);	// W-09
}

TEST_CASE(Weapon_OutOfAmmo_BatonAlreadyHeld_SlotEmptied)
{
	GameWorld world = Arena();
	Give(world, A, WeaponId::Taser, 1);		// [삼단봉, 테이저건]
	Attack(world, A, 0.0f);
	const Entity* a = world.ThiefOf(A);
	CHECK(a->weapons[0].id == WeaponId::Baton && a->weapons[1].id == WeaponId::None && a->activeSlot == 0);	// W-10
	const auto reject = Act(world, A, ActionType::SwitchWeapon);
	CHECK(reject && *reject == RejectReason::OnlyOneWeapon);		// CT-05
}

TEST_CASE(Weapon_Switch_TogglesActiveSlot)
{
	GameWorld world = Arena(1);
	Give(world, A, WeaponId::Pistol, 1, false);
	CHECK(!Act(world, A, ActionType::SwitchWeapon).has_value());
	CHECK(world.ThiefOf(A)->ActiveWeapon() == WeaponId::Pistol);
	Act(world, A, ActionType::SwitchWeapon);
	CHECK(world.ThiefOf(A)->ActiveWeapon() == WeaponId::Baton);
}

// ───────────────── 판정 ─────────────────

TEST_CASE(Weapon_Pistol_HitsFirstTargetOnly)
{
	GameWorld world = MakeWorld(3);
	ClearArena(world);
	Teleport(world, A, HallW, 0.0f);
	Teleport(world, B, Offset(HallW, 500, 0));
	Teleport(world, C, Offset(HallW, 900, 0));		// B 뒤
	Give(world, A, WeaponId::Pistol);
	Attack(world, A, 0.0f);
	CHECK_EQ(world.ThiefOf(B)->hp, 90);
	CHECK_EQ(world.ThiefOf(C)->hp, 100);				// 단일: 처음 맞은 하나
	CHECK_EQ(world.ThiefOf(A)->weapons[1].ammo, uint16_t{ 11 });
}

TEST_CASE(Weapon_Pistol_BlockedByWall)
{
	// Gallery1 동쪽 벽 (x 1880~1920, 높이 400). 벽 너머(Study2)에서 쏘면 안 맞는다 (A-11)
	GameWorld world = MakeWorld(2);
	ClearArena(world);
	Teleport(world, A, { 1700.0f, -3000.0f, 0.0f }, 0.0f);
	Teleport(world, B, { 2100.0f, -3000.0f, 0.0f });
	Give(world, A, WeaponId::Pistol);
	Attack(world, A, 0.0f);
	CHECK_EQ(world.ThiefOf(B)->hp, 100);
}

TEST_CASE(Weapon_Flamethrower_30PerSecond_Cone)
{
	GameWorld world = Arena();
	Teleport(world, B, Offset(HallW, 400, 0));
	Give(world, A, WeaponId::Flamethrower);
	Act(world, A, ActionType::AttackStart, 0, 0.0f);
	Run(world, 1000);
	Act(world, A, ActionType::AttackStop);
	const int dealt = 100 - world.ThiefOf(B)->hp;
	CHECK(dealt >= 58 && dealt <= 62);			// 2 × 30회 = 60 (W-14)
}

TEST_CASE(Weapon_Railgun_SixHits_IgnoresWalls_LocksMovement)
{
	GameWorld world = MakeWorld(2);
	ClearArena(world);
	Teleport(world, A, { 3300.0f, -4000.0f, 0.0f }, 0.0f);	// 벽 너머
	Teleport(world, B, { 3800.0f, -4000.0f, 0.0f });
	world.ThiefOf(B)->hp = 1000;							// 끝까지 맞게
	Give(world, A, WeaponId::Railgun);
	Act(world, A, ActionType::AttackStart, 0, 0.0f);
	world.Update(0.05f);
	Act(world, A, ActionType::AttackStop);
	CHECK((world.ThiefOf(A)->state & StateBit::RailgunLock) != 0);
	Teleport(world, A, { 0, 0, 0 }, 90.0f);					// 잠금 중 이동·회전 무시 (E-10)
	CHECK(world.ThiefOf(A)->pos.x == 3300.0f && world.ThiefOf(A)->yaw == 0.0f);
	CHECK_EQ(world.ThiefOf(A)->weapons[1].ammo, uint16_t{ 1 });		// 탄약은 누른 순간
	Run(world, 900);
	CHECK_EQ(world.ThiefOf(B)->hp, 1000);					// 충전 1초 동안은 판정 없음
	CHECK((world.ThiefOf(A)->state & StateBit::RailgunLock) != 0);
	Run(world, 600);
	CHECK((world.ThiefOf(A)->state & StateBit::RailgunLock) != 0);	// 발사 중에도 잠금 (충전+발사 2초)
	Run(world, 600);
	CHECK_EQ(world.ThiefOf(B)->hp, 1000 - 120);				// 20 × 6 (W-17)
	CHECK((world.ThiefOf(A)->state & StateBit::RailgunLock) == 0);
}

TEST_CASE(Weapon_Railgun_DownedWhileCharging_NoShot)
{
	GameWorld world = MakeWorld(2);
	ClearArena(world);
	Teleport(world, A, { 3300.0f, -4000.0f, 0.0f }, 0.0f);
	Teleport(world, B, { 3400.0f, -4000.0f, 0.0f }, 180.0f);	// A 바로 앞, A를 봄
	world.ThiefOf(B)->hp = 1000;
	Give(world, A, WeaponId::Railgun);
	Act(world, A, ActionType::AttackStart, 0, 0.0f);
	world.Update(0.05f);
	Act(world, A, ActionType::AttackStop);
	Run(world, 500);
	world.ThiefOf(A)->shield = 0;
	world.ThiefOf(A)->hp = 1;
	Attack(world, B, 180.0f);								// 충전 중 삼단봉에 쓰러짐
	CHECK((world.ThiefOf(A)->state & StateBit::Downed) != 0);
	Run(world, 2000);
	CHECK_EQ(world.ThiefOf(B)->hp, 1000);					// 발사되지 않음
}

TEST_CASE(Weapon_Grenade_FalloffAndThrowerImmune)
{
	GameWorld world = MakeWorld(3);
	ClearArena(world);
	Teleport(world, A, HallW, 0.0f);
	const Vec3 land = Offset(HallW, 600, 0);
	Teleport(world, B, Offset(land, 0, 150));		// 폭발 중심에서 1.5m → 120
	Teleport(world, C, Offset(land, 0, 450));		// 4.5m → 120 − 20×3 = 60
	world.ThiefOf(B)->hp = 500;
	world.ThiefOf(C)->hp = 500;
	Give(world, A, WeaponId::Grenade);
	Attack(world, A, 0.0f, land);
	std::vector<WorldEvent> events;
	RunUntil(world, 1500, [&] { return Count<Msg::Explosion>(events) > 0; }, &events);
	CHECK(First<Msg::Projectile>(events).has_value() || Count<Msg::Explosion>(events) == 1);
	CHECK_EQ(world.ThiefOf(B)->hp, 500 - 120);
	CHECK_EQ(world.ThiefOf(C)->hp, 500 - 60);
	CHECK_EQ(world.ThiefOf(A)->hp, 100);				// 던진 사람 피해 없음
}

TEST_CASE(Weapon_BlastMine_ArmsDetects_HurtsOwnerToo)
{
	GameWorld world = Arena();
	Give(world, A, WeaponId::BlastMine);
	Attack(world, A, 0.0f);
	const uint16_t mine = Nth(world, EntityKind::Mine);
	CHECK(mine != 0 && world.Find(mine)->ownerEntity == world.ThiefOf(A)->id);
	Run(world, 3000);
	CHECK((world.Find(mine)->state & StateBit::Armed) != 0);		// 3초 뒤 작동
	CHECK((world.Find(mine)->state & StateBit::Triggered) != 0);	// 설치자가 위에 서 있음 → 감지 (Q-26)
	Run(world, 600);
	CHECK(world.Find(mine) == nullptr);
	CHECK_EQ(world.ThiefOf(A)->hp, 100 - 80);						// 1m 안 80
}

TEST_CASE(Weapon_ShockMine_StunsGuard)
{
	GameWorld world = Arena(1);
	Give(world, A, WeaponId::ShockMine);
	Attack(world, A, 0.0f);
	Teleport(world, A, FarAway);
	Run(world, 3000);
	const uint16_t guard = PlaceGuard(world, Offset(HallW, 50, 0), 0.0f);
	Run(world, 600);
	const Entity* g = world.Find(guard);
	CHECK((g->state & StateBit::Stunned) != 0);			// 경비원도 걸림, 2초 경직 (W-20)
	CHECK_EQ(g->hp, 200 - 20);
}
