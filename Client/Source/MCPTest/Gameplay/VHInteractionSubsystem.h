#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/VHInteractionTypes.h"
#include "Gameplay/VHGameplayTypes.h"
#include "VHInteractionSubsystem.generated.h"

class AVHInteractableBase;
class AVHDroppedTreasure;
class UVHInteractionComponent;
class UVHThiefStateComponent;
class UVHFlashlightComponent;
class UVHInteractionAuthority;

DECLARE_MULTICAST_DELEGATE(FVHOnTeamTreasureChanged);
DECLARE_MULTICAST_DELEGATE_TwoParams(FVHOnWorldMessage, const FText& /*Message*/, bool /*bIsError*/);

/**
 * 게임 레벨의 상호작용 관리자 (레벨마다 1개). 규칙 판정은 하지 않는다(서버 v3.0 이 판정).
 *
 *  - 등록부: 상호작용 오브젝트와 의적 목록, 엔티티 ID 발급
 *  - 담당(Authority) 보관: 기본은 "무엇을 할 수 있는가"만 계산하는 UVHLocalInteractionAuthority,
 *    서버와 게임 중이면 요청을 CS_ACTION 으로 보내는 UVHServerInteractionAuthority
 *  - "결과 반영" 함수(Apply*): 서버 패킷(SC_INTERACT, SC_ACTION_REJECT …)의 결과를 화면/상태에 반영하는 입구
 */
UCLASS()
class MCPTEST_API UVHInteractionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// UWorldSubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// ── 등록부 ──
	void RegisterInteractable(AVHInteractableBase* Target);
	void UnregisterInteractable(AVHInteractableBase* Target);
	void RegisterThief(UVHThiefStateComponent* Thief);
	void UnregisterThief(UVHThiefStateComponent* Thief);

	TArray<AVHInteractableBase*> GetInteractables() const;
	TArray<UVHThiefStateComponent*> GetThieves() const;
	AVHInteractableBase* FindByEntityId(int32 EntityId) const;

	UVHInteractionAuthority* GetAuthority() const { return Authority; }
	/** 담당 교체 (서버와 게임 중: UVHServerInteractionAuthority) */
	void SetAuthority(UVHInteractionAuthority* NewAuthority);

	/** 진행도 계산에 쓰는 시각(초). 게임 시간 */
	double GetNow() const;

	/** 개발용: 상호작용 시간 배율 (0.1 = 10배 빠르게, 안내 문구의 시간 표시에만 영향) */
	float DebugDurationScale = 1.f;

	/** 자동화 테스트용: 값이 있으면 GetNow()가 이 시각을 돌려준다 */
	TOptional<double> NowOverride;

	// ── 결과 반영 (서버 패킷 → 화면) ──

	/** SC_INTERACT. Begin이면 Duration = 남은 시간, Total = 전체 시간(진행도 계산용) */
	void ApplyInteract(UVHInteractionComponent* Actor, AVHInteractableBase* Target, EVHInteractState State,
		double StartTime, float DurationSeconds, float TotalSeconds, EVHInteractMode Mode, bool bKeepProgress, const FText& ActionText);

	/** SC_ACTION_REJECT */
	void ApplyReject(UVHInteractionComponent* Actor, EVHRejectReason Reason, const FText& Detail);

	/** 의적 상태 변경 (자유/쓰러짐) — 표시만 바꾼다 */
	void ApplyThiefStatus(UVHThiefStateComponent* Thief, EVHThiefStatus Status);

	/** 알림·로그용 의적 이름 */
	static FString GetThiefLabel(const UVHThiefStateComponent* Thief);

	/** 떨어진 환금 아이템 생성 (SC_ENTITY_SPAWN) */
	AVHDroppedTreasure* SpawnDroppedTreasure(const FVector& Location, const FVHItemData& Item);

	/** 손전등 켜짐/꺼짐 (지금은 꺼 둔 기능. 코드 삭제는 기획 확인 뒤) */
	void ApplyFlashlight(UVHFlashlightComponent* Light, bool bOn);
	/** 손전등 요청 거절 */
	void ApplyFlashlightReject(UVHFlashlightComponent* Light, EVHRejectReason Reason);

	/** 화면 알림 */
	void PostMessage(const FText& Message, bool bIsError = false);

	FVHOnTeamTreasureChanged OnTeamTreasureChanged;
	FVHOnWorldMessage OnWorldMessage;

	/** 떨어진 환금 아이템 클래스(블루프린트 외형으로 바꾸고 싶을 때 컨트롤러가 지정) */
	UPROPERTY()
	TSubclassOf<AVHDroppedTreasure> DroppedTreasureClass;

private:
	TArray<TWeakObjectPtr<AVHInteractableBase>> Interactables;
	TArray<TWeakObjectPtr<UVHThiefStateComponent>> Thieves;

	UPROPERTY()
	TObjectPtr<UVHInteractionAuthority> Authority;

	int32 NextEntityId = 1;
};
