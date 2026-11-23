#include "ServerCore/CorePch.h"

#include "TestFramework.h"
#include "WorldTestUtil.h"

// 돈·금고·보물 (psd.md M2): 개인 반납, 쓰러짐 손실과 떨어뜨림, 금고 10초, 보물 순환·가치 상승

using namespace WorldTest;

namespace {

void GoTo(GameWorld& world, uint64_t playerId, uint16_t target)
{
	Teleport(world, playerId, world.Find(target)->interactPos);
}

void OpenSafe(GameWorld& world, uint64_t playerId)
{
	const uint16_t safe = Nth(world, EntityKind::Safe);
	GoTo(world, playerId, safe);
	Act(world, playerId, ActionType::InteractBegin, safe);
	Run(world, GameData::SafeOpenMs);
}

// 반납 구역에 가서 4초 머물러 반납
void DeliverAt(GameWorld& world, uint64_t playerId, uint16_t point)
{
	GoTo(world, playerId, point);
	Act(world, playerId, ActionType::InteractBegin, point);
	Run(world, GameData::ReturnDeliverMs + 100);
}

// A가 환금 아이템 25를 들고 첫 반납 구역 위에 선 상태 (경비원·상자는 치움)
uint16_t CarryToPoint(GameWorld& world)
{
	ClearArena(world);
	world.ThiefOf(A)->carried = { { ValuableQuality::Common, 25 } };
	const uint16_t point = Nth(world, EntityKind::ReturnPoint);
	GoTo(world, A, point);
	world.TakeEvents();
	return point;
}

} // namespace

TEST_CASE(Money_PickUpValuable_DeliverToOwnBank)
{
	GameWorld world = MakeWorld(2);
	ClearArena(world);
	Teleport(world, A, HallW);
	const uint16_t vase = world.AddNow(EntityKind::Valuable, Offset(HallW, 100, 0), static_cast<uint16_t>(ValuableQuality::Common), 25);
	CHECK(!Act(world, A, ActionType::InteractBegin, vase).has_value());		// F로 즉시 (I-01)
	CHECK_EQ(world.ThiefOf(A)->CarriedValue(), uint32_t{ 25 });
	world.TakeEvents();

	const uint16_t point = Nth(world, EntityKind::ReturnPoint);
	GoTo(world, A, point);
	CHECK(!Act(world, A, ActionType::InteractBegin, point).has_value());
	Run(world, GameData::ReturnDeliverMs + 100);
	CHECK_EQ(world.ThiefOf(A)->banked, uint32_t{ 25 });		// 개인 금액 (M-02, O-23)
	CHECK_EQ(world.ThiefOf(B)->banked, uint32_t{ 0 });
	CHECK(world.ThiefOf(A)->carried.empty());
	const auto events = world.TakeEvents();
	const auto money = First<Msg::Money>(events);
	CHECK(money && money->thieves.size() == 2 && money->thieves[0].banked == 25);
	CHECK(world.Find(point) == nullptr);						// 반납하면 지점은 다른 방으로 (O-24)

	const uint16_t next = Nth(world, EntityKind::ReturnPoint);
	GoTo(world, A, next);
	const auto empty = Act(world, A, ActionType::InteractBegin, next);
	CHECK(empty && *empty == RejectReason::InvalidState);		// 반납할 것이 없음
}

TEST_CASE(Money_ReturnPoint_Three_Warns5sBeforeReplace_30s)
{
	GameWorld world = MakeWorld(2);
	CHECK_EQ(CountKind(world, EntityKind::ReturnPoint), size_t{ 3 });	// 3곳 (클라 2026-11-23)
	const uint16_t point = Nth(world, EntityKind::ReturnPoint);
	Run(world, 24000);
	CHECK((world.Find(point)->state & StateBit::Warning) == 0);	// 24초: 아직 경고 없음
	Run(world, 1100);
	CHECK((world.Find(point)->state & StateBit::Warning) != 0);	// 25초: 빨강 (O-22, 마지막 5초)
	Run(world, 5000);
	CHECK(world.Find(point) == nullptr);						// 30초: 다른 방으로
	CHECK_EQ(CountKind(world, EntityKind::ReturnPoint), size_t{ 3 });
}

