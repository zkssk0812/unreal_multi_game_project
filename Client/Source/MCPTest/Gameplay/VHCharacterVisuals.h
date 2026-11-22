#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Gameplay/VHWeaponCatalog.h"
#include "VHCharacterVisuals.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;
class UAnimInstance;
class UAnimSequence;
class UAnimMontage;
class UBlendSpace;
class UStaticMesh;
class UMaterialInterface;

/** 무기를 캐릭터의 어디에 붙이는가 */
UENUM(BlueprintType)
enum class EVHWeaponHoldMode : uint8
{
	/** 몸 앞의 정해진 자리에서 시선 방향으로 (들고 있는 애니메이션이 없을 때) */
	BodyFront	UMETA(DisplayName = "몸 앞 기본 위치 (시선 방향)"),
	/** 캐릭터 메시의 손 소켓에 (무기를 든 애니메이션이 있을 때) */
	HandSocket	UMETA(DisplayName = "손 소켓"),
};

/** 무기를 쥘 때 손가락 뼈 하나를 더 돌리는 값 (뼈 자기 축 기준 회전, 든 자세 섞임 정도만큼) */
USTRUCT(BlueprintType)
struct FVHFingerCurl
{
	GENERATED_BODY()

	FVHFingerCurl() {}
	FVHFingerCurl(FName InBone, const FRotator& InRot) : Bone(InBone), Rotation(InRot) {}

	/** 손가락 뼈 이름 (예: RightHandIndex1) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grip") FName Bone;
	/** 이 뼈를 자기 축 기준으로 더 돌리는 각도 (도). 손가락을 안쪽으로 마는 쪽이 주먹 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grip") FRotator Rotation = FRotator::ZeroRotator;
};

/** 무기를 든 자세 하나 (분류별·무기별) */
USTRUCT(BlueprintType)
struct FVHHoldPose
{
	GENERATED_BODY()

	FVHHoldPose() {}
	FVHHoldPose(const FRotator& Rot, const FVector& Loc, const FVector& Elbow = FVector(-0.2, 0.55, -0.8), bool bLeft = false, const FVector& LeftOnWeapon = FVector(26.0, 0.0, -2.0))
		: Weapon(Rot, Loc), ElbowDirection(Elbow), bLeftHandSupport(bLeft), LeftHandOnWeapon(LeftOnWeapon) {}

	/** 무기 손잡이가 놓일 자리와 무기 각도. 위치 = 발 기준 앞(X)·오른쪽(Y)·높이(Z) cm, 회전 0 = 무기 앞이 조준 방향 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hold")
	FTransform Weapon;

	/** 오른쪽 팔꿈치가 향하는 쪽 (앞·오른쪽·위). 팔이 굽는 방향을 정한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hold")
	FVector ElbowDirection = FVector(-0.2, 0.55, -0.8);

	/** 왼손으로 무기를 받친다 (긴 총·지뢰) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hold")
	bool bLeftHandSupport = false;

	/** 왼손이 잡는 곳: 무기 피벗(총·칼은 손잡이, 지뢰는 바닥 가운데)에서 무기 기준 앞(X)·오른쪽(Y)·위(Z) cm (화면에 보이는 크기 기준) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hold", meta = (EditCondition = "bLeftHandSupport"))
	FVector LeftHandOnWeapon = FVector(26.0, 0.0, -2.0);
};

/** 무기 동작의 한 순간 (든 자세에 더하는 값. 좌표는 발 기준·조준 방향 기준: 앞 X·오른쪽 Y·위 Z) */
USTRUCT(BlueprintType)
struct FVHMotionKey
{
	GENERATED_BODY()

	FVHMotionKey() {}
	FVHMotionKey(float InTime, const FVector& InHand, const FRotator& InWeapon = FRotator::ZeroRotator, float InSpineYaw = 0.f, float InSpinePitch = 0.f)
		: Time(InTime), HandOffset(InHand), WeaponRotation(InWeapon), SpineYaw(InSpineYaw), SpinePitch(InSpinePitch) {}

