#include "ServerCore/CorePch.h"
#include "Content/Room/GameRoom.h"

#include <algorithm>
#include <chrono>
#include <variant>

#include "ServerCore/CoreClock.h"
#include "Content/ContentGlobal.h"
#include "Content/GameData.h"
#include "Content/Game/MapData.h"
#include "Content/Interface/MessageEncoder.h"
#include "Content/Interface/RecordStore.h"
#include "Content/Lobby/GameLobby.h"
#include "Content/Perf.h"

// 게임 진행 (docs/DESIGN.md 6.1): 로딩 → 시작 → 20Hz 틱 → 종료 → 결과 → 메인 복귀.
// 규칙(상호작용·공격·포획·탈출)은 GameWorld가 판정하고, 방은 그 결과(이벤트)를 보내기만 한다 (P4).
// 시간이 걸리는 일(로딩 제한, 다음 틱, 결과 화면)은 모두 JobTimer(DoTimer)로 이 방의 Job을 예약한다.
// 예약에는 판 번호(matchId)를 함께 넣는다: 예약이 실행될 때 판이 이미 바뀌었으면 아무것도 하지 않는다.

namespace Content {

namespace {

std::atomic<uint64_t> g_nextMatchId{ 1 };	// 서버 전체에서 판마다 새 번호

const GameData::Timing& Timing() { return GameData::CurrentTiming(); }

} // namespace

// ───────────────── 입력 수신함 (IOCP Worker가 부름) ─────────────────

void GameRoom::PushInput(uint64_t playerId, const InputCmd& input)
{
	// 이동은 초당 최대 20개 × 3명이라 입력마다 Job을 만들지 않는다. 수신함에 넣기만 하고 틱이 한꺼번에 가져간다.
	// 수신함은 Worker와 방 Job이 함께 만지므로 락으로 지킨다(락 안에서는 push_back 하나뿐이라 짧다).
	std::lock_guard lock(_inboxLock);
	_inbox.push_back({ playerId, input });
}

// ───────────────── 로딩 ─────────────────

void GameRoom::BeginLoading()
{
	_matchId = g_nextMatchId.fetch_add(1);
	_tick = 0;
	_startMs = 0;

	// 멤버 순서대로 플레이어 자리를 정한다 (0번 스폰은 방장). 퍽·닉네임도 세계로 넘긴다
	std::vector<ThiefSlot> thieves;
	std::vector<std::string> names;
	Msg::GameLoad load;
	for (size_t i = 0; i < _members.size(); ++i)
	{
		Member& member = _members[i];
		member.loaded = false;
		thieves.push_back({ member.playerId, member.characterId, static_cast<uint8_t>(i), member.perks, member.name });
		names.push_back(member.name);
		load.spawns.push_back({ member.playerId, static_cast<uint8_t>(i) });
	}

	const MapData& map = DefaultMap();
	load.matchId = _matchId;
	load.mapId = map.mapId;
	load.mapVersion = map.version;
	_world.Setup(map, thieves, _rng);		// 엔티티를 지금 만들어 둔다(SC_GAME_START에 그대로 보냄)
	load.loadTimeoutMs = Timing().loadTimeoutMs;

	// 지난 최고 기록을 미리 읽어 둔다 (DB 스레드에서 돌고, 결과는 이 방의 Job으로 돌아온다)
	_recordsLoaded = false;
	_bestRecords.clear();
	std::weak_ptr<JobQueue> weakSelf = shared_from_this();
	const uint64_t matchId = _matchId;
	Records().LoadBest(std::move(names), [weakSelf, matchId](bool ok, IRecordStore::BestMap best) {
		if (auto self = std::static_pointer_cast<GameRoom>(weakSelf.lock()))
			self->DoAsync(&GameRoom::OnBestLoaded, matchId, ok, std::move(best));
	});

	// 단계를 먼저 Game으로: SC_GAME_LOAD를 받은 클라가 보내는 CS_LOAD_COMPLETE가 Game 단계 검사를 통과하도록
	for (Member& member : _members)
		member.channel->SetPhase(ClientPhase::Game);
	Broadcast(Encoder().Encode(load));

	DoTimer(Timing().loadTimeoutMs, &GameRoom::OnLoadTimeout, _matchId);
	LOG_DEBUG("[Room {}] 로딩 시작 (판 {}, {}명)", _roomId, _matchId, _members.size());
}

void GameRoom::OnLoadComplete(Member& member)
{
	if (_state != RoomState::Loading)
		return;		// 이미 시작했다면(제한 시간이 지나서) 늦은 완료 알림은 무시
	member.loaded = true;
	if (std::all_of(_members.begin(), _members.end(), [](const Member& m) { return m.loaded; }))
		StartGame();
}

void GameRoom::OnLoadTimeout(uint64_t matchId)
{
	if (matchId != _matchId || _state != RoomState::Loading || _members.empty())
		return;		// 지난 판의 예약이거나 이미 시작함
	LOG_DEBUG("[Room {}] 로딩 제한 시간 → 기다리지 않고 시작", _roomId);
	StartGame();	// 아직 로딩 중인 사람은 끝나는 대로 따라 들어온다(SC_GAME_START는 지금 받음)
}

// ───────────────── 시작과 틱 ─────────────────

void GameRoom::StartGame()
{
	if (_members.empty())
		return;

	SetState(RoomState::Playing);
	_startMs = ServerCore::NowMs();
	_tick = 0;
	{
		std::lock_guard lock(_inboxLock);
		_inbox.clear();		// 로딩 중에 들어온 입력은 버린다
	}

	// SC_GAME_START는 받는 사람마다 "내 의적"(selfEntityId)이 달라서 각자 인코딩한다(3명이라 부담 없음)
	Msg::GameStart start;
	start.durationMs = Timing().gameMs;
	start.tickHz = static_cast<uint8_t>(1000 / Timing().tickMs);
	start.entities = _world.SpawnInfos();
	for (const Member& member : _members)
	{
		const Entity* thief = _world.ThiefOf(member.playerId);
		start.selfEntityId = thief ? thief->id : 0;
		member.channel->Send(Encoder().Encode(start));
	}

	DoTimer(Timing().tickMs, &GameRoom::Tick, _matchId);
	LOG_DEBUG("[Room {}] 게임 시작 (판 {})", _roomId, _matchId);
}

void GameRoom::Tick(uint64_t matchId)
{
	if (matchId != _matchId || _state != RoomState::Playing)
		return;		// 판이 끝났으면 다음 틱을 예약하지 않는다 → 틱이 멈춘다
	if (_members.empty())
		return EndGame(EndReason::Aborted);

	const auto tickBegin = std::chrono::steady_clock::now();	// 틱 처리 시간 측정 (Perf)

	++_tick;
	ProcessInputs();									// ① 쌓인 입력 반영 (공격 시작·상호작용 시작 등)
	_world.Update(Timing().tickMs / 1000.0f);			// ② 시간 흐름: 스테미나, 상호작용 진행, 공격 판정, 탈출

	// ③ 이번 틱에 생긴 일(공격·포획·금고 열림…)을 스냅샷보다 먼저 보낸다.
	//    std::visit: variant 안에 실제로 든 타입의 Encode 오버로드를 골라 부른다
	for (const WorldEvent& event : _world.TakeEvents())
		std::visit([this](const auto& message) { Broadcast(Encoder().Encode(message)); }, event);
	SendStatuses();

	const uint64_t now = ServerCore::NowMs();
	const uint64_t elapsed = now - _startMs;
	const uint32_t remaining = elapsed >= Timing().gameMs ? 0 : static_cast<uint32_t>(Timing().gameMs - elapsed);

	// ④ 스냅샷: 방마다 한 번 만들어 모두에게 같은 버퍼로
	Broadcast(Encoder().Encode(_world.MakeSnapshot(_tick, remaining)));

	Perf::RecordTick(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now() - tickBegin).count()));

	// ⑤ 끝났나: 시간 종료 (G-07: 그때 가장 많이 반납한 사람이 1등). 남은 사람이 없으면 중단
	if (!_world.AnyThiefActive())
		return EndGame(EndReason::Aborted);
	if (remaining == 0)
		return EndGame(EndReason::TimeUp);

	// 다음 틱. "지금부터 50ms 뒤"가 아니라 "시작 시각 + (틱 번호+1) × 50ms"로 예약한다.
	// 처리 시간만큼 매번 늦어지는 것이 쌓이지 않게(늦었으면 바로 다음 틱을 돈다).
	const uint64_t nextDue = _startMs + static_cast<uint64_t>(_tick + 1) * Timing().tickMs;
	DoTimer(nextDue > now ? nextDue - now : 0, &GameRoom::Tick, matchId);
}

