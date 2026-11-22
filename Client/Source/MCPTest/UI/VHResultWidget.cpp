#include "UI/VHResultWidget.h"

#include "Network/VHNetSubsystem.h"
#include "Game/VHMenuPlayerController.h"
#include "UI/VHCodeWidgets.h"
#include "Gameplay/VHRanking.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Components/HorizontalBoxSlot.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

void UVHResultWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	ToLobbyButton->OnClicked.AddDynamic(this, &UVHResultWidget::HandleToLobby);
}

void UVHResultWidget::OnScreenShown()
{
	bReturning = false;
	ShownSeconds = FPlatformTime::Seconds();
	bDevShotTaken = false;
	Fill();
}

void UVHResultWidget::Fill()
{
	const UVHNetSubsystem* Net = GetNet();
	if (!Net)
	{
		return;
	}
	const FVHGameEndInfo& Info = Net->GetLastGameEnd();
	const UVHUIStyle& S = Style();

	ResultTitleText->SetText(FText::FromString(FString::Printf(TEXT("%s%s"),
		*UVHNetText::MatchResultText(Info.Result).ToString(),
		Info.bIsSample ? TEXT(" (샘플)") : TEXT(""))));

	// 최고 기록은 서버가 준 값(best_record·new_record)을 그대로 보여 준다
	const bool bShowBest = Info.bServerResult || Info.bIsSample;
	const FVHPlayerResult* Mine = Info.Players.FindByPredicate([](const FVHPlayerResult& P) { return P.bIsMe; });

	FString InfoLine = FString::Printf(TEXT("종료 사유: %s   ·   플레이 시간: %s"),
		*UVHNetText::EndReasonText(Info.Reason).ToString(), *UVHNetText::DurationText(Info.DurationMs).ToString());
	if (bShowBest && Mine && (Mine->bNewRecord || Mine->BestRecord > 0))
	{
		InfoLine += FString::Printf(TEXT("   ·   %s: %s"), Mine->bNewRecord ? TEXT("내 최고 기록 갱신") : TEXT("내 최고 기록"), *VHRanking::Money(Mine->BestRecord));
	}
	ResultInfoText->SetText(FText::FromString(InfoLine));

	// PvP: 반납 금액 순위 (넷 서브시스템이 Rank를 채우고 순위 순으로 정렬해 둔다). 같은 금액이면 같은 등수(서버 v3.0 A-20)
	TArray<int32> Ranks;
	for (const FVHPlayerResult& P : Info.Players)
	{
		Ranks.Add(P.Rank);
	}
	auto IsTied = [&Ranks](int32 Rank) { return VHRanking::CountRank(Ranks, Rank) > 1; };

	// 내 결과를 맨 위에 크게: "1위 — 승리!" / "공동 1위 — 승리!" / "2위 — 패배"
	if (Mine && Mine->Rank > 0)
	{
		const bool bWin = Mine->Rank == 1;
		MyOutcomeBigText->SetText(FText::FromString(FString::Printf(TEXT("%s위 — %s"), *VHRanking::RankLabel(Mine->Rank, IsTied(Mine->Rank)),
			bWin ? TEXT("승리!") : TEXT("패배"))));
		S.ApplyText(MyOutcomeBigText, EVHTextRole::Big, bWin ? S.Success : S.Error);
	}
	else
	{
		MyOutcomeBigText->SetText(FText::GetEmpty());
	}

	// 플레이어 카드: 순위 순
	PlayersBox->ClearChildren();
	TArray<FVHPlayerResult> Sorted = Info.Players;
	Sorted.StableSort([](const FVHPlayerResult& A, const FVHPlayerResult& B) { return A.Rank < B.Rank; });
	for (const FVHPlayerResult& P : Sorted)
	{
		UVHPlayerResultCard* Card = CreateWidget<UVHPlayerResultCard>(this, UVHPlayerResultCard::StaticClass());
		Card->SetResult(P, IsTied(P.Rank), Info.bHasCombatStats, bShowBest);
		UPanelSlot* CardSlot = PlayersBox->AddChild(Card);
		if (UHorizontalBoxSlot* H = Cast<UHorizontalBoxSlot>(CardSlot))
		{
			H->SetPadding(FMargin(8.f));
			H->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			// 카드 높이 = 내용 높이 (세로로 꽉 채우면 아래 절반이 빈 칸이 된다)
			H->SetVerticalAlignment(VAlign_Top);
		}
	}

	// 서버 v3.0: 결과 뒤에는 모두 로비로 (대기실로 돌아가는 기능은 서버에 없다)
	ToLobbyButton->SetIsEnabled(true);
}

void UVHResultWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const UVHNetSubsystem* Net = GetNet();
	if (!Net)
	{
		return;
	}
	const FVHGameEndInfo& Info = Net->GetLastGameEnd();
	const double Elapsed = FPlatformTime::Seconds() - Info.ReceivedTimeSeconds;
	const int32 Remaining = FMath::Max(0, FMath::CeilToInt(Info.ResultScreenMs / 1000.0 - Elapsed));
	FString Text;
	if (bReturning)
	{
		Text = TEXT("이동하는 중...");
	}
	else if (Info.bIsSample)
	{
		Text = FString::Printf(TEXT("개발용 샘플 결과입니다. (실제 게임이라면 %d초 뒤 자동으로 로비로 돌아갑니다)"), Remaining);
	}
	else if (Remaining > 0)
	{
		Text = FString::Printf(TEXT("%d초 뒤 자동으로 로비로 돌아갑니다."), Remaining);
	}
	else
	{
		Text = TEXT("로비로 돌아가는 중...");
	}
	CountdownStatusText->SetText(FText::FromString(Text));

	// ── 개발용 실행 옵션 (봇 시험) ──
	// -VHShotResult: 결과창이 뜨고 1.5초 뒤 화면을 Saved/V3Test/result_<봇 이름>.png 로 저장
	// -VHBotReturn=초: 결과창이 뜨고 그 시간 뒤 [로비로]를 누른 것처럼 CS_RETURN 을 보낸다 (없으면 서버의 자동 복귀를 기다린다)
	static const bool bDevShot = FParse::Param(FCommandLine::Get(), TEXT("VHShotResult"));
	static float DevReturnSeconds = -1.f;
	static FString DevName;
	if (DevName.IsEmpty())
	{
		FParse::Value(FCommandLine::Get(), TEXT("VHBotReturn="), DevReturnSeconds);
		if (!FParse::Value(FCommandLine::Get(), TEXT("VHBot="), DevName))
		{
			DevName = TEXT("Player");
		}
	}
	const double Shown = FPlatformTime::Seconds() - ShownSeconds;
	if (bDevShot && !bDevShotTaken && Shown > 1.5)
	{
		bDevShotTaken = true;
		const FString File = FPaths::ProjectSavedDir() / TEXT("V3Test") / FString::Printf(TEXT("result_%s.png"), *DevName);
		FScreenshotRequest::RequestScreenshot(File, true, false);
		UE_LOG(LogTemp, Log, TEXT("[결과창] 화면 저장 %s"), *FPaths::GetCleanFilename(File));
	}
	if (DevReturnSeconds >= 0.f && !bReturning && Shown > DevReturnSeconds && !Info.bIsSample && Net->GetPhase() == EVHNetPhase::Game)
	{
		UE_LOG(LogTemp, Log, TEXT("[결과창] 봇: [로비로] (CS_RETURN)"));
		Return();
	}
}

void UVHResultWidget::HandleToLobby()
{
	Return();
}

void UVHResultWidget::Return()
{
	UVHNetSubsystem* Net = GetNet();
	if (!Net || bReturning)
	{
		return;
	}
	if (Net->GetLastGameEnd().bIsSample || Net->GetPhase() != EVHNetPhase::Game)
	{
		// 샘플 결과(서버와 무관)면 지금 연결 상태에 맞는 화면으로만 돌아간다.
		Net->ClearPendingResult();
		if (AVHMenuPlayerController* PC = GetMenuPC())
		{
			PC->ShowScreenForCurrentState();
		}
		return;
	}
	if (Net->ReturnFromResult())
	{
		bReturning = true;
		ToLobbyButton->SetIsEnabled(false);
		// 서버 응답(SC_LEAVE_ROOM)이 오면 단계가 바뀌고 화면이 바뀐다.
	}
}
