#pragma once

// 「털어라! 금고!」 클라이언트–서버 프로토콜 v3.0 (경쟁 PvPvE, docs/psd.md 3장)
//
// - 모든 패킷: [size:uint16][type:uint16][body]. size는 헤더 포함 전체 길이, 리틀 엔디언.
// - size < 4 또는 size > MAX_PACKET_SIZE면 즉시 연결 종료.
// - 가변 길이 패킷(배열이 마지막 멤버)은 count개까지만 보낸다. size는 *_Size(count) 함수로 계산.
// - 문자열은 UTF-8, 널 종료. 좌표는 cm 3D(UE 기준 Z-up), 각도는 도(degree), 시간은 ms.
// - v2.1과 호환되지 않는다(enum 번호를 새로 매김). 클라와 서버를 함께 바꾼다.
// - 서버는 숨길 정보(퍽 없는 사람의 경비원·플레이어 위치, 은신·연막 안의 대상)도 모두 보낸다. 숨기는 것은 클라 (Q-15).

#include <cstddef>
#include <cstdint>

constexpr short PORT = 9000;
constexpr uint32_t PROTOCOL_VERSION = (3u << 16) | 0u;   // major 3, minor 0. 정확히 일치해야 접속
constexpr uint16_t MAX_PACKET_SIZE = 8192;

constexpr int MAX_NAME_LEN = 48;            // 닉네임 UTF-8 바이트 (한글 12자 = 36바이트)
constexpr int MAX_ROOM_NAME_LEN = 60;       // 방 이름 UTF-8 바이트 (한글 20자)
constexpr int MAX_MESSAGE_LEN = 200;        // SC_KICK / SC_NOTICE 문자열
constexpr int MAX_ROOM_PLAYERS = 3;         // 방장 포함
constexpr int MAX_PERKS = 3;
constexpr int MAX_ROOM_LIST = 20;           // SC_ROOM_LIST 한 번에 보내는 방 수
constexpr int MAX_ENTITIES = 128;           // 스냅샷·스폰 한 패킷의 엔티티 상한 (A-23)

constexpr int TICK_HZ = 20;                 // 서버 틱 = 스냅샷 주기
constexpr int MOVE_SEND_HZ = 20;            // 클라 CS_MOVE 최대 주기
constexpr int GAME_DURATION_MS = 300000;    // 5분 (G-04)
constexpr int LOAD_TIMEOUT_MS = 30000;
constexpr int RESULT_SCREEN_MS = 15000;     // 결과 화면 뒤 자동으로 메인 복귀
constexpr int PING_INTERVAL_MS = 2000;
constexpr int HEARTBEAT_TIMEOUT_MS = 10000;
constexpr int PHASE_CHANGE_GRACE_MS = 1000; // 단계 전환 1초 이내 CS_MOVE/CS_ACTION은 조용히 버림


enum class PacketId : uint16_t {
	// 입장 1000~
	CS_LOGIN = 1001,
	SC_LOGIN = 1002,

	// 로비·방 2000~
	CS_ROOM_LIST = 2001,
	SC_ROOM_LIST = 2002,
	CS_CREATE_ROOM = 2003,
	SC_CREATE_ROOM = 2004,
	CS_JOIN_ROOM = 2005,
	SC_JOIN_ROOM = 2006,
	CS_LEAVE_ROOM = 2007,
	SC_LEAVE_ROOM = 2008,
	SC_ROOM_STATE = 2009,
	CS_SELECT_CHARACTER = 2010,		// 캐릭터 1~5 (스킬은 캐릭터에 고정)
	CS_SELECT_PERKS = 2011,			// 퍽 1~7, 최대 3개
	CS_READY = 2012,
	SC_ROOM_ERROR = 2013,

