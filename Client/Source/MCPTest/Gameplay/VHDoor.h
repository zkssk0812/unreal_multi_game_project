#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VHDoor.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class UMaterialInstanceDynamic;

/**
 * 방 입구의 미닫이 문 (보이는 연출만) [사용자 요청 2026-11-21].
 *
 *  - 자리는 서버 맵의 "문 위 벽"(above_door) 39곳. Tools/MansionBuild/setup_shell_level.py doors 가 레벨에 놓는다.
 *  - 이동·충돌·총알·시야 판정은 서버 그대로라 문은 아무것도 막지 않는다 (모든 컴포넌트 충돌 없음, 길찾기 영향 없음).
 *  - 나·다른 플레이어·경비원(AVHNetProxy)이 가까이 오면 열리고 모두 지나가면 닫힌다. 문짝은 벽 속으로 들어간다(포켓 도어).
 *    양문 = 문짝 두 개가 양쪽 벽으로, 한 짝 = 한 개가 SlideSign 쪽 벽으로.
 *  - 소리: 효과음 표 DoorOpen / DoorClose (Kenney RPG doorOpen·doorClose). 천장에 덮인 구역끼리의 문은 소리를 내지 않는다.
 *  - 잠긴 문(bLocked, 서버가 막는 바깥 문 2곳)은 늘 닫혀 있고 손잡이 옆에 빨간 잠금 표시.
 *  - 천장 가림: 문간 가림막(다른 구역 안이 열린 문으로 비치지 않게)은 UVHCeilingSubsystem 이 ZoneA/ZoneB 로 만든다.
 *
 * 로컬 축: X = 벽을 뚫는 방향(법선), Y = 문 폭 방향(벽을 따라), Z = 위. 원점 = 문 가운데 바닥.
 */
UCLASS()
class MCPTEST_API AVHDoor : public AActor
{
	GENERATED_BODY()

public:
	AVHDoor();

	/** 입구 폭 (벽에 뚫린 구멍, cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door", meta = (ClampMin = 60, Units = "cm"))
	float Width = 200.f;

	/** 벽 두께 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door", meta = (ClampMin = 5, Units = "cm"))
	float WallThickness = 40.f;

	/** 입구 높이 = 문 위 벽 아래면 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door", meta = (ClampMin = 100, Units = "cm"))
	float OpeningHeight = 240.f;

	/** 양문 (문짝 두 개) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	bool bDouble = false;

	/** 한 짝일 때 문짝이 들어가는 쪽 (+1 = 로컬 +Y, -1 = 로컬 -Y) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	float SlideSign = 1.f;

	/** 잠긴 문 (늘 닫힘, 빨간 표시) — 서버가 막는 바깥 문 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	bool bLocked = false;

	/** 로컬 -X 쪽 구역 (서버 방 id, 바깥이면 비움) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	FName ZoneA;

	/** 로컬 +X 쪽 구역 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	FName ZoneB;

	/** 서버 맵의 문 위 벽 id (기록용) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	FName ServerId;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 0 = 닫힘, 1 = 열림 */
	float GetOpenAlpha() const { return OpenAlpha; }
	bool IsWantOpen() const { return bWantOpen; }
	/** 개발용 한 줄 */
	FString Describe() const;

	/** 문을 보이게/숨기게 (vh.Doors 0/1) */
	void ApplyEnabled(bool bEnabled);

private:
	void Layout();
	void ApplyAlpha(float Alpha);
	bool IsAnyoneNear() const;
	bool IsAudible() const;
	UStaticMeshComponent* MakePart(const TCHAR* Name);
	/** 문 위치 표시 (바닥 띠·화살표 = 내 구역 문, 천장 위 틈 = 다른 구역 문). 매 틱 내 구역을 보고 바꾼다 */
	void UpdateMarks();
	UStaticMeshComponent* MakeMark(const TCHAR* Name);

	UPROPERTY(VisibleAnywhere, Category = "Door") TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere, Category = "Door") TObjectPtr<UStaticMeshComponent> JambL;
	UPROPERTY(VisibleAnywhere, Category = "Door") TObjectPtr<UStaticMeshComponent> JambR;
	UPROPERTY(VisibleAnywhere, Category = "Door") TObjectPtr<UStaticMeshComponent> Header;
	UPROPERTY(VisibleAnywhere, Category = "Door") TObjectPtr<UStaticMeshComponent> PanelA;
	UPROPERTY(VisibleAnywhere, Category = "Door") TObjectPtr<UStaticMeshComponent> PanelB;
	UPROPERTY(VisibleAnywhere, Category = "Door") TObjectPtr<UStaticMeshComponent> HandleA;
	UPROPERTY(VisibleAnywhere, Category = "Door") TObjectPtr<UStaticMeshComponent> HandleB;
	UPROPERTY(VisibleAnywhere, Category = "Door") TObjectPtr<UStaticMeshComponent> LockLight;
	UPROPERTY(VisibleAnywhere, Category = "Door") TObjectPtr<UStaticMeshComponent> MarkStrip;
	UPROPERTY(VisibleAnywhere, Category = "Door") TObjectPtr<UStaticMeshComponent> MarkArrow;
	UPROPERTY(VisibleAnywhere, Category = "Door") TObjectPtr<UStaticMeshComponent> MarkCeiling;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StripMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ArrowMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CeilingMID;
	int32 MarkSide = 0;			// 바닥 표시를 그린 쪽 (-1 = A 쪽, +1 = B 쪽, 0 = 없음)
	bool bCeilingMarkOn = false;
	float MarkBoostApplied = -1.f;

	float OpenAlpha = 0.f;
	float AppliedAlpha = -1.f;
	bool bWantOpen = false;
	bool bWasOpening = false;
	float CheckTimer = 0.f;
	double LastNearTime = -1000.0;
	bool bEnabledApplied = true;

	// 문짝 크기 (Layout 에서 계산)
	float ClearWidth = 190.f;
	float PanelHeight = 234.f;
};
