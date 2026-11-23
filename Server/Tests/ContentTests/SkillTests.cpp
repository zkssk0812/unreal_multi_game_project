#include "ServerCore/CorePch.h"

#include "TestFramework.h"
#include "WorldTestUtil.h"

// 스킬·퍽 (psd.md M6): 캐릭터 고유 스킬, 쿨타임 시작 시점, 끈끈이 지연·설치자 감속, 은신 해제 조건

using namespace WorldTest;

namespace {

// A = 도적1(테이저), B = 도적2(끈끈이), C = 도적3(은신)
GameWorld Arena()
{
	GameWorld world = MakeWorld(3);
	ClearArena(world);
	Teleport(world, A, HallW, 0.0f);
	Teleport(world, B, Offset(HallW, 200, 0), 180.0f);
	Teleport(world, C, Offset(HallW, 0, 600), 0.0f);
	world.TakeEvents();
	return world;
}

} // namespace

TEST_CASE(Skill_Taser_QDraws_AttackFires_Stuns2s_CooldownFromFire)
{
	GameWorld world = Arena();
	CHECK(!Act(world, A, ActionType::UseSkill).has_value());			// Q: 들기만 (쏘지 않음)
	CHECK((world.ThiefOf(A)->state & StateBit::TaserReady) != 0);
	CHECK((world.ThiefOf(B)->state & StateBit::Stunned) == 0);
	const auto drawn = First<Msg::Skill>(world.TakeEvents());
	CHECK(drawn && drawn->phase == SkillPhase::Active && drawn->skill == SkillId::Taser);

	Attack(world, A, 0.0f);											// 공격 키: 무기 대신 테이저건
	CHECK((world.ThiefOf(B)->state & StateBit::Stunned) != 0);		// 3m 단일, 2초 경직 (S-01)
	CHECK_EQ(world.ThiefOf(B)->hp, 100);							// 삼단봉은 나가지 않음
	CHECK((world.ThiefOf(A)->state & StateBit::TaserReady) == 0);	// 쏘면 집어넣음
	const auto events = world.TakeEvents();
	const auto cast = First<Msg::Skill>(events);
	CHECK(cast && cast->phase == SkillPhase::Cast);
	CHECK(!First<Msg::Fire>(events));

	const auto again = Act(world, A, ActionType::UseSkill);
	CHECK(again && *again == RejectReason::Cooldown);
	Run(world, 30000);
	CHECK(!Act(world, A, ActionType::UseSkill).has_value());			// 쏜 직후부터 30초
}

TEST_CASE(Skill_Taser_HoldingAttack_FiresOnce_ThenWeaponNeedsNewPress)
{
	GameWorld world = Arena();
	Act(world, A, ActionType::UseSkill);
	Act(world, A, ActionType::AttackStart, 0, 0.0f);
	Run(world, 1000);												// 누른 채로 1초
	CHECK_EQ(world.ThiefOf(B)->hp, 100);							// 테이저 뒤 삼단봉이 이어서 나가지 않음
	Attack(world, A, 0.0f);											// 다시 누르면 무기
	CHECK_EQ(world.ThiefOf(B)->hp, 90);
}

TEST_CASE(Skill_Taser_QAgain_Holsters_NoCooldown)
{
	GameWorld world = Arena();
	Act(world, A, ActionType::UseSkill);
	world.TakeEvents();
	CHECK(!Act(world, A, ActionType::UseSkill).has_value());			// 다시 Q: 집어넣음
	CHECK((world.ThiefOf(A)->state & StateBit::TaserReady) == 0);
	const auto end = First<Msg::Skill>(world.TakeEvents());
	CHECK(end && end->phase == SkillPhase::End);
	CHECK(!Act(world, A, ActionType::UseSkill).has_value());			// 쿨타임 없이 바로 다시 들 수 있음
	Attack(world, A, 0.0f);
	CHECK((world.ThiefOf(B)->state & StateBit::Stunned) != 0);
}

