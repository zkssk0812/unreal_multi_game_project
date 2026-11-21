#include "ServerCore/CorePch.h"

#include <map>

#include "Content/Interface/LobbyApi.h"
#include "ContentFixture.h"
#include "TestFramework.h"
#include "WorldTestUtil.h"

// 종료·결과·최고 기록 (psd.md M7): 순위(동점 같은 순위), 최고 기록 갱신 여부, 저장 요청

using namespace WorldTest;
namespace Cmd = Content::Cmd;

namespace {

// 가짜 기록 저장소: 메모리의 표. 실제 SQL Server처럼 "더 높을 때만 갱신"
class FakeRecordStore final : public IRecordStore
{
public:
	std::map<std::string, uint32_t> table;
	std::vector<std::pair<std::string, uint32_t>> saved;
	bool online = true;

	void LoadBest(std::vector<std::string> nicknames, std::function<void(bool, BestMap)> done) override
	{
		BestMap best;
		for (const auto& name : nicknames)
			if (auto it = table.find(name); it != table.end())
				best[name] = it->second;
		done(online, online ? best : BestMap{});
	}
	void SaveScore(std::string nickname, uint16_t, uint32_t score) override
	{
		saved.emplace_back(nickname, score);
		uint32_t& best = table[nickname];
		best = std::max(best, score);
	}
};

struct ShortTiming
{
	GameData::Timing saved = GameData::CurrentTiming();
	ShortTiming() { GameData::CurrentTiming() = GameData::Timing{ 50, 300, 300, 300 }; }
	~ShortTiming() { GameData::CurrentTiming() = saved; }
};

// 방 하나에서 두 사람(alice, bob)이 짧은 한 판을 한다 (아무것도 반납하지 않음)
std::optional<Msg::GameEnd> PlayOneGame(FakeRecordStore& store)
{
	ShortTiming timing;
	ContentFixture f(&store);
	auto a = f.LoggedIn("alice");
	auto b = f.LoggedIn("bob");
	Content::Lobby().HandleCreateRoom(a->PlayerId(), { "방" });
	Content::Lobby().HandleJoinRoom(b->PlayerId(), { 1 });
	f.Drain();
	ContentFixture::Post(a, Cmd::SelectCharacter{ 1 });
	ContentFixture::Post(b, Cmd::SelectCharacter{ 2 });
	ContentFixture::Post(a, Cmd::Ready{ true });
	ContentFixture::Post(b, Cmd::Ready{ true });
	ContentFixture::Post(a, Cmd::LoadComplete{});
	ContentFixture::Post(b, Cmd::LoadComplete{});
	ContentFixture::PumpUntil([&] { return ContentFixture::Last<Msg::GameEnd>(a).has_value(); });
	return ContentFixture::Last<Msg::GameEnd>(a);
}

} // namespace

TEST_CASE(Result_Rank_ByBanked_TiesShareRank)
{
	GameWorld world = MakeWorld(3);
	world.ThiefOf(A)->banked = 500;
	world.ThiefOf(B)->banked = 900;
	world.ThiefOf(C)->banked = 500;
	const auto results = world.MakeResults();
	CHECK(results.size() == 3);
	CHECK_EQ(results[1].rank, uint8_t{ 1 });
	CHECK_EQ(results[0].rank, uint8_t{ 2 });
	CHECK_EQ(results[2].rank, uint8_t{ 2 });		// 같은 금액은 같은 순위 (A-20)
	CHECK_EQ(results[0].name, std::string("p0"));
}

TEST_CASE(Result_FirstGame_NewRecord_ScoresSaved)
{
	FakeRecordStore store;
	const auto end = PlayOneGame(store);
	CHECK(end && end->reason == EndReason::TimeUp && end->players.size() == 2);
	if (!end)
		return;
	CHECK(end->players[0].name == "alice" && end->players[0].rank == 1);	// 0 : 0 → 둘 다 1등
	CHECK(!end->players[0].newRecord);						// 0점은 갱신이 아님
	CHECK_EQ(store.saved.size(), size_t{ 2 });				// 판이 끝나면 점수를 저장 요청 (F-08)
}

TEST_CASE(Result_PreviousRecordHigher_NotNew)
{
	FakeRecordStore store;
	store.table["alice"] = 5000;
	const auto end = PlayOneGame(store);
	CHECK(end.has_value());
	if (end)
		CHECK(!end->players[0].newRecord && end->players[0].bestRecord == 5000);
}

TEST_CASE(Result_DbOffline_GameStillEnds)
{
	FakeRecordStore store;
	store.online = false;
	const auto end = PlayOneGame(store);
	CHECK(end && !end->players[0].newRecord);				// DB가 없어도 게임은 끝난다 (A-21)
}