void GameRoom::ProcessInputs()
{
	{
		std::lock_guard lock(_inboxLock);
		_processing.swap(_inbox);		// 통째로 가져온다: 락은 swap 한 번뿐
	}

	for (const PendingInput& pending : _processing)
	{
		Member* member = Find(pending.playerId);
		if (member == nullptr)
			continue;		// 이미 나간 사람의 입력

		if (pending.input.kind == InputCmd::Kind::Move)
		{
			_world.ApplyMove(pending.playerId, pending.input);
		}
		else if (const auto reason = _world.ApplyAction(pending.playerId, pending.input))
		{
			// 거절: 클라가 미리 재생한 연출을 되돌리도록 그 사람에게만 알린다
			member->channel->Send(Encoder().Encode(Msg::ActionReject{ pending.input.seq, pending.input.action, *reason }));
		}
	}
	_processing.clear();	// 용량은 남아 다음 틱에 다시 쓴다
}

void GameRoom::SendStatuses()
{
	// HP·SP·무기·스킬처럼 본인만 알면 되는 값은 바뀐 틱에만 그 사람에게 (U-01, U-03)
	for (const auto& [playerId, status] : _world.TakeStatuses())
		if (Member* member = Find(playerId))
			member->channel->Send(Encoder().Encode(status));
}

