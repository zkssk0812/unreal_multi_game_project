#pragma once

// 방 하나 (docs/DESIGN.md 5장, 6장).
//   대기(Waiting) → 로딩(Loading) → 게임(Playing) → 결과(Result) → 모두 메인으로 돌아가면 방 삭제
//
// - JobQueue: 방의 모든 상태는 이 방의 Job 안에서만 바꾼다 → 락 없음. (예외: 입력 수신함 _inbox)
// - IRoomEndpoint: 방에 들어간 플레이어의 방 패킷은 브리지가 로비를 거치지 않고 여기로 바로 넣는다.
//
// 코드 위치
//   GameRoom.cpp     : 입장, 대기 상태(캐릭터·퍽·준비), 나가기, 공통 도우미       (P2)
//   GameRoomPlay.cpp : 로딩, 20Hz 틱, 입력 처리, 게임 이벤트 전송, 게임 종료, 결과, 메인 복귀  (P3, P4)
//
// 로비와의 역할 나눔
//   로비 : "누가 어느 방에 있나"(입장 허락, 정원, 방 목록)의 주인
//   방   : "방 안에서 무엇을 골랐나 / 게임이 어떻게 되고 있나"의 주인
//   둘은 서로의 JobQueue에 일을 넣는 방식으로만 대화한다(서로의 멤버를 직접 만지지 않음).

#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "ServerCore/JobQueue.h"
#include "Content/Game/GameWorld.h"
#include "Content/Interface/ClientChannel.h"
#include "Content/Interface/RoomEndpoint.h"
#include "Content/Messages/Types.h"

namespace Content {

class GameLobby;

class GameRoom final : public JobQueue, public IRoomEndpoint
{
public:
	// 로비가 방에 넘기는 플레이어 정보
	struct Entrant
	{
		uint64_t playerId = 0;
		std::string name;
		ClientChannelRef channel;
	};

	GameRoom(uint32_t roomId, std::string name, std::weak_ptr<GameLobby> lobby);
	~GameRoom() override;

	uint32_t RoomId() const { return _roomId; }

	// ── IRoomEndpoint (IOCP Worker가 부름) ──
	void Post(uint64_t playerId, RoomCmd command) override;
	void PushInput(uint64_t playerId, const InputCmd& input) override;	// 수신함에 넣기만 (Job을 만들지 않음)

	// ── 로비가 부름 (로비 Job 안에서) → 이 방의 Job으로 넘어감 ──
	void Enter(Entrant entrant, bool creator);		// 입장. creator면 SC_CREATE_ROOM, 아니면 SC_JOIN_ROOM으로 답한다
	void Disconnected(uint64_t playerId);			// 연결이 끊긴 멤버 정리

private:
	struct Member
	{
		uint64_t playerId = 0;
		std::string name;
		ClientChannelRef channel;
		uint16_t characterId = 0;		// 0 = 미선택
		std::vector<uint16_t> perks;
		bool ready = false;
		bool loaded = false;			// CS_LOAD_COMPLETE를 보냈나 (Loading 상태에서만 의미)
	};

	struct PendingInput
	{
		uint64_t playerId = 0;
		InputCmd input;
	};

	// ── Job (이 방의 스트랜드에서 한 번에 하나씩) ── GameRoom.cpp
	void DoEnter(Entrant entrant, bool creator);
	void DoDisconnected(uint64_t playerId);
	void HandleCommand(uint64_t playerId, RoomCmd command);

	// 대기 상태 명령 ── GameRoom.cpp
	void OnLeave(Member& member);
	void OnSelectCharacter(Member& member, uint16_t characterId);
	void OnSelectPerks(Member& member, const std::vector<uint16_t>& perks);
	void OnReady(Member& member, bool ready);

	// 게임 진행 ── GameRoomPlay.cpp
	void BeginLoading();							// 전원 준비 → SC_GAME_LOAD, 단계 Game
	void OnLoadComplete(Member& member);			// CS_LOAD_COMPLETE
	void OnLoadTimeout(uint64_t matchId);			// 30초가 지나면 기다리지 않고 시작
	void StartGame();								// SC_GAME_START, 틱 시작
	void Tick(uint64_t matchId);					// 20Hz: 입력 → 갱신 → 시간 확인 → 스냅샷
	void ProcessInputs();							// 수신함을 통째로 가져와 반영
	void SendStatuses();							// 바뀐 개인 상태(SC_PLAYER_STATUS)를 본인에게만
	void OnBestLoaded(uint64_t matchId, bool ok, std::unordered_map<std::string, uint32_t> best);	// 지난 최고 기록 (DB)
	void EndGame(EndReason reason);					// SC_GAME_END, 최고 기록 저장, 결과 타이머
	void OnResultTimeout(uint64_t matchId);			// 결과 화면 시간이 지나면 남은 사람 모두 메인으로
	void OnReturn(Member& member);					// CS_RETURN: 결과 화면에서 먼저 메인으로
	void ReturnToLobby(uint64_t playerId);			// 한 명을 방에서 내보내 Lobby 단계로

	// 도우미 ── GameRoom.cpp
	Member* Find(uint64_t playerId);
	void RemoveMember(uint64_t playerId);
	void SendError(Member& member, RoomRequest request, Result result);
	void BroadcastInfo();
	void Broadcast(const OutMessage& message);		// 한 번 인코딩한 것을 멤버 모두에게
	void SetState(RoomState state);
	std::shared_ptr<IRoomEndpoint> Self();

	const uint32_t _roomId;
	const std::string _name;
	std::weak_ptr<GameLobby> _lobby;

	std::vector<Member> _members;			// 들어온 순서. 맨 앞이 방장
	RoomState _state = RoomState::Waiting;

	// ── 게임 진행 (P3) ──
	GameWorld _world;
	std::mt19937 _rng{ std::random_device{}() };
	uint64_t _matchId = 0;					// 판마다 새 번호. 지난 판의 타이머가 늦게 와도 구분한다
	uint64_t _startMs = 0;					// 게임 시작 시각 (ServerCore::NowMs)
	uint32_t _tick = 0;

	// 최고 기록 (F-08): 로딩 때 DB에서 읽어 두고, 끝날 때 비교해 갱신 여부를 결과에 싣는다
	bool _recordsLoaded = false;
	std::unordered_map<std::string, uint32_t> _bestRecords;

	// 입력 수신함: IOCP Worker(PushInput)와 방 Job(Tick)이 함께 만지므로 이것만 락으로 지킨다
	std::mutex _inboxLock;
	std::vector<PendingInput> _inbox;
	std::vector<PendingInput> _processing;	// 틱에서 swap해 받는 그릇 (용량 재사용)
};

} // namespace Content
