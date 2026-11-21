#include "ServerCore/CorePch.h"

#include "Content/Lobby/NameRule.h"
#include "TestFramework.h"

// 닉네임·방 이름 규칙. 상태 없는 함수라 픽스처 없이 바로 시험한다.

using Content::NameRule::Normalize;

TEST_CASE(NameRule_TrimsSpaces)
{
	const auto name = Normalize("  금고털이\t ", 12);
	CHECK(name.has_value());
	if (name)
		CHECK_EQ(*name, std::string("금고털이"));
}

TEST_CASE(NameRule_EmptyOrSpacesOnly_Invalid)
{
	CHECK(!Normalize("", 12));
	CHECK(!Normalize("   ", 12));
}

TEST_CASE(NameRule_CountsCharactersNotBytes)
{
	// 한글 12자 = 36바이트지만 12글자라 통과, 13글자는 거절
	CHECK(Normalize("가나다라마바사아자차카타", 12).has_value());
	CHECK(!Normalize("가나다라마바사아자차카타파", 12));
	CHECK(Normalize("abcdefghijkl", 12).has_value());
	CHECK(!Normalize("abcdefghijklm", 12));
}

TEST_CASE(NameRule_ControlCharacter_Invalid)
{
	CHECK(!Normalize("금고\n털이", 12));
	CHECK(!Normalize(std::string("ab") + '\x7F', 12));
}

TEST_CASE(NameRule_BrokenUtf8_Invalid)
{
	CHECK(!Normalize(std::string("ab") + '\x80', 12));				// 연속 바이트로 시작
	CHECK(!Normalize(std::string("ab") + '\xEA' + '\xB8', 12));		// 3바이트 글자가 2바이트에서 끝남
}

TEST_CASE(NameRule_CompareKey_IgnoresAsciiCase)
{
	CHECK_EQ(Content::NameRule::CompareKey("Kim금고"), std::string("kim금고"));
}
