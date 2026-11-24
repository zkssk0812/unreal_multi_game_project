#pragma once

// 게임 한 판의 세계: 엔티티(플레이어·경비원·금고·상자·아이템…)를 들고 입력과 규칙을 반영한다 (docs/psd.md 5장).
//
// - JobQueue가 아닌 평범한 클래스. GameRoom의 Job 안에서만 쓰이므로 락이 없다.
// - 소켓·패킷·채널을 모른다. 그래서 ContentTests에서 방 없이도 바로 시험할 수 있다.
// - 시간: Update(초)로만 흐른다(_nowMs). 실제 시계를 직접 읽지 않으므로 시험에서 30초도 한 번에 넘길 수 있다.
// - 이벤트: 공격·대미지·쓰러짐처럼 "그 틱에 생긴 일"은 _events에 쌓고, 방이 TakeEvents()로 가져가 보낸다.
//
// 파일 나눔 (같은 클래스의 멤버 함수를 기능별 파일로)
//   GameWorld.cpp       : 준비, 입력 분기, 상호작용, 틱 순서, 반납 지점, 스냅샷·결과·개인 상태
//   WorldCombat.cpp     : 판정(근접·직선·부채꼴·빔), 무기 발사, 대미지, 쓰러짐·리스폰, 무기 칸
//   WorldExplosives.cpp : 투척물(수류탄·섬광탄), 지뢰, 폭발
//   WorldItems.cpp      : 환금 아이템·반납, 금고·보물 순환, 상자, 아이템 줍기, 필드 아이템
//   WorldSkills.cpp     : 스킬 3종, 소비 아이템 사용, 상태이상(감속·연막·섬광·제트팩)
//   GuardAI.cpp         : 경비원 상태 머신 (순찰·추격·사격·탐색·수리·경직)
//
// 엔티티를 넣고 빼는 규칙: _entities를 순회하는 동안 vector가 바뀌면 들고 있던 참조가 깨진다.
// 그래서 새 엔티티는 Spawn()이 _spawnQueue에 모으고, 지울 엔티티는 removed 표시만 한다.
// ApplyAction·Update가 끝날 때 Flush()가 한꺼번에 반영하고 SC_ENTITY_SPAWN/DESPAWN 이벤트를 만든다.

#include <array>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Content/Game/MapData.h"
#include "Content/Messages/Commands.h"
#include "Content/Messages/Messages.h"

namespace Content {

// 플레이어가 들고 있는 환금 아이템 한 개 (인벤토리 없음: 품질과 가치만, M-04)
struct CarriedItem
{
	ValuableQuality quality = ValuableQuality::Common;
	uint32_t value = 0;
};

struct Entity
{
	uint16_t id = 0;					// 방 안에서만 유효, 1부터
	EntityKind kind = EntityKind::Thief;
	uint16_t typeId = 0;				// 플레이어: 캐릭터 ID / 아이템: 종류 / 반납 지점: 방 번호 / 지뢰: WeaponId
	uint64_t ownerPlayerId = 0;			// 플레이어만
	Vec3 pos;
	float yaw = 0.0f;
	uint32_t state = 0;					// StateBit 조합
	uint32_t param = 0;					// 환금 아이템 가치 / 떨어진 무기 탄
	bool removed = false;				// 다음 Flush에서 지워짐
	bool dirty = false;					// 오브젝트: 이번 틱에 상태가 바뀜 → 스냅샷에 넣음

	// ── 체력 (플레이어·경비원·상자) ──
	int32_t hp = 0;
	uint32_t stunUntilMs = 0;

