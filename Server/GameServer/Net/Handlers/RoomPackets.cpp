#include "ServerCore/CorePch.h"
#include "GameServer/Net/Handlers/HandlerUtil.h"

#include "Content/ContentGlobal.h"
#include "Content/Interface/LobbyApi.h"

// 로비·방 2000~
//  - 방 밖의 요청(목록·생성·참여): ILobbyApi → 로비 JobQueue
//  - 방 안의 요청(나가기·캐릭터·퍽·준비): IRoomEndpoint::Post → GameRoom JobQueue

namespace {

bool Handle_CS_ROOM_LIST(GameSession& session, const char* packet, uint16 size)
{
	auto command = CommandDecoder::DecodeRoomList(packet, size);
	if (!command)
		return false;
	Content::Lobby().HandleRoomList(session.PlayerId(), *command);
	return true;
}

bool Handle_CS_CREATE_ROOM(GameSession& session, const char* packet, uint16 size)
{
	auto command = CommandDecoder::DecodeCreateRoom(packet, size);
	if (!command)
		return false;
	Content::Lobby().HandleCreateRoom(session.PlayerId(), std::move(*command));
	return true;
}

bool Handle_CS_JOIN_ROOM(GameSession& session, const char* packet, uint16 size)
{
	auto command = CommandDecoder::DecodeJoinRoom(packet, size);
	if (!command)
		return false;
	Content::Lobby().HandleJoinRoom(session.PlayerId(), *command);
	return true;
}

} // namespace

void RoomPackets::Register(PacketTable& table)
{
	using Phase = Content::ClientPhase;	// protocol.h의 전역 ClientPhase와 구분
	constexpr PhaseMask lobby = Mask(Phase::Lobby);
	constexpr PhaseMask room = Mask(Phase::Room);

	table.Add(PacketId::CS_ROOM_LIST, { .fn = Handle_CS_ROOM_LIST, .allowed = lobby, .name = "CS_ROOM_LIST" });
	table.Add(PacketId::CS_CREATE_ROOM, { .fn = Handle_CS_CREATE_ROOM, .allowed = lobby, .name = "CS_CREATE_ROOM" });
	table.Add(PacketId::CS_JOIN_ROOM, { .fn = Handle_CS_JOIN_ROOM, .allowed = lobby, .name = "CS_JOIN_ROOM" });

	table.Add(PacketId::CS_LEAVE_ROOM, { .fn = PostToRoom<CommandDecoder::DecodeLeaveRoom>, .allowed = room, .name = "CS_LEAVE_ROOM" });
	table.Add(PacketId::CS_SELECT_CHARACTER, { .fn = PostToRoom<CommandDecoder::DecodeSelectCharacter>, .allowed = room, .name = "CS_SELECT_CHARACTER" });
	table.Add(PacketId::CS_SELECT_PERKS, { .fn = PostToRoom<CommandDecoder::DecodeSelectPerks>, .allowed = room, .name = "CS_SELECT_PERKS" });
	table.Add(PacketId::CS_READY, { .fn = PostToRoom<CommandDecoder::DecodeReady>, .allowed = room, .name = "CS_READY" });
}
