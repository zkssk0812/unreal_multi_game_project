#include "Gameplay/VHThiefAnimInstance.h"

#include "Gameplay/VHCharacterVisuals.h"
#include "Gameplay/VHWeaponVisualComponent.h"
#include "Network/VHSettings.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "Gameplay/VHSoundConfig.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "TwoBoneIK.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogVHThiefAnim, Log, All);

bool UVHThiefAnimInstance::bDebugDraw = false;

// ───────────────── 게임 스레드 ─────────────────

void UVHThiefAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	WeaponVisual.Reset();
	bHavePose = false;
}

FAnimInstanceProxy* UVHThiefAnimInstance::CreateAnimInstanceProxy()
{
	return new FVHThiefAnimProxy(this);
}

void UVHThiefAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FVHThiefAnimProxy*>(InProxy);
}

void UVHThiefAnimInstance::SetLocomotionAnims(UAnimSequence* Idle, UAnimSequence* Walk, UAnimSequence* Run)
{
	IdleAnim = Idle;
	WalkAnim = Walk;
	RunAnim = Run;
}

void UVHThiefAnimInstance::PreUpdateAnimation(float DeltaSeconds)
{
	// 프록시가 값을 복사해 가기(Super) 전에 이번 프레임 값을 만든다
	const AActor* Owner = GetOwningActor();
	if (Owner && GetSkelMeshComponent())
	{
		VHGroundSpeed = ExternalSpeed >= 0.f ? ExternalSpeed : static_cast<float>(Owner->GetVelocity().Size2D());
		UpdateLocomotion(DeltaSeconds);
		UpdateRoll();
		UpdateArms(DeltaSeconds);
	}
	Super::PreUpdateAnimation(DeltaSeconds);
}

void UVHThiefAnimInstance::UpdateLocomotion(float DeltaSeconds)
{
	// 걷기 300 · 달리기 600 cm/s (서버 GameData 의 플레이어 속도). 그 사이는 섞는다. 경비원 등은 SetLocomotionSpeeds 값
	const float RunAt = RunSpeedOverride > 0.f ? RunSpeedOverride : 360.f;
	const float MoveTarget = FMath::Clamp((VHGroundSpeed - 15.f) / 120.f, 0.f, 1.f);
	const float RunTarget = FMath::Clamp((VHGroundSpeed - RunAt) / 160.f, 0.f, 1.f);
	Loco.MoveWeight = FMath::FInterpTo(Loco.MoveWeight, MoveTarget, DeltaSeconds, 10.f);
	Loco.RunWeight = FMath::FInterpTo(Loco.RunWeight, RunTarget, DeltaSeconds, 8.f);
	UAnimSequence* Idle = IdleOverride ? IdleOverride.Get() : IdleAnim.Get();
	Loco.Idle = Idle;
	Loco.Walk = WalkAnim;
	Loco.Run = RunAnim;
	// 쓰러짐 한 번 재생 (마지막 자세로 멈춤)
	Loco.OneShot = nullptr;
	if (!WeaponVisual.IsValid() && GetOwningActor())
	{
		WeaponVisual = GetOwningActor()->FindComponentByClass<UVHWeaponVisualComponent>();
	}
	// 이동 블렌드스페이스 (시선 기준 이동: 걷기·달리기 앞/뒤/좌/우). 경비원 등은 bNoBlendSpace
	UBlendSpace* BS = nullptr;
	const FVHCharacterVisual* Rig = WeaponVisual.IsValid() ? &UVHSettings::Get().GetCharacterVisuals().Find(WeaponVisual->GetCharacterId()) : nullptr;
	if (!bNoBlendSpace)
	{
		BS = BlendSpaceOverride ? BlendSpaceOverride.Get() : (Rig ? Rig->LocomotionBlendSpace.LoadSynchronous() : nullptr);
		// 든 무기에 맞는 이동 (권총형 4·7, 라이플형 5·6) — 구르기·쓰러짐 중에는 기본
		const uint8 W = WeaponVisual.IsValid() ? WeaponVisual->GetWeapon() : 0;
		if (Rig && !BlendSpaceOverride && WeaponVisual.IsValid() && WeaponVisual->IsWeaponShown())
		{
			UBlendSpace* ByWeapon = (W == 4 || W == 7) ? Rig->PistolLocomotion.LoadSynchronous() : ((W == 5 || W == 6) ? Rig->RifleLocomotion.LoadSynchronous() : nullptr);
			BS = ByWeapon ? ByWeapon : BS;
		}
	}
	Loco.BlendSpace = BS;
	if (BS)
	{
		UpdateBlendSpace(BS, DeltaSeconds);
		Loco.BlendSamples = BlendCache;
	}
	else
	{
		Loco.BlendSamples.Reset();
	}
	// 몸 동작 몽타주: 상체만인가, 팔 IK 를 풀어야 하는가
	bool bUpper = false;
	bBodyReleaseArms = false;
	if (WeaponVisual.IsValid())
	{
		WeaponVisual->GetBodyMontageFlags(bUpper, bBodyReleaseArms);
	}
	Loco.bUpperBodyOnly = bUpper;
	Loco.UpperBodyBone = Rig ? Rig->UpperBodyBone : FName(TEXT("Spine"));
	UAnimSequence* Down = nullptr;
	float DownTime = 0.f;
	if (WeaponVisual.IsValid() && WeaponVisual->GetDownState(Down, DownTime) && Down)
	{
		Loco.OneShot = Down;
		Loco.OneShotTime = FMath::Min<double>(DownTime, FMath::Max<double>(Down->GetPlayLength() - 0.02, 0.0));
	}
	auto Advance = [DeltaSeconds](const UAnimSequence* Seq, double& Time, float Rate)
	{
		const double Length = Seq ? Seq->GetPlayLength() : 0.0;
		Time = Length > 0.0 ? FMath::Fmod(Time + DeltaSeconds * Rate, Length) : 0.0;
	};
	Advance(Idle, Loco.IdleTime, 1.f);
	// 표시용 액터(경비원): 실제 속도에 맞춰 걷기·달리기 재생 빠르기를 바꿔 발이 덜 미끄러지게
	float Rate = 1.f;
	if (WalkAnimSpeed > 0.f && RunAnimSpeed > 0.f && VHGroundSpeed > 20.f)
	{
		Rate = FMath::Clamp(VHGroundSpeed / FMath::Lerp(WalkAnimSpeed, RunAnimSpeed, Loco.RunWeight), 0.5f, 2.f);
	}
	// 걷기와 달리기는 같은 박자(0~1)로 돌려 섞을 때 발이 엇갈리지 않게 한다
	const double WalkLength = WalkAnim ? WalkAnim->GetPlayLength() : 0.0;
	const double RunLength = RunAnim ? RunAnim->GetPlayLength() : 0.0;
	if (WalkLength > 0.0 && RunLength > 0.0)
	{
		const double CycleLength = FMath::Lerp(WalkLength, RunLength, static_cast<double>(Loco.RunWeight));
		const double Phase = FMath::Frac(Loco.WalkTime / WalkLength + DeltaSeconds * Rate / CycleLength);
		Loco.WalkTime = Phase * WalkLength;
		Loco.RunTime = Phase * RunLength;
	}
	else
	{
		Advance(WalkAnim, Loco.WalkTime, Rate);
		Advance(RunAnim, Loco.RunTime, Rate);
	}
}

