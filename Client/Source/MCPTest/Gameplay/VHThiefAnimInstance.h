#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimationAsset.h"
#include "VHThiefAnimInstance.generated.h"

class UAnimSequence;
class UBlendSpace;
class UVHWeaponVisualComponent;

/** 한 프레임의 팔 자세 계산 값 (게임 스레드에서 만들고 애니메이션 스레드가 읽는다. 모두 캐릭터 메시 기준 좌표) */
struct FVHArmFrame
{
	/** 0 = 애니메이션 그대로, 1 = 무기 든 자세 */
	float Alpha = 0.f;
	/** 손 소켓이 가야 할 자리 */
	FTransform SocketTarget = FTransform::Identity;
	/** 손 뼈 기준 소켓 위치 (메시 에셋의 소켓 값) */
	FTransform SocketLocal = FTransform::Identity;
	/** 오른쪽 팔꿈치가 향하는 쪽 */
	FVector ElbowDir = FVector(0.0, 0.0, -1.0);

	float LeftAlpha = 0.f;
	FVector LeftHandTarget = FVector::ZeroVector;
	FVector LeftElbowDir = FVector(0.0, 0.0, -1.0);
	float LeftPalmOffset = 8.f;

	FName RightUpper, RightLower, RightHand, LeftUpper, LeftLower, LeftHand;

	/** 무기 동작의 허리 회전 (메시 기준, 이미 Alpha 를 곱했다) */
	FName Spine;
	FQuat SpineRotation = FQuat::Identity;

	/** 쥔 손 모양: 손가락 뼈 → 자기 축 기준 추가 회전 (오른손은 Alpha, 왼손은 LeftAlpha 만큼 섞는다) */
	TArray<TPair<FName, FQuat>, TInlineAllocator<16>> RightFingers;
	TArray<TPair<FName, FQuat>, TInlineAllocator<16>> LeftFingers;
};

/** 한 프레임의 구르기 자세 (모두 캐릭터 메시 기준 좌표, 각도는 웅크림 정도를 이미 곱했다) */
struct FVHRollFrame
{
	bool bActive = false;
	/** 몸의 오른쪽 축 (웅크리는 뼈를 이 축으로 돌린다. 몸 방향을 돌리기 전 = 액터 기준) */
	FVector BodyRight = FVector(1.0, 0.0, 0.0);
	/** 몸 전체 회전 (구르는 방향으로 돌리기 → 앞구르기) 과 그 중심, 낮추기 */
	FQuat RootRotation = FQuat::Identity;
	FVector Pivot = FVector::ZeroVector;
	FVector Drop = FVector::ZeroVector;
	/** 몸의 앞 축 (무릎 모으기 회전축) */
	FVector BodyForward = FVector(0.0, 1.0, 0.0);
	float SpinePerBone = 0.f, HeadPerBone = 0.f, HipFlex = 0.f, KneeBend = 0.f, ArmForward = 0.f, ElbowBend = 0.f;
	/** 다리를 기본 자세 쪽으로 모으는 정도(0~1)와 무릎 모으기 각도 */
	float LegGather = 0.f, KneesIn = 0.f;
	TArray<FName, TInlineAllocator<6>> Spine, Head;
	FName ThighL, ThighR, CalfL, CalfR, FootL, FootR, UpperL, UpperR, LowerL, LowerR;
};

/** 그래프(애니메이션 블루프린트)가 없을 때 쓰는 이동 애니메이션 값 (다른 플레이어 표시) */
struct FVHLocoFrame
{
	const UAnimSequence* Idle = nullptr;
	const UAnimSequence* Walk = nullptr;
	const UAnimSequence* Run = nullptr;
	double IdleTime = 0.0;
	double WalkTime = 0.0;
	double RunTime = 0.0;
	float MoveWeight = 0.f;		// 0 = 서기, 1 = 이동
	float RunWeight = 0.f;		// 이동 중 0 = 걷기, 1 = 달리기
	/** 한 번 재생 (쓰러짐): 있으면 이동·그래프 대신 이 애니메이션의 OneShotTime 자세 */
	const UAnimSequence* OneShot = nullptr;
	double OneShotTime = 0.0;
	/** 이동 블렌드스페이스 (있으면 Idle/Walk/Run·그래프 대신): 샘플 무게·시간은 게임 스레드가 계산 */
	const UBlendSpace* BlendSpace = nullptr;
	TArray<FBlendSampleData> BlendSamples;
	/** 그래프 없이 몽타주(DefaultSlot)를 직접 섞는다. bUpperBodyOnly 면 UpperBodyBone 과 그 자식만 */
	bool bEvaluateSlot = false;
	bool bUpperBodyOnly = false;
	FName UpperBodyBone;
};