	/** 동작 시작부터 초 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion") float Time = 0.f;
	/** 손(무기 손잡이) 위치를 이만큼 옮긴다 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion") FVector HandOffset = FVector::ZeroVector;
	/** 무기 각도를 이만큼 돌린다 (조준 방향 기준. Yaw + = 오른쪽으로, Pitch + = 위로) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion") FRotator WeaponRotation = FRotator::ZeroRotator;
	/** 허리 비틀기 (도, + = 오른쪽) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion") float SpineYaw = 0.f;
	/** 허리 숙이기 (도, + = 앞으로 숙임, − = 뒤로 젖힘) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion") float SpinePitch = 0.f;
};

/**
 * 무기 동작 하나 (공격·꾹 누르기·레일건 버팀).
 * 새 애니메이션 파일 없이 키 프레임(시간 곡선)으로 팔·허리 뼈를 돌린다. 팀 캐릭터의 애니메이션이 생기면 Montage 칸에 넣으면 그것을 재생한다.
 */
USTRUCT(BlueprintType)
struct FVHWeaponMotion
{
	GENERATED_BODY()

	/** 키 프레임 (시간 순서). 첫 키와 마지막 키는 보통 0(든 자세 그대로) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion")
	TArray<FVHMotionKey> Keys;

	/** 연출 순간 (초): 총구 불꽃·휘두르기 궤적의 가장 빠른 때·수류탄이 손을 떠나는 때·지뢰가 바닥에 놓이는 때 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion")
	float EffectTime = 0.f;

	/** 연출이 이어지는 시간 (초, 동작 시간 기준). 근접 베기: 칼날이 판정 부채꼴 오른쪽 끝 → 왼쪽 끝을 지나는 시간 = 궤적이 지나가는 시간. 0 = 기본값 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion")
	float EffectDuration = 0.f;

	/** 꾹 누르는 동안 유지하는 자세에서 떨림 크기(cm)와 빠르기(Hz). 0 = 떨림 없음 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion")
	float ShakeCm = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion")
	float ShakeHz = 0.f;

	/** 팀 캐릭터 애니메이션(몽타주). 넣으면 키 프레임 대신 이것을 재생한다 (애니메이션 블루프린트에 DefaultSlot 이 있어야 한다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion")
	TSoftObjectPtr<UAnimMontage> Montage;

	float Duration() const { return Keys.Num() > 0 ? Keys.Last().Time : 0.f; }
	/** Time 초의 값 (키 사이는 천천히-빠르게-천천히로 잇는다) */
	FVHMotionKey Evaluate(float Time) const;
};

/** 구르기 동작의 한 순간. 시간은 구르기 전체를 1로 본 비율(0~1)이라 게임의 구르기 시간이 바뀌어도 그대로 맞는다 */
USTRUCT(BlueprintType)
struct FVHRollKey
{
	GENERATED_BODY()

	FVHRollKey() {}
	FVHRollKey(float InTime, float InTuck, float InFlip, float InDrop, float InFace)
		: Time(InTime), Tuck(InTuck), Flip(InFlip), Drop(InDrop), Face(InFace) {}

	/** 구르기 시작 0 → 끝 1 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll") float Time = 0.f;
	/** 웅크린 정도 (0 = 선 자세, 1 = 공처럼 웅크림. 각도는 FVHRollMotion 의 웅크림 칸) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll") float Tuck = 0.f;
	/** 앞구르기 회전 (도, 0 → 360 = 한 바퀴) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll") float Flip = 0.f;
	/** 몸을 낮추는 높이 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll") float Drop = 0.f;
	/** 몸을 구르는 방향으로 돌린 정도 (0 = 커서 방향 그대로, 1 = 구르는 방향) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll") float Face = 0.f;
};

/**
 * 구르기 동작 (Space). 새 애니메이션 파일 없이 키 프레임으로 몸을 웅크리고(허리·다리·팔 뼈) 몸 전체를 앞으로 한 바퀴 돌린다.
 * 길이는 게임의 구르기 시간(DA_VHGameplayConfig 의 Roll Seconds = 서버 무적 0.5초)에 자동으로 맞춘다.
 * 팀 캐릭터의 진짜 구르기 애니메이션이 오면 Montage 칸에 넣는다 → 키 프레임 대신 그것을 구르기 시간에 맞춰 재생한다.
 */
USTRUCT(BlueprintType)
struct FVHRollMotion
{
	GENERATED_BODY()