	// ── 플레이어 ──
	std::string name;					// 닉네임 (결과·최고 기록)
	float stamina = 0.0f;
	bool moving = false;				// 마지막 이동 입력에 이동 키가 있었나
	float moveYaw = 0.0f;				// 실제로 움직인 방향 (끈끈이를 뒤에 놓을 때)
	ActionType action = ActionType::None;	// 진행 중인 구르기 (스냅샷에 실림)
	uint32_t actionStartMs = 0;
	uint16_t interactTarget = 0;		// 상호작용 중인 대상
	int32_t shield = 0;					// 방탄조끼 보호막
	bool deathSaveLeft = false;			// 가시회생 남음
	uint16_t perkMask = 0;				// 1 << 퍽 ID
	std::array<Msg::WeaponSlot, 2> weapons{};
	uint8_t activeSlot = 0;
	ConsumableId consumable = ConsumableId::None;
	bool attackHeld = false;			// 공격 키를 누르고 있음
	uint32_t nextFireMs = 0;
	Vec3 aim;							// 마지막 조준 지점 (투척, A-14)
	std::vector<CarriedItem> carried;	// 들고 있는 환금 아이템 (보물 포함)
	uint32_t banked = 0;				// 반납 금액 = 점수
	uint32_t downedUntilMs = 0;
	DownCause downCause = DownCause::Player;
	uint8_t spawnIndex = 0;
	uint32_t invulnUntilMs = 0;			// 리스폰 무적
	SkillId skill = SkillId::None;
	uint32_t skillReadyMs = 0;			// 이 시각부터 다시 쓸 수 있음
	uint32_t skillActiveUntilMs = 0;	// 은신 끝
	uint32_t skillCastAtMs = 0;			// 끈끈이: 이 시각에 설치 (0 = 없음)
	uint32_t flashedUntilMs = 0;
	uint32_t railgunUntilMs = 0;
	uint32_t railgunNextMs = 0;
	uint8_t railgunLeft = 0;			// 남은 레일건 판정 횟수
	float railgunYaw = 0.0f;
	uint32_t jetpackUntilMs = 0;
	std::vector<uint16_t> jetpackHit;	// 이번 제트팩에 이미 부딪친 대상 (대상마다 한 번, A-19)
	std::optional<Msg::PlayerStatus> lastStatus;	// 마지막으로 보낸 개인 상태
	uint16_t kills = 0;
	uint16_t downs = 0;
	uint8_t treasuresDelivered = 0;

	// ── 경비원 ──
	uint32_t repairUntilMs = 0;
	uint32_t nextGunMs = 0;

	// ── 상호작용 대상 (금고·상자·반납 지점·아이템) ──
	Vec3 interactPos;					// 여기서 interactRange 안에 서야 상호작용 가능
	float interactRange = 0.0f;
	float progressMs = 0.0f;			// 진행한 시간 (작업 속도를 곱해 쌓는다)
	uint16_t interactor = 0;			// 지금 상호작용 중인 플레이어. 한 명만
	uint32_t spawnMs = 0;				// 생긴 시각 (반납 지점 55초·60초)
	uint16_t safeRoom = 0;				// 금고: 놓인 방 (MapData::rooms의 위치). 다음 금고는 이 방을 뺀다

	// ── 끈끈이·연막·지뢰 ──
	uint32_t expireMs = 0;
	uint16_t ownerEntity = 0;
	uint32_t armAtMs = 0;
	uint32_t triggerAtMs = 0;			// 0 = 아직 감지 안 됨

	bool IsThief() const { return kind == EntityKind::Thief; }
	bool IsGuard() const { return kind == EntityKind::Guard; }
	bool HasPerk(uint16_t perk) const { return (perkMask & (1u << perk)) != 0; }
	uint32_t CarriedValue() const;
	bool HasTreasure() const;
	WeaponId ActiveWeapon() const { return weapons[activeSlot].id; }
};

// 게임 시작 때 플레이어 한 명의 정보
struct ThiefSlot
{
	uint64_t playerId = 0;
	uint16_t characterId = 0;
	uint8_t spawnIndex = 0;
	std::vector<uint16_t> perks;
	std::string name;
};

// 그 틱에 생긴 일 (방이 꺼내서 SC_ 이벤트로 방 전원에게 보낸다)
using WorldEvent = std::variant<Msg::Fire, Msg::Damage, Msg::Downed, Msg::Respawned, Msg::Projectile, Msg::Interact,
	Msg::SafeOpened, Msg::Money, Msg::Explosion, Msg::TreasureTaken, Msg::GuardAlert, Msg::Knockback, Msg::Skill,
	Msg::Pickup, Msg::EntitySpawn, Msg::EntityDespawn>;

// ── 경비원 AI ── (GuardAI.cpp)
enum class GuardState : uint8_t
{
	Patrol,			// 순찰 지점을 따라 걷기, 갈림길에서 1초 둘러보기
	Chase,			// 본 플레이어를 달려서 쫓음 (공격 범위 밖)
	Attack,			// 타겟이 공격 범위(60°) 안 → 멈춰서 사격
	Search,			// 마지막으로 본 곳에 가서 둘러봄
	Repairing,		// HP 0 → 5초 자가 수리 (무적·정지)
	Stunned,		// 경직
};

// 경비원 AI 개선 항목 켜고 끄기 (docs/경비원AI_공격탐색_개선안.md). 기본은 모두 켬.
// 모두 끄면(None) 개선 전과 같이 움직인다 → 비교 측정(GuardBenchTests)과 문제가 생겼을 때 하나씩 끄는 데 쓴다.
struct GuardFeatures
{
	bool evade = true;				// A-2 물러나며 돌아서기
	bool predictFlee = true;		// S-1 도망 방향 예측
	bool fullSpin = true;			// S-3 한 바퀴 둘러보기
	bool extraSpots = true;			// S-4 주변 지점 추가 확인
	bool callAllies = true;			// S-5 동료 호출
	bool stunMemory = true;			// S-6 경직·수리 후 최근 본 곳 탐색
	bool leastVisited = true;		// P-1 오래 안 간 곳 우선
	bool spreadOut = true;			// P-2 동료와 겹치지 않기
	bool passThrough = true;		// P-3 지나가는 지점은 통과
	bool sweepLook = true;			// P-4 둘러보기 개선 (±45°, 문에서는 방 안쪽)
	bool importantRooms = true;		// P-5 중요한 방 가중치
	bool lookBack = true;			// P-6 가끔 뒤돌아보기
	bool alertPatrol = true;		// P-7 탐색 후 경계 순찰

