#include "ServerCore/CorePch.h"

#include "Content/Interface/LobbyApi.h"
#include "ContentFixture.h"
#include "TestFramework.h"

// 방 대기 상태: 캐릭터·퍽·준비·나가기·방장 넘기기·전원 준비 시작.
// 방 명령은 브리지의 PostToRoom처럼 채널에 연결된 방(FakeChannel::Room())에 Post로 넣는다.

using Content::ClientPhase;
using Content::Result;
using Content::RoomRequest;
using Content::RoomState;
namespace Msg = Content::Msg;
namespace Cmd = Content::Cmd;

namespace {

// 방장 + 손님 n명이 방 1에 모인 상태를 만든다
struct RoomSetup
{
	ContentFixture f;
	std::vector<std::shared_ptr<FakeChannel>> players;

	explicit RoomSetup(int count)
	{
		for (int i = 0; i < count; ++i)
			players.push_back(f.LoggedIn(std::format("p{}", i)));
		Content::Lobby().HandleCreateRoom(players[0]->PlayerId(), { "방" });
		for (int i = 1; i < count; ++i)
			Content::Lobby().HandleJoinRoom(players[static_cast<size_t>(i)]->PlayerId(), { 1 });
		f.Drain();
	}

	std::optional<Msg::RoomInfo> Info(size_t viewer = 0) const { return ContentFixture::Last<Msg::RoomInfo>(players[viewer]); }
};

const Msg::RoomMember* MemberOf(const Msg::RoomInfo& info, uint64_t playerId)
{
	for (const auto& m : info.members)
		if (m.playerId == playerId)
			return &m;
	return nullptr;
}

} // namespace

// ───────────────── 캐릭터 ─────────────────

TEST_CASE(Room_SelectCharacter_BroadcastsToAll)
{
	RoomSetup r(2);
	ContentFixture::Post(r.players[1], Cmd::SelectCharacter{ 2 });
	const auto info = r.Info(0);	// 방장도 손님의 선택을 본다
	const auto* member = info ? MemberOf(*info, r.players[1]->PlayerId()) : nullptr;
	CHECK(member && member->characterId == 2);
}

TEST_CASE(Room_SelectCharacter_Taken)
{
	RoomSetup r(2);
	ContentFixture::Post(r.players[0], Cmd::SelectCharacter{ 1 });
	ContentFixture::Post(r.players[1], Cmd::SelectCharacter{ 1 });
	const auto error = ContentFixture::Last<Msg::RoomError>(r.players[1]);
	CHECK(error && error->request == RoomRequest::SelectCharacter && error->result == Result::CharacterTaken);
	CHECK(!ContentFixture::Last<Msg::RoomError>(r.players[0]));	// 실패는 요청자에게만
}

TEST_CASE(Room_SelectCharacter_OutOfRange_Invalid)
{
	RoomSetup r(1);
	ContentFixture::Post(r.players[0], Cmd::SelectCharacter{ 0 });
	ContentFixture::Post(r.players[0], Cmd::SelectCharacter{ 4 });	// 캐릭터는 도적1~3 (2026-11-22)
	const auto errors = r.players[0]->Sent<Msg::RoomError>();
	CHECK(errors.size() == 2 && errors[0].result == Result::CharacterInvalid && errors[1].result == Result::CharacterInvalid);
	ContentFixture::Post(r.players[0], Cmd::SelectCharacter{ 3 });
	CHECK_EQ(r.players[0]->Sent<Msg::RoomError>().size(), size_t{ 2 });	// 3은 고를 수 있음
}

// ───────────────── 퍽 ─────────────────

