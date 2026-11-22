// 상호작용 순수 로직·판정 흐름 자동화 테스트 (에디터: 창 → 테스트 자동화 → VaultHeist.Interaction)
#include "Misc/AutomationTest.h"
#include "Gameplay/VHInteractionRules.h"
#include "Gameplay/VHInteractionConfig.h"
#include "Gameplay/VHInteractionSubsystem.h"
#include "Gameplay/VHInteractionAuthority.h"
#include "Gameplay/VHInteractionComponent.h"
#include "Gameplay/VHThiefStateComponent.h"
#include "Gameplay/VHTreasureCarrierComponent.h"
#include "Gameplay/VHSafe.h"
#include "Gameplay/VHDroppedTreasure.h"
#include "Gameplay/VHFlashlightComponent.h"
#include "Gameplay/VHStatusEffectComponent.h"
#include "Gameplay/VHMatchSubsystem.h"
#include "Gameplay/VHReturnPoint.h"
#include "Network/VHSettings.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

// ───────────────────────── 진행도 ─────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVHHoldProgressTest, "VaultHeist.Interaction.HoldProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVHHoldProgressTest::RunTest(const FString& Parameters)
{
	// 금고 30초: 10초 누르고 취소(유지) → 다시 누르면 20초 남음
	FVHHoldProgress P;
	TestEqual(TEXT("처음 남은 시간 30초"), P.Begin(100.0, 30.f), 30.f);
	TestEqual(TEXT("10초 뒤 1/3"), P.GetProgress(110.0), 1.f / 3.f, 0.001f);
	P.Cancel(110.0, true);
	TestFalse(TEXT("취소 후 진행 안 함"), P.bRunning);
	TestEqual(TEXT("취소해도 진행도 유지"), P.GetProgress(500.0), 1.f / 3.f, 0.001f);
	TestEqual(TEXT("다시 시작하면 20초 남음"), P.Begin(200.0, 30.f), 20.f, 0.001f);
	TestFalse(TEXT("19초 뒤 아직"), P.IsComplete(219.0));
	TestTrue(TEXT("20초 뒤 완료"), P.IsComplete(220.0));
	TestEqual(TEXT("완료 후 1을 넘지 않음"), P.GetProgress(999.0), 1.f);

	// 5초 홀드: 취소하면 초기화
	FVHHoldProgress R;
	R.Begin(0.0, 5.f);
	R.Cancel(4.0, false);
	TestEqual(TEXT("초기화 취소 → 0"), R.SavedProgress, 0.f);
	TestEqual(TEXT("다시 5초"), R.Begin(10.0, 5.f), 5.f);

	// 시간 설정이 바뀌어도 비율 유지 (30초 중 절반 → 60초로 바뀌면 30초 남음)
	FVHHoldProgress Q;
	Q.Begin(0.0, 30.f);
	Q.Cancel(15.0, true);
	TestEqual(TEXT("비율 유지"), Q.Begin(20.0, 60.f), 30.f, 0.001f);

	// 서버 방식: 시작 시각 + 남은 시간으로 복원
	FVHHoldProgress S;
	S.BeginWithRemaining(1000.0, 20.f, 30.f);
	TestEqual(TEXT("서버 정보로 복원한 진행도"), S.GetProgress(1000.0), 1.f / 3.f, 0.001f);
	TestTrue(TEXT("서버 정보 남은 시간 뒤 완료"), S.IsComplete(1020.0));
	return true;
}

// ───────────────────────── 대상 고르기 점수 (소리·공격 범위 판정은 서버 몫이라 삭제) ─────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVHRulesTest, "VaultHeist.Interaction.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVHRulesTest::RunTest(const FString& Parameters)
{
	// 대상 고르기 점수
	TestTrue(TEXT("반경 밖 제외"), VHRules::TargetScore(160.f, 0.f, 150.f, 75.f) < 0.f);
	TestTrue(TEXT("등 뒤 제외"), VHRules::TargetScore(50.f, 120.f, 150.f, 75.f) < 0.f);
	TestTrue(TEXT("밟고 있으면 각도 무관"), VHRules::TargetScore(0.f, 170.f, 150.f, 75.f) >= 0.f);
	TestTrue(TEXT("가깝고 정면이 우선"),
		VHRules::TargetScore(30.f, 5.f, 150.f, 75.f) < VHRules::TargetScore(120.f, 60.f, 150.f, 75.f));
	return true;
}