	static GuardFeatures All() { return {}; }
	static GuardFeatures None()
	{
		return { false, false, false, false, false, false, false, false, false, false, false, false, false };
	}
};

struct GuardBrain
{
	uint16_t guardId = 0;
	GuardState state = GuardState::Patrol;
	uint16_t target = 0;				// 쫓는 플레이어
	bool lockedByAttack = false;		// N-16: 공격 때문에 바뀐 타겟은 시야에서 벗어나기 전까지 고정
	Vec3 lastSeenPos;
	uint32_t lastSeenMs = 0;
	Vec3 seenVel;						// 타겟의 추정 속도 (cm/s, 본 위치·맞은 위치로 계산)
	Vec3 velSamplePos;
	uint32_t velSampleMs = 0;			// 0 = 아직 관측 없음
	uint32_t evadeUntilMs = 0;			// A-2 물러나며 돌아서기 중 (이 시각까지)
	Vec3 searchPos;						// 지금 가는 탐색 지점
	std::vector<Vec3> searchSpots;		// 탐색할 지점들: [0] 놓친 곳(S-1 예측), 나머지 주변 지점(S-4)
	size_t searchIndex = 0;
	bool looking = false;				// 도착해서 둘러보는 중
	uint32_t waitUntilMs = 0;			// 둘러보기 끝 (한 바퀴는 이 시각까지, 훑기는 안전장치)
	std::vector<float> lookYaws;		// 훑기: 차례로 돌아볼 방향. 비어 있으면 한쪽으로 계속 돈다
	size_t lookIndex = 0;

	std::vector<Vec3> path;				// 다음 점부터 목적지까지
	size_t pathIndex = 0;
	Vec3 goal;
	uint32_t repathAtMs = 0;

	uint16_t patrolNode = 0;
	uint16_t prevPatrolNode = 0xFFFF;
	uint32_t alertUntilMs = 0;			// P-7 경계 순찰 끝 (탐색을 마친 뒤 30초)
	Vec3 alertCenter;					// P-7 놓친 곳

	AlertLevel alertLevel = AlertLevel::Patrol;	// 마지막으로 보낸 SC_GUARD_ALERT
	uint16_t alertTarget = 0;
};

class GameWorld
{
public:
	// 엔티티를 모두 만든다: 플레이어, 경비원, 금고 1개, 반납 지점 5곳, 상자, 필드 아이템
	void Setup(const MapData& map, const std::vector<ThiefSlot>& thieves, std::mt19937& rng);
	void Clear();

	// ── 입력 (틱에서 호출) ──
	void ApplyMove(uint64_t playerId, const InputCmd& input);
	std::optional<RejectReason> ApplyAction(uint64_t playerId, const InputCmd& input);	// 거절이면 이유

	// ── 시간 흐름 ──
	void Update(float deltaSec);