	FVHRollMotion();

	/** 키 프레임 (Time 순서, 0 ~ 1) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll")
	TArray<FVHRollKey> Keys;

	/** 한 바퀴 돌 때의 중심 높이 (몸을 낮춘 뒤 발바닥 기준 cm). 웅크린 몸의 가운데쯤 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll", meta = (Units = "cm"))
	float PivotHeight = 55.f;

	// ── 다 웅크렸을 때(Tuck 1)의 뼈 각도 (도). 앞으로 굽히는 쪽이 + ──
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll|Tuck") float SpineCurl = 80.f;		// 허리 전체 (허리 뼈들에 나눔)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll|Tuck") float HeadTuck = 35.f;		// 목·머리
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll|Tuck") float HipFlex = 105.f;		// 허벅지를 앞으로
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll|Tuck") float KneeBend = 125.f;		// 무릎 굽힘
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll|Tuck") float ArmForward = 105.f;	// 팔을 앞으로 (허리를 숙인 만큼 팔이 뒤로 돌아가므로 그보다 크게 → 무릎을 감싸게)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll|Tuck") float ElbowBend = 55.f;		// 팔꿈치 굽힘

	// ── 웅크릴 때 돌리는 뼈 (없는 이름은 건너뛴다). 팔 뼈는 Weapon|Arm Bones 칸을 같이 쓴다 ──
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll|Bones") TArray<FName> SpineBones;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll|Bones") TArray<FName> HeadBones;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll|Bones") FName LeftThighBone = TEXT("thigh_l");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll|Bones") FName RightThighBone = TEXT("thigh_r");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll|Bones") FName LeftCalfBone = TEXT("calf_l");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll|Bones") FName RightCalfBone = TEXT("calf_r");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll|Bones") FName LeftFootBone = TEXT("foot_l");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll|Bones") FName RightFootBone = TEXT("foot_r");

	/**
	 * 다리 모으기 (0 = 걷던 보폭 그대로, 1 = 웅크릴 때 두 다리를 나란히 모음).
	 * 걷기·달리기 애니메이션의 보폭(한 발 앞·한 발 뒤, 옆으로 벌림) 위에 웅크림을 더하면 거꾸로 도는 순간 발이 벌어져 보이므로,
	 * 웅크린 만큼 허벅지·종아리·발을 기본 자세(나란히 선 자세) 쪽으로 돌려놓은 뒤 굽힌다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll|Tuck", meta = (ClampMin = "0", ClampMax = "1")) float LegGather = 1.f;
	/** 무릎을 몸 가운데로 모으는 각도 (도, 웅크림 1 기준). 기본 자세의 다리 벌림을 더 좁힌다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll|Tuck") float KneesIn = 6.f;

	/** 무기 든 팔 자세를 푸는 때와 다시 쥐는 때 (비율). 그 사이에는 팔이 무릎을 감싼다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll") float ReleaseAt = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll") float RegripAt = 0.74f;

	/**
	 * 구르는 동안 손에 든 무기를 숨긴다 (구르기 시작 → 다시 쥐는 때 RegripAt 에 다시 보임). 다른 플레이어 화면도 같다.
	 * 팀 캐릭터 몽타주가 무기를 쥔 채 구르는 동작이면 끈다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll") bool bHideWeapon = true;

	/** 팀 캐릭터의 구르기 애니메이션(몽타주). 넣으면 키 프레임 대신 재생한다 (내 캐릭터: 애니메이션 블루프린트에 DefaultSlot 필요) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll")
	TSoftObjectPtr<UAnimMontage> Montage;

	/** 몽타주 시작 위치(초): 앞의 준비 동작을 건너뛴다. 나머지 길이가 구르기 시간(0.5초)에 맞춰 재생된다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll", meta = (ClampMin = 0, Units = "s"))
	float MontageStartTime = 0.f;

	/** U(0~1) 순간의 값. 웅크림·낮춤·방향은 천천히-빠르게-천천히, 회전은 일정한 빠르기로 잇는다 */
	FVHRollKey Evaluate(float U) const;
};

/**
 * 몸 동작 하나 (맞음·줍기·마시기·던지기·금고/상자 작업·경직). 몽타주를 넣으면 그 순간 재생한다(비우면 아무것도 안 함).
 * 내 캐릭터·다른 플레이어 둘 다 재생된다 (UVHThiefAnimInstance 가 그래프 없이 DefaultSlot 을 직접 섞는다).
 */
USTRUCT(BlueprintType)
struct FVHBodyMontage
{
	GENERATED_BODY()