/**
 * 애니메이션 스레드 쪽 계산: (1) 기본 자세 = 애니메이션 블루프린트 그래프, 그래프가 없으면 서기·걷기·달리기를 직접 섞는다
 * (2) 그 위에 무기 든 팔 자세를 덮는다 — 어깨·팔꿈치 뼈를 돌려 손 소켓을 자세 위치로 보낸다(두 뼈 IK).
 */
USTRUCT()
struct FVHThiefAnimProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FVHThiefAnimProxy() {}
	explicit FVHThiefAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void Update(float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	void EvaluateLocomotion(FPoseContext& Output) const;
	void EvaluateBlendSpace(FPoseContext& Output) const;
	void EvaluateSlot(FPoseContext& Output);
	void ApplyRoll(FPoseContext& Output) const;
	void ApplyArms(FPoseContext& Output) const;
	void ApplyFingers(FPoseContext& Output) const;

	FVHArmFrame Arm;
	FVHLocoFrame Loco;
	FVHRollFrame Roll;
	// 몽타주 슬롯 무게 (Update 에서 계산해 Evaluate 가 쓴다)
	float SlotNodeWeight = 0.f, SlotSourceWeight = 1.f, SlotTotalWeight = 0.f;
	// 상체 뼈 표시 (뼈 구성이 바뀌면 다시 만든다)
	TArray<bool> UpperMask;
	FName UpperMaskBone;
	int32 UpperMaskBoneCount = -1;
};

/**
 * 의적 캐릭터의 애니메이션 인스턴스 (내 캐릭터의 애니메이션 블루프린트 부모 클래스이자, 다른 플레이어 표시가 직접 쓰는 클래스).
 *
 *  - 무기 든 자세: 무기 표시 컴포넌트(UVHWeaponVisualComponent)가 손 소켓 방식이면, 팔 뼈를 돌려 손을 DA_VHCharacterVisuals 의
 *    분류별·무기별 자세 위치로 보낸다. 무기는 손 소켓에 붙어 있으므로 걷기·달리기 중에도 손에서 떨어지지 않는다.
 *  - 내 캐릭터: 이 클래스를 부모로 하는 애니메이션 블루프린트(ABP_VHThief)의 그래프(이동·점프)가 기본 자세를 만든다.
 *  - 다른 플레이어(AVHNetProxy): 그래프 없이 이 클래스를 그대로 쓴다. SetLocomotionAnims 로 받은 서기·걷기·달리기를 속도에 따라 섞는다.
 *
 * 팀 캐릭터로 바꿀 때: 그 캐릭터의 애니메이션 블루프린트 부모 클래스를 이 클래스로 바꾸면(클래스 세팅 → 부모 클래스) 팔 자세가 그대로 입혀진다.
 * 캐릭터 애니메이션이 무기 든 자세를 직접 갖고 있으면 DA_VHCharacterVisuals 의 bProceduralArms 를 끈다. → Docs/캐릭터_교체_가이드.md
 */
