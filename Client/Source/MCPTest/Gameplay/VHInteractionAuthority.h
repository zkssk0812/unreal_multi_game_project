#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Gameplay/VHInteractionTypes.h"
#include "Gameplay/VHGameplayTypes.h"
#include "VHInteractionAuthority.generated.h"

class UVHInteractionSubsystem;
class UVHInteractionComponent;
class UVHFlashlightComponent;
class AVHInteractableBase;

/**
 * 상호작용 "담당" 인터페이스. 의적의 요청을 받아 어디로 보낼지 정한다.
 *
 * 클라이언트 → 담당: RequestBegin / RequestCancel  (= CS_ACTION InteractBegin / InteractCancel)
 * 결과 → 화면:      UVHInteractionSubsystem::Apply*  (= SC_INTERACT, SC_ACTION_REJECT …)
 *
 * 규칙 판정(누가 먼저 열었는가, 시간이 다 됐는가, 무엇을 얻는가)은 서버 v3.0 이 한다.
 * Evaluate 는 판정이 아니라 "지금 이 대상과 무엇을 할 수 있어 보이는가"를 화면(안내 문구·테두리)에 쓰려고 미리 계산하는 것이다.
 */
UCLASS(Abstract)
class MCPTEST_API UVHInteractionAuthority : public UObject
{
	GENERATED_BODY()

public:
	virtual void Init(UVHInteractionSubsystem* InSubsystem) { Subsystem = InSubsystem; }

	/** 이 의적이 이 대상과 지금 무엇을 할 수 있는가 (안내 문구·테두리 색) */
	virtual FVHInteractOffer Evaluate(const UVHInteractionComponent* By, const AVHInteractableBase* Target) const
		PURE_VIRTUAL(UVHInteractionAuthority::Evaluate, return FVHInteractOffer(););

	/** 상호작용 시작 요청 (F 키) */
	virtual void RequestBegin(UVHInteractionComponent* By, AVHInteractableBase* Target)
		PURE_VIRTUAL(UVHInteractionAuthority::RequestBegin, );

	/** 취소 요청 (이동·공격·구르기·F 다시 누르기) */
	virtual void RequestCancel(UVHInteractionComponent* By)
		PURE_VIRTUAL(UVHInteractionAuthority::RequestCancel, );

	/** 손전등 켜기/끄기 요청 (지금은 꺼 둔 기능) */
	virtual void RequestFlashlight(UVHFlashlightComponent* Light, bool bOn)
		PURE_VIRTUAL(UVHInteractionAuthority::RequestFlashlight, );

protected:
	UVHInteractionSubsystem* GetSubsystem() const { return Subsystem.Get(); }

	TWeakObjectPtr<UVHInteractionSubsystem> Subsystem;
};

/**
 * 기본 담당 (서버 없이 게임 레벨을 직접 ▶ 했을 때, 그리고 서버용 담당의 부모).
 *
 *   - Evaluate: 오브젝트 종류별 방식(꾹/한 번)·시간·안내 문구, 본인 상태(쓰러짐·경직), 다른 의적이 사용 중인지 → 화면 표시용
 *   - RequestBegin: 서버가 없으므로 "서버에 연결되어 있지 않습니다"로 거절한다 (판정하지 않는다)
 *
 * 서버와 게임 중일 때는 UVHServerInteractionAuthority 가 Evaluate 를 그대로 쓰고 요청만 서버로 보낸다.
 */
UCLASS()
class MCPTEST_API UVHLocalInteractionAuthority : public UVHInteractionAuthority
{
	GENERATED_BODY()

public:
	virtual FVHInteractOffer Evaluate(const UVHInteractionComponent* By, const AVHInteractableBase* Target) const override;
	virtual void RequestBegin(UVHInteractionComponent* By, AVHInteractableBase* Target) override;
	virtual void RequestCancel(UVHInteractionComponent* By) override;
	virtual void RequestFlashlight(UVHFlashlightComponent* Light, bool bOn) override;

private:
	/** 오브젝트 종류별 안내(방식·시간·문구) */
	FVHInteractOffer BuildOffer(const UVHInteractionComponent* By, const AVHInteractableBase* Target) const;
};
