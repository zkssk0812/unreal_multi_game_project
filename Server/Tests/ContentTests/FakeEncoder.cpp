#include "ServerCore/CorePch.h"
#include "FakeEncoder.h"

#include "ServerCore/SendBuffer.h"

Content::OutMessage FakeEncoder::Record(Recorded recorded)
{
	uint32 index = 0;
	{
		std::lock_guard lock(_lock);
		index = static_cast<uint32>(_messages.size());
		_messages.push_back(std::move(recorded));
	}
	// 진짜 송신 버퍼에 번호만 담는다. 그래서 OutMessage의 수명·공유 규칙은 실제 서버와 같다.
	return MakeSendBuffer(&index, sizeof(index));
}

std::optional<Recorded> FakeEncoder::Find(const Content::OutMessage& message) const
{
	if (!message || message->WriteSize() != sizeof(uint32))
		return std::nullopt;
	uint32 index = 0;
	std::memcpy(&index, message->Buffer(), sizeof(index));

	std::lock_guard lock(_lock);
	if (index >= _messages.size())
		return std::nullopt;
	return _messages[index];
}

size_t FakeEncoder::Count() const
{
	std::lock_guard lock(_lock);
	return _messages.size();
}
