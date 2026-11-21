#include "ServerCore/CorePch.h"

#include <set>

#include "Content/GameData.h"
#include "Content/Interface/LobbyApi.h"
#include "ContentFixture.h"
#include "TestFramework.h"

// 게임 진행 (P3): 로딩 → 시작 → 틱(스냅샷·입력) → 시간 종료 → 결과 → 메인 복귀.
// 5분을 기다릴 수 없으므로 시간을 짧게 바꾸고(ShortTiming), PumpUntil로 JobTimer를 돌린다.

using namespace Content;
namespace Msg = Content::Msg;
namespace Cmd = Content::Cmd;

namespace {

// 시험 동안만 시간을 짧게: 틱 50ms, 게임 400ms, 로딩 제한 300ms, 결과 화면 300ms
struct ShortTiming
{
	GameData::Timing saved = GameData::CurrentTiming();
	ShortTiming() { GameData::CurrentTiming() = GameData::Timing{ 50, 400, 300, 300 }; }
	~ShortTiming() { GameData::CurrentTiming() = saved; }
};

// n명이 방 1에 모여 캐릭터를 고르고 모두 준비한 상태 (→ Loading, SC_GAME_LOAD까지 받음)
struct ReadyRoom
{
	ShortTiming timing;		// 픽스처보다 먼저 만들고 나중에 되돌린다(선언 순서)
	ContentFixture f;
	std::vector<std::shared_ptr<FakeChannel>> players;

	explicit ReadyRoom(int count)
	{
		for (int i = 0; i < count; ++i)
			players.push_back(f.LoggedIn(std::format("p{}", i)));
		Content::Lobby().HandleCreateRoom(players[0]->PlayerId(), { "방" });
		for (int i = 1; i < count; ++i)
			Content::Lobby().HandleJoinRoom(players[static_cast<size_t>(i)]->PlayerId(), { 1 });
		f.Drain();
		for (int i = 0; i < count; ++i)
			ContentFixture::Post(players[static_cast<size_t>(i)], Cmd::SelectCharacter{ static_cast<uint16_t>(i + 1) });
		for (int i = 0; i < count; ++i)
			ContentFixture::Post(players[static_cast<size_t>(i)], Cmd::Ready{ true });
	}

	void LoadAll()
	{
		for (auto& p : players)
			ContentFixture::Post(p, Cmd::LoadComplete{});
	}

	static void Push(const std::shared_ptr<FakeChannel>& p, const InputCmd& input)
	{
		if (auto room = p->Room())
			room->PushInput(p->PlayerId(), input);
	}

	template <class T>
	static size_t Count(const std::shared_ptr<FakeChannel>& p) { return p->Sent<T>().size(); }
};

} // namespace

// ───────────────── 로딩 ─────────────────

TEST_CASE(Play_AllReady_SendsGameLoad_PhaseGame)
{
	ReadyRoom r(2);
	for (auto& p : r.players)
	{
		const auto load = ContentFixture::Last<Msg::GameLoad>(p);
		CHECK(load && load->spawns.size() == 2 && load->mapId == 1);
		CHECK(p->Phase() == ClientPhase::Game);		// CS_LOAD_COMPLETE가 단계 검사를 통과하도록 먼저 Game
	}
	CHECK(!ContentFixture::Last<Msg::GameStart>(r.players[0]));	// 아직 모두 로딩을 마치지 않음
}

TEST_CASE(Play_AllLoaded_StartsGame_EachSeesOwnThief)
{
	ReadyRoom r(3);
	r.LoadAll();

	std::set<uint16_t> selfIds;
	for (auto& p : r.players)
	{
		const auto start = ContentFixture::Last<Msg::GameStart>(p);
		CHECK(start && start->tickHz == 20 && start->entities.size() >= 17);	// 플레이어 3 + 경비원 3 + 금고 1 + 반납 지점 5 + 필드 아이템 5 + 상자
		if (start)
			selfIds.insert(start->selfEntityId);
	}
	CHECK(selfIds == std::set<uint16_t>({ 1, 2, 3 }));		// 각자 자기 의적 번호를 받음
}