void UVHThiefAnimInstance::UpdateBlendSpace(UBlendSpace* BS, float DeltaSeconds)
{
	if (CurrentBlendSpace != BS)
	{
		CurrentBlendSpace = BS;
		BlendCache.Reset();
		BlendTriangle = -1;
		BlendPhase = 0.0;
	}
	const AActor* Owner = GetOwningActor();
	FVector Vel = bHasExternalVelocity ? ExternalVelocity : Owner->GetVelocity();
	Vel.Z = 0.0;
	const float Speed = static_cast<float>(Vel.Size());
	// 방향 = 이동 방향 − 몸이 보는 쪽(커서). 멈추면 마지막 방향 유지
	if (Speed > 20.f)
	{
		const float Dir = FMath::FindDeltaAngleDegrees(static_cast<float>(Owner->GetActorRotation().Yaw), static_cast<float>(Vel.Rotation().Yaw));
		SmoothDir = FRotator::NormalizeAxis(SmoothDir + FMath::FindDeltaAngleDegrees(SmoothDir, Dir) * FMath::Clamp(DeltaSeconds * 12.f, 0.f, 1.f));
	}
	SmoothSpeed = FMath::FInterpTo(SmoothSpeed, Speed, DeltaSeconds, 12.f);
	VHMoveDirection = SmoothDir;
	BS->UpdateBlendSamples(FVector(SmoothDir, SmoothSpeed, 0.0), DeltaSeconds, BlendCache, BlendTriangle);
	// 모든 샘플을 같은 박자(0~1)로 돌린다: 한 바퀴 시간 = 무게로 섞은 (길이 ÷ 재생 빠르기)
	double Cycle = 0.0, WeightSum = 0.0;
	for (const FBlendSampleData& S : BlendCache)
	{
		if (S.Animation && S.TotalWeight > ZERO_ANIMWEIGHT_THRESH)
		{
			const float Rate = FMath::Max(BS->GetBlendSample(S.SampleDataIndex).RateScale * S.Animation->RateScale, 0.01f);
			Cycle += S.TotalWeight * S.Animation->GetPlayLength() / Rate;
			WeightSum += S.TotalWeight;
		}
	}
	if (WeightSum > 0.0 && Cycle > 0.0)
	{
		const double Before = BlendPhase;
		BlendPhase = FMath::Frac(BlendPhase + DeltaSeconds / (Cycle / WeightSum));
		// 발소리: 걷기·달리기 한 바퀴에 두 걸음 (박자가 0 과 0.5 를 지날 때)
		const bool bStep = (BlendPhase < Before) || (Before < 0.5 && BlendPhase >= 0.5);
		if (bStep && SmoothSpeed > 80.f && !VHRolling)
		{
			UVHSoundConfig::Play(Owner, EVHSound::Footstep, Owner->GetActorLocation());
		}
	}
	for (FBlendSampleData& S : BlendCache)
	{
		const float Length = S.Animation ? S.Animation->GetPlayLength() : 0.f;
		S.PreviousTime = S.Time;
		S.Time = static_cast<float>(BlendPhase) * Length;
	}
}

