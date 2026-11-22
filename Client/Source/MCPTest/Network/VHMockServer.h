#pragma once

#include "CoreMinimal.h"
#include "Network/VHProtocol.h"
#include "Network/VHNetTypes.h"

/** Mock 서버 동작 값 (UVHSettings에서 채운다) */
struct FVHMockConfig
{
	/** 가짜 게임 길이(초). 이 시간이 지나면 샘플 결과(SC_GAME_END)를 보낸다. 넷 서브시스템이 클라 설정의 제한 시간으로 넣는다 */
	float GameSeconds = 300.f;
	int32 ResultScreenMs = 30000;			// SC_GAME_END.result_screen_ms
	int32 FakePlayersInNewRoom = 2;			// 방을 만들면 몇 초 뒤 들어오는 가짜 플레이어 수(0~2)
	float FakeActionDelaySeconds = 1.5f;	// 가짜 플레이어 행동 간격
};

/**
 * 인프로세스 가짜 서버.
 * 클라이언트가 보낸 C_ 패킷(바이트)을 프로토콜 명세대로 처리하고 S_ 패킷(바이트)을 돌려준다.
 * 진짜 서버처럼 "단계별 허용 패킷"을 검사해서, 허용되지 않은 패킷이 오면 S_KICK 후 연결을 끊는다.
 * → 클라이언트가 단계 추적을 잘못하면 Mock에서도 바로 드러난다.
 *
 * 서버 프로토콜 v3.0 기준 (로그인 → 방 목록·만들기·입장 → 캐릭터·퍽·준비 → 로드 → 시작 → 결과 → 로비).
 * 최소 기능만 한다 [사용자 결정 2026-11-13]: 게임은 샘플 엔티티(의적·경비원·금고·반납지점·상자·아이템)를 보여 주고
 * 남은 시간만 알려 주다가, 시간이 끝나면 샘플 결과를 보낸다. 규칙 판정(금고·전투·반납)은 하지 않는다 → 규칙 시험은 실서버로.
 *
 * 테스트용 특수 이름(로그인): Taken → NameInUse, Busy → ServerBusy
 */
class FVHMockServer
{
public:
	explicit FVHMockServer(const FVHMockConfig& InConfig);

	/** 클라이언트 → 서버: 완성된 패킷 하나 */
	void ReceivePacket(const uint8* Data, int32 Size);
	void Tick(double NowSeconds);

	/** 서버 → 클라이언트 바이트 */
	TFunction<void(const TArray<uint8>&)> SendToClient;
	/** 서버가 연결을 끊음 */
	TFunction<void(const FString& Reason)> DisconnectClient;

private:
	struct FMember
	{
		uint8 Slot = 0;
		uint64 AccountId = 0;
		FString Name;
		uint16 CharacterId = 0;
		TArray<uint16> Perks;
		bool bReady = false;
		bool bFake = true;
	};

	struct FRoom
	{
		uint32 RoomId = 0;
		FString Name;
		uint64 HostAccountId = 0;
		RobProto::RoomState State = RobProto::RoomState::Waiting;
		TArray<FMember> Members;
		bool bCreatedByClient = false;
		uint64 MatchId = 0;

		FMember* FindMember(uint64 AccountId);
		FString HostName() const;
	};

	struct FTimer
	{
		double FireAt = 0.0;
		uint32 Epoch = 0;	// 방을 나가거나 단계가 바뀌면 Epoch를 올려 예약된 행동을 무효화
		TFunction<void()> Fn;
	};

	// 처리
	bool IsAllowed(RobProto::PacketId Id) const;
	void Kick(RobProto::KickReason Reason, const FString& Message);
	void HandleLogin(const uint8* Data, int32 Size);
	void HandleRoomList(const uint8* Data, int32 Size);
	void HandleCreateRoom(const uint8* Data, int32 Size);
	void HandleJoinRoom(const uint8* Data, int32 Size);
	void HandleLeaveRoom();
	void HandleSelectCharacter(const uint8* Data, int32 Size);
	void HandleSelectPerks(const uint8* Data, int32 Size);
	void HandleReady(const uint8* Data, int32 Size);
	void HandleLoadComplete(const uint8* Data, int32 Size);
	/** CS_ACTION: 규칙을 판정하지 않으므로 응답이 필요한 요청(상호작용·스킬·소비 아이템·무기 교체)만 거절로 답한다 */
	void HandleAction(const uint8* Data, int32 Size);
	void HandleReturn(const uint8* Data, int32 Size);
	/** 결과 화면 뒤(또는 CS_RETURN) → 방을 없애고 SC_LEAVE_ROOM(GameEnded), 단계 Lobby */
	void ReturnToLobbyAfterGame();

	// 방 진행
	FRoom* CurrentRoom();
	FRoom* FindRoom(uint32 RoomId);
	void LeaveCurrentRoom();
	void SendRoomState(const FRoom& Room);
	void SendRoomError(RobProto::PacketId Request, RobProto::ResultCode Code);
	void ScheduleFakeJoin(int32 Index, float Delay);
	void ScheduleFakePreparation(float Delay);
	void CheckAllReady();
	void StartGameLoad();
	/** 0.25초마다 SC_SNAPSHOT (남은 시간 + 의적·경비원 위치. 내 위치는 마지막 CS_MOVE 그대로) */
	void SendSnapshot();
	/** 게임 끝 → 샘플 SC_GAME_END */
	void EndGame();

	// 보내기
	template <typename T>
	void Send(const T& Packet, uint16 Size = static_cast<uint16>(sizeof(T)));

	void After(float Seconds, TFunction<void()> Fn);
	void InvalidateTimers() { ++Epoch; }
	uint64 ServerTimeMs() const;

	FVHMockConfig Config;
	RobProto::ClientPhase Phase = RobProto::ClientPhase::Anonymous;
	uint64 MyAccountId = 0;
	FString MyName;
	TArray<FRoom> Rooms;
	uint32 CurrentRoomId = 0;
	uint32 NextRoomId = 100;
	uint64 NextMatchId = 5000;
	TArray<FTimer> Timers;
	uint32 Epoch = 1;
	double Now = 0.0;
	double StartTime = 0.0;
	bool bKicked = false;

	// 가짜 게임 (샘플 엔티티)
	TArray<RobProto::EntitySpawn> GameEntities;
	uint16 SelfEntityId = 0;
	RobProto::Vec3 SelfPos = { 0.f, 0.f, 0.f };
	float SelfYaw = 0.f;
	double GameStartNow = 0.0;
	uint32 SnapshotTick = 0;
};
