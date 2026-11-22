#include "Gameplay/VHMatchSubsystem.h"

#include "Gameplay/VHGameplayConfig.h"
#include "Gameplay/VHInteractionSubsystem.h"
#include "Gameplay/VHReturnPoint.h"
#include "Gameplay/VHThiefStateComponent.h"
#include "Gameplay/VHRanking.h"
#include "Network/VHSettings.h"
#include "MCPTest.h"

#include "Engine/World.h"

bool UVHMatchSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UVHMatchSubsystem::Deinitialize()
{
	ReturnPoints.Reset();
	Stats.Reset();
	RespawnAt.Reset();
	Notices.Reset();
	ServerMoney.Reset();
	Super::Deinitialize();
}

TStatId UVHMatchSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UVHMatchSubsystem, STATGROUP_Tickables);
}

double UVHMatchSubsystem::GetNow() const
{
	if (const UVHInteractionSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UVHInteractionSubsystem>() : nullptr)
	{
		return Sub->GetNow();
	}
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

// ───────────────────────── 시작·제한 시간 ─────────────────────────

void UVHMatchSubsystem::StartMatch()
{
	if (bStarted)
	{
		return;
	}
	bStarted = true;
	// 서버 값(SC_GAME_START·SC_SNAPSHOT)이 오기 전까지는 설정값으로 시계를 돌린다
	TimeLimit = UVHSettings::Get().GetGameplayConfig().TimeLimitSeconds;
	MatchStartTime = GetNow();
	UE_LOG(LogMCPTest, Log, TEXT("[Match] 시작 (시간·반납 금액·반납지점은 서버 값으로 표시)"));
	OnMatchChanged.Broadcast();
}

float UVHMatchSubsystem::GetElapsedSeconds() const
{
	return bStarted ? static_cast<float>(GetNow() - MatchStartTime) : 0.f;
}

float UVHMatchSubsystem::GetRemainingSeconds() const
{
	return FMath::Max(0.f, TimeLimit - GetElapsedSeconds());
}

void UVHMatchSubsystem::ApplyServerTime(float DurationSeconds, float RemainingSeconds)
{
	if (DurationSeconds > 0.f)
	{
		TimeLimit = DurationSeconds;
	}
	MatchStartTime = GetNow() - (TimeLimit - FMath::Clamp(RemainingSeconds, 0.f, TimeLimit));
}

// ───────────────────────── 반납 금액·순위 (서버 SC_MONEY) ─────────────────────────

void UVHMatchSubsystem::ApplyServerMoney(const TArray<FVHServerMoneyRow>& Rows)
{
	ServerMoney = Rows;
	OnMatchChanged.Broadcast();
}

const FVHServerMoneyRow* UVHMatchSubsystem::GetMyServerMoney() const
{
	return ServerMoney.FindByPredicate([](const FVHServerMoneyRow& Row) { return Row.bIsMe; });
}

int32 UVHMatchSubsystem::GetReturnedValue(const UVHThiefStateComponent* Thief) const
{
	for (const FVHServerMoneyRow& Row : ServerMoney)
	{
		if (Thief && Row.EntityId != 0 && Row.EntityId == Thief->GetEntityId())
		{
			return Row.Banked;
		}
	}
	return 0;
}

TArray<FVHRankEntry> UVHMatchSubsystem::GetRanking() const
{
	// 같은 금액은 같은 순위 = 서버 결과(SC_GAME_END) 규칙과 같다
	TArray<FVHRankEntry> Rows;
	TArray<int32> Banked;
	for (const FVHServerMoneyRow& Row : ServerMoney)
	{
		FVHRankEntry& E = Rows.AddDefaulted_GetRef();
		E.Name = Row.Name;
		E.Value = Row.Banked;
		E.bIsMe = Row.bIsMe;
		Banked.Add(Row.Banked);
	}
	const TArray<int32> Ranks = VHRanking::CompetitionRanks(Banked);
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		Rows[i].Rank = Ranks[i];
		Rows[i].bTied = VHRanking::CountRank(Ranks, Ranks[i]) > 1;
	}
	Rows.Sort([](const FVHRankEntry& A, const FVHRankEntry& B)
	{
		return A.Rank != B.Rank ? A.Rank < B.Rank : A.Name < B.Name;
	});
	return Rows;
}

