#include "ServerCore/CorePch.h"
#include "GameServer/Net/PacketHandler.h"

#include "GameServer/Net/GameSession.h"

// ───────────────── PacketTable ─────────────────

void PacketTable::Add(PacketId id, const HandlerEntry& entry)
{
	const uint16 index = static_cast<uint16>(id);
	if (index >= kMaxId || entry.fn == nullptr || entry.allowed == 0)
	{
		LOG_ERROR("[PacketTable] 잘못된 등록: {} (id {})", entry.name, index);
		std::abort();
	}
	if (_entries[index].fn != nullptr)
	{
		LOG_ERROR("[PacketTable] id {} 중복 등록: {} / {}", index, _entries[index].name, entry.name);
		std::abort();
	}
	_entries[index] = entry;
	++_count;
}

const HandlerEntry* PacketTable::Find(uint16 id) const
{
	if (id >= kMaxId)
		return nullptr;
	const HandlerEntry& entry = _entries[id];
	return entry.fn != nullptr ? &entry : nullptr;
}

// ───────────────── PacketHandler ─────────────────

namespace PacketHandler {

namespace {

PacketTable g_table;
bool g_initialized = false;


} // namespace

void Initialize()
{
	if (g_initialized)
		return;
	SystemPackets::Register(g_table);
	LoginPackets::Register(g_table);
	RoomPackets::Register(g_table);
	GamePackets::Register(g_table);
	g_initialized = true;
	LOG_INFO("[PacketHandler] 핸들러 {}개 등록", g_table.Count());
}

const PacketTable& Table()
{
	return g_table;
}

const char* PhaseName(Content::ClientPhase phase)
{
	switch (phase)
	{
	case Content::ClientPhase::Anonymous:      return "Anonymous";
	case Content::ClientPhase::Authenticating: return "Authenticating";
	case Content::ClientPhase::Lobby:          return "Lobby";
	case Content::ClientPhase::Room:           return "Room";
	case Content::ClientPhase::Game:           return "Game";
	}
	return "?";
}

void Dispatch(GameSession& session, const char* packet, uint16 size, uint16 id)
{
	// 1) 등록되지 않은 id: 클라이언트가 프로토콜을 전혀 따르지 않는 것 → 응답 없이 끊는다
	const HandlerEntry* entry = g_table.Find(id);
	if (entry == nullptr)
	{
		LOG_WARN("Session {}: 등록되지 않은 패킷 id {}", session.SessionId(), id);
		session.Disconnect("unknown packet id");
		return;
	}

	// 2) 단계 검사. 단계는 한 번만 읽어서 판단한다(판단 도중 콘텐츠가 바꿔도 일관되게).
	const Content::ClientPhase phase = session.Phase();
	const PhaseMask bit = PhaseBit(phase);
	if ((entry->allowed & bit) == 0)
	{
		if ((entry->ignored & bit) != 0)
			return;
		if (entry->graceAfterPhaseChange)
		{
			// 경기가 끝나 Game → Room이 된 직후에 도착한 CS_MOVE: 클라는 정상인데 "늦게 도착"했을 뿐이다
			const uint64 changedMs = session.PhaseChangedMs();
			if (changedMs != 0 && ServerTimeMs() - changedMs < PHASE_CHANGE_GRACE_MS)
				return;
		}
		const std::string detail = std::format("{} not allowed in {}", entry->name, PhaseName(phase));
		session.Kick(KickReason::ProtocolViolation, detail.c_str());
		return;
	}

	// 3) 핸들러: 디코드(형식 검사) → 콘텐츠 API
	if (!entry->fn(session, packet, size))
	{
		const std::string detail = std::format("invalid {}", entry->name);
		session.Kick(KickReason::ProtocolViolation, detail.c_str());
	}
}

} // namespace PacketHandler
