#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/VHInteractionTypes.h"
#include "Engine/TimerHandle.h"
#include "VHThiefStateComponent.generated.h"

DECLARE_MULTICAST_DELEGATE_ThreeParams(FVHOnThiefStatusChanged, class UVHThiefStateComponent* /*Thief*/, EVHThiefStatus /*Old*/, EVHThiefStatus /*New*/);

/**
 * 의적 상태(자유/쓰러짐) 담당 컴포넌트. 상태는 서버가 정한다(SC_DOWNED·SC_RESPAWNED).
 * 어떤 캐릭터든 이 컴포넌트를 붙이면 "의적"으로 등록된다.
 * 상태에 따른 기본 처리(쓰러지면 안 보이고 못 움직이게)도 여기서 한다.
 */
UCLASS(ClassGroup = (VaultHeist), meta = (BlueprintSpawnableComponent, DisplayName = "VH 의적 상태"))
class MCPTEST_API UVHThiefStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVHThiefStateComponent();

	UFUNCTION(BlueprintPure, Category = "VaultHeist")
	EVHThiefStatus GetStatus() const { return Status; }

	/** 자유 상태(상호작용·이동 가능) */
	UFUNCTION(BlueprintPure, Category = "VaultHeist")
	bool IsFree() const { return Status == EVHThiefStatus::Free; }

	/** 맵에 살아 있는가(사망·리스폰 대기가 아님) */
	bool IsInGame() const { return Status != EVHThiefStatus::Dead; }

	/** 판정 결과 반영 */
	void ApplyStatus(EVHThiefStatus NewStatus);

	int32 GetEntityId() const { return EntityId; }
	void SetEntityId(int32 InId) { EntityId = InId; }

	/** 화면에 보여줄 이름 (내 의적 = 로그인 이름) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VaultHeist")
	FString DisplayName;

	FVHOnThiefStatusChanged OnStatusChanged;

	/** HP 표시 컴포넌트(UVHHealthComponent)가 없으면 런타임에 붙인다 (BeginPlay에서 자동 호출) */
	static void EnsureCombatComponents(AActor* Owner);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 상태별 기본 연출: 사망 = 안 보임 + 충돌 없음 + 이동 불가 */
	void ApplyPresentation();

	UPROPERTY(VisibleInstanceOnly, Category = "VaultHeist")
	EVHThiefStatus Status = EVHThiefStatus::Free;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "VaultHeist")
	int32 EntityId = 0;

	/** 쓰러지는 모습(Down Anim)을 보여 준 뒤 숨기는 타이머 */
	FTimerHandle DownHideTimer;
	bool bDownPresented = false;

};
