#pragma once

// 경로 찾기: 맵의 경로 격자(NavGrid, 50cm 칸) 위에서 A* (docs/DESIGN.md 7.3).
//
// - 8방향. 대각선은 양옆 두 칸이 모두 뚫려 있을 때만(벽 모서리를 비스듬히 뚫고 지나가지 않게).
// - '.' 칸만 지나간다. '#'(벽)과 '~'(낮은 장애물: 시야는 통과, 이동은 막힘)은 못 지나간다.
// - 결과는 시작 다음부터 목적지까지의 월드 좌표 점들. 직선으로 갈 수 있는 중간 점은 빼서(경로 다듬기)
//   경비원이 칸마다 꺾지 않고 곧게 걷는다.
//
// 메모리: A*에 필요한 칸별 배열(비용·부모·방문 표시, 21,760칸)을 방마다 두지 않고 "스레드마다" 하나 둔다(thread_local).
//   방의 틱은 한 번에 한 Worker에서만 돌기 때문에 스레드 버퍼를 빌려 써도 안전하고,
//   방이 1,000개여도 버퍼는 Worker 수만큼만 생긴다. 매번 지우지 않고 "세대 번호"로 지난 탐색의 값을 무시한다.

#include <cstdint>
#include <vector>

#include "Content/Game/MapData.h"

namespace Content {

struct PathStats
{
	uint32_t expanded = 0;		// A*가 꺼내 본 칸 수 (탐색 비용)
	uint64_t micros = 0;		// 걸린 시간
};

// from → to 경로. 찾으면 out에 점들(to 포함)을 넣고 true.
// from·to가 막힌 칸이면 가까운 뚫린 칸(반경 3칸)으로 옮겨서 찾는다. 그래도 없거나 길이 끊겼으면 false.
// 격자에 구역 번호(NavGrid::regions)가 있으면, 구역이 다를 때 탐색 없이 바로 false (expanded = 0).
bool FindPath(const NavGrid& nav, const Vec3& from, const Vec3& to, std::vector<Vec3>& out, PathStats* stats = nullptr);

// 두 점 사이를 직선으로 걸어갈 수 있나 (지나가는 칸이 모두 '.'). 경로 다듬기에 쓴다.
bool LineWalkable(const NavGrid& nav, const Vec3& a, const Vec3& b);

} // namespace Content
