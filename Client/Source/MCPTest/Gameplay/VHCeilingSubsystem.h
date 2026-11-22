#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VHCeilingSubsystem.generated.h"

class AActor;
class APawn;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class UTexture2D;
class AVHDoor;

/**
 * 천장 가림 (방 단위 시야) [사용자 요청 2026-11-21].
 *
 *  - 구역 = 레벨의 방 영역(AVHRoomVolume 중 ServerRoomId 가 있는 것 = 서버 맵 rooms 35개). 복도(Corridor)는 서버 맵에서 이미 한 방(조각 66개)이다.
 *    한 방의 조각이 서로 떨어져 있으면 이어진 것끼리 다른 구역으로 나눈다(지금 맵은 모두 이어져 있음 → 35구역).
 *  - 구역마다 벽 높이에 회색 천장(엔진 Plane 메시를 조각 크기로 늘린 인스턴스 + 언릿 재질 M_VHCeiling)을 만든다.
 *    그림자·간접광·반사에는 끼지 않는다(맵 밝기 그대로).
 *  - 내 구역 = 카메라가 따라가는 대상(뷰 타깃, 보통 내 캐릭터)의 위치. 지금 구역에서 CeilingSwitchMargin 이상 벗어나
 *    다른 구역 안에 들어가야 바뀐다(문가 깜빡임 방지). 바뀌면 CeilingFadeSeconds 동안 디더로 사라지기/덮이기.
 *  - 덮인 구역 안의 다른 플레이어·경비원(AVHNetProxy), 금고·상자·아이템·반납지점(AVHInteractableBase), 연출(AVHFxActor)은
 *    컴포넌트 HiddenInGame 으로 숨긴다 → 몸·이름표(HP 글자)·발밑 원·붙은 이펙트·상호작용 테두리가 모두 안 보인다.
 *    (다른 코드가 쓰는 SetVisibility·SetActorHiddenInGame 과 겹치지 않는 별도 스위치. 내가 숨긴 컴포넌트만 되돌린다)
 *  - 판정은 서버 그대로이고 보이는 것만 바꾼다. 미니맵·퍽 표시(미니맵/HUD)는 손대지 않는다.
 *  - 콘솔: vh.Ceiling 0/1, 개발용 vh.CeilingGo X Y(순간이동), vh.CeilingShots 장수 간격(연속 화면 저장)
 */
UCLASS()
class MCPTEST_API UVHCeilingSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** 천장 가림이 켜져 있는가 (설정 bCeiling 그리고 콘솔 vh.Ceiling) */
	bool IsEnabled() const;
	/** 천장으로 덮여 화면에서 숨긴 액터인가 (상호작용 후보·커서 정보창에서 뺀다) */
	bool IsCovered(const AActor* Actor) const;
	static bool IsActorCovered(const AActor* Actor);

	/** 이 위치가 지금 천장에 가려 보이지 않는 곳인가 (문소리 판단 등) */
	bool IsLocationCovered(const FVector& Location) const { return IsPointCovered(FVector2D(Location.X, Location.Y)); }

	/**
	 * 내 구역 앞 가림 지우기 [2026-11-21 (4)]: 이 액터의 재질 중 "ZoneTex" 파라미터가 있는 것(M_VHWallFadeTex 계열: 벽·기둥·창틀·문)을
	 * 구역 지도·내 구역 값을 받는 재질 인스턴스로 바꾼다. SelfZone: -1 = 자기 위치의 구역은 지우지 않음(벽·기둥), 0 = 위치 무관(문)
	 */
	void ApplyCutaway(AActor* Actor, float SelfZone);

	/** 지금 내 구역 id (서버 방 id, 없거나 꺼져 있으면 None) */
	FName GetCurrentZoneId() const { return (IsEnabled() && Zones.IsValidIndex(Current)) ? Zones[Current].Id : NAME_None; }
	/** 이 구역 천장 높이 (모르면 -1) */
	float GetZoneCeilingHeight(FName Id) const
	{
		const FZone* Z = Zones.FindByPredicate([Id](const FZone& Q) { return Q.Id == Id; });
		return Z ? Z->Height : -1.f;
	}

	/** 지금 내 구역 이름 (없으면 빈 문자열) */
	FString GetCurrentZoneLabel() const;
	/** 이 위치가 들어 있는 구역 이름 (없으면 "바깥") */
	FString GetZoneLabelAt(const FVector& Location) const;

	/** 개발용: 상태 한 줄 */
	FString Describe() const;

