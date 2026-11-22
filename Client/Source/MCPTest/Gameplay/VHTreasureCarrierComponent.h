#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/VHGameplayTypes.h"
#include "VHTreasureCarrierComponent.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FVHOnCarryChanged, class UVHTreasureCarrierComponent*);

/**
 * 내 의적이 들고 있는 환금 아이템을 화면에 보여 주려고 들고 있는 그릇. 줍기·반납 판정은 서버가 한다.
 *  - 개수: 서버가 주웠다고 알려 올 때 ApplyAdd, 반납·쓰러짐 때 ApplyRemoveAll
 *  - 가치 합계: 서버 SC_MONEY 의 "들고 있는 가치"(ApplyServerCarried)
 */
UCLASS(ClassGroup = (VaultHeist), meta = (BlueprintSpawnableComponent, DisplayName = "VH 보물 소지"))
class MCPTEST_API UVHTreasureCarrierComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVHTreasureCarrierComponent();

	UFUNCTION(BlueprintPure, Category = "VaultHeist")
	int32 GetCount() const { return Items.Num(); }

	/** 들고 있는 물건 가치 합계 ($) */
	UFUNCTION(BlueprintPure, Category = "VaultHeist")
	int32 GetTotalValue() const;

	const TArray<FVHItemData>& GetItems() const { return Items; }

	// ── 서버 결과 반영 ──
	void ApplyAdd(const FVHItemData& Item);
	/** 전부 내려놓는다(반납·쓰러짐). 내려놓은 물건 목록을 돌려준다 */
	TArray<FVHItemData> ApplyRemoveAll();

	/** SC_MONEY 의 "들고 있는 가치"를 그대로 쓴다 (GetTotalValue 가 이 값을 돌려준다) */
	void ApplyServerCarried(int32 ServerValue);
	FVHOnCarryChanged OnCarryChanged;

protected:
	UPROPERTY(VisibleInstanceOnly, Category = "VaultHeist")
	TArray<FVHItemData> Items;
	/** 서버가 알려 준 들고 있는 가치 합계 (서버 값이 오기 전에는 비어 있다) */
	TOptional<int32> ServerCarriedValue;
};
