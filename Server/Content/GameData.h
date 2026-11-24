#pragma once

// 게임 데이터 상수 (docs/psd.md 4장 = docs/컨텐츠_개발문서.md의 수치).
// 콘텐츠는 protocol.h를 모르므로 같은 값을 여기 따로 둔다. 기획이 바뀌면 이 파일만 고친다.
// 표기: [명세] 컨텐츠_개발문서.md에 있는 값 / [가정 A-xx] psd.md 2장의 임시 결정

#include <cstddef>
#include <cstdint>

#include "Content/Messages/Types.h"

namespace Content::GameData {

// ── 로비·방 ──
constexpr size_t MaxRoomPlayers = 3;		// 방장 포함 (protocol.h MAX_ROOM_PLAYERS와 같아야 함)
constexpr size_t MaxPerks = 3;				// 한 사람이 고를 수 있는 퍽 수
constexpr size_t MaxRoomList = 20;			// 방 목록 한 번에 보내는 수

constexpr uint16_t CharacterCount = 3;		// 도적1·2·3 (2026-11-22 확정, 전에는 임시 5명 CH-01~05)
constexpr uint16_t PerkCount = 7;			// [명세 K-01~07]

constexpr size_t NicknameMaxChars = 12;		// 글자 수 (UTF-8 바이트 수가 아님)
constexpr size_t RoomNameMaxChars = 20;

// ── 퍽 ID (K-01~07) ──
namespace Perk {
constexpr uint16_t Vest = 1;			// 방탄조끼: 보호막 20
constexpr uint16_t DeathSave = 2;		// 가시회생: 한 번 HP 1로 버팀
constexpr uint16_t Tools = 3;			// 유용한 도구: 작업 속도 +10%
constexpr uint16_t Sneakers = 4;		// 운동화: 달리기 +20% (클라가 적용)
constexpr uint16_t TreasureSense = 5;	// 보물탐색 (클라 표시)
constexpr uint16_t RobotSense = 6;		// 로봇탐지 (클라 표시)
constexpr uint16_t BioSense = 7;		// 생체감지 (클라 표시)
}

// 캐릭터 → 고유 스킬 [가정 A-06]. 캐릭터를 늘리면 이 표에 더한다 (없으면 스킬 없음)
inline SkillId SkillOf(uint16_t characterId)
{
	switch (characterId)
	{
	case 1: return SkillId::Taser;
	case 2: return SkillId::Sticky;
	case 3: return SkillId::Cloak;
	default: return SkillId::None;
	}
}

// ── 플레이어 능력치 (§4) ──
constexpr int32_t PlayerMaxHp = 100;				// [명세 P-01] 자연 회복 없음
constexpr float StaminaMax = 100.0f;				// [명세 P-02]
constexpr float StaminaRunCostPerSec = 10.0f;
constexpr float StaminaRegenPerSec = 10.0f;
constexpr float ExhaustedRegenScale = 0.5f;			// 탈진 중 +5/s
constexpr float DodgeStamina = 20.0f;
constexpr uint32_t DodgeInvulnMs = 500;
constexpr float WorkSpeedTools = 1.1f;				// [명세 K-03]
constexpr int32_t VestShield = 20;					// [명세 K-01]

// ── 상호작용 (§13) ──
constexpr float PickupRangeCm = 300.0f;				// [명세 IN-03] 아이템 3m
constexpr float WorkRangeCm = 130.0f;				// [명세 Q-33] 금고·상자 1m + 여유 30cm
constexpr float DoorRangeCm = 150.0f;				// 문: 가운데에서 150cm 안에서 F로 열고 닫기 (클라 2026-11-24)
constexpr float GuardDoorLookAheadCm = 100.0f;		// 경비원은 가는 길 1m 앞의 닫힌 문을 열고 지나감 (열린 채로 둠)
constexpr float ReturnPointRangeCm = 100.0f;		// 반납 지점 구획 중심에서 (지금 규칙 유지)
constexpr float FieldItemTouchCm = 60.0f;			// 필드 아이템: 닿았다고 보는 거리 (히트 박스 40 + 20)
constexpr uint32_t SafeOpenMs = 10000;				// [명세 O-02] 10초 × 작업 속도, 진행도 유지
constexpr uint32_t BoxOpenMs = 3000;				// [명세 O-11] 3초, 멈추면 처음부터

// ── 쓰러짐·리스폰 (§7) ──
constexpr uint32_t RespawnByPlayerMs = 5000;		// [명세 D-05]
constexpr uint32_t RespawnByGuardMs = 10000;
constexpr uint32_t LossPctByPlayer = 10;			// [명세 D-04] 반납 금액의 %
constexpr uint32_t LossPctByGuard = 40;
constexpr uint32_t RespawnInvulnMs = 5000;			// [명세 D-07]
constexpr float DropScatterCm = 80.0f;				// 떨어뜨린 아이템이 흩어지는 반경

// ── 판정 [가정 A-10~A-12] ──
constexpr float BodyRadiusCm = 40.0f;				// 플레이어·경비원 히트 박스(원기둥) 반지름
constexpr float BodyHeightCm = 180.0f;
constexpr float BoxRadiusCm = 50.0f;
constexpr float HitHeightCm = 150.0f;				// 높이 차이가 이보다 크면 안 맞음 (위층·아래층)
constexpr float ShotHeightCm = 100.0f;				// 벽 검사를 할 때 쓰는 가슴 높이
constexpr float UnlimitedExtraCm = 1000.0f;			// 무제한 사거리 = 맵 대각선 + 10m

// ── 무기 (§8) ──
enum class WeaponHit : uint8_t { MeleeCone, Ray, Cone, Beam, Thrown, Mine };
struct WeaponType
{
	WeaponId id;
	WeaponHit hit;
	uint16_t damage;			// 수류탄·지뢰는 중심 대미지
	uint32_t intervalMs;		// 한 번 공격하는 간격 (초당 공격 횟수의 역수, A-16)
	uint16_t ammo;				// 0 = 무한
	float rangeCm;				// 0 = 무제한 (A-12)
	float halfAngleDeg;			// 근접·화염방사기 부채꼴 반각
	uint32_t stunMs;
};
inline constexpr WeaponType WeaponTypes[] = {
	{ WeaponId::Baton,        WeaponHit::MeleeCone, 10,  500, 0,  200.0f, 60.0f, 0 },		// W-11
	{ WeaponId::Sword,        WeaponHit::MeleeCone, 25,  500, 0,  300.0f, 60.0f, 0 },		// W-15
	{ WeaponId::BeamSword,    WeaponHit::MeleeCone, 40,  333, 6,  400.0f, 60.0f, 0 },		// W-16
	{ WeaponId::Pistol,       WeaponHit::Ray,       10,  250, 12, 0.0f,   0.0f,  0 },		// W-13
	{ WeaponId::Flamethrower, WeaponHit::Cone,      2,   33,  90, 600.0f, 15.0f, 0 },		// W-14, A-13
	{ WeaponId::Railgun,      WeaponHit::Beam,      20,  1000, 2, 0.0f,   0.0f,  0 },		// W-17
	{ WeaponId::Taser,        WeaponHit::Ray,       1,   1000, 1, 300.0f, 0.0f,  2000 },	// W-12
	{ WeaponId::Grenade,      WeaponHit::Thrown,    120, 1000, 1, 600.0f, 0.0f,  0 },		// W-18
	{ WeaponId::BlastMine,    WeaponHit::Mine,      80,  1000, 1, 0.0f,   0.0f,  0 },		// W-19
	{ WeaponId::ShockMine,    WeaponHit::Mine,      20,  1000, 1, 0.0f,   0.0f,  2000 },	// W-20
};
inline const WeaponType* WeaponOf(WeaponId id)
{
	for (const WeaponType& w : WeaponTypes)
		if (w.id == id)
			return &w;
	return nullptr;
}
constexpr size_t MaxShotsPerTick = 4;				// 간격이 틱(50ms)보다 짧은 무기(화염방사기)는 한 틱에 여러 번

// 레일건 [명세 W-17]: 폭 50cm, 벽 무시. 누르면 1초 충전(판정 없음) → 충전 끝 + 200ms마다 (6번, 1초).
// 충전·발사 2초 동안 이동·회전 잠금 (클라 2026-11: 1초 충전 후 발사)
constexpr float RailgunHalfWidthCm = 25.0f;
constexpr uint32_t RailgunChargeMs = 1000;
constexpr uint32_t RailgunEveryMs = 200;
constexpr uint8_t RailgunHits = 6;
constexpr uint32_t RailgunBeamMs = 1000;			// 첫 판정 ~ 마지막 판정
constexpr uint32_t RailgunLockMs = RailgunChargeMs + RailgunBeamMs;

// 투척 [가정 A-14]: 커서 쪽으로 최대 6m, 1초 동안 날아감
constexpr uint32_t ThrowFlightMs = 1000;
constexpr float ThrowMaxCm = 600.0f;

// 폭발 [명세 W-18~20, 가정 A-15]
constexpr float GrenadeRadiusCm = 600.0f;
constexpr float MineRadiusCm = 300.0f;
constexpr float MineDetectCm = 100.0f;
constexpr uint32_t MineArmMs = 3000;
constexpr uint32_t MineWarnMs = 500;

// ── 스킬 (§9) ──
constexpr uint32_t SkillCooldownMs = 30000;
constexpr float SkillTaserRangeCm = 300.0f;			// [명세 S-01]
constexpr uint32_t StunMs = 2000;					// 테이저(스킬·무기)·감전 지뢰·섬광탄(경비원)
constexpr uint32_t StickyCastMs = 1000;				// [명세 S-02]
constexpr float StickyBackCm = 100.0f;
constexpr float StickyRadiusCm = 300.0f;
constexpr uint32_t StickyLifeMs = 10000;
constexpr float StickySlow = 0.5f;					// 이동 속도 −50% (설치자 포함)
constexpr uint32_t CloakMs = 20000;					// [명세 S-03]

// ── 소비 아이템 (§11.7) ──
constexpr int32_t HealKitHp = 30;
constexpr float SmokeRadiusCm = 500.0f;
constexpr uint32_t SmokeLifeMs = 10000;
constexpr float FlashRadiusCm = 300.0f;
constexpr uint32_t FlashPlayerMs = 3000;
constexpr uint32_t JetpackMs = 3000;
constexpr float JetpackPushCm = 300.0f;
constexpr uint32_t JetpackGuardStunMs = 1000;

// ── 환금 아이템 (§11.3) [가정 A-01] ──
struct QualityType { ValuableQuality quality; uint32_t minValue; uint32_t maxValue; };
inline constexpr QualityType QualityTypes[] = {
	{ ValuableQuality::Common, 1, 29 },
	{ ValuableQuality::Advanced, 30, 99 },
	{ ValuableQuality::Rare, 100, 500 },
};
constexpr uint32_t TreasureFirstValue = 2000;		// [명세 I-22]
// 다음 보물 가치 = 직전 × 1.2, 일의 자리 올림(10$ 단위) (Q-23)
constexpr uint32_t NextTreasureValue(uint32_t value) { return (value * 12 + 99) / 100 * 10; }

// ── 상자 (§11.5) [명세 O-11·O-12, 가정 A-02] ──
// 상자는 두 종류 (클라 2026-11-24). Box의 typeId = BoxType
//   무기 상자: 무기 80% · 소비 아이템 20%      환금 상자: 환금 아이템 100% (일반 60 · 고급 30 · 희귀 10)
//   빈 상자·필드 아이템(회복약)은 상자에서 나오지 않는다 (회복약은 맵에 20초마다). 부서지면 같은 자리에 같은 종류로
//   (전에는 한 종류: 무기 60 · 필드 10 · 없음 5 · 소비 15 · 일반 6 · 고급 3 · 희귀 1)
constexpr int32_t BoxHp = 20;
constexpr size_t BoxCount = 20;
constexpr size_t ValuableBoxCount = 6;				// 무기 상자 14 : 환금 상자 6
static_assert(ValuableBoxCount <= BoxCount);
constexpr uint32_t BoxRespawnMinMs = 30000;
constexpr uint32_t BoxRespawnMaxMs = 60000;
constexpr uint8_t WeaponBoxConsumablePct = 20;		// 무기 상자에서 소비 아이템이 나올 확률 (나머지는 무기)

struct BoxValuableChance { ValuableQuality quality; uint8_t percent; };
inline constexpr BoxValuableChance BoxValuables[] = {
	{ ValuableQuality::Common, 60 }, { ValuableQuality::Advanced, 30 }, { ValuableQuality::Rare, 10 },
};
consteval int SumBoxValuables() { int s = 0; for (const BoxValuableChance& c : BoxValuables) s += c.percent; return s; }
static_assert(SumBoxValuables() == 100, "환금 상자 품질 비율의 합은 100");

// 무기 상자에서 무기가 나왔을 때 무기별 비율(%). 권총·테이저건은 잘, 레일건·화염방사기는 드물게 (클라 2026-11-22).
// 삼단봉은 기본 무기라 나오지 않는다
struct BoxWeaponChance { WeaponId weapon; uint8_t percent; };
inline constexpr BoxWeaponChance BoxWeapons[] = {
	{ WeaponId::Pistol, 20 }, { WeaponId::Taser, 20 }, { WeaponId::Sword, 15 }, { WeaponId::Grenade, 12 },
	{ WeaponId::BlastMine, 8 }, { WeaponId::ShockMine, 8 }, { WeaponId::BeamSword, 7 },
	{ WeaponId::Flamethrower, 5 }, { WeaponId::Railgun, 5 },
};
consteval int SumBoxWeapons() { int s = 0; for (const BoxWeaponChance& c : BoxWeapons) s += c.percent; return s; }
static_assert(SumBoxWeapons() == 100, "상자 무기 비율의 합은 100");

// ── 필드 아이템 (§11.6) [명세 I-11~16, 가정 A-04] ──
struct FieldItemType { FieldItemId id; int32_t heal; uint8_t percent; };
inline constexpr FieldItemType FieldItemTypes[] = {
	{ FieldItemId::PotionS, 10, 60 }, { FieldItemId::PotionM, 30, 30 }, { FieldItemId::PotionL, 50, 10 },
};
inline int32_t HealOf(FieldItemId id)
{
	for (const FieldItemType& f : FieldItemTypes)
		if (f.id == id)
			return f.heal;
	return 0;
}
constexpr size_t FieldItemStart = 5;
constexpr uint32_t FieldItemEveryMs = 20000;
constexpr size_t FieldItemMax = 20;
constexpr float ItemSpacingCm = 100.0f;				// 무작위로 놓을 때 다른 오브젝트와 떨어질 거리

// ── 금고·반납 지점 ──
constexpr size_t SafeRoomsNeeded = MaxRoomPlayers + 1;	// 금고: 의적 시작 방 3곳을 빼고도 1곳 (맵 검사)
// 반납 구역 (클라 2026-11-23, 싸움 유도): 3곳 · 30초 · 구역 안에서 4초 머물러야 반납, 한 번에 한 명.
// 2초 이하 남은 구역에서는 시작할 수 없고, 반납 도중 구역이 닫히면 실패 (버저비터 없음)
constexpr size_t ReturnPointCount = 3;				// [명세 O-21] 전에는 5곳
constexpr uint32_t ReturnPointLifeMs = 30000;		// 전에는 60초
constexpr uint32_t ReturnPointWarnMs = ReturnPointLifeMs - 5000;	// 사라지기 5초 전 빨강
constexpr uint32_t ReturnDeliverMs = 4000;			// 반납에 걸리는 시간 (작업 속도 퍽과 관계없이 4초)
constexpr uint32_t ReturnMinLeftMs = 2000;			// 이 이하로 남으면 반납 시작 불가
static_assert(ReturnMinLeftMs < ReturnDeliverMs && ReturnDeliverMs < ReturnPointWarnMs);

// ── 맵 ──
constexpr float NavWallPaddingCm = 25.0f;		// 경로 격자를 만들 때 벽을 이만큼 넓혀서 막는다

// ── 경비원 (§12) ──
constexpr int32_t GuardMaxHp = 200;					// [명세 N-05]
constexpr uint32_t GuardRepairMs = 5000;			// [명세 N-06] 무적·정지
constexpr float GuardWalkSpeed = 210.0f;			// [명세 N-03] 플레이어 걷기 300 × 0.7
constexpr float GuardRunSpeed = 420.0f;				// 플레이어 달리기 600 × 0.7
constexpr float GuardTurnDegPerSec = 90.0f;			// [명세 N-04]
constexpr float GuardSightRangeCm = 1500.0f;		// [명세 N-11] 탐지 범위
constexpr float GuardSightHalfAngleDeg = 45.0f;		// 시야각 90°
constexpr float GuardAttackHalfAngleDeg = 30.0f;	// [명세 12.2] 공격 범위 60°
constexpr float GuardSprayHalfAngleDeg = 30.0f;	// [명세 12.2] 난사: 타겟 방향 중심 60° 부채꼴 안 무작위
constexpr int32_t GuardGunDamage = 10;
constexpr uint32_t GuardGunEveryMs = 100;
constexpr float GuardEyeCm = 160.0f;				// 경비원 눈 높이 (위치는 발 기준)
constexpr float ThiefBodyCm = 100.0f;				// 시야 검사 때 플레이어의 이 높이(가슴)를 본다
constexpr uint32_t ChaseLoseMs = 3000;				// 시야에서 이만큼 벗어나면 놓침
constexpr uint32_t SearchLookMs = 2000;				// 놓친 곳에서 둘러보는 시간
constexpr uint32_t PatrolLookMs = 1000;				// 순찰 갈림길에서 둘러보는 시간
constexpr uint32_t RepathMs = 500;					// 추격 중 경로를 다시 찾는 간격

// ── 경비원 AI 개선 (docs/경비원AI_공격탐색_개선안.md) ──
// 위의 기본 수치(이동·회전 속도, 시야, 공격 범위)는 바꾸지 않는다. 아래는 행동 방식의 조정값.
constexpr float EvadeRangeCm = 400.0f;				// A-2 이 거리 안에서 타겟이 정면 60° 밖이면 물러나며 돌아섬. 정면에 두고 이보다 벌어지면 끝
constexpr float EvadeDiagonalDeg = 45.0f;			// A-2 물러나는 방향을 타겟이 도는 쪽으로 비트는 각도
constexpr uint32_t EvadeMs = 2000;					// A-2 마지막으로 시작 조건이 생긴 뒤 이만큼 지나면 끝
constexpr float EvadeSideMinSpeed = 100.0f;			// A-2 타겟이 이보다 느리게 돌면 대각선이 아니라 바로 뒤로
constexpr float EvadeLookAheadCm = 60.0f;			// A-2 물러날 자리 너머 이만큼도 걸을 수 있어야 그쪽으로 감 (벽에 붙지 않게)
constexpr uint32_t PredictAheadMs = 1000;			// S-1 놓친 플레이어가 움직이던 방향으로 이 시간만큼 앞
constexpr float PredictMaxCm = 300.0f;				// S-1 최대 3m
constexpr float PredictMinSpeedCm = 50.0f;			// S-1 이보다 느리면 놓친 곳 그대로
constexpr uint32_t SearchSpinMs = 4000;				// S-3 첫 지점에서 한 바퀴 (90°/s × 4초 = 360°). 끄면 SearchLookMs(2초, N-19)
constexpr size_t SearchExtraSpots = 2;				// S-4 주변 지점 수
constexpr float SearchSpotMinCm = 300.0f;			// S-4 첫 지점에서 3~7m
constexpr float SearchSpotMaxCm = 700.0f;
constexpr float SearchSpotGapCm = 300.0f;			// S-4 지점끼리 이만큼은 떨어지게
constexpr float SearchDetourMax = 2.0f;				// S-4 걸어서 직선 거리의 2배를 넘게 돌아가면 뺀다
constexpr size_t SearchPathTries = 8;				// S-4 경로 찾기 최대 횟수 (탐색을 시작할 때만)
constexpr float SearchSweepDeg = 60.0f;				// S-4 주변 지점에서 좌우 60° 훑기 (약 2.7초)
constexpr float AllyCallRangeCm = 2000.0f;			// S-5 이 거리 안의 순찰·탐색 중인 동료를 부른다
constexpr uint32_t StunMemoryMs = 6000;				// S-6 경직·수리가 풀렸을 때 이 시간 안에 본(맞은) 곳을 탐색
constexpr uint32_t PatrolJitterMs = 20000;			// P-1 "오래 안 간 곳"에 섞는 무작위 폭 (예측당하지 않게)
constexpr float AllySpreadCm = 1000.0f;				// P-2 이 거리 안의 동료 쪽 지점은 뒤로 미룸
constexpr float AllySpreadDeg = 60.0f;				// P-2 동료 방향에서 이 각도 안이면 "그쪽"
constexpr uint32_t AllySpreadPenaltyMs = 60000;		// P-2 뒤로 미루는 정도 (60초 전에 다녀간 것만큼)
constexpr float PatrolSweepDeg = 45.0f;				// P-4 지점에서 왼쪽 45° → 오른쪽 45° 훑기 (90°/s로 1.5초, 가운데로 돌아오지 않음)
constexpr uint32_t ImportantRoomBonusMs = 30000;	// P-5 금고·반납 지점이 있는 방은 30초 더 오래 안 간 것으로
constexpr float LookBackChance = 0.2f;				// P-6 지점에서 왔던 쪽을 돌아볼 확률 (180°, 2초)
constexpr float AlertLookBackChance = 0.5f;			// P-6·P-7 경계 순찰 중에는 더 자주
constexpr uint32_t AlertPatrolMs = 30000;			// P-7 탐색을 마친 뒤 경계 순찰 시간
constexpr float AlertRadiusCm = 1500.0f;			// P-7 놓친 곳에서 이 안의 지점을 먼저
constexpr uint32_t AlertBonusMs = 60000;			// P-7 그 지점에 더하는 점수
constexpr uint32_t TargetVelGapMs = 1000;			// 타겟 속도 추정: 두 관측 사이가 이보다 길면 속도를 모름
constexpr float TargetVelMaxCm = 800.0f;			// 타겟 속도 추정의 상한 (순간이동 등 튀는 값)

// ── 시간 ──
// 게임 시간은 시험에서 줄여야 하므로 상수가 아니라 바꿀 수 있는 값으로 둔다.
struct Timing
{
	uint32_t tickMs = 50;				// 20Hz (protocol.h TICK_HZ)
	uint32_t gameMs = 300000;			// 5분 [명세 G-04]
	uint32_t loadTimeoutMs = 30000;		// 로딩 완료를 기다리는 최대 시간
	uint32_t resultMs = 15000;			// 결과 화면 뒤 자동으로 메인 복귀
};

inline Timing& CurrentTiming()
{
	static Timing timing;
	return timing;
}

} // namespace Content::GameData
