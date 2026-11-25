#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/VHInteractionTypes.h"
#include "VHInteractionComponent.generated.h"

class AVHInteractableBase;
class UVHInteractionSubsystem;

DECLARE_MULTICAST_DELEGATE_TwoParams(FVHOnInteractMessage, const FText& /*Message*/, bool /*bIsError*/);

/**
 * 상호작용 담당 컴포넌트 (캐릭터에 붙인다).
 *
 * 하는 일
 *  1) 매 프레임 주변(반경·바라보는 방향)에서 "현재 대상" 1개를 고르고 외곽선을 켠다
 *  2) 입력(컨트롤러가 전달)을 판정 담당(Authority)에게 "요청"으로 보낸다
 *  3) 판정 결과(시작/취소/완료/거절)를 받아 상태·이동 잠금·HUD 정보를 갱신한다
 *
 * 직접 판정하지 않는다. → 서버 판정으로 바꿔도 이 컴포넌트는 그대로 쓴다.
 */
UCLASS(ClassGroup = (VaultHeist), meta = (BlueprintSpawnableComponent, DisplayName = "VH 상호작용"))
class MCPTEST_API UVHInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVHInteractionComponent();

	// ── 입력 (컨트롤러가 호출) ──
	void InteractPressed();
	void InteractReleased();
	/** 이동 입력이 들어옴 → 길게 누르기 취소 */
	void NotifyMoveInput();
	/** 점프·구르기·공격 같은 동작 입력 → 길게 누르기 취소 */
	void NotifyActionInput();

	// ── 상태 조회 (HUD) ──
	AVHInteractableBase* GetFocusTarget() const { return FocusTarget.Get(); }
	/** 마우스 커서를 올린 물품 (거리와 관계없음). 정보창을 띄우고, 3m 안이면 F 대상으로 우선한다 */
	AVHInteractableBase* GetHoverTarget() const { return HoverTarget.Get(); }
	/** 개발용(봇): 실제 마우스 대신 이 물품의 화면 위치를 커서 위치로 쓴다. nullptr 이면 실제 마우스 */
	void DebugSetHoverAt(AVHInteractableBase* Target) { DebugHoverAt = Target; bDebugHover = Target != nullptr; }
	const FVHInteractOffer& GetFocusOffer() const { return FocusOffer; }

	UFUNCTION(BlueprintPure, Category = "VaultHeist")
	bool IsInteracting() const { return ActiveTarget.IsValid(); }
	/** 상호작용 키를 누르고 있는가 (서버 판정: 시작 응답 전에 손을 뗐는지 확인용) */
	bool IsInteractHeld() const { return bInteractHeld; }
	/** F를 누른 대상의 작업 지점으로 다가가는 중인가 ("한 번 누르기" 방식) */
	bool IsApproaching() const { return ApproachTarget.IsValid(); }
	/** 개발용(VHUse): 시선·손전등과 관계없이 이 대상에 상호작용 키를 누른 것처럼 */
	void DebugPressOn(AVHInteractableBase* Target);

	AVHInteractableBase* GetActiveTarget() const { return ActiveTarget.Get(); }
	EVHInteractMode GetActiveMode() const { return ActiveMode; }
	const FText& GetActiveActionText() const { return ActiveActionText; }

	/** 진행 중인 상호작용의 진행도(0~1)와 남은 초 */
	float GetActiveProgress() const;
	float GetActiveRemainingSeconds() const;

	// ── 판정 결과 (SC_INTERACT / SC_ACTION_REJECT 에 해당). UVHInteractionSubsystem이 호출 ──
	void HandleInteractState(AVHInteractableBase* Target, EVHInteractState State, double StartTime, float DurationSeconds, EVHInteractMode Mode, const FText& ActionText);
	void HandleReject(EVHRejectReason Reason, const FText& Detail);

	/** 안내 메시지(토스트) */
	FVHOnInteractMessage OnMessage;

	/** 다른 컴포넌트가 이 의적을 찾을 때 */
	AActor* GetOwnerActor() const { return GetOwner(); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 커서 아래 물품 찾기: 물품 경계를 화면에 비춘 사각형 안에 커서가 있는가 */
	AVHInteractableBase* FindHoverTarget() const;
	/** 자동 반납: 환금 아이템을 들고 반납지점 범위에 들어가면 요청을 보낸다 (설정 bAutoReturn) */
	void TickAutoReturn();
	/** F로 고를 수 있는 물품인가 (자동 반납이면 반납지점은 F 대상이 아니다) */
	bool IsPressTarget(const AVHInteractableBase* Target) const;
	/** 대상 찾기. Visible = 지금 내 눈에 보이는 물품(손전등 빛 안·항상 보임) */
	void UpdateFocus(const TSet<AVHInteractableBase*>& Visible);
	/** 물품 테두리 상태를 매 프레임 정한다 (빛 안 / 현재 대상 / 불가) */
	void UpdateOutlines(const TSet<AVHInteractableBase*>& Visible);
	/** F 대상 문 테두리 빛 (UVHDoorSubsystem::SetFocusDoor) [2026-11-25] */
	void UpdateDoorOutline();
	void SetFocus(AVHInteractableBase* NewTarget, const FVHInteractOffer& Offer);
	void LockMovement(bool bLock);
	void FaceTarget(const AVHInteractableBase* Target);
	UVHInteractionSubsystem* GetSubsystem() const;

	/** 다른 플레이어가 조작하는 캐릭터(더미 등)는 대상 찾기·외곽선을 하지 않는다 */
	bool IsLocallyControlledPlayer() const;

	TWeakObjectPtr<AVHInteractableBase> FocusTarget;
	FVHInteractOffer FocusOffer;
	bool bFocusAvailable = false;

	TWeakObjectPtr<AVHInteractableBase> HoverTarget;
	TWeakObjectPtr<AVHInteractableBase> DebugHoverAt;
	bool bDebugHover = false;
	double LastAutoReturnTime = -100.0;

	TWeakObjectPtr<AVHInteractableBase> ActiveTarget;
	EVHInteractMode ActiveMode = EVHInteractMode::Tap;
	FText ActiveActionText;
	double ActiveStartTime = 0.0;
	float ActiveDuration = 0.f;

	bool bInteractHeld = false;
	bool bMovementLocked = false;

	// "한 번 누르기" 방식: F를 누른 금고·상자의 작업 지점까지 걸어가는 중 (도착하면 작업을 요청한다)
	TWeakObjectPtr<AVHInteractableBase> ApproachTarget;
	double ApproachStartTime = 0.0;
	void TickApproach();
	void StopApproach();
};
