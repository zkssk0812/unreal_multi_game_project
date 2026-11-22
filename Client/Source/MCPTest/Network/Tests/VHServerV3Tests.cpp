// 서버 v3.0 기준 자동화 테스트 (에디터: 창 → 테스트 자동화 → VaultHeist.V3)
// 규칙 판정은 서버가 하므로, 클라가 책임지는 것만 시험한다: 패킷 크기 검증, 결과(순위·최고 기록) 표시, 표시용 이름 표.
#include "Misc/AutomationTest.h"
#include "Network/VHPacket.h"
#include "Network/VHNetTypes.h"
#include "Network/VHNetSubsystem.h"
#include "Gameplay/VHRanking.h"
#include "Gameplay/VHMatchSubsystem.h"
#include "Gameplay/VHInteractionSubsystem.h"
#include "Gameplay/VHServerGameSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace RobProto;

namespace
{
	/**
	 * 가변 길이 패킷(마지막 멤버가 배열) 읽기 검증:
	 * 크기가 *_Size(count)와 정확히 같을 때만 받고, 개수 초과·크기 불일치·헤더보다 짧은 것은 버린다.
	 * (서버도 같은 규칙으로 클라를 끊는다. 클라는 끊지 않고 그 패킷만 버린다)
	 */
	template <typename T, typename CountSetter, typename CountGetter, typename SizeFunc>
	void CheckVariablePacket(FAutomationTestBase& Test, const TCHAR* Name, PacketId Id, CountSetter SetCount, CountGetter GetCount, SizeFunc SizeOf)
	{
		TUniquePtr<T> Packet = MakeUnique<T>(VHPacket::Make<T>(Id));
		TUniquePtr<T> Out = MakeUnique<T>();
		for (int32 Count : { 0, 1, 3, MAX_ENTITIES })
		{
			SetCount(*Packet, static_cast<uint8>(Count));
			const TArray<uint8> Bytes = VHPacket::ToBytes(*Packet, SizeOf(static_cast<uint8>(Count)));
			const bool bOk = VHPacket::ReadVariable(Bytes.GetData(), Bytes.Num(), *Out, MAX_ENTITIES, GetCount, SizeOf);
			Test.TestTrue(FString::Printf(TEXT("%s: %d개 받기"), Name, Count), bOk && static_cast<int32>(GetCount(*Out)) == Count);
		}
		SetCount(*Packet, 3);
		{
			const TArray<uint8> Short = VHPacket::ToBytes(*Packet, SizeOf(2));
			Test.TestFalse(FString::Printf(TEXT("%s: 3개라고 했는데 2개 크기 → 버림"), Name),
				VHPacket::ReadVariable(Short.GetData(), Short.Num(), *Out, MAX_ENTITIES, GetCount, SizeOf));
			const TArray<uint8> Long = VHPacket::ToBytes(*Packet, SizeOf(4));
			Test.TestFalse(FString::Printf(TEXT("%s: 3개라고 했는데 4개 크기 → 버림"), Name),
				VHPacket::ReadVariable(Long.GetData(), Long.Num(), *Out, MAX_ENTITIES, GetCount, SizeOf));
		}
		{
			SetCount(*Packet, 200);	// MAX_ENTITIES(128) 초과
			const TArray<uint8> Over = VHPacket::ToBytes(*Packet);
			Test.TestFalse(FString::Printf(TEXT("%s: 개수 초과 → 버림"), Name),
				VHPacket::ReadVariable(Over.GetData(), Over.Num(), *Out, MAX_ENTITIES, GetCount, SizeOf));
			Test.TestFalse(FString::Printf(TEXT("%s: 헤더보다 짧음 → 버림"), Name),
				VHPacket::ReadVariable(Over.GetData(), 3, *Out, MAX_ENTITIES, GetCount, SizeOf));
		}
	}
}

