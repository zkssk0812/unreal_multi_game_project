#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/VHGameplayTypes.h"
#include "VHActionComponent.generated.h"

class UCharacterMovementComponent;
class UVHStatusEffectComponent;

/**
 * 의적 행동 담당 컴포넌트 (캐릭터에 붙인다) [기획 PDF 3·5·6·10장, PvP 규칙 v1 3장].
 *
 *  - 능력치: 게임 시작 시 DA_VHGameplayConfig.ThiefStats 를 복사해 쓴다(퍽은 이 복사본을 고친다).
 *  - 스테미나: 최대 100, 특수 동작을 하지 않을 때 초당 10 회복. 0이 되면 탈진(회복 50%, 특수 동작 불가, 가득 차면 해제)
 *  - 걷기 3m/s / 달리기(Shift) 6m/s·초당 10pt / 구르기(Space) 0.5초 9m/s·20pt·무적
 *  - SP를 쓰는 특수 동작은 달리기·구르기뿐. 공격은 SP를 쓰지 않는다(서버가 판정)
 *  - 점프는 없다 [팀 결정] → 시작 시 캐릭터의 점프를 끈다
 *
 * 이동 속도(CharacterMovement.MaxWalkSpeed)는 이 컴포넌트만 정한다.
 */
UCLASS(ClassGroup = (VaultHeist), meta = (BlueprintSpawnableComponent, DisplayName = "VH 행동(스테미나·달리기·구르기)"))
class MCPTEST_API UVHActionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVHActionComponent();

	/** 켜면 아래 CustomStats를 쓴다(캐릭터별 능력치). 끄면 DA_VHGameplayConfig.ThiefStats */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VaultHeist")
	bool bUseCustomStats = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VaultHeist", meta = (EditCondition = "bUseCustomStats"))
	FVHCharacterStats CustomStats;

	/** 지금 적용 중인 능력치 (퍽은 GetMutableStats로 고친다) */
	const FVHCharacterStats& GetStats() const { return Stats; }
	FVHCharacterStats& GetMutableStats() { return Stats; }

	// ── 입력 (컨트롤러가 호출) ──
	void SetWantsSprint(bool bInWants) { bWantsSprint = bInWants; }
	/** 구르기 요청. 할 수 없으면 false (이유는 화면 알림) */
	bool RequestRoll();

	// ── 서버가 상태만 알려 주고 클라가 적용하는 이동 효과 [서버 v3.0 T-64] ──
	/** 걷기·달리기 속도 배율 (끈끈이 0.5, 은신 0.2, 둘 다면 곱). 1 = 보통. 구르기 속도는 바꾸지 않는다 */
	void SetMoveSpeedScale(float Scale);
	/** 달리기 속도 배율 (운동화 퍽 1.2) */
	void SetRunSpeedScale(float Scale);
	/** 달리기·구르기 금지 (은신 중: 특수 동작 불가). 구르는 중이면 그 구르기는 끝까지 한다 */
	void SetSpecialBlocked(bool bBlocked);
	/** 이동 잠금 (레일건 발사 1초: 이동 불가). 회전 잠금은 UVHAimComponent::SetTurnLocked */
	void SetMoveLocked(bool bLocked);
	/** 제트팩: 켜져 있는 동안 커서 방향으로 Speed(cm/s)로 날아간다. 방향은 커서를 따라 바뀌고, 이동 키로 멈출 수 없다 */
	void SetJetpack(bool bOn, float Speed);
	/** 밀려남 (제트팩에 부딪힘): Seconds 동안 Direction 쪽으로 Distance(cm). 벽에 막히면 거기서 멈춘다 */
	void StartKnockback(const FVector& Direction, float Distance, float Seconds);
	bool IsMoveLocked() const { return bFireLocked; }
	bool IsJetpack() const { return bJetpack; }
	bool IsKnockedBack() const;
	float GetMoveSpeedScale() const { return MoveSpeedScale; }
	float GetRunSpeedScale() const { return RunSpeedScale; }
	bool IsSpecialBlocked() const { return bSpecialBlocked; }

	// ── 사망·리스폰 [PvP 규칙 v1 4장] ──
	/** 사망: 구르기 중지, 달리기 입력 해제 */
	void StopActions();
	/** 리스폰: SP 최대, 탈진 해제, 구르기 중지 */
	void ResetForRespawn();

	// ── 조회 ──
	UFUNCTION(BlueprintPure, Category = "VaultHeist") float GetStamina() const { return Stamina; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist") float GetMaxStamina() const { return Stats.MaxStamina; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist") bool IsRunning() const { return bRunning; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist") bool IsRolling() const { return bRolling; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist") bool IsExhausted() const;
	/** 구르기처럼 다른 동작을 막는 동작 중인가 */
	bool IsBusy() const { return bRolling; }

	/** 지금 최대 속도 (cm/s) */
	float GetCurrentMaxSpeed() const { return CurrentMaxSpeed; }

	/** 개발용: 스테미나 직접 설정 (0이면 탈진) */
	void DebugSetStamina(float Value);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	double GetNow() const;
	/** 스테미나를 쓴다. 0이 되면 탈진 */
	void SpendStamina(float Amount);
	void SetExhausted(bool bOn);
	void EndRoll();
	/** 조종하는 컨트롤러의 이동 입력을 막거나 푼다 (짝을 맞춰 호출) */
	void SetMoveInputBlocked(bool bBlocked);
	/** 의적 상태가 자유이고 경직이 아닌가 (특수 동작 가능 기본 조건) */
	bool CanAct() const;
	/** 내 화면 알림 (조종하는 플레이어일 때만) */
	void PostMessage(const FText& Message) const;

	UVHStatusEffectComponent* GetStatus() const;

	FVHCharacterStats Stats;
	float Stamina = 100.f;
	bool bExhausted = false;
	bool bWantsSprint = false;
	bool bRunning = false;
	float CurrentMaxSpeed = 0.f;
	float MoveSpeedScale = 1.f;
	float RunSpeedScale = 1.f;
	bool bSpecialBlocked = false;
	bool bFireLocked = false;		// 레일건 잠금 (bMoveBlocked 는 "플레이어 이동 입력을 막았는가")
	bool bJetpack = false;
	float JetpackSpeed = 1000.f;
	double KnockbackEndTime = -1.0;
	FVector KnockbackDirection = FVector::ForwardVector;
	float KnockbackSpeed = 0.f;
	bool bAccelBoosted = false;		// 강제 이동(제트팩·밀려남) 동안 가속을 크게 해 둠

	// 구르기
	bool bRolling = false;
	double RollEndTime = 0.0;
	FVector RollDirection = FVector::ForwardVector;
	float SavedMaxAcceleration = 0.f;
	bool bMoveBlocked = false;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> Movement;

	/** 캐릭터 메시의 원래 크기 (구를 때 웅크린 모양 표시용) */
	FVector MeshBaseScale = FVector::OneVector;
};
