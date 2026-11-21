#include "ServerCore/CorePch.h"

#include "TestFramework.h"
#include "WorldTestUtil.h"

// 전투 기반 (psd.md M1): HP·대미지 순서, 꾹 누르기, 쓰러짐·리스폰, 경비원 자가 수리

using namespace WorldTest;

namespace {

// A는 HallW에서 +X를 보고, B는 A 앞 1.5m (삼단봉 사거리 2m 안)
GameWorld Duel(std::vector<std::vector<uint16_t>> perks = {})
{
	GameWorld world = MakeWorld(2, false, std::move(perks));
	ClearArena(world);
	Teleport(world, A, HallW, 0.0f);
	Teleport(world, B, Offset(HallW, 150, 0), 180.0f);
	world.TakeEvents();
	return world;
}

} // namespace

TEST_CASE(Combat_Baton_HitsPlayerInFront_Damage10)
{
	GameWorld world = Duel();
	Act(world, A, ActionType::AttackStart, 0, 0.0f);
	world.Update(0.05f);
	const auto events = world.TakeEvents();
	const auto damage = First<Msg::Damage>(events);
	CHECK(damage && damage->target == world.ThiefOf(B)->id && damage->amount == 10 && damage->hpAfter == 90);
	CHECK(First<Msg::Fire>(events) && First<Msg::Fire>(events)->weapon == WeaponId::Baton);
	CHECK(world.ThiefOf(A)->stamina == GameData::StaminaMax);		// 공격은 SP를 쓰지 않는다 (Q-08)
}

TEST_CASE(Combat_HoldToAttack_TwicePerSecond)
{
	GameWorld world = Duel();
	Act(world, A, ActionType::AttackStart, 0, 0.0f);
	const auto events = RunCollect(world, 950);		// 0초·0.5초 (1.0초째는 다음 주기)
	CHECK_EQ(Count<Msg::Damage>(events), size_t{ 2 });		// 삼단봉 초당 2회 (C-02, W-11)
	Act(world, A, ActionType::AttackStop);
	CHECK_EQ(Count<Msg::Damage>(RunCollect(world, 1000)), size_t{ 0 });
}

TEST_CASE(Combat_BehindOrOutOfRange_Misses)
{
	GameWorld world = Duel();
	Attack(world, A, 180.0f);					// 등 뒤로 휘두름 (부채꼴 좌우 60° 밖)
	CHECK_EQ(world.ThiefOf(B)->hp, 100);
	Teleport(world, B, Offset(HallW, 300, 0));	// 3m: 삼단봉 2m 밖
	Run(world, 500);
	Attack(world, A, 0.0f);
	CHECK_EQ(world.ThiefOf(B)->hp, 100);
}

TEST_CASE(Combat_ShieldThenHp_DeathSaveOnce)
{
	GameWorld world = Duel({ {}, { GameData::Perk::Vest, GameData::Perk::DeathSave } });
	Entity* b = world.ThiefOf(B);
	Attack(world, A, 0.0f);
	CHECK(b->shield == 10 && b->hp == 100);		// 보호막이 먼저 (C-05)
	Run(world, 500);
	Attack(world, A, 0.0f);
	Run(world, 500);
	Attack(world, A, 0.0f);
	CHECK(b->shield == 0 && b->hp == 90);

	b->hp = 5;
	Run(world, 500);
	Attack(world, A, 0.0f);
	CHECK(b->hp == 1 && !b->deathSaveLeft && (b->state & StateBit::Downed) == 0);	// 가시회생 (K-02)
	Run(world, 500);
	Attack(world, A, 0.0f);
	CHECK((b->state & StateBit::Downed) != 0);
}

TEST_CASE(Combat_DownByPlayer_Respawn5s_AtSpawn_Invulnerable)
{
	GameWorld world = Duel();
	world.ThiefOf(B)->hp = 10;
	world.ThiefOf(B)->banked = 1000;
	Attack(world, A, 0.0f);
	const auto events = world.TakeEvents();
	const auto downed = First<Msg::Downed>(events);
	CHECK(downed && downed->cause == DownCause::Player && downed->lostBanked == 100 && downed->respawnMs == 5000);
	CHECK_EQ(world.ThiefOf(B)->banked, uint32_t{ 900 });		// −10% (D-04)
	CHECK_EQ(world.ThiefOf(A)->kills, uint16_t{ 1 });

	// 쓰러진 동안 행동 불가
	const auto act = Act(world, B, ActionType::Dodge);
	CHECK(act && *act == RejectReason::Downed);

	Run(world, 4900);
	CHECK((world.ThiefOf(B)->state & StateBit::Downed) != 0);
	Run(world, 150);
	const Entity* b = world.ThiefOf(B);
	CHECK((b->state & StateBit::Downed) == 0 && (b->state & StateBit::Invulnerable) != 0);
	CHECK(b->hp == 100 && b->weapons[0].id == WeaponId::Baton);
	CHECK(OnRoomCell(DefaultMap(), b->pos));		// 방 안 무작위 칸에서 리스폰 (D-06, A-26)

	// 리스폰 무적 5초 동안은 맞지 않는다 (C-06)
	Teleport(world, B, Offset(HallW, 150, 0));
	Attack(world, A, 0.0f);
	CHECK_EQ(world.ThiefOf(B)->hp, 100);
	Run(world, 5000);
	Attack(world, A, 0.0f);
	CHECK_EQ(world.ThiefOf(B)->hp, 90);
}

TEST_CASE(Combat_Dodging_NotHit)
{
	GameWorld world = Duel();
	Act(world, B, ActionType::Dodge);
	Attack(world, A, 0.0f);
	CHECK_EQ(world.ThiefOf(B)->hp, 100);
}

TEST_CASE(Combat_GuardHp_RepairsAtZero_Then200)
{
	GameWorld world = MakeWorld(1);
	ClearArena(world);
	Teleport(world, A, HallW);
	const uint16_t guard = PlaceGuard(world, Offset(HallW, 150, 0), 180.0f);
	world.Find(guard)->hp = 10;
	Attack(world, A, 0.0f);
	const Entity* g = world.Find(guard);
	CHECK((g->state & StateBit::Repairing) != 0 && (g->state & StateBit::Invulnerable) != 0);	// N-06
	Run(world, 500);
	Attack(world, A, 0.0f);
	CHECK_EQ(g->hp, 0);						// 수리 중 무적
	Run(world, 4600);
	CHECK((g->state & StateBit::Repairing) == 0 && g->hp == 200);
}

TEST_CASE(Combat_StunnedThief_CannotAct)
{
	GameWorld world = Duel();
	Give(world, A, WeaponId::Taser);
	Attack(world, A, 0.0f);				// 테이저건 1발, 2초 경직 (W-12)
	CHECK((world.ThiefOf(B)->state & StateBit::Stunned) != 0);
	const auto act = Act(world, B, ActionType::AttackStart);
	CHECK(act && *act == RejectReason::Stunned);
	Run(world, 2000);
	CHECK((world.ThiefOf(B)->state & StateBit::Stunned) == 0);
}
