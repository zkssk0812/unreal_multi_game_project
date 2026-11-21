#pragma once

// 로비 (docs/DESIGN.md 5장). 서버에 하나.
//
// - 닉네임 규칙 검사, 플레이어 ID 발급, 접속 중인 닉네임 중복 막기
// - 방 목록·생성·참여, "누가 어느 방에 있나"(정원 판단)의 주인
// - 방 안의 일(캐릭터·퍽·준비)은 GameRoom이 한다. 로비는 방에 사람을 넣고 빼는 것까지만.
//
// JobQueue라서 모든 상태는 로비의 Job 안에서만 바뀐다(락 없음).
// 공개 함수(Handle*, MemberLeft 등)는 아무 스레드에서 불러도 되고, 내부에서 Job으로 넘긴다.

#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ServerCore/JobQueue.h"
#include "Content/Interface/LobbyApi.h"
#include "Content/Messages/Types.h"

namespace Content {

class GameRoom;

class GameLobby final : public JobQueue, public ILobbyApi
{
public:
	// ── ILobbyApi: 브리지(IOCP Worker)가 부름 ──
	void HandleLogin(ClientChannelRef channel, Cmd::Login command) override;
	void HandleRoomList(uint64_t playerId, Cmd::RoomList command) override;
	void HandleCreateRoom(uint64_t playerId, Cmd::CreateRoom command) override;
	void HandleJoinRoom(uint64_t playerId, Cmd::JoinRoom command) override;
	void OnChannelClosed(uint64_t channelId, uint64_t playerId) override;

	// ── GameRoom이 부름 (방의 Job 안에서) ──
	void MemberLeft(uint32_t roomId, uint64_t playerId);		// 스스로 나감, 또는 입장이 거절됨
	void RoomStateChanged(uint32_t roomId, RoomState state);

	// 서버 종료 때(Worker가 멈춘 뒤) 방과 남은 Job을 정리한다
	void Shutdown();

private:
	struct Player
	{
		ClientChannelRef channel;
		std::string name;
		std::string nameKey;		// 중복 비교용 (영문 소문자)
		uint32_t roomId = 0;		// 0 = 방에 없음
	};

	struct RoomEntry
	{
		std::shared_ptr<GameRoom> room;
		std::string name;
		std::vector<uint64_t> members;	// 들어온 순서 (방의 멤버 순서와 같음 → 맨 앞이 방장)
		RoomState state = RoomState::Waiting;
	};

	// ── Job (로비 스트랜드) ──
	void Login(ClientChannelRef channel, Cmd::Login command);
	void RoomList(uint64_t playerId);
	void CreateRoom(uint64_t playerId, Cmd::CreateRoom command);
	void JoinRoom(uint64_t playerId, Cmd::JoinRoom command);
	void ChannelClosed(uint64_t channelId);
	void DoMemberLeft(uint32_t roomId, uint64_t playerId);
	void DoRoomStateChanged(uint32_t roomId, RoomState state);

	Player* FindPlayer(uint64_t playerId);
	void RemoveFromRoom(uint32_t roomId, uint64_t playerId);	// 명단에서 빼고, 비면 방 삭제

	std::unordered_map<uint64_t, Player> _players;				// 플레이어 ID → 정보
	std::unordered_map<uint64_t, uint64_t> _channelToPlayer;	// 채널 ID → 플레이어 ID (접속 종료 처리용)
	std::unordered_set<std::string> _nameKeys;					// 접속 중인 닉네임
	std::map<uint32_t, RoomEntry> _rooms;						// 방 ID 순서로 정렬 (목록이 만든 순서대로)

	uint64_t _nextPlayerId = 1;
	uint32_t _nextRoomId = 1;
};

} // namespace Content
