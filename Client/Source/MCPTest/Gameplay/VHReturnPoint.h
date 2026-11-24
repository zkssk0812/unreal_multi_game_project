#pragma once

#include "CoreMinimal.h"
#include "Gameplay/VHInteractableBase.h"
#include "VHReturnPoint.generated.h"

class UMaterialInstanceDynamic;

/**
 * 반납지점 [서버 v3.0]
 * - 방 바닥의 1m × 1m 초록 정사각형. 서버가 엔티티로 만들고 수명(서버 timer_ms, 2026-11-23 팀 서버 30초)이 끝나면 자리를 옮긴다(생성·삭제는 UVHServerGameSubsystem)
 * - 곧 사라질 때(서버 경고 비트 ES_WARNING = 사라지기 5초 전) 빨간색으로 바뀐다
 * - 환금 아이템을 들고 범위(가운데에서 100cm) 안에서 F → 시간 작업(서버 SC_INTERACT, 2026-11-23 팀 서버 4초). 한 번에 한 명
 * - 조명과 관계없이 항상 보인다 [팀 결정] (스스로 빛나는 단색 머티리얼)
 */
UCLASS(Blueprintable)
class MCPTEST_API AVHReturnPoint : public AVHInteractableBase
{
	GENERATED_BODY()

public:
	AVHReturnPoint();

	/** 생성 직후 부른다: 받은 시각과 서버가 알려 준 남은 수명(timer_ms). 화면의 남은 시간은 이 값으로만 센다 (수명을 클라에 고정하지 않음) */
	void InitPoint(double InReceivedTime, float InRemainingSeconds);

	double GetSpawnTime() const { return SpawnTime; }
	void SetSpawnTime(double InTime) { SpawnTime = InTime; UpdateColor(); }

	/** 등장 후 지난 시간(초) */
	float GetAge() const;
	/** 사라지기까지 남은 시간(초) */
	float GetRemainingSeconds() const;
	/** 빨간색(곧 사라짐) 상태인가. 서버가 만든 반납지점 = 서버 경고 비트(ES_WARNING), 서버 없이 놓인 것 = 등장 후 ReturnPointRedSeconds */
	bool IsRed() const;
	/** 서버 경고 비트(ES_WARNING, 사라지기 5초 전)를 반영한다 */
	void SetServerWarning(bool bWarning);
	/** 서버가 만든 반납지점인가 (남은 시간 = 서버 timer_ms 기준) */
	bool IsServerDriven() const { return bServerDriven; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

public:
	virtual float GetInteractDistance(const FVector& From) const override;
	virtual float GetInteractRange() const override;
	/** 반납은 시간 작업: F를 누르면 사각형 가운데 가까이(범위 안쪽) 다가간 뒤 서버에 요청한다 */
	virtual FVector GetWorkPoint() const override;
	virtual float GetWorkApproachDistance() const override;
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
	double ExpireTime = 0.0;		// 서버가 만든 반납지점: 받은 시각 + 서버 timer_ms
	bool bShownRed = false;
	bool bServerDriven = false;		// 서버가 만든 반납지점 (InitPoint)
	bool bServerWarning = false;
};
