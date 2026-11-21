#pragma once

#include <cstdint>

#include "Content/Messages/Commands.h"

namespace Content {

// 방에 들어간 뒤 브리지가 방에 바로 명령을 넣는 입구 (로비를 거치지 않음).
// 방이 BindRoom으로 채널에 자기를 알려 주면, 그 채널의 방 패킷은 여기로 온다.
class IRoomEndpoint
{
public:
	virtual ~IRoomEndpoint() = default;

	// 이동·행동 입력. Job을 만들지 않고 방의 수신함에 넣기만 한다(스레드 안전, 틱에서 소비).
	virtual void PushInput(uint64_t playerId, const InputCmd& input) = 0;

	// 선택·준비·로딩 완료 같은 드문 제어 명령. 방의 JobQueue에 DoAsync로 들어간다.
	virtual void Post(uint64_t playerId, RoomCmd command) = 0;
};

} // namespace Content