void UVHThiefAnimInstance::UpdateRoll()
{
	const AActor* Owner = GetOwningActor();
	const USkeletalMeshComponent* Skel = GetSkelMeshComponent();
	if (!WeaponVisual.IsValid())
	{
		WeaponVisual = Owner->FindComponentByClass<UVHWeaponVisualComponent>();
	}
	Roll = FVHRollFrame();
	bRollReleaseArms = false;
	RollTuck = 0.f;
	float U = 0.f;
	FVector Dir = FVector::ForwardVector;
	bool bMontage = false;
	VHRolling = WeaponVisual.IsValid() && WeaponVisual->GetRollState(U, Dir, bMontage);
	VHRollProgress = VHRolling ? U : 0.f;
	if (!VHRolling)
	{
		return;
	}
	const FVHCharacterVisual& Rig = UVHSettings::Get().GetCharacterVisuals().Find(WeaponVisual->GetCharacterId());
	const FVHRollMotion& Motion = Rig.RollMotion;
	// 무기 든 팔: 구르는 동안 풀었다가(팔이 무릎을 감쌈) 일어서기 직전에 다시 쥔다
	bRollReleaseArms = U >= Motion.ReleaseAt && U < Motion.RegripAt;
	const FVHRollKey K = Motion.Evaluate(U);
	RollTuck = bMontage ? 0.f : K.Tuck;

	// 액터 기준 → 메시 기준. 몸은 커서(액터 방향)를 보고 있으므로 Face 만큼 구르는 방향으로 돌린 뒤, 구르는 방향의 오른쪽 축으로 한 바퀴
	const FTransform MeshWorld = Skel->GetComponentTransform();
	const FVector Up = MeshWorld.InverseTransformVectorNoScale(FVector::UpVector);
	const float ActorYaw = Owner->GetActorRotation().Yaw;
	const FVector RollDir = Dir.GetSafeNormal2D(UE_SMALL_NUMBER, Owner->GetActorForwardVector());
	const float FaceDelta = FMath::FindDeltaAngleDegrees(ActorYaw, RollDir.Rotation().Yaw) * K.Face;
	const FQuat Yaw(Up, FMath::DegreesToRadians(FaceDelta));
	const FVector RollRight = MeshWorld.InverseTransformVectorNoScale(FVector::CrossProduct(FVector::UpVector, RollDir)).GetSafeNormal();
	// 몽타주: 구르는 동작은 몽타주가 하고, 코드는 몸을 구르는 방향으로 돌리기만 한다 (몸은 평소 커서를 본다)
	const FQuat Flip(RollRight, bMontage ? 0.f : FMath::DegreesToRadians(K.Flip));	// + = 위가 앞으로 (앞구르기)

	Roll.bActive = true;
	Roll.BodyRight = MeshWorld.InverseTransformVectorNoScale(FRotator(0.f, ActorYaw, 0.f).RotateVector(FVector::RightVector)).GetSafeNormal();
	Roll.BodyForward = FVector::CrossProduct(Roll.BodyRight, Up).GetSafeNormal();
	Roll.LegGather = FMath::Clamp(Motion.LegGather, 0.f, 1.f) * K.Tuck;
	Roll.KneesIn = Motion.KneesIn * K.Tuck;
	Roll.FootL = Motion.LeftFootBone;
	Roll.FootR = Motion.RightFootBone;
	Roll.RootRotation = (Flip * Yaw).GetNormalized();
	Roll.Pivot = Up * Motion.PivotHeight;
	Roll.Drop = bMontage ? FVector::ZeroVector : -Up * K.Drop;
	if (bMontage)
	{
		return;	// 웅크리기(뼈 굽힘)는 하지 않는다
	}
	Roll.Spine.Append(Motion.SpineBones);
	Roll.Head.Append(Motion.HeadBones);
	Roll.SpinePerBone = Motion.SpineBones.Num() > 0 ? Motion.SpineCurl * K.Tuck / Motion.SpineBones.Num() : 0.f;
	Roll.HeadPerBone = Motion.HeadBones.Num() > 0 ? Motion.HeadTuck * K.Tuck / Motion.HeadBones.Num() : 0.f;
	Roll.HipFlex = Motion.HipFlex * K.Tuck;
	Roll.KneeBend = Motion.KneeBend * K.Tuck;
	Roll.ArmForward = Motion.ArmForward * K.Tuck;
	Roll.ElbowBend = Motion.ElbowBend * K.Tuck;
	Roll.ThighL = Motion.LeftThighBone;
	Roll.ThighR = Motion.RightThighBone;
	Roll.CalfL = Motion.LeftCalfBone;
	Roll.CalfR = Motion.RightCalfBone;
	Roll.UpperL = Rig.LeftUpperArmBone;
	Roll.UpperR = Rig.RightUpperArmBone;
	Roll.LowerL = Rig.LeftLowerArmBone;
	Roll.LowerR = Rig.RightLowerArmBone;
}

