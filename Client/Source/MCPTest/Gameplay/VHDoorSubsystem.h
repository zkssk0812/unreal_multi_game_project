#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VHDoorSubsystem.generated.h"

class AVHDoor;
class APawn;
class UVHDoorSubsystem;

/**
 * 문 열림 상태가 "어디서 오는지" [사용자 요청 2026-11-24].
 *  - 로컬(FVHLocalDoorStateSource): F 를 누르면 내 클라에서 바로 바뀐다 (가짜 서버·서버 문이 없을 때).
 *  - 서버(FVHServerDoorStateSource, 2026-11-24 팀 서버 9afd591): F = CS_ACTION InteractBegin(문 엔티티 id) 만 보내고
 *    바꾸지 않는다. 서버 스냅샷의 ES_OPENED 가 바뀌면 UVHServerGameSubsystem 이 ApplyServerDoorState 를 부른다.
 * 문·시야·소리·표시·통과 막기는 모두 ApplyDoorState 로 바뀐 상태만 보므로 다른 코드는 출처를 모른다.
 */
class IVHDoorStateSource
{
public:
	virtual ~IVHDoorStateSource() = default;
	/** 로그용 이름 */
	virtual const TCHAR* GetName() const = 0;
	/** 게임 시작 때 문의 처음 상태 */
	virtual bool GetInitialOpen(const AVHDoor& Door) const = 0;
	/** F 로 열기/닫기 요청. 결과는 Sub.ApplyDoorState 로 돌아와야 한다 (로컬 = 바로, 서버 = 스냅샷 때) */
	virtual void RequestSetOpen(UVHDoorSubsystem& Sub, AVHDoor& Door, bool bOpen) = 0;
	/** 서버 출처인가 (서버 문과 짝지은 뒤) */
	virtual bool IsServer() const { return false; }
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FVHOnDoorStateChanged, AVHDoor* /*Door*/, bool /*bOpen*/);

/**
 * 문 상태를 한 곳에서 관리한다 [사용자 요청 2026-11-24].
 *  - 문 목록(레벨의 AVHDoor 39개), 열림 상태를 바꾸는 유일한 길(ApplyDoorState), 상태 출처(로컬/서버) 선택.
 *  - 서버 문 짝짓기(BindServerDoor): 서버 Door 엔티티 type_id = 맵 colliders.static 의 안쪽 above_door 순서
 *    = 클라 문 중 잠기지 않은 것을 ServerId(wall_NNN) 순으로 센 번호. pos(문 가운데)·yaw(지나가는 방향)로 한 번 더 확인.
 *  - F: 내 캐릭터 앞의 문 고르기(FindDoorFor), 열기/닫기 요청(TryToggle). 잠긴 문·문간에 서서 닫기는 거절 문구.
 *  - 닫힌 문의 총알 연출: TraceClosedDoor (서버도 닫힌 문을 벽처럼 친다. 레일건은 뚫음)
 *  - 콘솔: vh.DoorSet open|close|toggle all|near|<문 id>, vh.DoorBind (짝짓기 결과)
 */
UCLASS()
class MCPTEST_API UVHDoorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** 문이 처음 상태를 물을 때 (문 BeginPlay) */
	bool GetInitialOpen(const AVHDoor& Door);

	/** 상태 바꾸기 — 상태 출처만 부른다 (로컬 요청 결과·서버 스냅샷) */
	void ApplyDoorState(FName DoorId, bool bOpen, const TCHAR* Why);
	void ApplyDoorState(AVHDoor* Door, bool bOpen, const TCHAR* Why);

	/** 이 폰이 F 로 열고 닫을 수 있는 문 (가장 가까운 것, 없으면 nullptr) */
	AVHDoor* FindDoorFor(const APawn* Pawn) const;

	/**
	 * F 를 눌렀을 때: 문을 열거나 닫는 요청을 상태 출처에 보낸다.
	 * 할 수 없으면 false 와 안내 문구 (잠긴 문, 문간에 서 있어서 닫을 수 없음)
	 */
	bool TryToggle(APawn* Pawn, AVHDoor* Door, FText& OutMessage);

	/** 개발용: 문 열기/닫기 요청 (상태 출처를 거친다) */
	void RequestSetOpen(AVHDoor* Door, bool bOpen);

	/** 이 폰이 문간(문짝이 지나가는 자리)에 서 있는가 → 닫으면 안 됨 */
	static bool IsPawnInDoorway(const AVHDoor& Door, const APawn* Pawn);

	// ── 서버 문 (2026-11-24 팀 서버: EntityKind::Door 37개) ──

	/** 서버 Door 엔티티와 클라 문을 짝짓고 처음 상태를 적용한다. 서버 출처로 바꾼다(설정이 Server 일 때) */
	AVHDoor* BindServerDoor(uint16 EntityId, uint16 TypeId, const FVector& Pos, float Yaw, bool bOpen);
	/** 서버 스냅샷의 ES_OPENED 가 바뀜 */
	void ApplyServerDoorState(uint16 EntityId, bool bOpen);
	/** 게임 시작 엔티티를 다 받은 뒤: 짝짓기 결과 로그 (짝 수, 어긋남 목록, 서버에 없는 클라 문) */
	void LogServerBindSummary() const;
	AVHDoor* FindByServerEntity(uint16 EntityId) const;
	bool HasServerDoors() const { return ServerDoors.Num() > 0; }

	/**
	 * 선분 From→To 가 처음 지나는 "막는 문"(닫힌 문·잠긴 문)의 자리. 총알·테이저 연출이 문에서 멈추게 할 때.
	 * 없으면 false. OutT = From→To 비율
	 */
	bool TraceClosedDoor(const FVector& From, const FVector& To, FVector& OutHit, float& OutT, AVHDoor** OutDoor = nullptr) const;
	/** 문에 맞은 연출 (불꽃 + 소리 DoorHit). 총알 연출이 닫힌 문에서 멈출 때 */
	void PlayDoorHitFx(const FVector& At, const FLinearColor& Color) const;

	const TArray<TWeakObjectPtr<AVHDoor>>& GetDoors() const { return Doors; }
	const TCHAR* GetSourceName() const;
	bool IsServerSource() const { return Source && Source->IsServer(); }

	FVHOnDoorStateChanged OnDoorStateChanged;

private:
	void EnsureSource();
	void CollectDoors();

	TArray<TWeakObjectPtr<AVHDoor>> Doors;
	/** 잠기지 않은 클라 문을 ServerId 순으로 (서버 문 번호 = 이 배열의 위치) */
	TArray<TWeakObjectPtr<AVHDoor>> InteriorByServerOrder;
	TMap<uint16, TWeakObjectPtr<AVHDoor>> ServerDoors;
	TArray<FString> BindProblems;
	TUniquePtr<IVHDoorStateSource> Source;
	bool bCollected = false;
};
