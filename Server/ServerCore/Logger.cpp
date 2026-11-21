#include "CorePch.h"
#include "Logger.h"
#include "CoreTLS.h"
#include "ThreadManager.h"

#include <chrono>
#include <condition_variable>
#include <ctime>

namespace ServerCore::Log {

namespace {

using Clock = std::chrono::system_clock;	// 로그 시각은 사람이 읽는 벽시계 (게임 시각은 steady_clock)

// 큐에 들어가는 한 줄. 호출 스레드에서는 "재료"만 담고, 문자열 조립은 Logger 스레드가 한다.
struct Entry
{
	LogLevel level;
	uint32_t threadId;
	char threadName[ThreadNameSize];	// 포인터가 아니라 복사: 스레드가 먼저 끝나도 안전
	Clock::time_point time;
	std::string text;
};

struct State
{
	// 큐
	std::mutex queueLock;
	std::condition_variable wakeLogger;		// 새 로그가 들어옴 / 종료 요청
	std::condition_variable flushed;		// 큐가 비고 출력도 끝남 (Flush 대기용)
	std::vector<Entry> pending;
	bool running = false;					// 큐가 로그를 받는 중
	bool stopRequested = false;
	bool busy = false;						// Logger 스레드가 꺼내 간 묶음을 쓰는 중
	uint64_t maxDepth = 0;

	std::thread thread;
	std::atomic<bool> async{ false };
	std::atomic<LogLevel> minLevel{ LogLevel::Debug };

	// 출력 (Logger 스레드와 동기 경로가 함께 쓰므로 락)
	std::mutex outputLock;
	FILE* file = nullptr;
	LogOptions options;