	/** 재생할 몽타주 (슬롯 DefaultSlot). 루트 모션은 쓰지 않는다(이동은 게임 코드) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	TSoftObjectPtr<UAnimMontage> Montage;

	/** 켜면 허리(Upper Body Bone) 위만 이 동작, 다리는 걷기·달리기 그대로 (움직이면서 할 수 있는 동작) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	bool bUpperBodyOnly = false;

	/** 켜면 재생하는 동안 무기 든 팔 자세(팔 IK)를 푼다 → 팔도 이 동작대로 움직이고 무기는 손을 따라간다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	bool bReleaseArms = true;

	/** 재생 빠르기와 시작 위치(초, 앞부분 건너뛰기) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (ClampMin = 0.1))
	float PlayRate = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (ClampMin = 0, Units = "s"))
	float StartTime = 0.f;

	/** 재생할 길이(초, 애니메이션 시간 기준 = 시작 위치부터). 0 = 끝까지. 긴 Mixamo 동작에서 필요한 구간만 쓴다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (ClampMin = 0, Units = "s"))
	float Duration = 0.f;
};

/** 몸 동작 종류 (UVHWeaponVisualComponent::PlayBodyAction) */
enum class EVHBodyAction : uint8
{
	HitReact,		// 맞음 (SC_DAMAGE)
	PickUp,			// 줍기 (SC_PICKUP)
	Drink,			// 회복 키트 사용
	Throw,			// 섬광탄·끈끈이 던지기
};

/** 캐릭터 하나의 모습과 무기를 드는 방법. 캐릭터를 바꿀 때는 이 칸들만 바꾸면 된다 */
USTRUCT(BlueprintType)
struct FVHCharacterVisual
{
	GENERATED_BODY()

	FVHCharacterVisual();

	/** 캐릭터 메시. 비우면 블루프린트/코드에 들어 있는 메시 그대로 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	/** 내 캐릭터가 쓰는 애니메이션 블루프린트. 비우면 캐릭터 블루프린트에 들어 있는 것 그대로 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
	TSoftClassPtr<UAnimInstance> AnimClass;

	/** 다른 플레이어를 보여 줄 때 쓰는 애니메이션 (속도에 따라 서기·걷기·달리기 하나를 되풀이 재생) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Other Players")
	TSoftObjectPtr<UAnimSequence> IdleAnim;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Other Players")
	TSoftObjectPtr<UAnimSequence> WalkAnim;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Other Players")
	TSoftObjectPtr<UAnimSequence> RunAnim;

	/**
	 * 이동 블렌드스페이스 (가로 = 이동 방향 −180~180°, 몸이 보는 쪽 기준: 0 앞·90 오른쪽·−90 왼쪽·±180 뒤 / 세로 = 속도 cm/s).
	 * 넣으면 내 캐릭터·다른 플레이어 모두 이것으로 걷기·달리기 앞/뒤/좌/우를 섞는다(시선 기준 이동). 비우면 Idle/Walk/Run 과 애니메이션 블루프린트.
	 * 각 애니메이션의 재생 빠르기(Rate Scale)는 걸음이 게임 속도(걷기 300·달리기 600)에 맞게 미리 정해 둔다
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Locomotion")
	TSoftObjectPtr<UBlendSpace> LocomotionBlendSpace;

	/** 권총형(권총·테이저건)을 들었을 때 이동 (Pistol and Rifle Locomotion 1700, 2026-11-19). 비우면 위 기본. 손 위치는 그대로 코드 IK */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Locomotion")
	TSoftObjectPtr<UBlendSpace> PistolLocomotion;

