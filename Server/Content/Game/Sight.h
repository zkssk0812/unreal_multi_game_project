#pragma once

// 시야 검사: 두 점 사이를 시야를 막는 충돌체(벽·가구)가 가리는지 3D로 판정한다 (docs/DESIGN.md 7.2).
//
// - 충돌체는 바닥 위의 사각형(Box) 또는 원(Circle)에 높이 범위 [zMin, zMax]가 있는 기둥이다.
// - 선분 from → to가 충돌체의 바닥 모양을 지나는 구간 [t0, t1]을 구하고, 그 구간에서 선분의 높이가
//   [zMin, zMax]와 겹치면 가려진 것. 그래서 낮은 조리대(시야 통과)나 울타리 위로 내려다보는 경우가 구분된다.
// - 시야를 막지 않는 충돌체(blocks_sight = false: 낮은 조리대·분수대)와 열고 닫히는 충돌체(창고 문·탈출구,
//   창살이라 보인다고 본다)는 건너뛴다.

#include <cstdint>

#include "Content/Game/MapData.h"

namespace Content {

// from과 to 사이에 시야를 막는 것이 없으면 true. tests가 있으면 실제로 선분 검사를 한 충돌체 수를 더한다(비용 측정)
bool HasLineOfSight(const MapData& map, const Vec3& from, const Vec3& to, uint32_t* tests = nullptr);

} // namespace Content
