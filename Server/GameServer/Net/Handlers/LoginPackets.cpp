#include "ServerCore/CorePch.h"
#include "GameServer/Net/Handlers/HandlerUtil.h"

#include "ServerCore/SendBuffer.h"
#include "Protocol/PacketBuilder.h"
#include "Content/ContentGlobal.h"
#include "Content/Interface/LobbyApi.h"

// 입장 1000~ : 콘텐츠 입구는 ILobbyApi (로비 JobQueue로 넘어감)

namespace {

bool Handle_CS_LOGIN(GameSession& session, const char* packet, uint16 size)
{
	auto request = CommandDecoder::DecodeLogin(packet, size);
	if (!request)
		return false;

	// 버전 검사는 프로토콜의 일 → 브리지가 판단하고 콘텐츠까지 보내지 않는다.
	// VersionMismatch는 콘텐츠의 Result에 없는 코드라 브리지가 SC_LOGIN을 직접 만든다.
	if (request->protocolVersion != PROTOCOL_VERSION)
	{
		LOG_WARN("Session {}: 프로토콜 버전 불일치 0x{:08X} (서버 0x{:08X})", session.SessionId(),
			request->protocolVersion, PROTOCOL_VERSION);
		PacketBuilder<SC_Login> reply(PacketId::SC_LOGIN);
		reply->result = ResultCode::VersionMismatch;
		reply->server_time_ms = ServerTimeMs();
		session.SendAndDisconnect(reply.Finish(), "version mismatch");
		return true;	// 형식은 맞았다(SC_KICK이 아니라 SC_LOGIN으로 알린다)
	}

	// Authenticating은 브리지가 직접 설정한다: 응답을 기다리는 동안 CS_LOGIN을 또 보내면
	// 표의 ignored 규칙으로 버려진다. 성공·실패 후의 단계는 로비가 정한다.
	session.SetPhase(Content::ClientPhase::Authenticating);
	Content::Lobby().HandleLogin(session.SharedChannel(), std::move(request->command));
	return true;
}

} // namespace

void LoginPackets::Register(PacketTable& table)
{
	using Phase = Content::ClientPhase;	// protocol.h의 전역 ClientPhase와 구분
	table.Add(PacketId::CS_LOGIN, { .fn = Handle_CS_LOGIN,
		.allowed = Mask(Phase::Anonymous),
		.ignored = Mask(Phase::Authenticating),	// 응답 기다리는 중 CS_LOGIN 재전송은 버림
		.name = "CS_LOGIN" });
}
