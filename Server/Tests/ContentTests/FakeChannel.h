#pragma once

// 시험용 채널.
// GameSession 대신 콘텐츠에 넘겨, 콘텐츠가 채널에 한 일(보내기·단계 변경·플레이어 연결·끊기)을
// 순서대로 기록한다. 시험은 이 기록으로 "무엇을, 어떤 순서로 했나"를 확인한다.

#include <mutex>
#include <string>
#include <vector>

#include "Content/Interface/ClientChannel.h"
#include "Content/Interface/RoomEndpoint.h"
#include "FakeEncoder.h"

struct ChannelEvent
{
	enum class Kind : uint8_t { Send, SetPhase, BindPlayer, BindRoom, Close } kind;
	uint64_t value = 0;				// SetPhase: 단계, BindPlayer: 플레이어 ID, Close: 이유
	std::optional<Recorded> message;	// Send: 보낸 Msg
};

class FakeChannel final : public Content::IClientChannel
{
public:
	FakeChannel(uint64_t id, const FakeEncoder& encoder) : _id(id), _encoder(encoder) {}

	// IClientChannel
	uint64_t ChannelId() const override { return _id; }
	void Send(const Content::OutMessage& message) override;
	void SetPhase(Content::ClientPhase phase) override;
	void BindPlayer(uint64_t playerId) override;
	void BindRoom(std::weak_ptr<Content::IRoomEndpoint> room) override;
	void Close(Content::CloseReason reason) override;

	// 시험에서 확인
	std::vector<ChannelEvent> Events() const;
	Content::ClientPhase Phase() const;
	uint64_t PlayerId() const;
	std::shared_ptr<Content::IRoomEndpoint> Room() const;	// BindRoom으로 받은 방 (없으면 nullptr)

	// 보낸 메시지 중 T 타입만 순서대로
	template <class T>
	std::vector<T> Sent() const
	{
		std::vector<T> result;
		for (const ChannelEvent& e : Events())
			if (e.kind == ChannelEvent::Kind::Send && e.message)
				if (const T* m = std::get_if<T>(&e.message->message))
					result.push_back(*m);
		return result;
	}

	// "BindPlayer(1) → SetPhase(Lobby) → Send(LoginResult)" 같은 한 줄 요약 (순서 확인·실패 출력용)
	std::string Trace() const;

private:
	void Add(ChannelEvent event);

	const uint64_t _id;
	const FakeEncoder& _encoder;
	mutable std::mutex _lock;
	std::vector<ChannelEvent> _events;
	Content::ClientPhase _phase = Content::ClientPhase::Anonymous;
	uint64_t _playerId = 0;
	std::weak_ptr<Content::IRoomEndpoint> _room;
};
