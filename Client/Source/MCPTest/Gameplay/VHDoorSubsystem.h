#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VHDoorSubsystem.generated.h"

class AVHDoor;
class APawn;
class UVHDoorSubsystem;

/**
 * 문 열림 상태가 "어디서 오는지" [사용자 요청 2026-11-24].
 * 지금은 내 클라(FVHLocalDoorStateSource): F를 누르면 바로 바뀐다.
 * 서버에 문 상태가 생기면 이 인터페이스를 구현한 서버용 출처를 하나 더 만들어 끼운다:
 *   RequestSetOpen = CS_ 패킷 보내기(바로 바꾸지 않음), 서버 메시지(SC_)를 받으면 UVHServerGameSubsystem 이
 *   UVHDoorSubsystem::ApplyDoorState(문 id, 열림, TEXT("서버")) 를 부른다. 문 id = AVHDoor::ServerId (서버 맵 above_door id).
 * 문·시야·소리·표시·통과 막기는 모두 ApplyDoorState 로 바뀐 상태만 보므로 다른 코드는 고칠 필요가 없다.
 */
class IVHDoorStateSource
{
public:
	virtual ~IVHDoorStateSource() = default;
	/** 로그용 이름 */
	virtual const TCHAR* GetName() const = 0;
	/** 게임 시작 때 문의 처음 상태 */
	virtual bool GetInitialOpen(const AVHDoor& Door) const = 0;
	/** F 로 열기/닫기 요청. 결과는 Sub.ApplyDoorState 로 돌아와야 한다 (로컬 = 바로, 서버 = 응답 때) */
	virtual void RequestSetOpen(UVHDoorSubsystem& Sub, AVHDoor& Door, bool bOpen) = 0;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FVHOnDoorStateChanged, AVHDoor* /*Door*/, bool /*bOpen*/);

/**
 * 문 상태를 한 곳에서 관리한다 [사용자 요청 2026-11-24].
 *  - 문 목록(레벨의 AVHDoor 39개), 열림 상태를 바꾸는 유일한 길(ApplyDoorState), 상태 출처(로컬/서버) 선택.
 *  - F: 내 캐릭터 앞의 문 고르기(FindDoorFor), 열기/닫기 요청(TryToggle). 잠긴 문·문간에 서서 닫기는 거절 문구.
 *  - 콘솔: vh.DoorSet open|close|toggle all|near|<문 id>
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

	/** 상태 바꾸기 — 상태 출처만 부른다 (로컬 요청 결과·나중의 서버 메시지) */
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

	const TArray<TWeakObjectPtr<AVHDoor>>& GetDoors() const { return Doors; }
	const TCHAR* GetSourceName() const;

	FVHOnDoorStateChanged OnDoorStateChanged;

private:
	void EnsureSource();
	void CollectDoors();

	TArray<TWeakObjectPtr<AVHDoor>> Doors;
	TUniquePtr<IVHDoorStateSource> Source;
	bool bCollected = false;
};
