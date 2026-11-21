#include "ServerCore/CorePch.h"
#include "Content/Game/GameWorld.h"

#include <algorithm>
#include <cmath>

#include "Content/GameData.h"
#include "Content/Game/WorldMath.h"

// 스킬·소비 아이템·상태이상 (docs/psd.md M5·M6).
//
// 스킬은 캐릭터 고유 (F-02, A-06): 도적1 사제 테이저건 · 도적2 끈끈이 · 도적3 은신 · 도적4·5 없음.
// 이동 속도 효과(끈끈이 감속·은신 −80%·운동화)는 플레이어 이동이 클라 몫이라 클라가 적용하고(T-64),
// 서버는 상태 비트(Slowed·Cloaked)만 알린다. 경비원 감속은 서버가 적용한다(GuardAI.cpp).

namespace Content {

using namespace WorldMath;

// ───────────────── 스킬 (§9) ─────────────────

void GameWorld::SetSkillEvent(const Entity& caster, SkillPhase phase, const Vec3& pos)
{
	_events.push_back(Msg::Skill{ caster.id, caster.skill, phase, pos });
}

std::optional<RejectReason> GameWorld::UseSkill(Entity& thief)
{
	if ((thief.state & (StateBit::RailgunLock | StateBit::Jetpack)) != 0)
		return RejectReason::Locked;

	switch (thief.skill)
	{
	case SkillId::None:
		return RejectReason::NotAvailable;		// 스킬이 정해지지 않은 캐릭터 (도적4·5)

	case SkillId::Cloak:
		if ((thief.state & StateBit::Cloaked) != 0)
		{
			EndCloak(thief);					// 다시 누르면 즉시 해제 (S-03)
			return std::nullopt;
		}
		if (_nowMs < thief.skillReadyMs)
			return RejectReason::Cooldown;
		if (thief.interactTarget != 0)
			CancelInteract(thief);
		thief.state |= StateBit::Cloaked;
		ClearBits(thief.state, StateBit::Running);
		thief.skillActiveUntilMs = _nowMs + GameData::CloakMs;
		SetSkillEvent(thief, SkillPhase::Active, thief.pos);
		return std::nullopt;

	case SkillId::Taser:
	{
		// 사제 테이저건: 3m 단일(히트 박스), 맞으면 2초 경직. 쿨타임은 쓴 직후부터 (S-01)
		if (_nowMs < thief.skillReadyMs)
			return RejectReason::Cooldown;
		thief.skillReadyMs = _nowMs + GameData::SkillCooldownMs;
		SetSkillEvent(thief, SkillPhase::Cast, thief.pos);
		const HitResult hit = RayHit(thief, thief.yaw, GameData::SkillTaserRangeCm, false);
		if (hit.target != nullptr)
			ApplyDamage(*hit.target, &thief, WeaponId::Taser, 0, GameData::StunMs);
		return std::nullopt;
	}

	case SkillId::Sticky:
		// 끈끈이: 1초 동안 던지는 동작 → 이동 방향 반대쪽 1m 뒤에 함정 (S-02)
		if (_nowMs < thief.skillReadyMs)
			return RejectReason::Cooldown;
		thief.skillReadyMs = _nowMs + GameData::SkillCooldownMs;
		thief.skillCastAtMs = _nowMs + GameData::StickyCastMs;
		SetSkillEvent(thief, SkillPhase::Cast, thief.pos);
		return std::nullopt;
	}
	return RejectReason::NotAvailable;
}

void GameWorld::UpdateSkill(Entity& thief)
{
	if (thief.skillCastAtMs != 0 && _nowMs >= thief.skillCastAtMs)
	{
		thief.skillCastAtMs = 0;
		const Vec3 pos = Ahead(thief.pos, thief.moveYaw + 180.0f, GameData::StickyBackCm);
		Entity& trap = Spawn(EntityKind::StickyTrap, pos);
		trap.ownerEntity = thief.id;
		trap.ownerPlayerId = thief.ownerPlayerId;
		trap.expireMs = _nowMs + GameData::StickyLifeMs;
		SetSkillEvent(thief, SkillPhase::Active, pos);
	}
	if ((thief.state & StateBit::Cloaked) != 0 && _nowMs >= thief.skillActiveUntilMs)
		EndCloak(thief);						// 20초가 지남
}

void GameWorld::EndCloak(Entity& thief)
{
	if ((thief.state & StateBit::Cloaked) == 0)
		return;
	ClearBits(thief.state, StateBit::Cloaked);
	thief.skillActiveUntilMs = 0;
	thief.skillReadyMs = _nowMs + GameData::SkillCooldownMs;	// 은신은 풀릴 때 쿨타임 시작 (S-03)
	SetSkillEvent(thief, SkillPhase::End, thief.pos);
}

// ───────────────── 소비 아이템 (§11.7) ─────────────────

std::optional<RejectReason> GameWorld::UseConsumable(Entity& thief, const InputCmd& input)
{
	if (thief.consumable == ConsumableId::None)
		return RejectReason::NotAvailable;		// 없으면 아무 일도 없음 (CT-08)
	if ((thief.state & (StateBit::RailgunLock | StateBit::Jetpack)) != 0)
		return RejectReason::Locked;

	switch (thief.consumable)
	{
	case ConsumableId::HealKit:
		if (thief.hp >= GameData::PlayerMaxHp)
			return RejectReason::HpFull;
		thief.hp = std::min(GameData::PlayerMaxHp, thief.hp + GameData::HealKitHp);
		break;

	case ConsumableId::Smoke:
	{
		// 사용자 중심 반지름 5m, 10초 (I-32)
		Entity& smoke = Spawn(EntityKind::Smoke, thief.pos);
		smoke.ownerEntity = thief.id;
		smoke.expireMs = _nowMs + GameData::SmokeLifeMs;
		break;
	}

	case ConsumableId::Flashbang:
		Throw(thief, ExplosionKind::Flashbang, input.aim);		// 마우스 방향 6m (I-33)
		break;

	case ConsumableId::Jetpack:
		// 3초 동안 마우스 방향 10m/s. 이동은 클라가, 부딪침은 서버가 (I-34, A-19)
		thief.jetpackUntilMs = _nowMs + GameData::JetpackMs;
		thief.jetpackHit.clear();
		thief.state |= StateBit::Jetpack;
		if (thief.interactTarget != 0)
			CancelInteract(thief);
		break;

	default:
		return RejectReason::NotAvailable;
	}
	thief.consumable = ConsumableId::None;
	return std::nullopt;
}

void GameWorld::UpdateJetpack(Entity& thief)
{
	if ((thief.state & StateBit::Jetpack) == 0)
		return;
	if (_nowMs >= thief.jetpackUntilMs)
	{
		ClearBits(thief.state, StateBit::Jetpack);
		return;
	}
	// 부딪친 플레이어는 3m 밀려남(그 클라가 이동), 경비원은 1초 경직. 대상마다 한 번
	for (Entity& e : _entities)
	{
		if (&e == &thief || (!e.IsThief() && !e.IsGuard()) || !IsTargetable(e, nullptr, false))
			continue;
		if (std::find(thief.jetpackHit.begin(), thief.jetpackHit.end(), e.id) != thief.jetpackHit.end())
			continue;
		if (Dist2D(e.pos, thief.pos) > GameData::BodyRadiusCm * 2.0f)
			continue;
		thief.jetpackHit.push_back(e.id);
		if (e.IsThief())
			_events.push_back(Msg::Knockback{ e.id, YawTo(thief.pos, e.pos), GameData::JetpackPushCm });
		else
			Stun(e, GameData::JetpackGuardStunMs);
	}
}

// ───────────────── 끈끈이·연막 범위 ─────────────────

void GameWorld::UpdateEffects()
{
	// 이번 틱의 범위 비트를 새로 계산한다 (Slowed: 끈끈이 위, InSmoke: 연막 안)
	for (Entity& e : _entities)
		if (e.IsThief() || e.IsGuard())
			ClearBits(e.state, StateBit::Slowed | StateBit::InSmoke);

	for (Entity& area : _entities)
	{
		if (area.removed || (area.kind != EntityKind::StickyTrap && area.kind != EntityKind::Smoke))
			continue;
		if (_nowMs >= area.expireMs)
		{
			Remove(area);
			continue;
		}
		const bool trap = area.kind == EntityKind::StickyTrap;
		const float radius = trap ? GameData::StickyRadiusCm : GameData::SmokeRadiusCm;
		for (Entity& e : _entities)
		{
			if ((!e.IsThief() && !e.IsGuard()) || (e.state & (StateBit::Downed | StateBit::LeftGame)) != 0)
				continue;
			if (Dist2D(e.pos, area.pos) <= radius)
				e.state |= trap ? StateBit::Slowed : StateBit::InSmoke;	// 끈끈이는 설치자도 (Q-36)
		}
	}
}

} // namespace Content
