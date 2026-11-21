#pragma once

#include "ServerCore/CorePch.h"
#include "ServerCore/PacketSession.h"
#include "Protocol/protocol.h"
#include "Content/Interface/ClientChannel.h"
#include "Content/Interface/RoomEndpoint.h"

// 클라이언트 연결 하나.
//  - 네트워크 쪽: PacketSession. 패킷 조립은 코어가 하고, 완성된 패킷을 PacketHandler 표로 넘긴다.
//  - 콘텐츠 쪽: IClientChannel. 콘텐츠는 이 객체를 GameSession이 아니라 "채널"로만 본다.
//
// 상태 세 가지(단계, 계정 ID, 방 입구)는 쓰는 스레드와 읽는 스레드가 다르다.
// 콘텐츠(Lobby·GameRoom 스트랜드)가 쓰고, IOCP Worker(패킷 핸들러)가 읽는다 → 모두 atomic.
class GameSession final : public PacketSession, public Content::IClientChannel
{
public:
	// ── IClientChannel (콘텐츠가 호출, 아무 스레드) ──
	uint64_t ChannelId() const override { return SessionId(); }
	void Send(const Content::OutMessage& message) override { PacketSession::Send(message); }
	void SetPhase(Content::ClientPhase phase) override;
	void BindPlayer(uint64_t playerId) override { _playerId.store(playerId); }
	void BindRoom(std::weak_ptr<Content::IRoomEndpoint> room) override { _room.store(std::move(room)); }
	void Close(Content::CloseReason reason) override;

	// ── 패킷 핸들러가 사용 (IOCP Worker) ──
	Content::ClientPhase Phase() const { return _phase.load(); }
	uint64_t PhaseChangedMs() const { return _phaseChangedMs.load(); }
	uint64_t PlayerId() const { return _playerId.load(); }
	std::shared_ptr<Content::IRoomEndpoint> Room() const { return _room.load().lock(); }
	Content::ClientChannelRef SharedChannel();

	// ── Heartbeat가 사용 (다른 스레드에서 읽음) ──
	uint64 LastRecvMs() const { return _lastRecvMs.load(std::memory_order_relaxed); }

	// SC_KICK을 보내고, 상대가 받은 뒤에 끊는다.
	void Kick(KickReason reason, const char* detail);

protected:
	void OnConnected() override;
	void OnRecvPacket(const char* packet, uint16 size, uint16 id) override;
	void OnDisconnected(const char* reason) override;

private:
	std::atomic<Content::ClientPhase> _phase{ Content::ClientPhase::Anonymous };
	std::atomic<uint64_t> _phaseChangedMs{ 0 };		// 단계 전환 1초 유예에 쓴다
	std::atomic<uint64_t> _playerId{ 0 };			// 로그인한 플레이어 ID, 0 = 로그인 전
	// C++20 atomic<weak_ptr>: 콘텐츠가 BindRoom으로 바꾸는 동안 Worker가 읽어도 안전하다.
	// (내부적으로 작은 락을 쓰지만, 방 입장·퇴장 때만 쓰이고 읽기는 짧다)
	std::atomic<std::weak_ptr<Content::IRoomEndpoint>> _room;

	// 시각은 모두 ServerCore::NowMs(단조 시계). 정확한 순서가 필요 없는 "대략 언제"라 relaxed로 충분하다.
	std::atomic<uint64> _lastRecvMs{ 0 };	// 패킷마다 갱신: 하트비트 판정
};

// 서버 시각: 서버 시작 기준 단조 증가 ms
uint64 ServerTimeMs();