	std::atomic<uint64_t> written{ 0 };
	std::atomic<uint64_t> dropped{ 0 };
};

State& S()
{
	static State state;
	return state;
}

Entry MakeEntry(LogLevel level, std::string&& text)
{
	Entry e;
	e.level = level;
	e.threadId = tls_threadId;
	std::memcpy(e.threadName, tls_threadName, ThreadNameSize);
	e.time = Clock::now();
	e.text = std::move(text);
	return e;
}

std::string FormatLine(const Entry& e)
{
	static constexpr const char* kTags[] = { "DEBUG", "INFO ", "WARN ", "ERROR" };

	const std::time_t seconds = Clock::to_time_t(e.time);
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(e.time.time_since_epoch()).count() % 1000;
	std::tm local{};
	::localtime_s(&local, &seconds);

	const char* name = e.threadName[0] != '\0' ? e.threadName : "?";
	return std::format("{:02}:{:02}:{:02}.{:03} [{}][{}] {}\n",
		local.tm_hour, local.tm_min, local.tm_sec, ms, kTags[static_cast<int>(e.level)], name, e.text);
}

// outputLock을 잡은 상태에서 호출
void Output(const Entry& e)
{
	State& s = S();
	const std::string line = FormatLine(e);
	if (s.options.console)
		std::fputs(line.c_str(), e.level >= LogLevel::Warn ? stderr : stdout);
	if (s.file != nullptr)
		std::fputs(line.c_str(), s.file);
}

void Run()
{
	ThreadManager::InitTLS("Logger");
	State& s = S();
	std::vector<Entry> batch;

	while (true)
	{
		bool stopping = false;
		{
			std::unique_lock lock(s.queueLock);
			// 100ms마다 한 번은 깨어난다(신호를 놓쳐도 오래 멈추지 않게)
			s.wakeLogger.wait_for(lock, std::chrono::milliseconds(100),
				[&] { return s.stopRequested || !s.pending.empty(); });
			batch.swap(s.pending);		// 쌓인 것을 통째로 가져간다. 락 구간은 포인터 교환뿐
			stopping = s.stopRequested;
			s.busy = !batch.empty();
		}

		if (!batch.empty())
		{
			std::lock_guard out(s.outputLock);
			for (const Entry& e : batch)
				Output(e);
			std::fflush(stdout);
			if (s.file != nullptr)
				std::fflush(s.file);	// 묶음마다 한 번: 서버가 죽어도 직전 묶음까지는 디스크에 남는다
			s.written.fetch_add(batch.size(), std::memory_order_relaxed);
			batch.clear();				// clear는 용량을 유지하므로 다음 swap 때 재사용된다
		}

		{
			std::lock_guard lock(s.queueLock);
			s.busy = false;
			if (s.pending.empty())
				s.flushed.notify_all();
			if (stopping && s.pending.empty())
				break;
		}
	}
	ThreadManager::DestroyTLS();
}

void StartThread()
{
	State& s = S();
	{
		std::lock_guard lock(s.queueLock);
		s.stopRequested = false;
		s.running = true;
	}
	s.thread = std::thread(Run);
	s.async.store(true);
}

} // namespace

bool Start(const char* appName, const LogOptions& options)
{
	State& s = S();
	s.options = options;
	s.minLevel.store(options.minLevel);

	if (options.file)
	{
		::CreateDirectoryW(L"logs", nullptr);	// 이미 있으면 실패하지만 상관없다
		const std::time_t now = std::time(nullptr);
		std::tm local{};
		::localtime_s(&local, &now);
		const std::string path = std::format("logs/{}_{:04}{:02}{:02}_{:02}{:02}{:02}_{}.log", appName,
			local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec,
			::GetCurrentProcessId());
		if (::fopen_s(&s.file, path.c_str(), "w") != 0)
		{
			s.file = nullptr;
			std::fprintf(stderr, "로그 파일을 열 수 없음: %s\n", path.c_str());
		}
	}

	if (options.async)
		StartThread();
	return true;
}

void Stop()
{
	State& s = S();
	if (s.thread.joinable())
	{
		{
			std::lock_guard lock(s.queueLock);
			s.running = false;		// 이후 Submit은 동기 경로로
			s.stopRequested = true;
		}
		s.wakeLogger.notify_all();
		s.thread.join();			// Logger 스레드가 남은 것을 모두 쓰고 끝난다
	}
	s.async.store(false);

	std::lock_guard out(s.outputLock);
	std::fflush(stdout);
	if (s.file != nullptr)
	{
		std::fclose(s.file);
		s.file = nullptr;
	}
}

void Flush()
{
	State& s = S();
	if (s.async.load())
	{
		std::unique_lock lock(s.queueLock);
		s.wakeLogger.notify_one();
		s.flushed.wait(lock, [&] { return s.pending.empty() && !s.busy; });
	}
	std::lock_guard out(s.outputLock);
	std::fflush(stdout);
	if (s.file != nullptr)
		std::fflush(s.file);
}

void SetAsync(bool async)
{
	State& s = S();
	if (async == s.async.load())
		return;
	if (async)
	{
		if (s.thread.joinable())
			s.async.store(true);
		else
			StartThread();
	}
	else
	{
		Flush();		// 큐에 남은 것부터 다 쓰고 나서 동기로 바꿔야 순서가 뒤섞이지 않는다
		s.async.store(false);
	}
}

bool IsEnabled(LogLevel level)
{
	return level >= S().minLevel.load(std::memory_order_relaxed);
}

void Submit(LogLevel level, std::string&& message)
{
	State& s = S();
	Entry e = MakeEntry(level, std::move(message));

	if (s.async.load(std::memory_order_relaxed))
	{
		std::unique_lock lock(s.queueLock);
		if (s.running)
		{
			if (s.pending.size() >= s.options.maxQueued && level != LogLevel::Error)
			{
				// Logger가 따라가지 못한다. Worker를 기다리게 하느니 버리고 센다.
				s.dropped.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			const bool wasEmpty = s.pending.empty();
			s.pending.push_back(std::move(e));
			if (s.pending.size() > s.maxDepth)
				s.maxDepth = s.pending.size();
			lock.unlock();
			// 비어 있다가 처음 들어온 경우에만 깨운다. 이미 쌓여 있으면 Logger가 곧 가져간다.
			if (wasEmpty)
				s.wakeLogger.notify_one();
			return;
		}
	}

	// 동기 경로: Start 전, Stop 후, 또는 async=false
	std::lock_guard out(s.outputLock);
	Output(e);
	s.written.fetch_add(1, std::memory_order_relaxed);
}

Stats GetStats()
{
	State& s = S();
	std::lock_guard lock(s.queueLock);
	return { s.written.load(), s.dropped.load(), s.maxDepth };
}

} // namespace ServerCore::Log
