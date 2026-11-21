#pragma once

// 아주 작은 시험 틀. 외부 라이브러리(GoogleTest 등) 없이 매크로 셋으로 충분하다.
//
//   TEST_CASE(이름) { ... CHECK(조건); CHECK_EQ(값, 기대값); }
//
// TEST_CASE는 정적 객체의 생성자로 자기 자신을 목록에 등록한다(main이 목록을 몰라도 된다).
// 실패해도 그 시험을 멈추지 않고 계속 검사한다(한 번에 실패를 모두 보기 위해).

#include <format>
#include <string>
#include <vector>

namespace Test {

using TestFn = void (*)();

struct Case
{
	const char* name;
	TestFn fn;
};

std::vector<Case>& Registry();

struct Registrar
{
	Registrar(const char* name, TestFn fn) { Registry().push_back({ name, fn }); }
};

void Fail(const char* file, int line, const std::string& what);

// filter가 비어 있지 않으면 이름에 filter가 들어간 시험만. 실패한 시험 수를 돌려준다.
int RunAll(const std::string& filter);

} // namespace Test

#define TEST_CASE(name)                                          \
	static void name();                                          \
	static const Test::Registrar name##_registrar(#name, name); \
	static void name()

#define CHECK(cond)                                         \
	do                                                      \
	{                                                       \
		if (!(cond))                                        \
			Test::Fail(__FILE__, __LINE__, #cond);          \
	} while (0)

// 실패하면 양쪽 값을 함께 보여 준다 (std::format으로 출력할 수 있는 타입만)
#define CHECK_EQ(actual, expected)                                                               \
	do                                                                                           \
	{                                                                                            \
		const auto& vhActual = (actual);                                                         \
		const auto& vhExpected = (expected);                                                     \
		if (!(vhActual == vhExpected))                                                           \
			Test::Fail(__FILE__, __LINE__,                                                       \
				std::format("{} == {}  (실제 {}, 기대 {})", #actual, #expected, vhActual, vhExpected)); \
	} while (0)