private:
	struct FZone
	{
		FName Id;
		FString Label;
		TArray<FBox2D> Rects;		// 서버 방 조각(이동 가능 칸) — 내 구역 판정
		TArray<FBox2D> Wide;		// 천장·물체 판정용(벽 면까지 넓힘)
		FBox2D Bounds = FBox2D(ForceInit);
		float Height = 400.f;
		float Opacity = 1.f;		// 1 = 덮임, 0 = 걷힘
		float AppliedOpacity = -1.f;
		TWeakObjectPtr<UInstancedStaticMeshComponent> Mesh;	// CeilingActor 가 가진다
		TWeakObjectPtr<UMaterialInstanceDynamic> Material;		// Mesh 의 재질 칸이 가진다
		TArray<TWeakObjectPtr<AActor>> Props;	// 이 구역의 소품(가구) — 구역이 다 덮이면 숨긴다
		bool bPropsHidden = false;
		int32 Curtains = 0;						// 문간 가림막 수
	};

	void BuildZones();
	/** 구역 안에 갇힌 작은 빈칸(기둥 자리)을 천장 조각으로 메운다 */
	void FillSmallHoles(FZone& Z);
	/** 구역마다 둘레 벽 윗면 높이를 재서 천장 높이를 정한다 (중앙 홀 800, 나머지 400) */
	void MeasureWallHeights();
	void UpdateCurrentZone();
	void UpdateFade(float DeltaTime);
	void UpdateActors();
	/** 구역 지도 텍스처: 위에서 본 칸마다 구역 번호(1~, 0 = 없음) */
	void BuildZoneTexture();
	/** 문간 가림막: 문 자리마다 덮인 구역 쪽 벽 면에 세로 천장 조각 (열린 문으로 다른 구역 안이 비치지 않게) */
	void AddDoorCurtains();
	/** 구역마다 소품(가구 StaticMeshActor)을 모은다 (숨겨도 그림자·간접광은 남게) */
	void CollectProps();
	void UpdateProps();
	/** 내 구역(A)·직전 구역(B)과 지우기 정도를 재질에 보낸다 (바뀔 때만) */
	void UpdateCutParams(bool bForce = false);
	void SetCutParams(UMaterialInstanceDynamic* MID, float SelfZone) const;
	int32 ZoneIndexAt(const FVector2D& P) const;
	void SetActorCovered(AActor* Actor, bool bCovered);
	/** 이 점이 지금 천장에 가려 보이지 않아야 하는가 */
	bool IsPointCovered(const FVector2D& P) const;
	FVector GetViewerLocation() const;

	static float DistToRects(const TArray<FBox2D>& Rects, const FVector2D& P);

	TArray<FZone> Zones;
	int32 Current = INDEX_NONE;
	bool bBuilt = false;
	bool bWasEnabled = true;

	UPROPERTY() TObjectPtr<AActor> CeilingActor;

	// ── 내 구역 앞 가림 지우기 ──
	UPROPERTY() TObjectPtr<UTexture2D> ZoneTexture;
	FVector4 ZoneMapParam = FVector4(0, 0, 0.1, 0);	// (minX, minY, 1/칸, 0)
	FVector4 ZoneSizeParam = FVector4(1, 1, 0, 0);	// (칸 수 X, Y)
	int32 Previous = INDEX_NONE;					// 직전 구역 (천장이 다시 덮이는 동안 함께 서서히)
	float SentA = -1.f, SentAA = -1.f, SentB = -1.f, SentBA = -1.f, SentZ = -12345.f;
	/** 바꿔 넣은 재질: (원래 재질, SelfZone) → 재질 인스턴스. 벽·문이 함께 쓴다 */
	struct FCutMID { TWeakObjectPtr<UMaterialInterface> Base; float SelfZone = -1.f; UMaterialInstanceDynamic* MID = nullptr; };
	TArray<FCutMID> CutMIDs;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> CutMIDRefs;	// GC 보호
	int32 PropCount = 0;

	/** 가려진 내 캐릭터 실루엣: 내 메시들을 커스텀 뎁스(스텐실 0)에 그리고, 카메라 후처리에 M_VHSilhouette 을 붙인다 */
	void UpdateSilhouette();
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SilhouetteMID;
	TWeakObjectPtr<APawn> SilhouettePawn;
	bool bSilhouetteOn = false;

	/** 내가 숨긴 액터 → 내가 HiddenInGame 을 켠 컴포넌트들 (보일 때 이것만 되돌린다) */
	TMap<TWeakObjectPtr<AActor>, TArray<TWeakObjectPtr<USceneComponent>>> Hidden;
};
