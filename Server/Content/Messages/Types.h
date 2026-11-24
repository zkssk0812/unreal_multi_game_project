#pragma once

// 콘텐츠가 쓰는 기본 타입. protocol.h의 enum과 값이 같아야 한다(MessageEncoder.cpp가 컴파일할 때 확인).
// 콘텐츠는 protocol.h를 include하지 않으므로 같은 뜻의 타입을 자기 쪽에 따로 둔다.
// 프로토콜 v3.0 (docs/psd.md 3장)

#include <cstdint>

namespace Content {

struct Vec3 { float x = 0, y = 0, z = 0; };	// cm (UE 기준 Z-up)

// VersionMismatch처럼 브리지가 판단하는 코드는 여기 없다.
enum class Result : uint16_t
{
	Ok = 0,
	InvalidFormat = 1, ServerBusy = 2, InternalError = 3, NotAllowedNow = 4,
	NameInUse = 100, NameInvalid = 101,
	RoomNotFound = 200, RoomFull = 201, RoomNotWaiting = 202, AlreadyInRoom = 203, RoomNameInvalid = 204,
	CharacterTaken = 210, CharacterInvalid = 211, PerkInvalid = 212, PerkDuplicate = 213,
	NotReadyable = 214,
};

enum class RoomState : uint8_t { Waiting, Loading, Playing, Result };
enum class LeaveReason : uint8_t { Self, Disconnected, GameEnded };	// GameEnded: 게임이 끝나 메인으로 돌아감

// SC_ROOM_ERROR가 "어떤 요청이 실패했나"를 알리는 데 쓴다. 브리지가 패킷 ID로 바꾼다.
enum class RoomRequest : uint8_t { CreateRoom, JoinRoom, LeaveRoom, SelectCharacter, SelectPerks, Ready };

enum class EntityKind : uint8_t
{
	Thief, Guard, Safe, ReturnPoint,
	Valuable,			// 땅의 환금 아이템. typeId = ValuableQuality, param = 가치($)
	WeaponPickup,		// typeId = WeaponId, param = 남은 탄
	ConsumablePickup,	// typeId = ConsumableId
	FieldItem,			// typeId = FieldItemId (닿으면 효과)
	Box,				// 부수면 아이템이 나옴. typeId = BoxType
	StickyTrap,			// 끈끈이 함정. owner = 설치자
	Mine,				// typeId = WeaponId(BlastMine/ShockMine)
	Smoke,				// 연막
	Door,				// 방 사이 문 (2026-11-24). typeId = 문 번호(맵 doors 위치), Opened 비트 = 열림
};

enum class ActionType : uint8_t
{
	None, AttackStart, AttackStop, Dodge,
	InteractBegin, InteractCancel,		// 대상 = targetEntityId
	SwitchWeapon,						// 무기1 ↔ 무기2 (마우스 휠)
	UseSkill,							// Q. 은신 중 다시 누르면 해제
	UseConsumable,						// E
};

enum class RejectReason : uint8_t
{
	InvalidState, Cooldown, NoStamina, OutOfRange, TargetBusy, NotAvailable,
	Downed, Stunned, Locked /*레일건·제트팩 중, 사제 테이저건을 든 중(무기 교체·소비 아이템)*/, HpFull, OnlyOneWeapon,
};

enum class EndReason : uint8_t { TimeUp, Aborted };
enum class InteractState : uint8_t { Begin, Cancel, Complete };
enum class AlertLevel : uint8_t { Patrol, Chase, Attack, Search, Repairing, Stunned };

enum class WeaponId : uint8_t
{
	None, Baton, Sword, BeamSword, Pistol, Flamethrower, Railgun, Taser, Grenade, BlastMine, ShockMine,
};
enum class ConsumableId : uint8_t { None, HealKit, Smoke, Flashbang, Jetpack };
enum class FieldItemId : uint8_t { None, PotionS, PotionM, PotionL };
enum class ValuableQuality : uint8_t { Common, Advanced, Rare, Unique /*보물*/ };
enum class BoxType : uint8_t { None, Weapon /*무기·소비 아이템*/, Valuable /*환금 아이템*/ };
enum class SkillId : uint8_t { None, Taser, Sticky, Cloak };
enum class SkillPhase : uint8_t { Cast, Active, End };
enum class DownCause : uint8_t { Player, Guard };
enum class ExplosionKind : uint8_t { Grenade, BlastMine, ShockMine, Flashbang };

// 엔티티 상태 비트 (protocol.h의 EntityStateBit과 같은 값, 32비트)
namespace StateBit {
constexpr uint32_t Running          = 1u << 0;	// 플레이어·경비원
constexpr uint32_t Dodging          = 1u << 1;	// 플레이어: 구르기(무적)
constexpr uint32_t Interacting      = 1u << 2;
constexpr uint32_t Exhausted        = 1u << 3;	// 탈진
constexpr uint32_t Attacking        = 1u << 4;	// 공격 키를 누르는 중 / 경비원 사격 중
constexpr uint32_t Downed           = 1u << 5;	// 쓰러져 리스폰 대기
constexpr uint32_t Invulnerable     = 1u << 6;	// 리스폰 무적 / 경비원 자가 수리
constexpr uint32_t Stunned          = 1u << 7;	// 경직
constexpr uint32_t Slowed           = 1u << 8;	// 끈끈이 위
constexpr uint32_t Cloaked          = 1u << 9;	// 은신
constexpr uint32_t InSmoke          = 1u << 10;	// 연막 안
constexpr uint32_t Flashed          = 1u << 11;	// 섬광 (플레이어)
constexpr uint32_t RailgunLock      = 1u << 12;	// 레일건 충전·발사 중 2초 (이동·회전 불가)
constexpr uint32_t Jetpack          = 1u << 13;	// 제트팩 비행 중
constexpr uint32_t Repairing        = 1u << 14;	// 경비원 자가 수리
constexpr uint32_t LeftGame         = 1u << 15;	// 접속 끊김
constexpr uint32_t Opened           = 1u << 16;	// 금고·문: 열림
constexpr uint32_t Warning          = 1u << 17;	// 반납 지점: 곧 사라짐 (빨간색)
constexpr uint32_t Armed            = 1u << 18;	// 지뢰: 작동 중
constexpr uint32_t Triggered        = 1u << 19;	// 지뢰: 감지됨 (0.5초 경고)
constexpr uint32_t CarryingTreasure = 1u << 20;	// 플레이어: 보물을 들고 있음
constexpr uint32_t TaserReady       = 1u << 21;	// 플레이어: 사제 테이저건을 든 상태 (Q, 공격 키로 발사)
}

// CS_MOVE의 flags (protocol.h의 MoveFlag과 같은 값)
namespace MoveFlag {
constexpr uint8_t Moving  = 1 << 0;
constexpr uint8_t Running = 1 << 1;
}

} // namespace Content