TEST_CASE(Skill_Taser_WhileHeld_NoWeaponSwitch_NoConsumable)
{
	// 든 동안 무기 교체·소비 아이템은 Locked, 테이저는 계속 든다 (클라 2026-11-23)
	GameWorld world = Arena();
	Entity* a = world.ThiefOf(A);
	Act(world, A, ActionType::UseSkill);
	const auto alone = Act(world, A, ActionType::SwitchWeapon);	// 무기가 하나뿐이어도 OnlyOneWeapon이 아니라 Locked
	CHECK(alone && *alone == RejectReason::Locked);
	Give(world, A, WeaponId::Pistol, 1, false);
	const auto two = Act(world, A, ActionType::SwitchWeapon);
	CHECK(two && *two == RejectReason::Locked);
	CHECK(a->activeSlot == 0 && (a->state & StateBit::TaserReady) != 0);

	for (const ConsumableId item : { ConsumableId::Jetpack, ConsumableId::HealKit, ConsumableId::Smoke, ConsumableId::Flashbang })
	{
		a->consumable = item;
		a->hp = 50;
		const auto used = Act(world, A, ActionType::UseConsumable);
		CHECK(used && *used == RejectReason::Locked);
		CHECK(a->consumable == item && (a->state & StateBit::Jetpack) == 0);	// 쓰이지 않음
	}
	CHECK((a->state & StateBit::TaserReady) != 0);

	Act(world, A, ActionType::UseSkill);								// Q로 집어넣으면 둘 다 된다
	CHECK(!Act(world, A, ActionType::SwitchWeapon).has_value());
	CHECK(!Act(world, A, ActionType::UseConsumable).has_value());
}

TEST_CASE(Skill_Taser_StunCancels_StartsCooldown)
{
	GameWorld world = Arena();
	Act(world, A, ActionType::UseSkill);
	world.TakeEvents();
	Give(world, B, WeaponId::Taser);								// 줍는 무기 테이저건 (W-12)
	Attack(world, B, 180.0f);
	CHECK((world.ThiefOf(A)->state & StateBit::Stunned) != 0);
	CHECK((world.ThiefOf(A)->state & StateBit::TaserReady) == 0);	// 경직 → 집어넣음
	const auto events = world.TakeEvents();
	bool ended = false;
	for (const WorldEvent& e : events)
		if (const auto* skill = std::get_if<Msg::Skill>(&e); skill && skill->phase == SkillPhase::End)
			ended = true;
	CHECK(ended);
	Run(world, 2500);												// 경직이 풀린 뒤
	const auto again = Act(world, A, ActionType::UseSkill);
	CHECK(again && *again == RejectReason::Cooldown);				// 외부 요인 취소 → 쿨타임 30초
	Run(world, 28000);
	CHECK(!Act(world, A, ActionType::UseSkill).has_value());
}

TEST_CASE(Skill_Taser_OutOfRange_Misses)
{
	GameWorld world = Arena();
	Teleport(world, B, Offset(HallW, 400, 0));
	Act(world, A, ActionType::UseSkill);
	Attack(world, A, 0.0f);
	CHECK((world.ThiefOf(B)->state & StateBit::Stunned) == 0);
	CHECK((world.ThiefOf(A)->state & StateBit::TaserReady) == 0);	// 빗나가도 쏜 것 (쿨타임 시작)
}

TEST_CASE(Skill_Sticky_AfterOneSecond_BehindMovement_SlowsOwner)
{
	GameWorld world = Arena();
	Teleport(world, B, Offset(HallW, 300, 0), 0.0f, MoveFlag::Moving);	// +X로 1m 움직임
	CHECK(!Act(world, B, ActionType::UseSkill).has_value());
	Run(world, 900);
	CHECK_EQ(CountKind(world, EntityKind::StickyTrap), size_t{ 0 });		// 1초 동안 던지는 동작
	Run(world, 150);
	const uint16_t trap = Nth(world, EntityKind::StickyTrap);
	CHECK(trap != 0);
	if (trap)
		CHECK(std::fabs(world.Find(trap)->pos.x - Offset(HallW, 200, 0).x) < 1.0f);	// 움직인 방향 반대쪽 1m 뒤
	Run(world, 50);
	CHECK((world.ThiefOf(B)->state & StateBit::Slowed) != 0);		// 설치자도 느려짐 (Q-36)
	CHECK((world.ThiefOf(A)->state & StateBit::Slowed) != 0);		// A는 3m 안
	Run(world, 10000);
	CHECK_EQ(CountKind(world, EntityKind::StickyTrap), size_t{ 0 });	// 10초
}

