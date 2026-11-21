#include "ServerCore/CorePch.h"

#include <set>

#include "ServerCore/ThreadManager.h"
#include "Content/Interface/LobbyApi.h"
#include "ContentFixture.h"
#include "TestFramework.h"

// 로비: 로그인, 방 목록·생성·참여, 접속 종료. 브리지·패킷·IOCP 없이 Content::Lobby()만 쓴다.

using Content::ClientPhase;
using Content::Result;
namespace Msg = Content::Msg;
namespace Cmd = Content::Cmd;

// ───────────────── 로그인 ─────────────────

TEST_CASE(Login_Success_SetsPhaseBeforeSending)
{
	ContentFixture f;
	auto channel = f.NewChannel();
	Content::Lobby().HandleLogin(channel, { "금고털이" });
	f.Drain();

	// 규칙: 플레이어 묶기 → 단계 변경 → 응답 (응답이 먼저 가면 클라의 다음 패킷이 거절될 수 있음)
	CHECK_EQ(channel->Trace(), std::string("BindPlayer(1) → SetPhase(Lobby) → Send(LoginResult)"));
	const auto sent = ContentFixture::Last<Msg::LoginResult>(channel);
	CHECK(sent && sent->result == Result::Ok && sent->playerId == 1);
	CHECK(channel->Phase() == ClientPhase::Lobby);
}

TEST_CASE(Login_TrimmedName_IsReturned)
{
	ContentFixture f;
	auto channel = f.LoggedIn("  의적  ");
	const auto sent = ContentFixture::Last<Msg::LoginResult>(channel);
	CHECK(sent && sent->name == "의적");
}

TEST_CASE(Login_InvalidName_ReturnsToAnonymous)
{
	ContentFixture f;
	auto channel = f.LoggedIn("   ");
	CHECK_EQ(channel->Trace(), std::string("SetPhase(Anonymous) → Send(LoginResult)"));
	const auto sent = ContentFixture::Last<Msg::LoginResult>(channel);
	CHECK(sent && sent->result == Result::NameInvalid);
	CHECK_EQ(channel->PlayerId(), uint64_t{ 0 });	// 실패하면 플레이어를 묶지 않는다
}

TEST_CASE(Login_DuplicateName_NameInUse_IgnoringCase)
{
	ContentFixture f;
	auto first = f.LoggedIn("Kim");
	auto second = f.LoggedIn("kim");
	const auto sent = ContentFixture::Last<Msg::LoginResult>(second);
	CHECK(sent && sent->result == Result::NameInUse);
	CHECK(second->Phase() == ClientPhase::Anonymous);
	CHECK(first->Phase() == ClientPhase::Lobby);
}

TEST_CASE(Login_NameReleasedAfterDisconnect)
{
	ContentFixture f;
	auto first = f.LoggedIn("금고털이");
	ContentFixture::Disconnect(first);
	auto second = f.LoggedIn("금고털이");
	const auto sent = ContentFixture::Last<Msg::LoginResult>(second);
	CHECK(sent && sent->result == Result::Ok);
}

TEST_CASE(Login_ConcurrentCallers_UniquePlayerIds)
{
	// 8개 스레드가 동시에 HandleLogin을 부르고, 그 스레드들이 GlobalQueue도 함께 비운다(Worker 흉내).
	// 로비 상태에 락이 없어도 번호가 겹치거나 빠지지 않아야 한다(JobQueue가 한 번에 한 Job만 실행).
	constexpr int kThreads = 8;
	constexpr int kPerThread = 300;

	ContentFixture f;
	std::vector<std::shared_ptr<FakeChannel>> channels;
	for (int i = 0; i < kThreads * kPerThread; ++i)
		channels.push_back(f.NewChannel());
	{
		ThreadManager threads;
		for (int t = 0; t < kThreads; ++t)
		{
			threads.Launch(std::format("Login {}", t), [&channels, t] {
				for (int i = 0; i < kPerThread; ++i)
					Content::Lobby().HandleLogin(channels[static_cast<size_t>(t * kPerThread + i)], { std::format("p{}_{}", t, i) });
				ContentFixture::Drain();
			});
		}
		threads.JoinAll();
	}
	f.Drain();

	std::set<uint64_t> ids;
	for (const auto& channel : channels)
		if (const auto sent = ContentFixture::Last<Msg::LoginResult>(channel); sent && sent->result == Result::Ok)
			ids.insert(sent->playerId);
	CHECK_EQ(ids.size(), static_cast<size_t>(kThreads * kPerThread));
	CHECK(!ids.empty() && *ids.begin() == 1 && *ids.rbegin() == static_cast<uint64_t>(kThreads * kPerThread));
}

