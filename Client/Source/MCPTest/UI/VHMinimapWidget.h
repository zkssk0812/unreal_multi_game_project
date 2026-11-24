#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VHMinimapWidget.generated.h"

class AVHDoor;

/**
 * 미니맵 (HUD 좌상단) [기획 U-04·U-05, UI 수정본]. 위젯 트리 없이 NativePaint 에서 직접 그린다.
 *
 *  - 바탕: 레벨의 방 영역(AVHRoomVolume)을 사각형으로 그린 단순 평면도. 화면과 같은 방향(탑뷰 카메라 Yaw)으로 돌려 그린다
 *  - 전원: 반납지점 위치와 남은 시간(초, 곧 사라지면 빨강), 내 위치와 시선
 *  - 내가 쓰러지며 떨어뜨린 보물: 바닥에 있는 동안 보라색 ◇ (누가 주우면 사라짐)
 *  - 퍽이 있을 때만: 로봇탐지 = 경비원 위치, 생체감지 = 다른 플레이어 위치, 보물탐색 = 누군가 보물을 얻은 쪽 방향(위치는 아님)
 *  - 숨김: 연막 안·은신·쓰러진 대상은 표시하지 않는다. 섬광에 맞으면 3초 동안 미니맵 전체가 사라진다
 *
 * 서버는 모든 위치를 보내므로 "퍽이 없으면 안 보이는 것"은 여기서(클라가) 숨긴다 (서버 Q-15).
 */
UCLASS()
class MCPTEST_API UVHMinimapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 개발용: 마지막으로 그린 표시 개수 (시험 로그) */
	struct FDrawStats
	{
		int32 Rooms = 0;
		int32 ReturnPoints = 0;
		int32 Guards = 0;
		int32 Thieves = 0;
		bool bTreasureArrow = false;
		bool bMyTreasure = false;	// 내가 떨어뜨린 보물 ◇
		bool bHiddenByFlash = false;
		int32 Doors = 0;			// 문 자리 표시 (2026-11-21 (5))
		int32 LockedDoors = 0;
		int32 OpenDoors = 0;
	};
	const FDrawStats& GetLastStats() const { return LastStats; }

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** 방 영역으로 맵 범위를 한 번 계산해 둔다 (화면 방향 기준 좌표) */
	void CacheRooms() const;

	struct FRoomRect
	{
		FVector2D Min = FVector2D::ZeroVector;	// 화면 방향 좌표(u = 오른쪽, v = 위)
		FVector2D Max = FVector2D::ZeroVector;
		bool bPiece = false;		// 직사각형 여러 개로 된 방의 한 조각
		bool bCorridor = false;		// 반납지점이 생기지 않는 공간(복도)
	};
	mutable TArray<FRoomRect> Rooms;	// 그릴 직사각형(방 조각)
	/** 문 자리 (서버 맵 입구 39곳 = AVHDoor): 벽을 따라 문 폭만큼의 선분 (화면 방향 좌표) */
	struct FDoorMark
	{
		FVector2D A = FVector2D::ZeroVector;
		FVector2D B = FVector2D::ZeroVector;
		bool bLocked = false;
		TWeakObjectPtr<AVHDoor> Door;	// 열림/닫힘 색 [2026-11-24]
	};
	mutable TArray<FDoorMark> Doors;
	mutable int32 RoomCount = 0;		// 방 수
	mutable FVector2D MapMin = FVector2D::ZeroVector;
	mutable FVector2D MapMax = FVector2D(1.f, 1.f);
	mutable FVector2D AxisRight = FVector2D(0.f, 1.f);	// 월드 XY → 화면 오른쪽
	mutable FVector2D AxisUp = FVector2D(1.f, 0.f);		// 월드 XY → 화면 위
	mutable bool bRoomsCached = false;
	mutable FDrawStats LastStats;
};