UCLASS()
class MCPTEST_API UVHThiefAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** 그래프가 없을 때 쓰는 이동 애니메이션 (다른 플레이어 표시) */
	void SetLocomotionAnims(UAnimSequence* Idle, UAnimSequence* Walk, UAnimSequence* Run);

	/** 이동 속도를 밖에서 넣는다 (캐릭터 무브먼트가 없는 표시용 액터). 음수 = 소유 액터의 속도를 쓴다 */
	void SetExternalSpeed(float Speed) { ExternalSpeed = Speed; }
	/** 이동 속도·방향(수평 cm/s)을 밖에서 넣는다 (블렌드스페이스 방향). 한 번 넣으면 그다음부터 이것을 쓴다 */
	void SetExternalVelocity(const FVector& Velocity) { ExternalVelocity = Velocity; bHasExternalVelocity = true; ExternalSpeed = static_cast<float>(Velocity.Size2D()); }
	/** 그래프가 없을 때 쓰는 이동 블렌드스페이스 (nullptr = DA_VHCharacterVisuals 의 Locomotion Blend Space 또는 Idle/Walk/Run) */
	void SetBlendSpaceOverride(UBlendSpace* InBlendSpace, bool bDisable = false) { BlendSpaceOverride = InBlendSpace; bNoBlendSpace = bDisable; }

	/** 걷기→달리기 속도와 애니메이션이 보여 주는 속도 (경비원처럼 의적과 속도가 다른 표시용). 0 = 의적 기본값 */
	void SetLocomotionSpeeds(float InRunSpeed, float InWalkAnimSpeed, float InRunAnimSpeed) { RunSpeedOverride = InRunSpeed; WalkAnimSpeed = InWalkAnimSpeed; RunAnimSpeed = InRunAnimSpeed; }

	/** 그래프가 없을 때 서기 자리에 쓸 애니메이션 (경비원 사격 자세). nullptr = 원래 서기 */
	void SetIdleOverride(UAnimSequence* Anim) { IdleOverride = Anim; }

	/** 바닥 이동 속도 (cm/s). 애니메이션 블루프린트에서 읽을 수 있다 */
	UPROPERTY(BlueprintReadOnly, Category = "VaultHeist")
	float VHGroundSpeed = 0.f;

	/** 지금 든 무기 번호 (0 = 맨손) */
	UPROPERTY(BlueprintReadOnly, Category = "VaultHeist")
	uint8 VHWeaponId = 0;

	/** 무기 든 팔 자세가 적용된 정도 (0~1) */
	UPROPERTY(BlueprintReadOnly, Category = "VaultHeist")
	float VHArmAlpha = 0.f;

	/** 구르는 중인가와 진행(0~1). 애니메이션 블루프린트에서 읽을 수 있다 */
	UPROPERTY(BlueprintReadOnly, Category = "VaultHeist")
	bool VHRolling = false;
	UPROPERTY(BlueprintReadOnly, Category = "VaultHeist")
	float VHRollProgress = 0.f;

	/** 이동 방향 (몸이 보는 쪽 기준 도, 0 앞·90 오른쪽) — 블렌드스페이스 가로축 */
	UPROPERTY(BlueprintReadOnly, Category = "VaultHeist")
	float VHMoveDirection = 0.f;

	/** 개발용(vh.ArmDebug 1): 손 목표 위치를 화면에 그린다 */
	static bool bDebugDraw;

protected:
	virtual void NativeInitializeAnimation() override;
	virtual void PreUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

	void UpdateLocomotion(float DeltaSeconds);
	void UpdateRoll();
	void UpdateArms(float DeltaSeconds);

	friend struct FVHThiefAnimProxy;

	UPROPERTY(Transient) TObjectPtr<UAnimSequence> IdleAnim;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> WalkAnim;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> RunAnim;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> IdleOverride;
	UPROPERTY(Transient) TObjectPtr<UBlendSpace> BlendSpaceOverride;
	UPROPERTY(Transient) TObjectPtr<UBlendSpace> CurrentBlendSpace;
	bool bNoBlendSpace = false;
	TArray<FBlendSampleData> BlendCache;
	int32 BlendTriangle = -1;
	double BlendPhase = 0.0;
	float SmoothDir = 0.f;
	float SmoothSpeed = 0.f;
	FVector ExternalVelocity = FVector::ZeroVector;
	bool bHasExternalVelocity = false;
	void UpdateBlendSpace(UBlendSpace* BS, float DeltaSeconds);
	/** 몸 동작 몽타주가 팔 IK 를 풀어 달라고 하는가 (UpdateLocomotion 이 정하고 UpdateArms 가 읽는다) */
	bool bBodyReleaseArms = false;
	TWeakObjectPtr<UVHWeaponVisualComponent> WeaponVisual;
	float RunSpeedOverride = 0.f;
	float WalkAnimSpeed = 0.f;
	float RunAnimSpeed = 0.f;

	float ExternalSpeed = -1.f;
	FVHLocoFrame Loco;
	FVHArmFrame Arm;
	FVHRollFrame Roll;
	/** 구르는 동안 무기 든 팔 자세를 풀어 둘 것인가 (UpdateRoll 이 정하고 UpdateArms 가 읽는다) */
	bool bRollReleaseArms = false;
	float RollTuck = 0.f;

	// 자세가 바뀔 때 부드럽게 섞기 위한 지금 값 (발 기준·조준 방향 기준)
	bool bHavePose = false;
	FTransform CurSocketTarget = FTransform::Identity;
	FVector CurElbow = FVector(0.0, 0.0, -1.0);
	FVector CurLeftTarget = FVector::ZeroVector;
	float LeftAlpha = 0.f;
};
