#pragma once

// 패킷 ID → 핸들러 + 허용 단계 표.
//
// 중앙 switch 대신, 모듈(System/Login/Room/Game)이 자기 패킷을 이 표에 등록한다.
// 받은 패킷은 Dispatch가 "등록됐나 → 지금 단계에서 허용되나 → 핸들러" 순서로 처리한다.
//
//   등록 안 된 id               → 응답 없이 즉시 끊기
//   허용 안 된 단계              → SC_KICK(ProtocolViolation) 후 끊기
//     단, ignored 단계           → 조용히 버림 (Authenticating 중 CS_LOGIN 재전송)
//     단, 전환 1초 이내 입력      → 조용히 버림 (CS_MOVE/CS_ACTION, 경기 끝 직전에 보낸 입력)
//   핸들러가 false (형식 위반)   → SC_KICK(ProtocolViolation) 후 끊기
//
// 표는 서버 시작 때(Listener 전) 한 번 채우고 이후 읽기만 하므로 락이 없다.

#include "ServerCore/CorePch.h"
#include "Protocol/protocol.h"
#include "Content/Interface/ClientChannel.h"

class GameSession;

// 단계 집합을 비트로: Anonymous=1, Authenticating=2, Lobby=4, Room=8, Game=16
using PhaseMask = uint8_t;

constexpr PhaseMask PhaseBit(Content::ClientPhase phase)
{
	return static_cast<PhaseMask>(1u << static_cast<uint8_t>(phase));
}

template <class... Phases>
constexpr PhaseMask Mask(Phases... phases)
{
	return static_cast<PhaseMask>((PhaseMask{ 0 } | ... | PhaseBit(phases)));
}

constexpr PhaseMask kAnyPhase = Mask(Content::ClientPhase::Anonymous, Content::ClientPhase::Authenticating,
	Content::ClientPhase::Lobby, Content::ClientPhase::Room, Content::ClientPhase::Game);

// 핸들러는 "디코드 → 콘텐츠 API 호출"만 한다. 게임 규칙 판단 없음.
// false = 형식 위반 → Dispatch가 SC_KICK. (세션은 호출 동안 살아 있으므로 참조로 받는다)
using PacketHandlerFunc = bool (*)(GameSession& session, const char* packet, uint16 size);

struct HandlerEntry
{
	PacketHandlerFunc fn = nullptr;
	PhaseMask allowed = 0;						// 처리하는 단계
	PhaseMask ignored = 0;						// 끊지 않고 조용히 버리는 단계
	bool graceAfterPhaseChange = false;			// 단계 전환 1초 이내면 허용 단계가 아니어도 버림
	const char* name = "";
};

class PacketTable
{
public:
	// 같은 id를 두 번 등록하면(두 모듈이 겹침) 시작할 때 바로 멈춘다.
	void Add(PacketId id, const HandlerEntry& entry);
	const HandlerEntry* Find(uint16 id) const;
	uint32 Count() const { return _count; }

private:
	// id는 9999 이하. 배열 인덱스로 바로 찾는다: 해시도 분기도 없이 한 번에.
	// 1만 칸 × 24바이트 = 약 240KB. 서버 전체에 하나뿐이라 메모리보다 단순함을 택했다.
	static constexpr uint16 kMaxId = 10000;
	std::array<HandlerEntry, kMaxId> _entries{};
	uint32 _count = 0;
};

// 모듈별 등록 (중앙 switch 없음). 새 모듈은 Register 하나를 만들고 PacketHandler::Initialize에 한 줄 추가.
namespace SystemPackets { void Register(PacketTable& table); }	// CS_PING
namespace LoginPackets { void Register(PacketTable& table); }	// CS_LOGIN
namespace RoomPackets { void Register(PacketTable& table); }	// 로비·방
namespace GamePackets { void Register(PacketTable& table); }	// 로딩·이동·행동·복귀

namespace PacketHandler {

void Initialize();		// 모든 모듈 등록. Listener 시작 전에 한 번
void Dispatch(GameSession& session, const char* packet, uint16 size, uint16 id);

const PacketTable& Table();
const char* PhaseName(Content::ClientPhase phase);

} // namespace PacketHandler
