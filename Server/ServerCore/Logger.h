#pragma once

// 비동기 로거.
//
//   LOG_INFO("접속 #{} {}", id, addr);
//
// 호출한 스레드(IOCP Worker 등)는 문자열을 만들어 큐에 넣기만 하고 바로 돌아간다.
// 전용 Logger 스레드가 큐를 통째로 가져가(swap) 시각·레벨·스레드 이름을 붙여 콘솔과 파일에 쓴다.
// → Worker가 느린 콘솔·디스크 출력을 기다리지 않는다.
//
// Start 전과 Stop 후에는 호출한 스레드가 직접 쓴다(동기). 그래서 초기화·종료 중 로그도 사라지지 않는다.

#include <cstdint>
#include <format>
#include <string>
#include <utility>

namespace ServerCore {

enum class LogLevel : uint8_t { Debug, Info, Warn, Error };

struct LogOptions
{
	bool console = true;				// 콘솔 출력 (Warn 이상은 stderr)
	bool file = true;					// logs/<앱이름>_<날짜시각>.log
	bool async = true;					// false면 호출 스레드가 직접 쓴다(비교·디버깅용)
	LogLevel minLevel = LogLevel::Debug;
	size_t maxQueued = 100000;			// 큐가 이만큼 쌓이면 Error 외에는 버린다(Worker를 막지 않기 위해)
};

namespace Log {

bool Start(const char* appName, const LogOptions& options = {});
void Stop();						// 쌓인 로그를 모두 쓰고 Logger 스레드를 끝낸다
void Flush();						// 지금까지 넣은 로그가 다 쓰일 때까지 기다린다
void SetAsync(bool async);			// 실행 중 동기/비동기 전환(측정용). 전환 전에 Flush한다

bool IsEnabled(LogLevel level);
void Submit(LogLevel level, std::string&& message);

struct Stats { uint64_t written; uint64_t dropped; uint64_t maxQueueDepth; };
Stats GetStats();

template <class... Args>
void Write(LogLevel level, std::format_string<Args...> fmt, Args&&... args)
{
	// 걸러지는 레벨이면 문자열을 만드는 비용조차 쓰지 않는다
	if (!IsEnabled(level))
		return;
	Submit(level, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace Log
} // namespace ServerCore

#define LOG_DEBUG(...) ::ServerCore::Log::Write(::ServerCore::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::ServerCore::Log::Write(::ServerCore::LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...)  ::ServerCore::Log::Write(::ServerCore::LogLevel::Warn, __VA_ARGS__)
#define LOG_ERROR(...) ::ServerCore::Log::Write(::ServerCore::LogLevel::Error, __VA_ARGS__)