// ───────────────────────── 프로토콜 v3.0 상수와 패킷 크기 ─────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVHV3ProtocolTest, "VaultHeist.V3.Protocol",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVHV3ProtocolTest::RunTest(const FString& Parameters)
{
	// 서버와 정확히 같아야 하는 값 (protocol.h 를 바꿔 끼웠을 때 알아채기 위한 확인)
	TestEqual(TEXT("프로토콜 버전 3.0"), static_cast<uint32>(PROTOCOL_VERSION), static_cast<uint32>(0x00030000));
	TestEqual(TEXT("서버 포트"), static_cast<int32>(PORT), 9000);
	TestEqual(TEXT("제한 시간 5분"), static_cast<int32>(GAME_DURATION_MS), 300000);
	TestEqual(TEXT("결과 화면 15초"), static_cast<int32>(RESULT_SCREEN_MS), 15000);
	TestEqual(TEXT("방 인원 3"), static_cast<int32>(MAX_ROOM_PLAYERS), 3);
	TestEqual(TEXT("퍽 3개"), static_cast<int32>(MAX_PERKS), 3);
	TestEqual(TEXT("핑 2초"), static_cast<int32>(PING_INTERVAL_MS), 2000);

	// 가변 길이 게임 패킷 4종
	CheckVariablePacket<SC_GameStart>(*this, TEXT("SC_GAME_START"), PacketId::SC_GAME_START,
		[](SC_GameStart& P, uint8 N) { P.entity_count = N; }, [](const SC_GameStart& P) { return P.entity_count; }, SC_GameStart_Size);
	CheckVariablePacket<SC_Snapshot>(*this, TEXT("SC_SNAPSHOT"), PacketId::SC_SNAPSHOT,
		[](SC_Snapshot& P, uint8 N) { P.entity_count = N; }, [](const SC_Snapshot& P) { return P.entity_count; }, SC_Snapshot_Size);
	CheckVariablePacket<SC_EntitySpawn>(*this, TEXT("SC_ENTITY_SPAWN"), PacketId::SC_ENTITY_SPAWN,
		[](SC_EntitySpawn& P, uint8 N) { P.count = N; }, [](const SC_EntitySpawn& P) { return P.count; }, SC_EntitySpawn_Size);
	CheckVariablePacket<SC_EntityDespawn>(*this, TEXT("SC_ENTITY_DESPAWN"), PacketId::SC_ENTITY_DESPAWN,
		[](SC_EntityDespawn& P, uint8 N) { P.count = N; }, [](const SC_EntityDespawn& P) { return P.count; }, SC_EntityDespawn_Size);

	// 고정 길이 SC_GAME_END: 결과 한 줄(이름·반납 금액·순위·최고 기록)이 바이트를 거쳐도 그대로인가
	SC_GameEnd End = VHPacket::Make<SC_GameEnd>(PacketId::SC_GAME_END);
	End.match_id = 7;
	End.reason = EndReason::TimeUp;
	End.duration_ms = GAME_DURATION_MS;
	End.player_count = 2;
	End.result_screen_ms = RESULT_SCREEN_MS;
	VHPacket::WriteString(End.players[0].name, sizeof(End.players[0].name), TEXT("Alice"));
	End.players[0].banked_value = 109938;
	End.players[0].rank = 1;
	End.players[0].best_record = 120000;
	End.players[0].new_record = false;
	VHPacket::WriteString(End.players[1].name, sizeof(End.players[1].name), TEXT("도둑고양이"));
	End.players[1].banked_value = 29353;
	End.players[1].rank = 2;
	End.players[1].best_record = 29353;
	End.players[1].new_record = true;
	End.players[1].left_early = true;
	const TArray<uint8> Bytes = VHPacket::ToBytes(End);
	SC_GameEnd Out;
	FMemory::Memzero(&Out, sizeof(Out));
	TestTrue(TEXT("SC_GAME_END 받기"), VHPacket::ReadFixed(Bytes.GetData(), Bytes.Num(), Out));
	TestFalse(TEXT("SC_GAME_END 1바이트 모자람 → 버림"), VHPacket::ReadFixed(Bytes.GetData(), Bytes.Num() - 1, Out));
	TestEqual(TEXT("이름"), VHPacket::ReadString(Out.players[0].name, sizeof(Out.players[0].name)), FString(TEXT("Alice")));
	TestEqual(TEXT("한글 이름"), VHPacket::ReadString(Out.players[1].name, sizeof(Out.players[1].name)), FString(TEXT("도둑고양이")));
	TestEqual(TEXT("반납 금액"), static_cast<int32>(Out.players[0].banked_value), 109938);
	TestEqual(TEXT("최고 기록"), static_cast<int32>(Out.players[0].best_record), 120000);
	TestTrue(TEXT("갱신 표시·중도 이탈"), !Out.players[0].new_record && Out.players[1].new_record && Out.players[1].left_early);
	TestEqual(TEXT("결과 화면 시간"), static_cast<int32>(Out.result_screen_ms), static_cast<int32>(RESULT_SCREEN_MS));
	return true;
}

