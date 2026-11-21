#include "ServerCore/CorePch.h"
#include "GameServer/Net/Handlers/HandlerUtil.h"

#include "ServerCore/SendBuffer.h"
#include "Protocol/PacketBuilder.h"
#include "Protocol/ProtocolUtil.h"

// 시스템 9000~ : 콘텐츠 입구 없이 브리지가 바로 응답한다.

namespace {

bool Handle_CS_PING(GameSession& session, const char* packet, uint16 size)
{
	CS_Ping ping;
	if (!Protocol::ReadPacket(packet, size, ping))
		return false;

	// C4: 지역 구조체를 만들어 복사하지 않고, 송신 버퍼 위에 바로 쓴다.
	PacketBuilder<SC_Pong> pong(PacketId::SC_PONG);
	pong->client_time_ms = ping.client_time_ms;	// 그대로 돌려줘야 클라가 RTT를 계산한다
	pong->server_time_ms = ServerTimeMs();
	session.Send(pong.Finish());
	return true;
}

} // namespace

void SystemPackets::Register(PacketTable& table)
{
	// 하트비트를 겸하므로 모든 단계에서 허용
	table.Add(PacketId::CS_PING, { .fn = Handle_CS_PING, .allowed = kAnyPhase, .name = "CS_PING" });
}