// ───────────────── 방 목록·생성·참여 ─────────────────

TEST_CASE(CreateRoom_EntersRoomAsHost)
{
	ContentFixture f;
	auto host = f.LoggedIn("방장");
	Content::Lobby().HandleCreateRoom(host->PlayerId(), { "금고 털러 갈 사람" });
	f.Drain();

	// 방 입구 연결 → 단계 Room → SC_CREATE_ROOM → SC_ROOM_STATE 순서
	CHECK_EQ(host->Trace(), std::string("BindPlayer(1) → SetPhase(Lobby) → Send(LoginResult) → BindRoom → SetPhase(Room) "
		"→ Send(CreateRoomResult) → Send(RoomInfo)"));
	const auto result = ContentFixture::Last<Msg::CreateRoomResult>(host);
	CHECK(result && result->result == Result::Ok && result->roomId == 1);
	const auto info = ContentFixture::Last<Msg::RoomInfo>(host);
	CHECK(info && info->members.size() == 1 && info->hostPlayerId == host->PlayerId());
	CHECK(host->Room() != nullptr);
}

TEST_CASE(CreateRoom_InvalidName_StaysInLobby)
{
	ContentFixture f;
	auto host = f.LoggedIn("방장");
	Content::Lobby().HandleCreateRoom(host->PlayerId(), { "   " });
	f.Drain();
	const auto result = ContentFixture::Last<Msg::CreateRoomResult>(host);
	CHECK(result && result->result == Result::RoomNameInvalid);
	CHECK(host->Phase() == ClientPhase::Lobby);
}

TEST_CASE(RoomList_ShowsWaitingRoomsWithHostName)
{
	ContentFixture f;
	auto host = f.LoggedIn("방장");
	auto guest = f.LoggedIn("손님");
	Content::Lobby().HandleCreateRoom(host->PlayerId(), { "첫 방" });
	f.Drain();
	Content::Lobby().HandleRoomList(guest->PlayerId(), {});
	f.Drain();

	const auto list = ContentFixture::Last<Msg::RoomList>(guest);
	CHECK(list && list->rooms.size() == 1);
	if (list && list->rooms.size() == 1)
	{
		CHECK_EQ(list->rooms[0].name, std::string("첫 방"));
		CHECK_EQ(list->rooms[0].hostName, std::string("방장"));
		CHECK_EQ(list->rooms[0].playerCount, uint8_t{ 1 });
	}
}

TEST_CASE(JoinRoom_BothSeeTwoMembers)
{
	ContentFixture f;
	auto host = f.LoggedIn("방장");
	auto guest = f.LoggedIn("손님");
	Content::Lobby().HandleCreateRoom(host->PlayerId(), { "방" });
	f.Drain();
	Content::Lobby().HandleJoinRoom(guest->PlayerId(), { 1 });
	f.Drain();

	const auto joined = ContentFixture::Last<Msg::JoinRoomResult>(guest);
	CHECK(joined && joined->result == Result::Ok && joined->roomId == 1);
	CHECK(guest->Phase() == ClientPhase::Room);
	const auto hostView = ContentFixture::Last<Msg::RoomInfo>(host);
	const auto guestView = ContentFixture::Last<Msg::RoomInfo>(guest);
	CHECK(hostView && hostView->members.size() == 2);
	CHECK(guestView && guestView->members.size() == 2 && guestView->hostPlayerId == host->PlayerId());
}