// ───────────────────────── 결과창 순위 (같은 금액은 같은 순위) ─────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVHV3ResultRankingTest, "VaultHeist.V3.ResultRanking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVHV3ResultRankingTest::RunTest(const FString& Parameters)
{
	// 서버 A-20: 같은 금액이면 같은 등수, 다음 등수는 건너뜀. 클라는 서버 순위가 비어 있을 때(샘플 결과)만 직접 매긴다
	TestTrue(TEXT("{900,900,500} → 1,1,3"), VHRanking::CompetitionRanks({ 900, 900, 500 }) == TArray<int32>({ 1, 1, 3 }));
	TestTrue(TEXT("{500,900,900} → 3,1,1"), VHRanking::CompetitionRanks({ 500, 900, 900 }) == TArray<int32>({ 3, 1, 1 }));
	TestTrue(TEXT("{100,200,300} → 3,2,1"), VHRanking::CompetitionRanks({ 100, 200, 300 }) == TArray<int32>({ 3, 2, 1 }));
	TestTrue(TEXT("{900,500,500} → 1,2,2"), VHRanking::CompetitionRanks({ 900, 500, 500 }) == TArray<int32>({ 1, 2, 2 }));
	TestTrue(TEXT("모두 0원 → 모두 1위"), VHRanking::CompetitionRanks({ 0, 0, 0 }) == TArray<int32>({ 1, 1, 1 }));
	TestEqual(TEXT("빈 목록"), VHRanking::CompetitionRanks(TArray<int32>()).Num(), 0);
	TestEqual(TEXT("공동 표시"), VHRanking::RankLabel(1, true), FString(TEXT("공동 1")));
	TestEqual(TEXT("혼자 1위"), VHRanking::RankLabel(1, false), FString(TEXT("1")));
	TestEqual(TEXT("금액 표시"), VHRanking::Money(109938), FString(TEXT("$109,938")));
	TestEqual(TEXT("같은 등수 세기"), VHRanking::CountRank({ 1, 1, 3 }, 1), 2);

	TArray<FVHPlayerResult> R;
	auto Add = [&R](const TCHAR* Name, int32 Value) { FVHPlayerResult& P = R.AddDefaulted_GetRef(); P.Name = Name; P.ValueDelivered = Value; };
	Add(TEXT("Coco"), 0);
	Add(TEXT("Me"), 5200);
	Add(TEXT("Bella"), 5200);
	VHRankResults(R);
	TestTrue(TEXT("결과 1,1,3"), R.Num() == 3 && R[0].Rank == 1 && R[1].Rank == 1 && R[2].Rank == 3);
	TestTrue(TEXT("공동 1위는 원래 순서"), R.Num() == 3 && R[0].Name == TEXT("Me") && R[1].Name == TEXT("Bella"));
	TestTrue(TEXT("3위 = Coco"), R.Num() == 3 && R[2].Name == TEXT("Coco"));
	return true;
}

// ───────────────────────── 표시용 이름 표 (서버 ID → 한국어 이름) ─────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVHV3DisplayTablesTest, "VaultHeist.V3.DisplayTables",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVHV3DisplayTablesTest::RunTest(const FString& Parameters)
{
	// 서버가 ID만 보내므로 이름·설명은 클라 표에서 찾는다. 서버에 종류가 늘었는데 표를 안 고치면 여기서 걸린다
	auto CheckNames = [this](const TCHAR* What, int32 First, int32 Last, TFunctionRef<FString(int32)> NameOf)
	{
		TSet<FString> Seen;
		for (int32 Id = First; Id <= Last; ++Id)
		{
			const FString Name = NameOf(Id);
			TestFalse(FString::Printf(TEXT("%s %d 이름 있음"), What, Id), Name.IsEmpty());
			TestFalse(FString::Printf(TEXT("%s %d 이름이 겹치지 않음 (%s)"), What, Id, *Name), Seen.Contains(Name));
			Seen.Add(Name);
		}
	};
	CheckNames(TEXT("무기"), static_cast<int32>(WeaponId::Baton), static_cast<int32>(WeaponId::ShockMine),
		[](int32 Id) { return FString(UVHServerGameSubsystem::WeaponName(static_cast<WeaponId>(Id))); });
	CheckNames(TEXT("소비 아이템"), static_cast<int32>(ConsumableId::HealKit), static_cast<int32>(ConsumableId::Jetpack),
		[](int32 Id) { return FString(UVHServerGameSubsystem::ConsumableName(static_cast<ConsumableId>(Id))); });
	CheckNames(TEXT("필드 아이템"), static_cast<int32>(FieldItemId::PotionS), static_cast<int32>(FieldItemId::PotionL),
		[](int32 Id) { return FString(UVHServerGameSubsystem::FieldItemName(static_cast<FieldItemId>(Id))); });
	CheckNames(TEXT("스킬"), static_cast<int32>(SkillId::Taser), static_cast<int32>(SkillId::Cloak),
		[](int32 Id) { return FString(UVHServerGameSubsystem::SkillName(static_cast<SkillId>(Id))); });

	for (int32 Id = static_cast<int32>(WeaponId::Baton); Id <= static_cast<int32>(WeaponId::ShockMine); ++Id)
	{
		TestFalse(FString::Printf(TEXT("무기 %d 대미지 문구 있음"), Id), FString(UVHServerGameSubsystem::WeaponDamageText(static_cast<WeaponId>(Id))).IsEmpty());
	}
	for (int32 Id = static_cast<int32>(ConsumableId::HealKit); Id <= static_cast<int32>(ConsumableId::Jetpack); ++Id)
	{
		TestFalse(FString::Printf(TEXT("소비 아이템 %d 효과 문구 있음"), Id), FString(UVHServerGameSubsystem::ConsumableEffectText(static_cast<ConsumableId>(Id))).IsEmpty());
	}
	return true;
}

