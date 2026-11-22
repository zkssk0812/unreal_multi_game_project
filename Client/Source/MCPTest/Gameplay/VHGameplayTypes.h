#pragma once

#include "CoreMinimal.h"
#include "VHGameplayTypes.generated.h"

/**
 * 게임 규칙 공통 타입 (물건·캐릭터 능력치).
 * 수치 기본값은 Docs/게임_시스템.pdf 기준. 실제 값은 DA_VHGameplayConfig에서 바꾼다.
 */

/**
 * 물건 종류 [기획 PDF 9장].
 * 이번에는 보물만 쓴다. 의자·그림·책·꽃병은 다음에 종류만 추가하면 같은 구조(줍기·소지·떨어뜨리기·반납)로 붙일 수 있다.
 */
UENUM(BlueprintType)
enum class EVHItemKind : uint8
{
	Treasure	UMETA(DisplayName = "보물"),
	Chair		UMETA(DisplayName = "의자"),
	Painting	UMETA(DisplayName = "그림"),
	Book		UMETA(DisplayName = "책"),
	Vase		UMETA(DisplayName = "꽃병")
};

/** 환금 아이템 품질 [기획 2026-11]. 서버 ValuableQuality 와 같은 순서. 보물 = 유일 */
UENUM(BlueprintType)
enum class EVHItemQuality : uint8
{
	Common		UMETA(DisplayName = "일반"),
	Advanced	UMETA(DisplayName = "고급"),
	Rare		UMETA(DisplayName = "희귀"),
	Unique		UMETA(DisplayName = "유일(보물)")
};

/** 물건 하나 (들고 다니고, 떨어뜨리고, 반납하는 단위) */
USTRUCT(BlueprintType)
struct FVHItemData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	EVHItemKind Kind = EVHItemKind::Treasure;

	/** 품질 (Kind 가 보물이면 항상 유일로 본다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	EVHItemQuality Quality = EVHItemQuality::Common;

	EVHItemQuality GetQuality() const { return Kind == EVHItemKind::Treasure ? EVHItemQuality::Unique : Quality; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FText DisplayName;

	/** 가치 ($) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item", meta = (ClampMin = 0))
	int32 Value = 0;

	static FVHItemData MakeTreasure(const FText& InName, int32 InValue)
	{
		FVHItemData D;
		D.Kind = EVHItemKind::Treasure;
		D.DisplayName = InName;
		D.Value = InValue;
		return D;
	}
};

/**
 * 캐릭터 능력치 [기획 PDF 3·5·6·7장]. 퍽으로 바뀔 수 있는 값은 전부 여기에 둔다.
 * 캐릭터의 UVHActionComponent가 게임 시작 시 이 값을 복사해 쓰고, 퍽은 그 복사본을 고치면 된다.
 * 길이·속도는 언리얼 단위(cm, cm/s), 시간은 초.
 */
USTRUCT(BlueprintType)
struct FVHCharacterStats
{
	GENERATED_BODY()

	/** 걷기 속도 3m/s */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (Units = "cm/s"))
	float WalkSpeed = 300.f;

	/** 달리기 속도 6m/s */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (Units = "cm/s"))
	float RunSpeed = 600.f;

	/** 구르기 속도 9m/s */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (Units = "cm/s"))
	float RollSpeed = 900.f;

	/** 구르기 시간 0.5초 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (Units = "s"))
	float RollSeconds = 0.5f;

	/** 최대 스테미나 100pt */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina")
	float MaxStamina = 100.f;

	/** 자연 회복 초당 10pt (특수 동작을 하지 않을 때) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina")
	float StaminaRegenPerSecond = 10.f;

	/** 달리기 소비 초당 10pt */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina")
	float RunStaminaPerSecond = 10.f;

	/** 구르기 소비 20pt */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina")
	float RollStaminaCost = 20.f;

	/** 최대 HP 100 (HUD 표시용. 서버 값과 같게) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health", meta = (ClampMin = 1))
	float MaxHP = 100.f;
};
