#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VHWeaponVisualComponent.generated.h"

class UStaticMeshComponent;
class USkeletalMeshComponent;
class UMaterialInterface;
class UAnimSequence;
class UAnimMontage;
class UAnimInstance;
struct FVHBodyMontage;
enum class EVHBodyAction : uint8;
enum class EVHWeaponPoint : uint8;
struct FVHCharacterVisual;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVHOnWeaponChanged, uint8, WeaponId);
class UVHWeaponVisualComponent;
/** 무기 동작의 연출 순간 (총구 불꽃·궤적·수류탄 출발·지뢰 놓임). 어느 캐릭터든 한곳에서 받는다 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FVHOnWeaponActionEffect, UVHWeaponVisualComponent* /*Who*/, uint8 /*WeaponId*/);

/** 애니메이션(UVHThiefAnimInstance)이 팔 자세를 만들 때 읽는 값. 좌표는 모두 "발 기준·조준 방향 기준"(앞 X·오른쪽 Y·위 Z cm) */
struct FVHHoldInfo
{
	bool bProceduralArms = false;
	FName SocketName;
	/** 손 소켓이 가야 할 자리·방향 */
	FTransform SocketTarget = FTransform::Identity;
	FVector ElbowDirection = FVector(0.0, 0.0, -1.0);
	bool bLeftHand = false;
	FVector LeftHandTarget = FVector::ZeroVector;
	/**
	 * 반동·떨림 (2026-11-20): 섞기(BlendSpeed) 뒤에 더한다. 손 소켓 최종 = (섞인 SocketTarget) × RecoilDelta, 왼손 최종 = 섞인 LeftHandTarget + LeftHandRecoil.
	 * 섞기에 넣으면 0.05초짜리 튐이 부드럽게 지워져서 거의 보이지 않았다
	 */
	FTransform RecoilDelta = FTransform::Identity;
	FVector LeftHandRecoil = FVector::ZeroVector;
	FName RightUpper, RightLower, RightHand, LeftUpper, LeftLower, LeftHand;
	float LeftPalmOffset = 8.f;
	float BlendSpeed = 14.f;
	/** 무기 동작의 허리 회전 (조준 방향 기준. Pitch − = 앞으로 숙임) */
	FName SpineBone;
	FRotator SpineRotation = FRotator::ZeroRotator;
	/** 원거리 무기(총)인가 → 쥔 손 모양을 총 손잡이용으로 */
	bool bRanged = false;
};

/**
 * 캐릭터가 지금 든 무기를 보여 주는 컴포넌트 (표시만, 판정 없음).
 * 어떤 캐릭터든 이 컴포넌트만 붙이면 동작한다: 내 캐릭터 블루프린트에도, 다른 플레이어 표시 액터(AVHNetProxy)에도 같은 것을 쓴다.
 *
 *  - SetWeapon(무기 번호): DA_VHWeaponCatalog 에서 메시·재질을 찾아 붙인다. 0 이면 숨긴다.
 *  - SetCharacterId(캐릭터 번호): DA_VHCharacterVisuals 에서 드는 방법(손 소켓 / 몸 앞)·소켓 이름·보정 값을 읽는다.
 *  - 최종 위치 = 캐릭터 보정 × 무기 보정. 두 값은 서로 다른 데이터 에셋에 있어 따로 고칠 수 있다.
 *  - 캐릭터 메시에 소켓이 없으면 멈추지 않고 경고만 한 번 남긴 뒤 몸 앞 기본 위치에 붙인다.
 *
 * 손 소켓 방식이면 무기를 손 소켓(WeaponSocket_R)에 붙이고, 애니메이션 인스턴스(UVHThiefAnimInstance)가 GetHoldInfo 를 읽어
 * 오른팔(긴 총·지뢰는 왼팔도)을 DA_VHCharacterVisuals 의 분류별·무기별 자세로 뻗는다. → Docs/캐릭터_교체_가이드.md
 */
