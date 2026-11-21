#pragma once

// 게임 틱 성능 지표 (docs/DESIGN.md 8.1). 시연에서 "방 N개가 돌 때 틱 하나에 몇 µs 걸리나"를 보여 준다.
// P5: 경비원 AI 비용(AI에 쓴 시간, 경로 탐색 횟수·시간·탐색한 칸 수, 시야 검사 수)도 같이 모은다.
//
// - 방마다 틱이 끝날 때 RecordTick(걸린 µs)을 부른다. 여러 Worker 스레드가 동시에 부르므로 atomic.
// - main의 지표 보고(StatsReporter)가 5초마다 TakeTickReport()로 그동안의 평균·최대를 꺼내고 0으로 돌린다.
// - 틱 하나당 atomic 연산 몇 개라 방이 수천 개여도 부담이 없다 (초당 20 × 방 수).

#include <cstdint>

namespace Content::Perf {

struct TickReport
{
	uint32_t playingRooms = 0;		// 지금 게임 중인 방
	uint64_t ticks = 0;				// 이번 구간에 돈 틱 수 (모든 방 합)
	uint64_t avgUs = 0;				// 틱 하나의 평균 처리 시간
	uint64_t maxUs = 0;				// 가장 오래 걸린 틱

	// 경비원 AI (P5)
	uint64_t aiAvgUs = 0;			// 틱 하나에서 AI(UpdateGuards)에 쓴 평균 시간
	uint64_t paths = 0;				// 경로 탐색(A*) 횟수
	uint64_t pathAvgUs = 0;
	uint64_t pathMaxUs = 0;
	uint64_t pathAvgExpanded = 0;	// 한 번에 꺼내 본 칸 수 평균 (탐색 비용)
	uint64_t sightTests = 0;		// 시야 선분 검사를 실제로 한 충돌체 수
};

void RecordTick(uint64_t microseconds);
void RoomPlaying(bool playing);		// 방이 Playing에 들어가면 true, 나오면 false
void RecordAI(uint64_t microseconds, uint32_t sightTests);		// GameWorld::Update가 틱마다 한 번
void RecordPath(uint64_t microseconds, uint32_t expanded);		// A* 한 번
TickReport TakeTickReport();		// 지금까지 모은 것을 꺼내고 틱 지표는 0으로 (방 수는 유지)

} // namespace Content::Perf
