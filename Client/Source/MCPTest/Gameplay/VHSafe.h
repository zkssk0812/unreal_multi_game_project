#pragma once

#include "CoreMinimal.h"
#include "Gameplay/VHInteractableBase.h"
#include "VHSafe.generated.h"

UENUM(BlueprintType)
enum class EVHSafeState : uint8
{
	Closed	UMETA(DisplayName = "닫힘"),
	Opened	UMETA(DisplayName = "열림(보물 있음)"),	// ES_OPENED
	Empty	UMETA(DisplayName = "빈 금고")
};

/**
 * 금고 [서버 v3.0]: F로 다가가서 10초 작업(진행도 유지), 열리면 다시 열 수 없다.
 * 열림·빈 금고 상태는 서버 엔티티 상태로 바뀐다(ApplyOpened / ApplyTreasureTaken).
 * 보물은 금고 앞에 따로 생기는 환금 아이템 엔티티(AVHDroppedTreasure)로 줍는다.
 */
UCLASS(Blueprintable)
class MCPTEST_API AVHSafe : public AVHInteractableBase
{
	GENERATED_BODY()

public:
	AVHSafe();

	EVHSafeState GetSafeState() const { return SafeState; }

	/** 서버 결과 반영: 문이 열린다 */
	void ApplyOpened();

	/** 서버 결과 반영: 빈 금고가 된다 (안의 보물 외형을 숨긴다) */
	void ApplyTreasureTaken();

	/** 개발용: 처음 상태로 */
	void ResetSafe();

	virtual bool ShouldShowIdleProgress() const override;
	/** 금고 앞 115cm (맵 파일 vault_slots[].interact_pos와 같은 지점). 서버가 여기서 작업 거리를 잰다 */
	virtual FVector GetWorkPoint() const override;
	virtual float GetWorkApproachDistance() const override;

	/**
	 * 서버가 방 안 아무 칸에 놓은 금고(새 맵): 서버는 작업 거리(1m + 여유 30cm)를 금고 중심에서 잰다.
	 * 켜면 작업 지점이 금고 중심이 되어 어느 쪽에서 다가가도 작업할 수 있다(몸통에 닿을 만큼 다가감).
	 */
	void SetWorkFromAnySide(bool bEnable) { bWorkFromAnySide = bEnable; }

protected:
	virtual void Tick(float DeltaSeconds) override;

	/** 문 회전축(앞면 왼쪽 모서리) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> DoorHinge;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Door;

	/** 안에 든 보물 외형 (열리면 보임) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> TreasureMesh;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "VaultHeist")
	EVHSafeState SafeState = EVHSafeState::Closed;

	/** 문이 열리는 각도와 시간 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VaultHeist|Visual")
	float DoorOpenYaw = -110.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VaultHeist|Visual")
	float DoorOpenTime = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VaultHeist|Visual")
	FLinearColor DoorColor = FLinearColor(0.12f, 0.14f, 0.18f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VaultHeist|Visual")
	FLinearColor TreasureColor = FLinearColor(1.f, 0.72f, 0.05f);

	virtual void OnConstruction(const FTransform& Transform) override;

private:
	float DoorAlpha = 0.f;
	bool bWorkFromAnySide = false;
};
