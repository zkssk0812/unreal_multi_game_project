#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Network/VHProtocol.h"
#include "Gameplay/VHInteractionAuthority.h"
#include "Gameplay/VHInteractionTypes.h"
#include "Gameplay/VHGameplayTypes.h"
#include "Gameplay/VHTickCause.h"
#include "VHServerGameSubsystem.generated.h"

class AVHNetProxy;
class AVHInteractableBase;
class AVHSafe;
class UVHNetSubsystem;
class UVHInteractionSubsystem;
class UVHInteractionComponent;
class UVHThiefStateComponent;
class UVHTreasureCarrierComponent;
class UVHMatchSubsystem;
class UVHServerGameSubsystem;
class UVHWeaponVisualComponent;
class APawn;
class UMaterialInterface;
struct FVHItemData;

/** 퍽 번호 (서버 GameData::Perk 와 같다) */
namespace VHPerk
{
	constexpr int32 Vest = 1;			// 방탄조끼: 보호막 20
	constexpr int32 DeathSave = 2;		// 기사회생: 한 번 HP 1로 버팀
	constexpr int32 Tools = 3;			// 유용한 도구: 작업 속도 +10%
	constexpr int32 Sneakers = 4;		// 운동화: 달리기 +20% (클라가 적용)
	constexpr int32 TreasureSense = 5;	// 보물탐색 (미니맵 표시)
	constexpr int32 RobotSense = 6;		// 로봇탐지 (미니맵 표시)
	constexpr int32 BioSense = 7;		// 생체감지 (미니맵 표시)
}

/**
 * 서버와 게임 중일 때의 상호작용 담당. Evaluate(무엇을 할 수 있어 보이는가)는 안내 문구·테두리 색에만 쓰고,
 * 시작·취소는 CS_ACTION 으로 서버에 보낸다. 결과는 서버 이벤트(UVHServerGameSubsystem)가 Apply* 로 반영한다.
 */
UCLASS()
class MCPTEST_API UVHServerInteractionAuthority : public UVHLocalInteractionAuthority
{
	GENERATED_BODY()

public:
	virtual FVHInteractOffer Evaluate(const UVHInteractionComponent* By, const AVHInteractableBase* Target) const override;
	virtual void RequestBegin(UVHInteractionComponent* By, AVHInteractableBase* Target) override;
	virtual void RequestCancel(UVHInteractionComponent* By) override;

	TWeakObjectPtr<UVHServerGameSubsystem> Game;
};

/** SC_PLAYER_STATUS 내용 (본인에게만 오는 내 상태). HUD가 읽는다 */
struct FVHServerPlayerStatus
{
	bool bValid = false;
	int32 Hp = 0;
	int32 Shield = 0;
	int32 Stamina = 0;				// 0~100
	uint8 WeaponId[2] = { 0, 0 };	// RobProto::WeaponId
	int32 WeaponAmmo[2] = { 0, 0 };	// 0 = 무한(삼단봉·도검) 또는 빈 칸
	int32 ActiveSlot = 0;
	uint8 Consumable = 0;			// RobProto::ConsumableId
	uint8 Skill = 0;				// RobProto::SkillId
	int32 SkillCooldownMs = 0;
	int32 SkillActiveMs = 0;
	bool bDeathSaveLeft = false;
	double ReceivedSeconds = 0.0;	// FPlatformTime::Seconds (쿨타임을 화면에서 줄여 보일 때 기준)
};

/**
 * 서버(프로토콜 v3.0. 실서버 또는 가짜 서버)와 게임 중일 때 게임 레벨에서 서버 패킷을 처리한다. 판정은 모두 서버가 한다.
 *
 *  - 보내기: CS_MOVE(발 위치·시선·달리기, 최대 20Hz), CS_ACTION(공격 시작/끝, 구르기, 상호작용, 무기 교체, 스킬, 소비 아이템)
 *  - 받기: SC_GAME_START / SC_SNAPSHOT / SC_ENTITY_SPAWN·DESPAWN → 엔티티 표(Entities)와 화면의 액터
 *      · 플레이어·경비원 = AVHNetProxy (내 캐릭터는 내 폰)
 *      · 금고 = 레벨에 놓인 금고 후보와 위치로 짝짓기(서버는 한 번에 1개만 보낸다 → 나머지 후보는 숨김)
 *      · 반납지점·떨어진 환금 아이템 = 서버 위치에 생성
 *  - 이벤트 4001~4015: SC_DAMAGE·SC_DOWNED·SC_RESPAWNED·SC_MONEY·SC_PLAYER_STATUS·SC_PICKUP·SC_TREASURE_TAKEN 등
 *
 * 서버 없이 게임 레벨을 직접 ▶ 플레이할 때는 아무것도 하지 않는다(이동·카메라·HUD 배치만 볼 수 있다).
 */