void UVHThiefAnimInstance::UpdateArms(float DeltaSeconds)
{
	const AActor* Owner = GetOwningActor();
	const USkeletalMeshComponent* Skel = GetSkelMeshComponent();
	if (!WeaponVisual.IsValid())
	{
		WeaponVisual = Owner->FindComponentByClass<UVHWeaponVisualComponent>();
	}
	FVHHoldInfo Info;
	const bool bHold = WeaponVisual.IsValid() && WeaponVisual->GetHoldInfo(Info) && Info.bProceduralArms && !bRollReleaseArms && !bBodyReleaseArms;
	VHWeaponId = WeaponVisual.IsValid() ? WeaponVisual->GetWeapon() : 0;

	// 구르기 0.5초 안에 풀고 다시 쥐어야 하므로 그동안은 더 빨리 섞는다
	const float BlendSpeed = VHRolling ? 24.f : (bHold ? Info.BlendSpeed : 14.f);
	VHArmAlpha = FMath::FInterpTo(VHArmAlpha, bHold ? 1.f : 0.f, DeltaSeconds, BlendSpeed);
	if (bHold)
	{
		const USkeletalMeshSocket* Socket = Skel->GetSocketByName(Info.SocketName);
		Arm.SocketLocal = Socket ? Socket->GetSocketLocalTransform() : FTransform::Identity;
		Arm.RightUpper = Info.RightUpper;
		Arm.RightLower = Info.RightLower;
		Arm.RightHand = Socket ? Socket->BoneName : Info.RightHand;
		Arm.LeftUpper = Info.LeftUpper;
		Arm.LeftLower = Info.LeftLower;
		Arm.LeftHand = Info.LeftHand;
		Arm.LeftPalmOffset = Info.LeftPalmOffset;

		// 자세가 바뀌면(무기 교체) 순간이동하지 않고 섞는다
		const float Blend = bHavePose ? FMath::Clamp(1.f - FMath::Exp(-BlendSpeed * DeltaSeconds), 0.f, 1.f) : 1.f;
		CurSocketTarget.Blend(CurSocketTarget, Info.SocketTarget, Blend);
		CurElbow = FMath::Lerp(CurElbow, Info.ElbowDirection, Blend);
		CurLeftTarget = FMath::Lerp(CurLeftTarget, Info.LeftHandTarget, Blend);
		bHavePose = true;
		LeftAlpha = FMath::FInterpTo(LeftAlpha, Info.bLeftHand ? 1.f : 0.f, DeltaSeconds, BlendSpeed);

		// 쥔 손 모양: 총은 RangedGripFingers(비면 GripFingers), 나머지는 GripFingers
		const FVHCharacterVisual& Rig = UVHSettings::Get().GetCharacterVisuals().Find(WeaponVisual->GetCharacterId());
		const TArray<FVHFingerCurl>& Right = (Info.bRanged && Rig.RangedGripFingers.Num() > 0) ? Rig.RangedGripFingers : Rig.GripFingers;
		Arm.RightFingers.Reset();
		for (const FVHFingerCurl& F : Right)
		{
			Arm.RightFingers.Emplace(F.Bone, F.Rotation.Quaternion());
		}
		Arm.LeftFingers.Reset();
		for (const FVHFingerCurl& F : Rig.LeftGripFingers)
		{
			Arm.LeftFingers.Emplace(F.Bone, F.Rotation.Quaternion());
		}
	}
	else
	{
		LeftAlpha = FMath::FInterpTo(LeftAlpha, 0.f, DeltaSeconds, BlendSpeed);
		if (VHArmAlpha < 0.01f)
		{
			bHavePose = false;
		}
	}

	// 발 기준·조준 방향 기준 → 캐릭터 메시 기준. 발 = 메시 컴포넌트의 원점, 조준 방향 = 액터가 바라보는 쪽
	const FTransform MeshWorld = Skel->GetComponentTransform();
	const FTransform AimFrame(FRotator(0.f, Owner->GetActorRotation().Yaw, 0.f), MeshWorld.GetLocation());
	// 웅크린 만큼 팔 자세를 약하게 (몸이 돌아가는 동안 손을 서 있는 자세 기준 자리로 잡아당기지 않게)
	Arm.Alpha = VHArmAlpha * (1.f - RollTuck);
	Arm.LeftAlpha = Arm.Alpha * LeftAlpha;
	// 반동·떨림은 섞은 자세 위에 바로 더한다 (UVHWeaponVisualComponent::GetHoldInfo 의 RecoilDelta)
	const FTransform KickedTarget = bHold ? CurSocketTarget * Info.RecoilDelta : CurSocketTarget;
	Arm.SocketTarget = (KickedTarget * AimFrame).GetRelativeTransform(MeshWorld);
	Arm.SocketTarget.SetScale3D(FVector::OneVector);
	// 허리 회전: 조준 방향 기준 회전 → 메시 기준 회전 (든 자세 섞임 정도만큼)
	Arm.Spine = bHold ? Info.SpineBone : NAME_None;
	if (bHold && !Info.SpineRotation.IsNearlyZero(0.01f))
	{
		const FQuat Aim = AimFrame.GetRotation();
		const FQuat Mesh = MeshWorld.GetRotation();
		const FQuat InMesh = Mesh.Inverse() * Aim * Info.SpineRotation.Quaternion() * Aim.Inverse() * Mesh;
		Arm.SpineRotation = FQuat::Slerp(FQuat::Identity, InMesh, VHArmAlpha).GetNormalized();
	}
	else
	{
		Arm.SpineRotation = FQuat::Identity;
	}
	auto DirToMesh = [&](const FVector& Dir)
	{
		return MeshWorld.InverseTransformVectorNoScale(AimFrame.TransformVectorNoScale(Dir)).GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0, 0.0, -1.0));
	};
	Arm.ElbowDir = DirToMesh(CurElbow);
	Arm.LeftElbowDir = DirToMesh(FVector(CurElbow.X, -CurElbow.Y, CurElbow.Z));	// 왼팔은 좌우를 뒤집는다
	Arm.LeftHandTarget = MeshWorld.InverseTransformPositionNoScale(AimFrame.TransformPositionNoScale(CurLeftTarget + (bHold ? Info.LeftHandRecoil : FVector::ZeroVector)));

#if ENABLE_DRAW_DEBUG
	if (bDebugDraw && VHArmAlpha > 0.01f)
	{
		const FTransform World = CurSocketTarget * AimFrame;
		DrawDebugCoordinateSystem(Owner->GetWorld(), World.GetLocation(), World.Rotator(), 25.f, false, -1.f, 0, 1.f);
		if (LeftAlpha > 0.01f)
		{
			DrawDebugSphere(Owner->GetWorld(), AimFrame.TransformPositionNoScale(CurLeftTarget), 4.f, 8, FColor::Yellow, false, -1.f, 0, 0.5f);
		}
	}
#endif
}

// ───────────────── 애니메이션 스레드 ─────────────────

void FVHThiefAnimProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);
	// 그래프가 없으면(다른 플레이어 표시, DA 의 Anim Class 가 이 클래스일 때) 몽타주 슬롯을 직접 등록해 섞는다 → 몽타주가 그래프 없이도 재생된다
	if (GetRootNode() == nullptr)
	{
		RegisterSlotNodeWithAnimInstance(FAnimSlotGroup::DefaultSlotName);
	}
}

