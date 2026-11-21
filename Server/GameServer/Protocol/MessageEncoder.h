#pragma once

// 브리지: 콘텐츠의 Msg::* → 패킷 바이트 (docs/DESIGN.md 2장).
// 함수마다 모양이 같다: PacketBuilder로 송신 버퍼 위에 패킷을 만들고 → 필드를 옮기고 → Finish.
// 선언은 VH_MESSAGES 목록(Content/Interface/MessageEncoder.h)으로 한 번에 만든다.

#include "Content/Interface/MessageEncoder.h"

class MessageEncoder final : public Content::IMessageEncoder
{
public:
#define VH_OVERRIDE_ENCODE(Type) Content::OutMessage Encode(const Content::Msg::Type& message) override;
	VH_MESSAGES(VH_OVERRIDE_ENCODE)
#undef VH_OVERRIDE_ENCODE
};

// UTF-8 문자열을 고정 길이 char 배열(capacity = 배열 크기, 널 포함)에 복사한다.
// 넘치면 글자 경계에서 자른다(멀티바이트 글자의 중간에서 끊지 않음). 잘랐으면 false.
bool CopyUtf8(char* dest, size_t capacity, const std::string& source);
