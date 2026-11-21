#include "ServerCore/CorePch.h"
#include "Content/Lobby/GameLobby.h"

#include "Content/ContentGlobal.h"
#include "Content/GameData.h"
#include "Content/Interface/MessageEncoder.h"
#include "Content/Lobby/NameRule.h"
#include "Content/Room/GameRoom.h"

namespace Content {

// ───────────────── 입구: 다른 스레드가 부름 → 로비의 Job으로 ─────────────────

void GameLobby::HandleLogin(ClientChannelRef channel, Cmd::Login command)
{
	DoAsync(&GameLobby::Login, std::move(channel), std::move(command));
}

void GameLobby::HandleRoomList(uint64_t playerId, Cmd::RoomList /*command*/)
{
	DoAsync(&GameLobby::RoomList, playerId);
}

void GameLobby::HandleCreateRoom(uint64_t playerId, Cmd::CreateRoom command)
{
	DoAsync(&GameLobby::CreateRoom, playerId, std::move(command));
}

void GameLobby::HandleJoinRoom(uint64_t playerId, Cmd::JoinRoom command)
{
	DoAsync(&GameLobby::JoinRoom, playerId, command);
}

void GameLobby::OnChannelClosed(uint64_t channelId, uint64_t /*playerId*/)
{
	// playerId는 쓰지 않는다: 로그인 응답을 기다리는 중에 끊기면 세션은 아직 0을 들고 있지만,
	// 로비 큐에는 Login Job이 먼저 들어가 있어서 이 Job이 실행될 때는 이미 등록되어 있다.
	// 그래서 채널 ID로 찾는다.
	DoAsync(&GameLobby::ChannelClosed, channelId);
}

void GameLobby::MemberLeft(uint32_t roomId, uint64_t playerId)
{
	DoAsync(&GameLobby::DoMemberLeft, roomId, playerId);
}

void GameLobby::RoomStateChanged(uint32_t roomId, RoomState state)
{
	DoAsync(&GameLobby::DoRoomStateChanged, roomId, state);
}

void GameLobby::Shutdown()
{
	// Worker가 모두 멈춘 뒤에 불린다. 방의 남은 Job이 방·채널을 붙잡고 있을 수 있으므로 비운다.
	for (auto& [id, entry] : _rooms)
		entry.room->ClearJobs();
	_rooms.clear();
	_players.clear();
	_channelToPlayer.clear();
	_nameKeys.clear();
	ClearJobs();
}

// ───────────────── Job: 입장 ─────────────────

void GameLobby::Login(ClientChannelRef channel, Cmd::Login command)
{
	Msg::LoginResult result;
	const std::optional<std::string> name = NameRule::Normalize(command.name, GameData::NicknameMaxChars);
	const std::string key = name ? NameRule::CompareKey(*name) : std::string{};

	if (!name || _nameKeys.contains(key))
	{
		// 실패: 단계를 Anonymous로 되돌려 같은 연결로 다시 CS_LOGIN을 보낼 수 있게 한다
		result.result = name ? Result::NameInUse : Result::NameInvalid;
		result.name = command.name;
		channel->SetPhase(ClientPhase::Anonymous);
		channel->Send(Encoder().Encode(result));
		return;
	}

	const uint64_t playerId = _nextPlayerId++;
	_players[playerId] = Player{ channel, *name, key, 0 };
	_channelToPlayer[channel->ChannelId()] = playerId;
	_nameKeys.insert(key);

	result.result = Result::Ok;
	result.playerId = playerId;
	result.name = *name;

	// 순서: 플레이어 묶기 → 단계 변경 → 응답. 응답을 받은 클라가 곧바로 보내는 로비 패킷이
	// 아직 Authenticating인 세션에 도착해 "허용되지 않은 패킷"으로 끊기지 않게 한다.
	channel->BindPlayer(playerId);
	channel->SetPhase(ClientPhase::Lobby);
	channel->Send(Encoder().Encode(result));

	LOG_DEBUG("[Lobby] 로그인 \"{}\" → 플레이어 {}, 접속 {}명", *name, playerId, _players.size());
}

void GameLobby::ChannelClosed(uint64_t channelId)
{
	const auto it = _channelToPlayer.find(channelId);
	if (it == _channelToPlayer.end())
		return;		// 로그인한 적 없는 연결
	const uint64_t playerId = it->second;
	_channelToPlayer.erase(it);

	Player* player = FindPlayer(playerId);
	if (player == nullptr)
		return;

	// 방에 있었다면: 방에는 정리하라고 알리고, 로비 명단에서는 바로 뺀다
	if (player->roomId != 0)
	{
		const uint32_t roomId = player->roomId;
		if (auto found = _rooms.find(roomId); found != _rooms.end())
			found->second.room->Disconnected(playerId);
		RemoveFromRoom(roomId, playerId);
	}

	_nameKeys.erase(player->nameKey);		// 닉네임을 다른 사람이 쓸 수 있게
	LOG_DEBUG("[Lobby] 플레이어 {} \"{}\" 접속 종료", playerId, player->name);
	_players.erase(playerId);
}

// ───────────────── Job: 방 목록·생성·참여 ─────────────────

void GameLobby::RoomList(uint64_t playerId)
{
	Player* player = FindPlayer(playerId);
	if (player == nullptr)
		return;

	Msg::RoomList list;
	for (const auto& [roomId, entry] : _rooms)
	{
		if (entry.state != RoomState::Waiting || entry.members.empty())
			continue;	// 게임이 시작된 방은 들어갈 수 없으니 보여 주지 않는다
		const Player* host = FindPlayer(entry.members.front());
		list.rooms.push_back({ roomId, entry.name, host ? host->name : std::string{},
			static_cast<uint8_t>(entry.members.size()), entry.state });
		if (list.rooms.size() >= GameData::MaxRoomList)
			break;
	}
	player->channel->Send(Encoder().Encode(list));
}

void GameLobby::CreateRoom(uint64_t playerId, Cmd::CreateRoom command)
{
	Player* player = FindPlayer(playerId);
	if (player == nullptr)
		return;

	if (player->roomId != 0)
		return player->channel->Send(Encoder().Encode(Msg::CreateRoomResult{ Result::AlreadyInRoom, 0 }));

	const std::optional<std::string> name = NameRule::Normalize(command.name, GameData::RoomNameMaxChars);
	if (!name)
		return player->channel->Send(Encoder().Encode(Msg::CreateRoomResult{ Result::RoomNameInvalid, 0 }));

	const uint32_t roomId = _nextRoomId++;
	auto room = std::make_shared<GameRoom>(roomId, *name, std::static_pointer_cast<GameLobby>(shared_from_this()));

	RoomEntry& entry = _rooms[roomId];
	entry.room = room;
	entry.name = *name;
	entry.members.push_back(playerId);
	player->roomId = roomId;

	// 응답(SC_CREATE_ROOM, SC_ROOM_STATE)은 방이 보낸다: 방 입구 연결과 단계 변경을 방이 해야 하기 때문
	room->Enter({ playerId, player->name, player->channel }, true);
	LOG_DEBUG("[Lobby] 방 {} \"{}\" 생성 (방장 {})", roomId, *name, playerId);
}

void GameLobby::JoinRoom(uint64_t playerId, Cmd::JoinRoom command)
{
	Player* player = FindPlayer(playerId);
	if (player == nullptr)
		return;

	auto fail = [&](Result result) {
		player->channel->Send(Encoder().Encode(Msg::JoinRoomResult{ result, command.roomId }));
	};

	if (player->roomId != 0)
		return fail(Result::AlreadyInRoom);
	const auto it = _rooms.find(command.roomId);
	if (it == _rooms.end())
		return fail(Result::RoomNotFound);
	RoomEntry& entry = it->second;
	if (entry.state != RoomState::Waiting)
		return fail(Result::RoomNotWaiting);
	if (entry.members.size() >= GameData::MaxRoomPlayers)
		return fail(Result::RoomFull);

	// 정원 판단은 로비의 명단으로 한다: 모든 입장이 로비를 거치므로 로비의 명단이 기준이다
	entry.members.push_back(playerId);
	player->roomId = command.roomId;
	entry.room->Enter({ playerId, player->name, player->channel }, false);
}

// ───────────────── Job: 방이 보낸 알림 ─────────────────

void GameLobby::DoMemberLeft(uint32_t roomId, uint64_t playerId)
{
	// 접속 종료로 이미 지웠다면 아무 일도 없다(두 번 와도 안전)
	RemoveFromRoom(roomId, playerId);
}

void GameLobby::DoRoomStateChanged(uint32_t roomId, RoomState state)
{
	if (auto it = _rooms.find(roomId); it != _rooms.end())
		it->second.state = state;
}

// ───────────────── 도우미 ─────────────────

GameLobby::Player* GameLobby::FindPlayer(uint64_t playerId)
{
	const auto it = _players.find(playerId);
	return it == _players.end() ? nullptr : &it->second;
}

void GameLobby::RemoveFromRoom(uint32_t roomId, uint64_t playerId)
{
	if (Player* player = FindPlayer(playerId); player != nullptr && player->roomId == roomId)
		player->roomId = 0;

	const auto it = _rooms.find(roomId);
	if (it == _rooms.end())
		return;
	std::erase(it->second.members, playerId);
	if (it->second.members.empty())
	{
		// 마지막 사람이 나갔다. 방 객체는 남은 Job이 끝나면(그 Job들이 붙잡은 참조가 풀리면) 사라진다.
		LOG_DEBUG("[Lobby] 방 {} 삭제 (빈 방)", roomId);
		_rooms.erase(it);
	}
}

} // namespace Content