void GameRoom::OnBestLoaded(uint64_t matchId, bool ok, std::unordered_map<std::string, uint32_t> best)
{
	if (matchId != _matchId)
		return;		// 지난 판의 응답
	_recordsLoaded = ok;
	_bestRecords = std::move(best);
	if (!ok)
		LOG_DEBUG("[Room {}] 최고 기록을 읽지 못함 (DB 없음) → 갱신 표시 없이 진행", _roomId);
}

// ───────────────── 종료·결과·복귀 ─────────────────

void GameRoom::EndGame(EndReason reason)
{
	if (_state == RoomState::Result)
		return;
	SetState(RoomState::Result);

	Msg::GameEnd end;
	end.matchId = _matchId;
	end.reason = reason;
	end.durationMs = _startMs != 0 ? static_cast<uint32_t>(ServerCore::NowMs() - _startMs) : 0;
	end.players = _world.MakeResults();
	end.resultScreenMs = Timing().resultMs;

	// 최고 기록 (F-08): 점수(= 반납 금액)가 지난 기록보다 높으면 갱신. 저장은 DB 스레드가 하고 기다리지 않는다
	for (Msg::PlayerResult& result : end.players)
	{
		const auto found = _bestRecords.find(result.name);
		const uint32_t previous = found != _bestRecords.end() ? found->second : 0;
		result.newRecord = _recordsLoaded && result.banked > previous;
		result.bestRecord = std::max(previous, result.banked);
		if (reason == EndReason::TimeUp)
			Records().SaveScore(result.name, result.characterId, result.banked);
	}
	Broadcast(Encoder().Encode(end));
	{
		std::lock_guard lock(_inboxLock);
		_inbox.clear();
	}

	if (!_members.empty())
		DoTimer(Timing().resultMs, &GameRoom::OnResultTimeout, _matchId);
	LOG_DEBUG("[Room {}] 게임 종료 (판 {}, 이유 {})", _roomId, _matchId, static_cast<int>(reason));
}

void GameRoom::OnResultTimeout(uint64_t matchId)
{
	if (matchId != _matchId || _state != RoomState::Result)
		return;
	// 남은 사람을 모두 메인으로. ReturnToLobby가 _members를 지우므로 번호를 먼저 복사해 둔다
	std::vector<uint64_t> remaining;
	for (const Member& member : _members)
		remaining.push_back(member.playerId);
	for (uint64_t playerId : remaining)
		ReturnToLobby(playerId);
}

void GameRoom::OnReturn(Member& member)
{
	if (_state != RoomState::Result)
		return;		// 게임 중의 CS_RETURN은 무시 (게임 중 나가기는 없음)
	ReturnToLobby(member.playerId);
}

void GameRoom::ReturnToLobby(uint64_t playerId)
{
	Member* member = Find(playerId);
	if (member == nullptr)
		return;
	const ClientChannelRef channel = member->channel;
	std::erase_if(_members, [playerId](const Member& m) { return m.playerId == playerId; });

	// 나가기(OnLeave)와 같은 순서: 방 입구 끊기 → 단계 Lobby → 로비에 먼저 알림 → 응답
	channel->BindRoom({});
	channel->SetPhase(ClientPhase::Lobby);
	if (auto lobby = _lobby.lock())
		lobby->MemberLeft(_roomId, playerId);	// 마지막 사람이면 로비가 방을 지운다
	channel->Send(Encoder().Encode(Msg::LeftRoom{ LeaveReason::GameEnded }));
}

} // namespace Content