TEST_CASE(Room_SelectPerks_Rules)
{
	RoomSetup r(2);
	ContentFixture::Post(r.players[0], Cmd::SelectPerks{ { 1, 2, 3 } });
	ContentFixture::Post(r.players[1], Cmd::SelectPerks{ { 1, 2, 3 } });	// 다른 사람과 겹쳐도 된다
	const auto info = r.Info(0);
	const auto* a = info ? MemberOf(*info, r.players[0]->PlayerId()) : nullptr;
	const auto* b = info ? MemberOf(*info, r.players[1]->PlayerId()) : nullptr;
	CHECK(a && a->perks.size() == 3 && b && b->perks.size() == 3);

	ContentFixture::Post(r.players[0], Cmd::SelectPerks{ { 4, 4 } });		// 자기 안에서 중복
	ContentFixture::Post(r.players[0], Cmd::SelectPerks{ { 9 } });		// 없는 퍽
	const auto errors = r.players[0]->Sent<Msg::RoomError>();
	CHECK(errors.size() == 2 && errors[0].result == Result::PerkDuplicate && errors[1].result == Result::PerkInvalid);
}

TEST_CASE(Room_SelectPerks_EmptyIsAllowed)
{
	RoomSetup r(1);
	ContentFixture::Post(r.players[0], Cmd::SelectPerks{ { 1 } });
	ContentFixture::Post(r.players[0], Cmd::SelectPerks{ {} });
	const auto info = r.Info(0);
	CHECK(info && info->members[0].perks.empty());
}

// ───────────────── 준비·시작 ─────────────────

TEST_CASE(Room_Ready_WithoutCharacter_NotReadyable)
{
	RoomSetup r(1);
	ContentFixture::Post(r.players[0], Cmd::Ready{ true });
	const auto error = ContentFixture::Last<Msg::RoomError>(r.players[0]);
	CHECK(error && error->request == RoomRequest::Ready && error->result == Result::NotReadyable);
}

TEST_CASE(Room_ChangingCharacter_ClearsReady)
{
	RoomSetup r(2);
	ContentFixture::Post(r.players[1], Cmd::SelectCharacter{ 1 });
	ContentFixture::Post(r.players[1], Cmd::Ready{ true });
	ContentFixture::Post(r.players[1], Cmd::SelectCharacter{ 2 });
	const auto info = r.Info(0);
	const auto* member = info ? MemberOf(*info, r.players[1]->PlayerId()) : nullptr;
	CHECK(member && member->characterId == 2 && !member->ready);
}

TEST_CASE(Room_AllReady_StartsLoading)
{
	RoomSetup r(2);
	ContentFixture::Post(r.players[0], Cmd::SelectCharacter{ 1 });
	ContentFixture::Post(r.players[1], Cmd::SelectCharacter{ 2 });
	ContentFixture::Post(r.players[0], Cmd::Ready{ true });
	CHECK(r.Info(0) && r.Info(0)->state == RoomState::Waiting);	// 아직 한 명만 준비
	ContentFixture::Post(r.players[1], Cmd::Ready{ true });
	CHECK(r.Info(0) && r.Info(0)->state == RoomState::Loading);
	CHECK(r.Info(1) && r.Info(1)->state == RoomState::Loading);
}

TEST_CASE(Room_SinglePlayer_CanStart)
{
	RoomSetup r(1);		// 시연·시험용: 혼자서도 시작 가능
	ContentFixture::Post(r.players[0], Cmd::SelectCharacter{ 3 });
	ContentFixture::Post(r.players[0], Cmd::Ready{ true });
	CHECK(r.Info(0) && r.Info(0)->state == RoomState::Loading);
}

TEST_CASE(Room_Loading_HiddenFromListAndNotJoinable)
{
	RoomSetup r(1);
	auto late = r.f.LoggedIn("늦은손님");
	ContentFixture::Post(r.players[0], Cmd::SelectCharacter{ 1 });
	ContentFixture::Post(r.players[0], Cmd::Ready{ true });

	Content::Lobby().HandleRoomList(late->PlayerId(), {});
	Content::Lobby().HandleJoinRoom(late->PlayerId(), { 1 });
	r.f.Drain();
	const auto list = ContentFixture::Last<Msg::RoomList>(late);
	CHECK(list && list->rooms.empty());
	const auto join = ContentFixture::Last<Msg::JoinRoomResult>(late);
	CHECK(join && join->result == Result::RoomNotWaiting);
	CHECK(late->Phase() == ClientPhase::Lobby);
}