void FVHThiefAnimProxy::Update(float DeltaSeconds)
{
	FAnimInstanceProxy::Update(DeltaSeconds);
	if (GetRootNode() == nullptr)
	{
		GetSlotWeight(FAnimSlotGroup::DefaultSlotName, SlotNodeWeight, SlotSourceWeight, SlotTotalWeight);
		UpdateSlotNodeWeight(FAnimSlotGroup::DefaultSlotName, SlotNodeWeight, 1.f);
	}
}

void FVHThiefAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (const UVHThiefAnimInstance* Instance = Cast<UVHThiefAnimInstance>(InAnimInstance))
	{
		Arm = Instance->Arm;
		Loco = Instance->Loco;
		Roll = Instance->Roll;
	}
}

bool FVHThiefAnimProxy::Evaluate(FPoseContext& Output)
{
	if (Loco.OneShot)
	{
		// 쓰러짐: 그래프·이동·팔 자세 대신 그 애니메이션의 한 순간 (마지막 자세로 멈춤)
		FAnimationPoseData PoseData(Output);
		Loco.OneShot->GetAnimationPose(PoseData, FAnimExtractContext(Loco.OneShotTime, false));
		return true;
	}
	if (GetRootNode() != nullptr)
	{
		EvaluateAnimationNode(Output);	// 애니메이션 블루프린트의 그래프
	}
	else
	{
		// 그래프 없음 (다른 플레이어 표시, 또는 DA 의 Anim Class 가 이 클래스인 내 캐릭터): 이동 → 몽타주
		if (Loco.BlendSpace)
		{
			EvaluateBlendSpace(Output);
		}
		else
		{
			EvaluateLocomotion(Output);
		}
		EvaluateSlot(Output);
	}
	ApplyRoll(Output);
	ApplyArms(Output);
	ApplyFingers(Output);
	return true;
}

void FVHThiefAnimProxy::EvaluateBlendSpace(FPoseContext& Output) const
{
	if (Loco.BlendSamples.Num() == 0)
	{
		Output.ResetToRefPose();
		return;
	}
	TArray<FBlendSampleData> Samples = Loco.BlendSamples;
	FAnimationPoseData Data(Output);
	Loco.BlendSpace->GetAnimationPose(Samples, FAnimExtractContext(0.0, false), Data);
}

void FVHThiefAnimProxy::EvaluateSlot(FPoseContext& Output)
{
	if (SlotNodeWeight <= ZERO_ANIMWEIGHT_THRESH)
	{
		return;
	}
	FPoseContext Source(Output);
	Source.Pose.CopyBonesFrom(Output.Pose);
	Source.Curve.CopyFrom(Output.Curve);
	Source.CustomAttributes.CopyFrom(Output.CustomAttributes);
	{
		const FAnimationPoseData SourceData(Source);
		FAnimationPoseData OutData(Output);
		SlotEvaluatePose(FAnimSlotGroup::DefaultSlotName, SourceData, SlotSourceWeight, OutData, SlotNodeWeight, SlotTotalWeight);
	}
	if (!Loco.bUpperBodyOnly)
	{
		return;
	}
	// 상체만: UpperBodyBone 과 그 자식은 몽타주 결과, 나머지(골반·다리)는 이동 자세 그대로
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	const int32 Count = Output.Pose.GetNumBones();
	if (UpperMaskBone != Loco.UpperBodyBone || UpperMaskBoneCount != Count || UpperMask.Num() != Count)
	{
		UpperMaskBone = Loco.UpperBodyBone;
		UpperMaskBoneCount = Count;
		UpperMask.Init(false, Count);
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Loco.UpperBodyBone);
		const FCompactPoseBoneIndex Upper = MeshIndex != INDEX_NONE ? Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex)) : FCompactPoseBoneIndex(INDEX_NONE);
		for (FCompactPoseBoneIndex I : Output.Pose.ForEachBoneIndex())
		{
			const FCompactPoseBoneIndex Parent = Bones.GetParentBoneIndex(I);
			UpperMask[I.GetInt()] = (Upper != INDEX_NONE && I == Upper) || (Parent != INDEX_NONE && UpperMask[Parent.GetInt()]);
		}
	}
	for (FCompactPoseBoneIndex I : Output.Pose.ForEachBoneIndex())
	{
		if (!UpperMask[I.GetInt()])
		{
			Output.Pose[I] = Source.Pose[I];
		}
	}
}

void FVHThiefAnimProxy::EvaluateLocomotion(FPoseContext& Output) const
{
	auto Sample = [](const UAnimSequence* Seq, double Time, FPoseContext& Context)
	{
		FAnimationPoseData Data(Context);
		Seq->GetAnimationPose(Data, FAnimExtractContext(Time, false, {}, true));
	};
	const UAnimSequence* Walk = Loco.Walk ? Loco.Walk : Loco.Run;
	const UAnimSequence* Run = Loco.Run ? Loco.Run : Loco.Walk;
	const float MoveWeight = Walk ? Loco.MoveWeight : 0.f;
	if (!Loco.Idle && !Walk)
	{
		Output.ResetToRefPose();
		return;
	}

	bool bHasMove = false;
	if (MoveWeight > 0.001f || !Loco.Idle)
	{
		const float RunWeight = Walk == Run ? 0.f : Loco.RunWeight;
		if (RunWeight >= 0.999f)
		{
			Sample(Run, Loco.RunTime, Output);
		}
		else
		{
			Sample(Walk, Walk == Loco.Walk ? Loco.WalkTime : Loco.RunTime, Output);
			if (RunWeight > 0.001f)
			{
				FPoseContext RunPose(Output);
				Sample(Run, Loco.RunTime, RunPose);
				FAnimationPoseData A(Output), B(RunPose);
				FAnimationRuntime::BlendTwoPosesTogetherInPlace(A, B, 1.f - RunWeight);
			}
		}
		bHasMove = true;
	}
	if (!Loco.Idle)
	{
		return;
	}
	if (!bHasMove)
	{
		Sample(Loco.Idle, Loco.IdleTime, Output);
	}
	else if (MoveWeight < 0.999f)
	{
		FPoseContext IdlePose(Output);
		Sample(Loco.Idle, Loco.IdleTime, IdlePose);
		FAnimationPoseData A(Output), B(IdlePose);
		FAnimationRuntime::BlendTwoPosesTogetherInPlace(A, B, MoveWeight);
	}
}