TEST_CASE(Play_LoadTimeout_StartsWithoutWaiting)
{
	ReadyRoom r(2);
	ContentFixture::Post(r.players[0], Cmd::LoadComplete{});		// 한 명만 완료
	const bool started = ContentFixture::PumpUntil([&] { return ContentFixture::Last<Msg::GameStart>(r.players[1]).has_value(); });
	CHECK(started);		// 300ms 뒤 기다리지 않고 시작. 로딩이 덜 된 사람도 SC_GAME_START를 받는다
}

TEST_CASE(Play_DisconnectDuringLoading_OthersStart)
{
	ReadyRoom r(2);
	ContentFixture::Post(r.players[0], Cmd::LoadComplete{});
	ContentFixture::Disconnect(r.players[1]);		// 남은 사람(0번)은 이미 완료 → 바로 시작
	const auto start = ContentFixture::Last<Msg::GameStart>(r.players[0]);
	CHECK(start.has_value());
	if (start)
		CHECK((start->entities[1].state & StateBit::LeftGame) != 0);	// 끊긴 사람의 의적은 이탈 표시
}

// ───────────────── 틱 ─────────────────

TEST_CASE(Play_Tick_SendsSnapshots_WithClientPosition)
{
	ReadyRoom r(2);
	r.LoadAll();

	InputCmd move;
	move.kind = InputCmd::Kind::Move;
	move.pos = { 111, 222, 0 };
	move.flags = MoveFlag::Moving;
	ReadyRoom::Push(r.players[0], move);

	// 다른 사람(1번)이 받은 스냅샷에서 0번의 의적(엔티티 1)이 그 위치에 보이면 성공
	const bool seen = ContentFixture::PumpUntil([&] {
		const auto snap = ContentFixture::Last<Msg::Snapshot>(r.players[1]);
		return snap && !snap->entities.empty() && snap->entities[0].pos.x == 111 && snap->entities[0].pos.y == 222;
	});
	CHECK(seen);
	const auto snap = ContentFixture::Last<Msg::Snapshot>(r.players[1]);
	CHECK(snap && snap->tick >= 1 && snap->remainingMs <= 400);
}

TEST_CASE(Play_UnsupportedAction_RejectedToSenderOnly)
{
	ReadyRoom r(2);
	r.LoadAll();
	InputCmd attack;
	attack.kind = InputCmd::Kind::Action;
	attack.action = ActionType::UseConsumable;	// 소비 아이템이 없으면 거절 (CT-08)
	attack.seq = 42;
	ReadyRoom::Push(r.players[0], attack);

	const bool rejected = ContentFixture::PumpUntil([&] { return ContentFixture::Last<Msg::ActionReject>(r.players[0]).has_value(); });
	CHECK(rejected);
	const auto reject = ContentFixture::Last<Msg::ActionReject>(r.players[0]);
	CHECK(reject && reject->seq == 42 && reject->reason == RejectReason::NotAvailable);
	CHECK(!ContentFixture::Last<Msg::ActionReject>(r.players[1]));
}

TEST_CASE(Play_PlayerStatus_SentToEachPlayerOnly)
{
	ReadyRoom r(2);
	r.LoadAll();
	const bool got = ContentFixture::PumpUntil([&] {
		return ContentFixture::Last<Msg::PlayerStatus>(r.players[0]).has_value() && ContentFixture::Last<Msg::PlayerStatus>(r.players[1]).has_value();
	});
	CHECK(got);
	const auto status = ContentFixture::Last<Msg::PlayerStatus>(r.players[0]);
	CHECK(status && status->hp == 100 && status->weapons[0].id == WeaponId::Baton && status->skill == SkillId::Taser);
	CHECK_EQ(r.players[0]->Sent<Msg::PlayerStatus>().size(), size_t{ 1 });	// 바뀐 것이 없으면 다시 안 보냄
}

