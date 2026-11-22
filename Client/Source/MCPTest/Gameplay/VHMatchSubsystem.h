#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/VHGameplayTypes.h"
#include "Gameplay/VHThiefStateComponent.h"	// 아래 FindStats 등이 약한 포인터를 만들므로 완전한 타입이 필요하다 (include 순서에 기대지 않게)
#include "VHMatchSubsystem.generated.h"

class AVHReturnPoint;
class UVHThiefStateComponent;

DECLARE_MULTICAST_DELEGATE(FVHOnMatchChanged);

/** 의적 1명의 이번 판 기록 (HUD 쓰러짐 화면용. 값은 서버 SC_DOWNED 로 채운다) */
struct FVHThiefMatchStats
{
	/** 쓰러진 횟수 */
	int32 Deaths = 0;
	/** 쓰러져 잃은 반납 금액 합계 */
	int32 ValueLost = 0;

	// ── 마지막으로 쓰러졌을 때 (HUD 쓰러짐 화면: 사유·손실 금액) ──
	/** 그때 잃은 반납 금액 */
	int32 LastDeathLoss = 0;
	/** 나를 쓰러뜨린 의적 이름 (경비원이면 빈 문자열) */
	FString LastKillerName;
	/** 경비원에게 당한 것인가 */
	bool bLastDeathByGuard = false;
	/** 쓰러지기 직전의 반납 금액 (0이면 "반납한 돈이 없어 잃은 금액 없음") */
	int32 LastBankedBefore = 0;
	/** 쓰러질 때 들고 있던 보물 가치 합계 (보물은 어느 경우든 쓰러진 자리에 떨어진다) */
	int32 LastDropTreasureValue = 0;
	/** 쓰러질 때 들고 있던 환금 아이템(보물 말고) 개수·가치 (플레이어에게 = 떨어뜨림, 경비원에게 = 사라짐) */
	int32 LastDropValuableCount = 0;
	int32 LastDropValuableValue = 0;
};

/** 순위표 한 줄 (반납 금액 기준, 같은 금액은 같은 순위) */
struct FVHRankEntry
{
	FString Name;
	int32 Value = 0;
	/** 1부터. 같은 금액이면 같은 등수, 다음 등수는 건너뜀 (1, 1, 3) [서버 A-20] */
	int32 Rank = 0;
	/** 같은 등수가 2명 이상 */
	bool bTied = false;
	/** 내 줄인가 */
	bool bIsMe = false;
};

/** 서버 SC_MONEY 한 줄: 플레이어별 들고 있는 가치·반납 금액 */
struct FVHServerMoneyRow
{
	uint16 EntityId = 0;
	FString Name;
	bool bIsMe = false;
	int32 Carried = 0;
	int32 Banked = 0;
};

/** HUD 알림 영역용 최근 알림 (보물 획득·쓰러짐 등) */
struct FVHMatchNotice
{
	FText Text;
	/** 알림 시각 (UVHInteractionSubsystem::GetNow 기준) */
	double Time = 0.0;
};

/**
 * 한 판의 진행 상황을 HUD에 보여 주려고 모아 두는 곳 (게임 레벨마다 1개). 규칙 판정은 하지 않는다.
 *
 *  - 제한 시간: 서버 SC_SNAPSHOT 의 남은 시간(ApplyServerTime)
 *  - 반납 금액·순위: 서버 SC_MONEY (ApplyServerMoney)
 *  - 반납지점 목록: 서버 엔티티로 생긴 것(AddServerReturnPoint / RemoveServerReturnPoint)
 *  - 쓰러짐 기록·리스폰까지 남은 시간(SC_DOWNED), 알림, 전체 화면 안내
 *
 * 게임 레벨의 플레이어 컨트롤러가 StartMatch()를 부르면 시작한다(메뉴 레벨에서는 아무것도 안 함).
 * 서버 없이 게임 레벨을 직접 ▶ 했을 때는 시계만 흐르고 나머지는 비어 있다.
 */