	// 게임 진행 3000~
	SC_GAME_LOAD = 3001,
	CS_LOAD_COMPLETE = 3002,
	SC_GAME_START = 3003,
	CS_MOVE = 3010,
	SC_SNAPSHOT = 3011,
	CS_ACTION = 3020,
	SC_ACTION_REJECT = 3021,
	SC_ENTITY_SPAWN = 3030,
	SC_ENTITY_DESPAWN = 3031,
	SC_GAME_END = 3090,
	CS_RETURN = 3091,

	// 게임 이벤트 4000~ (방 전원. SC_PLAYER_STATUS만 본인에게)
	SC_FIRE = 4001,				// 공격 한 번 (연출·소리)
	SC_DAMAGE = 4002,
	SC_DOWNED = 4003,
	SC_RESPAWNED = 4004,
	SC_PROJECTILE = 4005,		// 수류탄·섬광탄 비행
	SC_INTERACT = 4006,
	SC_SAFE_OPENED = 4007,
	SC_MONEY = 4008,			// 플레이어별 들고 있는 가치·반납 금액
	SC_EXPLOSION = 4009,
	SC_TREASURE_TAKEN = 4010,	// "누군가 보물을 획득했다" 전원 메시지
	SC_GUARD_ALERT = 4011,
	SC_KNOCKBACK = 4012,		// 제트팩에 밀려남 (받은 클라가 이동)
	SC_PLAYER_STATUS = 4013,	// 본인에게만: HP·SP·무기·스킬·소비 아이템
	SC_SKILL = 4014,
	SC_PICKUP = 4015,

	// 시스템 9000~
	CS_PING = 9001,			// 모든 단계에서 허용, 하트비트 겸용
	SC_PONG = 9002,
	SC_KICK = 9003,
	SC_NOTICE = 9004,
};

enum class ResultCode : uint16_t {
	Ok = 0,
	// 1~99 공통
	InvalidFormat = 1, ServerBusy = 2, InternalError = 3, NotAllowedNow = 4,
	// 100~ 입장
	NameInUse = 100, NameInvalid = 101, VersionMismatch = 102,
	// 200~ 로비·방
	RoomNotFound = 200, RoomFull = 201, RoomNotWaiting = 202, AlreadyInRoom = 203, RoomNameInvalid = 204,
	CharacterTaken = 210, CharacterInvalid = 211, PerkInvalid = 212, PerkDuplicate = 213,
	NotReadyable = 214,
};

// 연결 단계별 허용 CS_ 패킷 (CS_PING은 항상 허용, 그 외는 연결 종료)
//   Anonymous      : CS_LOGIN
//   Authenticating : 없음 (CS_LOGIN 재전송은 버림)
//   Lobby          : CS_ROOM_LIST, CS_CREATE_ROOM, CS_JOIN_ROOM
//   Room           : CS_LEAVE_ROOM, CS_SELECT_CHARACTER, CS_SELECT_PERKS, CS_READY
//   Game           : CS_LOAD_COMPLETE, CS_MOVE, CS_ACTION, CS_RETURN
enum class ClientPhase : uint8_t { Anonymous, Authenticating, Lobby, Room, Game };

enum class RoomState : uint8_t { Waiting, Loading, Playing, Result };
enum class LeaveReason : uint8_t { Self, Disconnected, GameEnded };

enum class EntityKind : uint8_t {
	Thief, Guard, Safe, ReturnPoint,
	Valuable,			// type_id = ValuableQuality, param = 가치($)
	WeaponPickup,		// type_id = WeaponId, param = 남은 탄
	ConsumablePickup,	// type_id = ConsumableId
	FieldItem,			// type_id = FieldItemId (닿으면 효과, 상호작용 없음)
	Box,
	StickyTrap,			// owner = 설치한 플레이어, timer_ms = 남은 시간
	Mine,				// type_id = WeaponId(BlastMine/ShockMine)
	Smoke,				// timer_ms = 남은 시간
};

