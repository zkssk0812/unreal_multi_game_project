#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VHDoor.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class UMaterialInstanceDynamic;
class UBoxComponent;

/**
 * 방 입구의 미닫이 문 (보이는 연출만) [사용자 요청 2026-11-21].
 *
 *  - 자리는 서버 맵의 "문 위 벽"(above_door) 39곳. Tools/MansionBuild/setup_shell_level.py doors 가 레벨에 놓는다.
 *  - [2026-11-24] 처음엔 닫힘, 문 앞에서 F 로 열고 닫는다. 열림 상태는 UVHDoorSubsystem 만 바꾼다(SetOpenState) —
 *    지금은 내 클라가 정하고, 나중에 서버 메시지로 바꿔 끼운다(IVHDoorStateSource).
 *  - 닫힌 문은 내 캐릭터만 막는다(Blocker 상자: Pawn 채널만 막음, 총알·시야·마우스 조준·카메라 추적에는 안 걸림). 서버 판정은 그대로.
 *    문짝은 벽 속으로 들어간다(포켓 도어). 양문 = 문짝 두 개가 양쪽 벽으로, 한 짝 = 한 개가 SlideSign 쪽 벽으로.
 *  - 열린 문틈(GetAperture)으로 문 너머 구역이 부채꼴로 보인다 (UVHCeilingSubsystem 엿보기).
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

	/** 0 = 닫힘, 1 = 열림 (움직이는 중간값 포함) */
	float GetOpenAlpha() const { return OpenAlpha; }
	/** 상태(목표): 열림인가. UVHDoorSubsystem 이 정한다 */
	bool IsOpen() const { return bWantOpen; }
	/** 상태 바꾸기 — UVHDoorSubsystem::ApplyDoorState 만 부른다 */
	void SetOpenState(bool bOpen);
	/** 문이 켜져 있는가 (vh.Doors 0 이면 숨김 = 늘 열린 것으로 친다) */
	bool IsDoorEnabled() const { return bEnabledApplied; }
	/** 지금 뚫린 문틈 (로컬 Y 범위). 닫혀 있으면 false */
	bool GetAperture(float& OutY0, float& OutY1) const;
	/** F 안내를 띄울 위치 (이 위치 쪽 문 앞) */
	FVector GetPromptLocation(const FVector& From) const;
	/** 개발용 한 줄 */
	FString Describe() const;

	/**
	 * 이 문이 지금 총알·근접·폭발을 막는가 (서버 규칙 2026-11-24: 닫힌 문 = 벽, 열림 = ES_OPENED).
	 * 상태(IsOpen) 기준이고 문짝 움직임과는 무관하다. 잠긴 바깥 문은 늘 막는다. vh.Doors 0 이면 막지 않는다
	 */
	bool BlocksShots() const { return bEnabledApplied && (bLocked || !bWantOpen); }

	/** 선분 From→To 가 문짝 자리(벽 두께 × 문 폭 × 입구 높이)를 지나면 true 와 처음 닿는 비율(0~1) */
	bool IntersectPanel(const FVector& From, const FVector& To, float& OutT) const;

	/** 서버 Door 엔티티 번호 (게임 중 UVHDoorSubsystem::BindServerDoor 가 짝지음. 0 = 서버에 없는 문, 예: 잠긴 바깥 문) */
	uint16 ServerEntityId = 0;
	/** 서버 문 번호 (Door 엔티티 type_id, 짝이 없으면 -1) */
	int32 ServerDoorIndex = -1;

	/** 문을 보이게/숨기게 (vh.Doors 0/1) */
	void ApplyEnabled(bool bEnabled);

	/** F 로 열고 닫을 수 있는 문(F 대상)이면 테두리 빛 (상자·금고와 같은 스텐실 2) [2026-11-25]. UVHDoorSubsystem::SetFocusDoor 가 부른다 */
	void SetFocusOutline(bool bOn);
	bool HasFocusOutline() const { return bFocusOutline; }

private:
	void Layout();
	void ApplyAlpha(float Alpha);
	void UpdateBlocker();
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
	/** 테두리 빛 전용 "문 자리" 상자 (화면에는 안 그림, 커스텀 뎁스에만) — 열린 문도 닫힌 문과 같은 모양으로 빛나게 [2026-11-25] */
	UPROPERTY(VisibleAnywhere, Category = "Door") TObjectPtr<UStaticMeshComponent> OutlineProxy;
	UPROPERTY(VisibleAnywhere, Category = "Door") TObjectPtr<UStaticMeshComponent> MarkStrip;
	UPROPERTY(VisibleAnywhere, Category = "Door") TObjectPtr<UStaticMeshComponent> MarkArrow;
	UPROPERTY(VisibleAnywhere, Category = "Door") TObjectPtr<UStaticMeshComponent> MarkCeiling;
	/** 닫힌 문의 통과 막기 (내 캐릭터 = Pawn 채널만) */
	UPROPERTY(VisibleAnywhere, Category = "Door") TObjectPtr<UBoxComponent> Blocker;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StripMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ArrowMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CeilingMID;
	int32 MarkSide = 0;			// 바닥 표시를 그린 쪽 (-1 = A 쪽, +1 = B 쪽, 0 = 없음)
	bool bCeilingMarkOn = false;
	bool bFocusOutline = false;
	int32 OutlinePanelsApplied = -1;	// 문짝 테두리를 켠 상태 (열리면 문틀만)
	void ApplyOutlineParts();
	bool bBlocking = false;

	float OpenAlpha = 0.f;
	float AppliedAlpha = -1.f;
	bool bWantOpen = false;
	bool bEnabledApplied = true;

	// 문짝 크기 (Layout 에서 계산)
	float ClearWidth = 190.f;
	float PanelHeight = 234.f;
};
