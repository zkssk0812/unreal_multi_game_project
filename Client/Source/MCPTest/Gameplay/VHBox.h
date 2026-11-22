#pragma once

#include "CoreMinimal.h"
#include "Gameplay/VHInteractableBase.h"
#include "VHBox.generated.h"

/**
 * 상자 [기획 2026-11 2.3, 서버 v3.0].
 * - 공격(HP 20) 또는 3초 작업으로 부순다. 작업을 멈추면 처음부터. 부서지면 서버가 정한 아이템이 그 자리에 나온다.
 * - 서버 엔티티(EntityKind::Box) 위치에 만들어 보여 주기만 한다. HP·작업·드롭 판정은 모두 서버가 한다.
 * - 외형은 임시 기본 도형(나무색 상자). 블루프린트 자식에서 메시를 바꾸면 된다.
 */
UCLASS(Blueprintable)
class MCPTEST_API AVHBox : public AVHInteractableBase
{
	GENERATED_BODY()

public:
	AVHBox();

	/** 서버가 알려 준 남은 HP (스냅샷) */
	void SetServerHp(int32 InHp) { ServerHp = InHp; }
	int32 GetServerHp() const { return ServerHp; }

	virtual FVector GetWorkPoint() const override { return GetActorLocation(); }
	virtual float GetWorkApproachDistance() const override;

protected:
	/** 한 변 길이 (서버 히트 박스 반지름 50cm에 맞춘 80cm 상자) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VaultHeist|Visual", meta = (ClampMin = 10, Units = "cm"))
	float BoxSize = 80.f;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "VaultHeist")
	int32 ServerHp = 20;
};
