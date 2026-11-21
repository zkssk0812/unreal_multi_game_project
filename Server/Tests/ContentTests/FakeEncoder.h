#pragma once

// 시험용 인코더.
// 진짜 MessageEncoder는 Msg를 패킷 바이트로 만든다. 시험에서는 바이트가 아니라 "콘텐츠가 무엇을 보냈나"가
// 궁금하므로, Msg를 그대로 목록에 보관하고 OutMessage에는 그 목록의 번호(4바이트)만 담아 돌려준다.
// FakeChannel은 Send로 받은 OutMessage에서 번호를 꺼내 원래 Msg를 찾는다.
//
// 콘텐츠는 OutMessage의 내용을 보지 않고 Send에 넘기기만 하므로 이런 바꿔치기가 가능하다.
// 메시지 목록은 VH_MESSAGES 하나뿐이다: 거기 한 줄을 넣으면 여기(variant와 Encode)도 저절로 늘어난다.

#include <mutex>
#include <optional>
#include <variant>
#include <vector>

#include "Content/Interface/MessageEncoder.h"

#define VH_VARIANT_ITEM(Type) , Content::Msg::Type
using RecordedMsg = std::variant<std::monostate VH_MESSAGES(VH_VARIANT_ITEM)>;
#undef VH_VARIANT_ITEM

struct Recorded
{
	RecordedMsg message;
	const char* name;		// "LoginResult" 같은 타입 이름 (Trace 출력용)
};

class FakeEncoder final : public Content::IMessageEncoder
{
public:
	// Encode 하나 = 한 줄: 받은 Msg를 이름과 함께 기록한다
#define VH_FAKE_ENCODE(Type) \
	Content::OutMessage Encode(const Content::Msg::Type& message) override { return Record({ message, #Type }); }
	VH_MESSAGES(VH_FAKE_ENCODE)
#undef VH_FAKE_ENCODE

	// OutMessage → 원래 Msg. 이 인코더가 만든 것이 아니면 nullopt.
	std::optional<Recorded> Find(const Content::OutMessage& message) const;
	size_t Count() const;

private:
	Content::OutMessage Record(Recorded recorded);

	mutable std::mutex _lock;	// 콘텐츠 스트랜드가 여러 스레드에서 돌 수 있다(멀티스레드 시험)
	std::vector<Recorded> _messages;
};
