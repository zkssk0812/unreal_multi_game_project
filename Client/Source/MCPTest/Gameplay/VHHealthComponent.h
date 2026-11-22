#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VHHealthComponent.generated.h"

class UVHHealthComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FVHOnHealthChanged, UVHHealthComponent* /*Health*/);

/**
 * 내 의적의 HP·보호막을 화면(HUD)에 보여 주려고 들고 있는 그릇. 대미지·쓰러짐 판정은 하지 않는다.
 *
 *  - 값은 서버가 SC_PLAYER_STATUS 로 알려 준 것을 UVHServerGameSubsystem 이 SetHP / SetShield 로 넣는다.
 *  - 최대 HP는 표시용 설정값(DA_VHGameplayConfig.ThiefStats.MaxHP, 서버와 같은 100).
 *
 * 의적 캐릭터에는 UVHThiefStateComponent 가 BeginPlay 때 없으면 자동으로 붙인다(블루프린트 수정 불필요).
 */
UCLASS(ClassGroup = (VaultHeist), meta = (BlueprintSpawnableComponent, DisplayName = "VH HP"))
class MCPTEST_API UVHHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVHHealthComponent();

	/** 켜면 시작할 때 최대 HP를 DA_VHGameplayConfig.ThiefStats.MaxHP 에서 읽는다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VaultHeist")
	bool bUseConfigValues = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VaultHeist", meta = (ClampMin = 1, EditCondition = "!bUseConfigValues"))
	float MaxHP = 100.f;

	// ── 조회 (HUD용) ──
	UFUNCTION(BlueprintPure, Category = "VaultHeist") float GetHP() const { return HP; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist") float GetMaxHP() const { return MaxHP; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist") float GetShield() const { return Shield; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist") float GetHPRatio() const { return MaxHP > 0.f ? HP / MaxHP : 0.f; }

	// ── 서버 값 반영 ──
	void SetHP(float Value);
	void SetShield(float Value);

	FVHOnHealthChanged OnHealthChanged;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleInstanceOnly, Category = "VaultHeist")
	float HP = 100.f;

	UPROPERTY(VisibleInstanceOnly, Category = "VaultHeist")
	float Shield = 0.f;
};
