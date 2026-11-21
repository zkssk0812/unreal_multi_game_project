#include "ServerCore/CorePch.h"
#include "GameServer/Net/GameSession.h"

#include <chrono>

#include "ServerCore/CoreClock.h"
#include "ServerCore/SendBuffer.h"
#include "ServerCore/SocketUtils.h"
#include "Protocol/PacketBuilder.h"
#include "Content/ContentGlobal.h"
#include "Content/Interface/LobbyApi.h"
#include "GameServer/Net/PacketHandler.h"
#include "GameServer/Protocol/MessageEncoder.h"

// 코어는 헤더 형식만 알고 protocol.h는 모른다. 둘이 같은 형식인지는 여기(게임 쪽)서 확인한다.
static_assert(sizeof(PacketHeader) == PacketSession::HeaderSize, "protocol.h 헤더와 코어 헤더 크기가 다름");
static_assert(offsetof(PacketHeader, size) == 0 && offsetof(PacketHeader, type) == 2, "헤더 필드 순서가 다름");
static_assert(MAX_PACKET_SIZE == PacketSession::DefaultMaxPacketSize, "최대 패킷 크기가 다름");

uint64 ServerTimeMs()
{
	// 벽시계(system_clock)가 아니라 단조 시계: 시스템 시간을 바꿔도 뒤로 가지 않는다.
	static const auto start = std::chrono::steady_clock::now();
	return static_cast<uint64>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - start).count());
}

namespace {

// SendAndDisconnect의 reason은 OnDisconnected까지 보관되므로 문자열 상수여야 한다.
const char* KickText(KickReason reason)
{
	switch (reason)
	{
	case KickReason::ProtocolViolation: return "kick: protocol violation";
	case KickReason::ServerShutdown:    return "kick: server shutdown";
	case KickReason::Duplicate:         return "kick: duplicate";
	case KickReason::Admin:             return "kick: admin";
	}
	return "kick";
}

} // namespace

// ───────────────── IClientChannel ─────────────────

void GameSession::SetPhase(Content::ClientPhase phase)
{
	// 시각을 먼저 쓰고 단계를 나중에 쓴다. 핸들러는 단계를 읽은 뒤 시각을 읽으므로,
	// 새 단계를 본 핸들러는 (거의) 새 시각을 본다. 조금 어긋나도 결과는 "1초 유예가 조금 길거나 짧음"뿐이다.
	_phaseChangedMs.store(std::max<uint64>(ServerTimeMs(), 1));	// 0은 "바뀐 적 없음"
	_phase.store(phase);
}

void GameSession::Close(Content::CloseReason reason)
{
	switch (reason)
	{
	case Content::CloseReason::Kicked:            Kick(KickReason::Admin, "kicked"); break;
	case Content::CloseReason::ServerShutdown:    Kick(KickReason::ServerShutdown, "server shutdown"); break;
	case Content::CloseReason::ProtocolViolation: Kick(KickReason::ProtocolViolation, "protocol violation"); break;
	}
}

Content::ClientChannelRef GameSession::SharedChannel()
{
	// shared_from_this()는 IocpObject 타입. GameSession으로 내린 뒤 IClientChannel로 올린다
	// (다중 상속이라 IClientChannel 부분의 주소는 GameSession 주소와 다르다: shared_ptr 변환이 맞춰 준다).
	return std::static_pointer_cast<GameSession>(shared_from_this());
}

void GameSession::Kick(KickReason reason, const char* detail)
{
	if (!IsConnected() || IsClosing())
		return;

	LOG_WARN("추방 #{} ({}, {}) 단계 {} 플레이어 {}", SessionId(), KickText(reason), detail,
		static_cast<int>(Phase()), PlayerId());

	PacketBuilder<SC_Kick> kick(PacketId::SC_KICK);
	kick->reason = reason;
	CopyUtf8(kick->message, sizeof(kick->message), detail);
	SendAndDisconnect(kick.Finish(), KickText(reason));
}

// ───────────────── PacketSession ─────────────────

void GameSession::OnConnected()
{
	const uint64 now = ServerCore::NowMs();
	_lastRecvMs.store(now, std::memory_order_relaxed);	// 접속 순간을 "마지막 수신"으로: 아무것도 안 보내면 하트비트 시간 뒤 끊김
	LOG_DEBUG("접속 #{} {}", SessionId(), SocketUtils::ToString(RemoteAddress()));
}

void GameSession::OnRecvPacket(const char* packet, uint16 size, uint16 id)
{
	// 어떤 패킷이든(허용 여부와 무관하게) 받았다는 사실이 하트비트다.
	_lastRecvMs.store(ServerCore::NowMs(), std::memory_order_relaxed);
	PacketHandler::Dispatch(*this, packet, size, id);
}

void GameSession::OnDisconnected(const char* reason)
{
	LOG_DEBUG("해제 #{} ({})", SessionId(), reason);

	// 콘텐츠에 알린다. 로그인을 시도한 적이 없으면(Anonymous, 플레이어 ID 없음) 알릴 것이 없다.
	// Authenticating 중에 끊긴 경우도 알린다: Lobby JobQueue는 넣은 순서대로 실행하므로
	// 로비는 "로그인 처리 → 종료 처리" 순서로 받는다.
	const uint64_t playerId = PlayerId();
	if (playerId != 0 || Phase() != Content::ClientPhase::Anonymous)
		Content::Lobby().OnChannelClosed(SessionId(), playerId);
}
