#include "ServerCore/CorePch.h"
#include "Content/Room/GameRoom.h"

#include <algorithm>

#include "Content/ContentGlobal.h"
#include "Content/GameData.h"
#include "Content/Interface/MessageEncoder.h"
#include "Content/Lobby/GameLobby.h"
#include "Content/Perf.h"

// 입장, 대기 상태(캐릭터·퍽·준비), 나가기, 공통 도우미. 게임 진행은 GameRoomPlay.cpp.

namespace Content {

GameRoom::GameRoom(uint32_t roomId, std::string name, std::weak_ptr<GameLobby> lobby)
	: _roomId(roomId), _name(std::move(name)), _lobby(std::move(lobby))
{
}

GameRoom::~GameRoom()
{
	if (_state == RoomState::Playing)
		Perf::RoomPlaying(false);	// 게임 중에 방이 사라져도 "게임 중인 방" 수가 맞게
}

// ───────────────── 입구: 다른 스레드가 부름 → 이 방의 Job으로 ─────────────────

void GameRoom::Post(uint64_t playerId, RoomCmd command)
{
	DoAsync(&GameRoom::HandleCommand, playerId, std::move(command));
}

void GameRoom::Enter(Entrant entrant, bool creator)
{
	DoAsync(&GameRoom::DoEnter, std::move(entrant), creator);
}

void GameRoom::Disconnected(uint64_t playerId)
{
	DoAsync(&GameRoom::DoDisconnected, playerId);
}

// ───────────────── Job ─────────────────

void GameRoom::DoEnter(Entrant entrant, bool creator)
{
	// 로비는 정원·상태를 확인하고 보냈지만, 그 사이에 방이 Loading으로 바뀌었을 수 있다(로비가 아직 모름).
	// 방이 마지막으로 한 번 더 확인하고, 안 되면 로비에 "이 사람은 못 들어왔다"고 알린다.
	if (_state != RoomState::Waiting || _members.size() >= GameData::MaxRoomPlayers)
	{
		const Result result = _state != RoomState::Waiting ? Result::RoomNotWaiting : Result::RoomFull;
		if (auto lobby = _lobby.lock())
			lobby->MemberLeft(_roomId, entrant.playerId);
		entrant.channel->Send(creator ? Encoder().Encode(Msg::CreateRoomResult{ result, 0 })
			: Encoder().Encode(Msg::JoinRoomResult{ result, _roomId }));
		return;
	}

	Member& member = _members.emplace_back();
	member.playerId = entrant.playerId;
	member.name = std::move(entrant.name);
	member.channel = std::move(entrant.channel);

	// 순서: 방 입구 연결 → 단계 변경 → 응답. 응답을 받은 클라가 곧바로 보내는 CS_SELECT_CHARACTER가
	// Room 단계 검사와 방 입구(Room())를 모두 통과하려면, 둘 다 응답보다 먼저 되어 있어야 한다.
	member.channel->BindRoom(Self());
	member.channel->SetPhase(ClientPhase::Room);
	member.channel->Send(creator ? Encoder().Encode(Msg::CreateRoomResult{ Result::Ok, _roomId })
		: Encoder().Encode(Msg::JoinRoomResult{ Result::Ok, _roomId }));
	BroadcastInfo();

	LOG_DEBUG("[Room {}] 플레이어 {} 입장 ({}/{})", _roomId, member.playerId, _members.size(), GameData::MaxRoomPlayers);
}

void GameRoom::DoDisconnected(uint64_t playerId)
{
	// 로비가 이미 자기 명단에서 지웠다. 방은 자기 멤버에서만 지우면 된다(로비에 다시 알리지 않음).
	if (Find(playerId) == nullptr)
		return;
	RemoveMember(playerId);
	LOG_DEBUG("[Room {}] 플레이어 {} 연결 끊김", _roomId, playerId);
}

void GameRoom::HandleCommand(uint64_t playerId, RoomCmd command)
{
	// 방을 나간 직후 늦게 도착한 명령이면 멤버가 아니다 → 버린다
	Member* member = Find(playerId);
	if (member == nullptr)
		return;

	// variant에 무엇이 들었는지 보고 나눈다
	if (std::holds_alternative<Cmd::LeaveRoom>(command))
		OnLeave(*member);
	else if (const auto* select = std::get_if<Cmd::SelectCharacter>(&command))
		OnSelectCharacter(*member, select->characterId);
	else if (const auto* perks = std::get_if<Cmd::SelectPerks>(&command))
		OnSelectPerks(*member, perks->perks);
	else if (const auto* ready = std::get_if<Cmd::Ready>(&command))
		OnReady(*member, ready->ready);
	else if (std::holds_alternative<Cmd::LoadComplete>(command))
		OnLoadComplete(*member);
	else if (std::holds_alternative<Cmd::Return>(command))
		OnReturn(*member);
}

// ───────────────── 대기 상태 명령 ─────────────────

void GameRoom::OnLeave(Member& member)
{
	// 게임이 시작되면(Loading부터) 클라 단계가 Game이라 브리지가 CS_LEAVE_ROOM을 막는다. 여기서도 한 번 더 막는다.
	if (_state != RoomState::Waiting)
		return SendError(member, RoomRequest::LeaveRoom, Result::RoomNotWaiting);

	// member는 RemoveMember 뒤에 사라지므로 필요한 것을 먼저 꺼내 둔다
	const uint64_t playerId = member.playerId;
	const ClientChannelRef channel = member.channel;
	RemoveMember(playerId);

	channel->BindRoom({});						// 방 입구 끊기: 이후 방 패킷은 버려진다
	channel->SetPhase(ClientPhase::Lobby);
	// 로비에 먼저 알리고 응답은 나중에. 응답을 받은 클라가 곧바로 CS_CREATE_ROOM을 보내도,
	// 로비의 큐에는 "나갔다"가 먼저 들어가 있어서 AlreadyInRoom으로 거절되지 않는다.
	if (auto lobby = _lobby.lock())
		lobby->MemberLeft(_roomId, playerId);
	channel->Send(Encoder().Encode(Msg::LeftRoom{ LeaveReason::Self }));

	LOG_DEBUG("[Room {}] 플레이어 {} 퇴장", _roomId, playerId);
}

void GameRoom::OnSelectCharacter(Member& member, uint16_t characterId)
{
	if (_state != RoomState::Waiting)
		return SendError(member, RoomRequest::SelectCharacter, Result::RoomNotWaiting);
	if (characterId == 0 || characterId > GameData::CharacterCount)
		return SendError(member, RoomRequest::SelectCharacter, Result::CharacterInvalid);
	if (member.characterId == characterId)
		return;		// 이미 고른 캐릭터: 바뀐 것이 없음

	const bool taken = std::any_of(_members.begin(), _members.end(),
		[&](const Member& other) { return other.playerId != member.playerId && other.characterId == characterId; });
	if (taken)
		return SendError(member, RoomRequest::SelectCharacter, Result::CharacterTaken);

	member.characterId = characterId;
	member.ready = false;		// 고른 것을 바꾸면 준비가 풀린다
	BroadcastInfo();
}

void GameRoom::OnSelectPerks(Member& member, const std::vector<uint16_t>& perks)
{
	if (_state != RoomState::Waiting)
		return SendError(member, RoomRequest::SelectPerks, Result::RoomNotWaiting);
	if (perks.size() > GameData::MaxPerks)
		return SendError(member, RoomRequest::SelectPerks, Result::PerkInvalid);
	for (size_t i = 0; i < perks.size(); ++i)
	{
		if (perks[i] == 0 || perks[i] > GameData::PerkCount)
			return SendError(member, RoomRequest::SelectPerks, Result::PerkInvalid);
		for (size_t k = 0; k < i; ++k)
			if (perks[k] == perks[i])
				return SendError(member, RoomRequest::SelectPerks, Result::PerkDuplicate);	// 자기 안에서 중복
	}

	member.perks = perks;		// 다른 사람과는 겹쳐도 된다
	member.ready = false;
	BroadcastInfo();
}

void GameRoom::OnReady(Member& member, bool ready)
{
	if (_state != RoomState::Waiting)
		return SendError(member, RoomRequest::Ready, Result::RoomNotWaiting);
	if (ready && member.characterId == 0)
		return SendError(member, RoomRequest::Ready, Result::NotReadyable);	// 캐릭터를 골라야 준비 가능

	member.ready = ready;
	const bool allReady = std::all_of(_members.begin(), _members.end(), [](const Member& m) { return m.ready; });
	if (allReady)
		SetState(RoomState::Loading);
	BroadcastInfo();			// 준비 표시와 방 상태를 한 번에 알린다

	if (allReady)
		BeginLoading();			// 전원 준비 (1명이어도 시작) → 게임 진행은 GameRoomPlay.cpp
}

// ───────────────── 도우미 ─────────────────

GameRoom::Member* GameRoom::Find(uint64_t playerId)
{
	for (Member& member : _members)
		if (member.playerId == playerId)
			return &member;
	return nullptr;
}

void GameRoom::RemoveMember(uint64_t playerId)
{
	std::erase_if(_members, [playerId](const Member& m) { return m.playerId == playerId; });

	switch (_state)
	{
	case RoomState::Waiting:
		if (!_members.empty())
			BroadcastInfo();	// 방장이 나갔다면 다음 사람(맨 앞)이 방장이 된 것도 여기서 알려진다
		break;

	case RoomState::Loading:
		// 로딩 중에 끊김: 의적은 이미 만들어 두었으니 "이탈"로 표시한다.
		// 남은 사람이 모두 로딩을 마쳤다면 더 기다릴 이유가 없다
		_world.MarkLeft(playerId);
		if (!_members.empty() && std::all_of(_members.begin(), _members.end(), [](const Member& m) { return m.loaded; }))
			StartGame();
		break;

	case RoomState::Playing:
		// 게임 중에 끊김: 의적은 "이탈"로 남겨 결과에 포함한다. 남은 사람이 없으면 판을 끝낸다
		_world.MarkLeft(playerId);
		if (_members.empty())
			EndGame(EndReason::Aborted);
		break;

	case RoomState::Result:
		break;	// 결과 화면에서 나감(메인 복귀 또는 끊김): 할 일 없음
	}
	// 마지막 사람이 나가면 로비가 방을 지운다(남은 Job이 끝나면 이 객체도 사라진다)
}

void GameRoom::SendError(Member& member, RoomRequest request, Result result)
{
	member.channel->Send(Encoder().Encode(Msg::RoomError{ request, result }));
}

void GameRoom::BroadcastInfo()
{
	Msg::RoomInfo info;
	info.roomId = _roomId;
	info.name = _name;
	info.hostPlayerId = _members.empty() ? 0 : _members.front().playerId;
	info.state = _state;
	for (const Member& member : _members)
		info.members.push_back({ member.playerId, member.name, member.characterId, member.perks, member.ready });
	Broadcast(Encoder().Encode(info));
}

void GameRoom::Broadcast(const OutMessage& message)
{
	// 한 번 인코딩한 버퍼를 모두에게 보낸다 (Send마다 복사되는 것은 shared_ptr 포인터뿐)
	for (const Member& member : _members)
		member.channel->Send(message);
}

void GameRoom::SetState(RoomState state)
{
	if (_state == state)
		return;
	if (state == RoomState::Playing || _state == RoomState::Playing)
		Perf::RoomPlaying(state == RoomState::Playing);		// 틱 지표의 "게임 중인 방" 수
	_state = state;
	if (auto lobby = _lobby.lock())
		lobby->RoomStateChanged(_roomId, state);	// 방 목록·입장 판단에 쓰도록 로비에도 알린다
}

std::shared_ptr<IRoomEndpoint> GameRoom::Self()
{
	// shared_from_this()는 JobQueue 타입이라 GameRoom으로 내린 뒤 IRoomEndpoint로 올린다
	return std::static_pointer_cast<GameRoom>(shared_from_this());
}

} // namespace Content
