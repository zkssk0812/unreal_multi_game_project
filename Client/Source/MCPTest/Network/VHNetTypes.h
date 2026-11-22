#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VHNetTypes.generated.h"

// 프로토콜 구조체(RobProto::*)를 UI·블루프린트에서 쓰기 쉬운 언리얼 타입으로 옮겨 담은 것.
// 서버에서 받은 값은 서브시스템이 이 타입으로 변환해 보관하고, 위젯은 이것만 본다.

/** 클라이언트가 추적하는 연결 단계. 서버의 ClientPhase에 연결 전/연결 중 단계를 더했다. */
UENUM(BlueprintType)
enum class EVHNetPhase : uint8
{
	Disconnected,
	Connecting,
	Anonymous,		// TCP 연결됨, 로그인 전
	Authenticating,	// CS_LOGIN 보냄, 응답 대기
	Lobby,
	Room,
	Game
};

UENUM(BlueprintType)
enum class EVHDifficulty : uint8 { Easy, Normal, Hard };

UENUM(BlueprintType)
enum class EVHRoomState : uint8 { Waiting, Loading, Playing, Result };

/** 팀 결과. Clear~Failed는 PvE(탈출) 때 값. PvP는 탈출이 없으므로 끝나면 Finished("게임 종료"), 승패는 플레이어별 순위로 [PvP 규칙 v1 2장] */
UENUM(BlueprintType)
enum class EVHMatchResult : uint8 { Clear, PartialClear, Failed, Aborted, Finished };

/** 서버 v3.0 EndReason과 같은 순서 */
UENUM(BlueprintType)
enum class EVHEndReason : uint8 { TimeUp, Aborted };

USTRUCT(BlueprintType)
struct FVHPlayerStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 PlayCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 ClearCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 PartialCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 FailCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 EscapeCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 CaughtCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 TreasureTotal = 0;
	UPROPERTY(BlueprintReadOnly) int32 GuardStunCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 BestScore = 0;
};

USTRUCT(BlueprintType)
struct FVHRoomSummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int64 RoomId = 0;
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) FString HostName;
	UPROPERTY(BlueprintReadOnly) int32 PlayerCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 MaxPlayers = 3;	// v2.1에는 없는 값 → MAX_ROOM_PLAYERS
	UPROPERTY(BlueprintReadOnly) EVHRoomState State = EVHRoomState::Waiting;

	bool CanJoin() const { return State == EVHRoomState::Waiting && PlayerCount < MaxPlayers; }
};

USTRUCT(BlueprintType)
struct FVHRoomMember
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 Slot = 0;		// v2.1에는 slot이 없어 members 배열 순서(0 = 방장)를 쓴다
	UPROPERTY(BlueprintReadOnly) int64 AccountId = 0;	// 서버 player_id
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) int32 CharacterId = 0;	// 0 = 미선택
	UPROPERTY(BlueprintReadOnly) TArray<int32> Perks;
	UPROPERTY(BlueprintReadOnly) bool bReady = false;
};

USTRUCT(BlueprintType)
struct FVHRoomState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) bool bValid = false;
	UPROPERTY(BlueprintReadOnly) int64 RoomId = 0;
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) int64 HostAccountId = 0;
	UPROPERTY(BlueprintReadOnly) EVHRoomState State = EVHRoomState::Waiting;
	UPROPERTY(BlueprintReadOnly) TArray<FVHRoomMember> Members;

	const FVHRoomMember* FindMember(int64 AccountId) const
	{
		return Members.FindByPredicate([AccountId](const FVHRoomMember& M) { return M.AccountId == AccountId; });
	}
};

USTRUCT(BlueprintType)
struct FVHPlayerResult
{
	GENERATED_BODY()