enum class ActionType : uint8_t {
	None = 0,
	AttackStart = 1, AttackStop = 2,	// 꾹 누르기 시작·끝. 누르는 동안 무기 간격마다 공격
	Dodge = 3,
	InteractBegin = 4, InteractCancel = 5,	// target_entity_id = 대상
	SwitchWeapon = 6,					// 무기1 ↔ 무기2 (마우스 휠)
	UseSkill = 7,						// Q (은신 중 다시 누르면 해제, 사제 테이저건은 들기/집어넣기)
	UseConsumable = 8,					// E
};

enum class RejectReason : uint8_t {
	InvalidState, Cooldown, NoStamina, OutOfRange, TargetBusy, NotAvailable,
	Downed, Stunned, Locked /*레일건·제트팩 중*/, HpFull, OnlyOneWeapon,
};

enum class EndReason : uint8_t { TimeUp, Aborted };
enum class InteractState : uint8_t { Begin, Cancel, Complete };
enum class AlertLevel : uint8_t { Patrol, Chase, Attack, Search, Repairing, Stunned };
enum class KickReason : uint8_t { ProtocolViolation, ServerShutdown, Duplicate, Admin };

enum class WeaponId : uint8_t {
	None, Baton, Sword, BeamSword, Pistol, Flamethrower, Railgun, Taser, Grenade, BlastMine, ShockMine,
};
enum class ConsumableId : uint8_t { None, HealKit, Smoke, Flashbang, Jetpack };
enum class FieldItemId : uint8_t { None, PotionS, PotionM, PotionL };
enum class ValuableQuality : uint8_t { Common, Advanced, Rare, Unique };
enum class SkillId : uint8_t { None, Taser, Sticky, Cloak };
enum class SkillPhase : uint8_t { Cast, Active, End };
enum class DownCause : uint8_t { Player, Guard };
enum class ExplosionKind : uint8_t { Grenade, BlastMine, ShockMine, Flashbang };

// 엔티티 상태 비트 (EntitySpawn::state, EntitySnapshot::state), 32비트
enum EntityStateBit : uint32_t {
	ES_RUNNING           = 1u << 0,		// 플레이어·경비원
	ES_DODGING           = 1u << 1,		// 플레이어: 구르기(무적)
	ES_INTERACTING       = 1u << 2,
	ES_EXHAUSTED         = 1u << 3,		// 탈진
	ES_ATTACKING         = 1u << 4,		// 공격 키를 누르는 중 / 경비원 사격 중
	ES_DOWNED            = 1u << 5,		// 쓰러져 리스폰 대기
	ES_INVULNERABLE      = 1u << 6,		// 리스폰 무적 / 경비원 자가 수리
	ES_STUNNED           = 1u << 7,		// 경직
	ES_SLOWED            = 1u << 8,		// 끈끈이 위 (이동 −50%, 클라가 적용)
	ES_CLOAKED           = 1u << 9,		// 은신 (다른 클라는 화면에서 숨김)
	ES_IN_SMOKE          = 1u << 10,	// 연막 안
	ES_FLASHED           = 1u << 11,	// 섬광 3초
	ES_RAILGUN_LOCK      = 1u << 12,	// 레일건 충전 1초 + 발사 1초 (이동·회전 불가)
	ES_JETPACK           = 1u << 13,	// 제트팩 3초
	ES_REPAIRING         = 1u << 14,	// 경비원 자가 수리
	ES_LEFT_GAME         = 1u << 15,	// 접속 끊김
	ES_OPENED            = 1u << 16,	// 금고: 열림
	ES_WARNING           = 1u << 17,	// 반납 지점: 곧 사라짐 (빨간색)
	ES_ARMED             = 1u << 18,	// 지뢰: 작동 중
	ES_TRIGGERED         = 1u << 19,	// 지뢰: 감지됨 (0.5초 경고)
	ES_CARRYING_TREASURE = 1u << 20,	// 플레이어: 보물을 들고 있음
	ES_TASER_READY       = 1u << 21,	// 플레이어: 사제 테이저건을 든 상태 (Q로 들기, 공격 키로 발사)
};

// CS_Move::flags
enum MoveFlag : uint8_t {
	MF_MOVING  = 1 << 0,		// 이동 입력 있음
	MF_RUNNING = 1 << 1,		// Shift
};


