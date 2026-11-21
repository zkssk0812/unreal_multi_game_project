#include "NetStats.h"
#include "ThreadManager.h"

#include <chrono>

namespace {

// 스레드 하나의 카운터 묶음. alignas(64): 다른 스레드의 묶음과 같은 캐시 줄(64바이트)에 놓이지 않게.
// (일부러 채우는 것이므로 "맞춤 때문에 구조체가 채워졌다" 경고 C4324는 끈다)
#pragma warning(push)
#pragma warning(disable : 4324)
struct alignas(64) ThreadCounters
{
	std::array<std::atomic<uint64>, NetStats::kCount> values{};
};
#pragma warning(pop)

std::mutex g_registryLock;
std::vector<std::unique_ptr<ThreadCounters>> g_registry;	// 스레드가 끝나도 지우지 않는다(누적값 유지)
thread_local ThreadCounters* tls_counters = nullptr;

ThreadCounters& Mine()
{
	if (tls_counters == nullptr)
	{
		auto block = std::make_unique<ThreadCounters>();
		tls_counters = block.get();
		std::lock_guard lock(g_registryLock);
		g_registry.push_back(std::move(block));		// 스레드마다 처음 한 번만 락
	}
	return *tls_counters;
}

} // namespace

void NetStats::Add(Counter counter, uint64 value)
{
	// 이 묶음에 쓰는 스레드는 자기 자신뿐이다. 그래서 fetch_add(원자적 읽기-수정-쓰기, lock 접두어 명령)
	// 대신 "읽고 더해서 쓰기"를 각각 원자적으로 하면 충분하다. 읽는 쪽(보고 스레드)이 찢어진 값을
	// 보지 않도록 atomic은 유지하되, 가장 싼 memory_order_relaxed를 쓴다.
	std::atomic<uint64>& v = Mine().values[counter];
	v.store(v.load(std::memory_order_relaxed) + value, std::memory_order_relaxed);
}

std::array<uint64, NetStats::kCount> NetStats::Snapshot()
{
	std::array<uint64, kCount> sum{};
	std::lock_guard lock(g_registryLock);
	for (const auto& block : g_registry)
		for (uint32 i = 0; i < kCount; ++i)
			sum[i] += block->values[i].load(std::memory_order_relaxed);
	return sum;
}

// ───────────────────────── StatsReporter ─────────────────────────

void StatsReporter::Start(ThreadManager& threads, uint32 intervalMs, std::function<std::string()> extra)
{
	_extra = std::move(extra);
	{
		std::lock_guard lock(_lock);
		_stop = false;
	}
	threads.Launch("Stats", [this, intervalMs] { Run(intervalMs); });
}

void StatsReporter::Stop()
{
	{
		std::lock_guard lock(_lock);
		_stop = true;
	}
	_cv.notify_all();	// 1초를 다 기다리지 않고 바로 깨운다
}

void StatsReporter::Run(uint32 intervalMs)
{
	auto prev = NetStats::Snapshot();
	auto prevTime = std::chrono::steady_clock::now();

	while (true)
	{
		{
			std::unique_lock lock(_lock);
			if (_cv.wait_for(lock, std::chrono::milliseconds(intervalMs), [this] { return _stop; }))
				return;
		}

		const auto now = NetStats::Snapshot();
		const auto nowTime = std::chrono::steady_clock::now();
		const double sec = std::chrono::duration<double>(nowTime - prevTime).count();

		std::array<uint64, NetStats::kCount> d{};
		bool active = false;
		for (uint32 i = 0; i < NetStats::kCount; ++i)
		{
			d[i] = now[i] - prev[i];
			active |= d[i] != 0;
		}
		prev = now;
		prevTime = nowTime;

		// 아무 일도 없으면 찍지 않는다(한가한 서버의 콘솔이 지표로 덮이지 않게)
		if (!active)
			continue;

		const auto perSec = [sec](uint64 v) { return static_cast<double>(v) / sec; };
		LOG_INFO("{} | 수신 {:.1f}KB/s ({:.0f}회/s) | 송신 {:.1f}KB/s (WSASend {:.0f}회/s) | 패킷 {:.0f}/s"
			" | 조립대기 헤더 {} 본문 {} | 접속 +{} -{}",
			_extra ? _extra() : std::string{},
			perSec(d[NetStats::RecvBytes]) / 1024.0, perSec(d[NetStats::RecvCalls]),
			perSec(d[NetStats::SendBytes]) / 1024.0, perSec(d[NetStats::SendCalls]),
			perSec(d[NetStats::Packets]),
			d[NetStats::PartialHeader], d[NetStats::PartialBody],
			d[NetStats::Connects], d[NetStats::Disconnects]);
	}
}