UCLASS()
class MCPTEST_API UVHServerGameSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** 서버와 게임 중인가 (게임 레벨이 시작될 때 정해짐) */
	bool IsActive() const { return bActive; }
	/**
	 * 내 작업 속도 (서버와 같은 단위: 기본 100, 유용한 도구 +10 → 110. 팀 서버 0168a0a).
	 * SC_INTERACT 의 duration·progress 는 이미 이 속도로 계산된 실제 ms 로 오므로 게이지에는 쓰지 않고, 시작 전 안내 시간·로그에만 쓴다
	 */
	float GetMyWorkSpeed() const;
	uint16 GetSelfEntityId() const { return SelfEntityId; }
	/** 내 상태 비트 (RobProto::EntityStateBit, 스냅샷 기준) */
	uint32 GetSelfState() const { return SelfState; }
	bool HasSelfState(uint32 Bits) const { return (SelfState & Bits) != 0; }
	const FVHServerPlayerStatus& GetMyStatus() const { return MyStatus; }
	/** 개발용(vh.Consumable N): HUD 확인용으로 내 소비 아이템 표시만 바꾼다 (다음 서버 상태가 오면 서버 값으로 돌아간다) */
	void DebugSetConsumable(uint8 Id) { MyStatus.Consumable = Id; }
	/** 쓰러져 리스폰을 기다리는 중이면 남은 초, 아니면 0 */
	float GetRespawnRemaining() const;
	/** 내가 방에서 고른 퍽에 이 번호가 있는가 (VHPerk::…) */
	bool HasMyPerk(int32 PerkId) const;
	/** 섬광에 맞아 화면이 하얀 정도 (1 = 새하얌 → 3초에 걸쳐 0). HUD가 읽는다 */
	float GetFlashAlpha() const;
	/** 섬광 중인가 (3초 동안 미니맵을 숨긴다) */
	bool IsFlashed() const { return GetFlashAlpha() > 0.f; }
	/** 보물탐색 퍽: 방금 누군가 보물을 얻은 위치 (미니맵은 이 "방향"만 표시한다). 퍽이 없거나 시간이 지났으면 false */
	bool GetTreasureSense(FVector& OutPos) const;
	/** 내가 쓰러지며 떨어뜨린 보물이 아직 바닥에 있으면 위치·가치 (미니맵 ◇). 누가 주우면 false */
	bool GetMyDroppedTreasure(FVector& OutPos, int32& OutValue) const;
	/** 리스폰 뒤 화면 방향 표시: 남은 시간(초). 0이면 표시하지 않는다 (보물을 누가 주우면 바로 0) */
	float GetDroppedTreasureHintRemaining() const;
	/**
	 * 개발용(콘솔 VHFake 이름 [초]): 서버가 보낸 것처럼 내 상태·이벤트를 흉내 낸다. 내 화면에만 적용되고 서버는 모른다.
	 * 상태: knockback, jetpack, railgun, sticky, cloak, stun, flash, smoke
	 * 연출: grenade, flashbang, blastmine, shockmine, smokefx, trap, railbeam, pistol, baton, flame
	 */
	void DebugFake(const FString& What, float Seconds);

	struct FEntity
	{
		uint16 Id = 0;
		RobProto::EntityKind Kind = RobProto::EntityKind::Thief;
		uint16 TypeId = 0;
		uint64 Owner = 0;
		FVector Pos = FVector::ZeroVector;
		float Yaw = 0.f;
		uint32 State = 0;
		uint16 Hp = 0;
		uint32 TimerMs = 0;
		uint32 Param = 0;
		RobProto::ActionType Action = RobProto::ActionType::None;
		RobProto::WeaponId Weapon = RobProto::WeaponId::None;
		TWeakObjectPtr<AVHNetProxy> Proxy;					// 다른 플레이어·경비원
		TWeakObjectPtr<AVHInteractableBase> Object;		// 금고·반납지점·떨어진 아이템
		TWeakObjectPtr<AActor> Fx;							// 끈끈이·지뢰·연막 (표시만)
	};
	const TMap<uint16, FEntity>& GetEntities() const { return Entities; }

	// ── 서버 판정 담당이 부른다 ──
	uint16 FindEntityIdFor(const AVHInteractableBase* Object) const;
	/** 이 물품이 지금 들고 있는 것과 같은 종류의 소비 아이템인가 (주워도 바뀌는 것이 없음 → 대상 우선순위를 낮추고 줍기를 막는다) */
	bool IsSameAsHeldConsumable(const AVHInteractableBase* Object) const;
	void SendInteractBegin(UVHInteractionComponent* By, AVHInteractableBase* Target, const FVHInteractOffer& Offer);
	void SendInteractCancel(UVHInteractionComponent* By);
	/**
	 * 문 F (2026-11-24 팀 서버): CS_ACTION InteractBegin(문 엔티티 id). 열림은 서버 스냅샷 ES_OPENED 로만 바뀐다(UVHDoorSubsystem).
	 * 거절(SC_ACTION_REJECT)은 문 전용 안내 (TargetBusy = 문간에 누가 있음, OutOfRange = 150cm 밖)
	 */
	void SendDoorInteract(uint16 DoorEntityId, bool bWantOpen, const FString& DoorDesc);

	// ── 플레이어 컨트롤러가 부른다 ──
	/** 공격 키를 누름/뗌 (AttackStart / AttackStop). 누르는 동안 서버가 무기 간격마다 공격한다 */
	void SendAttackHeld(bool bHeld);
	/**
	 * 레일건 (2026-11-22 팀 서버): 서버가 누른 순간 SC_FIRE·탄약 → 1초 충전(판정 없음) → 200ms마다 6번 판정, 이동·회전 잠금 2초(ES_RAILGUN_LOCK), 충전 중 쓰러지면 발사 안 됨.
	 * 클라는 클릭 즉시 AttackStart(클릭 한 번 = 한 발: 내 SC_FIRE 를 받으면 AttackStop). 충전 연출(소리·빛 1초)과 빔은 SC_FIRE 기준이라
	 * 다른 플레이어의 레일건도 똑같이 보인다(PendingRailShots). 충전 중 쏜 사람이 쓰러지면 연출을 끊는다(서버도 쏘지 않음)
	 */
	bool IsRailgunCharging() const;
	/** 쏜 사람의 레일건 충전 연출을 끊는다 (서버 기준: 충전 중 쓰러짐·사라짐). Reason = 로그용 */
	void CancelRailgunShot(uint16 Shooter, const TCHAR* Reason);
	/** 내 캐릭터의 꾹 누르기 표시를 지금 보낸 공격 상태에 맞춘다 (보낼 때·거절·쓰러짐 즉시) */
	void ApplyMyAttackHeldVisual();
	void SendDodge();
	void SendSwitchWeapon();
	void SendUseSkill();
	void SendUseConsumable();
	/**
	 * Q 키. 도적1(사제 테이저건, 2026-11-22 팀 서버가 판정): Q = UseSkill → 서버가 들기(SC_SKILL Active, 상태 비트 ES_TASER_READY),
	 * 든 채로 Q 다시 = 집어넣기(SC_SKILL End, 쿨타임 없음). 든 채로 공격 키 = AttackStart → 서버가 무기 대신 테이저 발사(SC_SKILL Cast, 쿨타임 시작).
	 * 무기 교체·경직·쓰러짐·제트팩이면 서버가 집어넣는다. 다른 스킬(끈끈이·은신)은 예전처럼 바로 사용
	 */
	void PressSkillKey();
	/** 사제 테이저건을 든 상태 (서버 상태: 내 ES_TASER_READY 비트 / 내 SC_SKILL Active·End·Cast) */
	bool IsSkillTaserHeld() const { return bSkillTaserHeld; }
	/** 스킬 쿨타임 남은 초 (서버 상태 기준, 받은 뒤 흐른 시간을 뺌) */
	float GetSkillCooldownRemaining() const;

	// ── 게임 중 마우스 조준점(UVHCrosshairWidget)이 읽는다 (2026-11-25) ──
	/**
	 * 내 공격이 서버 판정으로 맞음: SC_DAMAGE(attacker = 나, 대상 = 다른 플레이어·경비원·상자) → bKill false,
	 * SC_DOWNED(attacker = 나) → bKill true. 클라 추측으로는 부르지 않는다. TargetKind = RobProto::EntityKind 값
	 */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FVHOnMyHitConfirmed, bool /*bKill*/, uint8 /*TargetKind*/);
	FVHOnMyHitConfirmed OnMyHitConfirmed;
	/** 내 공격 한 번 (내 SC_FIRE. 레일건은 충전이 끝나 빔이 나가는 순간). 조준점 반동 벌어짐 연출용 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FVHOnMyShot, uint8 /*WeaponId*/);
	FVHOnMyShot OnMyShot;
	/** 조준점에 쓸 지금 무기: 내 레일건 충전·빔 중이면 레일건(마지막 탄이면 서버 무기는 이미 삼단봉), 아니면 서버의 든 무기 */
	uint8 GetCrosshairWeapon() const;
	/** 든 무기의 남은 탄 (서버 SC_PLAYER_STATUS. 0 = 무한 또는 없음) */
	int32 GetMyActiveAmmo() const;
	/** 내 레일건: 충전 중이면 true + 진행도(0~1, 서버 SC_FIRE 기준 1초), 빔 중이면 bOutBeam. 둘 다 아니면 false */
	bool GetMyRailgunCharge(float& OutProgress, bool& bOutBeam) const;
	/** 내 마지막 공격(SC_FIRE) 시각 (FPlatformTime, 0 = 없음) — 조준점 쿨타임 원 */
	double GetMyLastShotTime() const { return MyLastShotTime; }
	uint8 GetMyLastShotWeapon() const { return MyLastShotWeapon; }
	/** 쿨타임 중 Q 를 눌러 막힌 시각 (FPlatformTime, 0 = 없음) — 조준점 옆에 잠깐 쿨타임 표시 */
	double GetSkillCooldownNoticeTime() const { return SkillCooldownNoticeTime; }

	/** 서버 환금 아이템(품질, 가치) → 클라 물건 표시 데이터 */
	static FVHItemData ItemForValuable(uint16 Quality, uint32 Value);
	/** 표시용 이름 */
	static const TCHAR* KindName(RobProto::EntityKind Kind);
	static const TCHAR* WeaponName(RobProto::WeaponId Id);
	static const TCHAR* ConsumableName(RobProto::ConsumableId Id);
	static const TCHAR* FieldItemName(RobProto::FieldItemId Id);
	/** 정보창용: 무기 대미지 ("10", "20×6", "40~120"). 서버 GameData.h 무기 표와 같은 표시용 값 */
	static const TCHAR* WeaponDamageText(RobProto::WeaponId Id);
	/** 정보창용: 소비 아이템 효과를 짧게 ("HP 30 회복", "시야 방해", "이동") [기획 그림] */
	static const TCHAR* ConsumableEffectText(RobProto::ConsumableId Id);
	static const TCHAR* SkillName(RobProto::SkillId Id);
	/** 한국어 조사: 앞 글자에 받침이 있으면 WithBatchim("을", "으로"…), 없으면 Without. "으로/로"는 ㄹ 받침이면 "로" */
	static FString WithJosa(const FString& Word, const TCHAR* WithBatchim, const TCHAR* Without);
	/** 엔티티 이름 (플레이어 이름 / 경비원 번호) */
	FString NameOfEntity(uint16 Id) const;