#pragma pack(push, 1)
struct PacketHeader {
	uint16_t size;		// 헤더 포함 전체 길이
	PacketId type;
};

struct Vec3 {
	float x, y, z;		// cm
};

struct RoomSummary {
	uint32_t room_id;
	char name[MAX_ROOM_NAME_LEN + 1];
	char host_name[MAX_NAME_LEN + 1];
	uint8_t player_count;
	RoomState state;
};

struct RoomMemberInfo {
	uint64_t player_id;
	char name[MAX_NAME_LEN + 1];
	uint16_t character_id;			// 0 = 미선택
	uint8_t perk_count;
	uint16_t perks[MAX_PERKS];
	bool ready;
};

struct SpawnAssign {
	uint64_t player_id;
	uint8_t spawn_index;
};

struct EntitySpawn {
	uint16_t entity_id;				// 방 안에서만 유효, 1부터
	EntityKind kind;
	uint16_t type_id;				// 플레이어: 캐릭터 ID, 아이템: 종류(EntityKind 주석)
	uint64_t owner_player_id;		// 플레이어·끈끈이·지뢰, 나머지 0
	Vec3 pos;
	float yaw;
	uint32_t state;					// EntityStateBit 초기값
	uint32_t timer_ms;				// 남은 수명 (반납 지점·끈끈이·연막), 없으면 0
	uint32_t param;					// 환금 아이템 가치 / 떨어진 무기의 남은 탄
};

struct EntitySnapshot {
	uint16_t entity_id;
	uint32_t state;					// EntityStateBit
	Vec3 pos;
	float yaw;
	uint16_t hp;					// 플레이어·경비원·상자, 그 밖은 0
	ActionType action;				// 진행 중인 동작(구르기), 없으면 None
	WeaponId weapon;				// 들고 있는 무기 (애니메이션)
};

struct PlayerResult {
	uint64_t player_id;
	char name[MAX_NAME_LEN + 1];
	uint16_t character_id;
	bool left_early;
	uint32_t banked_value;			// 반납 금액 = 점수
	uint8_t rank;					// 1부터, 같은 금액은 같은 순위
	uint32_t best_record;			// 이 판까지의 최고 기록
	bool new_record;				// 이 판으로 최고 기록 갱신
	uint16_t kills;
	uint16_t downs;
	uint8_t treasures_delivered;
};

struct ThiefMoney {
	uint16_t thief;					// entity_id
	uint32_t carried_value;			// 들고 있는 환금 아이템 가치 합 ($)
	uint32_t banked_value;			// 반납 금액 ($)
};

struct WeaponSlot {
	WeaponId id;
	uint16_t ammo;					// 0 = 무한(삼단봉·도검) 또는 빈 칸
};
#pragma pack(pop)


#pragma pack(push, 1)
// ───────────────────────── 입장 ─────────────────────────

struct CS_Login {
	uint16_t size;
	PacketId type;
	uint32_t protocol_version;		// PROTOCOL_VERSION
	char name[MAX_NAME_LEN + 1];	// 닉네임
};

struct SC_Login {
	uint16_t size;
	PacketId type;
	ResultCode result;
	uint64_t player_id;
	char name[MAX_NAME_LEN + 1];	// 서버가 정리한 닉네임(앞뒤 공백 제거)
	uint64_t server_time_ms;		// 시간 동기화 기준
};

// ───────────────────────── 로비·방 ─────────────────────────
// 방 안의 변경 요청(캐릭터·퍽·준비)은 개별 성공 응답이 없다.
// 성공 → 방 전원에게 SC_ROOM_STATE, 실패 → 요청자에게만 SC_ROOM_ERROR.

struct CS_RoomList {
	uint16_t size;
	PacketId type;
};

struct SC_RoomList {					// 가변 길이
	uint16_t size;
	PacketId type;
	uint8_t count;
	RoomSummary rooms[MAX_ROOM_LIST];
};

