#pragma once

#include "CoreMinimal.h"
#include "Gameplay/VHInteractableBase.h"
#include "VHReturnPoint.generated.h"

class UMaterialInstanceDynamic;

/**
 * 반납지점 [서버 v3.0]
 * - 방 바닥의 1m × 1m 초록 정사각형. 서버가 엔티티로 만들고 60초마다 자리를 옮긴다(생성·삭제는 UVHServerGameSubsystem)
 * - 곧 사라질 때(서버 경고 비트 ES_WARNING = 남은 5초) 빨간색으로 바뀐다
 * - 환금 아이템을 들고 범위(가운데에서 100cm)에 들어가면 반납(자동 또는 F)
 * - 조명과 관계없이 항상 보인다 [팀 결정] (스스로 빛나는 단색 머티리얼)
 */
UCLASS(Blueprintable)
class MCPTEST_API AVHReturnPoint : public AVHInteractableBase
{
	GENERATED_BODY()

public:
	AVHReturnPoint();

	/** 생성 직후 부른다: 언제 생겼는지 (서버가 알려 준 남은 수명에서 계산) */
	void InitPoint(double InSpawnTime);

	double GetSpawnTime() const { return SpawnTime; }
	void SetSpawnTime(double InTime) { SpawnTime = InTime; UpdateColor(); }

	/** 등장 후 지난 시간(초) */
	float GetAge() const;
	/** 사라지기까지 남은 시간(초) */
	float GetRemainingSeconds() const;
	/** 빨간색(곧 사라짐) 상태인가. 서버가 만든 반납지점 = 서버 경고 비트(ES_WARNING), 서버 없이 놓인 것 = 등장 후 ReturnPointRedSeconds */
	bool IsRed() const;
	/** 서버 경고 비트(ES_WARNING, 등장 55초 = 남은 5초)를 반영한다. 켜지는 순간 화면의 남은 시간도 서버에 맞춘다 */
	void SetServerWarning(bool bWarning);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

public:
	virtual float GetInteractDistance(const FVector& From) const override;
	virtual float GetInteractRange() const override;
protected:

	void UpdateColor();
	double GetNow() const;

	/** 바닥 정사각형 위에 살짝 떠 있는 테두리(정사각형 4변) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TArray<TObjectPtr<UStaticMeshComponent>> Edges;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ColorMID;

private:
	double SpawnTime = 0.0;
	bool bShownRed = false;
	bool bServerDriven = false;		// 서버가 만든 반납지점 (InitPoint)
	bool bServerWarning = false;
};
