#pragma once

// 시험 하나마다 콘텐츠를 새로 띄우고 정리한다 (서버의 main.cpp가 하는 계층 조립의 시험판).
//
//   ContentFixture f;                 // Content::Initialize(FakeEncoder) → 새 로비
//   auto ch = f.NewChannel();         // GameSession 대신 FakeChannel
//   Content::Lobby().HandleLogin(ch, ...);
//   f.Drain();                        // 넘겨진 Job까지 모두 실행
//
// 왜 Drain이 필요한가: JobQueue는 비어 있는 큐에 Job을 넣은 스레드가 바로 실행하지만, 그 스레드의
// 시간 예산만큼만 한다. Worker가 아닌 스레드(시험의 main)는 예산이 0이라 Job 1개만 하고 나머지는
// GlobalQueue로 넘긴다. 서버에서는 Worker가 GlobalQueue를 비우고, 시험에서는 Drain이 그 역할을 한다.

#include "ServerCore/CoreClock.h"
#include "ServerCore/CoreGlobal.h"
#include "ServerCore/JobQueue.h"
#include "ServerCore/JobTimer.h"
#include "Content/ContentGlobal.h"
#include "Content/Interface/LobbyApi.h"
#include "Content/Interface/RecordStore.h"
#include "FakeChannel.h"
#include "FakeEncoder.h"

class ContentFixture
{
public:
	explicit ContentFixture(Content::IRecordStore* records = nullptr) { Content::Initialize(encoder, records); }
	~ContentFixture()
	{
		Drain();
		GJobTimer->Clear();		// 이 시험의 방이 남긴 예약(틱·결과 타이머)이 다음 시험에서 실행되지 않게
		Content::Shutdown();
	}

	ContentFixture(const ContentFixture&) = delete;
	ContentFixture& operator=(const ContentFixture&) = delete;

	std::shared_ptr<FakeChannel> NewChannel() { return std::make_shared<FakeChannel>(++_nextChannelId, encoder); }

	// 로그인까지 마친 채널 (P2 시험용)
	std::shared_ptr<FakeChannel> LoggedIn(const std::string& name)
	{
		auto channel = NewChannel();
		Content::Lobby().HandleLogin(channel, { name });
		Drain();
		return channel;
	}

	// 브리지의 PostToRoom이 하는 일: 채널에 연결된 방에 명령을 넣는다
	static void Post(const std::shared_ptr<FakeChannel>& channel, Content::RoomCmd command)
	{
		if (auto room = channel->Room())
			room->Post(channel->PlayerId(), std::move(command));
		Drain();
	}

	// 브리지의 GameSession::OnDisconnected가 하는 일
	static void Disconnect(const std::shared_ptr<FakeChannel>& channel)
	{
		Content::Lobby().OnChannelClosed(channel->ChannelId(), channel->PlayerId());
		Drain();
	}

	// 서버의 Worker 루프를 흉내 낸다: 시간이 된 예약(JobTimer)을 꺼내 넣고 → GlobalQueue를 비운다.
	// pred가 참이 되거나 timeoutMs가 지날 때까지 1ms마다 반복. 틱·로딩 제한·결과 타이머 시험에 쓴다(P3).
	template <class Pred>
	static bool PumpUntil(Pred pred, uint32_t timeoutMs = 3000)
	{
		const uint64_t deadline = ServerCore::NowMs() + timeoutMs;
		while (ServerCore::NowMs() < deadline)
		{
			GJobTimer->Distribute(ServerCore::NowMs());
			Drain();
			if (pred())
				return true;
			::Sleep(1);
		}
		return pred();
	}

	// 채널이 받은 T 타입 메시지 중 마지막 것
	template <class T>
	static std::optional<T> Last(const std::shared_ptr<FakeChannel>& channel)
	{
		const auto sent = channel->Sent<T>();
		if (sent.empty())
			return std::nullopt;
		return sent.back();
	}

	// GlobalQueue에 넘어간 JobQueue를 모두 실행한다(이 스레드가 Worker 역할)
	static void Drain()
	{
		while (GGlobalQueue->Size() > 0)
			GGlobalQueue->Execute(ServerCore::NowMs() + 100);
	}

	FakeEncoder encoder;

private:
	uint64_t _nextChannelId = 0;
};