namespace
{
	FCompactPoseBoneIndex FindBone(const FBoneContainer& Bones, FName Name)
	{
		const int32 MeshIndex = Name.IsNone() ? INDEX_NONE : Bones.GetPoseBoneIndexForBoneName(Name);
		return MeshIndex != INDEX_NONE ? Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex)) : FCompactPoseBoneIndex(INDEX_NONE);
	}

	/** 어깨·팔꿈치를 돌려 손(Hand)을 Effector 로 보낸다. bSetHandRotation 이면 손 회전도 HandRotation 으로 맞춘다 */
	void SolveArm(FCSPose<FCompactPose>& Pose, FCompactPoseBoneIndex Upper, FCompactPoseBoneIndex Lower, FCompactPoseBoneIndex Hand,
		const FVector& Effector, const FVector& ElbowDir, bool bSetHandRotation, const FQuat& HandRotation)
	{
		FTransform UpperT = Pose.GetComponentSpaceTransform(Upper);
		FTransform LowerT = Pose.GetComponentSpaceTransform(Lower);
		FTransform HandT = Pose.GetComponentSpaceTransform(Hand);
		const FQuat LowerBefore = LowerT.GetRotation();
		const FVector JointTarget = UpperT.GetLocation() + ElbowDir * 100.0;
		AnimationCore::SolveTwoBoneIK(UpperT, LowerT, HandT, JointTarget, Effector, false, 1.0, 1.0);
		if (bSetHandRotation)
		{
			HandT.SetRotation(HandRotation);
		}
		else
		{
			// 손은 아래팔을 따라 같이 돈다
			HandT.SetRotation((LowerT.GetRotation() * LowerBefore.Inverse() * HandT.GetRotation()).GetNormalized());
		}
		Pose.SetComponentSpaceTransform(Upper, UpperT);
		Pose.SetComponentSpaceTransform(Lower, LowerT);
		Pose.SetComponentSpaceTransform(Hand, HandT);
	}
}

void FVHThiefAnimProxy::ApplyRoll(FPoseContext& Output) const
{
	if (!Roll.bActive)
	{
		return;
	}
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	FCSPose<FCompactPose> Pose;
	Pose.InitPose(Output.Pose);
	// (1) 웅크리기: 뼈를 몸의 오른쪽 축으로 돌린다 (부모 → 자식 순서. 자식은 부모 회전을 물려받아 굽힘이 쌓인다)
	//     + = 위쪽이 앞으로(허리·머리 숙임, 무릎 굽힘), − = 아래쪽이 앞으로(허벅지·팔을 앞으로 들기)
	auto BendAbout = [&](FName Name, const FVector& Axis, float Degrees)
	{
		const FCompactPoseBoneIndex Index = FindBone(Bones, Name);
		if (Index == INDEX_NONE || FMath::Abs(Degrees) < 0.01f)
		{
			return;
		}
		const FCompactPoseBoneIndex Parent = Bones.GetParentBoneIndex(Index);
		const FQuat BoneCS = Pose.GetComponentSpaceTransform(Index).GetRotation();
		const FQuat ParentCS = Parent != INDEX_NONE ? Pose.GetComponentSpaceTransform(Parent).GetRotation() : FQuat::Identity;
		const FQuat Delta(Axis, FMath::DegreesToRadians(Degrees));
		Output.Pose[Index].SetRotation((ParentCS.Inverse() * Delta * BoneCS).GetNormalized());
		Pose.InitPose(Output.Pose);
	};
	auto Bend = [&](FName Name, float Degrees) { BendAbout(Name, Roll.BodyRight, Degrees); };

	// (0) 다리 모으기: 걷기·달리기 보폭을 웅크린 만큼 기본 자세(나란히 선 다리) 쪽으로 되돌린다 → 거꾸로 돌 때 발이 벌어지지 않는다
	if (Roll.LegGather > 0.001f)
	{
		for (const FName& Name : { Roll.ThighL, Roll.ThighR, Roll.CalfL, Roll.CalfR, Roll.FootL, Roll.FootR })
		{
			const FCompactPoseBoneIndex Index = FindBone(Bones, Name);
			if (Index != INDEX_NONE)
			{
				const FQuat Ref = Bones.GetRefPoseTransform(Index).GetRotation();
				Output.Pose[Index].SetRotation(FQuat::Slerp(Output.Pose[Index].GetRotation(), Ref, Roll.LegGather).GetNormalized());
			}
		}
		Pose.InitPose(Output.Pose);
	}
	// 무릎 모으기 (허벅지를 굽히기 전, 다리가 아래를 향할 때 앞 축으로 안쪽으로)
	BendAbout(Roll.ThighL, Roll.BodyForward, Roll.KneesIn);
	BendAbout(Roll.ThighR, Roll.BodyForward, -Roll.KneesIn);
	for (const FName& Name : Roll.Spine) { Bend(Name, Roll.SpinePerBone); }
	for (const FName& Name : Roll.Head) { Bend(Name, Roll.HeadPerBone); }
	Bend(Roll.UpperL, -Roll.ArmForward);
	Bend(Roll.UpperR, -Roll.ArmForward);
	Bend(Roll.LowerL, -Roll.ElbowBend);
	Bend(Roll.LowerR, -Roll.ElbowBend);
	Bend(Roll.ThighL, -Roll.HipFlex);
	Bend(Roll.ThighR, -Roll.HipFlex);
	Bend(Roll.CalfL, Roll.KneeBend);
	Bend(Roll.CalfR, Roll.KneeBend);

	// (2) 몸 전체: 낮춘 뒤 중심(Pivot)을 축으로 구르는 방향 돌리기 + 앞구르기. 루트 뼈 하나만 움직이면 모든 뼈가 따라온다
	FTransform& Root = Output.Pose[FCompactPoseBoneIndex(0)];
	const FVector Lowered = Root.GetLocation() + Roll.Drop;
	Root.SetLocation(Roll.Pivot + Roll.RootRotation.RotateVector(Lowered - Roll.Pivot));
	Root.SetRotation((Roll.RootRotation * Root.GetRotation()).GetNormalized());
}

