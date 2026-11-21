#include "ServerCore/CorePch.h"
#include "Content/Game/GameWorld.h"

#include <algorithm>
#include <cmath>

#include "Content/GameData.h"
#include "Content/Game/Sight.h"
#include "Content/Game/WorldMath.h"

// 전투 (docs/psd.md M1·M4): 판정 도구, 무기 발사, 대미지, 쓰러짐·리스폰, 무기 칸.
//
// 판정 모양 (A-10~A-13)
//   근접·화염방사기: 시선 방향 부채꼴 안의 대상 모두 (근접 좌우 60°, 화염방사기 좌우 15°)
//   권총·테이저·기관총: 시선 방향 직선, 처음 맞은 대상 하나 (투사체 없이 히트 박스로 바로 판정)
//   레일건: 폭 50cm 빔, 벽을 무시하고 닿는 대상 모두
//   폭발: 중심에서 반지름 안, 거리에 따라 대미지가 줄어듦
// 레일건 말고는 벽에 막힌다: 쏜 사람 → 대상 사이를 HasLineOfSight로 검사 (A-11)

namespace Content {

using namespace WorldMath;

namespace {

float RadiusOf(const Entity& e) { return e.kind == EntityKind::Box ? GameData::BoxRadiusCm : GameData::BodyRadiusCm; }

bool HeightOk(const Entity& a, const Entity& b) { return std::fabs(a.pos.z - b.pos.z) <= GameData::HitHeightCm; }

} // namespace

// ───────────────── 판정 도구 ─────────────────

bool GameWorld::IsTargetable(const Entity& e, const Entity* shooter, bool guardShot) const
{
	if (e.removed || &e == shooter)
		return false;
	switch (e.kind)
	{
	case EntityKind::Thief:
		// 쓰러짐·이탈·구르기 무적·리스폰 무적은 맞지 않는다 (C-06)
		return (e.state & (StateBit::Downed | StateBit::LeftGame | StateBit::Dodging | StateBit::Invulnerable)) == 0;
	case EntityKind::Guard:
		return !guardShot && (e.state & StateBit::Repairing) == 0;	// 경비원 총알은 다른 경비원에 맞지 않음 (A-18)
	case EntityKind::Box:
		return !guardShot;			// 경비원 공격은 플레이어에게만 (N-09)
	default:
		return false;
	}
}

bool GameWorld::Blocked(const Vec3& from, const Vec3& to) const
{
	if (_map == nullptr)
		return false;
	const Vec3 a{ from.x, from.y, from.z + GameData::ShotHeightCm };
	const Vec3 b{ to.x, to.y, to.z + GameData::ShotHeightCm };
	return !HasLineOfSight(*_map, a, b);
}

GameWorld::HitResult GameWorld::RayHit(const Entity& shooter, float yaw, float range, bool guardShot)
{
	// 직선 위에서 가장 가까운 대상 (원기둥과 직선의 거리로 판정). 그 대상까지 벽이 있으면 아무도 안 맞음
	const float r = yaw * Pi / 180.0f;
	const float dx = std::cos(r), dy = std::sin(r);
	HitResult best;
	float bestT = 0.0f;
	for (Entity& e : _entities)
	{
		if (!IsTargetable(e, &shooter, guardShot) || !HeightOk(shooter, e))
			continue;
		const float rx = e.pos.x - shooter.pos.x, ry = e.pos.y - shooter.pos.y;
		const float t = rx * dx + ry * dy;				// 직선 방향으로 얼마나 앞에 있나
		const float radius = RadiusOf(e);
		if (t <= 0.0f || t > range + radius)
			continue;
		const float perp2 = rx * rx + ry * ry - t * t;	// 직선에서 떨어진 거리²
		if (perp2 > radius * radius)
			continue;
		if (best.target == nullptr || t < bestT)
		{
			best.target = &e;
			bestT = t;
		}
	}
	if (best.target != nullptr && Blocked(shooter.pos, best.target->pos))
		return {};
	best.distance = bestT;
	return best;
}

std::vector<Entity*> GameWorld::ConeHits(const Entity& shooter, float yaw, float range, float halfAngleDeg)
{
	std::vector<Entity*> hits;
	for (Entity& e : _entities)
	{
		if (!IsTargetable(e, &shooter, false) || !HeightOk(shooter, e))
			continue;
		const float d = Dist2D(shooter.pos, e.pos);
		if (d > range + RadiusOf(e))
			continue;
		if (d > RadiusOf(e) && std::fabs(AngleDiff(YawTo(shooter.pos, e.pos), yaw)) > halfAngleDeg)
			continue;			// 거의 겹쳐 있으면 방향과 상관없이 맞음
		if (Blocked(shooter.pos, e.pos))
			continue;
		hits.push_back(&e);
	}
	return hits;
}

std::vector<Entity*> GameWorld::BeamHits(const Entity& shooter, float yaw, float range)
{
	// 레일건: 폭 50cm 빔. 벽은 무시한다 (W-17)
	const float r = yaw * Pi / 180.0f;
	const float dx = std::cos(r), dy = std::sin(r);
	std::vector<Entity*> hits;
	for (Entity& e : _entities)
	{
		if (!IsTargetable(e, &shooter, false) || !HeightOk(shooter, e))
			continue;
		const float rx = e.pos.x - shooter.pos.x, ry = e.pos.y - shooter.pos.y;
		const float t = rx * dx + ry * dy;
		if (t <= 0.0f || t > range)
			continue;
		const float reach = GameData::RailgunHalfWidthCm + RadiusOf(e);
		if (rx * rx + ry * ry - t * t <= reach * reach)
			hits.push_back(&e);
	}
	return hits;
}

std::vector<Entity*> GameWorld::RadiusHits(const Vec3& center, float radius)
{
	std::vector<Entity*> hits;
	for (Entity& e : _entities)
	{
		if (!IsTargetable(e, nullptr, false))
			continue;
		if (Dist2D(center, e.pos) > radius || std::fabs(center.z - e.pos.z) > GameData::HitHeightCm)
			continue;
		if (Blocked(center, e.pos))
			continue;			// 벽 너머는 안전 (A-11)
		hits.push_back(&e);
	}
	return hits;
}

// ───────────────── 무기 발사 ─────────────────

void GameWorld::UpdateFiring(Entity& thief)
{
	if (!thief.attackHeld || !CanAct(thief) || (thief.state & (StateBit::RailgunLock | StateBit::Jetpack)) != 0)
		return;
	// 오래 멈췄다가(경직 등) 다시 쏠 때 밀린 공격을 한꺼번에 쏘지 않게
	thief.nextFireMs = std::max(thief.nextFireMs, _nowMs > 50 ? _nowMs - 50 : 0);

	// 간격이 틱보다 짧은 무기(화염방사기 33ms)는 한 틱에 여러 번 쏜다
	for (size_t shot = 0; shot < GameData::MaxShotsPerTick && thief.attackHeld && _nowMs >= thief.nextFireMs; ++shot)
	{
		const GameData::WeaponType* type = GameData::WeaponOf(thief.ActiveWeapon());
		if (type == nullptr)
			return;
		thief.nextFireMs += type->intervalMs;
		FireWeapon(thief);
		if ((thief.state & (StateBit::RailgunLock | StateBit::Downed)) != 0)
			return;
	}
}

void GameWorld::FireWeapon(Entity& thief)
{
	const WeaponId weapon = thief.ActiveWeapon();
	const GameData::WeaponType* type = GameData::WeaponOf(weapon);
	if (type == nullptr)
		return;
	const float yaw = thief.yaw;
	const float range = type->rangeCm > 0.0f ? type->rangeCm : MaxRangeCm();
	Msg::Fire fire{ thief.id, weapon, thief.pos, yaw, 0 };

	switch (type->hit)
	{
	case GameData::WeaponHit::MeleeCone:
	case GameData::WeaponHit::Cone:
	{
		// 부채꼴 안의 대상 모두 (근접 120°, 화염방사기 30°)
		const std::vector<Entity*> hits = ConeHits(thief, yaw, range, type->halfAngleDeg);
		if (!hits.empty())
			fire.hitEntity = hits.front()->id;
		_events.push_back(fire);
		for (Entity* target : hits)
			ApplyDamage(*target, &thief, weapon, type->damage, type->stunMs);
		break;
	}

	case GameData::WeaponHit::Ray:
	{
		// 처음 맞은 대상 하나 (권총·테이저건)
		const HitResult hit = RayHit(thief, yaw, range, false);
		if (hit.target != nullptr)
			fire.hitEntity = hit.target->id;
		_events.push_back(fire);
		if (hit.target != nullptr)
			ApplyDamage(*hit.target, &thief, weapon, type->damage, type->stunMs);
		break;
	}

	case GameData::WeaponHit::Beam:
		// 레일건: 1초 동안 0·200·…·1000ms에 판정, 그동안 이동·회전 잠금 (W-17)
		_events.push_back(fire);
		thief.railgunYaw = yaw;
		thief.railgunLeft = GameData::RailgunHits;
		thief.railgunNextMs = _nowMs;
		thief.railgunUntilMs = _nowMs + GameData::RailgunLockMs;
		thief.state |= StateBit::RailgunLock;
		UseAmmo(thief);
		UpdateRailgun(thief);		// 쏜 직후 판정
		return;

	case GameData::WeaponHit::Thrown:
		_events.push_back(fire);
		Throw(thief, ExplosionKind::Grenade, thief.aim);
		break;

	case GameData::WeaponHit::Mine:
		PlaceMine(thief, weapon);
		break;
	}
	UseAmmo(thief);
}

void GameWorld::UpdateRailgun(Entity& thief)
{
	while (thief.railgunLeft > 0 && _nowMs >= thief.railgunNextMs)
	{
		for (Entity* target : BeamHits(thief, thief.railgunYaw, MaxRangeCm()))
			ApplyDamage(*target, &thief, WeaponId::Railgun, GameData::WeaponOf(WeaponId::Railgun)->damage, 0);
		--thief.railgunLeft;
		thief.railgunNextMs += GameData::RailgunEveryMs;
	}
	if (thief.railgunLeft == 0 && (thief.state & StateBit::RailgunLock) != 0 && _nowMs >= thief.railgunUntilMs)
		ClearBits(thief.state, StateBit::RailgunLock);
}

// ───────────────── 무기 칸 (W-01~W-10) ─────────────────

void GameWorld::UseAmmo(Entity& thief)
{
	const uint8_t slot = thief.activeSlot;
	const GameData::WeaponType* type = GameData::WeaponOf(thief.weapons[slot].id);
	if (type == nullptr || type->ammo == 0)
		return;					// 탄 무한 (삼단봉·도검)
	if (thief.weapons[slot].ammo > 0)
		--thief.weapons[slot].ammo;
	if (thief.weapons[slot].ammo == 0)
		RemoveWeapon(thief, slot);	// 다 쓰면 자동으로 사라짐 (W-06)
}

void GameWorld::RemoveWeapon(Entity& thief, uint8_t slot)
{
	// 빈 칸은 삼단봉이 채운다 (W-09). 삼단봉은 최대 1개라 이미 있으면 빈 칸으로 두고 삼단봉을 든다 (W-10)
	const uint8_t other = static_cast<uint8_t>(slot ^ 1);
	if (thief.weapons[other].id == WeaponId::Baton)
	{
		thief.weapons[slot] = {};
		thief.activeSlot = other;
	}
	else
	{
		thief.weapons[slot] = { WeaponId::Baton, 0 };
	}
}

void GameWorld::ResetLoadout(Entity& thief)
{
	// 게임 시작·리스폰 상태 (D-07): HP·SP 가득, 삼단봉만, 스킬 쿨타임 초기화, 퍽 다시 충전
	thief.hp = GameData::PlayerMaxHp;
	thief.stamina = GameData::StaminaMax;
	thief.shield = thief.HasPerk(GameData::Perk::Vest) ? GameData::VestShield : 0;
	thief.deathSaveLeft = thief.HasPerk(GameData::Perk::DeathSave);
	thief.weapons = { Msg::WeaponSlot{ WeaponId::Baton, 0 }, Msg::WeaponSlot{} };
	thief.activeSlot = 0;
	thief.consumable = ConsumableId::None;
	thief.attackHeld = false;
	thief.action = ActionType::None;
	thief.skillReadyMs = 0;
	thief.skillActiveUntilMs = 0;
	thief.skillCastAtMs = 0;
	thief.railgunLeft = 0;
	thief.jetpackUntilMs = 0;
	thief.jetpackHit.clear();
	thief.stunUntilMs = 0;
	thief.flashedUntilMs = 0;
	thief.state &= StateBit::LeftGame;		// 상태이상 모두 제거
}

// ───────────────── 대미지 ─────────────────

void GameWorld::Stun(Entity& target, uint32_t ms)
{
	target.stunUntilMs = std::max(target.stunUntilMs, _nowMs + ms);
	target.state |= StateBit::Stunned;
	if (target.IsThief() && target.interactTarget != 0)
		CancelInteract(target);
}

void GameWorld::ApplyDamage(Entity& target, Entity* attacker, WeaponId weapon, int32_t amount, uint32_t stunMs)
{
	const uint16_t attackerId = attacker != nullptr ? attacker->id : 0;
	switch (target.kind)
	{
	case EntityKind::Thief:
	{
		if (!IsTargetable(target, nullptr, false))
			return;
		if ((target.state & StateBit::Cloaked) != 0)
			EndCloak(target);					// 공격받으면 은신이 풀림 (Q-30)
		if (target.interactTarget != 0)
			CancelInteract(target);				// 공격받으면 금고 열기·상자 열기가 멈춤 (IN-05)
		// 무적 → 보호막 → HP → 가시회생 → 쓰러짐 (C-05, K-01, K-02)
		int32_t left = amount;
		const int32_t absorbed = std::min(target.shield, left);
		target.shield -= absorbed;
		left -= absorbed;
		target.hp -= left;
		if (stunMs > 0)
			Stun(target, stunMs);
		_events.push_back(Msg::Damage{ attackerId, target.id, weapon, static_cast<uint16_t>(amount),
			static_cast<uint16_t>(std::max(0, target.hp)), static_cast<uint16_t>(target.shield) });
		if (target.hp <= 0)
		{
			if (target.deathSaveLeft)
			{
				target.hp = 1;
				target.deathSaveLeft = false;
			}
			else
			{
				Down(target, attacker, attacker != nullptr && attacker->IsGuard() ? DownCause::Guard : DownCause::Player);
			}
		}
		return;
	}

	case EntityKind::Guard:
		if ((target.state & StateBit::Repairing) != 0)
			return;								// 자가 수리 중 무적 (N-06)
		target.hp -= amount;
		if (stunMs > 0)
			Stun(target, stunMs);
		_events.push_back(Msg::Damage{ attackerId, target.id, weapon, static_cast<uint16_t>(amount),
			static_cast<uint16_t>(std::max(0, target.hp)), 0 });
		if (attacker != nullptr && attacker->IsThief())
			OnGuardAttacked(target, *attacker);	// 공격자로 타겟 변경 (N-15)
		if (target.hp <= 0)
			StartRepair(target);
		return;

	case EntityKind::Box:
		target.hp -= amount;
		target.dirty = true;
		_events.push_back(Msg::Damage{ attackerId, target.id, weapon, static_cast<uint16_t>(amount),
			static_cast<uint16_t>(std::max(0, target.hp)), 0 });
		if (target.hp <= 0)
			BreakBox(target);					// 대미지 20 누적이면 부서짐 (O-11, A-02)
		return;

	default:
		return;
	}
}

void GameWorld::StartRepair(Entity& guard)
{
	guard.hp = 0;
	guard.state |= StateBit::Repairing | StateBit::Invulnerable;
	ClearBits(guard.state, StateBit::Attacking | StateBit::Running);
	guard.repairUntilMs = _nowMs + GameData::GuardRepairMs;
	guard.dirty = true;
}

// ───────────────── 쓰러짐·리스폰 (§7) ─────────────────

void GameWorld::Down(Entity& thief, Entity* attacker, DownCause cause)
{
	if (thief.interactTarget != 0)
		CancelInteract(thief);

	// 반납 금액 손실: 플레이어에게 10%, 경비원에게 40%. 잃은 금액은 그냥 사라짐 (D-04, Q-03)
	const uint32_t pct = cause == DownCause::Guard ? GameData::LossPctByGuard : GameData::LossPctByPlayer;
	const uint32_t lost = thief.banked * pct / 100;
	thief.banked -= lost;

	// 환금 아이템: 플레이어에게 → 그 자리에 모두 떨어뜨림, 경비원에게 → 사라지고 보물만 떨어뜨림 (D-02)
	DropValuables(thief, cause == DownCause::Guard);
	// 무기는 삼단봉 외 사라짐, 소비 아이템도 사라짐 (D-03, D-08). 리스폰 때 ResetLoadout이 삼단봉을 쥐여 준다
	thief.weapons = {};
	thief.consumable = ConsumableId::None;
	thief.attackHeld = false;
	thief.action = ActionType::None;
	thief.railgunLeft = 0;
	thief.jetpackUntilMs = 0;
	thief.skillCastAtMs = 0;
	thief.skillActiveUntilMs = 0;
	thief.hp = 0;
	thief.state = (thief.state & StateBit::LeftGame) | StateBit::Downed;

	const uint32_t wait = cause == DownCause::Guard ? GameData::RespawnByGuardMs : GameData::RespawnByPlayerMs;
	thief.downedUntilMs = _nowMs + wait;
	thief.downCause = cause;
	++thief.downs;
	if (attacker != nullptr && attacker->IsThief() && attacker != &thief)
		++attacker->kills;

	_events.push_back(Msg::Downed{ thief.id, attacker != nullptr ? attacker->id : uint16_t{ 0 }, cause, lost, wait });
	PushMoneyEvent();
}

void GameWorld::Respawn(Entity& thief)
{
	// 방 안 무작위 칸 (D-06, A-26). 위치는 클라가 정하므로 SC_RESPAWNED의 pos로 옮기라고 알린다
	if (_map != nullptr)
	{
		thief.pos = ThiefSpot(&thief, {}).pos;
		thief.yaw = std::uniform_real_distribution<float>(-180.0f, 180.0f)(_rng);
	}
	ResetLoadout(thief);
	thief.state |= StateBit::Invulnerable;
	thief.invulnUntilMs = _nowMs + GameData::RespawnInvulnMs;
	_events.push_back(Msg::Respawned{ thief.id, thief.pos, GameData::RespawnInvulnMs });
}

} // namespace Content
