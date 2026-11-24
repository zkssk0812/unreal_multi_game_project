#include "ServerCore/CorePch.h"
#include "Content/Game/GameWorld.h"

#include <algorithm>
#include <cmath>

#include "Content/GameData.h"
#include "Content/Game/WorldMath.h"

// 아이템 (docs/psd.md M2·M5): 환금 아이템·반납, 금고·보물 순환, 상자, 무기·소비 아이템 줍기, 필드 아이템.
//
// 보물의 한살이 (§11.4)
//   금고 1개(방 안 무작위 칸) → 10초 열기 → 보물(환금 아이템, 품질 유일)이 나타남 → 누가 주우면 금고 사라짐 + 전원 메시지
//   → 쓰러지면 그 자리에 떨어짐(누구에게 쓰러지든) → 반납되면 다음 보물 가치 ×1.2 → 직전 방을 빼고 새 금고
//   보물은 반납 말고는 사라지지 않는다 (I-24). 그래서 맵에 보물은 언제나 하나뿐이고 금고도 그때만 생긴다 (O-06).

namespace Content {

using namespace WorldMath;

// ───────────────── 금고·보물 ─────────────────

void GameWorld::PlaceSafe(const std::vector<uint16_t>& excludeRooms)
{
	// 방 안 무작위 위치 (O-01, Q-38). 처음에는 의적 시작 방을, 그 뒤로는 직전 금고 방(O-07)을 뺀다 (A-05)
	const auto spot = RandomRoomSpot(excludeRooms);
	if (!spot)
	{
		LOG_WARN("[Game] 금고를 놓을 빈 칸을 찾지 못함");
		return;
	}
	Entity& safe = Spawn(EntityKind::Safe, spot->pos);
	safe.yaw = std::uniform_int_distribution<int>(0, 3)(_rng) * 90.0f;	// 연출용 방향
	safe.interactPos = spot->pos;
	safe.interactRange = GameData::WorkRangeCm;		// 다가가서 1m 안에서 연다 (Q-33)
	safe.safeRoom = spot->room;
	_lastSafeRoom = spot->room;
}

void GameWorld::OnSafeOpened(Entity& safe, Entity& opener)
{
	safe.state |= StateBit::Opened;
	safe.dirty = true;
	_events.push_back(Msg::SafeOpened{ safe.id, opener.id });
	// 보물이 금고 앞에 나타난다. 줍는 것은 보통 환금 아이템처럼 F (O-03)
	Spawn(EntityKind::Valuable, safe.interactPos, static_cast<uint16_t>(ValuableQuality::Unique), _treasureValue);
}

// ───────────────── 줍기 ─────────────────

std::optional<RejectReason> GameWorld::PickUpItem(Entity& thief, Entity& item)
{
	switch (item.kind)
	{
	case EntityKind::Valuable:
	{
		// 환금 아이템: 즉시 획득, 소지 한도 없음 (I-01, M-05)
		const auto quality = static_cast<ValuableQuality>(item.typeId);
		thief.carried.push_back({ quality, item.param });
		if (quality == ValuableQuality::Unique)
		{
			thief.state |= StateBit::CarryingTreasure;
			_events.push_back(Msg::TreasureTaken{ thief.id, thief.pos, item.param });	// 전원 메시지 (I-23)
			for (Entity& e : _entities)
				if (e.kind == EntityKind::Safe && (e.state & StateBit::Opened) != 0)
					Remove(e);								// 보물을 가져가면 금고는 사라짐 (O-05)
		}
		PushMoneyEvent();
		break;
	}

	case EntityKind::WeaponPickup:
	{
		// 빈 칸이 있으면 빈 칸에, 꽉 찼으면 무기1과 바꾸고 원래 무기1은 주운 자리에 (W-03).
		// 삼단봉은 빈 칸을 채우는 무기라 바뀌면 땅에 남기지 않는다 (W-10)
		const Msg::WeaponSlot picked{ static_cast<WeaponId>(item.typeId), static_cast<uint16_t>(item.param) };
		if (thief.weapons[0].id == WeaponId::None)
			thief.weapons[0] = picked;
		else if (thief.weapons[1].id == WeaponId::None)
			thief.weapons[1] = picked;
		else
		{
			const Msg::WeaponSlot old = thief.weapons[0];
			thief.weapons[0] = picked;
			if (old.id != WeaponId::Baton)
				Spawn(EntityKind::WeaponPickup, item.pos, static_cast<uint16_t>(old.id), old.ammo);
		}
		break;
	}

	case EntityKind::ConsumablePickup:
		// 소비 아이템은 한 종류 1개만. 들고 있으면 바꾸고 들고 있던 것은 그 자리에 (Q-25, A-03)
		if (thief.consumable != ConsumableId::None)
			Spawn(EntityKind::ConsumablePickup, item.pos, static_cast<uint16_t>(thief.consumable));
		thief.consumable = static_cast<ConsumableId>(item.typeId);
		break;

	default:
		return RejectReason::InvalidState;
	}

	_events.push_back(Msg::Pickup{ thief.id, item.kind, item.typeId, item.param });
	Remove(item);
	return std::nullopt;
}

// ───────────────── 반납·떨어뜨리기 ─────────────────

void GameWorld::Deliver(Entity& thief)
{
	// 들고 있는 환금 아이템 전부 → 자기 반납 금액 (O-23)
	bool treasure = false;
	for (const CarriedItem& item : thief.carried)
	{
		thief.banked += item.value;
		if (item.quality == ValuableQuality::Unique)
		{
			treasure = true;
			++thief.treasuresDelivered;
		}
	}
	thief.carried.clear();
	ClearBits(thief.state, StateBit::CarryingTreasure);
	PushMoneyEvent();

	if (treasure)
	{
		// 다음 보물은 20% 비싸지고(I-22), 금고는 직전 방을 빼고 새로 생긴다 (O-07)
		_treasureValue = GameData::NextTreasureValue(_treasureValue);
		std::vector<uint16_t> exclude;
		if (_lastSafeRoom)
			exclude.push_back(*_lastSafeRoom);
		PlaceSafe(exclude);
	}
}

void GameWorld::DropValuables(Entity& thief, bool treasureOnly)
{
	for (const CarriedItem& item : thief.carried)
	{
		if (treasureOnly && item.quality != ValuableQuality::Unique)
			continue;		// 경비원에게 쓰러지면 보물 말고는 사라짐 (D-02)
		Spawn(EntityKind::Valuable, Scatter(thief.pos), static_cast<uint16_t>(item.quality), item.value);
	}
	thief.carried.clear();
	ClearBits(thief.state, StateBit::CarryingTreasure);
}

void GameWorld::PushMoneyEvent()
{
	Msg::Money money;
	for (const Entity& e : _entities)
		if (e.IsThief())
			money.thieves.push_back({ e.id, e.CarriedValue(), e.banked });
	_events.push_back(std::move(money));
}

// ───────────────── 상자 (§11.5) ─────────────────

void GameWorld::BreakBox(Entity& box)
{
	if (box.removed)
		return;
	if (Entity* user = Find(box.interactor); user != nullptr && user->interactTarget == box.id)
	{
		user->interactTarget = 0;			// 여는 중에 누가 부숴 버림
		ClearBits(user->state, StateBit::Interacting);
	}
	Remove(box);
	// 같은 자리에 30~60초 뒤 다시 생김 (O-14, A-02)
	const uint32_t wait = std::uniform_int_distribution<uint32_t>(GameData::BoxRespawnMinMs, GameData::BoxRespawnMaxMs)(_rng);
	_boxRespawns.push_back({ box.pos, _nowMs + wait, box.typeId });	// 같은 종류로

	// 드롭 표 (O-12, 클라 2026-11-24): 상자 종류마다 항상 아이템 하나
	const Vec3 pos = box.pos;
	if (static_cast<BoxType>(box.typeId) == BoxType::Valuable)
	{
		// 환금 상자: 일반 60 · 고급 30 · 희귀 10
		int pick = std::uniform_int_distribution<int>(0, 99)(_rng);
		ValuableQuality quality = GameData::BoxValuables[0].quality;
		for (const GameData::BoxValuableChance& chance : GameData::BoxValuables)
		{
			if (pick < chance.percent)
			{
				quality = chance.quality;
				break;
			}
			pick -= chance.percent;
		}
		Spawn(EntityKind::Valuable, pos, static_cast<uint16_t>(quality), RandomValue(quality));
		return;
	}

	// 무기 상자 (종류가 없는 상자도 무기 상자로): 소비 아이템 20%, 나머지는 무기
	if (std::uniform_int_distribution<int>(0, 99)(_rng) < GameData::WeaponBoxConsumablePct)
	{
		Spawn(EntityKind::ConsumablePickup, pos, static_cast<uint16_t>(std::uniform_int_distribution<int>(1, 4)(_rng)));
		return;
	}
	// 무기별 비율 GameData::BoxWeapons (권총·테이저건 20%, 레일건·화염방사기 5% …). 삼단봉은 나오지 않는다
	int pick = std::uniform_int_distribution<int>(0, 99)(_rng);
	WeaponId weapon = GameData::BoxWeapons[0].weapon;
	for (const GameData::BoxWeaponChance& chance : GameData::BoxWeapons)
	{
		if (pick < chance.percent)
		{
			weapon = chance.weapon;
			break;
		}
		pick -= chance.percent;
	}
	const GameData::WeaponType* w = GameData::WeaponOf(weapon);
	Spawn(EntityKind::WeaponPickup, pos, static_cast<uint16_t>(w->id), w->ammo);
}

void GameWorld::UpdateBoxRespawns()
{
	for (size_t i = 0; i < _boxRespawns.size();)
	{
		if (_nowMs >= _boxRespawns[i].atMs)
		{
			Spawn(EntityKind::Box, _boxRespawns[i].pos, _boxRespawns[i].typeId);
			_boxRespawns.erase(_boxRespawns.begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		++i;
	}
}

// ───────────────── 필드 아이템 (§11.6) ─────────────────

void GameWorld::UpdateFieldItems()
{
	// 맵에 최대 20개. 회복약 소 60% · 중 30% · 대 10% (I-11~I-14)
	const auto count = std::count_if(_entities.begin(), _entities.end(),
		[](const Entity& e) { return e.kind == EntityKind::FieldItem && !e.removed; }) +
		std::count_if(_spawnQueue.begin(), _spawnQueue.end(), [](const Entity& e) { return e.kind == EntityKind::FieldItem; });
	if (static_cast<size_t>(count) >= GameData::FieldItemMax)
		return;
	const auto cell = RandomFreeCell();
	if (!cell)
		return;
	int roll = std::uniform_int_distribution<int>(0, 99)(_rng);
	FieldItemId id = FieldItemId::PotionS;
	for (const GameData::FieldItemType& type : GameData::FieldItemTypes)
	{
		if (roll < type.percent)
		{
			id = type.id;
			break;
		}
		roll -= type.percent;
	}
	Spawn(EntityKind::FieldItem, *cell, static_cast<uint16_t>(id));
}

void GameWorld::TouchFieldItems(Entity& thief)
{
	// 닿으면 바로 회복. HP가 100이면 먹지 않고 그대로 둔다 (I-15, I-16)
	if (thief.hp >= GameData::PlayerMaxHp)
		return;
	for (Entity& e : _entities)
	{
		if (e.kind != EntityKind::FieldItem || e.removed || Dist2D(e.pos, thief.pos) > GameData::FieldItemTouchCm)
			continue;
		thief.hp = std::min(GameData::PlayerMaxHp, thief.hp + GameData::HealOf(static_cast<FieldItemId>(e.typeId)));
		_events.push_back(Msg::Pickup{ thief.id, e.kind, e.typeId, 0 });
		Remove(e);
		return;
	}
}

// ───────────────── 도우미 ─────────────────

bool GameWorld::IsFreeSpot(const Vec3& p, const Entity* ignore) const
{
	// 다른 오브젝트(플레이어·경비원 포함)와 1m 이상 떨어졌나 (A-04)
	auto crowded = [&](const Entity& e) { return &e != ignore && !e.removed && Dist2D(e.pos, p) < GameData::ItemSpacingCm; };
	return std::none_of(_entities.begin(), _entities.end(), crowded) && std::none_of(_spawnQueue.begin(), _spawnQueue.end(), crowded);
}

std::optional<GameWorld::RoomSpot> GameWorld::RandomRoomSpot(const std::vector<uint16_t>& excludeRooms, const Entity* ignore)
{
	// 놓을 칸이 있는 방 중 excludeRooms를 빼고 방을 먼저 고른 뒤(큰 방에 몰리지 않게),
	// 그 방에서 다른 오브젝트와 1m 이상 떨어진 칸을 고른다. 방마다 10번 해 보고 안 되면 다음 방
	if (_map == nullptr)
		return std::nullopt;
	std::vector<uint16_t> rooms;
	for (size_t i = 0; i < _map->rooms.size(); ++i)
	{
		const uint16_t room = static_cast<uint16_t>(i);
		if (!_map->rooms[i].returnCells.empty() && std::find(excludeRooms.begin(), excludeRooms.end(), room) == excludeRooms.end())
			rooms.push_back(room);
	}
	std::shuffle(rooms.begin(), rooms.end(), _rng);
	for (const uint16_t room : rooms)
	{
		const std::vector<Vec3>& cells = _map->rooms[room].returnCells;
		for (int attempt = 0; attempt < 10; ++attempt)
		{
			const Vec3 pos = cells[std::uniform_int_distribution<size_t>(0, cells.size() - 1)(_rng)];
			if (IsFreeSpot(pos, ignore))
				return RoomSpot{ pos, room };
		}
	}
	return std::nullopt;
}

GameWorld::RoomSpot GameWorld::ThiefSpot(const Entity* thief, const std::vector<uint16_t>& avoidRooms)
{
	// 의적 시작·리스폰: 방 안 무작위 칸 (A-26). 시작 때는 다른 의적과 다른 방, 안 되면 아무 방.
	// 리스폰하는 의적 자신(쓰러진 자리)은 빈 칸 검사에서 뺀다
	auto spot = RandomRoomSpot(avoidRooms, thief);
	if (!spot && !avoidRooms.empty())
		spot = RandomRoomSpot({}, thief);
	if (spot)
		return *spot;
	// 방이 없거나 빈 칸이 없으면 맵의 기준 지점
	if (_map != nullptr && !_map->thiefSpawns.empty())
	{
		const size_t index = thief != nullptr ? thief->spawnIndex : avoidRooms.size();
		return { _map->thiefSpawns[index % _map->thiefSpawns.size()].pos, 0 };
	}
	return {};
}

std::optional<Vec3> GameWorld::RandomFreeCell()
{
	// 방 안의 칸(복도 제외) 중 다른 오브젝트와 1m 이상 떨어진 곳 — 상자·필드 아이템 (A-02, A-04). 몇 번 해 봐도 없으면 포기
	if (_itemCells.empty())
		return std::nullopt;
	for (int attempt = 0; attempt < 30; ++attempt)
	{
		const Vec3& p = _itemCells[std::uniform_int_distribution<size_t>(0, _itemCells.size() - 1)(_rng)];
		if (IsFreeSpot(p))
			return p;
	}
	return std::nullopt;
}

Vec3 GameWorld::Scatter(const Vec3& center)
{
	const float a = std::uniform_real_distribution<float>(0.0f, 2.0f * Pi)(_rng);
	const float r = std::uniform_real_distribution<float>(0.0f, GameData::DropScatterCm)(_rng);
	return { center.x + r * std::cos(a), center.y + r * std::sin(a), center.z };
}

uint32_t GameWorld::RandomValue(ValuableQuality quality)
{
	for (const GameData::QualityType& q : GameData::QualityTypes)
		if (q.quality == quality)
			return std::uniform_int_distribution<uint32_t>(q.minValue, q.maxValue)(_rng);
	return 0;
}

} // namespace Content