void FVHThiefAnimProxy::ApplyFingers(FPoseContext& Output) const
{
	// 손가락 뼈를 자기 축 기준으로 더 돌려 손잡이를 감싸 쥔다 (뼈 방향과 무관하게 DA 값만으로 맞춘다)
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	auto Curl = [&](const TArray<TPair<FName, FQuat>, TInlineAllocator<16>>& Fingers, float Alpha)
	{
		if (Alpha <= 0.001f)
		{
			return;
		}
		for (const TPair<FName, FQuat>& F : Fingers)
		{
			const FCompactPoseBoneIndex Index = FindBone(Bones, F.Key);
			if (Index != INDEX_NONE)
			{
				FTransform& Bone = Output.Pose[Index];
				const FQuat Target = (Bone.GetRotation() * F.Value).GetNormalized();
				Bone.SetRotation(FQuat::Slerp(Bone.GetRotation(), Target, Alpha).GetNormalized());
			}
		}
	};
	Curl(Arm.RightFingers, Arm.Alpha);
	Curl(Arm.LeftFingers, Arm.LeftAlpha);
}

void FVHThiefAnimProxy::ApplyArms(FPoseContext& Output) const
{
	if (Arm.Alpha <= 0.001f)
	{
		return;
	}
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	const FCompactPoseBoneIndex RU = FindBone(Bones, Arm.RightUpper), RL = FindBone(Bones, Arm.RightLower), RH = FindBone(Bones, Arm.RightHand);
	if (RU == INDEX_NONE || RL == INDEX_NONE || RH == INDEX_NONE
		|| Bones.GetParentBoneIndex(RH) != RL || Bones.GetParentBoneIndex(RL) != RU)
	{
		return;	// 뼈 이름이 다르거나 팔 구조가 다른 캐릭터: 건드리지 않는다 (DA_VHCharacterVisuals 의 팔 뼈 이름 확인)
	}
	const FCompactPoseBoneIndex LU = FindBone(Bones, Arm.LeftUpper), LL = FindBone(Bones, Arm.LeftLower), LH = FindBone(Bones, Arm.LeftHand);
	const bool bLeft = Arm.LeftAlpha > 0.001f && LU != INDEX_NONE && LL != INDEX_NONE && LH != INDEX_NONE
		&& Bones.GetParentBoneIndex(LH) == LL && Bones.GetParentBoneIndex(LL) == LU;

	// 섞기 전 값 (Alpha < 1 일 때 원래 애니메이션과 뼈별로 섞는다)
	const FCompactPoseBoneIndex Touched[] = { RU, RL, RH, LU, LL, LH };
	FTransform Before[6];
	for (int32 i = 0; i < 6; ++i)
	{
		Before[i] = Touched[i] != INDEX_NONE ? Output.Pose[Touched[i]] : FTransform::Identity;
	}

	FCSPose<FCompactPose> Pose;
	Pose.InitPose(Output.Pose);

	// 무기 동작의 허리 비틀기·숙이기 (어깨가 따라 움직인 뒤 팔을 맞춘다)
	const FCompactPoseBoneIndex Spine = FindBone(Bones, Arm.Spine);
	if (Spine != INDEX_NONE && !Arm.SpineRotation.Equals(FQuat::Identity, 1e-4))
	{
		const FCompactPoseBoneIndex Parent = Bones.GetParentBoneIndex(Spine);
		const FQuat SpineCS = Pose.GetComponentSpaceTransform(Spine).GetRotation();
		const FQuat ParentCS = Parent != INDEX_NONE ? Pose.GetComponentSpaceTransform(Parent).GetRotation() : FQuat::Identity;
		Output.Pose[Spine].SetRotation((ParentCS.Inverse() * Arm.SpineRotation * SpineCS).GetNormalized());
		Pose.InitPose(Output.Pose);
	}

	// 오른손: 소켓이 목표 자리·방향에 오도록 손 뼈의 자리·방향을 거꾸로 구한다
	const FQuat HandRotation = (Arm.SocketTarget.GetRotation() * Arm.SocketLocal.GetRotation().Inverse()).GetNormalized();
	const FVector HandLocation = Arm.SocketTarget.GetLocation() - HandRotation.RotateVector(Arm.SocketLocal.GetLocation());
	SolveArm(Pose, RU, RL, RH, HandLocation, Arm.ElbowDir, true, HandRotation);

	if (bLeft)
	{
		// 왼손: 손바닥이 무기에 닿게 손목을 팔 방향으로 조금 물린다
		const FVector Shoulder = Pose.GetComponentSpaceTransform(LU).GetLocation();
		const FVector Wrist = Arm.LeftHandTarget - (Arm.LeftHandTarget - Shoulder).GetSafeNormal() * Arm.LeftPalmOffset;
		SolveArm(Pose, LU, LL, LH, Wrist, Arm.LeftElbowDir, false, FQuat::Identity);
	}

	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(Pose, Output.Pose);

	auto BlendBack = [&](int32 First, int32 Last, float Alpha)
	{
		if (Alpha >= 0.999f)
		{
			return;
		}
		for (int32 i = First; i <= Last; ++i)
		{
			if (Touched[i] != INDEX_NONE)
			{
				FTransform& Bone = Output.Pose[Touched[i]];
				Bone.SetRotation(FQuat::Slerp(Before[i].GetRotation(), Bone.GetRotation(), Alpha).GetNormalized());
				Bone.SetTranslation(Before[i].GetTranslation());
			}
		}
	};
	BlendBack(0, 2, Arm.Alpha);
	BlendBack(3, 5, bLeft ? Arm.LeftAlpha : 0.f);
}

