#include "ServerCore/CorePch.h"
#include "Content/Game/Sight.h"

#include <algorithm>
#include <cmath>

namespace Content {

namespace {

// 선분 p(t) = a + t·d (t ∈ [0, 1])가 축 정렬 사각형을 지나는 t 구간 (슬랩 방법)
bool SegmentBox(float ax, float ay, float dx, float dy, const MapCollider& c, float& t0, float& t1)
{
	t0 = 0.0f;
	t1 = 1.0f;
	// x 축 슬랩, y 축 슬랩 각각에서 "안에 있는 t 범위"를 구해 겹치는 부분만 남긴다
	const float origin[2] = { ax, ay }, dir[2] = { dx, dy };
	const float lo[2] = { c.xmin, c.ymin }, hi[2] = { c.xmax, c.ymax };
	for (int axis = 0; axis < 2; ++axis)
	{
		if (std::fabs(dir[axis]) < 1e-6f)
		{
			if (origin[axis] < lo[axis] || origin[axis] > hi[axis])
				return false;		// 이 축으로 평행하게 지나가는데 범위 밖
			continue;
		}
		float enter = (lo[axis] - origin[axis]) / dir[axis];
		float exit = (hi[axis] - origin[axis]) / dir[axis];
		if (enter > exit)
			std::swap(enter, exit);
		t0 = std::max(t0, enter);
		t1 = std::min(t1, exit);
		if (t0 > t1)
			return false;
	}
	return true;
}

// 선분이 원 안을 지나는 t 구간: |a + t·d − c|² ≤ r² 을 t에 대한 2차식으로 풀기
bool SegmentCircle(float ax, float ay, float dx, float dy, const MapCollider& c, float& t0, float& t1)
{
	const float fx = ax - c.cx, fy = ay - c.cy;
	const float A = dx * dx + dy * dy;
	const float B = 2.0f * (fx * dx + fy * dy);
	const float C = fx * fx + fy * fy - c.r * c.r;
	if (A < 1e-6f)
	{
		t0 = 0.0f;
		t1 = 1.0f;
		return C <= 0.0f;		// 위에서 똑바로 내려다보는 선분: 시작점이 원 안인지
	}
	const float disc = B * B - 4.0f * A * C;
	if (disc < 0.0f)
		return false;
	const float root = std::sqrt(disc);
	t0 = std::max(0.0f, (-B - root) / (2.0f * A));
	t1 = std::min(1.0f, (-B + root) / (2.0f * A));
	return t0 <= t1;
}

} // namespace

bool SegmentBlockedBy(const MapCollider& c, const Vec3& from, const Vec3& to)
{
	const float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
	const bool box = c.shape == MapCollider::Shape::Box;
	const float cminX = box ? c.xmin : c.cx - c.r, cmaxX = box ? c.xmax : c.cx + c.r;
	const float cminY = box ? c.ymin : c.cy - c.r, cmaxY = box ? c.ymax : c.cy + c.r;
	if (cmaxX < std::min(from.x, to.x) || cminX > std::max(from.x, to.x) || cmaxY < std::min(from.y, to.y) || cminY > std::max(from.y, to.y))
		return false;
	float t0 = 0.0f, t1 = 0.0f;
	const bool hit = box ? SegmentBox(from.x, from.y, dx, dy, c, t0, t1) : SegmentCircle(from.x, from.y, dx, dy, c, t0, t1);
	if (!hit)
		return false;
	const float z0 = from.z + dz * t0, z1 = from.z + dz * t1;
	return std::max(z0, z1) >= c.zMin && std::min(z0, z1) <= c.zMax;
}

bool HasLineOfSight(const MapData& map, const Vec3& from, const Vec3& to, uint32_t* tests)
{
	const float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
	// 선분을 감싸는 사각형: 이것과 안 겹치는 충돌체는 계산 없이 건너뛴다 (대부분이 여기서 걸러진다)
	const float minX = std::min(from.x, to.x), maxX = std::max(from.x, to.x);
	const float minY = std::min(from.y, to.y), maxY = std::max(from.y, to.y);

	for (const MapCollider& c : map.colliders)
	{
		if (!c.blocksSight || c.dynamic)
			continue;
		const bool box = c.shape == MapCollider::Shape::Box;
		const float cminX = box ? c.xmin : c.cx - c.r, cmaxX = box ? c.xmax : c.cx + c.r;
		const float cminY = box ? c.ymin : c.cy - c.r, cmaxY = box ? c.ymax : c.cy + c.r;
		if (cmaxX < minX || cminX > maxX || cmaxY < minY || cminY > maxY)
			continue;

		if (tests)
			++*tests;
		float t0 = 0.0f, t1 = 0.0f;
		const bool hit = box ? SegmentBox(from.x, from.y, dx, dy, c, t0, t1) : SegmentCircle(from.x, from.y, dx, dy, c, t0, t1);
		if (!hit)
			continue;

		// 바닥 모양을 지나는 동안 선분의 높이 범위가 충돌체 높이와 겹치면 가려짐
		const float z0 = from.z + dz * t0, z1 = from.z + dz * t1;
		if (std::max(z0, z1) >= c.zMin && std::min(z0, z1) <= c.zMax)
			return false;
	}
	return true;
}

} // namespace Content
