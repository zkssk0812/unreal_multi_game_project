#pragma once

#include "CoreMinimal.h"
#include "VHInteractionTypes.generated.h"

/**
 * 상호작용 공통 타입.
 * 서버 protocol.h의 열거형과 순서를 맞춰 두었다(나중에 패킷 값과 바로 변환하기 위해).
 */

/** protocol.h EntityKind 와 같은 순서 */
UENUM(BlueprintType)
enum class EVHEntityKind : uint8
{
	Thief,
	Guard,
	Safe,
	ExitGate,
	HideSpot,
	DroppedTreasure,
	JailPoint,
	/** 반납지점 [기획 PDF 2장]. protocol.h EntityKind 에 아직 없음 → 서버 추가 요청 (클라이언트 전용 값) */
	ReturnPoint,
	// ── 서버 v3.0 아이템 (2-4b). 서버 엔티티 위치에 표시만 하고 판정은 서버가 한다 ──
	/** 상자: 공격(HP 20) 또는 3초 작업으로 부순다 */
	Box,
	/** 떨어져 있는 무기: F로 즉시 줍기 */
	WeaponPickup,
	/** 떨어져 있는 소비 아이템: F로 즉시 줍기 */
	ConsumablePickup,
	/** 필드 아이템(회복약): 닿으면 획득, F 상호작용 없음 */
	FieldItem
};

/** 커서 정보창의 색 [기획 2026-11: 품질별 반투명 창]. 품질이 없는 것(무기·소비·이름만)은 Plain */
UENUM(BlueprintType)
enum class EVHInfoTone : uint8
{
	Plain		UMETA(DisplayName = "기본(이름만·무기·소비)"),
	Common		UMETA(DisplayName = "일반(흰색)"),
	Advanced	UMETA(DisplayName = "고급(초록)"),
	Rare		UMETA(DisplayName = "희귀(파랑)"),
	Unique		UMETA(DisplayName = "보물(보라)")
};

/** 커서를 올렸을 때 뜨는 정보창 한 줄 ("권총/무기/대미지 10"). 금고·상자·필드 아이템은 이름만 */
struct FVHInfoCard
{
	FText Text;
	EVHInfoTone Tone = EVHInfoTone::Plain;
	/** 글자 왼쪽에 보여 줄 아이콘 (소비 아이템: HUD 소비 칸과 같은 그림). 없으면 글자만 */
	class UTexture2D* Icon = nullptr;
	/** 창 왼쪽 가장자리의 색 띠 (소비 아이템의 발광 띠 색). 알파 0 = 띠 없음 */
	FLinearColor Accent = FLinearColor::Transparent;
	bool IsValid() const { return !Text.IsEmpty(); }
};

/** 길게 누르기 / 짧게 누르기 */
UENUM(BlueprintType)
enum class EVHInteractMode : uint8
{
	Hold	UMETA(DisplayName = "길게 누르기"),
	Tap		UMETA(DisplayName = "짧게 누르기")
};

/** protocol.h InteractState 와 같은 순서 (SC_INTERACT.state) */
UENUM(BlueprintType)
enum class EVHInteractState : uint8
{
	Begin,
	Cancel,
	Complete
};

/** protocol.h RejectReason 과 같은 순서 (SC_ACTION_REJECT.reason) */
UENUM(BlueprintType)
enum class EVHRejectReason : uint8
{
	Cooldown,
	InvalidState,
	OutOfRange,
	NoFlashlight,
	TargetBusy,
	TreasureFull
};

/**
 * 지금 상호작용할 수 없는 이유(안내 문구용, 클라이언트 전용 상세 사유).
 * 서버로 보낼 때는 VHInteract::ToRejectReason()으로 RejectReason에 대응시킨다.
 */
UENUM(BlueprintType)
enum class EVHInteractBlock : uint8
{
	None,				// 가능
	Busy,				// 다른 의적이 사용 중
	NotAvailable,		// 내 상태가 상호작용 불가(사망 등)
	OutOfRange
};

/**
 * 상호작용 물품 테두리(외곽선) 상태 [팀 결정 2026-10-25].
 * 값이 그대로 커스텀 스텐실 값이 되고, 포스트 프로세스 머티리얼 M_VHOutline 이 값마다 다른 색으로 테두리를 그린다.
 */
UENUM(BlueprintType)
enum class EVHOutline : uint8
{
	None	= 0	UMETA(DisplayName = "없음"),
	Lit		= 1	UMETA(DisplayName = "빛 안"),			// 내 손전등 빛 안(가려지지 않음)
	Focus	= 2	UMETA(DisplayName = "현재 대상"),		// 더 진한 색
	Blocked	= 3	UMETA(DisplayName = "현재 대상(불가)")	// 회색
};

/** 의적 상태 (PvP) */
UENUM(BlueprintType)
enum class EVHThiefStatus : uint8
{
	Free		UMETA(DisplayName = "자유"),
	/** 사망 후 리스폰 대기 (안 보임·충돌 없음·이동 불가) */
	Dead		UMETA(DisplayName = "사망(리스폰 대기)")
};

/**
 * 대상이 "지금 이 의적에게" 제공하는 상호작용 1개.
 * 판정(UVHInteractionAuthority::Evaluate)의 결과이며, HUD 안내 문구도 이것으로 그린다.
 */
USTRUCT(BlueprintType)
struct FVHInteractOffer
{
	GENERATED_BODY()

	/** false면 이 대상은 지금 아무 상호작용도 없다(예: 빈 금고) → 대상으로 고르지 않음 */
	UPROPERTY(BlueprintReadOnly) bool bHasOffer = false;

	UPROPERTY(BlueprintReadOnly) EVHInteractMode Mode = EVHInteractMode::Tap;

	/** 홀드 = 처음부터 끝까지 걸리는 전체 시간, 탭 = 짧은 동작 시간 (초) */
	UPROPERTY(BlueprintReadOnly) float DurationSeconds = 0.5f;

	/** 홀드 취소 시 진행도 유지 여부 */
	UPROPERTY(BlueprintReadOnly) bool bKeepProgressOnCancel = false;

	/** "금고 열기" 같은 동작 이름 */
	UPROPERTY(BlueprintReadOnly) FText ActionText;

	/** None이 아니면 지금은 할 수 없음(회색 안내) */
	UPROPERTY(BlueprintReadOnly) EVHInteractBlock Block = EVHInteractBlock::None;

	/** 회색 안내 문구 (예: "다른 의적이 사용 중입니다") */
	UPROPERTY(BlueprintReadOnly) FText BlockText;

	bool IsAvailable() const { return bHasOffer && Block == EVHInteractBlock::None; }

	static FVHInteractOffer Make(EVHInteractMode InMode, float InSeconds, bool bInKeep, const FText& InAction)
	{
		FVHInteractOffer O;
		O.bHasOffer = true;
		O.Mode = InMode;
		O.DurationSeconds = InSeconds;
		O.bKeepProgressOnCancel = bInKeep;
		O.ActionText = InAction;
		return O;
	}

	FVHInteractOffer& Blocked(EVHInteractBlock InBlock, const FText& InText)
	{
		Block = InBlock;
		BlockText = InText;
		return *this;
	}
};

namespace VHInteract
{
	/** 상세 사유 → 서버 RejectReason */
	MCPTEST_API EVHRejectReason ToRejectReason(EVHInteractBlock Block);

	/** 서버 거절 사유 → 한국어 안내 */
	MCPTEST_API FText RejectReasonText(EVHRejectReason Reason);
}
