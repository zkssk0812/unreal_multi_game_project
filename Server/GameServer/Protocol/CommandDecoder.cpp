#include "ServerCore/CorePch.h"
#include "GameServer/Protocol/CommandDecoder.h"

#include <cmath>

#include "Protocol/ProtocolUtil.h"
#include "Protocol/protocol.h"

// 콘텐츠 enum과 프로토콜 enum은 static_cast로 옮긴다. 값이 같다는 약속은 MessageEncoder.cpp가 확인한다.

namespace CommandDecoder {

// using namespace Content를 쓰지 않는다: Vec3, ActionType 등이 protocol.h의 전역 이름과 겹친다.
namespace Cmd = Content::Cmd;
using Content::InputCmd;

namespace {

// 고정 길이 char 배열 안에 널 문자가 있는지. 없으면 배열 밖까지 읽게 되므로 형식 위반이다.
// (보내는 쪽 C++ 클라는 MakePacket이 0으로 채워 주지만, 네트워크로 온 바이트는 믿지 않는다)
template <size_t N>
std::optional<std::string> ReadFixedString(const char (&field)[N])
{
	const void* end = std::memchr(field, '\0', N);
	if (end == nullptr)
		return std::nullopt;
	return std::string(field, static_cast<const char*>(end));
}

// 받은 바이트의 bool 필드. bool 자리에 0/1이 아닌 값(예: 0x02)이 들어 있으면 그 bool을 읽는 것은
// 정의되지 않은 동작이다. 바이트로 읽어서 0이 아니면 참으로 정규화한다.
bool ReadBool(const bool& field)
{
	uint8_t raw;
	std::memcpy(&raw, &field, 1);
	return raw != 0;
}

// NaN·무한대가 들어오면 이후 거리 계산이 모두 NaN이 되어 검증을 빠져나간다 → 형식 위반으로 본다.
bool IsFinite(float value) { return std::isfinite(value); }

// 본문 없이 헤더만 있는 패킷(CS_ROOM_LIST, CS_LEAVE_ROOM 등): 크기만 확인
template <class Packet, class Command>
std::optional<Command> DecodeEmpty(const char* packet, uint16_t size)
{
	Packet raw;
	if (!Protocol::ReadPacket(packet, size, raw))
		return std::nullopt;
	return Command{};
}

} // namespace

// ───────────────── 입장 ─────────────────

std::optional<LoginRequest> DecodeLogin(const char* packet, uint16_t size)
{
	CS_Login raw;
	if (!Protocol::ReadPacket(packet, size, raw))
		return std::nullopt;			// 크기가 정확히 sizeof(CS_Login)이 아님

	auto name = ReadFixedString(raw.name);
	if (!name)
		return std::nullopt;			// 49바이트 안에 널이 없음 → 이름이 48바이트를 넘거나 쓰레기

	LoginRequest request;
	request.protocolVersion = raw.protocol_version;
	request.command.name = std::move(*name);	// 빈 이름도 여기서는 통과: "빈 이름은 안 됨"은 로비 규칙
	return request;
}

// ───────────────── 로비·방 ─────────────────

std::optional<Cmd::RoomList> DecodeRoomList(const char* packet, uint16_t size)
{
	return DecodeEmpty<CS_RoomList, Cmd::RoomList>(packet, size);
}

std::optional<Cmd::CreateRoom> DecodeCreateRoom(const char* packet, uint16_t size)
{
	CS_CreateRoom raw;
	if (!Protocol::ReadPacket(packet, size, raw))
		return std::nullopt;
	auto name = ReadFixedString(raw.name);
	if (!name)
		return std::nullopt;
	return Cmd::CreateRoom{ std::move(*name) };
}

std::optional<Cmd::JoinRoom> DecodeJoinRoom(const char* packet, uint16_t size)
{
	CS_JoinRoom raw;
	if (!Protocol::ReadPacket(packet, size, raw))
		return std::nullopt;
	return Cmd::JoinRoom{ raw.room_id };
}

std::optional<Cmd::LeaveRoom> DecodeLeaveRoom(const char* packet, uint16_t size)
{
	return DecodeEmpty<CS_LeaveRoom, Cmd::LeaveRoom>(packet, size);
}

std::optional<Cmd::SelectCharacter> DecodeSelectCharacter(const char* packet, uint16_t size)
{
	CS_SelectCharacter raw;
	if (!Protocol::ReadPacket(packet, size, raw))
		return std::nullopt;
	return Cmd::SelectCharacter{ raw.character_id };
}

std::optional<Cmd::SelectPerks> DecodeSelectPerks(const char* packet, uint16_t size)
{
	CS_SelectPerks raw;
	if (!Protocol::ReadPacket(packet, size, raw))
		return std::nullopt;
	// 개수는 형식이다: 배열이 3칸이라 4 이상이면 배열 밖을 읽게 된다. (중복 여부는 콘텐츠 규칙)
	if (raw.perk_count > MAX_PERKS)
		return std::nullopt;
	return Cmd::SelectPerks{ std::vector<uint16_t>(raw.perks, raw.perks + raw.perk_count) };
}

std::optional<Cmd::Ready> DecodeReady(const char* packet, uint16_t size)
{
	CS_Ready raw;
	if (!Protocol::ReadPacket(packet, size, raw))
		return std::nullopt;
	return Cmd::Ready{ ReadBool(raw.ready) };
}

// ───────────────── 게임 진행 ─────────────────

std::optional<Cmd::LoadComplete> DecodeLoadComplete(const char* packet, uint16_t size)
{
	return DecodeEmpty<CS_LoadComplete, Cmd::LoadComplete>(packet, size);
}

std::optional<Cmd::Return> DecodeReturn(const char* packet, uint16_t size)
{
	return DecodeEmpty<CS_Return, Cmd::Return>(packet, size);
}

std::optional<InputCmd> DecodeMove(const char* packet, uint16_t size)
{
	CS_Move raw;
	if (!Protocol::ReadPacket(packet, size, raw))
		return std::nullopt;
	if (!IsFinite(raw.pos.x) || !IsFinite(raw.pos.y) || !IsFinite(raw.pos.z) || !IsFinite(raw.yaw))
		return std::nullopt;

	InputCmd input;
	input.kind = InputCmd::Kind::Move;
	input.seq = raw.seq;
	input.pos = { raw.pos.x, raw.pos.y, raw.pos.z };
	input.yaw = raw.yaw;
	input.flags = raw.flags;
	return input;
}

std::optional<InputCmd> DecodeAction(const char* packet, uint16_t size)
{
	CS_Action raw;
	if (!Protocol::ReadPacket(packet, size, raw))
		return std::nullopt;
	if (!IsFinite(raw.yaw) || !IsFinite(raw.aim.x) || !IsFinite(raw.aim.y) || !IsFinite(raw.aim.z))
		return std::nullopt;

	InputCmd input;
	input.kind = InputCmd::Kind::Action;
	input.seq = raw.seq;
	input.yaw = raw.yaw;
	input.action = static_cast<Content::ActionType>(raw.action);	// 없는 값이면 방이 SC_ACTION_REJECT
	input.targetEntityId = raw.target_entity_id;
	input.aim = { raw.aim.x, raw.aim.y, raw.aim.z };
	return input;
}

} // namespace CommandDecoder
