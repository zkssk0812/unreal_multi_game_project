#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/VHInteractionTypes.h"
#include "VHStatusEffectComponent.generated.h"

class UStaticMeshComponent;

DECLARE_MULTICAST_DELEGATE_TwoParams(FVHOnStunChanged, class UVHStatusEffectComponent* /*Status*/, bool /*bStunned*/);

/**
 * 내 의적의 상태이상을 화면과 조작에 적용하는 컴포넌트. 누가 언제 걸리는지는 서버가 정한다(상태 비트·이벤트).
 *
 *  - 경직: 정해진 시간 동안 이동 입력을 막고 머리 위에 표시를 띄운다 (서버 상태 비트 → UVHServerGameSubsystem 이 ApplyStun)
 *  - 무적: 구르는 동안 UVHActionComponent가 켠다(표시용). 리스폰 무적은 시간이 정해져 있다(SC_RESPAWNED)
 *  - 탈진: 스테미나 0이 되면 UVHActionComponent가 켜고 끈다(표시용 상태만 보관)
 */
UCLASS(ClassGroup = (VaultHeist), meta = (BlueprintSpawnableComponent, DisplayName = "VH 상태이상"))
class MCPTEST_API UVHStatusEffectComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVHStatusEffectComponent();

	/** 의적(Thief) / 경비원(Guard) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VaultHeist")
	EVHEntityKind Kind = EVHEntityKind::Thief;

	// ── 조회 ──
	UFUNCTION(BlueprintPure, Category = "VaultHeist") bool IsStunned() const { return bStunned; }
	/** 구르기 무적 (리스폰 무적은 IsTimedInvulnerable) */
	UFUNCTION(BlueprintPure, Category = "VaultHeist") bool IsInvulnerable() const { return bInvulnerable; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist") bool IsExhausted() const { return bExhausted; }
	/** 리스폰 무적처럼 시간이 정해진 무적 중인가 */
	UFUNCTION(BlueprintPure, Category = "VaultHeist") bool IsTimedInvulnerable() const;
	float GetTimedInvulnerableRemaining() const;
	float GetStunRemaining() const;

	// ── 변경 (서버 결과 반영) ──
	/** 경직 시작: Seconds 동안 이동 입력을 막는다 */
	void ApplyStun(float Seconds);
	void SetInvulnerable(bool bOn) { bInvulnerable = bOn; }
	void SetExhausted(bool bOn) { bExhausted = bOn; }
	/** 리스폰 무적 시작 (Seconds초) */
	void ApplyTimedInvulnerable(float Seconds);
	/** 상태이상 전부 삭제 (쓰러짐·리스폰): 경직·탈진 표시·무적 */
	void ClearAll();

	/** 시간이 지났으면 경직을 끝낸다 (매 틱 호출) */
	void UpdateTimers();

	FVHOnStunChanged OnStunChanged;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	double GetNow() const;
	void EndStun();
	/** 조종하는 컨트롤러의 이동 입력을 막거나 푼다 (짝을 맞춰 호출) */
	void SetMoveInputBlocked(bool bBlocked);

	bool bStunned = false;
	bool bInvulnerable = false;
	bool bExhausted = false;
	bool bMoveBlocked = false;
	double InvulnerableEndTime = -1.0;
	double StunEndTime = 0.0;

	/** 경직 중 머리 위에서 도는 표시 */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> StunMarker;
};
