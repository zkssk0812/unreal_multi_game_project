#include "ServerCore/CorePch.h"
#include "Content/Perf.h"

#include <atomic>

namespace Content::Perf {

namespace {
std::atomic<int32_t> g_playingRooms{ 0 };
std::atomic<uint64_t> g_ticks{ 0 };
std::atomic<uint64_t> g_totalUs{ 0 };
std::atomic<uint64_t> g_maxUs{ 0 };

std::atomic<uint64_t> g_aiTicks{ 0 };
std::atomic<uint64_t> g_aiUs{ 0 };
std::atomic<uint64_t> g_sightTests{ 0 };
std::atomic<uint64_t> g_paths{ 0 };
std::atomic<uint64_t> g_pathUs{ 0 };
std::atomic<uint64_t> g_pathMaxUs{ 0 };
std::atomic<uint64_t> g_pathExpanded{ 0 };

// 최대값: 지금 값보다 클 때만 바꾼다. 다른 스레드가 먼저 바꿨으면 그 값으로 다시 비교
void StoreMax(std::atomic<uint64_t>& target, uint64_t value)
{
	uint64_t prev = target.load(std::memory_order_relaxed);
	while (value > prev && !target.compare_exchange_weak(prev, value, std::memory_order_relaxed))
	{
	}
}
} // namespace

void RecordTick(uint64_t microseconds)
{
	g_ticks.fetch_add(1, std::memory_order_relaxed);
	g_totalUs.fetch_add(microseconds, std::memory_order_relaxed);

	StoreMax(g_maxUs, microseconds);
}

void RecordAI(uint64_t microseconds, uint32_t sightTests)
{
	g_aiTicks.fetch_add(1, std::memory_order_relaxed);
	g_aiUs.fetch_add(microseconds, std::memory_order_relaxed);
	g_sightTests.fetch_add(sightTests, std::memory_order_relaxed);
}

void RecordPath(uint64_t microseconds, uint32_t expanded)
{
	g_paths.fetch_add(1, std::memory_order_relaxed);
	g_pathUs.fetch_add(microseconds, std::memory_order_relaxed);
	g_pathExpanded.fetch_add(expanded, std::memory_order_relaxed);
	StoreMax(g_pathMaxUs, microseconds);
}

void RoomPlaying(bool playing)
{
	g_playingRooms.fetch_add(playing ? 1 : -1, std::memory_order_relaxed);
}

TickReport TakeTickReport()
{
	TickReport report;
	report.playingRooms = static_cast<uint32_t>(std::max(0, g_playingRooms.load(std::memory_order_relaxed)));
	report.ticks = g_ticks.exchange(0, std::memory_order_relaxed);
	const uint64_t total = g_totalUs.exchange(0, std::memory_order_relaxed);
	report.maxUs = g_maxUs.exchange(0, std::memory_order_relaxed);
	report.avgUs = report.ticks > 0 ? total / report.ticks : 0;

	const uint64_t aiTicks = g_aiTicks.exchange(0, std::memory_order_relaxed);
	const uint64_t aiUs = g_aiUs.exchange(0, std::memory_order_relaxed);
	report.aiAvgUs = aiTicks > 0 ? aiUs / aiTicks : 0;
	report.sightTests = g_sightTests.exchange(0, std::memory_order_relaxed);
	report.paths = g_paths.exchange(0, std::memory_order_relaxed);
	const uint64_t pathUs = g_pathUs.exchange(0, std::memory_order_relaxed);
	const uint64_t expanded = g_pathExpanded.exchange(0, std::memory_order_relaxed);
	report.pathMaxUs = g_pathMaxUs.exchange(0, std::memory_order_relaxed);
	report.pathAvgUs = report.paths > 0 ? pathUs / report.paths : 0;
	report.pathAvgExpanded = report.paths > 0 ? expanded / report.paths : 0;
	return report;
}

} // namespace Content::Perf