struct CS_CreateRoom {
	uint16_t size;
	PacketId type;
	char name[MAX_ROOM_NAME_LEN + 1];
};

struct SC_CreateRoom {				// 성공하면 이어서 SC_ROOM_STATE
	uint16_t size;
	PacketId type;
	ResultCode result;
	uint32_t room_id;
};

struct CS_JoinRoom {
	uint16_t size;
	PacketId type;
	uint32_t room_id;
};

struct SC_JoinRoom {					// 성공하면 이어서 SC_ROOM_STATE
	uint16_t size;
	PacketId type;
	ResultCode result;
	uint32_t room_id;
};

struct CS_LeaveRoom {
	uint16_t size;
	PacketId type;
};

struct SC_LeaveRoom {
	uint16_t size;
	PacketId type;
	LeaveReason reason;
};

struct SC_RoomState {
	uint16_t size;
	PacketId type;
	uint32_t room_id;
	char name[MAX_ROOM_NAME_LEN + 1];
	uint64_t host_player_id;
	RoomState state;
	uint8_t member_count;
	RoomMemberInfo members[MAX_ROOM_PLAYERS];
};

struct CS_SelectCharacter {
	uint16_t size;
	PacketId type;
	uint16_t character_id;
};

struct CS_SelectPerks {
	uint16_t size;
	PacketId type;
	uint8_t perk_count;				// 0~3, 서로 달라야 함
	uint16_t perks[MAX_PERKS];
};

struct CS_Ready {
	uint16_t size;
	PacketId type;
	bool ready;
};

struct SC_RoomError {
	uint16_t size;
	PacketId type;
	PacketId request_id;			// 실패한 CS_ 패킷 ID
	ResultCode result;
};

// ───────────────────────── 게임 진행 ─────────────────────────

struct SC_GameLoad {
	uint16_t size;
	PacketId type;
	uint64_t match_id;
	uint32_t map_id;
	uint32_t map_version;			// 맵 파일의 meta.version. 클라 맵 버전과 다르면 위치가 어긋남
	uint8_t spawn_count;
	SpawnAssign spawns[MAX_ROOM_PLAYERS];
	uint32_t load_timeout_ms;
};

struct CS_LoadComplete {
	uint16_t size;
	PacketId type;
};

struct SC_GameStart {				// 가변 길이
	uint16_t size;
	PacketId type;
	uint64_t start_server_time_ms;
	uint32_t duration_ms;			// 300000
	uint8_t tick_hz;				// 20
	uint16_t self_entity_id;		// 받는 사람 자신의 플레이어
	uint8_t entity_count;
	EntitySpawn entities[MAX_ENTITIES];	// 경기 시작 시점의 전체 엔티티
};

// 클라가 최대 20Hz로 전송. 멈춰 있으면 보내지 않되 1초에 한 번은 보낸다.
struct CS_Move {
	uint16_t size;
	PacketId type;
	uint16_t seq;					// 입력 순번(CS_ACTION과 공유, 순환)
	Vec3 pos;						// 클라가 정한 위치를 서버가 그대로 쓴다
	float yaw;						// 시선(마우스 커서 방향) = 이동·공격의 기준
	uint8_t flags;					// MoveFlag
};

// 서버가 매 틱(20Hz) 방 전원에게 같은 패킷을 보낸다. 가변 길이.
// 플레이어·경비원은 매 틱, 그 밖의 오브젝트는 상태가 바뀐 틱에만 들어 있다.
struct SC_Snapshot {
	uint16_t size;
	PacketId type;
	uint32_t server_tick;			// 경기 시작 후 틱 번호
	uint32_t remaining_ms;
	uint8_t entity_count;
	EntitySnapshot entities[MAX_ENTITIES];
};

struct CS_Action {
	uint16_t size;
	PacketId type;
	uint16_t seq;					// CS_MOVE와 같은 카운터
	ActionType action;
	float yaw;						// 시선 방향 (공격 방향)
	uint16_t target_entity_id;		// 상호작용 대상, 없으면 0
	Vec3 aim;						// 커서가 가리키는 바닥 지점 (수류탄·섬광탄 조준)
};