TEST_CASE(JoinRoom_FourthPlayer_RoomFull)
{
	ContentFixture f;
	auto a = f.LoggedIn("a");
	auto b = f.LoggedIn("b");
	auto c = f.LoggedIn("c");
	auto d = f.LoggedIn("d");
	Content::Lobby().HandleCreateRoom(a->PlayerId(), { "방" });
	Content::Lobby().HandleJoinRoom(b->PlayerId(), { 1 });
	Content::Lobby().HandleJoinRoom(c->PlayerId(), { 1 });
	Content::Lobby().HandleJoinRoom(d->PlayerId(), { 1 });
	f.Drain();

	const auto result = ContentFixture::Last<Msg::JoinRoomResult>(d);
	CHECK(result && result->result == Result::RoomFull);
	CHECK(d->Phase() == ClientPhase::Lobby);
	const auto info = ContentFixture::Last<Msg::RoomInfo>(a);
	CHECK(info && info->members.size() == 3);
}

TEST_CASE(JoinRoom_UnknownRoom_RoomNotFound)
{
	ContentFixture f;
	auto guest = f.LoggedIn("손님");
	Content::Lobby().HandleJoinRoom(guest->PlayerId(), { 99 });
	f.Drain();
	const auto result = ContentFixture::Last<Msg::JoinRoomResult>(guest);
	CHECK(result && result->result == Result::RoomNotFound);
}

TEST_CASE(CreateRoom_WhileInRoom_AlreadyInRoom)
{
	ContentFixture f;
	auto host = f.LoggedIn("방장");
	Content::Lobby().HandleCreateRoom(host->PlayerId(), { "하나" });
	Content::Lobby().HandleCreateRoom(host->PlayerId(), { "둘" });	// 브리지 단계 검사를 우회한 경우를 가정
	f.Drain();
	// 순서는 거절이 먼저다: 두 번째 요청은 로비가 바로 거절하고, 첫 번째의 성공 응답은 방이 나중에 보낸다
	const auto results = host->Sent<Msg::CreateRoomResult>();
	CHECK(results.size() == 2 && results[0].result == Result::AlreadyInRoom && results[1].result == Result::Ok);
}

TEST_CASE(Disconnect_InRoom_OthersNotified_NameReleased)
{
	ContentFixture f;
	auto host = f.LoggedIn("방장");
	auto guest = f.LoggedIn("손님");
	Content::Lobby().HandleCreateRoom(host->PlayerId(), { "방" });
	Content::Lobby().HandleJoinRoom(guest->PlayerId(), { 1 });
	f.Drain();

	ContentFixture::Disconnect(guest);
	const auto info = ContentFixture::Last<Msg::RoomInfo>(host);
	CHECK(info && info->members.size() == 1);

	auto again = f.LoggedIn("손님");		// 닉네임이 풀렸다
	const auto login = ContentFixture::Last<Msg::LoginResult>(again);
	CHECK(login && login->result == Result::Ok);
}

TEST_CASE(Disconnect_LastMember_RoomDeleted)
{
	ContentFixture f;
	auto host = f.LoggedIn("방장");
	auto other = f.LoggedIn("구경꾼");
	Content::Lobby().HandleCreateRoom(host->PlayerId(), { "방" });
	f.Drain();
	ContentFixture::Disconnect(host);

	Content::Lobby().HandleRoomList(other->PlayerId(), {});
	f.Drain();
	const auto list = ContentFixture::Last<Msg::RoomList>(other);
	CHECK(list && list->rooms.empty());
}

TEST_CASE(Disconnect_WhileLoginPending_IsCleanedUp)
{
	// 로그인 응답 전에 끊기면 세션은 플레이어 ID 0으로 OnChannelClosed를 부른다.
	// 로비는 채널 ID로 찾아서 정리해야 한다(안 그러면 닉네임이 영원히 묶임).
	ContentFixture f;
	auto channel = f.NewChannel();
	Content::Lobby().HandleLogin(channel, { "유령" });
	Content::Lobby().OnChannelClosed(channel->ChannelId(), 0);
	f.Drain();

	auto again = f.LoggedIn("유령");
	const auto login = ContentFixture::Last<Msg::LoginResult>(again);
	CHECK(login && login->result == Result::Ok);
}
