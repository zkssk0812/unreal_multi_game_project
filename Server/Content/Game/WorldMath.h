#pragma once

// GameWorld의 여러 파일(GameWorld.cpp, WorldCombat.cpp, GuardAI.cpp …)이 함께 쓰는 작은 계산 도우미.
// 좌표는 cm, 방향(yaw)은 도(degree). UE 기준: yaw 0 = +X, 90 = +Y.

#include <cmath>
#include <cstdint>

#include "Content/Messages/Types.h"

namespace Content::WorldMath {

constexpr float Pi = 3.14159265f;

inline void ClearBits(uint32_t& state, uint32_t bits) { state &= ~bits; }

// 각도 차이를 -180 ~ 180도로
inline float AngleDiff(float a, float b)
{
	float d = std::fmod(a - b, 360.0f);
	if (d > 180.0f) d -= 360.0f;
	if (d < -180.0f) d += 360.0f;
	return d;
}

// from에서 to를 바라보는 방향
inline float YawTo(const Vec3& from, const Vec3& to) { return std::atan2(to.y - from.y, to.x - from.x) * 180.0f / Pi; }

// 바닥 위(2D) 거리
inline float Dist2D(const Vec3& a, const Vec3& b)
{
	const float dx = a.x - b.x, dy = a.y - b.y;
	return std::sqrt(dx * dx + dy * dy);
}

// yaw 방향으로 distance만큼 간 곳 (높이는 그대로)
inline Vec3 Ahead(const Vec3& from, float yaw, float distance)
{
	const float r = yaw * Pi / 180.0f;
	return { from.x + std::cos(r) * distance, from.y + std::sin(r) * distance, from.z };
}

} // namespace Content::WorldMath