#if !UE_BUILD_SHIPPING
namespace
{
	// vh.ArmDebug 0/1 : 손 목표 위치(좌표축)와 왼손 목표(노란 공)를 화면에 그린다
	FAutoConsoleCommand GVHArmDebugCommand(TEXT("vh.ArmDebug"), TEXT("개발용: 무기 든 손의 목표 위치를 화면에 그린다 (0/1)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			UVHThiefAnimInstance::bDebugDraw = Args.Num() > 0 ? FCString::Atoi(*Args[0]) != 0 : !UVHThiefAnimInstance::bDebugDraw;
		}));

	// vh.SocketGuess : 지금 캐릭터 메시의 뼈 위치로 손 소켓(WeaponSocket_R)의 추천 값을 계산해 로그로 알려 준다.
	// 약속: 소켓 +X = 손목→손가락 쪽(무기 앞), +Z = 엄지 쪽(무기 위). 손가락 뼈 이름은 언리얼 마네킹 기준
	FAutoConsoleCommandWithWorldAndArgs GVHSocketGuessCommand(TEXT("vh.SocketGuess"),
		TEXT("개발용: 캐릭터 메시의 뼈 위치로 손 소켓(WeaponSocket_R) 추천 값을 로그에 적는다. 인자: [손 뼈] [가운뎃손가락 뼈] [집게손가락 뼈] [새끼손가락 뼈]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const FName HandName(Args.Num() > 0 ? *Args[0] : TEXT("hand_r"));
			const FName MiddleName(Args.Num() > 1 ? *Args[1] : TEXT("middle_metacarpal_r"));
			const FName IndexName(Args.Num() > 2 ? *Args[2] : TEXT("index_metacarpal_r"));
			const FName PinkyName(Args.Num() > 3 ? *Args[3] : TEXT("pinky_metacarpal_r"));
			for (TObjectIterator<UVHThiefAnimInstance> It; It; ++It)
			{
				const USkeletalMeshComponent* Skel = It->GetSkelMeshComponent();
				const USkeletalMesh* Mesh = Skel && It->GetWorld() == World ? Skel->GetSkeletalMeshAsset() : nullptr;
				if (!Mesh)
				{
					continue;
				}
				const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
				const int32 Hand = Ref.FindBoneIndex(HandName), Middle = Ref.FindBoneIndex(MiddleName), Index = Ref.FindBoneIndex(IndexName), Pinky = Ref.FindBoneIndex(PinkyName);
				if (Hand == INDEX_NONE || Middle == INDEX_NONE || Index == INDEX_NONE || Pinky == INDEX_NONE)
				{
					UE_LOG(LogVHThiefAnim, Warning, TEXT("[손 소켓 추천] %s: 뼈를 찾지 못했습니다 (%s %d, %s %d, %s %d, %s %d). 뼈 이름을 인자로 주세요."), *Mesh->GetName(),
						*HandName.ToString(), Hand, *MiddleName.ToString(), Middle, *IndexName.ToString(), Index, *PinkyName.ToString(), Pinky);
					return;
				}
				const FTransform HandT = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Hand);
				const FVector MiddleP = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Middle).GetLocation();
				const FVector IndexP = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Index).GetLocation();
				const FVector PinkyP = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Pinky).GetLocation();
				const FVector Forward = (MiddleP - HandT.GetLocation()).GetSafeNormal();
				FVector Up = (IndexP - PinkyP);
				Up = (Up - Forward * FVector::DotProduct(Up, Forward)).GetSafeNormal();
				const FQuat WorldRot = FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();
				const FVector Palm = FVector::CrossProduct(Forward, Up);	// 손바닥이 향하는 쪽이거나 그 반대 (오른손: 무기 왼쪽 = 손바닥)
				const FVector Grip = MiddleP + Forward * 3.0;
				const FTransform Local = FTransform(WorldRot, Grip).GetRelativeTransform(HandT);
				const FRotator R = Local.Rotator();
				const FVector L = Local.GetLocation();
				UE_LOG(LogVHThiefAnim, Display, TEXT("[손 소켓 추천] %s 본 %s → 위치 (%.1f, %.1f, %.1f) 회전 Pitch %.1f Yaw %.1f Roll %.1f (손바닥 쪽 %s)"),
					*Mesh->GetName(), *HandName.ToString(), L.X, L.Y, L.Z, R.Pitch, R.Yaw, R.Roll, *HandT.InverseTransformVectorNoScale(Palm).ToCompactString());
				return;
			}
			UE_LOG(LogVHThiefAnim, Warning, TEXT("[손 소켓 추천] UVHThiefAnimInstance 를 쓰는 캐릭터가 없습니다 (게임 레벨에서 ▶ 를 누른 뒤 실행)."));
		}));
}
#endif