UCLASS()
class MCPTEST_API UVHMatchSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** 한 판 시작 (시계 시작. 실제 남은 시간은 서버 값으로 맞춰진다) */
	void StartMatch();
	bool IsStarted() const { return bStarted; }

	// ── 제한 시간 (서버 5분) ──
	float GetTimeLimit() const { return TimeLimit; }
	float GetElapsedSeconds() const;
	float GetRemainingSeconds() const;

	// ── 반납지점 (서버가 만들고 없앤다) ──
	TArray<AVHReturnPoint*> GetReturnPoints() const;
	void AddServerReturnPoint(AVHReturnPoint* Point);
	void RemoveServerReturnPoint(AVHReturnPoint* Point);

	// ── 전체 화면 메시지 (HUD가 읽는다) ──
	void ShowBanner(const FText& Text, float Seconds);
	bool IsBannerVisible() const;
	const FText& GetBannerText() const { return BannerText; }

	// ── 쓰러짐 기록 ──
	FVHThiefMatchStats& StatsFor(const UVHThiefStateComponent* Thief);
	/** 기록이 있으면 돌려준다 (없으면 nullptr) */
	const FVHThiefMatchStats* FindStats(const UVHThiefStateComponent* Thief) const { return Stats.Find(Thief); }

	// ── 반납 금액·순위 (서버 SC_MONEY) ──
	/** SC_GAME_START·SC_SNAPSHOT: 제한 시간과 남은 시간을 서버 값으로 */
	void ApplyServerTime(float DurationSeconds, float RemainingSeconds);
	/** SC_MONEY: 플레이어별 들고 있는 가치·반납 금액 */
	void ApplyServerMoney(const TArray<FVHServerMoneyRow>& Rows);
	const TArray<FVHServerMoneyRow>& GetServerMoney() const { return ServerMoney; }
	const FVHServerMoneyRow* GetMyServerMoney() const;
	/** 이 의적의 반납 금액 (서버 엔티티 번호가 같은 줄. 없으면 0) */
	int32 GetReturnedValue(const UVHThiefStateComponent* Thief) const;
	/** 순위표: 등수 → 이름 순. 같은 금액이면 같은 등수, 다음 등수는 건너뜀 (1, 1, 3) */
	TArray<FVHRankEntry> GetRanking() const;

	// ── 리스폰까지 남은 시간 (HUD 쓰러짐 화면. 실제 리스폰은 서버 SC_RESPAWNED) ──
	void ScheduleRespawn(UVHThiefStateComponent* Thief, float Seconds);
	void ClearRespawn(const UVHThiefStateComponent* Thief);
	/** 리스폰까지 남은 초 (대기 중이 아니면 0) */
	float GetRespawnRemaining(const UVHThiefStateComponent* Thief) const;

	// ── 알림 (HUD 알림 영역) ──
	/** 알림 추가 (HUD 알림 영역이 최근 몇 개를 몇 초 동안 보여준다 — UVHUIStyle.HudNotice*). 최근 MaxNotices개만 보관 */
	void PushNotice(const FText& Text, bool bIsError = false);
	const TArray<FVHMatchNotice>& GetNotices() const { return Notices; }
	static constexpr int32 MaxNotices = 8;

	/** 반납 금액·반납지점·알림이 바뀌었을 때 (HUD) */
	FVHOnMatchChanged OnMatchChanged;

protected:
	double GetNow() const;

	double MatchStartTime = 0.0;
	float TimeLimit = 300.f;
	bool bStarted = false;
	FText BannerText;
	double BannerEndTime = -1.0;

	TArray<TWeakObjectPtr<AVHReturnPoint>> ReturnPoints;
	TMap<TWeakObjectPtr<const UVHThiefStateComponent>, FVHThiefMatchStats> Stats;
	/** 리스폰 예약: 의적 → 리스폰 시각 (표시용) */
	TMap<TWeakObjectPtr<UVHThiefStateComponent>, double> RespawnAt;
	TArray<FVHMatchNotice> Notices;
	TArray<FVHServerMoneyRow> ServerMoney;
};