TEST_CASE(Money_Return_Takes4s_SendsProgress)
{
	GameWorld world = MakeWorld(2);
	const uint16_t point = CarryToPoint(world);
	CHECK(!Act(world, A, ActionType::InteractBegin, point).has_value());
	const auto begin = First<Msg::Interact>(world.TakeEvents());
	CHECK(begin && begin->state == InteractState::Begin && begin->target == point &&
		begin->progressMs == 0 && begin->durationMs == GameData::ReturnDeliverMs);	// 클라 게이지
	CHECK((world.ThiefOf(A)->state & StateBit::Interacting) != 0);

	Run(world, 3900);
	CHECK_EQ(world.ThiefOf(A)->banked, uint32_t{ 0 });			// 4초가 안 됨
	CHECK(world.Find(point) != nullptr);
	const auto events = RunCollect(world, 200);
	CHECK_EQ(world.ThiefOf(A)->banked, uint32_t{ 25 });
	const auto done = First<Msg::Interact>(events);
	CHECK(done && done->state == InteractState::Complete && done->target == point);
	CHECK(world.Find(point) == nullptr);						// 완료하면 바로 다른 방으로
	CHECK((world.ThiefOf(A)->state & StateBit::Interacting) == 0);
}

TEST_CASE(Money_Return_ToolsPerk_StillFourSeconds)
{
	GameWorld world = MakeWorld(2, false, { { GameData::Perk::Tools } });
	const uint16_t point = CarryToPoint(world);
	Act(world, A, ActionType::InteractBegin, point);
	Run(world, 3800);											// 작업 속도 +10%면 3.64초에 끝났을 것
	CHECK_EQ(world.ThiefOf(A)->banked, uint32_t{ 0 });
	Run(world, 300);
	CHECK_EQ(world.ThiefOf(A)->banked, uint32_t{ 25 });
}

TEST_CASE(Money_Return_OneAtATime)
{
	GameWorld world = MakeWorld(2);
	const uint16_t point = CarryToPoint(world);
	world.ThiefOf(B)->carried = { { ValuableQuality::Common, 30 } };
	GoTo(world, B, point);
	Act(world, A, ActionType::InteractBegin, point);
	const auto busy = Act(world, B, ActionType::InteractBegin, point);
	CHECK(busy && *busy == RejectReason::TargetBusy);			// 먼저 시작한 사람만
	Run(world, GameData::ReturnDeliverMs + 100);
	CHECK_EQ(world.ThiefOf(A)->banked, uint32_t{ 25 });
	CHECK_EQ(world.ThiefOf(B)->banked, uint32_t{ 0 });
}

TEST_CASE(Money_Return_LeavingZone_CancelsAndResets)
{
	GameWorld world = MakeWorld(2);
	const uint16_t point = CarryToPoint(world);
	Act(world, A, ActionType::InteractBegin, point);
	Run(world, 3000);
	world.TakeEvents();
	Teleport(world, A, Offset(world.Find(point)->interactPos, 300, 0));	// 구역 밖
	const auto events = RunCollect(world, 100);
	const auto cancel = First<Msg::Interact>(events);
	CHECK(cancel && cancel->state == InteractState::Cancel && cancel->progressMs == 0);
	CHECK((world.ThiefOf(A)->state & StateBit::Interacting) == 0);

	GoTo(world, A, point);
	Act(world, A, ActionType::InteractBegin, point);
	const auto again = First<Msg::Interact>(world.TakeEvents());
	CHECK(again && again->state == InteractState::Begin && again->progressMs == 0);	// 처음부터
	Run(world, 3000);
	CHECK_EQ(world.ThiefOf(A)->banked, uint32_t{ 0 });
	Run(world, 1100);
	CHECK_EQ(world.ThiefOf(A)->banked, uint32_t{ 25 });
}

TEST_CASE(Money_Return_CancelledByHit_Attack_Dodge_FCancel)
{
	GameWorld world = MakeWorld(2);
	const uint16_t point = CarryToPoint(world);
	const auto interrupted = [&](auto&& interrupt) {
		GoTo(world, A, point);
		Act(world, A, ActionType::InteractBegin, point);
		Run(world, 1000);
		interrupt();
		const bool cancelled = (world.ThiefOf(A)->state & StateBit::Interacting) == 0;
		Run(world, 2000);										// 경직·쿨타임이 풀리도록
		world.TakeEvents();
		return cancelled && world.ThiefOf(A)->banked == 0;
	};

	CHECK(interrupted([&] { Act(world, A, ActionType::InteractCancel); }));		// F 취소
	CHECK(interrupted([&] { Act(world, A, ActionType::Dodge); }));
	CHECK(interrupted([&] { Attack(world, A, 0.0f); }));							// 내가 공격
	CHECK(interrupted([&] {
		Teleport(world, B, Offset(world.ThiefOf(A)->pos, 150, 0), 180.0f);
		Attack(world, B, 180.0f);												// 맞음 (IN-05, 금고·상자와 같음)
		Teleport(world, B, FarAway);
	}));
	CHECK_EQ(world.ThiefOf(A)->carried.size(), size_t{ 1 });					// 들고 있던 것은 그대로
}

