#pragma once


#include <cstdint>
#include <memory>

#include "ServerCore/Type.h"

namespace Content {

class IRoomEndpoint;

enum class ClientPhase : uint8_t { Anonymous, Authenticating, Lobby, Room, Game };

enum class CloseReason : uint8_t { Kicked, ServerShutdown, ProtocolViolation };

using OutMessage = SendBufferRef;//인코딩 메세지

class IClientChannel
{
public:
	virtual ~IClientChannel() = default;

	virtual uint64_t ChannelId() const = 0;
	virtual void Send(const OutMessage& message) = 0;
	virtual void SetPhase(ClientPhase phase) = 0;
	virtual void BindPlayer(uint64_t playerId) = 0;	// 로그인 성공: 이 연결의 주인(플레이어 ID)
	virtual void BindRoom(std::weak_ptr<IRoomEndpoint> room) = 0;	// 게임 입력이 바로 이 방으로 간다
	virtual void Close(CloseReason reason) = 0;
};

using ClientChannelRef = std::shared_ptr<IClientChannel>;
using ClientChannelWeak = std::weak_ptr<IClientChannel>;//계층분리

} // namespace Content