	/** 라이플형(화염방사기·레일건)을 들었을 때 이동. 비우면 위 기본 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Locomotion")
	TSoftObjectPtr<UBlendSpace> RifleLocomotion;

	// ── 몸 동작 (몽타주). 비어 있는 칸은 재생하지 않는다 ──
	/** 허리 위만 재생하는 동작의 기준 뼈 (이 뼈와 그 아래 자식들 = 상체) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Motion")
	FName UpperBodyBone = TEXT("Spine");
	/** 맞았을 때 (SC_DAMAGE, 경직·쓰러짐이 아닐 때) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Motion")
	FVHBodyMontage HitReactMontage;
	/** 아이템·무기 줍기 (SC_PICKUP) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Motion")
	FVHBodyMontage PickUpMontage;
	/** 회복 키트 사용 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Motion")
	FVHBodyMontage DrinkMontage;
	/** 섬광탄·끈끈이(스킬) 던지기 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Motion")
	FVHBodyMontage ThrowMontage;
	/** 금고·상자 작업 시작 (ES_INTERACTING 이 켜질 때 한 번) → 끝나면 Interact Loop */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Motion")
	FVHBodyMontage InteractStartMontage;
	/** 금고·상자 작업 중 되풀이 (ES_INTERACTING 이 꺼지면 멈춤) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Motion")
	FVHBodyMontage InteractLoopMontage;
	/** 경직 중 되풀이 (ES_STUNNED: 테이저·감전 지뢰) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Motion")
	FVHBodyMontage StunnedMontage;

	// ── 무기 들기 (캐릭터 쪽 보정. 무기 쪽 보정은 DA_VHWeaponCatalog) ──

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	EVHWeaponHoldMode HoldMode = EVHWeaponHoldMode::HandSocket;

	/** 손 소켓 이름. 약속: 소켓의 +X = 무기 앞(총구), +Z = 무기 위. 메시에 이 소켓이 없으면 경고만 남기고 몸 앞 기본 위치에 붙인다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FName WeaponSocketName = TEXT("WeaponSocket_R");

	/** 손 소켓에 붙일 때 이 캐릭터만의 위치·회전 보정 (손 크기·모양 차이) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FTransform SocketOffset;

	// ── 무기를 든 자세: 분류별. 손 소켓 방식이면 오른팔이 이 자리로 손을 뻗고(팔 뼈를 코드가 돌린다), 몸 앞 방식이면 무기만 이 자리에 놓인다.
	//    위치 = 발 기준 앞(X)·오른쪽(Y)·높이(Z) cm, 회전 0 = 무기 앞(총구·날 끝)이 조준 방향. Roll 90 = 무기를 옆으로 눕혀 탑뷰에서 옆모습이 보이게.
	//    키가 다른 캐릭터는 Z 를 고친다

	/** 근접 무기 (삼단봉·도검·빔 소드): 오른손에 쥐고 날 끝이 조준 방향 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Hold Pose")
	FVHHoldPose MeleePose;

	/** 원거리 무기 (권총·화염방사기·레일건·테이저건): 오른팔을 앞으로 뻗어 총구가 조준 방향 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Hold Pose")
	FVHHoldPose RangedPose;

	/** 투척 무기 (수류탄): 오른손에 쥔 모습 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Hold Pose")
	FVHHoldPose ThrownPose;

	/** 설치 무기 (지뢰): 몸 앞에 평평하게 든 모습 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Hold Pose")
	FVHHoldPose PlacedPose;

	/** 무기별 자세 (칸이 있는 무기는 분류 자세 대신 이것을 쓴다. 예: 긴 총은 두 손으로 허리 높이) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Hold Pose")
	TMap<EVHWeaponId, FVHHoldPose> WeaponPoses;

	// ── 무기 사용 동작 (서버 이벤트에 맞춰 재생. 길이는 서버 공격 간격보다 길면 자동으로 빨리 재생한다) ──

	/** 공격 한 번 (SC_FIRE): 휘두르기·반동·던지기·설치. 무기별 칸 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Motion")
	TMap<EVHWeaponId, FVHWeaponMotion> AttackMotions;

	/** 꾹 누르는 동안 유지하는 자세 (공격 키를 누르는 중 = ES_ATTACKING). 화염방사기 겨누기, 레일건 충전 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Motion")
	TMap<EVHWeaponId, FVHWeaponMotion> HoldMotions;

	/** 레일건 발사 1초(ES_RAILGUN_LOCK) 동안 버티는 자세 (첫 키 하나만 쓴다 + 떨림) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Motion")
	FVHWeaponMotion RailgunLockMotion;

	/** 분류별 공격 몽타주 (무기 칸에 몽타주가 없을 때 쓴다. 비우면 키 프레임 동작) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Motion")
	TMap<EVHWeaponCategory, TSoftObjectPtr<UAnimMontage>> CategoryAttackMontages;

	// ── 팔 뼈 (손 소켓 방식에서 코드가 돌리는 뼈). 캐릭터의 뼈 이름이 다르면 여기만 고친다 ──

	/**
	 * 켜면 무기를 들었을 때 코드가 팔 뼈를 돌려 손을 자세 위치로 보낸다 (든 자세 애니메이션이 없는 캐릭터용).
	 * 캐릭터의 애니메이션이 무기 든 자세를 직접 갖고 있으면 끈다 → 무기는 손 소켓을 그대로 따라간다
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Arm Bones")
	bool bProceduralArms = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Arm Bones") FName RightUpperArmBone = TEXT("upperarm_r");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Arm Bones") FName RightLowerArmBone = TEXT("lowerarm_r");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Arm Bones") FName RightHandBone = TEXT("hand_r");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Arm Bones") FName LeftUpperArmBone = TEXT("upperarm_l");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Arm Bones") FName LeftLowerArmBone = TEXT("lowerarm_l");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Arm Bones") FName LeftHandBone = TEXT("hand_l");
	/** 휘두르기·설치 동작에서 숙이거나 비트는 허리 뼈 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Arm Bones") FName SpineBone = TEXT("spine_03");

	/** 왼손바닥이 손목 뼈에서 떨어진 거리 (왼손으로 받칠 때 손목을 이만큼 물린다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Arm Bones", meta = (Units = "cm"))
	float LeftPalmOffset = 8.f;

	/** 자세가 바뀔 때(무기 교체·맨손) 섞이는 빠르기 (클수록 빨리) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Arm Bones", meta = (ClampMin = 1))
	float PoseBlendSpeed = 14.f;

	/** 이 캐릭터가 든 무기의 크기 배율 (몸집 차이) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = 0.1))
	float WeaponScale = 1.f;

	/** 구르기 동작 (키 프레임 또는 몽타주) — 내 캐릭터·다른 플레이어 같은 값 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll")
	FVHRollMotion RollMotion;

	// ── 쥔 손 모양 (무기를 들었을 때 오른손 손가락을 말아 손잡이를 감싸 쥔다) ──
	/** 근접·투척·설치 무기를 쥘 때 손가락 (주먹). 비우면 손가락은 애니메이션 그대로 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Grip")
	TArray<FVHFingerCurl> GripFingers;
	/** 총(원거리)을 쥘 때 손가락 (집게손가락은 방아쇠 쪽으로 펴고 나머지는 손잡이를 감쌈). 비우면 GripFingers 를 쓴다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Grip")
	TArray<FVHFingerCurl> RangedGripFingers;
	/** 왼손으로 무기를 받칠 때 왼손 손가락 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Grip")
	TArray<FVHFingerCurl> LeftGripFingers;

	// ── 쓰러짐 ──
	/** 쓰러질 때 한 번 재생하는 애니메이션 (마지막 자세로 멈춤). 비우면 바로 숨긴다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Downed")
	TSoftObjectPtr<UAnimSequence> DownAnim;
	/** 쓰러지는 모습을 보여 준 뒤 숨기기까지 (초). 리스폰 대기 중에는 숨긴다(기존 규칙) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Downed", meta = (ClampMin = 0, Units = "s"))
	float DownShowSeconds = 1.4f;
	/** 쓰러짐 애니메이션 재생 빠르기 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Downed", meta = (ClampMin = 0.1))
	float DownPlayRate = 1.3f;
};

/** 경비원(로봇) 표시. Mesh 가 비면 예전 기본 도형으로 그린다 */
USTRUCT(BlueprintType)
struct FVHGuardVisual
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guard") TSoftObjectPtr<USkeletalMesh> Mesh;
	/** 서기·걷기(순찰)·달리기(추적). 속도로 섞는다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guard") TSoftObjectPtr<UAnimSequence> IdleAnim;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guard") TSoftObjectPtr<UAnimSequence> WalkAnim;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guard") TSoftObjectPtr<UAnimSequence> RunAnim;
	/** 사격 중(ES_ATTACKING) 서 있을 때 자세. 비우면 IdleAnim */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guard") TSoftObjectPtr<UAnimSequence> AttackAnim;
	/** 경직 중(ES_STUNNED: 테이저) 서 있을 때 되풀이. 비우면 몸을 기울이기만 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guard") TSoftObjectPtr<UAnimSequence> StunAnim;
	/** 손에 든 기관총 모델과 붙일 뼈·보정 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guard|Gun") TSoftObjectPtr<UStaticMesh> GunMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guard|Gun") FName GunBone = TEXT("RightHand");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guard|Gun") FTransform GunOffset;
	/** 걷기 → 달리기로 넘어가는 속도 (cm/s). 경비원 순찰·추적 속도에 맞춘다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guard") float RunSpeed = 450.f;
	/** 걷기 애니메이션이 제자리에서 이 속도(cm/s)로 걷는 것처럼 보인다 → 실제 속도에 맞춰 재생 빠르기를 바꾼다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guard") float WalkAnimSpeed = 180.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guard") float RunAnimSpeed = 400.f;
};

/**
 * 캐릭터의 모습과 무기 드는 방법을 모은 표 (DA_VHCharacterVisuals).
 * 내 캐릭터(무기 표시 컴포넌트)와 다른 플레이어(AVHNetProxy)가 같은 표를 본다 → 캐릭터를 바꿀 때 여기 한곳만 고친다.
 * 방에서 고른 캐릭터 번호(1~5)별 칸이 없으면 기본(Default) 칸을 쓴다.
 */
UCLASS(BlueprintType)
class MCPTEST_API UVHCharacterVisuals : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UVHCharacterVisuals();

