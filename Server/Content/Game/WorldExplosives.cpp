#include "ServerCore/CorePch.h"
#include "Content/Game/GameWorld.h"

#include <algorithm>
#include <cmath>

#include "Content/GameData.h"
#include "Content/Game/WorldMath.h"

// 폭발물 (docs/psd.md T-45·T-46·T-51): 투척물(수류탄·섬광탄), 지뢰(폭발·감전), 폭발.
//
// 투척물은 엔티티가 아니다: 서버는 _thrown 목록으로 1초 비행을 따라가며 벽·사람에 닿는지만 보고,
// 클라는 SC_PROJECTILE(출발·도착·비행 시간)로 포물선을 그린다. 터지면 SC_EXPLOSION.
// 지뢰는 엔티티다(맵에 남아 있어야 보이므로): 설치 3초 뒤 작동 → 1m 안에 들어오면 감지 → 0.5초 경고 → 폭발.

namespace Content {

using namespace WorldMath;

namespace {

// 거리 d(cm)에 따른 대미지 (A-15): 중심 구간 안은 최대, 그 뒤 1m마다 감소
int32_t FalloffDamage(float d, float fullCm, int32_t full, int32_t perMeter, int32_t minimum)
{
	if (d <= fullCm)
		return full;
	const int32_t steps = static_cast<int32_t>(std::ceil((d - fullCm) / 100.0f));
	return std::max(minimum, full - perMeter * steps);
}

} // namespace

void GameWorld::Throw(Entity& thrower, ExplosionKind kind, const Vec3& aim)
{
	// 커서 지점 쪽으로 최대 6m (조준이 없으면 시선 방향 6m)
	Vec3 to;
	const bool noAim = aim.x == 0.0f && aim.y == 0.0f && aim.z == 0.0f;
	const float dist = noAim ? 0.0f : Dist2D(thrower.pos, aim);
	if (noAim || dist < 1.0f)
		to = Ahead(thrower.pos, thrower.yaw, GameData::ThrowMaxCm);
	else
		to = Ahead(thrower.pos, YawTo(thrower.pos, aim), std::min(dist, GameData::ThrowMaxCm));
	to.z = thrower.pos.z;

	_thrown.push_back({ thrower.id, kind, thrower.pos, to, thrower.pos, _nowMs });
	_events.push_back(Msg::Projectile{ thrower.id, kind, thrower.pos, to, GameData::ThrowFlightMs });
}

void GameWorld::UpdateThrown()
{
	for (size_t i = 0; i < _thrown.size();)
	{
		Thrown& t = _thrown[i];
		const float k = std::min(1.0f, static_cast<float>(_nowMs - t.startMs) / static_cast<float>(GameData::ThrowFlightMs));
		const Vec3 now{ t.from.x + (t.to.x - t.from.x) * k, t.from.y + (t.to.y - t.from.y) * k, t.from.z };

		// 벽에 닿으면 닿기 전 자리, 사람(던진 사람 제외)에 닿으면 그 자리, 아니면 도착 지점에서 (W-18)
		std::optional<Vec3> boom;
		if (Blocked(t.last, now))
			boom = t.last;
		else
		{
			for (const Entity& e : _entities)
			{
				if ((e.IsThief() || e.IsGuard()) && e.id != t.owner && IsTargetable(e, nullptr, false) &&
					Dist2D(e.pos, now) <= GameData::BodyRadiusCm)
				{
					boom = now;
					break;
				}
			}
		}
		if (!boom && k >= 1.0f)
			boom = t.to;

		if (boom)
		{
			const Thrown done = t;
			_thrown.erase(_thrown.begin() + static_cast<std::ptrdiff_t>(i));
			Explode(done.kind, *boom, done.owner);	// 폭발이 새 투척물을 만들지 않으므로 목록은 그대로 안전
			continue;
		}
		t.last = now;
		++i;
	}
}

void GameWorld::Explode(ExplosionKind kind, const Vec3& pos, uint16_t ownerId)
{
	Entity* owner = Find(ownerId);
	float radius = 0.0f;
	switch (kind)
	{
	case ExplosionKind::Grenade:
		// 반지름 6m, 2m까지 120 → 1m마다 −20. 던진 사람은 피해 없음 (W-18)
		radius = GameData::GrenadeRadiusCm;
		for (Entity* e : RadiusHits(pos, radius))
			if (e->id != ownerId)
				ApplyDamage(*e, owner, WeaponId::Grenade, FalloffDamage(Dist2D(pos, e->pos), 200.0f, 120, 20, 0), 0);
		break;

	case ExplosionKind::BlastMine:
		// 반지름 3m, 1m까지 80 → 1m마다 −30. 설치한 사람도 맞음 (W-19, Q-26)
		radius = GameData::MineRadiusCm;
		for (Entity* e : RadiusHits(pos, radius))
			ApplyDamage(*e, owner, WeaponId::BlastMine, FalloffDamage(Dist2D(pos, e->pos), 100.0f, 80, 30, 0), 0);
		break;

	case ExplosionKind::ShockMine:
		// 반지름 3m, 1m까지 20 → 1m마다 −10, 최소 1, 경직 2초 (W-20)
		radius = GameData::MineRadiusCm;
		for (Entity* e : RadiusHits(pos, radius))
			ApplyDamage(*e, owner, WeaponId::ShockMine, FalloffDamage(Dist2D(pos, e->pos), 100.0f, 20, 10, 1), GameData::StunMs);
		break;

	case ExplosionKind::Flashbang:
		// 반지름 3m. 플레이어: 3초 섬광, 경비원: 2초 경직. 사용자는 영향 없음 (I-33)
		radius = GameData::FlashRadiusCm;
		for (Entity* e : RadiusHits(pos, radius))
		{
			if (e->id == ownerId)
				continue;
			if (e->IsThief())
			{
				e->flashedUntilMs = _nowMs + GameData::FlashPlayerMs;
				e->state |= StateBit::Flashed;
			}
			else if (e->IsGuard())
			{
				Stun(*e, GameData::StunMs);
			}
		}
		break;
	}
	_events.push_back(Msg::Explosion{ kind, pos, radius });
}

void GameWorld::PlaceMine(Entity& thief, WeaponId mine)
{
	// 발밑에 설치, 3초 뒤 작동 (W-19, W-20)
	Entity& m = Spawn(EntityKind::Mine, thief.pos, static_cast<uint16_t>(mine));
	m.ownerEntity = thief.id;
	m.ownerPlayerId = thief.ownerPlayerId;
	m.armAtMs = _nowMs + GameData::MineArmMs;
	_events.push_back(Msg::Fire{ thief.id, mine, thief.pos, thief.yaw, 0 });
}

void GameWorld::UpdateMine(Entity& mine)
{
	if ((mine.state & StateBit::Armed) == 0)
	{
		if (_nowMs < mine.armAtMs)
			return;
		mine.state |= StateBit::Armed;
		mine.dirty = true;
	}

	if (mine.triggerAtMs == 0)
	{
		// 1m 안에 플레이어(설치자 포함)나 경비원이 들어오면 감지 → 0.5초 경고음
		for (const Entity& e : _entities)
		{
			if ((e.IsThief() || e.IsGuard()) && IsTargetable(e, nullptr, false) && Dist2D(e.pos, mine.pos) <= GameData::MineDetectCm)
			{
				mine.triggerAtMs = _nowMs + GameData::MineWarnMs;
				mine.state |= StateBit::Triggered;
				mine.dirty = true;
				break;
			}
		}
		return;
	}
	if (_nowMs >= mine.triggerAtMs)
	{
		const ExplosionKind kind = static_cast<WeaponId>(mine.typeId) == WeaponId::ShockMine ? ExplosionKind::ShockMine : ExplosionKind::BlastMine;
		Remove(mine);
		Explode(kind, mine.pos, mine.ownerEntity);
	}
}

} // namespace Content
