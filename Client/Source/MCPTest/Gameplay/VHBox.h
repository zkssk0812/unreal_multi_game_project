#pragma once

#include "CoreMinimal.h"
#include "Gameplay/VHInteractableBase.h"
#include "VHBox.generated.h"

/**
 * 상자 [기획 2026-11 2.3, 서버 v3.0].
 * - 공격(HP 20) 또는 3초 작업으로 부순다. 작업을 멈추면 처음부터. 부서지면 서버가 정한 아이템이 그 자리에 나온다.
 * - 서버 엔티티(EntityKind::Box) 위치에 만들어 보여 주기만 한다. HP·작업·드롭 판정은 모두 서버가 한다.
 * - [2026-11-24 팀 서버] 두 종류: type_id = BoxType 1 무기 상자(무기 80·소비 20) / 2 환금 상자(환금 아이템 100).
 *   모양·색·안내 문구로 구분: 무기 상자 = 짙은 청회색 철제 상자 + 하늘색 띠, 환금 상자 = 나무 상자 + 금색 띠·금화.
 * - 외형은 임시 기본 도형. 블루프린트 자식에서 메시를 바꾸면 된다.
 */
UCLASS(Blueprintable)
class MCPTEST_API AVHBox : public AVHInteractableBase
{
	GENERATED_BODY()

public:
	AVHBox();

	/** 서버가 알려 준 남은 HP (스냅샷) */
	void SetServerHp(int32 InHp) { ServerHp = InHp; }
	int32 GetServerHp() const { return ServerHp; }

	/** 서버 BoxType (0 모름, 1 무기 상자, 2 환금 상자) → 모양·색 */
	void SetBoxType(uint8 InType);
	uint8 GetBoxType() const { return BoxType; }
	bool IsWeaponBox() const { return BoxType == 1; }
	bool IsValuableBox() const { return BoxType == 2; }
	/** "무기 상자" / "환금 상자" / "상자" */
	FText GetBoxName() const;

	virtual FVector GetWorkPoint() const override { return GetActorLocation(); }
	virtual float GetWorkApproachDistance() const override;
	virtual FVHInfoCard GetInfoCard() const override;
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	void ApplyLook();

	/** 한 변 길이 (서버 히트 박스 반지름 50cm에 맞춘 80cm 상자) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VaultHeist|Visual", meta = (ClampMin = 10, Units = "cm"))
	float BoxSize = 80.f;

	/** 무기 상자: 몸통·띠·위 표시 색 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VaultHeist|Visual")
	FLinearColor WeaponBoxColor = FLinearColor(0.16f, 0.2f, 0.26f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VaultHeist|Visual")
	FLinearColor WeaponBandColor = FLinearColor(0.25f, 0.75f, 1.f);

	/** 환금 상자: 몸통·띠·금화 색 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VaultHeist|Visual")
	FLinearColor ValuableBoxColor = FLinearColor(0.45f, 0.27f, 0.1f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VaultHeist|Visual")
	FLinearColor ValuableBandColor = FLinearColor(1.f, 0.75f, 0.15f);

	/** 가운데 띠 (상자를 두르는 얇은 판) */
	UPROPERTY(VisibleAnywhere, Category = "VaultHeist|Visual")
	TObjectPtr<UStaticMeshComponent> Band;
	/** 뚜껑 위 표시: 무기 상자 = 가로 막대(총 모양), 환금 상자 = 금화(납작한 원기둥) */
	UPROPERTY(VisibleAnywhere, Category = "VaultHeist|Visual")
	TObjectPtr<UStaticMeshComponent> Emblem;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "VaultHeist")
	int32 ServerHp = 20;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "VaultHeist")
	uint8 BoxType = 0;
};
