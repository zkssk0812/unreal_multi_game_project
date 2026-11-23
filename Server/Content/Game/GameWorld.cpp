#include "ServerCore/CorePch.h"
#include "Content/Game/GameWorld.h"

#include <algorithm>
#include <cmath>

#include "Content/GameData.h"
#include "Content/Game/WorldMath.h"

// 준비, 입력 분기, 상호작용, 틱 순서, 반납 지점, 스냅샷·결과·개인 상태 (docs/psd.md 5장).
// 전투·아이템·스킬·경비원은 같은 클래스의 다른 파일(WorldCombat.cpp 등)에 있다.

namespace Content {

using namespace WorldMath;

namespace {

constexpr uint16_t NoRoom = 0xFFFF;		// 아직 방에 놓이지 않은 반납 지점

} // namespace

uint32_t Entity::CarriedValue() const
{
	uint32_t value = 0;
	for (const CarriedItem& item : carried)
		value += item.value;
	return value;
}

bool Entity::HasTreasure() const
{
	return std::any_of(carried.begin(), carried.end(), [](const CarriedItem& i) { return i.quality == ValuableQuality::Unique; });
}

// ───────────────── 준비 ─────────────────

void GameWorld::Setup(const MapData& map, const std::vector<ThiefSlot>& thieves, std::mt19937& rng)
{
	Clear();
	_map = &map;
	_rng.seed(rng());		// 판마다 다른 배치 (시험은 rng 씨앗을 고정해 같은 결과)

	// 무제한 사거리 = 맵 대각선 + 10m (A-12)
	const float w = map.bounds.xmax - map.bounds.xmin, h = map.bounds.ymax - map.bounds.ymin;
	_maxRangeCm = (w > 0.0f && h > 0.0f ? std::sqrt(w * w + h * h) : 10000.0f) + GameData::UnlimitedExtraCm;
	_treasureValue = GameData::TreasureFirstValue;

	// 상자·필드 아이템을 놓을 수 있는 칸: 방 안의 걸을 수 있고 플레이어가 닿는 칸 (반납 지점 후보와 같은 칸)
	for (const MapRoom& room : map.rooms)
		_itemCells.insert(_itemCells.end(), room.returnCells.begin(), room.returnCells.end());

	// 1) 플레이어: 방 안 무작위 칸, 셋이 서로 다른 방에서 시작 (A-26). 플레이어가 1~3번 엔티티가 되도록 가장 먼저 만든다.
	std::vector<uint16_t> startRooms;
	for (const ThiefSlot& slot : thieves)
	{
		const RoomSpot spot = ThiefSpot(nullptr, startRooms);
		startRooms.push_back(spot.room);
		const float yaw = std::uniform_real_distribution<float>(-180.0f, 180.0f)(_rng);
		Entity& thief = Add(EntityKind::Thief, spot.pos, yaw, slot.characterId, slot.playerId);
		thief.name = slot.name;
		thief.spawnIndex = slot.spawnIndex;
		for (uint16_t perk : slot.perks)
			if (perk < 16)
				thief.perkMask = static_cast<uint16_t>(thief.perkMask | (1u << perk));
		thief.skill = GameData::SkillOf(slot.characterId);
		ResetLoadout(thief);
	}

	// 2) 경비원
	for (const MapPoint& spawn : map.guardSpawns)
		Add(EntityKind::Guard, spawn.pos, spawn.yaw, 1).hp = GameData::GuardMaxHp;

	// 3) 반납 지점: 서로 다른 방 5곳
	for (size_t i = 0; i < GameData::ReturnPointCount; ++i)
	{
		Entity& point = Add(EntityKind::ReturnPoint, {}, 0.0f, NoRoom);
		if (!PlaceReturnPoint(point, std::nullopt))
		{
			_entities.pop_back();
			--_nextId;
			break;
		}
	}

	// 4) 금고 1개 (O-01), 상자 (A-02), 필드 아이템 (I-14)
	PlaceSafe(startRooms);		// 의적이 시작하는 방에는 두지 않는다 (시작하자마자 열리지 않게)
	for (size_t i = 0; i < GameData::BoxCount; ++i)
		if (const auto cell = RandomFreeCell())
			Spawn(EntityKind::Box, *cell);
	for (size_t i = 0; i < GameData::FieldItemStart; ++i)
		UpdateFieldItems();
	_nextFieldItemMs = GameData::FieldItemEveryMs;
	Flush();

	// 5) 경비원 AI: 경비원마다 뇌 하나, 가장 가까운 순찰 지점부터 순찰
	_patrolLinks.assign(map.patrolNodes.size(), {});
	for (const MapPatrolEdge& edge : map.patrolEdges)
	{
		if (edge.a >= map.patrolNodes.size() || edge.b >= map.patrolNodes.size())
			continue;
		_patrolLinks[edge.a].push_back(edge.b);
		_patrolLinks[edge.b].push_back(edge.a);
	}
	_patrolVisitMs.assign(map.patrolNodes.size(), 0);
	for (Entity& e : _entities)
	{
		if (!e.IsGuard())
			continue;
		GuardBrain& brain = _brains.emplace_back();
		brain.guardId = e.id;
		if (!map.patrolNodes.empty())
			StartPatrol(e, brain);
	}
	_events.clear();	// 시작 전의 알림은 보내지 않는다 (SC_GAME_START에 모두 들어 있음)
}

void GameWorld::Clear()
{
	_entities.clear();
	_spawnQueue.clear();
	_nextId = 1;
	_nowMs = 0;
	_events.clear();
	_map = nullptr;
	_brains.clear();
	_patrolLinks.clear();
	_patrolVisitMs.clear();
	_thrown.clear();
	_boxRespawns.clear();
	_itemCells.clear();
	_lastSafeRoom.reset();
	_nextFieldItemMs = 0;
}

// ───────────────── 엔티티 넣고 빼기 ─────────────────

Entity& GameWorld::Add(EntityKind kind, Vec3 pos, float yaw, uint16_t typeId, uint64_t owner)
{
	Entity& e = _entities.emplace_back();
	e.id = _nextId++;
	e.kind = kind;
	e.pos = pos;
	e.yaw = yaw;
	e.typeId = typeId;
	e.ownerPlayerId = owner;
	e.spawnMs = _nowMs;
	return e;
}

Entity& GameWorld::Spawn(EntityKind kind, Vec3 pos, uint16_t typeId, uint32_t param)
{
	// 큐에 넣고 id는 지금 정한다. 돌려준 참조는 다음 Spawn 전까지만 쓸 것(큐가 커지면 옮겨짐)
	Entity& e = _spawnQueue.emplace_back();
	e.id = _nextId++;
	e.kind = kind;
	e.pos = pos;
	e.typeId = typeId;
	e.param = param;
	e.spawnMs = _nowMs;
	e.interactPos = pos;
	switch (kind)
	{
	case EntityKind::Box:
		e.hp = GameData::BoxHp;
		e.interactRange = GameData::WorkRangeCm + GameData::BoxRadiusCm;	// 상자 겉에서 1m
		break;
	case EntityKind::Valuable:
	case EntityKind::WeaponPickup:
	case EntityKind::ConsumablePickup:
		e.interactRange = GameData::PickupRangeCm;		// 3m 안이면 F로 즉시 줍기 (IN-03)
		break;
	default:
		break;
	}
	return e;
}

uint16_t GameWorld::AddNow(EntityKind kind, Vec3 pos, uint16_t typeId, uint32_t param)
{
	const uint16_t id = Spawn(kind, pos, typeId, param).id;
	Flush();
	return id;
}

void GameWorld::Remove(Entity& e)
{
	if (e.IsThief() || e.IsGuard())
		return;			// 플레이어·경비원은 지우지 않는다 (판 끝까지 같은 자리)
	e.removed = true;
}

void GameWorld::Flush()
{
	// 지운 것 먼저: 같은 틱에 지우고 새로 만든 것이 있어도 클라는 Despawn → Spawn 순서로 받는다
	Msg::EntityDespawn gone;
	for (const Entity& e : _entities)
		if (e.removed)
			gone.entityIds.push_back(e.id);
	if (!gone.entityIds.empty())
	{
		std::erase_if(_entities, [](const Entity& e) { return e.removed; });
		_events.push_back(std::move(gone));
	}

	if (!_spawnQueue.empty())
	{
		Msg::EntitySpawn spawn;
		for (Entity& e : _spawnQueue)
		{
			spawn.entities.push_back(InfoOf(e));
			_entities.push_back(std::move(e));
		}
		_spawnQueue.clear();
		_events.push_back(std::move(spawn));
	}
}

Msg::EntityInfo GameWorld::InfoOf(const Entity& e) const
{
	Msg::EntityInfo info{ e.id, e.kind, e.typeId, e.ownerPlayerId, e.pos, e.yaw, e.state, 0, e.param };
	if (e.kind == EntityKind::ReturnPoint)
	{
		const uint32_t age = _nowMs - e.spawnMs;
		info.timerMs = age < GameData::ReturnPointLifeMs ? GameData::ReturnPointLifeMs - age : 0;
	}
	else if (e.expireMs > _nowMs)
	{
		info.timerMs = e.expireMs - _nowMs;		// 끈끈이·연막
	}
	return info;
}

// ───────────────── 입력 ─────────────────

bool GameWorld::CanAct(const Entity& thief) const
{
	return (thief.state & (StateBit::Downed | StateBit::LeftGame | StateBit::Stunned)) == 0;
}

void GameWorld::ApplyMove(uint64_t playerId, const InputCmd& input)
{
	Entity* thief = ThiefOf(playerId);
	if (thief == nullptr || (thief->state & (StateBit::Downed | StateBit::LeftGame)) != 0)
		return;
	if ((thief->state & StateBit::RailgunLock) != 0)
		return;		// 레일건 충전·발사 2초는 이동·회전 불가 (E-10)

	// 위치는 클라가 정한 값을 그대로 쓴다(이동은 검증하지 않음). 움직인 방향은 끈끈이를 뒤에 놓을 때 쓴다
	if (Dist2D(thief->pos, input.pos) > 1.0f)
		thief->moveYaw = YawTo(thief->pos, input.pos);
	thief->pos = input.pos;
	thief->yaw = input.yaw;			// 시선 = 마우스 커서 방향 (CT-09)
	thief->moving = (input.flags & MoveFlag::Moving) != 0;

	// 달리기: 스테미나가 있고, 탈진·은신·경직이 아닐 때만 (은신 중 특수 동작 불가)
	const bool wantsRun = thief->moving && (input.flags & MoveFlag::Running) != 0;
	const uint32_t noRun = StateBit::Exhausted | StateBit::Cloaked | StateBit::Stunned;
	if (wantsRun && thief->stamina > 0.0f && (thief->state & noRun) == 0)
		thief->state |= StateBit::Running;
	else
		ClearBits(thief->state, StateBit::Running);
}

std::optional<RejectReason> GameWorld::ApplyAction(uint64_t playerId, const InputCmd& input)
{
	Entity* thief = ThiefOf(playerId);
	if (thief == nullptr || (thief->state & StateBit::LeftGame) != 0)
		return RejectReason::InvalidState;
	if ((thief->state & StateBit::Downed) != 0)
		return RejectReason::Downed;

	const bool stunned = (thief->state & StateBit::Stunned) != 0;
	const bool locked = (thief->state & (StateBit::RailgunLock | StateBit::Jetpack)) != 0;

	std::optional<RejectReason> result;
	switch (input.action)
	{
	case ActionType::AttackStart:
		// 꾹 누르기 시작: 이후 틱마다 무기 간격으로 공격 (C-02). SP는 쓰지 않는다 (Q-08)
		if (stunned)
		{
			result = RejectReason::Stunned;
			break;
		}
		if ((thief->state & StateBit::RailgunLock) == 0)
			thief->yaw = input.yaw;
		thief->aim = input.aim;
		if (!thief->attackHeld)
		{
			thief->attackHeld = true;
			thief->nextFireMs = std::max(thief->nextFireMs, _nowMs);
		}
		thief->state |= StateBit::Attacking;
		if (thief->interactTarget != 0)
			CancelInteract(*thief);			// 금고를 열다가 공격하면 열기는 멈춘다(진행도는 남음)
		if ((thief->state & StateBit::Cloaked) != 0)
			EndCloak(*thief);				// 은신 중 공격하면 풀림 (Q-30)
		break;

	case ActionType::AttackStop:
		thief->attackHeld = false;
		ClearBits(thief->state, StateBit::Attacking);
		break;

	case ActionType::Dodge:
	{
		if (stunned)
			result = RejectReason::Stunned;
		else if (locked)
			result = RejectReason::Locked;
		else if ((thief->state & StateBit::Cloaked) != 0)
			result = RejectReason::InvalidState;	// 은신 중 특수 동작 불가
		else if (thief->action != ActionType::None)
			result = RejectReason::Cooldown;
		else if ((thief->state & StateBit::Exhausted) != 0 || thief->stamina < GameData::DodgeStamina)
			result = RejectReason::NoStamina;
		if (result)
			break;
		if (thief->interactTarget != 0)
			CancelInteract(*thief);
		thief->stamina = std::max(0.0f, thief->stamina - GameData::DodgeStamina);
		if (thief->stamina <= 0.0f)
			thief->state |= StateBit::Exhausted;
		thief->action = ActionType::Dodge;
		thief->actionStartMs = _nowMs;
		thief->state |= StateBit::Dodging;
		break;
	}

	case ActionType::InteractBegin:
		if (stunned)
			result = RejectReason::Stunned;
		else if (locked)
			result = RejectReason::Locked;
		else
			result = BeginInteract(*thief, input.targetEntityId);
		break;

	case ActionType::InteractCancel:
		if (thief->interactTarget == 0)
			result = RejectReason::InvalidState;
		else
			CancelInteract(*thief);
		break;

	case ActionType::SwitchWeapon:
	{
		const uint8_t other = static_cast<uint8_t>(thief->activeSlot ^ 1);
		if ((thief->state & StateBit::TaserReady) != 0)
			result = RejectReason::Locked;				// 사제 테이저건을 든 동안은 무기 수와 상관없이 교체 불가 (Q로 먼저 집어넣기)
		else if (thief->weapons[other].id == WeaponId::None)
			result = RejectReason::OnlyOneWeapon;		// 무기가 하나뿐이면 교체 불가 (CT-05)
		else if ((thief->state & StateBit::RailgunLock) != 0)
			result = RejectReason::Locked;
		else
			thief->activeSlot = other;
		break;
	}

	case ActionType::UseSkill:
		result = stunned ? std::optional(RejectReason::Stunned) : UseSkill(*thief);
		break;

	case ActionType::UseConsumable:
		result = stunned ? std::optional(RejectReason::Stunned) : UseConsumable(*thief, input);
		break;

	default:
		result = RejectReason::NotAvailable;
		break;
	}

	Flush();		// 행동으로 생긴 엔티티(지뢰·떨어진 무기 등)를 반영. 이 뒤로 thief 포인터는 쓰지 않는다
	return result;
}

// ───────────────── 상호작용 (§13) ─────────────────
// 즉시: 환금·무기·소비 아이템 줍기(3m), 반납 지점에 반납
// 시간: 금고 열기(10초 × 작업 속도, 진행도 유지), 상자 열기(3초, 멈추면 처음부터) — 1m 안에서

std::optional<RejectReason> GameWorld::BeginInteract(Entity& thief, uint16_t targetId)
{
	if (thief.interactTarget != 0)
		return RejectReason::InvalidState;		// 이미 다른 것과 상호작용 중
	Entity* target = Find(targetId);
	if (target == nullptr)
		return RejectReason::InvalidState;
	const bool known = target->kind == EntityKind::Safe || target->kind == EntityKind::Box ||
		target->kind == EntityKind::ReturnPoint || target->kind == EntityKind::Valuable ||
		target->kind == EntityKind::WeaponPickup || target->kind == EntityKind::ConsumablePickup;
	if (!known)
		return RejectReason::InvalidState;		// 필드 아이템은 닿아야만 (IN-04)
	if (!InRange(thief, *target))
		return RejectReason::OutOfRange;

	switch (target->kind)
	{
	case EntityKind::Valuable:
	case EntityKind::WeaponPickup:
	case EntityKind::ConsumablePickup:
	{
		const auto reject = PickUpItem(thief, *target);
		if (!reject && (thief.state & StateBit::Cloaked) != 0)
			EndCloak(thief);					// 상호작용하면 은신이 풀림 (Q-30)
		return reject;
	}

	case EntityKind::ReturnPoint:
		if (thief.carried.empty())
			return RejectReason::InvalidState;	// 반납할 것이 없음
		Deliver(thief);
		ReplaceReturnPoint(*target);
		if ((thief.state & StateBit::Cloaked) != 0)
			EndCloak(thief);
		return std::nullopt;

	case EntityKind::Safe:
		if ((target->state & StateBit::Opened) != 0)
			return RejectReason::InvalidState;	// 열린 금고는 안의 보물(환금 아이템)을 줍는다
		break;

	case EntityKind::Box:
		if (target->interactor == 0)
			target->progressMs = 0.0f;			// 상자는 처음부터
		break;

	default:
		return RejectReason::InvalidState;
	}

	// 시간 걸리는 상호작용 시작: 대상마다 한 명만
	if (target->interactor != 0)
		return RejectReason::TargetBusy;
	if ((thief.state & StateBit::Cloaked) != 0)
		EndCloak(thief);
	thief.interactTarget = target->id;
	target->interactor = thief.id;
	thief.state |= StateBit::Interacting;
	ClearBits(thief.state, StateBit::Running);
	_events.push_back(Msg::Interact{ thief.id, target->id, InteractState::Begin,
		static_cast<uint32_t>(target->progressMs), InteractDurationMs(*target) });
	return std::nullopt;
}

void GameWorld::CancelInteract(Entity& thief)
{
	if (Entity* target = Find(thief.interactTarget); target != nullptr && target->interactor == thief.id)
	{
		target->interactor = 0;
		if (target->kind == EntityKind::Box)
			target->progressMs = 0.0f;			// 상자는 멈추면 처음부터. 금고는 진행도 유지 (O-02)
		_events.push_back(Msg::Interact{ thief.id, target->id, InteractState::Cancel,
			static_cast<uint32_t>(target->progressMs), InteractDurationMs(*target) });
	}
	thief.interactTarget = 0;
	ClearBits(thief.state, StateBit::Interacting);
}

void GameWorld::UpdateInteract(Entity& thief, uint32_t dtMs)
{
	Entity* target = Find(thief.interactTarget);
	if (target == nullptr || !InRange(thief, *target))
	{
		CancelInteract(thief);					// 범위를 벗어나면 중단
		return;
	}
	target->progressMs += static_cast<float>(dtMs) * WorkSpeed(thief);
	if (target->progressMs >= static_cast<float>(InteractDurationMs(*target)))
		CompleteInteract(thief, *target);
}

void GameWorld::CompleteInteract(Entity& thief, Entity& target)
{
	const uint32_t duration = InteractDurationMs(target);
	target.interactor = 0;
	thief.interactTarget = 0;
	ClearBits(thief.state, StateBit::Interacting);
	_events.push_back(Msg::Interact{ thief.id, target.id, InteractState::Complete, duration, duration });

	if (target.kind == EntityKind::Safe)
		OnSafeOpened(target, thief);
	else if (target.kind == EntityKind::Box)
		BreakBox(target);
}

uint32_t GameWorld::InteractDurationMs(const Entity& target) const
{
	switch (target.kind)
	{
	case EntityKind::Safe: return GameData::SafeOpenMs;
	case EntityKind::Box: return GameData::BoxOpenMs;
	default: return 0;
	}
}

bool GameWorld::InRange(const Entity& thief, const Entity& target) const
{
	// 바닥 위 거리(2D)만 본다: 맵의 상호작용 위치는 바닥 높이, 플레이어 위치는 캡슐 중심이라 z가 다르다
	return Dist2D(thief.pos, target.interactPos) <= target.interactRange;
}

float GameWorld::WorkSpeed(const Entity& thief) const
{
	return thief.HasPerk(GameData::Perk::Tools) ? GameData::WorkSpeedTools : 1.0f;	// K-03
}

// ───────────────── 반납 지점 (§11.8) ─────────────────

bool GameWorld::PlaceReturnPoint(Entity& point, std::optional<uint16_t> excludeRoom)
{
	// 후보 = 반납할 수 있는 방 − 빠질 방 − 이미 다른 반납 지점이 있는 방
	std::vector<uint16_t> candidates;
	for (size_t i = 0; i < _map->rooms.size(); ++i)
	{
		const uint16_t room = static_cast<uint16_t>(i);
		if (_map->rooms[i].returnCells.empty() || room == excludeRoom)
			continue;
		const bool taken = std::any_of(_entities.begin(), _entities.end(), [&](const Entity& e) {
			return &e != &point && e.kind == EntityKind::ReturnPoint && e.typeId == room;
		});
		if (!taken)
			candidates.push_back(room);
	}
	if (candidates.empty())
		return false;

	const uint16_t room = candidates[std::uniform_int_distribution<size_t>(0, candidates.size() - 1)(_rng)];
	const std::vector<Vec3>& cells = _map->rooms[room].returnCells;
	const Vec3 pos = cells[std::uniform_int_distribution<size_t>(0, cells.size() - 1)(_rng)];
	point.pos = pos;
	point.interactPos = pos;
	point.interactRange = GameData::ReturnPointRangeCm;
	point.typeId = room;
	point.spawnMs = _nowMs;
	ClearBits(point.state, StateBit::Warning);
	return true;
}

void GameWorld::ReplaceReturnPoint(Entity& point)
{
	// 반납했거나 60초가 지남: 옛 지점은 사라지고, 다른 방에 새 지점 (새 id로 같은 엔티티 칸을 다시 쓴다)
	const uint16_t oldId = point.id;
	const uint16_t oldRoom = point.typeId;
	if (!PlaceReturnPoint(point, oldRoom))
		PlaceReturnPoint(point, std::nullopt);
	point.id = _nextId++;
	_events.push_back(Msg::EntityDespawn{ { oldId } });
	_events.push_back(Msg::EntitySpawn{ { InfoOf(point) } });
}

void GameWorld::UpdateReturnPoint(Entity& point)
{
	const uint32_t age = _nowMs - point.spawnMs;
	if (age >= GameData::ReturnPointLifeMs)
	{
		ReplaceReturnPoint(point);
	}
	else if (age >= GameData::ReturnPointWarnMs && (point.state & StateBit::Warning) == 0)
	{
		point.state |= StateBit::Warning;
		point.dirty = true;
	}
}

// ───────────────── 시간 흐름 ─────────────────

void GameWorld::Update(float deltaSec)
{
	const uint32_t dtMs = static_cast<uint32_t>(std::lround(deltaSec * 1000.0f));
	_nowMs += dtMs;

	// ① 끈끈이·연막의 범위 표시(감속·연막 비트)와 만료 — 경비원 이동 속도와 시야가 이것을 본다
	UpdateEffects();
	// ② 경비원 AI: 돌고, 움직이고, 쏜다
	UpdateGuards(dtMs);

	// ③ 엔티티마다. 순회 중에는 넣거나 지우지 않는다 (Spawn은 큐, Remove는 표시만)
	for (Entity& e : _entities)
	{
		if (e.removed)
			continue;
		switch (e.kind)
		{
		case EntityKind::Thief: UpdateThief(e, deltaSec, dtMs); break;
		case EntityKind::Guard: UpdateGuardTimers(e); break;
		case EntityKind::ReturnPoint: UpdateReturnPoint(e); break;
		case EntityKind::Mine: UpdateMine(e); break;
		default: break;
		}
	}

	// ④ 날아가는 수류탄·섬광탄, 상자 다시 생기기, 필드 아이템 생성
	UpdateThrown();
	UpdateBoxRespawns();
	if (_nowMs >= _nextFieldItemMs)
	{
		UpdateFieldItems();
		_nextFieldItemMs = _nowMs + GameData::FieldItemEveryMs;
	}

	// ⑤ 모아 둔 엔티티 생성·삭제
	Flush();
}

void GameWorld::UpdateThief(Entity& thief, float deltaSec, uint32_t dtMs)
{
	if ((thief.state & StateBit::LeftGame) != 0)
		return;
	if ((thief.state & StateBit::Downed) != 0)
	{
		if (_nowMs >= thief.downedUntilMs)
			Respawn(thief);						// 5초 / 10초 뒤 시작 위치에서 (D-05, D-06)
		return;
	}

	// 시간이 다 된 상태 풀기
	if ((thief.state & StateBit::Invulnerable) != 0 && _nowMs >= thief.invulnUntilMs)
		ClearBits(thief.state, StateBit::Invulnerable);
	if ((thief.state & StateBit::Stunned) != 0 && _nowMs >= thief.stunUntilMs)
		ClearBits(thief.state, StateBit::Stunned);
	if ((thief.state & StateBit::Flashed) != 0 && _nowMs >= thief.flashedUntilMs)
		ClearBits(thief.state, StateBit::Flashed);

	UpdateStamina(thief, deltaSec);

	// 구르기: 0.5초 무적 (Dodging 비트)
	if (thief.action == ActionType::Dodge && _nowMs - thief.actionStartMs >= GameData::DodgeInvulnMs)
	{
		thief.action = ActionType::None;
		ClearBits(thief.state, StateBit::Dodging);
	}

	UpdateSkill(thief);			// 끈끈이 설치 지연, 은신 끝
	UpdateJetpack(thief);
	UpdateRailgun(thief);
	if (thief.interactTarget != 0)
		UpdateInteract(thief, dtMs);
	UpdateFiring(thief);		// 꾹 누르는 동안 간격마다 공격
	TouchFieldItems(thief);		// 닿으면 회복약 (I-15)
}

void GameWorld::UpdateStamina(Entity& thief, float deltaSec)
{
	// 0이 되면 탈진(달리기·구르기 불가, 회복 절반) → 최대치까지 차면 풀림 (P-03)
	if ((thief.state & StateBit::Running) != 0)
	{
		thief.stamina = std::max(0.0f, thief.stamina - GameData::StaminaRunCostPerSec * deltaSec);
		if (thief.stamina <= 0.0f)
		{
			ClearBits(thief.state, StateBit::Running);
			thief.state |= StateBit::Exhausted;
		}
		return;
	}
	const float scale = (thief.state & StateBit::Exhausted) != 0 ? GameData::ExhaustedRegenScale : 1.0f;
	thief.stamina = std::min(GameData::StaminaMax, thief.stamina + GameData::StaminaRegenPerSec * scale * deltaSec);
	if ((thief.state & StateBit::Exhausted) != 0 && thief.stamina >= GameData::StaminaMax)
		ClearBits(thief.state, StateBit::Exhausted);
}

void GameWorld::MarkLeft(uint64_t playerId)
{
	Entity* thief = ThiefOf(playerId);
	if (thief == nullptr)
		return;
	if (thief->interactTarget != 0)
		CancelInteract(*thief);
	// 들고 있던 환금 아이템은 그 자리에: 보물은 반납 말고는 사라지지 않는다 (I-24)
	if ((thief->state & StateBit::Downed) == 0 && !thief->carried.empty())
	{
		DropValuables(*thief, false);
		PushMoneyEvent();
	}
	thief->attackHeld = false;
	thief->action = ActionType::None;
	thief->state |= StateBit::LeftGame;
	ClearBits(thief->state, StateBit::Running | StateBit::Dodging | StateBit::Attacking | StateBit::Cloaked |
		StateBit::Interacting | StateBit::CarryingTreasure);
	Flush();
}

// ───────────────── 조회 ─────────────────

Entity* GameWorld::ThiefOf(uint64_t playerId)
{
	for (Entity& e : _entities)
		if (e.IsThief() && e.ownerPlayerId == playerId)
			return &e;
	return nullptr;
}

Entity* GameWorld::Find(uint16_t id)
{
	if (id == 0)
		return nullptr;
	for (Entity& e : _entities)
		if (e.id == id && !e.removed)
			return &e;
	return nullptr;
}

bool GameWorld::AnyThiefActive() const
{
	return std::any_of(_entities.begin(), _entities.end(),
		[](const Entity& e) { return e.IsThief() && (e.state & StateBit::LeftGame) == 0; });
}

// ───────────────── 메시지 만들기 ─────────────────

std::vector<WorldEvent> GameWorld::TakeEvents()
{
	std::vector<WorldEvent> events;
	events.swap(_events);
	return events;
}

Msg::PlayerStatus GameWorld::StatusOf(const Entity& thief) const
{
	Msg::PlayerStatus s;
	s.hp = static_cast<uint16_t>(std::max(0, thief.hp));
	s.shield = static_cast<uint16_t>(std::max(0, thief.shield));
	s.stamina = static_cast<uint8_t>(std::lround(thief.stamina));
	s.weapons[0] = thief.weapons[0];
	s.weapons[1] = thief.weapons[1];
	s.activeSlot = thief.activeSlot;
	s.consumable = thief.consumable;
	s.skill = thief.skill;
	s.skillCooldownMs = thief.skillReadyMs > _nowMs ? thief.skillReadyMs - _nowMs : 0;
	s.skillActiveMs = thief.skillActiveUntilMs > _nowMs ? thief.skillActiveUntilMs - _nowMs : 0;
	s.deathSaveLeft = thief.deathSaveLeft;
	return s;
}

std::vector<std::pair<uint64_t, Msg::PlayerStatus>> GameWorld::TakeStatuses()
{
	// 바뀐 사람만 (U-01, U-03). 처음 한 번은 모두
	std::vector<std::pair<uint64_t, Msg::PlayerStatus>> changed;
	for (Entity& e : _entities)
	{
		if (!e.IsThief() || (e.state & StateBit::LeftGame) != 0)
			continue;
		const Msg::PlayerStatus status = StatusOf(e);
		if (!e.lastStatus || !(*e.lastStatus == status))
		{
			e.lastStatus = status;
			changed.emplace_back(e.ownerPlayerId, status);
		}
	}
	return changed;
}

std::vector<Msg::EntityInfo> GameWorld::SpawnInfos() const
{
	std::vector<Msg::EntityInfo> infos;
	infos.reserve(_entities.size());
	for (const Entity& e : _entities)
		if (!e.removed)
			infos.push_back(InfoOf(e));
	return infos;
}

Msg::Snapshot GameWorld::MakeSnapshot(uint32_t tick, uint32_t remainingMs)
{
	// 움직이는 것(플레이어·경비원)은 매 틱, 오브젝트는 상태가 바뀐 틱에만 (psd 3.3)
	Msg::Snapshot snapshot;
	snapshot.tick = tick;
	snapshot.remainingMs = remainingMs;
	for (Entity& e : _entities)
	{
		if (e.removed || (!e.IsThief() && !e.IsGuard() && !e.dirty))
			continue;
		e.dirty = false;
		const uint16_t hp = static_cast<uint16_t>(std::max(0, e.hp));
		snapshot.entities.push_back({ e.id, e.state, e.pos, e.yaw, hp, e.action, e.IsThief() ? e.ActiveWeapon() : WeaponId::None });
	}
	return snapshot;
}

std::vector<Msg::PlayerResult> GameWorld::MakeResults() const
{
	std::vector<Msg::PlayerResult> results;
	for (const Entity& e : _entities)
	{
		if (!e.IsThief())
			continue;
		Msg::PlayerResult r;
		r.playerId = e.ownerPlayerId;
		r.name = e.name;
		r.characterId = e.typeId;
		r.leftEarly = (e.state & StateBit::LeftGame) != 0;
		r.banked = e.banked;
		r.kills = e.kills;
		r.downs = e.downs;
		r.treasuresDelivered = e.treasuresDelivered;
		results.push_back(std::move(r));
	}
	// 순위 = 반납 금액 내림차순. 같은 금액은 같은 순위 (G-07, A-20)
	for (Msg::PlayerResult& r : results)
		r.rank = static_cast<uint8_t>(1 + std::count_if(results.begin(), results.end(),
			[&](const Msg::PlayerResult& o) { return o.banked > r.banked; }));
	return results;
}

} // namespace Content