protected:
	void HandlePacket(uint16 Type, const uint8* Data, int32 Size);
	void ApplyGameStart(const RobProto::SC_GameStart& P);
	/** 플레이어 구분 색: 의적 엔티티 번호 순서(모든 클라가 같은 목록을 받으므로 같은 사람 = 같은 색) → 발밑 링·이름표 */
	void ApplyPlayerColors();
	void ApplySnapshot(const RobProto::SC_Snapshot& P);
	void AddEntity(const RobProto::EntitySpawn& S, bool bFromStart);
	void RemoveEntity(uint16 Id);
	void HandleEvent(uint16 Type, const uint8* Data, int32 Size);

	// 레벨 금고 짝짓기
	void HideLevelSafes();
	void SetLevelSafeActive(AVHSafe* Safe, bool bActiveNow);
	void BindSafe(FEntity& E);
	/** 새 맵: 레벨에 금고 자리가 없으므로 서버가 보낸 위치에 금고를 만든다 (서버 A-05: 방 안 무작위 칸) */
	void SpawnServerSafe(FEntity& E);
	/** 내가 방금 취소·공격·구르기·스킬·소비 아이템을 보냈나 (서버가 멈춘 Cancel의 원인이 나인지 구분) */
	bool SentOwnActionRecently() const;
	void ApplyObjectState(FEntity& E, uint32 OldState);
	void ApplySelfState(uint32 OldState, uint32 NewState);

	// 이벤트
	void OnInteract(const RobProto::SC_Interact& P);
	void OnMoney(const RobProto::SC_Money& P);
	void OnPlayerStatus(const RobProto::SC_PlayerStatus& P);
	void OnDamage(const RobProto::SC_Damage& P);
	/** 엔티티 번호 → 그 의적의 무기·몸 표시 컴포넌트 (나 또는 다른 플레이어 표시). 없으면 nullptr */
	class UVHWeaponVisualComponent* VisualOfEntity(uint16 EntityId) const;
	void OnFire(const RobProto::SC_Fire& P);
	void OnDowned(const RobProto::SC_Downed& P);
	void OnRespawned(const RobProto::SC_Respawned& P);
	void OnPickup(const RobProto::SC_Pickup& P);
	void OnReject(const RobProto::SC_ActionReject& P);
	/** 제트팩에 부딪혀 밀려남: 대상이 나면 내 캐릭터를 그 방향으로 그 거리만큼 민다 */
	void OnKnockback(uint16 Target, float Yaw, float Distance);
	/** 스킬 연출 (테이저 발사 선, 끈끈이 던지는 동작). 은신은 상태 비트(ES_CLOAKED)로 표시한다 */
	void OnSkill(const RobProto::SC_Skill& P);

	// ── 서버가 상태만 알려 주고 클라가 적용하는 것 ──
	/** 내 이동 속도 배율을 지금 상태 비트·퍽에 맞춘다 (끈끈이 −50%, 은신 −80%·특수 동작 불가, 운동화 달리기 +20%) */
	void UpdateSelfSpeed();
	/** 내 캐릭터를 반투명하게: 0 = 보통, 1 = 은신(하늘색), 2 = 연막 안(회색 윤곽). 다른 사람 화면에서는 아예 숨겨진다 */
	void SetSelfGhost(int32 Mode);

	// ── 연출 (임시 도형 AVHFxActor) ──
	void OnProjectile(const RobProto::SC_Projectile& P);
	void OnExplosion(RobProto::ExplosionKind Kind, const FVector& Pos, float Radius);
	/** 공격 한 번의 표시: 직선(총·테이저·레일건·경비원 사격) = 빔, 부채꼴(근접·화염) = 부챗살 */
	void SpawnFireFx(const FVector& Origin, float Yaw, float Range, float HalfAngle, const FLinearColor& Color, float Life, float Thickness, bool bThroughWalls);
	/** 지뢰의 상태(설치됨 → 작동 → 감지)에 맞춰 표시를 바꾼다 */
	void ApplyFxState(FEntity& E);

	void SendMoveIfNeeded();
	/** 지금 위치를 바로 CS_MOVE 로 (상호작용·공격 직전: 서버는 마지막 CS_MOVE 위치로 거리·판정을 잰다) */
	void SendCurrentPositionNow();
	void TickBotWalk(float DeltaTime);
	/** 개발용 봇(-VHBotHeist): 금고 → 보물 → 반납 되풀이 */
	void TickBotHeist(float DeltaTime);
	/** 개발용 봇(-VHBotHunt): 가장 가까운 다른 플레이어 옆으로 가서 공격한다 (플레이어끼리의 전투 시험용) */
	void TickBotHunt(float DeltaTime);
	/** 개발용 봇(-VHBotDownTest=Guard,Player): 보물을 든 채 경비원·플레이어 옆에 서서 쓰러진다. 처리했으면 true */
	bool TickBotDownTest(APawn* Pawn);
	/** 개발용(-VHBotDownTest): 쓰러짐 시험 화면 저장 (Saved/V3Test/down_<경우>_<번호>.png) */
	void DownTestShot(const TCHAR* Tag);
	APawn* GetLocalPawn() const;
	/** 시선(커서) 방향 yaw 와 커서가 가리키는 바닥 지점 */
	float GetLookYaw() const;
	FVector GetAimPoint() const;
	UVHInteractionSubsystem* GetInteraction() const;
	UVHMatchSubsystem* GetMatch() const;
	UVHInteractionComponent* GetMyInteraction() const;
	UVHThiefStateComponent* GetMyThief() const;
	UVHTreasureCarrierComponent* GetMyCarrier() const;
	AVHInteractableBase* ObjectFor(uint16 Id) const;
	float GetPawnHalfHeight(const APawn* Pawn) const;
	FString NameForPlayer(uint64 PlayerId) const;
	/** 그 플레이어가 방에서 고른 캐릭터 번호 (모르면 0 = 기본 모습) */
	int32 CharacterForPlayer(uint64 PlayerId) const;
	/** 내 캐릭터의 무기 표시 컴포넌트에 지금 든 무기·캐릭터 번호를 알린다 */
	void UpdateMyWeaponVisual();

	TWeakObjectPtr<UVHNetSubsystem> Net;
	UPROPERTY() TObjectPtr<UVHServerInteractionAuthority> ServerAuthority;
	FDelegateHandle PacketHandle;
	bool bActive = false;
	bool bStarted = false;
	bool bSafesHidden = false;
	bool bAnnounceNewSafe = false;	// 첫 금고는 알리지 않고, 그 뒤에 생기는 금고만 알린다
	int32 LevelSafeCount = 0;		// 레벨에 미리 놓인 금고 수. 0이면(새 맵) 서버가 보낸 위치에 금고를 만든다
	int32 PlaceChecks = 0;			// 배치 점검: 서버가 고른 자리 수 / 그중 레벨과 어긋난 수 ([동기화] 로그에 함께 남긴다)
	int32 PlaceCheckWarnings = 0;
	int32 FramesSeen = 0;			// [성능] 로그용: 5초 동안 그린 프레임 수
	FVHTickCause TickCause;					// 이번 틱에 받은 이벤트로 "공격받아 중단"·"상호작용으로 은신 해제"를 가린다
	double LastOwnActionSentTime = 0.0;			// 내가 취소·공격·구르기·스킬·소비 아이템을 보낸 때
	RobProto::ActionType LastOwnAction = RobProto::ActionType::None;	// 그때 보낸 것 (반납이 끊긴 이유 안내용)
	/**
	 * 반납 작업이 끊긴 틱의 기록 (2026-11-23 팀 서버: 반납 = 4초 시간 작업). 서버 Cancel 에는 이유가 없으므로
	 * 같은 틱의 일(구역 사라짐·경직·쓰러짐·맞음·내 행동·구역 밖)을 보고 스냅샷이 올 때 안내 문구를 고른다 (ResolveReturnCancel)
	 */
	struct FReturnCancel
	{
		bool bActive = false;
		uint16 TargetId = 0;
		bool bOwn = false;							// 방금 내가 취소·공격·구르기·스킬·소비 아이템을 보냄
		RobProto::ActionType OwnAction = RobProto::ActionType::None;
		bool bClosed = false;						// 같은 틱에 Despawn 된 반납지점 id 가 Cancel 의 target 과 같음 → 구역 닫혀 실패 (이것만 본다)
		float RemainingAtCancel = 0.f;				// Cancel 때 화면의 남은 수명(서버 timer_ms 기준, 로그용)
		bool bOutside = false;						// Cancel 때 내 위치가 구역 범위 밖
		float DistanceAtCancel = 0.f;
	};
	FReturnCancel ReturnCancel;
	/** 이번 틱(다음 스냅샷까지)에 사라진 반납지점 id (게임 시작 직후처럼 3곳이 함께 바뀌는 틱 확인용 로그) */
	TArray<uint16> TickDespawnedReturnPoints;
	/** 보낸 문 F (응답 = 스냅샷의 ES_OPENED 변화, 거절 = SC_ACTION_REJECT 같은 seq) */
	struct FPendingDoor
	{
		uint16 Seq = 0;
		uint16 DoorId = 0;
		bool bWantOpen = false;
		double SentTime = 0.0;
	};
	FPendingDoor PendingDoor;
	bool bApplyingGameStart = false;			// ApplyGameStart 안에서 시작 엔티티를 만드는 중
	float StartTimerLagSeconds = 0.f;			// SC_GAME_START 를 받은 뒤 적용까지 걸린 시간 (반납지점 남은 수명 보정)
	/** 스냅샷(틱 끝)에서: 끊긴 반납이 있으면 이유를 골라 안내하고 true (일반 "공격받아 작업이 멈춤" 안내 대신) */
	bool ResolveReturnCancel(bool bHitInterrupted);
	/** 반납 시작 거절 안내 (TargetBusy·NotAvailable 등). 반납 대상이 아니면 빈 문구 */
	FText ReturnRejectText(RobProto::RejectReason Reason) const;
	/** 사제 테이저건을 든 동안이면 보내지 않고 "Q로 먼저 집어넣으세요" 안내 후 true (무기 교체·소비 아이템, 2026-11-23 팀 서버 Locked) */
	bool BlockWhileTaserHeld(const TCHAR* What);
	double LastTaserBlockNoticeTime = 0.0;
	uint16 SelfEntityId = 0;
	uint32 SelfState = 0;
	float DurationSeconds = 300.f;
	TMap<uint16, FEntity> Entities;
	FVHServerPlayerStatus MyStatus;
	double RespawnAtSeconds = 0.0;		// FPlatformTime::Seconds 기준, 0 = 대기 중 아님
	bool bSelfSpeedApplied = false;		// 게임 시작 뒤 한 번 (운동화 퍽)
	int32 SelfGhostMode = 0;
	uint32 DebugSelfBits = 0;			// 개발용 흉내: 스냅샷의 내 상태에 덧씌우는 비트
	double DebugSelfBitsUntil = 0.0;
	/** 은신 중 바꿔 둔 내 캐릭터의 원래 머티리얼 */
	UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> SavedSelfMaterials;

	// 보낸 상호작용(응답 대기). 즉시 상호작용(줍기·반납)은 SC_INTERACT 없이 SC_PICKUP·SC_MONEY 로 결과를 안다
	struct FPending
	{
		uint16 Seq = 0;
		uint16 TargetId = 0;
		TWeakObjectPtr<UVHInteractionComponent> By;
		TWeakObjectPtr<AVHInteractableBase> Target;
		FVHInteractOffer Offer;
		bool bInstant = false;
		/** 소비 아이템 줍기를 보낼 때 들고 있던 것 (SC_PICKUP 이 SC_PLAYER_STATUS 보다 늦게 와도 교체 알림이 맞도록) */
		uint8 PrevConsumable = 0;
		double SentTime = 0.0;
		/** 길게 누르기가 클라에서 거의 끝난 뒤 손을 뗌 → 서버 완료가 곧 오므로 취소를 미뤄 둠 */
		bool bCancelDeferred = false;
		double CancelDeferredTime = 0.0;
	};
	FPending Pending;
	bool bAttackHeldSent = false;
	double MyLastShotTime = 0.0;		// 조준점 쿨타임 원: 내 마지막 SC_FIRE (FPlatformTime)
	uint8 MyLastShotWeapon = 0;
	double SkillCooldownNoticeTime = 0.0;

	// 레일건: 내 클릭 → AttackStart → 내 SC_FIRE 를 기다림 (받으면 AttackStop, 0.6초 안에 없으면 끝)
	bool bRailAwaitFire = false;
	double RailClickTime = 0.0;		// FPlatformTime (로그의 ms 계산)
	/** SC_FIRE(레일건)를 받은 쏜 사람별 충전 연출 → 1초 뒤 빔. 빔 1초 동안은 손에 레일건을 붙잡아 둔다(마지막 탄이면 서버 무기는 이미 삼단봉) */
	struct FRailShot
	{
		uint16 Shooter = 0;
		FVector Origin = FVector::ZeroVector;
		float Yaw = 0.f;
		double FireAt = 0.0;		// FPlatformTime: 빔(서버 첫 판정)
		double ReleaseAt = 0.0;		// FPlatformTime: 빔 끝 → 손 무기 붙잡기 풀기
		bool bFired = false;
		TWeakObjectPtr<UVHWeaponVisualComponent> Visual;
	};
	TArray<FRailShot> PendingRailShots;
	void TickRailgunShots();
	/** 사제 테이저건을 든 상태 (서버 상태를 따른다) */
	bool bSkillTaserHeld = false;
	/** 테이저건을 든 채 누른 공격 키 (이 누름은 테이저 한 발이라 꾹 누르기 자세를 보이지 않는다) */
	bool bTaserShotPress = false;
	void SetSkillTaserHeld(bool bHeld, const TCHAR* Why);
	uint8 MyActiveWeapon() const;

	// 연출
	struct FProjectileFx
	{
		TWeakObjectPtr<AActor> Fx;
		RobProto::ExplosionKind Kind = RobProto::ExplosionKind::Grenade;
	};
	TArray<FProjectileFx> Projectiles;			// 날아가는 수류탄·섬광탄 (터지면 지운다)
	TMap<uint16, double> LastConeFxSeconds;		// 화염방사기처럼 빠른 연사의 부채꼴 표시를 0.1초에 한 번으로 줄인다
	TMap<uint16, double> FakeEntityUntil;		// 개발용 흉내로 만든 엔티티(지뢰·연막·끈끈이)의 끝나는 시각
	uint16 NextFakeEntityId = 60000;
	double FlashStartSeconds = -1.0;			// 섬광에 맞은 시각 (FPlatformTime::Seconds), -1 = 없음
	FVector TreasureSensePos = FVector::ZeroVector;	// 다른 사람이 보물을 얻은 위치 (SC_TREASURE_TAKEN)
	double TreasureSenseSeconds = -1.0;

	// 내가 쓰러지며 떨어뜨린 보물 (미니맵 ◇ + 리스폰 뒤 화면 방향 표시)
	FVector DownPos = FVector::ZeroVector;		// 쓰러진 자리 (발 위치)
	double DownSeconds = -1.0;					// 쓰러진 시각. 그 뒤 잠깐 동안 근처에 생기는 환금 아이템이 내가 떨어뜨린 것
	int32 DownExpectTreasure = 0;				// 떨어질 보물 가치 (아직 엔티티가 안 왔으면 > 0)
	int32 DownExpectValuables = 0;				// 떨어질 환금 아이템 수 (플레이어에게 쓰러졌을 때만)
	int32 DownSeenValuables = 0;				// 실제로 근처에 생긴 환금 아이템 수 (확인 로그)
	bool bDownByGuard = false;
	uint16 MyDroppedTreasureId = 0;				// 바닥에 있는 내 보물 엔티티, 0 = 없음
	int32 MyDroppedTreasureValue = 0;
	double DroppedHintUntil = 0.0;				// 리스폰 뒤 방향 표시가 끝나는 시각
	FString DroppedTreasureTaker;				// 바닥의 내 보물을 주운 사람 (SC_TREASURE_TAKEN → 사라질 때 알림)
	TArray<FVHItemData> LastRemovedItems;		// SC_MONEY 로 소지품을 비운 직전 목록 (SC_DOWNED 보다 먼저 온 경우 대비)
	double LastRemovedSeconds = -1.0;
	int32 FiresSeen = 0;
	int32 DamagesSeen = 0;
	int32 HitShotsTaken = 0;

	// CS_MOVE 보내기
	double LastMoveSendTime = 0.0;
	FVector LastSentPos = FVector::ZeroVector;
	float LastSentYaw = 0.f;
	uint8 LastSentFlags = 0;
	int32 MovesSent = 0;
	int32 SnapshotsReceived = 0;
	double LastStatLogTime = 0.0;

	// 개발용 봇
	float BotWalkTime = 0.f;
	float BotAttackTime = 0.f;
	float BotHeistTimer = 0.f;
	// 개발용 봇 -VHBotRailTest (2026-11-20): 상자를 열어 레일건 획득 → 충전 발사 → 무기 교체 취소 → 경비원 앞에서 쓰러짐 취소
	void TickBotRailTest(float DeltaTime);
	// 개발용 봇 -VHBotTaserTest (2026-11-22): 다른 플레이어 앞에서 Q 들기 → Q 넣기(쿨타임 없음) → Q 들기 → 공격 키 발사(쿨타임 시작)
	void TickBotTaserTest(float DeltaTime);
	// 개발용 봇 -VHBotTaserStun (2026-11-24): 상자에서 테이저건(무기)을 구해, 사제 테이저건을 든 다른 플레이어를 쏴서 경직시킨다 (든 테이저건이 경직으로 취소 → 쿨타임 시험)
	void TickBotTaserStun(float DeltaTime);
	int32 TaserStunShots = 0;
	double TaserStunNext = 0.0;
	TMap<uint16, int32> TaserStunTries;
	// 개발용 봇 -VHBotReturnTest (2026-11-24): 4초 반납 시험 — 구역 밖 → 바쁨+맞음(짝 봇) → 늦음(NotAvailable) → 도중에 닫힘 → 완료. 로그 [반납 시험]
	bool TickBotReturnTest(APawn* Pawn, UVHInteractionComponent* I, bool bCarrying);
	// 개발용 봇 -VHBotReturnPartner (2026-11-24): 환금 아이템 하나를 든 채 기다리다, 다른 플레이어가 "신호 자리"(구역 가운데에서 30~60cm)에서 반납을 시작하면
	// 같은 구역에서 F(→ TargetBusy) → 1.2초 뒤 그 사람을 공격(→ 반납이 맞아서 끊김). 로그 [반납 짝]
	bool TickBotReturnPartner(APawn* Pawn, UVHInteractionComponent* I);
	int32 RetCase = 0;
	int32 RetStage = 0;
	int32 RetTries = 0;
	double RetT0 = 0.0;
	uint16 RetZone = 0;
	bool bRetMoved = false;
	int32 RetShotIndex = 0;
	FString RetResult;						// 마지막 반납 결과 (완료 / 끊긴 이유 / 거절 이유) — 시험 봇이 기대값과 비교
	uint16 PartnerZone = 0;					// 짝 봇: 다른 플레이어가 반납을 시작한 구역
	uint16 PartnerActor = 0;
	double PartnerSeenTime = 0.0;
	int32 PartnerStage = 0;
	double PartnerT0 = 0.0;
	int32 TaserTestStage = 0;
	double TaserTestT0 = 0.0;
	uint16 TaserTestTarget = 0;
	// 개발용 봇 -VHBotDoorShot (2026-11-24): 무기 상자를 열어 권총·레일건을 구한 뒤, 닫힌 문(-VHDoorShotX/Y, 기본 wall_119 앞) 너머
	// 다른 플레이어를 쏜다 → 권총은 막혀야(피해 0) 하고 문을 열면 맞음, 레일건은 닫힌 문을 뚫어야 함. 로그 [문 사격]
	void TickBotDoorShot(float DeltaTime);
	int32 DoorShotStage = 0;
	int32 DoorShotSub = 0;
	double DoorShotNext = 0.0;
	uint8 DoorShotWeapon = 0;				// 지금 시험하는 무기
	int32 DoorShotHitsBefore = 0;
	bool bDoorShotPistolDone = false;
	bool bDoorShotRailDone = false;
	TMap<uint16, int32> DoorShotTries;
	int32 DamageDealtToThieves = 0;			// 내가 다른 플레이어에게 준 SC_DAMAGE 수 (보호막만 깎여도 센다)
	/** 부서진 상자 (드롭 기록: 같은 자리에 1.5초 안에 생긴 아이템 = 그 상자에서 나온 것) */
	struct FBrokenBox { FVector Pos; uint16 Type = 0; double Time = 0.0; };
	TArray<FBrokenBox> BrokenBoxes;
	int32 RailTestStage = 0;
	int32 RailTestSub = 0;
	double RailTestT0 = 0.0;
	double RailTestNext = 0.0;
	uint16 RailTestTarget = 0;
	int32 RailTestTargetHp = 0;
	int32 RailTestBoxes = 0;
	TMap<uint16, int32> RailTestTries;
	FString RailLastCancel;
	int32 RailBeamsSeen = 0;		// 내 레일건 빔 수 (시험 로그)
	float BotHuntTimer = 0.f;
	float BotHuntAge = 0.f;			// 게임 시작 뒤 흐른 시간 (-VHBotHuntDelay)
	uint16 BotHuntTarget = 0;
	float BotSkillTimer = 0.f;
	int32 BotHeistStep = 0;			// 0 = 대상 고르고 이동, 1 = 화면 저장, 2 = F 누르기(필드 아이템은 밟기)
	uint16 BotGoalId = 0;
	int32 BotSwapBudget = 3;		// 칸이 찬 뒤에도 무기·소비 아이템을 바꿔 주워 보는 횟수 (끝없이 서로 바꾸지 않게)
	int32 BotLootShots = 0;
	double BotUseConsumableAt = 0.0;
	int32 BotDownStage = 0;			// -VHBotDownTest: 지금 시험 중인 경우 번호
	double BotDownIdleUntil = 0.0;	// 리스폰 뒤 촬영하는 동안 가만히
	bool bBotDownLogged = false;
	bool bBotDownShootRespawn = false;	// 이번 쓰러짐이 시험 대상(보물을 들고 쓰러짐)이라 리스폰 뒤에도 찍는다
	double BotHuntKillSeconds = -1.0;	// -VHBotHuntLoot: 내가 마지막으로 쓰러뜨린 시각
	int32 BotHuntLootStep = 0;
	/** 개발용(-VHBotPickTest): 다음 줍기 하나는 같은 종류 소비 아이템이라도 막지 않고 서버로 보낸다 (서버 동작 확인용) */
	bool bForceSameConsumableOnce = false;
	/** 개발용 털이 봇: 다가가도 F 대상이 잡히지 않은 횟수 (벽에 막힌 상자 등). 2번이면 그 대상은 건너뛴다 */
	TMap<uint16, int32> BotGoalFails;
	/** -VHBotPickTest: 이 소비 아이템을 지금 주우러 갈까 (단계별 연막탄 줍기 세 경우) */
	bool BotPickTestWants(const FEntity& E) const;
	FVector BotDir = FVector::ForwardVector;
};
