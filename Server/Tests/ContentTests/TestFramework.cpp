#include "ServerCore/CorePch.h"
#include "TestFramework.h"

#include <chrono>

namespace Test {

namespace {
int g_failuresInCase = 0;
}

std::vector<Case>& Registry()
{
	// 함수 안 정적 변수: 다른 파일의 정적 Registrar가 먼저 불려도 그 시점에 만들어진다
	// (전역 vector였다면 "정적 초기화 순서 문제"로 빈 vector에 push할 수 있다).
	static std::vector<Case> cases;
	return cases;
}

void Fail(const char* file, int line, const std::string& what)
{
	++g_failuresInCase;
	// __FILE__에서 파일 이름만. std::filesystem::path를 쓰지 않는다: 경로에 한글이 있으면(/utf-8로 빌드한
	// UTF-8 바이트) ANSI 코드 페이지 변환에서 예외가 나서, 실패를 보고하려다 프로그램이 죽는다(실제로 겪음).
	const char* name = file;
	for (const char* p = file; *p != '\0'; ++p)
		if (*p == '\\' || *p == '/')
			name = p + 1;
	std::printf("      실패 %s(%d): %s\n", name, line, what.c_str());
}

int RunAll(const std::string& filter)
{
	int failedCases = 0;
	int ran = 0;
	for (const Case& c : Registry())
	{
		if (!filter.empty() && std::string_view(c.name).find(filter) == std::string_view::npos)
			continue;
		++ran;
		g_failuresInCase = 0;
		const auto start = std::chrono::steady_clock::now();
		c.fn();
		const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		std::printf("  %s %-52s %7.1fms\n", g_failuresInCase == 0 ? "[통과]" : "[실패]", c.name, ms);
		failedCases += g_failuresInCase == 0 ? 0 : 1;
	}
	std::printf("[ContentTests] 시험 %d개: 통과 %d / 실패 %d → %s\n", ran, ran - failedCases, failedCases,
		failedCases == 0 ? "성공" : "실패");
	return failedCases;
}

} // namespace Test
