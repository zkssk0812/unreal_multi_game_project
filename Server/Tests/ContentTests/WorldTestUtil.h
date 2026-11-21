#pragma once

// GameWorld 규칙 시험의 공용 도우미 (psd.md T-05).
// GameWorld만으로 시험한다: 방·채널·시계가 없고 시간은 Run(ms)으로 흘린다.
// 좌표는 실제 맵(Data/Maps/mansion_1f_server.json ← Mansion_1F_Collision.json). 북쪽 전시실 Gallery1
// (x −75~1875, y −4350~−2425, 19m × 19m)은 가운데 낮은 벤치 둘(x 760~1040) 말고는 트인 곳이라 전투 시험을 여기서 한다.

#include <algorithm>
#include <functional>
#include <optional>
#include <random>
#include <vector>

#include "Content/Game/GameWorld.h"
#include "Content/GameData.h"

namespace WorldTest {

using namespace Content;

constexpr uint64_t A = 100;		// 플레이어 A의 playerId (캐릭터 1 = 사제 테이저건)
constexpr uint64_t B = 101;		// 캐릭터 2 = 끈끈이
constexpr uint64_t C = 102;		// 캐릭터 3 = 은신

const Vec3 HallW{ 400.0f, -3900.0f, 0.0f };		// Gallery1 서쪽 (벤치보다 남쪽 줄)
const Vec3 HallE{ 1400.0f, -3900.0f, 0.0f };	// HallW에서 +X로 10m
const Vec3 FarAway{ 3875.0f, 2075.0f, 0.0f };	// 동쪽 객실 GuestRoom7: Gallery1에서 보이지 않음
const Vec3 Parking{ -20000.0f, -20000.0f, 0.0f };	// 맵 밖 (방해되는 오브젝트를 치워 두는 곳)

inline Vec3 Offset(const Vec3& p, float dx, float dy) { return { p.x + dx, p.y + dy, p.z }; }

// pos가 들어 있는 방 (없으면 -1)
inline int RoomOf(const MapData& map, const Vec3& pos)
{
	for (size_t i = 0; i < map.rooms.size(); ++i)
		if (map.rooms[i].Contains(pos))
			return static_cast<int>(i);
	return -1;
}

// 방 안에서 무언가를 놓을 수 있는 칸인가 (복도 아님, 걸을 수 있음, 의적 쪽과 이어짐)
inline bool OnRoomCell(const MapData& map, const Vec3& pos)
{
	const int room = RoomOf(map, pos);
	return room >= 0 && map.rooms[static_cast<size_t>(room)].returnPoint && map.nav.IsWalkable(pos) &&
		map.nav.Connected(map.thiefSpawns[0].pos, pos);
}

// thieves명, 캐릭터 1부터. perks[i]는 i번 플레이어의 퍽. ai를 끄면 경비원은 시험이 직접 둔다
inline GameWorld MakeWorld(int thieves, bool ai = false, std::vector<std::vector<uint16_t>> perks = {}, uint32_t seed = 1234)
{
	std::vector<ThiefSlot> slots;
	for (int i = 0; i < thieves; ++i)
	{
		ThiefSlot slot{ A + static_cast<uint64_t>(i), static_cast<uint16_t>(i + 1), static_cast<uint8_t>(i), {}, std::format("p{}", i) };
		if (static_cast<size_t>(i) < perks.size())
			slot.perks = perks[static_cast<size_t>(i)];
		slots.push_back(slot);
	}
	GameWorld world;
	std::mt19937 rng(seed);
	world.Setup(DefaultMap(), slots, rng);
	world.SetAIEnabled(ai);
	world.TakeEvents();
	return world;
}

// 경비원·상자·필드 아이템·금고를 맵 밖으로 치운다 (전투 시험이 엉뚱한 대상을 맞히거나 줍지 않게.
// 금고·상자는 방 안 무작위 자리라 Gallery1에 생길 수도 있다)
inline void ClearArena(GameWorld& world)
{
	for (const Entity& e : world.Entities())
	{
		if (e.kind == EntityKind::Guard || e.kind == EntityKind::Box || e.kind == EntityKind::FieldItem || e.kind == EntityKind::Safe)
			world.Find(e.id)->pos = Parking;
	}
}

// 서버 틱처럼 50ms씩 흘린다
inline void Run(GameWorld& world, uint32_t ms)
{
	for (uint32_t t = 0; t < ms; t += 50)
		world.Update(0.05f);
}

// 흘리면서 이벤트를 모은다
inline std::vector<WorldEvent> RunCollect(GameWorld& world, uint32_t ms)
{
	std::vector<WorldEvent> all;
	for (uint32_t t = 0; t < ms; t += 50)
	{
		world.Update(0.05f);
		auto events = world.TakeEvents();
		all.insert(all.end(), events.begin(), events.end());
	}
	return all;
}

// until이 참이 될 때까지 (최대 maxMs) 흘린다
inline bool RunUntil(GameWorld& world, uint32_t maxMs, const std::function<bool()>& until, std::vector<WorldEvent>* events = nullptr)
{
	for (uint32_t t = 0; t < maxMs; t += 50)
	{
		world.Update(0.05f);
		auto taken = world.TakeEvents();
		if (events)
			events->insert(events->end(), taken.begin(), taken.end());
		if (until())
			return true;
	}
	return false;
}

inline void Teleport(GameWorld& world, uint64_t playerId, Vec3 pos, float yaw = 0.0f, uint8_t flags = 0)
{
	InputCmd input;
	input.kind = InputCmd::Kind::Move;
	input.pos = pos;
	input.yaw = yaw;
	input.flags = flags;
	world.ApplyMove(playerId, input);
}

inline std::optional<RejectReason> Act(GameWorld& world, uint64_t playerId, ActionType action, uint16_t target = 0,
	float yaw = 0.0f, Vec3 aim = {})
{
	InputCmd input;
	input.kind = InputCmd::Kind::Action;
	input.action = action;
	input.targetEntityId = target;
	input.yaw = yaw;
	input.aim = aim;
	return world.ApplyAction(playerId, input);
}

// kind 종류의 n번째 엔티티 (id로 돌려준다: 엔티티가 늘면 참조는 깨질 수 있으므로)
inline uint16_t Nth(const GameWorld& world, EntityKind kind, size_t n = 0)
{
	for (const Entity& e : world.Entities())
		if (e.kind == kind && !e.removed && n-- == 0)
			return e.id;
	return 0;
}

inline size_t CountKind(const GameWorld& world, EntityKind kind)
{
	return static_cast<size_t>(std::count_if(world.Entities().begin(), world.Entities().end(),
		[kind](const Entity& e) { return e.kind == kind && !e.removed; }));
}

template <class T>
size_t Count(const std::vector<WorldEvent>& events)
{
	return static_cast<size_t>(std::count_if(events.begin(), events.end(),
		[](const WorldEvent& e) { return std::holds_alternative<T>(e); }));
}

template <class T>
std::optional<T> First(const std::vector<WorldEvent>& events)
{
	for (const WorldEvent& e : events)
		if (const T* found = std::get_if<T>(&e))
			return *found;
	return std::nullopt;
}

// 무기를 쥐여 준다 (slot 칸, 탄은 무기 표의 기본값)
inline void Give(GameWorld& world, uint64_t playerId, WeaponId weapon, uint8_t slot = 1, bool hold = true)
{
	Entity* thief = world.ThiefOf(playerId);
	const GameData::WeaponType* type = GameData::WeaponOf(weapon);
	thief->weapons[slot] = { weapon, type ? type->ammo : uint16_t{ 0 } };
	if (hold)
		thief->activeSlot = slot;
}

// 첫 경비원을 pos에 yaw 방향으로 세운다
inline uint16_t PlaceGuard(GameWorld& world, Vec3 pos, float yaw, size_t n = 0)
{
	const uint16_t id = Nth(world, EntityKind::Guard, n);
	world.Find(id)->pos = pos;
	world.Find(id)->yaw = yaw;
	return id;
}

// 한 번만 공격 (누르고 바로 다음 틱에 뗀다)
inline void Attack(GameWorld& world, uint64_t playerId, float yaw, Vec3 aim = {})
{
	Act(world, playerId, ActionType::AttackStart, 0, yaw, aim);
	world.Update(0.05f);
	Act(world, playerId, ActionType::AttackStop);
}

} // namespace WorldTest