	/** 캐릭터 번호별 칸이 없을 때 쓰는 기본 모습 = 도적1(캐릭터1) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Characters")
	FVHCharacterVisual Default;

	/** 캐릭터 번호(도적1~3) → 모습. 번호 칸이 없거나 그 칸의 Mesh 가 비어 있으면 Default(도적1)를 쓴다. 지금: 2 = 도적2, 3 = 비어 있음(리깅본 대기) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Characters")
	TMap<int32, FVHCharacterVisual> Characters;

	const FVHCharacterVisual& Find(int32 CharacterId) const;

	/** 경비원(로봇) 모습 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guard")
	FVHGuardVisual Guard;

	// ── 플레이어 구분 (의적 모델이 하나라 모두 같아 보이므로 발밑 색 링 + 이름표 색으로 구분) ──
	/** 발밑 링을 보인다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Players")
	bool bShowPlayerRing = true;
	/** 플레이어 색 (게임 시작 때 의적 엔티티 번호 순서로 1번째·2번째·3번째. 모든 화면에서 같은 사람이 같은 색) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Players")
	TArray<FLinearColor> PlayerColors;
	/** 링 메시(원점 = 가운데, 바닥에 눕힌 판)와 재질(벡터 파라미터 Color) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Players")
	TSoftObjectPtr<UStaticMesh> PlayerRingMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Players")
	TSoftObjectPtr<UMaterialInterface> PlayerRingMaterial;
	/** 링 지름 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Players", meta = (Units = "cm"))
	float PlayerRingDiameter = 130.f;
	/** 다른 플레이어 이름표 글자도 그 색으로 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Players")
	bool bTintPlayerLabel = true;

	FLinearColor PlayerColor(int32 Index) const;

	/** 이 무기를 든 자세: 무기별 칸이 있으면 그것, 없으면 분류(EVHWeaponCategory 값) 자세 */
	static const FVHHoldPose& HoldPose(const FVHCharacterVisual& Visual, uint8 WeaponId, uint8 Category);

	/** 메시(와 bUseAnimClass 면 애니메이션 블루프린트)를 스켈레탈 메시 컴포넌트에 입힌다. 비어 있는 칸은 건드리지 않는다 */
	static void ApplyMesh(USkeletalMeshComponent* Target, const FVHCharacterVisual& Visual, bool bUseAnimClass);
};
