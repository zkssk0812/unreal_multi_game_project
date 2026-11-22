#pragma once

#include "CoreMinimal.h"
#include "Gameplay/VHInteractableBase.h"
#include "VHItemPickup.generated.h"

/**
 * 바닥에 놓인 아이템 [기획 2026-11 3장, 서버 v3.0]: 무기 / 소비 아이템 / 필드 아이템(회복약).
 * - 무기·소비 아이템: 3m 안에서 F로 즉시 줍는다 (칸이 차 있으면 서버가 바꿔 주고, 들고 있던 것은 그 자리에 떨어진다)
 * - 필드 아이템: F 상호작용 없음. 닿으면 서버가 바로 효과를 준다(HP가 가득이면 그대로 남는다). 커서를 올리면 이름만 보인다
 * 서버 엔티티 위치에 만들어 보여 주기만 한다. 종류(TypeId)·남은 탄(Param)은 서버 값 그대로다.
 * 외형: 무기 = DA_VHWeaponCatalog 의 모델(구상안 v2.2. 모델이 없는 무기는 주황 막대), 소비 = 하늘색 원통, 회복약 = 초록 공(소·중·대 크기).
 */
UCLASS(Blueprintable)
class MCPTEST_API AVHItemPickup : public AVHInteractableBase
{
	GENERATED_BODY()

public:
	AVHItemPickup();

	/** 스폰 직후(FinishSpawning 전)에 부른다. InKind = WeaponPickup / ConsumablePickup / FieldItem */
	void InitPickup(EVHEntityKind InKind, int32 InTypeId, int32 InParam);

	int32 GetTypeId() const { return TypeId; }
	int32 GetParam() const { return Param; }
	/** "권총", "회복 키트", "회복약(소)" */
	FText GetItemName() const;

	virtual FVHInfoCard GetInfoCard() const override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 서버 종류 번호 (RobProto::WeaponId / ConsumableId / FieldItemId) */
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "VaultHeist")
	int32 TypeId = 0;

	/** 무기: 남은 탄 (0 = 무제한) */
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "VaultHeist")
	int32 Param = 0;

	/** 위아래로 떠다니는 연출 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VaultHeist|Visual")
	float BobHeight = 5.f;

private:
	float BaseBodyZ = 0.f;
	float TimeOffset = 0.f;
	/** 무기 모델(DA_VHWeaponCatalog)을 쓰는 중인가. 아니면 임시 도형 */
	bool bWeaponModel = false;
	FTransform FloorBase;
	FVector ModelCenter = FVector::ZeroVector;
	float SpinYaw = 0.f;
};