// ───────────────────────── 반납지점 (서버 엔티티) ─────────────────────────

TArray<AVHReturnPoint*> UVHMatchSubsystem::GetReturnPoints() const
{
	TArray<AVHReturnPoint*> Out;
	for (const TWeakObjectPtr<AVHReturnPoint>& Weak : ReturnPoints)
	{
		if (AVHReturnPoint* P = Weak.Get())
		{
			Out.Add(P);
		}
	}
	return Out;
}

void UVHMatchSubsystem::AddServerReturnPoint(AVHReturnPoint* Point)
{
	if (Point)
	{
		ReturnPoints.AddUnique(Point);
		OnMatchChanged.Broadcast();
	}
}

void UVHMatchSubsystem::RemoveServerReturnPoint(AVHReturnPoint* Point)
{
	ReturnPoints.Remove(Point);
	OnMatchChanged.Broadcast();
}

// ───────────────────────── 쓰러짐 기록·리스폰 표시 ─────────────────────────

FVHThiefMatchStats& UVHMatchSubsystem::StatsFor(const UVHThiefStateComponent* Thief)
{
	return Stats.FindOrAdd(Thief);
}

void UVHMatchSubsystem::ScheduleRespawn(UVHThiefStateComponent* Thief, float Seconds)
{
	if (Thief)
	{
		RespawnAt.Add(Thief, GetNow() + FMath::Max(0.f, Seconds));
		OnMatchChanged.Broadcast();
	}
}

void UVHMatchSubsystem::ClearRespawn(const UVHThiefStateComponent* Thief)
{
	RespawnAt.Remove(const_cast<UVHThiefStateComponent*>(Thief));
}

float UVHMatchSubsystem::GetRespawnRemaining(const UVHThiefStateComponent* Thief) const
{
	if (const double* At = RespawnAt.Find(const_cast<UVHThiefStateComponent*>(Thief)))
	{
		return FMath::Max(0.f, static_cast<float>(*At - GetNow()));
	}
	return 0.f;
}

// ───────────────────────── 알림·전체 화면 안내 ─────────────────────────

void UVHMatchSubsystem::PushNotice(const FText& Text, bool bIsError)
{
	if (Text.IsEmpty())
	{
		return;
	}
	Notices.Add({ Text, GetNow() });
	if (Notices.Num() > MaxNotices)
	{
		Notices.RemoveAt(0, Notices.Num() - MaxNotices);
	}
	// 화면 표시는 HUD 알림 영역(UVHGameHUDWidget)이 이 목록을 읽어서 한다 (토스트와 중복되지 않게 여기서는 토스트를 띄우지 않음)
	UE_LOG(LogMCPTest, Log, TEXT("[Notice] %s%s"), bIsError ? TEXT("(!) ") : TEXT(""), *Text.ToString());
	OnMatchChanged.Broadcast();
}

void UVHMatchSubsystem::ShowBanner(const FText& Text, float Seconds)
{
	BannerText = Text;
	BannerEndTime = GetNow() + Seconds;
}

bool UVHMatchSubsystem::IsBannerVisible() const
{
	return !BannerText.IsEmpty() && GetNow() < BannerEndTime;
}

// ───────────────────────── 매 프레임 ─────────────────────────

void UVHMatchSubsystem::Tick(float DeltaTime)
{
	// 시간 끝·반납지점 수명은 서버가 정한다 (SC_GAME_END, SC_ENTITY_DESPAWN). 여기서는 사라진 액터만 목록에서 뺀다
	ReturnPoints.RemoveAll([](const TWeakObjectPtr<AVHReturnPoint>& W) { return !W.IsValid(); });
}