TEST_CASE(Room_Loading_ChangesRejected)
{
	RoomSetup r(1);
	ContentFixture::Post(r.players[0], Cmd::SelectCharacter{ 1 });
	ContentFixture::Post(r.players[0], Cmd::Ready{ true });
	ContentFixture::Post(r.players[0], Cmd::SelectCharacter{ 2 });
	const auto error = ContentFixture::Last<Msg::RoomError>(r.players[0]);
	CHECK(error && error->result == Result::RoomNotWaiting);
}

// ───────────────── 나가기·방장 ─────────────────

TEST_CASE(Room_Leave_BackToLobby_OthersNotified)
{
	RoomSetup r(2);
	ContentFixture::Post(r.players[1], Cmd::LeaveRoom{});

	const auto left = ContentFixture::Last<Msg::LeftRoom>(r.players[1]);
	CHECK(left.has_value());
	CHECK(r.players[1]->Phase() == ClientPhase::Lobby);
	CHECK(r.players[1]->Room() == nullptr);		// 방 입구가 끊겼다
	const auto info = r.Info(0);
	CHECK(info && info->members.size() == 1);
}

TEST_CASE(Room_Leave_ThenCreateImmediately_Allowed)
{
	// 방이 로비에 "나갔다"를 먼저 알리고 SC_LEAVE_ROOM을 보내므로, 곧바로 방을 만들어도 AlreadyInRoom이 아니다
	RoomSetup r(2);
	ContentFixture::Post(r.players[1], Cmd::LeaveRoom{});
	Content::Lobby().HandleCreateRoom(r.players[1]->PlayerId(), { "새 방" });
	r.f.Drain();
	const auto result = ContentFixture::Last<Msg::CreateRoomResult>(r.players[1]);
	CHECK(result && result->result == Result::Ok && result->roomId == 2);
}

TEST_CASE(Room_HostLeaves_NextMemberBecomesHost)
{
	RoomSetup r(3);
	ContentFixture::Post(r.players[0], Cmd::LeaveRoom{});
	const auto info = r.Info(1);
	CHECK(info && info->hostPlayerId == r.players[1]->PlayerId() && info->members.size() == 2);

	// 로비의 방 목록에도 새 방장 이름이 보인다
	auto viewer = r.f.LoggedIn("구경꾼");
	Content::Lobby().HandleRoomList(viewer->PlayerId(), {});
	r.f.Drain();
	const auto list = ContentFixture::Last<Msg::RoomList>(viewer);
	CHECK(list && list->rooms.size() == 1 && list->rooms[0].hostName == "p1" && list->rooms[0].playerCount == 2);
}

TEST_CASE(Room_LeaveDuringLoading_Rejected)
{
	// P3부터: 전원 준비 → Loading이 되면 클라 단계가 Game이라 브리지가 CS_LEAVE_ROOM을 막는다.
	// 방도 한 번 더 막는다(로딩·게임 중 이탈은 접속 종료로만 → GamePlayTests).
	RoomSetup r(2);
	ContentFixture::Post(r.players[0], Cmd::SelectCharacter{ 1 });
	ContentFixture::Post(r.players[1], Cmd::SelectCharacter{ 2 });
	ContentFixture::Post(r.players[0], Cmd::Ready{ true });
	ContentFixture::Post(r.players[1], Cmd::Ready{ true });
	ContentFixture::Post(r.players[1], Cmd::LeaveRoom{});
	const auto error = ContentFixture::Last<Msg::RoomError>(r.players[1]);
	CHECK(error && error->request == RoomRequest::LeaveRoom && error->result == Result::RoomNotWaiting);
	CHECK(r.players[1]->Phase() == ClientPhase::Game);
}

TEST_CASE(Room_CommandFromNonMember_Ignored)
{
	RoomSetup r(1);
	auto room = r.players[0]->Room();
	CHECK(room != nullptr);
	if (room)
		room->Post(9999, Cmd::Ready{ true });	// 멤버가 아닌 ID (오래된 방에 늦게 온 명령)
	r.f.Drain();
	CHECK(!ContentFixture::Last<Msg::RoomError>(r.players[0]));
}
