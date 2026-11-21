#pragma once

// 보내는 메시지(Msg::*): 콘텐츠가 만들어 IMessageEncoder에 넘기는 순수 구조체.
// 브리지(MessageEncoder)가 같은 이름의 SC_ 패킷으로 바꾼다. 배열은 패킷의 상한까지만 보낸다.
// 프로토콜 v3.0 (docs/psd.md 3.4)

#include <cstdint>
#include <string>
#include <vector>

#include "Content/Messages/Types.h"

namespace Content::Msg {

// ── 입장·시스템 ──
struct LoginResult								// SC_LOGIN
{
	Result result = Result::Ok;
	uint64_t playerId = 0;
	std::string name;				// 로비가 정리한 닉네임
	// server_time_ms는 넣지 않는다: 서버 시각은 브리지가 보내는 순간에 채운다(콘텐츠의 관심사가 아님)
};

struct Notice { std::string message; };			// SC_NOTICE

// ── 로비·방 ──
struct RoomSummary
{
	uint32_t roomId = 0;
	std::string name;
	std::string hostName;
	uint8_t playerCount = 0;
	RoomState state = RoomState::Waiting;
};

struct RoomList { std::vector<RoomSummary> rooms; };					// SC_ROOM_LIST
struct CreateRoomResult { Result result = Result::Ok; uint32_t roomId = 0; };	// SC_CREATE_ROOM
struct JoinRoomResult { Result result = Result::Ok; uint32_t roomId = 0; };		// SC_JOIN_ROOM
struct LeftRoom { LeaveReason reason = LeaveReason::Self; };			// SC_LEAVE_ROOM

struct RoomMember
{
	uint64_t playerId = 0;
	std::string name;
	uint16_t characterId = 0;		// 0 = 미선택
	std::vector<uint16_t> perks;
	bool ready = false;
};

struct RoomInfo									// SC_ROOM_STATE
{
	uint32_t roomId = 0;
	std::string name;
	uint64_t hostPlayerId = 0;
	RoomState state = RoomState::Waiting;
	std::vector<RoomMember> members;
};

struct RoomError { RoomRequest request = RoomRequest::CreateRoom; Result result = Result::Ok; };	// SC_ROOM_ERROR

// ── 게임 진행 ──
struct SpawnAssign { uint64_t playerId = 0; uint8_t spawnIndex = 0; };

struct GameLoad									// SC_GAME_LOAD
{
	uint64_t matchId = 0;
	uint32_t mapId = 0;
	uint32_t mapVersion = 0;		// 클라가 같은 맵 파일인지 확인
	std::vector<SpawnAssign> spawns;
	uint32_t loadTimeoutMs = 0;
};

struct EntityInfo								// 처음 나타날 때의 정보
{
	uint16_t entityId = 0;
	EntityKind kind = EntityKind::Thief;
	uint16_t typeId = 0;
	uint64_t ownerPlayerId = 0;
	Vec3 pos;
	float yaw = 0.0f;
	uint32_t state = 0;				// StateBit 비트
	uint32_t timerMs = 0;			// 남은 수명 (반납 지점·끈끈이·연막), 없으면 0 (O-25)
	uint32_t param = 0;				// 환금 아이템 가치 / 떨어진 무기의 남은 탄 (U-09, A-22)
};

struct GameStart								// SC_GAME_START (받는 사람마다 selfEntityId가 다름)
{
	uint32_t durationMs = 0;
	uint8_t tickHz = 0;
	uint16_t selfEntityId = 0;
	std::vector<EntityInfo> entities;
	// start_server_time_ms는 브리지가 보내는 순간의 서버 시각으로 채운다
};

struct EntityState								// 스냅샷 한 줄
{
	uint16_t entityId = 0;
	uint32_t state = 0;
	Vec3 pos;
	float yaw = 0.0f;
	uint16_t hp = 0;				// 플레이어·경비원·상자. 그 밖은 0
	ActionType action = ActionType::None;
	WeaponId weapon = WeaponId::None;	// 들고 있는 무기 (애니메이션)
};

struct Snapshot { uint32_t tick = 0; uint32_t remainingMs = 0; std::vector<EntityState> entities; };	// SC_SNAPSHOT
struct ActionReject { uint16_t seq = 0; ActionType action = ActionType::None; RejectReason reason = RejectReason::InvalidState; };	// SC_ACTION_REJECT
struct EntitySpawn { std::vector<EntityInfo> entities; };		// SC_ENTITY_SPAWN
struct EntityDespawn { std::vector<uint16_t> entityIds; };		// SC_ENTITY_DESPAWN

struct PlayerResult
{
	uint64_t playerId = 0;
	std::string name;
	uint16_t characterId = 0;
	bool leftEarly = false;
	uint32_t banked = 0;			// 반납 금액 = 점수 (G-07, F-08)
	uint8_t rank = 0;				// 1부터. 같은 금액은 같은 순위 (A-20)
	uint32_t bestRecord = 0;		// 이 판을 반영한 최고 기록
	bool newRecord = false;			// 이 판으로 최고 기록을 갱신했나 (U-08)
	uint16_t kills = 0;
	uint16_t downs = 0;
	uint8_t treasuresDelivered = 0;
};

struct GameEnd									// SC_GAME_END
{
	uint64_t matchId = 0;
	EndReason reason = EndReason::TimeUp;
	uint32_t durationMs = 0;
	std::vector<PlayerResult> players;
	uint32_t resultScreenMs = 0;
};

// ── 게임 이벤트 (entity_id로 가리킨다) ──
struct Fire { uint16_t shooter = 0; WeaponId weapon = WeaponId::None; Vec3 origin; float yaw = 0.0f; uint16_t hitEntity = 0; };	// SC_FIRE
struct Damage { uint16_t attacker = 0; uint16_t target = 0; WeaponId weapon = WeaponId::None; uint16_t amount = 0; uint16_t hpAfter = 0; uint16_t shieldAfter = 0; };	// SC_DAMAGE
struct Downed { uint16_t victim = 0; uint16_t attacker = 0; DownCause cause = DownCause::Player; uint32_t lostBanked = 0; uint32_t respawnMs = 0; };	// SC_DOWNED
struct Respawned { uint16_t thief = 0; Vec3 pos; uint32_t invulnMs = 0; };	// SC_RESPAWNED
struct Projectile { uint16_t thrower = 0; ExplosionKind kind = ExplosionKind::Grenade; Vec3 from; Vec3 to; uint32_t flightMs = 0; };	// SC_PROJECTILE

struct Interact									// SC_INTERACT
{
	uint16_t actor = 0;
	uint16_t target = 0;
	InteractState state = InteractState::Begin;
	uint32_t progressMs = 0;
	uint32_t durationMs = 0;
};

struct SafeOpened { uint16_t safe = 0; uint16_t opener = 0; };						// SC_SAFE_OPENED

struct ThiefMoney { uint16_t thief = 0; uint32_t carried = 0; uint32_t banked = 0; };
struct Money { std::vector<ThiefMoney> thieves; };									// SC_MONEY

struct Explosion { ExplosionKind kind = ExplosionKind::Grenade; Vec3 pos; float radius = 0.0f; };	// SC_EXPLOSION
struct TreasureTaken { uint16_t thief = 0; Vec3 pos; uint32_t value = 0; };			// SC_TREASURE_TAKEN (전원 메시지, K-05)
struct GuardAlert { uint16_t guard = 0; AlertLevel level = AlertLevel::Patrol; uint16_t target = 0; };	// SC_GUARD_ALERT
struct Knockback { uint16_t target = 0; float yaw = 0.0f; float distance = 0.0f; };	// SC_KNOCKBACK (A-19)

struct WeaponSlot { WeaponId id = WeaponId::None; uint16_t ammo = 0; };
struct PlayerStatus								// SC_PLAYER_STATUS (본인에게만, 바뀔 때만)
{
	uint16_t hp = 0;
	uint16_t shield = 0;
	uint8_t stamina = 0;			// 0~100 (정수로 반올림)
	WeaponSlot weapons[2];
	uint8_t activeSlot = 0;
	ConsumableId consumable = ConsumableId::None;
	SkillId skill = SkillId::None;
	uint32_t skillCooldownMs = 0;	// 남은 쿨타임
	uint32_t skillActiveMs = 0;		// 남은 지속 시간 (은신)
	bool deathSaveLeft = false;		// 가시회생 남음

	bool operator==(const PlayerStatus& o) const
	{
		return hp == o.hp && shield == o.shield && stamina == o.stamina && weapons[0].id == o.weapons[0].id &&
			weapons[0].ammo == o.weapons[0].ammo && weapons[1].id == o.weapons[1].id && weapons[1].ammo == o.weapons[1].ammo &&
			activeSlot == o.activeSlot && consumable == o.consumable && skill == o.skill &&
			skillCooldownMs / 1000 == o.skillCooldownMs / 1000 && skillActiveMs / 1000 == o.skillActiveMs / 1000 &&
			deathSaveLeft == o.deathSaveLeft;		// 시간은 초 단위로만 비교 (매 틱 보내지 않게)
	}
};

struct Skill { uint16_t caster = 0; SkillId skill = SkillId::None; SkillPhase phase = SkillPhase::Cast; Vec3 pos; };	// SC_SKILL
struct Pickup { uint16_t thief = 0; EntityKind kind = EntityKind::Valuable; uint16_t typeId = 0; uint32_t param = 0; };	// SC_PICKUP

} // namespace Content::Msg
