#include "ServerCore/CorePch.h"

#include "ServerCore/SendBuffer.h"
#include "ContentFixture.h"
#include "TestFramework.h"

// 시험 도구 자체의 시험. 가짜가 틀리면 그 위의 모든 시험 결과를 믿을 수 없다.

TEST_CASE(Framework_EmptyTestRuns)
{
	// 완료 기준 "빈 테스트 실행": 검사가 하나도 없는 시험도 등록·실행·통과한다.
}

TEST_CASE(FakeEncoder_FindsOriginalMessage)
{
	FakeEncoder encoder;
	const Content::OutMessage first = encoder.Encode(Content::Msg::Notice{ "점검 10분 전" });
	Content::Msg::LoginResult login;
	login.playerId = 42;
	const Content::OutMessage second = encoder.Encode(login);

	CHECK_EQ(encoder.Count(), size_t{ 2 });
	const auto notice = encoder.Find(first);
	CHECK(notice && std::holds_alternative<Content::Msg::Notice>(notice->message));
	if (notice)
		CHECK_EQ(std::get<Content::Msg::Notice>(notice->message).message, std::string("점검 10분 전"));
	const auto result = encoder.Find(second);
	CHECK(result && std::get<Content::Msg::LoginResult>(result->message).playerId == 42);

	// 이 인코더가 만들지 않은 버퍼는 찾지 못해야 한다
	const char bytes[3] = { 1, 2, 3 };
	CHECK(!encoder.Find(MakeSendBuffer(bytes, sizeof(bytes))));
}

TEST_CASE(FakeChannel_RecordsCallsInOrder)
{
	FakeEncoder encoder;
	FakeChannel channel(7, encoder);
	channel.BindPlayer(3);
	channel.SetPhase(Content::ClientPhase::Room);
	channel.Send(encoder.Encode(Content::Msg::Notice{ "hi" }));

	CHECK_EQ(channel.Trace(), std::string("BindPlayer(3) → SetPhase(Room) → Send(Notice)"));
	CHECK(channel.Phase() == Content::ClientPhase::Room);
	CHECK_EQ(channel.PlayerId(), uint64_t{ 3 });
	CHECK_EQ(channel.Sent<Content::Msg::Notice>().size(), size_t{ 1 });
}
