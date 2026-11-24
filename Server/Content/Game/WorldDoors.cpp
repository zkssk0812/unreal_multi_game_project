#include "ServerCore/CorePch.h"
#include "Content/Game/GameWorld.h"

#include <algorithm>

#include "Content/GameData.h"
#include "Content/Game/Sight.h"
#include "Content/Game/WorldMath.h"

// 문 (클라 2026-11-24): 방 사이 문 37곳을 F로 열고 닫는다.
//
// - 맵의 안쪽 above_door(문 위 벽) 자리마다 Door 엔티티 하나. 처음엔 모두 닫힘, 열림 = Opened 비트.
//   잠긴 바깥 문 2곳은 맵의 벽(closed_exterior_door)이라 엔티티가 없다.
// - 닫힌 문은 벽처럼 시야·공격을 막는다: 경비원 시야, 총알·근접·폭발의 벽 검사(Blocked)가 모두 SeesThrough를 거친다.
//   레일건은 벽을 뚫듯이 문도 뚫는다.
// - 이동은 클라가 막는다(서버는 이동을 검증하지 않음). 그래서 문간에 누가 있으면 닫지 않는다.
// - 경비원은 가는 길 1m 앞의 닫힌 문을 서버가 열고 지나간다. 문은 열린 채로 둔다.

namespace Content {

using namespace WorldMath;

void GameWorld::PlaceDoors()
{
	_doorOpen.assign(_map->doors.size(), 0);
	_doorIds.assign(_map->doors.size(), 0);
	for (size_t i = 0; i < _map->doors.size(); ++i)
	{
		const MapDoor& d = _map->doors[i];
		Entity& door = Add(EntityKind::Door, d.center, d.yaw, static_cast<uint16_t>(i));
		door.interactPos = d.center;
		door.interactRange = GameData::DoorRangeCm;
		_doorIds[i] = door.id;
	}
}

bool GameWorld::DoorOccupied(size_t index) const
{
	// 문짝 자리(문 위 벽의 바닥 모양)에 몸(반지름 40cm)이 걸친 플레이어·경비원이 있나
	const MapCollider& p = _map->doors[index].panel;
	const float r = GameData::BodyRadiusCm;
	for (const Entity& e : _entities)
	{
		if (e.removed || !(e.IsThief() || e.IsGuard()) || (e.state & StateBit::LeftGame) != 0)
			continue;
		if (e.pos.x >= p.xmin - r && e.pos.x <= p.xmax + r && e.pos.y >= p.ymin - r && e.pos.y <= p.ymax + r)
			return true;
	}
	return false;
}

void GameWorld::SetDoorOpen(Entity& door, bool open)
{
	if (door.typeId >= _doorOpen.size() || (_doorOpen[door.typeId] != 0) == open)
		return;
	_doorOpen[door.typeId] = open ? 1 : 0;
	if (open)
		door.state |= StateBit::Opened;
	else
		ClearBits(door.state, StateBit::Opened);
	door.dirty = true;				// 바뀐 틱의 스냅샷에 실려 모두에게 간다
}

std::optional<RejectReason> GameWorld::ToggleDoor(Entity& door)
{
	if (door.typeId >= _doorOpen.size())
		return RejectReason::InvalidState;
	const bool open = _doorOpen[door.typeId] != 0;
	if (open && DoorOccupied(door.typeId))
		return RejectReason::TargetBusy;		// 문간에 누가 있으면 닫지 않음
	SetDoorOpen(door, !open);
	return std::nullopt;
}

bool GameWorld::ClosedDoorBlocks(const Vec3& from, const Vec3& to) const
{
	for (size_t i = 0; i < _doorOpen.size(); ++i)
		if (_doorOpen[i] == 0 && SegmentBlockedBy(_map->doors[i].panel, from, to))
			return true;
	return false;
}

bool GameWorld::SeesThrough(const Vec3& from, const Vec3& to, uint32_t* tests) const
{
	if (_map == nullptr)
		return true;
	return HasLineOfSight(*_map, from, to, tests) && !ClosedDoorBlocks(from, to);
}

void GameWorld::OpenDoorsOnWay(const Vec3& from, const Vec3& to)
{
	// 바닥에서 가슴 높이 사이로 지나가는 선분이 닫힌 문짝을 지나면 연다
	const Vec3 a{ from.x, from.y, GameData::ShotHeightCm };
	const Vec3 b{ to.x, to.y, GameData::ShotHeightCm };
	for (size_t i = 0; i < _doorOpen.size(); ++i)
	{
		if (_doorOpen[i] != 0 || !SegmentBlockedBy(_map->doors[i].panel, a, b))
			continue;
		if (Entity* door = Find(_doorIds[i]))
			SetDoorOpen(*door, true);
		else
			_doorOpen[i] = 1;
	}
}

} // namespace Content