// ───────────────────────── 같은 틱의 이벤트로 이유 가리기 (작업 중단·은신 해제) ─────────────────────────
// 서버는 한 틱에 [이벤트들] → [스냅샷] 순서로 보낸다. 맞아서 멈출 때는 SC_INTERACT(Cancel) 다음에 SC_DAMAGE 가 온다 (정호님 답 2026-11-16)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVHV3TickCauseTest, "VaultHeist.V3.TickCause",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVHV3TickCauseTest::RunTest(const FString& Parameters)
{
	// 공격받아 멈춤: Cancel → 대미지 → 스냅샷
	{
		FVHTickCause T;
		T.OnInteractCancel(false);
		T.OnSelfDamage();
		TestTrue(TEXT("Cancel 뒤 같은 틱의 대미지 → 공격받아 멈춤"), T.EndTick(false, false));
		TestFalse(TEXT("판정 뒤에는 비워진다"), T.EndTick(false, false));
	}
	// 대미지가 Cancel보다 먼저: 서버 순서상 이 대미지는 Cancel의 원인이 아니다
	{
		FVHTickCause T;
		T.OnSelfDamage();
		T.OnInteractCancel(false);
		TestFalse(TEXT("대미지 → Cancel 순서는 공격받아 멈춘 것이 아님"), T.EndTick(false, false));
	}
	// 이전 틱의 대미지 + 이번 틱의 Cancel(범위 이탈 등)
	{
		FVHTickCause T;
		T.OnSelfDamage();
		T.EndTick(false, false);
		T.OnInteractCancel(false);
		TestFalse(TEXT("이전 틱에 맞고 이번 틱에 멈춘 것은 안내하지 않음"), T.EndTick(false, false));
	}
	// Cancel과 대미지가 서로 다른 틱
	{
		FVHTickCause T;
		T.OnInteractCancel(false);
		TestFalse(TEXT("Cancel만 온 틱"), T.EndTick(false, false));
		T.OnSelfDamage();
		TestFalse(TEXT("다음 틱의 대미지는 그 Cancel과 무관"), T.EndTick(false, false));
	}
	// 내가 공격·구르기·직접 취소를 보내서 멈춤 (같은 틱에 우연히 맞아도 안내하지 않음)
	{
		FVHTickCause T;
		T.OnInteractCancel(true);
		T.OnSelfDamage();
		TestFalse(TEXT("내 행동으로 멈춘 Cancel"), T.EndTick(false, false));
	}
	// 쓰러짐: Cancel → 대미지 → SC_DOWNED
	{
		FVHTickCause T;
		T.OnInteractCancel(false);
		T.OnSelfDamage();
		T.OnSelfDowned();
		TestFalse(TEXT("쓰러져서 멈춤 (SC_DOWNED)"), T.EndTick(false, true));
		T.OnInteractCancel(false);
		T.OnSelfDamage();
		TestFalse(TEXT("쓰러져서 멈춤 (스냅샷의 Downed 비트만 보고도)"), T.EndTick(false, true));
	}
	// 경직(감전 지뢰 등: 대미지와 경직이 같은 틱)
	{
		FVHTickCause T;
		T.OnInteractCancel(false);
		T.OnSelfDamage();
		TestFalse(TEXT("경직으로 멈춤"), T.EndTick(true, false));
	}
	// 은신: 상호작용이 실제로 시작된 틱에만 "상호작용해서 풀림"
	{
		FVHTickCause T;
		TestFalse(TEXT("F만 누르고 확인이 없으면(거절) 상호작용 때문이 아님"), T.WasInteractConfirmed());
		T.OnInteractConfirmed();
		TestTrue(TEXT("작업 시작·줍기·반납이 확인된 틱"), T.WasInteractConfirmed());
		T.EndTick(false, false);
		TestFalse(TEXT("다음 틱에는 남지 않는다"), T.WasInteractConfirmed());
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