	// ── 상태 변화 ──
	void MarkLeft(uint64_t playerId);	// 접속이 끊긴 플레이어

	// ── 경비원 AI ──
	void SetAIEnabled(bool enabled) { _aiEnabled = enabled; }	// 규칙 시험은 끄고 경비원을 직접 둔다
	void SetGuardFeatures(const GuardFeatures& features) { _guardFeatures = features; }
	const GuardFeatures& Features() const { return _guardFeatures; }
	const GuardBrain* BrainOf(uint16_t guardId) const;
	bool CanSee(const Entity& guard, const Entity& thief);		// 시야 부채꼴 + 벽 검사

	// ── 조회 ──
	Entity* ThiefOf(uint64_t playerId);
	Entity* Find(uint16_t id);
	const std::vector<Entity>& Entities() const { return _entities; }
	uint32_t NowMs() const { return _nowMs; }
	uint32_t TreasureValue() const { return _treasureValue; }	// 지금 보물(또는 다음 보물)의 가치
	bool AnyThiefActive() const;

	// 시험·도구용: 엔티티를 바로 하나 만든다 (Spawn + Flush)
	uint16_t AddNow(EntityKind kind, Vec3 pos, uint16_t typeId = 0, uint32_t param = 0);

	// ── 메시지 만들기 ──
	std::vector<WorldEvent> TakeEvents();
	std::vector<std::pair<uint64_t, Msg::PlayerStatus>> TakeStatuses();	// 바뀐 개인 상태 (playerId, 상태)
	std::vector<Msg::EntityInfo> SpawnInfos() const;
	Msg::Snapshot MakeSnapshot(uint32_t tick, uint32_t remainingMs);	// 플레이어·경비원 + 바뀐 오브젝트. dirty를 지운다
	std::vector<Msg::PlayerResult> MakeResults() const;					// 순위까지 (최고 기록은 방이 채운다)
	Msg::PlayerStatus StatusOf(const Entity& thief) const;

private:
	// ── 엔티티 넣고 빼기 (GameWorld.cpp) ──
	Entity& Add(EntityKind kind, Vec3 pos, float yaw, uint16_t typeId = 0, uint64_t owner = 0);	// Setup 전용(바로 넣음)
	Entity& Spawn(EntityKind kind, Vec3 pos, uint16_t typeId = 0, uint32_t param = 0);			// 큐에 넣음
	void Remove(Entity& e);
	void Flush();
	Msg::EntityInfo InfoOf(const Entity& e) const;

	// ── 상호작용 (GameWorld.cpp) ──
	std::optional<RejectReason> BeginInteract(Entity& thief, uint16_t targetId);
	void CancelInteract(Entity& thief);
	void CompleteInteract(Entity& thief, Entity& target);
	void UpdateInteract(Entity& thief, uint32_t dtMs);
	uint32_t InteractDurationMs(const Entity& target) const;
	bool InRange(const Entity& thief, const Entity& target) const;
	float WorkSpeed(const Entity& thief) const;

	// ── 플레이어 틱 (GameWorld.cpp) ──
	void UpdateThief(Entity& thief, float deltaSec, uint32_t dtMs);
	void UpdateStamina(Entity& thief, float deltaSec);
	void UpdateReturnPoint(Entity& point);
	bool PlaceReturnPoint(Entity& point, std::optional<uint16_t> excludeRoom);
	void ReplaceReturnPoint(Entity& point);
	uint32_t ReturnPointLeftMs(const Entity& point) const;
	bool CanAct(const Entity& thief) const;

	// ── 문 (WorldDoors.cpp, 클라 2026-11-24) ──
	void PlaceDoors();
	std::optional<RejectReason> ToggleDoor(Entity& door);		// F: 즉시 열기·닫기. 문간에 누가 있으면 닫지 않음
	void SetDoorOpen(Entity& door, bool open);
	bool DoorOccupied(size_t index) const;
	bool ClosedDoorBlocks(const Vec3& from, const Vec3& to) const;	// 닫힌 문이 선분을 가리나
	void OpenDoorsOnWay(const Vec3& from, const Vec3& to);			// 경비원: 가는 길의 닫힌 문을 연다
	bool SeesThrough(const Vec3& from, const Vec3& to, uint32_t* tests = nullptr) const;	// 벽·닫힌 문 모두 없나

