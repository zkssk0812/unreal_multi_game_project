#pragma once

// 브리지가 로비에 명령을 넣는 입구. 구현은 Content/Lobby/GameLobby.
// 모든 함수는 스레드 안전해야 한다(내부에서 로비의 JobQueue로 넘김).

#include <cstdint>

#include "Content/Interface/ClientChannel.h"
#include "Content/Messages/Commands.h"

namespace Content {

class ILobbyApi
{
public:
	virtual ~ILobbyApi() = default;

	// 로그인은 아직 플레이어 ID가 없으므로 채널 자체를 넘긴다. 성공하면 로비가 BindPlayer·SetPhase를 부른다.
	virtual void HandleLogin(ClientChannelRef channel, Cmd::Login command) = 0;

	// 로그인 이후 명령에는 브리지가 BindPlayer로 받은 플레이어 ID를 붙인다.
	virtual void HandleRoomList(uint64_t playerId, Cmd::RoomList command) = 0;
	virtual void HandleCreateRoom(uint64_t playerId, Cmd::CreateRoom command) = 0;
	virtual void HandleJoinRoom(uint64_t playerId, Cmd::JoinRoom command) = 0;

	// 연결이 끊겼다(브리지의 세션 종료). 닉네임 해제, 방에서 나가기 등.
	virtual void OnChannelClosed(uint64_t channelId, uint64_t playerId) = 0;
};

} // namespace Content