// ───────────────────────── 안내(제안) 계산과 서버 결과 반영 (가상 월드) ─────────────────────────
// 규칙 판정은 서버가 한다. 클라가 책임지는 것만 시험한다:
//  ① 대상별 안내(방식·가능 여부) ② 서버가 없으면 요청을 거절로 안내 ③ 서버 결과(SC_INTERACT)를 화면 상태에 반영

namespace
{
	struct FTestThief
	{
		AActor* Actor = nullptr;
		UVHInteractionComponent* Interaction = nullptr;
		UVHTreasureCarrierComponent* Carrier = nullptr;
		UVHThiefStateComponent* State = nullptr;
		UVHStatusEffectComponent* Status = nullptr;
	};

	FTestThief SpawnThief(UWorld* World, const FVector& Location)
	{
		FTestThief T;
		T.Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location));
		USceneComponent* Root = NewObject<USceneComponent>(T.Actor, TEXT("Root"));
		T.Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		T.Actor->SetActorLocation(Location);
		T.Carrier = NewObject<UVHTreasureCarrierComponent>(T.Actor, TEXT("Carrier"));
		T.Carrier->RegisterComponent();
		T.State = NewObject<UVHThiefStateComponent>(T.Actor, TEXT("State"));
		T.State->RegisterComponent();
		T.Interaction = NewObject<UVHInteractionComponent>(T.Actor, TEXT("Interaction"));
		T.Interaction->RegisterComponent();
		T.Status = NewObject<UVHStatusEffectComponent>(T.Actor, TEXT("Status"));
		T.Status->RegisterComponent();
		return T;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVHInteractOfferTest, "VaultHeist.Interaction.Offer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVHInteractOfferTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("VHInteractionTestWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	if (!World->HasBegunPlay())
	{
		// 게임모드 없는 테스트 월드: 액터 BeginPlay(→ 등록부 등록)를 직접 시작
		World->GetWorldSettings()->NotifyBeginPlay();
	}

	UVHInteractionSubsystem* Sub = World->GetSubsystem<UVHInteractionSubsystem>();
	if (!TestNotNull(TEXT("서브시스템"), Sub))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	UVHInteractionAuthority* Auth = Sub->GetAuthority();
	Sub->NowOverride = 0.0;

	AVHSafe* Safe = World->SpawnActor<AVHSafe>(AVHSafe::StaticClass(), FTransform(FVector(150.f, 0.f, 0.f)));
	AVHReturnPoint* Point = World->SpawnActor<AVHReturnPoint>(AVHReturnPoint::StaticClass(), FTransform(FVector(0.f, 400.f, 0.f)));
	FTestThief A = SpawnThief(World, FVector(0.f, 0.f, 0.f));
	FTestThief B = SpawnThief(World, FVector(0.f, 60.f, 0.f));
	TestEqual(TEXT("등록된 의적 2명"), Sub->GetThieves().Num(), 2);
	TestTrue(TEXT("금고·반납지점이 등록부에 있음"), Sub->GetInteractables().Contains(Safe) && Sub->GetInteractables().Contains(Point));

	// 1) 닫힌 금고 = 꾹(작업) 안내, 진행도 유지
	FVHInteractOffer Offer = Auth->Evaluate(A.Interaction, Safe);
	TestTrue(TEXT("닫힌 금고 = 작업 안내"), Offer.IsAvailable() && Offer.Mode == EVHInteractMode::Hold && Offer.bKeepProgressOnCancel);

	// 2) 서버가 없으면(기본 담당) 요청은 거절 안내로 끝나고, 아무 상태도 바뀌지 않는다
	bool bGotMessage = false;
	A.Interaction->OnMessage.AddLambda([&bGotMessage](const FText&, bool bIsError) { bGotMessage = bGotMessage || bIsError; });
	Auth->RequestBegin(A.Interaction, Safe);
	TestTrue(TEXT("서버 없음 → 안내 문구"), bGotMessage);
	TestNull(TEXT("서버 없음 → 금고를 쓰는 사람 없음"), Safe->GetActiveUser());
	TestTrue(TEXT("서버 없음 → 금고는 닫힌 그대로"), Safe->GetSafeState() == EVHSafeState::Closed);

	// 3) 서버 결과 반영: A가 금고 작업 시작(전체 10초) → 다른 의적에게는 "사용 중"
	Sub->ApplyInteract(A.Interaction, Safe, EVHInteractState::Begin, 0.0, 10.f, 10.f, EVHInteractMode::Hold, true, FText::GetEmpty());
	TestTrue(TEXT("작업 중인 사람 = A"), Safe->GetActiveUser() == A.Interaction);
	Offer = Auth->Evaluate(B.Interaction, Safe);
	TestTrue(TEXT("B에게는 사용 중으로 막힘"), Offer.bHasOffer && !Offer.IsAvailable() && Offer.Block == EVHInteractBlock::Busy);
	Sub->NowOverride = 4.0;
	TestEqual(TEXT("4초 뒤 진행도 40%"), Safe->GetHoldProgress().GetProgress(4.0), 0.4f, 0.01f);

	// 4) 취소(진행도 유지) → 다시 시작하면 남은 시간만큼만
	Sub->ApplyInteract(A.Interaction, Safe, EVHInteractState::Cancel, 4.0, 0.f, 10.f, EVHInteractMode::Hold, true, FText::GetEmpty());
	TestNull(TEXT("취소 뒤 쓰는 사람 없음"), Safe->GetActiveUser());
	TestEqual(TEXT("취소해도 진행도 40% 유지"), Safe->GetHoldProgress().SavedProgress, 0.4f, 0.01f);

	// 5) 경직·쓰러짐 상태에서는 안내가 막힌다
	B.Status->ApplyStun(2.f);
	Offer = Auth->Evaluate(B.Interaction, Safe);
	TestTrue(TEXT("경직 중에는 할 수 없음"), Offer.bHasOffer && !Offer.IsAvailable());
	Sub->NowOverride = 7.0;
	B.Status->UpdateTimers();
	TestFalse(TEXT("2초 뒤 경직 끝"), B.Status->IsStunned());
	Sub->ApplyThiefStatus(B.State, EVHThiefStatus::Dead);
	Offer = Auth->Evaluate(B.Interaction, Safe);
	TestTrue(TEXT("쓰러져 있으면 할 수 없음"), Offer.bHasOffer && !Offer.IsAvailable());
	Sub->ApplyThiefStatus(B.State, EVHThiefStatus::Free);
	TestTrue(TEXT("리스폰 뒤 다시 가능"), Auth->Evaluate(B.Interaction, Safe).IsAvailable());

	// 6) 열린 금고에는 상호작용이 없다 (보물은 금고 앞에 따로 생기는 환금 아이템을 줍는다)
	Safe->ApplyOpened();
	TestFalse(TEXT("열린 금고 = 안내 없음"), Auth->Evaluate(A.Interaction, Safe).bHasOffer);

	// 7) 반납지점: 든 것이 없으면 막힘, 환금 아이템을 들면 가능
	Offer = Auth->Evaluate(A.Interaction, Point);
	TestTrue(TEXT("빈손 = 반납 불가"), Offer.bHasOffer && !Offer.IsAvailable());
	A.Carrier->ApplyAdd(FVHItemData::MakeTreasure(FText::FromString(TEXT("보물")), 2000));
	Offer = Auth->Evaluate(A.Interaction, Point);
	TestTrue(TEXT("환금 아이템을 들면 반납 가능"), Offer.IsAvailable() && Offer.Mode == EVHInteractMode::Tap);
	A.Carrier->ApplyServerCarried(2000);
	TestEqual(TEXT("들고 있는 가치 = 서버 값"), A.Carrier->GetTotalValue(), 2000);
	TestEqual(TEXT("반납(전부 내려놓기) 1개"), A.Carrier->ApplyRemoveAll().Num(), 1);

	Sub->NowOverride.Reset();
	World->EndPlay(EEndPlayReason::Quit);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// ───────────────────────── 커서 기준 이동 ⟪기획 변경 2026-10-26⟫ ─────────────────────────