TEST_CASE(Money_Return_NoBuzzerBeater)
{
	GameWorld world = MakeWorld(2);
	const uint16_t point = CarryToPoint(world);
	Run(world, GameData::ReturnPointLifeMs - GameData::ReturnMinLeftMs);	// 2초 남음
	const auto late = Act(world, A, ActionType::InteractBegin, point);
	CHECK(late && *late == RejectReason::NotAvailable);			// 2초 이하 남으면 시작 불가
	CHECK((world.ThiefOf(A)->state & StateBit::Interacting) == 0);
}

TEST_CASE(Money_Return_ZoneClosesMidway_Fails)
{
	GameWorld world = MakeWorld(2);
	const uint16_t point = CarryToPoint(world);
	Run(world, GameData::ReturnPointLifeMs - 3000);			// 3초 남음: 시작은 되지만 4초가 안 됨
	CHECK(!Act(world, A, ActionType::InteractBegin, point).has_value());
	world.TakeEvents();
	const auto events = RunCollect(world, 3100);
	CHECK(world.Find(point) == nullptr);						// 구역이 닫힘
	const auto cancel = First<Msg::Interact>(events);
	CHECK(cancel && cancel->state == InteractState::Cancel && cancel->target == point);
	CHECK_EQ(world.ThiefOf(A)->banked, uint32_t{ 0 });			// 반납 실패
	CHECK_EQ(world.ThiefOf(A)->carried.size(), size_t{ 1 });
	CHECK((world.ThiefOf(A)->state & StateBit::Interacting) == 0);
	CHECK_EQ(world.ThiefOf(A)->interactTarget, uint16_t{ 0 });
}

TEST_CASE(Money_DownByPlayer_DropsAllValuables_Loses10Percent)
{
	GameWorld world = MakeWorld(2);
	ClearArena(world);
	Teleport(world, A, HallW, 0.0f);
	Teleport(world, B, Offset(HallW, 150, 0));
	Entity* b = world.ThiefOf(B);
	b->carried = { { ValuableQuality::Rare, 300 }, { ValuableQuality::Unique, 2000 } };
	b->banked = 500;
	b->consumable = ConsumableId::Smoke;
	b->hp = 5;
	Attack(world, A, 0.0f);
	CHECK((b->state & StateBit::Downed) != 0);
	CHECK_EQ(b->banked, uint32_t{ 450 });
	CHECK_EQ(CountKind(world, EntityKind::Valuable), size_t{ 2 });	// 그 자리에 떨어뜨림 (D-02)
	CHECK(b->consumable == ConsumableId::None);						// 소비 아이템 소멸 (D-08)
	CHECK(b->carried.empty());
}

TEST_CASE(Money_DownByGuard_OnlyTreasureDrops_Loses40Percent)
{
	GameWorld world = MakeWorld(1, true);
	ClearArena(world);
	Teleport(world, A, HallW);
	PlaceGuard(world, Offset(HallW, 500, 0), 180.0f);		// A를 정면으로 봄, 5m
	Entity* a = world.ThiefOf(A);
	a->carried = { { ValuableQuality::Rare, 300 }, { ValuableQuality::Unique, 2000 } };
	a->banked = 1000;
	a->hp = 10;
	std::vector<WorldEvent> events;
	CHECK(RunUntil(world, 2000, [&] { return (a->state & StateBit::Downed) != 0; }, &events));
	const auto downed = First<Msg::Downed>(events);
	CHECK(downed && downed->cause == DownCause::Guard && downed->respawnMs == 10000);
	CHECK_EQ(a->banked, uint32_t{ 600 });							// −40%
	CHECK_EQ(CountKind(world, EntityKind::Valuable), size_t{ 1 });	// 보물만 남음 (D-02, Q-22)
	const uint16_t left = Nth(world, EntityKind::Valuable);
	CHECK(world.Find(left)->typeId == static_cast<uint16_t>(ValuableQuality::Unique) && world.Find(left)->param == 2000);
}