struct SC_ActionReject {				// 클라가 미리 재생한 연출을 되돌린다
	uint16_t size;
	PacketId type;
	uint16_t seq;
	ActionType action;
	RejectReason reason;
};

struct SC_EntitySpawn {				// 가변 길이. 경기 중 생긴 것 (아이템·상자·지뢰 등)
	uint16_t size;
	PacketId type;
	uint8_t count;
	EntitySpawn entities[MAX_ENTITIES];
};

struct SC_EntityDespawn {			// 가변 길이
	uint16_t size;
	PacketId type;
	uint8_t count;
	uint16_t entity_ids[MAX_ENTITIES];
};

struct SC_GameEnd {
	uint16_t size;
	PacketId type;
	uint64_t match_id;
	EndReason reason;
	uint32_t duration_ms;
	uint8_t player_count;
	PlayerResult players[MAX_ROOM_PLAYERS];
	uint32_t result_screen_ms;		// 이 시간 뒤 서버가 자동으로 메인(Lobby)으로 돌려보냄
};

struct CS_Return {					// 결과 화면에서 메인으로
	uint16_t size;
	PacketId type;
};

// ───────────────────────── 게임 이벤트 ─────────────────────────
// 받는 사람: 방 멤버 전원 (SC_PLAYER_STATUS만 본인). 같은 틱에 생긴 일은 이벤트를 먼저, 스냅샷을 나중에 보낸다.

struct SC_Fire {						// 공격 한 번 (근접 휘두르기, 총 한 발, 경비원 기관총 한 발 등)
	uint16_t size;
	PacketId type;
	uint16_t shooter;
	WeaponId weapon;				// 경비원 기관총은 None
	Vec3 origin;
	float yaw;
	uint16_t hit_entity;			// 처음 맞은 대상, 없으면 0
};

struct SC_Damage {
	uint16_t size;
	PacketId type;
	uint16_t attacker;				// 0 = 알 수 없음
	uint16_t target;
	WeaponId weapon;
	uint16_t amount;
	uint16_t hp_after;
	uint16_t shield_after;
};

struct SC_Downed {
	uint16_t size;
	PacketId type;
	uint16_t victim;
	uint16_t attacker;
	DownCause cause;
	uint32_t lost_banked;			// 잃은 반납 금액 (10% / 40%)
	uint32_t respawn_ms;			// 5000 / 10000
};

struct SC_Respawned {				// 받은 플레이어의 클라는 pos로 옮긴다
	uint16_t size;
	PacketId type;
	uint16_t thief;
	Vec3 pos;
	uint32_t invuln_ms;
};

struct SC_Projectile {
	uint16_t size;
	PacketId type;
	uint16_t thrower;
	ExplosionKind kind;				// Grenade 또는 Flashbang
	Vec3 from;
	Vec3 to;
	uint32_t flight_ms;
};

struct SC_Interact {
	uint16_t size;
	PacketId type;
	uint16_t actor;
	uint16_t target;
	InteractState state;
	uint32_t progress_ms;			// 이미 진행된 시간(금고는 중단해도 유지)
	uint32_t duration_ms;			// 끝나는 데 필요한 전체 시간 (작업 속도 1.0 기준)
};

struct SC_SafeOpened {
	uint16_t size;
	PacketId type;
	uint16_t safe;
	uint16_t opener;
};

struct SC_Money {					// 들고 있는 가치·반납 금액이 바뀔 때마다
	uint16_t size;
	PacketId type;
	uint8_t thief_count;
	ThiefMoney thieves[MAX_ROOM_PLAYERS];
};

struct SC_Explosion {
	uint16_t size;
	PacketId type;
	ExplosionKind kind;
	Vec3 pos;
	float radius;
};