TEST_CASE(Skill_Sticky_SlowsGuard)
{
	GameWorld world = Arena();
	const uint16_t guard = PlaceGuard(world, Offset(HallW, 200, 0), 0.0f);
	Act(world, B, ActionType::UseSkill);
	Run(world, 1100);
	CHECK((world.Find(guard)->state & StateBit::Slowed) != 0);
}

TEST_CASE(Skill_Cloak_ToggleOff_CooldownStartsAtEnd)
{
	GameWorld world = Arena();
	CHECK(!Act(world, C, ActionType::UseSkill).has_value());
	CHECK((world.ThiefOf(C)->state & StateBit::Cloaked) != 0);
	const auto dodge = Act(world, C, ActionType::Dodge);
	CHECK(dodge && *dodge == RejectReason::InvalidState);			// 은신 중 특수 동작 불가
	Run(world, 5000);
	CHECK(!Act(world, C, ActionType::UseSkill).has_value());		// 다시 누르면 해제
	CHECK((world.ThiefOf(C)->state & StateBit::Cloaked) == 0);
	Run(world, 29000);
	const auto cooling = Act(world, C, ActionType::UseSkill);
	CHECK(cooling && *cooling == RejectReason::Cooldown);			// 해제한 때부터 30초 (S-03)
	Run(world, 1100);
	CHECK(!Act(world, C, ActionType::UseSkill).has_value());
}

TEST_CASE(Skill_Cloak_EndsAfter20s)
{
	GameWorld world = Arena();
	Act(world, C, ActionType::UseSkill);
	Run(world, 19900);
	CHECK((world.ThiefOf(C)->state & StateBit::Cloaked) != 0);
	Run(world, 150);
	CHECK((world.ThiefOf(C)->state & StateBit::Cloaked) == 0);
}

TEST_CASE(Skill_Cloak_BrokenByAttackingOrBeingHit)
{
	GameWorld world = Arena();
	Act(world, C, ActionType::UseSkill);
	Act(world, C, ActionType::AttackStart, 0, 0.0f);
	CHECK((world.ThiefOf(C)->state & StateBit::Cloaked) == 0);		// 내가 공격하면 풀림 (Q-30)
	Act(world, C, ActionType::AttackStop);

	Run(world, 30000);
	Teleport(world, C, Offset(HallW, 150, 0));
	Act(world, C, ActionType::UseSkill);
	CHECK((world.ThiefOf(C)->state & StateBit::Cloaked) != 0);
	Teleport(world, B, FarAway);
	Attack(world, A, 0.0f);
	CHECK((world.ThiefOf(C)->state & StateBit::Cloaked) == 0);		// 공격받으면 풀림
}

TEST_CASE(Skill_Cloak_BrokenByInteracting)
{
	GameWorld world = Arena();
	const uint16_t box = world.AddNow(EntityKind::Box, Offset(world.ThiefOf(C)->pos, 100, 0));
	Act(world, C, ActionType::UseSkill);
	CHECK(!Act(world, C, ActionType::InteractBegin, box).has_value());
	CHECK((world.ThiefOf(C)->state & StateBit::Cloaked) == 0);		// 상호작용하면 풀림
}

TEST_CASE(Skill_EveryCharacterHasSkill)
{
	// 도적1~3 모두 고유 스킬이 있다 (도적4·5는 2026-11-22에 없어짐)
	for (uint16_t id = 1; id <= GameData::CharacterCount; ++id)
		CHECK(GameData::SkillOf(id) != SkillId::None);
}

TEST_CASE(Skill_RespawnResetsCooldown)
{
	GameWorld world = Arena();
	Act(world, A, ActionType::UseSkill);
	world.ThiefOf(A)->hp = 5;
	Teleport(world, B, Offset(HallW, 150, 0), 180.0f);
	Run(world, 2000);										// B 경직이 풀리길 기다림
	Attack(world, B, 180.0f);
	CHECK((world.ThiefOf(A)->state & StateBit::Downed) != 0);
	Run(world, 5100);
	CHECK(!Act(world, A, ActionType::UseSkill).has_value());	// 리스폰하면 쿨타임 초기화 (D-07)
}
