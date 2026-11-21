#include "ServerCore/CorePch.h"
#include "FakeChannel.h"

namespace {

const char* PhaseText(uint64_t phase)
{
	static const char* names[] = { "Anonymous", "Authenticating", "Lobby", "Room", "Game" };
	return phase < std::size(names) ? names[phase] : "?";
}

} // namespace

void FakeChannel::Add(ChannelEvent event)
{
	std::lock_guard lock(_lock);
	if (event.kind == ChannelEvent::Kind::SetPhase)
		_phase = static_cast<Content::ClientPhase>(event.value);
	else if (event.kind == ChannelEvent::Kind::BindPlayer)
		_playerId = event.value;
	_events.push_back(std::move(event));
}

void FakeChannel::Send(const Content::OutMessage& message)
{
	Add({ ChannelEvent::Kind::Send, 0, _encoder.Find(message) });
}

void FakeChannel::SetPhase(Content::ClientPhase phase)
{
	Add({ ChannelEvent::Kind::SetPhase, static_cast<uint64_t>(phase), std::nullopt });
}

void FakeChannel::BindPlayer(uint64_t playerId)
{
	Add({ ChannelEvent::Kind::BindPlayer, playerId, std::nullopt });
}

void FakeChannel::BindRoom(std::weak_ptr<Content::IRoomEndpoint> room)
{
	const bool bound = !room.expired();
	{
		std::lock_guard lock(_lock);
		_room = std::move(room);
	}
	Add({ ChannelEvent::Kind::BindRoom, bound ? 1u : 0u, std::nullopt });	// 1 = 방 연결, 0 = 방 끊기
}

std::shared_ptr<Content::IRoomEndpoint> FakeChannel::Room() const
{
	std::lock_guard lock(_lock);
	return _room.lock();
}

void FakeChannel::Close(Content::CloseReason reason)
{
	Add({ ChannelEvent::Kind::Close, static_cast<uint64_t>(reason), std::nullopt });
}

std::vector<ChannelEvent> FakeChannel::Events() const
{
	std::lock_guard lock(_lock);
	return _events;
}

Content::ClientPhase FakeChannel::Phase() const
{
	std::lock_guard lock(_lock);
	return _phase;
}

uint64_t FakeChannel::PlayerId() const
{
	std::lock_guard lock(_lock);
	return _playerId;
}

std::string FakeChannel::Trace() const
{
	std::string out;
	for (const ChannelEvent& e : Events())
	{
		if (!out.empty())
			out += " → ";
		switch (e.kind)
		{
		case ChannelEvent::Kind::Send:        out += std::format("Send({})", e.message ? e.message->name : "?"); break;
		case ChannelEvent::Kind::SetPhase:    out += std::format("SetPhase({})", PhaseText(e.value)); break;
		case ChannelEvent::Kind::BindPlayer: out += std::format("BindPlayer({})", e.value); break;
		case ChannelEvent::Kind::BindRoom:    out += e.value ? "BindRoom" : "UnbindRoom"; break;
		case ChannelEvent::Kind::Close:       out += std::format("Close({})", e.value); break;
		}
	}
	return out;
}