UCLASS(ClassGroup = (VaultHeist), meta = (BlueprintSpawnableComponent, DisplayName = "VH Weapon Visual"))
class MCPTEST_API UVHWeaponVisualComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVHWeaponVisualComponent();

	/** 든 무기를 바꾼다 (서버의 무기 번호 0~10, 0 = 맨손) */
	UFUNCTION(BlueprintCallable, Category = "VaultHeist|Weapon")
	void SetWeapon(uint8 WeaponId);

	/** 방에서 고른 캐릭터 번호 (1~5, 0 = 기본). 드는 방법과 보정 값이 이 번호의 칸에서 온다 */
	UFUNCTION(BlueprintCallable, Category = "VaultHeist|Weapon")
	void SetCharacterId(int32 InCharacterId);

	/** 설정값(데이터 에셋)을 고친 뒤 다시 붙인다 (개발용 명령·에디터에서 값 조정할 때) */
	UFUNCTION(BlueprintCallable, Category = "VaultHeist|Weapon")
	void Refresh();

	/** 무기 전체를 한 재질로 덮는다 (은신 중 반투명). nullptr = 원래 재질 */
	void SetOverrideMaterial(UMaterialInterface* Material);

	UFUNCTION(BlueprintPure, Category = "VaultHeist|Weapon")
	uint8 GetWeapon() const { return CurrentWeapon; }

	UStaticMeshComponent* GetWeaponMesh() const { return WeaponMesh; }

	/**
	 * 지금 무기를 손 소켓에 붙여 들고 있으면 true 와 팔 자세 값을 돌려준다 (애니메이션 인스턴스가 매 프레임 읽는다).
	 * 맨손·숨김·몸 앞 방식·소켓 없음이면 false → 팔은 애니메이션 그대로
	 */
	bool GetHoldInfo(FVHHoldInfo& Out) const;

	/** 무기가 지금 손 소켓에 붙어 있는가 (아니면 몸 앞 기본 위치) */
	bool IsOnHandSocket() const { return bOnHandSocket; }

	// ── 무기 사용 동작 (서버 이벤트로 재생. 판정은 서버가 이미 했다) ──

	/** 서버가 알려 준 공격 한 번(SC_FIRE). WeaponId = 그 공격의 무기 (든 무기가 이미 바뀌었어도 동작이 끝날 때까지 이 무기를 보여 준다) */
	void PlayAttack(uint8 WeaponId);
	/** 공격 키를 누르고 있는가 (ES_ATTACKING): 화염방사기 겨누기·레일건 충전 자세 */
	void SetAttackHeld(bool bHeld);
	/** 레일건 발사 잠금 1초 (ES_RAILGUN_LOCK): 버팀 자세 */
	void SetRailgunLock(bool bLock);
	/**
	 * 레일건 충전 중 (2026-11-20, 내 캐릭터만 — 프로토콜에 충전 상태가 없어 다른 플레이어 표시는 켜지 않는다).
	 * 충전 자세 + 총구에 모이는 빛·충전음(UVHWeaponFxSubsystem). Seconds = 충전 시간(진행도 계산용)
	 */
	void SetRailgunCharging(bool bCharging, float Seconds = 1.f);
	bool IsRailgunCharging() const { return RailChargeStart >= 0.0; }
	/** 충전 진행도 0~1 (충전 중이 아니면 0) */
	float GetRailgunChargeProgress() const;
	/** 반동이 아직 남아 있는가 */
	bool IsRecoilActive() const { return Kicks.Num() > 0; }
	/** 공격 동작 재생 중인가 */
	bool IsActionPlaying() const { return ActionStart >= 0.0; }
	/** 재생 중인 동작의 무기와 지난 시간(초, 재생 빠르기 반영) */
	uint8 GetActionWeapon() const { return ActionWeapon; }
	/** 재생 빠르기 (동작이 서버 공격 간격보다 길면 1보다 크다) */
	float GetActionRate() const { return ActionRate; }
	float GetActionTime() const;
	/** 꾹 누르기 자세가 적용된 정도 (0~1) */
	float GetHoldAlpha() const { return HoldAlpha; }
	/** 레일건 버팀 자세가 적용된 정도 (0~1) */
	float GetLockAlpha() const { return LockAlpha; }
	/** 무기가 지금 화면에 보이는가 (캐릭터가 숨으면 무기도 숨는다) */
	bool IsWeaponShown() const;
	/** 무기 메시(손에 든 무기)의 지금 월드 위치·회전 (연출의 총구·칼끝 자리를 구할 때) */
	FTransform GetWeaponWorldTransform() const;
	/** 총구(칼끝) 월드 위치: 근접은 BladeTip 소켓, 그 밖은 Muzzle 소켓 → 무기 표 대신 값 → 메시 앞(+X) 끝. 무기가 없으면 false */
	bool GetMuzzleLocation(FVector& Out) const;
	/** 무기 기준점(소켓 Muzzle·BladeTip·BladeBase·Grip·SupportHand)의 월드 위치. 소켓이 없으면 무기 표 대신 값·자동 계산. 정할 수 없으면 false */
	bool GetWeaponPoint(EVHWeaponPoint Point, FVector& Out) const;
	/** 같은 점의 무기 메시 좌표 (메시에 붙이는 이펙트용) */
	bool GetWeaponPointLocal(EVHWeaponPoint Point, FVector& Out) const;

	// ── 구르기 동작 (내 캐릭터: 구르기 시작 때 액션 컴포넌트가, 다른 플레이어: 스냅샷 ES_DODGING 이 켜질 때 표시 액터가 부른다) ──

	/** 구르기 시작. WorldDir = 실제로 굴러가는 방향(수평), Duration = 게임의 구르기 시간(초) */
	void PlayRoll(const FVector& WorldDir, float Duration);
	/** 구르기가 일찍 끝남 (경직·쓰러짐 등) → 바로 선 자세로 */
	void StopRoll();
	/** 구르는 방향을 고친다 (다른 플레이어: 화면의 실제 이동 방향을 처음 몇 프레임 동안 따라간다) */
	void SetRollDirection(const FVector& WorldDir);
	bool IsRolling() const { return RollStart >= 0.0; }
	/** 이 표시가 쓰는 캐릭터 번호 (DA_VHCharacterVisuals 칸) */
	int32 GetCharacterId() const { return CharacterId; }

	// ── 쓰러짐 동작 (DA_VHCharacterVisuals 의 Down Anim 을 한 번 재생, 마지막 자세로 멈춤. 무기는 숨긴다) ──
	/** 쓰러짐 시작. 반환 = 쓰러지는 모습을 보여 줄 시간(초). 0 = 애니메이션이 없어 바로 숨기면 된다 */
	float PlayDown();
	void StopDown();
	bool IsDownPlaying() const { return DownStart >= 0.0; }
	/** 쓰러짐 애니메이션과 지난 시간(재생 빠르기 반영) — 애니메이션 인스턴스가 읽는다 */
	bool GetDownState(UAnimSequence*& OutAnim, float& OutTime) const;

	// ── 몸 동작 (DA_VHCharacterVisuals 의 Body Motion 몽타주 칸. 내 캐릭터·다른 플레이어 같은 코드) ──
	/** 한 번 재생하는 몸 동작 (맞음·줍기·마시기·던지기). 쓰러짐·구르기·경직·작업 중에는 무시 */
	void PlayBodyAction(EVHBodyAction Action);
	/** 계속되는 몸 상태 (스냅샷 ES_INTERACTING·ES_STUNNED): 켜지면 작업(무릎 꿇기 → 살피기 반복)·경직(감전 반복), 꺼지면 멈춤 */
	void SetBodyStates(bool bInteracting, bool bStunned);
	/** 지금 몸 동작 몽타주가 재생(또는 섞여 사라지는) 중이면 상체만인지·팔 IK 를 풀지 (애니메이션 인스턴스가 매 프레임 읽는다) */
	void GetBodyMontageFlags(bool& bOutUpperBody, bool& bOutReleaseArms) const;
	/** 개발용 이름 (vh.Body) */
	FString GetBodyPhaseName() const;

	// ── 플레이어 구분 발밑 링 (DA_VHCharacterVisuals 의 Players 칸) ──
	/** 발밑 링 색을 정하고 보인다 (Index = 게임 시작 때 의적 순서). 음수 = 링 없앰 */
	void SetPlayerColorIndex(int32 Index);
	int32 GetPlayerColorIndex() const { return PlayerColorIndex; }
	/** 링만 숨기기·보이기 (표시 액터가 몸을 숨길 때) */
	void SetRingVisible(bool bVisible);
	/** 구르기 진행(0~1)과 방향. bMontage = 몽타주가 재생 중(키 프레임 자세는 쓰지 않고 팔 자세만 푼다) */
	bool GetRollState(float& OutU, FVector& OutDir, bool& bOutMontage) const;
	/** 개발용(vh.RollFreeze U): 내 캐릭터의 구르기 동작을 이 비율(0~1)에 멈춰 보여 준다. 음수 = 끔 */
	static float DebugRollFreeze;
	/** 개발용(구르기 촬영·봇): 8방향 중 앞뒤로 가장 길게 트인 방향을 고르고, 뒤쪽 벽 가까이로 물러나게 한다. 반환 = 방향, OutFree = 앞으로 트인 길이 */
	static FVector PrepareOpenRun(APawn* Pawn, float& OutFree);

	/** 모든 무기 표시 컴포넌트의 연출 순간을 받는 곳 (무기 이펙트가 여기에 붙는다) */
	static FVHOnWeaponActionEffect OnAnyActionEffect;

	/** 개발용(vh.WeaponAnim N 시간): 내 캐릭터의 동작을 이 시간(초)에 멈춰 보여 준다. 음수 = 끔 */
	static float DebugFreezeTime;
	/** 개발용(vh.ProxyTest): 멈춤 시간을 시험용 다른 플레이어 표시에도 적용한다 */
	static bool bDebugFreezeProxies;
	/** 개발용(vh.WeaponHold): 내 캐릭터의 꾹 누르기 표시를 강제로 0/1. -1 = 끔 */
	static int32 DebugHold;
	/** 개발용(vh.RailLock): 내 캐릭터의 레일건 잠금 표시를 강제로 0/1. -1 = 끔 */
	static int32 DebugRailLock;

	/** 무기가 바뀌었을 때 (애니메이션 블루프린트가 자세를 바꿀 자리) */
	UPROPERTY(BlueprintAssignable, Category = "VaultHeist|Weapon")
	FVHOnWeaponChanged OnWeaponChanged;

	/** 개발용(콘솔 vh.Weapon N): 내 캐릭터가 든 무기를 서버 값 대신 이 번호로 보여 준다. -1 = 끔 */
	static int32 DebugLocalWeapon;

	/**
	 * 켜면 DA_VHCharacterVisuals 의 메시·애니메이션 블루프린트를 이 캐릭터에 입힌다 (내 캐릭터).
	 * 다른 플레이어 표시 액터는 스스로 입히므로 끈다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VaultHeist|Weapon")
	bool bApplyCharacterMesh = true;

	/** 무기를 붙일 스켈레탈 메시 컴포넌트 이름. 비우면 캐릭터의 기본 메시(없으면 처음 찾은 스켈레탈 메시) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VaultHeist|Weapon")
	FName TargetMeshComponentName;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	USkeletalMeshComponent* FindTargetMesh() const;
	void ApplyWeapon();
	void ApplyAttachment();
	void ApplyMaterials();
	void SyncVisibility();
	void ApplyWeaponId(uint8 WeaponId);
	void EndAction();
	bool IsLocalPawn() const;
	void UpdateTickRate();

	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> WeaponMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> RingMesh;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> DownAnim;
	double DownStart = -1.0;
	float DownRate = 1.f;
	int32 PlayerColorIndex = -1;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> OverrideMaterial;

	uint8 CurrentWeapon = 0;
	int32 CharacterId = 0;
	bool bWarnedNoSocket = false;
	bool bOnHandSocket = false;
	/** 모델 좌표 → 무기 좌표 (모델 보정·표시 배율·Grip 소켓). ApplyAttachment 가 정한다 */
	FTransform MeshInner = FTransform::Identity;

	// 동작 상태
	uint8 ActionWeapon = 0;
	double ActionStart = -1.0;		// 월드 시간(초). 음수 = 재생 중 아님
	float ActionRate = 1.f;
	float ActionDuration = 0.f;
	float ActionEffectTime = 0.f;
	bool bEffectFired = false;
	bool bActionMontage = false;
	bool bActionRecoil = false;		// 이 동작은 키 프레임 대신 반동(Recoil 칸)으로 움직인다
	bool bHiddenByAction = false;	// 수류탄·지뢰가 손을 떠난 뒤
	bool bHiddenByRoll = false;		// 구르는 동안 (Roll Motion 의 Hide Weapon, 다시 쥐는 때까지)
	void UpdateRollHide();
	int32 PendingWeapon = -1;		// 동작이 끝나면 바꿀 무기
	bool bAttackHeld = false;
	bool bRailgunLock = false;
	float HoldAlpha = 0.f;
	float LockAlpha = 0.f;
	double RailChargeStart = -1.0;	// 월드 시간(초). 음수 = 충전 중 아님
	float RailChargeSeconds = 1.f;

	// 반동 (무기 표 DA_VHWeaponCatalog 의 Recoil 칸). 발사마다 하나씩 쌓이고 끝나면 지운다
	struct FRecoilKick
	{
		double Start = 0.0;
		uint8 Weapon = 0;
		float Side = 0.f;		// -1~1 (좌우 무작위)
		float CameraPhase = 0.f;
	};
	TArray<FRecoilKick> Kicks;
	void AddRecoil(uint8 WeaponId);
	/** 지금 반동 값을 더한다 (손 위치·무기 회전·허리) */
	void EvaluateRecoil(float Now, FVector& Hand, FRotator& Rot, float& SpineYaw, float& SpinePitch) const;
	/** 내 캐릭터 화면 흔들림 (카메라 위치를 아주 조금). 반동이 끝나면 원래 위치 */
	void UpdateCameraShake(float Now);
	TWeakObjectPtr<class UCameraComponent> ShakeCamera;
	FVector ShakeCameraBase = FVector::ZeroVector;
	bool bCameraShaken = false;

	// 구르기 상태
	double RollStart = -1.0;		// 월드 시간(초). 음수 = 구르는 중 아님
	float RollDuration = 0.5f;
	FVector RollDir = FVector::ForwardVector;
	bool bRollMontage = false;
	TWeakObjectPtr<class UAnimMontage> RollMontage;

	// 몸 동작 상태
	enum class EBodyPhase : uint8 { None, OneShot, InteractStart, InteractLoop, Stunned };
	EBodyPhase BodyPhase = EBodyPhase::None;
	TWeakObjectPtr<UAnimMontage> BodyMontage;
	bool bBodyUpper = false;
	bool bBodyRelease = false;
	float BodyEndPosition = -1.f;
	double NextDialSound = 0.0;	// 몽타주 위치(초)가 여기 오면 멈춘다. 음수 = 끝까지
	bool bBodyInteracting = false;
	bool bBodyStunned = false;
	UAnimInstance* GetBodyAnimInstance() const;
	bool PlayBodyMontage(const FVHBodyMontage& Entry, bool bLoop, EBodyPhase Phase);
	void StopBodyMontage(float BlendOut);
	void UpdateBodyMontage();
};