	// ── 전투 (WorldCombat.cpp) ──
	struct HitResult { Entity* target = nullptr; float distance = 0.0f; };
	bool IsTargetable(const Entity& e, const Entity* shooter, bool guardShot) const;
	bool Blocked(const Vec3& from, const Vec3& to) const;			// 벽에 막히나 (A-11)
	HitResult RayHit(const Entity& shooter, float yaw, float range, bool guardShot);
	std::vector<Entity*> ConeHits(const Entity& shooter, float yaw, float range, float halfAngleDeg);
	std::vector<Entity*> BeamHits(const Entity& shooter, float yaw, float range);
	std::vector<Entity*> RadiusHits(const Vec3& center, float radius);
	float MaxRangeCm() const { return _maxRangeCm; }
	void UpdateFiring(Entity& thief);
	void FireWeapon(Entity& thief);
	void UpdateRailgun(Entity& thief);
	void ApplyDamage(Entity& target, Entity* attacker, WeaponId weapon, int32_t amount, uint32_t stunMs);
	void Down(Entity& thief, Entity* attacker, DownCause cause);
	void Respawn(Entity& thief);
	void ResetLoadout(Entity& thief);
	void UseAmmo(Entity& thief);
	void RemoveWeapon(Entity& thief, uint8_t slot);
	void StartRepair(Entity& guard);
	void Stun(Entity& target, uint32_t ms);
	void OnGuardAttacked(Entity& guard, Entity& attacker);			// N-15·N-16 (GuardAI.cpp)

	// ── 폭발물 (WorldExplosives.cpp) ──
	struct Thrown { uint16_t owner = 0; ExplosionKind kind = ExplosionKind::Grenade; Vec3 from; Vec3 to; Vec3 last; uint32_t startMs = 0; };
	void Throw(Entity& thrower, ExplosionKind kind, const Vec3& aim);
	void UpdateThrown();
	void Explode(ExplosionKind kind, const Vec3& pos, uint16_t ownerId);
	void PlaceMine(Entity& thief, WeaponId mine);
	void UpdateMine(Entity& mine);

	// ── 아이템 (WorldItems.cpp) ──
	void PlaceSafe(const std::vector<uint16_t>& excludeRooms);	// 방 안 무작위 빈 칸 (A-05)
	struct RoomSpot { Vec3 pos; uint16_t room = 0; };
	std::optional<RoomSpot> RandomRoomSpot(const std::vector<uint16_t>& excludeRooms, const Entity* ignore = nullptr);	// 방을 먼저 고르고 그 방의 빈 칸
	RoomSpot ThiefSpot(const Entity* thief, const std::vector<uint16_t>& avoidRooms);	// 의적 시작·리스폰 (A-26)
	void OnSafeOpened(Entity& safe, Entity& opener);
	std::optional<RejectReason> PickUpItem(Entity& thief, Entity& item);
	void Deliver(Entity& thief);
	void DropValuables(Entity& thief, bool treasureOnly);
	void PushMoneyEvent();
	void BreakBox(Entity& box);
	void UpdateBoxRespawns();
	void UpdateFieldItems();
	void TouchFieldItems(Entity& thief);
	bool IsFreeSpot(const Vec3& p, const Entity* ignore = nullptr) const;	// 다른 오브젝트(ignore 말고)와 1m 이상 떨어졌나
	std::optional<Vec3> RandomFreeCell();
	Vec3 Scatter(const Vec3& center);
	uint32_t RandomValue(ValuableQuality quality);

	// ── 스킬·소비 아이템·상태이상 (WorldSkills.cpp) ──
	std::optional<RejectReason> UseSkill(Entity& thief);
	std::optional<RejectReason> UseConsumable(Entity& thief, const InputCmd& input);
	void UpdateSkill(Entity& thief);
	void EndCloak(Entity& thief);
	bool FireTaser(Entity& thief);									// 든 테이저건을 쏨 (든 상태가 아니면 false)
	void HolsterTaser(Entity& thief, bool startCooldown);			// 든 테이저건을 쏘지 않고 집어넣음 (외부 요인이면 쿨타임)
	void UpdateEffects();											// 끈끈이·연막 범위, 만료
	void UpdateJetpack(Entity& thief);
	void SetSkillEvent(const Entity& caster, SkillPhase phase, const Vec3& pos);

