#include "ServerCore/CorePch.h"
#include "Content/Lobby/NameRule.h"

namespace Content::NameRule {

namespace {

bool IsSpace(unsigned char c) { return c == ' ' || c == '\t'; }

// UTF-8 한 글자의 바이트 수. 첫 바이트가 올바르지 않으면 0.
//   0xxxxxxx → 1, 110xxxxx → 2, 1110xxxx → 3, 11110xxx → 4
size_t CharLength(unsigned char first)
{
	if (first < 0x80) return 1;
	if ((first & 0xE0) == 0xC0) return 2;
	if ((first & 0xF0) == 0xE0) return 3;
	if ((first & 0xF8) == 0xF0) return 4;
	return 0;	// 10xxxxxx(연속 바이트)가 글자의 시작에 오면 깨진 문자열
}

} // namespace

std::optional<std::string> Normalize(const std::string& input, size_t maxChars)
{
	// 1) 앞뒤 공백 제거
	size_t begin = 0;
	size_t end = input.size();
	while (begin < end && IsSpace(static_cast<unsigned char>(input[begin])))
		++begin;
	while (end > begin && IsSpace(static_cast<unsigned char>(input[end - 1])))
		--end;
	if (begin == end)
		return std::nullopt;	// 비었거나 공백뿐

	// 2)~4) 글자 단위로 훑으며 검사
	size_t chars = 0;
	for (size_t i = begin; i < end;)
	{
		const unsigned char first = static_cast<unsigned char>(input[i]);
		const size_t len = CharLength(first);
		if (len == 0 || i + len > end)
			return std::nullopt;	// 깨진 UTF-8
		for (size_t k = 1; k < len; ++k)
			if ((static_cast<unsigned char>(input[i + k]) & 0xC0) != 0x80)
				return std::nullopt;	// 이어져야 할 바이트가 연속 바이트가 아님
		if (len == 1 && (first < 0x20 || first == 0x7F))
			return std::nullopt;	// 제어 문자 (줄바꿈 등)
		i += len;
		if (++chars > maxChars)
			return std::nullopt;
	}
	return input.substr(begin, end - begin);
}

std::string CompareKey(const std::string& normalized)
{
	std::string key = normalized;
	for (char& c : key)
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');	// 영문만 소문자로. 한글 바이트(0x80 이상)는 그대로
	return key;
}

} // namespace Content::NameRule
