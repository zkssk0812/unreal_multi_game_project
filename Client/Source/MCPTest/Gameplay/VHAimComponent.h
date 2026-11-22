#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VHAimComponent.generated.h"

class UStaticMeshComponent;
class USceneComponent;

/**
 * 시선·이동 기준 컴포넌트 (캐릭터에 붙인다) ⟪기획 변경 2026-10-26: 팀 결정⟫.
 *
 *  - 탑뷰 카메라: 캐릭터의 스프링암을 위에서 내려다보는 고정 각도로 맞춘다(회전 없음).
 *  - 캐릭터의 "앞" = 마우스 커서가 가리키는 바닥 지점 방향(AimYaw). 커서가 1m 안이면 마지막 방향 유지.
 *  - 몸 전체가 AimYaw 쪽으로 빠르게 돈다(컨트롤러 회전 = AimYaw, CharacterMovement가 BodyTurnSpeed로 따라감).
 *    손전등은 몸에 붙어 있어 함께 돈다. 시선(GetLookYaw) = 몸 방향.
 *  - WASD 이동은 AimYaw 기준(W 앞 / S 뒤 / A 왼쪽 / D 오른쪽) — AMCPTestCharacter::DoMove → VHRules::AimRelativeMove.
 *  - 경직·사망 중에는 방향을 바꾸지 않는다.
 *  (이전: W = 월드 X 고정, 몸통은 이동 방향, 시선만 몸통 기준 ±90° — 게임_시스템.pdf 4장 방식은 대체됨)
 *
 * 내가 조종하지 않는 캐릭터(더미 등)는 예전처럼 이동 방향으로 돌고, 시선 = 몸 방향.
 */
UCLASS(ClassGroup = (VaultHeist), meta = (BlueprintSpawnableComponent, DisplayName = "VH 시선"))
class MCPTEST_API UVHAimComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVHAimComponent();

	/** 시선 방향(수평 단위 벡터) */
	UFUNCTION(BlueprintPure, Category = "VaultHeist")
	FVector GetLookDirection() const;

	/** 시선 방향(월드 Yaw, 도) = 몸 방향 */
	UFUNCTION(BlueprintPure, Category = "VaultHeist")
	float GetLookYaw() const;

	/** 커서가 가리키는 방향(월드 Yaw). 이동 기준·몸이 돌아갈 목표 */
	UFUNCTION(BlueprintPure, Category = "VaultHeist")
	float GetAimYaw() const { return AimYaw; }

	/** 마우스가 가리키는 바닥 지점 (내 캐릭터만) */
	UFUNCTION(BlueprintPure, Category = "VaultHeist")
	FVector GetAimPoint() const { return AimPoint; }

	/** 이동 입력(Forward = W/S, Right = D/A)을 커서 기준 월드 이동 벡터로 (길이 = 세기 × 방향별 배율) */
	FVector ComputeMoveVector(float Forward, float Right) const;

	/** 지금 이동 기준이 화면(월드) 축 고정인지 (설정 bScreenAxisMove, 콘솔 vh.MoveMode로 바꿀 수 있음) */
	static bool IsScreenAxisMove();

	/** 몸과 시선을 이 방향(Yaw)으로 바로 맞춘다. 개발용·순간이동 후 (커서가 움직이면 다시 커서를 따라간다) */
	void SnapLookYaw(float WorldYaw);

	/** 시선 고정 (레일건 발사 중: 커서를 움직여도 몸이 돌지 않는다) */
	void SetTurnLocked(bool bLocked) { bTurnLocked = bLocked; }
	bool IsTurnLocked() const { return bTurnLocked; }

	/** 캐릭터의 스프링암·카메라를 탑뷰 설정으로 맞춘다 (BeginPlay에서 자동 호출) */
	void ApplyTopDownCamera();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	bool IsLocallyControlledPlayer() const;
	/** 마우스 → 바닥 평면 교차점. 실패하면 false */
	bool TraceMouseToFloor(FVector& OutPoint) const;
	/** 경직·사망이면 방향을 바꾸지 않는다 */
	bool CanTurn() const;
	/** 내 캐릭터면 "몸이 컨트롤러 방향(=커서)을 따라 돈다"로, 아니면 "이동 방향으로 돈다"로 */
	void ApplyRotationMode(bool bPlayer);
	void ApplyFollowers();
	/** 벽 가림 처리용 파라미터(내 캐릭터 위치)를 벽 재질에 넘긴다 */
	void UpdateOcclusion(const class UVHGameplayConfig& C);
	void CreateIndicator();

	float AimYaw = 0.f;
	FVector AimPoint = FVector::ZeroVector;
	bool bPlayerRotationMode = false;
	bool bTurnLocked = false;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> LookIndicator;
};