struct SC_TreasureTaken {			// "누군가 보물을 획득했다" (보물탐색 퍽은 pos로 방향 표시)
	uint16_t size;
	PacketId type;
	uint16_t thief;
	Vec3 pos;
	uint32_t value;
};

struct SC_GuardAlert {				// 경비원 상태 연출용
	uint16_t size;
	PacketId type;
	uint16_t guard;
	AlertLevel level;
	uint16_t target;				// 쫓는·쏘는 플레이어, 없으면 0
};

struct SC_Knockback {
	uint16_t size;
	PacketId type;
	uint16_t target;
	float yaw;						// 밀려나는 방향
	float distance;					// cm
};

struct SC_PlayerStatus {				// 본인에게만, 바뀔 때만
	uint16_t size;
	PacketId type;
	uint16_t hp;
	uint16_t shield;
	uint8_t stamina;				// 0~100
	WeaponSlot weapons[2];
	uint8_t active_slot;
	ConsumableId consumable;
	SkillId skill;
	uint32_t skill_cooldown_ms;
	uint32_t skill_active_ms;
	bool death_save_left;
};

struct SC_Skill {
	uint16_t size;
	PacketId type;
	uint16_t caster;
	SkillId skill;
	SkillPhase phase;
	Vec3 pos;
};

struct SC_Pickup {
	uint16_t size;
	PacketId type;
	uint16_t thief;
	EntityKind kind;
	uint16_t type_id;
	uint32_t param;
};

// ───────────────────────── 시스템 ─────────────────────────

struct CS_Ping {						// 모든 단계에서 2초마다
	uint16_t size;
	PacketId type;
	uint64_t client_time_ms;
};

struct SC_Pong {
	uint16_t size;
	PacketId type;
	uint64_t client_time_ms;
	uint64_t server_time_ms;
};

struct SC_Kick {						// 보낸 직후 서버가 끊음
	uint16_t size;
	PacketId type;
	KickReason reason;
	char message[MAX_MESSAGE_LEN + 1];
};

struct SC_Notice {
	uint16_t size;
	PacketId type;
	char message[MAX_MESSAGE_LEN + 1];
};
#pragma pack(pop)


// 가변 길이 패킷의 실제 전송 크기 (배열을 count개만 보낼 때)
constexpr uint16_t SC_RoomList_Size(uint8_t count) {
	return static_cast<uint16_t>(offsetof(SC_RoomList, rooms) + count * sizeof(RoomSummary));
}
constexpr uint16_t SC_GameStart_Size(uint8_t count) {
	return static_cast<uint16_t>(offsetof(SC_GameStart, entities) + count * sizeof(EntitySpawn));
}
constexpr uint16_t SC_Snapshot_Size(uint8_t count) {
	return static_cast<uint16_t>(offsetof(SC_Snapshot, entities) + count * sizeof(EntitySnapshot));
}
constexpr uint16_t SC_EntitySpawn_Size(uint8_t count) {
	return static_cast<uint16_t>(offsetof(SC_EntitySpawn, entities) + count * sizeof(EntitySpawn));
}
constexpr uint16_t SC_EntityDespawn_Size(uint8_t count) {
	return static_cast<uint16_t>(offsetof(SC_EntityDespawn, entity_ids) + count * sizeof(uint16_t));
}

// 모든 패킷이 최대 크기 안에 들어가는지 컴파일 시점 확인
static_assert(sizeof(PacketHeader) == 4);
static_assert(sizeof(SC_RoomList) <= MAX_PACKET_SIZE);
static_assert(sizeof(SC_RoomState) <= MAX_PACKET_SIZE);
static_assert(sizeof(SC_GameStart) <= MAX_PACKET_SIZE);
static_assert(sizeof(SC_Snapshot) <= MAX_PACKET_SIZE);
static_assert(sizeof(SC_EntitySpawn) <= MAX_PACKET_SIZE);
static_assert(sizeof(SC_GameEnd) <= MAX_PACKET_SIZE);
static_assert(MAX_ENTITIES <= 255, "entity_count가 uint8_t");