// ───────────────── 종료·결과·복귀 ─────────────────

TEST_CASE(Play_TimeUp_GameEnd_ThenEveryoneBackToLobby)
{
	ReadyRoom r(2);
	r.LoadAll();

	const bool ended = ContentFixture::PumpUntil([&] { return ContentFixture::Last<Msg::GameEnd>(r.players[0]).has_value(); });
	CHECK(ended);
	const auto end = ContentFixture::Last<Msg::GameEnd>(r.players[0]);
	CHECK(end && end->reason == EndReason::TimeUp && end->players.size() == 2 && end->resultScreenMs == 300);

	// 결과 화면 시간(300ms)이 지나면 모두 메인으로
	const bool back = ContentFixture::PumpUntil([&] {
		return r.players[0]->Phase() == ClientPhase::Lobby && r.players[1]->Phase() == ClientPhase::Lobby;
	});
	CHECK(back);
	const auto left = ContentFixture::Last<Msg::LeftRoom>(r.players[1]);
	CHECK(left && left->reason == LeaveReason::GameEnded);
	CHECK(r.players[0]->Room() == nullptr);

	// 방은 사라졌다
	Content::Lobby().HandleRoomList(r.players[0]->PlayerId(), {});
	r.f.Drain();
	const auto list = ContentFixture::Last<Msg::RoomList>(r.players[0]);
	CHECK(list && list->rooms.empty());
}

TEST_CASE(Play_ReturnEarly_FromResultScreen)
{
	ReadyRoom r(2);
	r.LoadAll();
	ContentFixture::PumpUntil([&] { return ContentFixture::Last<Msg::GameEnd>(r.players[0]).has_value(); });

	ContentFixture::Post(r.players[0], Cmd::Return{});		// 0번만 먼저 메인으로
	CHECK(r.players[0]->Phase() == ClientPhase::Lobby);
	CHECK(r.players[1]->Phase() == ClientPhase::Game);		// 1번은 아직 결과 화면

	// 먼저 나간 사람은 곧바로 새 방을 만들 수 있다
	Content::Lobby().HandleCreateRoom(r.players[0]->PlayerId(), { "다음 판" });
	r.f.Drain();
	const auto created = ContentFixture::Last<Msg::CreateRoomResult>(r.players[0]);
	CHECK(created && created->result == Result::Ok && created->roomId == 2);
}

TEST_CASE(Play_ReturnDuringGame_Ignored)
{
	ReadyRoom r(1);
	r.LoadAll();
	ContentFixture::Post(r.players[0], Cmd::Return{});
	CHECK(r.players[0]->Phase() == ClientPhase::Game);		// 게임 중에는 나가기가 없다
}

TEST_CASE(Play_DisconnectDuringGame_ThiefMarkedLeft)
{
	ReadyRoom r(2);
	r.LoadAll();
	ContentFixture::Disconnect(r.players[1]);

	const bool ended = ContentFixture::PumpUntil([&] { return ContentFixture::Last<Msg::GameEnd>(r.players[0]).has_value(); });
	CHECK(ended);
	const auto end = ContentFixture::Last<Msg::GameEnd>(r.players[0]);
	CHECK(end && end->players.size() == 2 && !end->players[0].leftEarly && end->players[1].leftEarly);
}

TEST_CASE(Play_EveryoneDisconnects_RoomGone)
{
	ReadyRoom r(2);
	r.LoadAll();
	auto viewer = r.f.LoggedIn("구경꾼");
	ContentFixture::Disconnect(r.players[0]);
	ContentFixture::Disconnect(r.players[1]);
	ContentFixture::PumpUntil([] { return false; }, 120);	// 틱이 몇 번 돌 시간 (남은 사람이 없으니 멈춰야 함)

	Content::Lobby().HandleRoomList(viewer->PlayerId(), {});
	r.f.Drain();
	const auto list = ContentFixture::Last<Msg::RoomList>(viewer);
	CHECK(list && list->rooms.empty());
}
