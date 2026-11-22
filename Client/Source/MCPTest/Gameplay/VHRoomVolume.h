#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VHRoomVolume.generated.h"

class UBoxComponent;

/**
 * 방(지역) 하나 [기획 PDF 2장: 반납지점은 각 지역(방)의 랜덤한 바닥 위치에 나타난다].
 * 레벨에 방마다 하나씩 놓고, 상자(Area)를 방 바닥 크기에 맞춘다. 게임 중에는 보이지 않는다.
 * 반납지점 관리자(UVHMatchSubsystem)가 이 상자 안의 빈 바닥을 골라 반납지점을 만든다.
 */
UCLASS(Blueprintable)
class MCPTEST_API AVHRoomVolume : public AActor
{
	GENERATED_BODY()

public:
	AVHRoomVolume();

	/** 화면·로그에 쓰는 방 이름 (예: 현관, 서재) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VaultHeist")
	FText RoomName;

	/** 이 방에 반납지점이 나타날 수 있는가 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VaultHeist")
	bool bAllowReturnPoint = true;

	/** 서버 맵의 방 id (mansion_1f_server.json 의 rooms[].id, 예: GuestRoom1). 서버와 맞춘 레벨에서만 채운다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VaultHeist")
	FName ServerRoomId;

	/**
	 * 방 모양이 직사각형 여러 개일 때(복도처럼 꺾인 방)의 조각들. 월드 좌표(cm), 서버 맵의 rooms[].rects 와 같은 값.
	 * 비어 있으면 상자(Area) 하나가 방 모양이다. 채워져 있으면 상자는 전체를 감싸는 범위로만 쓴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VaultHeist")
	TArray<FBox2D> Rects;

	/** 방 영역 전체를 감싸는 월드 상자 */
	FBox GetRoomBox() const;

	/** 방 모양 조각들(월드 좌표). Rects 가 비어 있으면 상자 하나를 돌려준다 */
	void GetRoomRects(TArray<FBox2D>& OutRects) const;

	/** 이 점이 방 안(수평 기준)인가 */
	bool ContainsPoint(const FVector& Point, float Slack = 0.f) const;

	/**
	 * 방 안의 빈 바닥 한 점을 무작위로 찾는다.
	 * HalfSize: 놓을 물건의 반 크기(1m 정사각형 → 50), WallMargin: 방 가장자리에서 띄울 거리.
	 * 바닥을 아래로 찍어 찾고, 그 자리에 벽·금고·캐릭터 등이 겹치지 않는지 확인한다.
	 */
	bool FindRandomFloorPoint(float HalfSize, float WallMargin, FVector& OutPoint, int32 Tries = 40) const;

	/** 위치가 들어 있는 방 (없으면 nullptr) */
	static AVHRoomVolume* FindRoomAt(const UWorld* World, const FVector& Point);

	FString GetRoomLabel() const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> Area;
};