// 탑뷰 화면: 위 = 월드 +X, 오른쪽 = 월드 +Y. Forward = W(+1)/S(-1), Right = D(+1)/A(-1)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVHAimMoveTest, "VaultHeist.Interaction.AimMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVHAimMoveTest::RunTest(const FString& Parameters)
{
	auto Near = [this](const TCHAR* What, const FVector& Got, const FVector& Want)
	{
		TestTrue(FString::Printf(TEXT("%s: 결과 (%.2f, %.2f) / 기대 (%.2f, %.2f)"), What, Got.X, Got.Y, Want.X, Want.Y),
			Got.Equals(Want, 0.01f));
	};
	const float R2 = UE_HALF_SQRT_2;

	// 화면(월드) 축 고정 이동 [D1, 2026-11-13] = 같은 함수에 커서 대신 "탑뷰 카메라 방향"을 넣은 것.
	// L_Mansion_1F는 화면 위쪽이 월드 +Y(TopViewYaw 90): W → +Y, S → -Y, D → -X(화면 오른쪽), A → +X, 대각선도 세기 1
	Near(TEXT("화면 축 + W → 화면 위(+Y)"), VHRules::AimRelativeMove(1, 0, 90.f), FVector(0, 1, 0));
	Near(TEXT("화면 축 + S → 화면 아래(-Y)"), VHRules::AimRelativeMove(-1, 0, 90.f), FVector(0, -1, 0));
	Near(TEXT("화면 축 + D → 화면 오른쪽(-X)"), VHRules::AimRelativeMove(0, 1, 90.f), FVector(-1, 0, 0));
	Near(TEXT("화면 축 + A → 화면 왼쪽(+X)"), VHRules::AimRelativeMove(0, -1, 90.f), FVector(1, 0, 0));
	Near(TEXT("화면 축 + W+D → 대각선, 세기 1"), VHRules::AimRelativeMove(1, 1, 90.f), FVector(-R2, R2, 0));

	// 커서가 화면 위쪽(+X, Yaw 0)
	Near(TEXT("커서 위 + W → 위"), VHRules::AimRelativeMove(1, 0, 0.f), FVector(1, 0, 0));
	Near(TEXT("커서 위 + S → 아래"), VHRules::AimRelativeMove(-1, 0, 0.f), FVector(-1, 0, 0));
	Near(TEXT("커서 위 + D → 오른쪽"), VHRules::AimRelativeMove(0, 1, 0.f), FVector(0, 1, 0));
	Near(TEXT("커서 위 + A → 왼쪽"), VHRules::AimRelativeMove(0, -1, 0.f), FVector(0, -1, 0));
	// 커서가 화면 오른쪽(+Y, Yaw 90): W → 오른쪽, D → 아래(-X), A → 위
	Near(TEXT("커서 오른쪽 + W"), VHRules::AimRelativeMove(1, 0, 90.f), FVector(0, 1, 0));
	Near(TEXT("커서 오른쪽 + D → 화면 아래"), VHRules::AimRelativeMove(0, 1, 90.f), FVector(-1, 0, 0));
	Near(TEXT("커서 오른쪽 + A → 화면 위"), VHRules::AimRelativeMove(0, -1, 90.f), FVector(1, 0, 0));
	Near(TEXT("커서 오른쪽 + S → 왼쪽"), VHRules::AimRelativeMove(-1, 0, 90.f), FVector(0, -1, 0));
	// 커서가 화면 아래(-X, Yaw 180): W → 아래, A → 오른쪽(+Y)
	Near(TEXT("커서 아래 + W"), VHRules::AimRelativeMove(1, 0, 180.f), FVector(-1, 0, 0));
	Near(TEXT("커서 아래 + A → 화면 오른쪽"), VHRules::AimRelativeMove(0, -1, 180.f), FVector(0, 1, 0));
	// 커서가 화면 왼쪽(-Y, Yaw -90): W → 왼쪽, D → 위
	Near(TEXT("커서 왼쪽 + W"), VHRules::AimRelativeMove(1, 0, -90.f), FVector(0, -1, 0));
	Near(TEXT("커서 왼쪽 + D → 화면 위"), VHRules::AimRelativeMove(0, 1, -90.f), FVector(1, 0, 0));
	// 커서가 오른쪽 위 대각선(Yaw 45)
	Near(TEXT("커서 오른쪽 위 + W"), VHRules::AimRelativeMove(1, 0, 45.f), FVector(R2, R2, 0));
	Near(TEXT("커서 오른쪽 위 + D → 오른쪽 아래"), VHRules::AimRelativeMove(0, 1, 45.f), FVector(-R2, R2, 0));

	// 대각선 입력: 방향은 합치고 세기는 1
	Near(TEXT("커서 위 + W+A → 왼쪽 위, 세기 1"), VHRules::AimRelativeMove(1, -1, 0.f), FVector(R2, -R2, 0));
	TestEqual(TEXT("대각선 세기 = 1"), (float)VHRules::AimRelativeMove(1, 1, 30.f).Size(), 1.f, 0.001f);
	TestTrue(TEXT("입력 없음 → 0"), VHRules::AimRelativeMove(0, 0, 30.f).IsNearlyZero());
	TestEqual(TEXT("아날로그 반쯤 → 0.5"), (float)VHRules::AimRelativeMove(0.5f, 0, 0.f).Size(), 0.5f, 0.001f);

	// 방향별 속도 배율 (기본은 모두 1 → 뒤·옆도 같은 속도)
	TestEqual(TEXT("기본: 뒤 = 1"), (float)VHRules::AimRelativeMove(-1, 0, 0.f).Size(), 1.f, 0.001f);
	TestEqual(TEXT("기본: 옆 = 1"), (float)VHRules::AimRelativeMove(0, 1, 0.f).Size(), 1.f, 0.001f);
	TestEqual(TEXT("뒤 배율 0.5"), (float)VHRules::AimRelativeMove(-1, 0, 0.f, 1.f, 0.5f, 0.8f).Size(), 0.5f, 0.001f);
	TestEqual(TEXT("옆 배율 0.8"), (float)VHRules::AimRelativeMove(0, -1, 0.f, 1.f, 0.5f, 0.8f).Size(), 0.8f, 0.001f);
	TestEqual(TEXT("앞+옆 대각선 = 둘의 평균 0.9"), (float)VHRules::AimRelativeMove(1, 1, 0.f, 1.f, 0.5f, 0.8f).Size(), 0.9f, 0.001f);

	// 커서 방향 계산 (흔들림 방지: 1m 안이면 이전 방향 유지)
	const FVector Me(100, 100, 90);
	TestEqual(TEXT("커서가 위(+X) 3m → 0°"), VHRules::AimYawToPoint(Me, FVector(400, 100, 0), 37.f, 100.f), 0.f, 0.01f);
	TestEqual(TEXT("커서가 오른쪽(+Y) → 90°"), VHRules::AimYawToPoint(Me, FVector(100, 300, 0), 37.f, 100.f), 90.f, 0.01f);
	TestEqual(TEXT("커서가 아래(-X) → 180°"), FMath::Abs(VHRules::AimYawToPoint(Me, FVector(-300, 100, 0), 37.f, 100.f)), 180.f, 0.01f);
	TestEqual(TEXT("커서가 50cm 안 → 이전 37° 유지"), VHRules::AimYawToPoint(Me, FVector(140, 130, 0), 37.f, 100.f), 37.f, 0.01f);
	TestEqual(TEXT("커서가 캐릭터 바로 위 → 이전 유지"), VHRules::AimYawToPoint(Me, FVector(100, 100, 0), -120.f, 100.f), -120.f, 0.01f);
	return true;
}

#endif