	// 서버 v3.0 SC_GAME_END 의 PlayerResult 한 줄
	UPROPERTY(BlueprintReadOnly) int64 AccountId = 0;
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) int32 CharacterId = 0;
	UPROPERTY(BlueprintReadOnly) bool bLeftEarly = false;
	UPROPERTY(BlueprintReadOnly) int32 TreasureEscaped = 0;	// treasures_delivered (반납한 보물 수)
	UPROPERTY(BlueprintReadOnly) int32 ValueDelivered = 0;	// banked_value (반납 금액 = 점수 $)
	/** 서버가 매긴 순위 (같은 금액은 같은 순위: 1, 1, 3). 1위 = 승리 */
	UPROPERTY(BlueprintReadOnly) int32 Rank = 0;
	/** 쓰러뜨린 횟수(kills)·쓰러진 횟수(downs) */
	UPROPERTY(BlueprintReadOnly) int32 Kills = 0;
	UPROPERTY(BlueprintReadOnly) int32 Deaths = 0;
	/** 내 결과인가 (넷 서브시스템이 계정 ID로 정한다. 프로토콜에는 없는 값) */
	UPROPERTY(BlueprintReadOnly) bool bIsMe = false;
	/** 이 판까지의 최고 기록($), 이 판으로 갱신했는가. 서버가 DB 없이 돌면 이번 판 금액 / false */
	UPROPERTY(BlueprintReadOnly) int32 BestRecord = 0;
	UPROPERTY(BlueprintReadOnly) bool bNewRecord = false;
};

/** 결과의 Rank를 ValueDelivered 기준 공동 순위로 채우고 순위 순(같으면 원래 순서)으로 정렬한다 */
void VHRankResults(TArray<FVHPlayerResult>& Players);

USTRUCT(BlueprintType)
struct FVHGameEndInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) bool bValid = false;
	UPROPERTY(BlueprintReadOnly) bool bIsSample = false;	// 개발용 샘플 결과(서버 응답 아님)
	UPROPERTY(BlueprintReadOnly) int64 MatchId = 0;
	UPROPERTY(BlueprintReadOnly) EVHMatchResult Result = EVHMatchResult::Aborted;
	UPROPERTY(BlueprintReadOnly) EVHEndReason Reason = EVHEndReason::TimeUp;
	UPROPERTY(BlueprintReadOnly) int32 DurationMs = 0;
	UPROPERTY(BlueprintReadOnly) TArray<FVHPlayerResult> Players;
	UPROPERTY(BlueprintReadOnly) int32 ResultScreenMs = 0;
	UPROPERTY(BlueprintReadOnly) double ReceivedTimeSeconds = 0.0;	// FPlatformTime::Seconds 기준
	/** 쓰러뜨림·쓰러짐 값이 있는가 (v3.0 결과와 샘플에는 항상 있다) */
	UPROPERTY(BlueprintReadOnly) bool bHasCombatStats = false;
	/** 실서버 v3.0 SC_GAME_END 로 만든 결과인가 (가짜 서버의 샘플 결과면 false) */
	UPROPERTY(BlueprintReadOnly) bool bServerResult = false;
};

/** 서버 코드·열거형을 한국어 안내 문구로 바꾸는 함수 모음 */
UCLASS()
class UVHNetText : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** ResultCode(서버 결과 코드) → 사용자 안내 문구 */
	UFUNCTION(BlueprintPure, Category = "VaultHeist|Text")
	static FText ResultCodeText(int32 Code);

	UFUNCTION(BlueprintPure, Category = "VaultHeist|Text")
	static FText DifficultyText(EVHDifficulty Difficulty);

	UFUNCTION(BlueprintPure, Category = "VaultHeist|Text")
	static FText RoomStateText(EVHRoomState State);

	UFUNCTION(BlueprintPure, Category = "VaultHeist|Text")
	static FText MatchResultText(EVHMatchResult Result);

	UFUNCTION(BlueprintPure, Category = "VaultHeist|Text")
	static FText EndReasonText(EVHEndReason Reason);

	UFUNCTION(BlueprintPure, Category = "VaultHeist|Text")
	static FText KickReasonText(int32 Reason);

	UFUNCTION(BlueprintPure, Category = "VaultHeist|Text")
	static FText LeaveReasonText(int32 Reason);

	/** 밀리초 → "m분 s초" */
	UFUNCTION(BlueprintPure, Category = "VaultHeist|Text")
	static FText DurationText(int32 Ms);
};