	// ── 경비원 AI (GuardAI.cpp) ──
	void UpdateGuards(uint32_t dtMs);
	void UpdateGuardTimers(Entity& guard);
	void ThinkGuard(Entity& guard, GuardBrain& brain, uint32_t dtMs);
	void SetGuardState(Entity& guard, GuardBrain& brain, GuardState state, uint16_t target = 0);
	Entity* NearestVisibleThief(Entity& guard);
	bool SetGoal(Entity& guard, GuardBrain& brain, const Vec3& goal);
	bool FollowPath(Entity& guard, GuardBrain& brain, float speed, uint32_t dtMs);
	void TurnToward(Entity& guard, float yaw, uint32_t dtMs);
	void LookAround(Entity& guard, uint32_t dtMs);
	void StartPatrol(Entity& guard, GuardBrain& brain);
	void GuardShoot(Entity& guard, Entity& target);
	void NoteTargetPos(GuardBrain& brain, const Vec3& pos);		// 타겟을 본·맞은 위치 기록 (속도 추정)
	Vec3 GuessTargetPos(const GuardBrain& brain) const;			// 마지막 위치 + 속도로 지금 있을 곳 (최대 1초 앞)
	void EvadeStep(Entity& guard, const GuardBrain& brain, const Vec3& threat, float speed, uint32_t dtMs);	// A-2
	void StartSearch(Entity& guard, GuardBrain& brain, const Vec3& center, bool useFlee);	// S-1·S-4
	void AddSearchSpots(GuardBrain& brain, const Vec3& from, const Vec3* flee);			// S-4
	void CallAllies(const Entity& caller, const Vec3& where);							// S-5
	void StartSpin(GuardBrain& brain, uint32_t ms);
	void StartSweep(GuardBrain& brain, float center, float halfDeg, bool backToCenter);	// 좌우 훑기
	bool UpdateLook(Entity& guard, GuardBrain& brain, uint32_t dtMs);					// 둘러보기가 끝났으면 true
	void ArrivePatrolNode(Entity& guard, GuardBrain& brain);							// P-1·P-3·P-4·P-6
	void PickPatrolNode(Entity& guard, GuardBrain& brain);								// P-1·P-2·P-5·P-7
	GuardBrain* BrainFor(uint16_t guardId);

	std::vector<Entity> _entities;
	std::vector<Entity> _spawnQueue;	// 다음 Flush에서 _entities 끝에 붙는다
	uint16_t _nextId = 1;
	uint32_t _nowMs = 0;
	const MapData* _map = nullptr;		// 서버가 끝날 때까지 사는 맵 (DefaultMap)
	bool _aiEnabled = true;
	GuardFeatures _guardFeatures;
	std::vector<GuardBrain> _brains;
	std::vector<std::vector<uint16_t>> _patrolLinks;	// 순찰 지점마다 이어진 지점 (patrolEdges 순서)
	std::vector<uint32_t> _patrolVisitMs;	// P-1 순찰 지점마다 경비원이 마지막으로 다녀간 시각 (3명 공유, 0 = 아직)
	uint32_t _sightTests = 0;			// 이번 틱의 시야 검사 횟수 (Perf)
	std::vector<WorldEvent> _events;
	std::mt19937 _rng{ 7 };				// Setup에서 방의 rng로 다시 씨앗을 준다

	float _maxRangeCm = 0.0f;			// 무제한 사거리 (A-12)
	uint32_t _treasureValue = 0;		// 지금 보물의 가치 (반납되면 다음 보물 가치로)
	std::optional<uint16_t> _lastSafeRoom;
	std::vector<Thrown> _thrown;
	struct BoxRespawn { Vec3 pos; uint32_t atMs = 0; uint16_t typeId = 0; };	// typeId = BoxType
	std::vector<BoxRespawn> _boxRespawns;
	uint32_t _nextFieldItemMs = 0;
	std::vector<uint8_t> _doorOpen;		// 맵 doors마다 열림(1)·닫힘(0). 시야 검사가 엔티티를 찾지 않고 바로 본다
	std::vector<uint16_t> _doorIds;		// 맵 doors마다 Door 엔티티 id
	std::vector<Vec3> _itemCells;		// 상자·필드 아이템을 놓을 수 있는 칸 (방 안, 걸을 수 있고 닿는 칸)
};

} // namespace Content
