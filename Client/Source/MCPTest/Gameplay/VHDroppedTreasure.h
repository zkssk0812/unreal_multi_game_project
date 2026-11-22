#pragma once

#include "CoreMinimal.h"
#include "Gameplay/VHInteractableBase.h"
#include "Gameplay/VHGameplayTypes.h"
#include "VHDroppedTreasure.generated.h"

class UPointLightComponent;

/**
 * 떨어진 보물(물건) [기획 PDF 10장, 팀 결정 2026-10-25]
 * - PvP: 캐릭터가 사망할 때 들고 있던 보물을 주위 바닥에 떨어뜨린다 (사망은 나중 단계에서 구현)
 * - 떨어뜨리기 전의 가치를 그대로 가진다. 짧게 누르기로 다시 줍는다. 게임 끝까지 사라지지 않는다.
 * - 어두운 맵에서 찾기 쉽게 약하게 빛난다 [제안].
 * (레벨에 처음부터 놓는 용도로는 쓰지 않는다 — 보물은 금고에서만 나온다)
 */
UCLASS(Blueprintable)
class MCPTEST_API AVHDroppedTreasure : public AVHInteractableBase
{
	GENERATED_BODY()

public:
	AVHDroppedTreasure();

	/** 이 물건 (가치) */
	const FVHItemData& GetItem() const { return Item; }
	void SetItem(const FVHItemData& InItem) { Item = InItem; }

	virtual void SetOutline(EVHOutline State) override;
	/** "환금 아이템/일반/4$" — 품질에 따라 정보창 색이 달라진다 [기획 2026-11 2.1] */
	virtual FVHInfoCard GetInfoCard() const override;

	/** 품질 이름 (일반·고급·희귀·유일) */
	static FText QualityText(EVHItemQuality Quality);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPointLightComponent> Glow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VaultHeist")
	FVHItemData Item;

	/** 위아래로 떠다니는 연출 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VaultHeist|Visual")
	float BobHeight = 6.f;

private:
	float BaseBodyZ = 0.f;
	float TimeOffset = 0.f;
};