TEST_CASE(Money_Safe_Opens10s_ProgressKept)
{
	GameWorld world = MakeWorld(2);
	const uint16_t safe = Nth(world, EntityKind::Safe);
	GoTo(world, A, safe);
	CHECK(!Act(world, A, ActionType::InteractBegin, safe).has_value());
	GoTo(world, B, safe);
	const auto busy = Act(world, B, ActionType::InteractBegin, safe);
	CHECK(busy && *busy == RejectReason::TargetBusy);			// 한 명씩 (O-04)
	Run(world, 4000);
	Act(world, A, ActionType::InteractCancel);
	CHECK((world.Find(safe)->state & StateBit::Opened) == 0);

	CHECK(!Act(world, B, ActionType::InteractBegin, safe).has_value());	// 다른 사람이 이어서
	Run(world, 5900);
	CHECK((world.Find(safe)->state & StateBit::Opened) == 0);
	Run(world, 150);
	CHECK((world.Find(safe)->state & StateBit::Opened) != 0);	// 4초 + 6초 = 10초 (O-02)
	CHECK_EQ(CountKind(world, EntityKind::Valuable), size_t{ 1 });	// 보물이 나타남 (O-03)
}

TEST_CASE(Money_Safe_ToolsPerkFaster)
{
	GameWorld world = MakeWorld(1, false, { { GameData::Perk::Tools } });
	const uint16_t safe = Nth(world, EntityKind::Safe);
	GoTo(world, A, safe);
	Act(world, A, ActionType::InteractBegin, safe);
	Run(world, 9100);										// 10초 ÷ 1.1 ≈ 9.09초 (K-03)
	CHECK((world.Find(safe)->state & StateBit::Opened) != 0);
}

TEST_CASE(Money_Safe_TooFar_OutOfRange)
{
	GameWorld world = MakeWorld(1);
	const uint16_t safe = Nth(world, EntityKind::Safe);
	Teleport(world, A, Offset(world.Find(safe)->interactPos, 200, 0));	// 금고는 1m 안에서 (Q-33)
	const auto reject = Act(world, A, ActionType::InteractBegin, safe);
	CHECK(reject && *reject == RejectReason::OutOfRange);
}

TEST_CASE(Money_Treasure_TakeRemovesSafe_DeliverRaisesValue_NewSafeElsewhere)
{
	GameWorld world = MakeWorld(1);
	const uint16_t safe = Nth(world, EntityKind::Safe);
	const uint16_t firstRoom = world.Find(safe)->safeRoom;
	OpenSafe(world, A);
	world.TakeEvents();

	const uint16_t treasure = Nth(world, EntityKind::Valuable);
	CHECK(!Act(world, A, ActionType::InteractBegin, treasure).has_value());
	const auto events = world.TakeEvents();
	const auto taken = First<Msg::TreasureTaken>(events);
	CHECK(taken && taken->value == 2000);							// 전원 메시지 (I-23)
	CHECK(world.Find(safe) == nullptr);								// 금고 사라짐 (O-05)
	CHECK_EQ(CountKind(world, EntityKind::Safe), size_t{ 0 });		// 보물이 있는 동안 금고 없음 (O-06)
	CHECK((world.ThiefOf(A)->state & StateBit::CarryingTreasure) != 0);

	DeliverAt(world, A, Nth(world, EntityKind::ReturnPoint));
	CHECK_EQ(world.ThiefOf(A)->banked, uint32_t{ 2000 });
	CHECK_EQ(world.ThiefOf(A)->treasuresDelivered, uint8_t{ 1 });
	CHECK_EQ(world.TreasureValue(), uint32_t{ 2400 });			// +20% (I-22)
	const uint16_t newSafe = Nth(world, EntityKind::Safe);
	CHECK(newSafe != 0 && world.Find(newSafe)->safeRoom != firstRoom);	// 직전 금고 방 제외 (O-07)
}

TEST_CASE(Money_TreasureValue_CompoundRoundUpTens)
{
	// 2,000 → 2,400 → 2,880 → 3,460 → 4,160 → 5,000 (Q-23)
	uint32_t v = GameData::TreasureFirstValue;
	const uint32_t expected[] = { 2400, 2880, 3460, 4160, 5000 };
	for (uint32_t e : expected)
	{
		v = GameData::NextTreasureValue(v);
		CHECK_EQ(v, e);
	}
}
