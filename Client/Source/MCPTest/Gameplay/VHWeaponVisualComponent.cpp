#include "Gameplay/VHWeaponVisualComponent.h"

#include "Gameplay/VHCharacterVisuals.h"
#include "Gameplay/VHWeaponCatalog.h"
#include "Gameplay/VHThiefAnimInstance.h"
#include "Gameplay/VHSoundConfig.h"
#include "Network/VHSettings.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Containers/Ticker.h"
#include "Gameplay/VHActionComponent.h"
#include "Gameplay/VHAimComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Gameplay/VHWeaponFx.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogVHWeaponVisual, Log, All);

int32 UVHWeaponVisualComponent::DebugLocalWeapon = -1;
float UVHWeaponVisualComponent::DebugFreezeTime = -1.f;
bool UVHWeaponVisualComponent::bDebugFreezeProxies = false;
float UVHWeaponVisualComponent::DebugRollFreeze = -1.f;
int32 UVHWeaponVisualComponent::DebugHold = -1;
int32 UVHWeaponVisualComponent::DebugRailLock = -1;
FVHOnWeaponActionEffect UVHWeaponVisualComponent::OnAnyActionEffect;

UVHWeaponVisualComponent::UVHWeaponVisualComponent()
{
	// 보임/숨김만 캐릭터 메시에 맞춘다 → 0.1초에 한 번이면 충분하다
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
}

void UVHWeaponVisualComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bApplyCharacterMesh)
	{
		UVHCharacterVisuals::ApplyMesh(FindTargetMesh(), UVHSettings::Get().GetCharacterVisuals().Find(CharacterId), true);
	}
	ApplyWeapon();
}

USkeletalMeshComponent* UVHWeaponVisualComponent::FindTargetMesh() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	if (!TargetMeshComponentName.IsNone())
	{
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Owner);
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			if (Mesh->GetFName() == TargetMeshComponentName)
			{
				return Mesh;
			}
		}
	}
	if (const ACharacter* Character = Cast<ACharacter>(Owner))
	{
		return Character->GetMesh();
	}
	return Owner->FindComponentByClass<USkeletalMeshComponent>();
}

void UVHWeaponVisualComponent::SetWeapon(uint8 WeaponId)
{
	if (DebugLocalWeapon >= 0)
	{
		const APawn* Pawn = Cast<APawn>(GetOwner());
		if (Pawn && Pawn->IsLocallyControlled())
		{
			WeaponId = static_cast<uint8>(DebugLocalWeapon);
		}
	}
	if (IsActionPlaying() && WeaponId != ActionWeapon)
	{
		// 동작(특히 던지기·설치) 중에 서버가 든 무기를 바꿨다(탄을 다 써서 삼단봉 등) → 동작이 끝난 뒤 바꾼다
		PendingWeapon = WeaponId;
		return;
	}
	PendingWeapon = -1;
	ApplyWeaponId(WeaponId);
}

void UVHWeaponVisualComponent::ApplyWeaponId(uint8 WeaponId)
{
	if (CurrentWeapon == WeaponId)
	{
		return;
	}
	CurrentWeapon = WeaponId;
	ApplyWeapon();
	OnWeaponChanged.Broadcast(WeaponId);
}

bool UVHWeaponVisualComponent::IsLocalPawn() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn && Pawn->IsLocallyControlled();
}

float UVHWeaponVisualComponent::GetActionTime() const
{
	if (!IsActionPlaying())
	{
		return 0.f;
	}
	if (DebugFreezeTime >= 0.f && (IsLocalPawn() || (bDebugFreezeProxies && GetOwner() && GetOwner()->ActorHasTag(TEXT("VHTestProxy")))))
	{
		return DebugFreezeTime;
	}
	const UWorld* World = GetWorld();
	return World ? static_cast<float>(World->GetTimeSeconds() - ActionStart) * ActionRate : 0.f;
}

FTransform UVHWeaponVisualComponent::GetWeaponWorldTransform() const
{
	return WeaponMesh ? WeaponMesh->GetComponentTransform() : (GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity);
}

bool UVHWeaponVisualComponent::IsWeaponShown() const
{
	const AActor* Owner = GetOwner();
	const USkeletalMeshComponent* Skel = FindTargetMesh();
	return CurrentWeapon != 0 && Owner && !Owner->IsHidden() && (!Skel || Skel->IsVisible());
}

bool UVHWeaponVisualComponent::GetMuzzleLocation(FVector& Out) const
{
	if (!WeaponMesh || !WeaponMesh->GetStaticMesh() || CurrentWeapon == 0)
	{
		return false;
	}
	// 근접은 칼끝 소켓이 있으면 그것, 없으면 예전처럼 앞 끝(Muzzle 계산)
	if (UVHWeaponCatalog::CategoryOf(CurrentWeapon) == EVHWeaponCategory::Melee
		&& WeaponMesh->GetStaticMesh()->FindSocket(FVHWeaponVisual::SocketName(EVHWeaponPoint::BladeTip)))
	{
		return GetWeaponPoint(EVHWeaponPoint::BladeTip, Out);
	}
	return GetWeaponPoint(EVHWeaponPoint::Muzzle, Out);
}

bool UVHWeaponVisualComponent::GetWeaponPointLocal(EVHWeaponPoint Point, FVector& Out) const
{
	const UStaticMesh* Mesh = WeaponMesh ? WeaponMesh->GetStaticMesh() : nullptr;
	if (!Mesh || CurrentWeapon == 0)
	{
		return false;
	}
	const FVHWeaponVisual* Visual = UVHSettings::Get().GetWeaponCatalog().Find(CurrentWeapon);
	if (Visual)
	{
		return Visual->PointLocal(Mesh, Point, Out);
	}
	FVHWeaponVisual Default;	// 표에 없는 무기: 보정 없이 소켓·자동 계산
	return Default.PointLocal(Mesh, Point, Out);
}

bool UVHWeaponVisualComponent::GetWeaponPoint(EVHWeaponPoint Point, FVector& Out) const
{
	FVector Local;
	if (!GetWeaponPointLocal(Point, Local))
	{
		return false;
	}
	Out = WeaponMesh->GetComponentTransform().TransformPosition(Local);
	return true;
}

void UVHWeaponVisualComponent::PlayAttack(uint8 WeaponId)
{
	if (WeaponId == 0 || !GetWorld())
	{
		return;
	}
	const FVHCharacterVisual& Rig = UVHSettings::Get().GetCharacterVisuals().Find(CharacterId);
	// 무기 표에 반동(Recoil 칸)이 있으면 키 프레임·몽타주 대신 반동으로 움직인다 (2026-11-20, 권총·레일건·테이저건)
	const FVHWeaponVisual* WeaponRow = UVHSettings::Get().GetWeaponCatalog().Find(WeaponId);
	const FVHWeaponRecoil* Recoil = (WeaponRow && WeaponRow->Recoil.HasKick()) ? &WeaponRow->Recoil : nullptr;
	const FVHWeaponMotion* Motion = Recoil ? nullptr : Rig.AttackMotions.Find(static_cast<EVHWeaponId>(WeaponId));
	UAnimMontage* Montage = Motion ? Motion->Montage.LoadSynchronous() : nullptr;
	if (!Montage && !Recoil)
	{
		if (const TSoftObjectPtr<UAnimMontage>* ByCategory = Rig.CategoryAttackMontages.Find(UVHWeaponCatalog::CategoryOf(WeaponId)))
		{
			Montage = ByCategory->LoadSynchronous();
		}
	}
	const float Duration = Recoil ? Recoil->TotalSeconds() : (Montage ? Montage->GetPlayLength() : (Motion ? Motion->Duration() : 0.f));
	if (Duration <= 0.f)
	{
		// 동작이 없는 무기(화염방사기: 꾹 누르는 자세만): 앞의 다른 무기 동작은 끝내고, 연출 순간만 바로 알린다
		if (IsActionPlaying() && ActionWeapon != WeaponId)
		{
			EndAction();
		}
		OnAnyActionEffect.Broadcast(this, WeaponId);
		return;
	}
	if (IsActionPlaying())
	{
		EndAction();	// 앞 동작이 남아 있으면 끊고 새로 (연사)
	}
	if (WeaponId != CurrentWeapon)
	{
		// 서버가 이미 든 무기를 바꿨어도(수류탄 1개를 던지면 바로 삼단봉) 던지는 동안은 수류탄을 보여 준다
		PendingWeapon = CurrentWeapon;
		ApplyWeaponId(WeaponId);
	}
	// 서버의 공격 간격보다 길면 빨리 재생한다 (판정은 서버가 했으니 화면이 다음 공격과 겹치지 않게만)
	const float Interval = UVHWeaponCatalog::ServerIntervalSeconds(WeaponId);
	ActionRate = Duration > Interval * 0.95f ? Duration / (Interval * 0.95f) : 1.f;
	ActionWeapon = WeaponId;
	ActionStart = GetWorld()->GetTimeSeconds();
	ActionDuration = Duration;
	ActionEffectTime = Motion ? Motion->EffectTime : 0.f;
	bEffectFired = false;
	bHiddenByAction = false;
	bActionMontage = false;
	bActionRecoil = Recoil != nullptr;
	if (Recoil)
	{
		AddRecoil(WeaponId);
	}
	if (Montage)
	{
		if (USkeletalMeshComponent* Skel = FindTargetMesh())
		{
			if (UAnimInstance* Anim = Skel->GetAnimInstance())
			{
				bActionMontage = Anim->Montage_Play(Montage, ActionRate) > 0.f;
			}
		}
	}
	if (ActionEffectTime <= 0.f)
	{
		bEffectFired = true;
		OnAnyActionEffect.Broadcast(this, WeaponId);
	}
	UpdateTickRate();
}

void UVHWeaponVisualComponent::PlayRoll(const FVector& WorldDir, float Duration)
{
	if (!GetWorld() || Duration <= 0.f)
	{
		return;
	}
	if (IsRolling())
	{
		StopRoll();
	}
	RollStart = GetWorld()->GetTimeSeconds();
	RollDuration = Duration;
	RollDir = WorldDir.GetSafeNormal2D();
	if (RollDir.IsNearlyZero())
	{
		RollDir = GetOwner() ? GetOwner()->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	}
	bRollMontage = false;
	RollMontage.Reset();
	if (BodyPhase == EBodyPhase::OneShot)
	{
		StopBodyMontage(0.1f);	// 맞음·줍기 등은 구르기로 끊는다
	}
	// 구르기 몽타주가 있으면 구르기 시간에 맞춰 재생. 그래프가 있는 애니메이션 블루프린트 또는 UVHThiefAnimInstance(그래프 없이 슬롯을 직접 섞음)
	const FVHCharacterVisual& Rig = UVHSettings::Get().GetCharacterVisuals().Find(CharacterId);
	if (UAnimMontage* Montage = Rig.RollMotion.Montage.LoadSynchronous())
	{
		USkeletalMeshComponent* Skel = FindTargetMesh();
		UAnimInstance* Anim = Skel ? Skel->GetAnimInstance() : nullptr;
		if (Anim && (Cast<UAnimBlueprintGeneratedClass>(Anim->GetClass()) || Anim->IsA<UVHThiefAnimInstance>()) && Montage->GetPlayLength() > 0.f)
		{
			// 몽타주 길이(시작 위치 뒤) ÷ 구르기 시간 = 재생 빠르기 → 어떤 길이든 구르기 시간 안에 끝난다
			const float Start = FMath::Clamp(Rig.RollMotion.MontageStartTime, 0.f, Montage->GetPlayLength() * 0.8f);
			bRollMontage = Anim->Montage_Play(Montage, (Montage->GetPlayLength() - Start) / Duration, EMontagePlayReturnType::MontageLength, Start, false) > 0.f;
			RollMontage = Montage;
		}
	}
	UVHSoundConfig::Play(this, EVHSound::Roll, GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);
	UE_LOG(LogVHWeaponVisual, Log, TEXT("[구르기 동작] %s 시작: 방향 (%.2f, %.2f), %.2f초, %s"), *GetNameSafe(GetOwner()), RollDir.X, RollDir.Y, Duration,
		bRollMontage ? TEXT("몽타주") : TEXT("키 프레임"));
	UpdateTickRate();
	UpdateRollHide();
}

FVector UVHWeaponVisualComponent::PrepareOpenRun(APawn* Pawn, float& OutFree)
{
	OutFree = 0.f;
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	if (!World)
	{
		return FVector::ForwardVector;
	}
	const FVector From = Pawn->GetActorLocation();
	FCollisionQueryParams Query(FName(TEXT("VHOpenRun")), false, Pawn);
	const FCollisionShape Ball = FCollisionShape::MakeSphere(30.f);
	auto Free = [&](const FVector& D)
	{
		FHitResult Hit;
		return World->SweepSingleByChannel(Hit, From, From + D * 1500.f, FQuat::Identity, ECC_Pawn, Ball, Query) ? Hit.Distance : 1500.f;
	};
	FVector Best = FVector::ForwardVector;
	float BestTotal = -1.f, BestBack = 0.f, BestFront = 0.f;
	for (int32 i = 0; i < 8; ++i)
	{
		const FVector D = FRotator(0.f, 45.f * i, 0.f).Vector();
		const float Front = Free(D), Back = Free(-D);
		if (Front + Back > BestTotal + 1.f)
		{
			BestTotal = Front + Back;
			Best = D;
			BestFront = Front;
			BestBack = Back;
		}
	}
	if (BestBack > 80.f)
	{
		Pawn->TeleportTo(From - Best * (BestBack - 60.f), Best.Rotation());
	}
	OutFree = BestFront + FMath::Max(0.f, BestBack - 60.f);
	return Best;
}

void UVHWeaponVisualComponent::UpdateRollHide()
{
	// 구르기 시작 → 팔을 다시 쥐는 때(RegripAt)까지 무기를 숨긴다. 다른 플레이어(AVHNetProxy)도 같은 컴포넌트라 똑같이 숨는다
	float U = 0.f;
	FVector Dir;
	bool bMontage = false;
	bool bHide = false;
	if (GetRollState(U, Dir, bMontage))
	{
		const FVHRollMotion& Motion = UVHSettings::Get().GetCharacterVisuals().Find(CharacterId).RollMotion;
		bHide = Motion.bHideWeapon && U < Motion.RegripAt;
	}
	if (bHide != bHiddenByRoll)
	{
		bHiddenByRoll = bHide;
		SyncVisibility();
	}
}

void UVHWeaponVisualComponent::StopRoll()
{
	if (!IsRolling())
	{
		return;
	}
	RollStart = -1.0;
	if (bHiddenByRoll)
	{
		bHiddenByRoll = false;
		SyncVisibility();
	}
	if (bRollMontage && RollMontage.IsValid())
	{
		if (USkeletalMeshComponent* Skel = FindTargetMesh(); Skel && Skel->GetAnimInstance())
		{
			Skel->GetAnimInstance()->Montage_Stop(0.1f, RollMontage.Get());
		}
	}
	bRollMontage = false;
	RollMontage.Reset();
	UpdateTickRate();
}

void UVHWeaponVisualComponent::SetRollDirection(const FVector& WorldDir)
{
	const FVector Dir = WorldDir.GetSafeNormal2D();
	if (IsRolling() && !Dir.IsNearlyZero())
	{
		RollDir = Dir;
	}
}

bool UVHWeaponVisualComponent::GetRollState(float& OutU, FVector& OutDir, bool& bOutMontage) const
{
	if (DebugRollFreeze >= 0.f && IsLocalPawn())
	{
		OutU = FMath::Clamp(DebugRollFreeze, 0.f, 1.f);
		OutDir = RollDir;
		bOutMontage = false;
		return true;
	}
	const UWorld* World = GetWorld();
	if (!IsRolling() || !World)
	{
		return false;
	}
	OutU = FMath::Clamp(static_cast<float>(World->GetTimeSeconds() - RollStart) / FMath::Max(RollDuration, 0.01f), 0.f, 1.f);
	OutDir = RollDir;
	bOutMontage = bRollMontage;
	return true;
}

void UVHWeaponVisualComponent::EndAction()
{
	ActionStart = -1.0;
	bActionMontage = false;
	if (bHiddenByAction)
	{
		bHiddenByAction = false;
		SyncVisibility();
	}
	if (PendingWeapon >= 0)
	{
		const uint8 Next = static_cast<uint8>(PendingWeapon);
		PendingWeapon = -1;
		ApplyWeaponId(Next);
	}
	UpdateTickRate();
}

void UVHWeaponVisualComponent::SetAttackHeld(bool bHeld)
{
	bAttackHeld = bHeld;
	UpdateTickRate();
}

void UVHWeaponVisualComponent::SetRailgunLock(bool bLock)
{
	bRailgunLock = bLock;
	UpdateTickRate();
}

void UVHWeaponVisualComponent::SetRailgunCharging(bool bCharging, float Seconds)
{
	if (bCharging == IsRailgunCharging())
	{
		return;
	}
	RailChargeStart = bCharging && GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
	RailChargeSeconds = FMath::Max(Seconds, 0.05f);
	UpdateTickRate();
	if (UVHWeaponFxSubsystem* Fx = UVHWeaponFxSubsystem::Get(this))
	{
		Fx->OnRailgunChargeChanged(this, bCharging);
	}
}

float UVHWeaponVisualComponent::GetRailgunChargeProgress() const
{
	if (!IsRailgunCharging() || !GetWorld())
	{
		return 0.f;
	}
	return FMath::Clamp(static_cast<float>(GetWorld()->GetTimeSeconds() - RailChargeStart) / RailChargeSeconds, 0.f, 1.f);
}

void UVHWeaponVisualComponent::AddRecoil(uint8 WeaponId)
{
	if (!GetWorld())
	{
		return;
	}
	FRecoilKick& K = Kicks.AddDefaulted_GetRef();
	K.Start = GetWorld()->GetTimeSeconds();
	K.Weapon = WeaponId;
	K.Side = FMath::FRandRange(-1.f, 1.f);
	K.CameraPhase = FMath::FRandRange(0.f, 2.f * PI);
	if (Kicks.Num() > 8)
	{
		Kicks.RemoveAt(0);	// 아주 빠른 연사에서도 쌓이는 수를 묶는다
	}
	UpdateTickRate();
}

void UVHWeaponVisualComponent::EvaluateRecoil(float Now, FVector& Hand, FRotator& Rot, float& SpineYaw, float& SpinePitch) const
{
	if (Kicks.Num() == 0)
	{
		return;
	}
	const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
	// 무기별로 곡선을 더한 뒤 MaxStack 으로 자른다 (연사해도 총이 하늘로 솟지 않게)
	float SumMuzzle = 0.f, SumBody = 0.f, SumSide = 0.f;
	const FVHWeaponRecoil* R = nullptr;
	for (const FRecoilKick& K : Kicks)
	{
		const FVHWeaponVisual* Row = Catalog.Find(K.Weapon);
		if (!Row || !Row->Recoil.HasKick())
		{
			continue;
		}
		float M, B;
		Row->Recoil.Evaluate(Now - static_cast<float>(K.Start), M, B);
		SumMuzzle += M;
		SumBody += B;
		SumSide += M * K.Side;
		R = &Row->Recoil;	// 마지막 발의 무기 값으로 세기를 정한다 (보통 한 무기)
	}
	if (!R)
	{
		return;
	}
	const float Cap = FMath::Max(R->MaxStack, 1.f);
	const float M = FMath::Min(SumMuzzle, Cap);
	const float B = FMath::Min(SumBody, Cap);
	const float S = SumMuzzle > Cap ? SumSide * Cap / SumMuzzle : SumSide;
	// 손(무기 손잡이) 위치: 뒤로(−X) + 총구가 들릴 때 조금 위로(+Z). 무기 회전: 총구 위로(+Pitch) + 좌우 무작위
	Hand += FVector(-R->KickBackCm * B, 0.f, R->KickUpCm * M);
	Rot += FRotator(R->MuzzleUpDeg * M, R->SideDeg * S, 0.f);
	// 상체: 뒤로 젖힘(허리 Pitch 음수 = 뒤로) + 비틀림. 몸은 손과 같이 늦게 따라온다
	SpinePitch -= R->SpineBackDeg * B;
	SpineYaw += R->SpineTwistDeg * B;
}

void UVHWeaponVisualComponent::UpdateCameraShake(float Now)
{
	// 내 캐릭터만. 탑뷰라 카메라 위치(화면 평면 Y·Z)만 아주 조금 흔든다 → 조준(커서 아래 바닥 지점)은 카메라와 따로 계산하므로 영향 없음
	if (!IsLocalPawn())
	{
		return;
	}
	const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
	FVector Offset = FVector::ZeroVector;
	for (const FRecoilKick& K : Kicks)
	{
		const FVHWeaponVisual* Row = Catalog.Find(K.Weapon);
		if (!Row || Row->Recoil.CameraShakeCm <= 0.f)
		{
			continue;
		}
		const float Age = Now - static_cast<float>(K.Start);
		const float Dur = FMath::Max(Row->Recoil.CameraShakeSeconds, 0.02f);
		if (Age < 0.f || Age >= Dur)
		{
			continue;
		}
		const float A = Row->Recoil.CameraShakeCm * FMath::Square(1.f - Age / Dur);
		const float P = 2.f * PI * Age;
		Offset += FVector(0.f, FMath::Sin(P * 29.f + K.CameraPhase), FMath::Sin(P * 23.f + K.CameraPhase * 1.7f) * 0.8f + 0.4f) * A;
	}
	if (!ShakeCamera.IsValid())
	{
		if (Offset.IsNearlyZero())
		{
			return;
		}
		ShakeCamera = GetOwner() ? GetOwner()->FindComponentByClass<UCameraComponent>() : nullptr;
		if (!ShakeCamera.IsValid())
		{
			return;
		}
	}
	UCameraComponent* Cam = ShakeCamera.Get();
	if (!bCameraShaken)
	{
		if (Offset.IsNearlyZero())
		{
			return;
		}
		ShakeCameraBase = Cam->GetRelativeLocation();
		bCameraShaken = true;
	}
	Cam->SetRelativeLocation(ShakeCameraBase + Offset);
	if (Offset.IsNearlyZero())
	{
		bCameraShaken = false;	// 원래 자리로 돌아왔다
	}
}

void UVHWeaponVisualComponent::UpdateTickRate()
{
	// 동작 중에는 매 프레임(연출 순간·숨기기를 정확히), 아니면 0.1초에 한 번(보임/숨김만)
	const bool bBusy = IsActionPlaying() || IsRolling() || bAttackHeld || bRailgunLock || HoldAlpha > 0.001f || LockAlpha > 0.001f || BodyPhase != EBodyPhase::None
		|| IsRailgunCharging() || Kicks.Num() > 0 || bCameraShaken
		|| (IsLocalPawn() && (DebugHold > 0 || DebugRailLock > 0));
	SetComponentTickInterval(bBusy ? 0.f : 0.1f);
}


void UVHWeaponVisualComponent::SetCharacterId(int32 InCharacterId)
{
	if (CharacterId == InCharacterId)
	{
		return;
	}
	CharacterId = InCharacterId;
	bWarnedNoSocket = false;
	if (bApplyCharacterMesh && HasBegunPlay())
	{
		// 확인용: 내 캐릭터가 서버가 알려 준 캐릭터 번호의 모습으로 바뀜 (다른 플레이어 표시는 AVHNetProxy::InitProxy 가 따로 적는다)
		UE_LOG(LogVHWeaponVisual, Display, TEXT("[캐릭터] 내 캐릭터 %d → 메시 %s"), CharacterId,
			*UVHSettings::Get().GetCharacterVisuals().Find(CharacterId).Mesh.ToSoftObjectPath().GetAssetName());
		UVHCharacterVisuals::ApplyMesh(FindTargetMesh(), UVHSettings::Get().GetCharacterVisuals().Find(CharacterId), true);
	}
	ApplyAttachment();
}

void UVHWeaponVisualComponent::Refresh()
{
	bWarnedNoSocket = false;
	ApplyWeapon();
}

void UVHWeaponVisualComponent::SetOverrideMaterial(UMaterialInterface* Material)
{
	if (OverrideMaterial == Material)
	{
		return;
	}
	OverrideMaterial = Material;
	ApplyMaterials();
}

void UVHWeaponVisualComponent::ApplyWeapon()
{
	AActor* Owner = GetOwner();
	if (!Owner || !HasBegunPlay())
	{
		return;
	}
	const FVHWeaponVisual* Visual = UVHSettings::Get().GetWeaponCatalog().Find(CurrentWeapon);
	UStaticMesh* Mesh = Visual ? Visual->Mesh.LoadSynchronous() : nullptr;
	if (!Mesh)
	{
		// 맨손이거나 아직 모델이 없는 무기: 아무것도 보여 주지 않는다
		if (WeaponMesh)
		{
			WeaponMesh->SetStaticMesh(nullptr);
			WeaponMesh->SetVisibility(false);
		}
		return;
	}
	if (!WeaponMesh)
	{
		WeaponMesh = NewObject<UStaticMeshComponent>(Owner, TEXT("VHHeldWeapon"), RF_Transient);
		WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WeaponMesh->SetGenerateOverlapEvents(false);
		WeaponMesh->SetCanEverAffectNavigation(false);
		WeaponMesh->SetCastShadow(false);
		WeaponMesh->SetupAttachment(Owner->GetRootComponent());
		WeaponMesh->RegisterComponent();
	}
	WeaponMesh->SetStaticMesh(Mesh);
	ApplyMaterials();
	ApplyAttachment();
	SyncVisibility();
}

void UVHWeaponVisualComponent::ApplyMaterials()
{
	if (!WeaponMesh || !WeaponMesh->GetStaticMesh())
	{
		return;
	}
	WeaponMesh->EmptyOverrideMaterials();
	const int32 Num = WeaponMesh->GetNumMaterials();
	if (OverrideMaterial)
	{
		for (int32 i = 0; i < Num; ++i)
		{
			WeaponMesh->SetMaterial(i, OverrideMaterial);
		}
		return;
	}
	if (const FVHWeaponVisual* Visual = UVHSettings::Get().GetWeaponCatalog().Find(CurrentWeapon))
	{
		for (int32 i = 0; i < Num && i < Visual->MaterialOverrides.Num(); ++i)
		{
			if (UMaterialInterface* Material = Visual->MaterialOverrides[i].LoadSynchronous())
			{
				WeaponMesh->SetMaterial(i, Material);
			}
		}
	}
}

void UVHWeaponVisualComponent::ApplyAttachment()
{
	AActor* Owner = GetOwner();
	if (!WeaponMesh || !Owner)
	{
		return;
	}
	const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
	const FVHWeaponVisual* Weapon = Catalog.Find(CurrentWeapon);
	const FVHCharacterVisual& Rig = UVHSettings::Get().GetCharacterVisuals().Find(CharacterId);
	USkeletalMeshComponent* Skel = FindTargetMesh();
	const float Scale = Catalog.HeldScale * (Weapon ? Weapon->HeldScale : 1.f) * Rig.WeaponScale;
	const float Thick = Weapon ? Weapon->HeldThickness : 1.f;

	const bool bWantSocket = Rig.HoldMode == EVHWeaponHoldMode::HandSocket;
	const bool bHasSocket = bWantSocket && Skel && Skel->DoesSocketExist(Rig.WeaponSocketName);
	if (bWantSocket && !bHasSocket && !bWarnedNoSocket)
	{
		// 멈추지 않는다: 경고만 한 번 남기고 몸 앞 기본 위치에 붙인다
		bWarnedNoSocket = true;
		UE_LOG(LogVHWeaponVisual, Warning, TEXT("[무기 표시] %s: 캐릭터 메시(%s)에 소켓 '%s'이 없습니다 → 몸 앞 기본 위치에 붙입니다. (Docs/캐릭터_교체_가이드.md 참고)"),
			*Owner->GetName(), Skel && Skel->GetSkeletalMeshAsset() ? *Skel->GetSkeletalMeshAsset()->GetName() : TEXT("없음"), *Rig.WeaponSocketName.ToString());
	}

	bOnHandSocket = bHasSocket;
	// 모델 좌표 → 무기 좌표: 모델 보정(회전·크기) + 표시 배율(길이·두께) + Grip 소켓을 손 자리로 (2026-11-20)
	const FVector Display(Scale, Scale * Thick, Scale * Thick);
	FVHWeaponVisual Default;
	MeshInner = (Weapon ? *Weapon : Default).MeshToWeapon(WeaponMesh->GetStaticMesh(), Display, true);
	if (bHasSocket)
	{
		// 최종 위치 = 모델 → 무기 보정(손잡이 맞춤) → 캐릭터 보정(손 크기·모양) → 소켓. 팔 자세는 애니메이션 인스턴스가 만든다(GetHoldInfo)
		WeaponMesh->AttachToComponent(Skel, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Rig.WeaponSocketName);
		FTransform Base = (Weapon ? Weapon->GripOffset : FTransform::Identity) * Rig.SocketOffset;
		Base.SetScale3D(FVector::OneVector);
		WeaponMesh->SetRelativeTransform(MeshInner * Base);
	}
	else
	{
		// 몸 앞 기본 위치: 액터가 바라보는 방향(= 시선·공격 방향) 그대로. 높이는 발 기준이다
		USceneComponent* Root = Owner->GetRootComponent();
		WeaponMesh->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
		// 분류별(무기별) 자세 → 그 위에 무기 쪽 보정(GripOffset). 무기 앞(+X)이 시선 방향이다
		const float FeetZ = (Skel && Skel->GetAttachParent() == Root) ? Skel->GetRelativeLocation().Z : 0.f;
		const uint8 Category = static_cast<uint8>(Weapon ? Weapon->Category : UVHWeaponCatalog::CategoryOf(CurrentWeapon));
		FTransform Pose = UVHCharacterVisuals::HoldPose(Rig, CurrentWeapon, Category).Weapon;
		Pose.AddToTranslation(FVector(0.f, 0.f, FeetZ));
		FTransform Base = (Weapon ? Weapon->GripOffset : FTransform::Identity) * Pose;
		Base.SetScale3D(FVector::OneVector);
		WeaponMesh->SetRelativeTransform(MeshInner * Base);
	}
}

bool UVHWeaponVisualComponent::GetHoldInfo(FVHHoldInfo& Out) const
{
	if (!bOnHandSocket || !WeaponMesh || !WeaponMesh->GetStaticMesh() || CurrentWeapon == 0 || (!WeaponMesh->IsVisible() && !bHiddenByAction && !bHiddenByRoll))
	{
		return false;
	}
	const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
	const FVHWeaponVisual* Weapon = Catalog.Find(CurrentWeapon);
	const FVHCharacterVisual& Rig = UVHSettings::Get().GetCharacterVisuals().Find(CharacterId);
	const uint8 Category = static_cast<uint8>(Weapon ? Weapon->Category : UVHWeaponCatalog::CategoryOf(CurrentWeapon));
	const FVHHoldPose& Pose = UVHCharacterVisuals::HoldPose(Rig, CurrentWeapon, Category);

	// 자세 값 = 무기 손잡이(무기 피벗)의 자리. 무기 = 붙임 보정 × 소켓 이므로 소켓 = 붙임 보정⁻¹ × 무기
	FTransform Attach = (Weapon ? Weapon->GripOffset : FTransform::Identity) * Rig.SocketOffset;
	Attach.SetScale3D(FVector::OneVector);
	FTransform WeaponPose = Pose.Weapon;
	WeaponPose.SetScale3D(FVector::OneVector);

	// 무기 사용 동작: 공격(키 프레임) > 레일건 버팀 > 꾹 누르기 순서로 든 자세에 더한다 (몽타주 재생 중이면 몽타주가 팔을 움직이므로 더하지 않는다)
	FVector Hand = FVector::ZeroVector;
	FRotator Rot = FRotator::ZeroRotator;
	float SpineYaw = 0.f, SpinePitch = 0.f;
	FVector RHand = FVector::ZeroVector;		// 반동·떨림 (섞지 않고 바로)
	FRotator RRot = FRotator::ZeroRotator;
	auto AddKey = [&](const FVHMotionKey& K, float W)
	{
		Hand += K.HandOffset * W;
		Rot += K.WeaponRotation * W;
		SpineYaw += K.SpineYaw * W;
		SpinePitch += K.SpinePitch * W;
	};
	const bool bFrozen = DebugFreezeTime >= 0.f && IsLocalPawn();
	const float Now = bFrozen ? DebugFreezeTime : (GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.f);
	auto AddHeld = [&](const FVHWeaponMotion& M, float W)
	{
		if (W <= 0.001f || M.Keys.Num() == 0)
		{
			return;
		}
		AddKey(M.Keys[0], W);
		if (M.ShakeCm > 0.f && M.ShakeHz > 0.f)
		{
			const float P = 2.f * PI * M.ShakeHz * Now;
			Hand += FVector(FMath::Sin(P), FMath::Sin(P * 1.37f + 1.f) * 0.6f, FMath::Sin(P * 0.83f + 2.f) * 0.8f) * (M.ShakeCm * W);
		}
	};
	if (IsActionPlaying() && !bActionMontage && !bActionRecoil)
	{
		if (const FVHWeaponMotion* Attack = Rig.AttackMotions.Find(static_cast<EVHWeaponId>(ActionWeapon)))
		{
			AddKey(Attack->Evaluate(GetActionTime()), 1.f);
		}
	}
	else if (!bActionMontage)
	{
		// (반동 무기는 반동이 남아 있어도 꾹 누르기·레일건 버팀 자세를 같이 더한다: 반동이 그 위에 얹힌다)
		const FVHWeaponMotion* Held = Rig.HoldMotions.Find(static_cast<EVHWeaponId>(CurrentWeapon));
		if (CurrentWeapon == static_cast<uint8>(EVHWeaponId::Railgun) && LockAlpha > 0.001f)
		{
			AddHeld(Rig.RailgunLockMotion, LockAlpha);
			if (Held)
			{
				AddHeld(*Held, HoldAlpha * (1.f - LockAlpha));
			}
		}
		else if (Held)
		{
			AddHeld(*Held, HoldAlpha);
		}
		// 쏘는 동안 잔잔한 떨림 (화염방사기, 무기 표 Recoil 의 Sustain 칸)
		const FVHWeaponVisual* Row = Weapon;
		if (Row && HoldAlpha > 0.001f && (Row->Recoil.SustainShakeCm > 0.f || Row->Recoil.SustainShakeDeg > 0.f))
		{
			const float P = 2.f * PI * FMath::Max(Row->Recoil.SustainShakeHz, 0.1f) * Now;
			// 서로 다른 빠르기의 사인 3개를 섞어 규칙적이지 않게 (뒤로 밀리는 쪽이 조금 더 크다)
			const FVector N(FMath::Sin(P * 1.13f) * 0.6f + FMath::Sin(P * 2.71f + 1.3f) * 0.4f - 0.35f,
				FMath::Sin(P * 0.91f + 2.1f) * 0.5f, FMath::Sin(P * 1.57f + 0.7f) * 0.6f + FMath::Sin(P * 3.19f) * 0.3f);
			RHand += N * (Row->Recoil.SustainShakeCm * HoldAlpha);
			RRot += FRotator((FMath::Sin(P * 1.37f + 0.4f) * 0.7f + 0.3f) * Row->Recoil.SustainShakeDeg, FMath::Sin(P * 0.77f + 1.9f) * 0.5f * Row->Recoil.SustainShakeDeg, 0.f) * HoldAlpha;
			SpinePitch += -0.25f * Row->Recoil.SustainShakeDeg * HoldAlpha * (0.6f + 0.4f * FMath::Sin(P * 0.63f));
		}
	}
	// 반동: 발사마다 쌓인 것을 모두 더한다 (공격 키 프레임·버팀 자세 위에)
	// (반동은 월드 시간 기준: vh.WeaponTest at 의 게임 일시정지에서는 같이 멈춘다)
	EvaluateRecoil(GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.f, RHand, RRot, SpineYaw, SpinePitch);
	const FTransform PoseNoKick = WeaponPose;
	WeaponPose.AddToTranslation(Hand);
	WeaponPose.SetRotation((Rot.Quaternion() * WeaponPose.GetRotation()).GetNormalized());
	// 반동·떨림은 따로 (애니메이션 인스턴스가 자세 목표를 부드럽게 따라가며 섞는데, 거기에 넣으면 0.05초짜리 튐이 지워진다 → 섞은 뒤에 더한다)
	FTransform KickPose = PoseNoKick;
	KickPose.AddToTranslation(Hand + RHand);
	KickPose.SetRotation(((Rot + RRot).Quaternion() * PoseNoKick.GetRotation()).GetNormalized());
	Out.SpineBone = Rig.SpineBone;
	Out.SpineRotation = FRotator(-SpinePitch, SpineYaw, 0.f);

	Out.bProceduralArms = Rig.bProceduralArms;
	Out.SocketName = Rig.WeaponSocketName;
	Out.SocketTarget = Attach.Inverse() * WeaponPose;
	Out.RecoilDelta = Out.SocketTarget.Inverse() * (Attach.Inverse() * KickPose);
	Out.ElbowDirection = Pose.ElbowDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0, 0.0, -1.0));
	Out.bLeftHand = Pose.bLeftHandSupport;
	// 보조 손: 무기 메시에 SupportHand 소켓이 있으면 그 자리, 없으면 캐릭터 쪽 값 (2026-11-20)
	FVector SupportLocal;
	bool bSupportSocket = false;
	if (Weapon && Weapon->PointLocal(WeaponMesh->GetStaticMesh(), EVHWeaponPoint::SupportHand, SupportLocal, &bSupportSocket) && bSupportSocket)
	{
		Out.LeftHandTarget = WeaponPose.TransformPositionNoScale(MeshInner.TransformPosition(SupportLocal));
		Out.LeftHandRecoil = KickPose.TransformPositionNoScale(MeshInner.TransformPosition(SupportLocal)) - Out.LeftHandTarget;
	}
	else
	{
		Out.LeftHandTarget = WeaponPose.TransformPositionNoScale(Pose.LeftHandOnWeapon);
		Out.LeftHandRecoil = KickPose.TransformPositionNoScale(Pose.LeftHandOnWeapon) - Out.LeftHandTarget;
	}
	Out.RightUpper = Rig.RightUpperArmBone;
	Out.RightLower = Rig.RightLowerArmBone;
	Out.RightHand = Rig.RightHandBone;
	Out.LeftUpper = Rig.LeftUpperArmBone;
	Out.LeftLower = Rig.LeftLowerArmBone;
	Out.LeftHand = Rig.LeftHandBone;
	Out.LeftPalmOffset = Rig.LeftPalmOffset;
	Out.BlendSpeed = Rig.PoseBlendSpeed;
	Out.bRanged = Category == static_cast<uint8>(EVHWeaponCategory::Ranged);
	return true;
}

// ───────────────── 몸 동작 (몽타주) ─────────────────

UAnimInstance* UVHWeaponVisualComponent::GetBodyAnimInstance() const
{
	const USkeletalMeshComponent* Skel = FindTargetMesh();
	return Skel ? Skel->GetAnimInstance() : nullptr;
}

bool UVHWeaponVisualComponent::PlayBodyMontage(const FVHBodyMontage& Entry, bool bLoop, EBodyPhase Phase)
{
	UAnimMontage* Montage = Entry.Montage.LoadSynchronous();
	UAnimInstance* Anim = GetBodyAnimInstance();
	if (!Montage || !Anim)
	{
		return false;
	}
	if (BodyMontage.IsValid() && BodyMontage.Get() != Montage)
	{
		Anim->Montage_Stop(0.15f, BodyMontage.Get());
	}
	// 다른 몽타주(구르기)는 멈추지 않는다 (bStopAllMontages = false)
	const float Length = Anim->Montage_Play(Montage, FMath::Max(Entry.PlayRate, 0.1f), EMontagePlayReturnType::MontageLength, Entry.StartTime, false);
	if (Length <= 0.f)
	{
		return false;
	}
	if (bLoop && Montage->CompositeSections.Num() > 0)
	{
		const FName Section = Montage->CompositeSections[0].SectionName;
		Anim->Montage_SetNextSection(Section, Section, Montage);	// 같은 구간을 되풀이 (Montage_Stop 까지)
	}
	BodyMontage = Montage;
	BodyEndPosition = Entry.Duration > 0.f ? Entry.StartTime + Entry.Duration : -1.f;
	bBodyUpper = Entry.bUpperBodyOnly;
	bBodyRelease = Entry.bReleaseArms;
	BodyPhase = Phase;
	UE_LOG(LogVHWeaponVisual, Log, TEXT("[몸 동작] %s %s 재생 (%s%s)"), *GetNameSafe(GetOwner()), *Montage->GetName(),
		bBodyUpper ? TEXT("상체") : TEXT("온몸"), bLoop ? TEXT(", 반복") : TEXT(""));
	UpdateTickRate();
	return true;
}

void UVHWeaponVisualComponent::StopBodyMontage(float BlendOut)
{
	if (BodyMontage.IsValid())
	{
		if (UAnimInstance* Anim = GetBodyAnimInstance())
		{
			Anim->Montage_Stop(BlendOut, BodyMontage.Get());	// 섞여 사라지는 동안은 GetBodyMontageFlags 가 계속 값을 준다
		}
	}
	BodyPhase = EBodyPhase::None;
	UpdateTickRate();
}

void UVHWeaponVisualComponent::PlayBodyAction(EVHBodyAction Action)
{
	if (IsDownPlaying() || IsRolling() || bBodyStunned || bBodyInteracting)
	{
		return;
	}
	const FVHCharacterVisual& Rig = UVHSettings::Get().GetCharacterVisuals().Find(CharacterId);
	const FVHBodyMontage* Entry = nullptr;
	switch (Action)
	{
	case EVHBodyAction::HitReact:	Entry = &Rig.HitReactMontage; break;
	case EVHBodyAction::PickUp:		Entry = &Rig.PickUpMontage; break;
	case EVHBodyAction::Drink:		Entry = &Rig.DrinkMontage; break;
	case EVHBodyAction::Throw:		Entry = &Rig.ThrowMontage; break;
	}
	if (Entry)
	{
		PlayBodyMontage(*Entry, false, EBodyPhase::OneShot);
	}
}

void UVHWeaponVisualComponent::SetBodyStates(bool bInteracting, bool bStunned)
{
	const FVHCharacterVisual& Rig = UVHSettings::Get().GetCharacterVisuals().Find(CharacterId);
	if (bStunned != bBodyStunned)
	{
		bBodyStunned = bStunned;
		if (bStunned && !IsDownPlaying())
		{
			UVHSoundConfig::Play(this, EVHSound::ElectricShock, GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);
			StopRoll();
			PlayBodyMontage(Rig.StunnedMontage, true, EBodyPhase::Stunned);
		}
		else if (!bStunned && BodyPhase == EBodyPhase::Stunned)
		{
			StopBodyMontage(0.25f);
		}
	}
	if (bInteracting != bBodyInteracting)
	{
		bBodyInteracting = bInteracting;
		if (bInteracting && !bBodyStunned && !IsDownPlaying())
		{
			if (!PlayBodyMontage(Rig.InteractStartMontage, false, EBodyPhase::InteractStart))
			{
				PlayBodyMontage(Rig.InteractLoopMontage, true, EBodyPhase::InteractLoop);
			}
		}
		else if (!bInteracting && (BodyPhase == EBodyPhase::InteractStart || BodyPhase == EBodyPhase::InteractLoop))
		{
			StopBodyMontage(0.3f);
		}
	}
}

void UVHWeaponVisualComponent::UpdateBodyMontage()
{
	if (BodyPhase == EBodyPhase::None)
	{
		return;
	}
	UAnimInstance* Anim = GetBodyAnimInstance();
	UAnimMontage* Montage = BodyMontage.Get();
	// 작업 시작(무릎 꿇기)이 끝나 가면(또는 끝에서 저절로 섞여 나가기 시작하면) 살피기 반복으로 넘어간다
	if (BodyPhase == EBodyPhase::InteractStart && Anim && Montage)
	{
		const FVHCharacterVisual& Rig = UVHSettings::Get().GetCharacterVisuals().Find(CharacterId);
		const float Rate = FMath::Max(Rig.InteractStartMontage.PlayRate, 0.1f);
		const float Left = (Montage->GetPlayLength() - Anim->Montage_GetPosition(Montage)) / Rate;
		if (!Anim->Montage_IsPlaying(Montage) || Left <= Montage->BlendOut.GetBlendTime() + 0.05f)
		{
			PlayBodyMontage(Rig.InteractLoopMontage, true, EBodyPhase::InteractLoop);
			return;
		}
	}
	// 금고·상자 작업 중: 다이얼 딸깍 소리 (0.45초마다)
	if ((BodyPhase == EBodyPhase::InteractStart || BodyPhase == EBodyPhase::InteractLoop) && GetWorld() && GetOwner())
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now >= NextDialSound)
		{
			NextDialSound = Now + 0.45;
			UVHSoundConfig::Play(this, EVHSound::SafeDial, GetOwner()->GetActorLocation());
		}
	}
	if (!Anim || !Montage || !Anim->Montage_IsPlaying(Montage))
	{
		BodyPhase = EBodyPhase::None;
		UpdateTickRate();
		return;
	}
	// 정한 구간(StartTime + Duration)이 끝나 가면 섞으며 멈춘다
	if (BodyPhase == EBodyPhase::OneShot && BodyEndPosition > 0.f)
	{
		const float Rate = FMath::Max(Anim->Montage_GetPlayRate(Montage), 0.1f);
		if (Anim->Montage_GetPosition(Montage) >= BodyEndPosition - Montage->BlendOut.GetBlendTime() * Rate)
		{
			StopBodyMontage(Montage->BlendOut.GetBlendTime());
			return;
		}
	}
}

void UVHWeaponVisualComponent::GetBodyMontageFlags(bool& bOutUpperBody, bool& bOutReleaseArms) const
{
	bOutUpperBody = false;
	bOutReleaseArms = false;
	const UAnimInstance* Anim = GetBodyAnimInstance();
	if (Anim && BodyMontage.IsValid() && Anim->Montage_IsActive(BodyMontage.Get()))
	{
		bOutUpperBody = bBodyUpper;
		bOutReleaseArms = bBodyRelease;
	}
}

FString UVHWeaponVisualComponent::GetBodyPhaseName() const
{
	static const TCHAR* Names[] = { TEXT("없음"), TEXT("한 번"), TEXT("작업 시작"), TEXT("작업 반복"), TEXT("경직") };
	return FString::Printf(TEXT("%s %s"), Names[static_cast<int32>(BodyPhase)], *GetNameSafe(BodyMontage.Get()));
}

float UVHWeaponVisualComponent::PlayDown()
{
	const FVHCharacterVisual& Rig = UVHSettings::Get().GetCharacterVisuals().Find(CharacterId);
	DownAnim = Rig.DownAnim.LoadSynchronous();
	if (BodyPhase != EBodyPhase::None)
	{
		StopBodyMontage(0.1f);
	}
	if (!DownAnim || !GetWorld())
	{
		DownStart = -1.0;
		return 0.f;
	}
	StopRoll();
	EndAction();
	UE_LOG(LogTemp, Log, TEXT("[쓰러짐 동작] %s 재생 (%s)"), *GetNameSafe(GetOwner()), *DownAnim->GetName());
	DownRate = FMath::Max(Rig.DownPlayRate, 0.1f);
	DownStart = GetWorld()->GetTimeSeconds();
	SyncVisibility();
	return Rig.DownShowSeconds;
}

void UVHWeaponVisualComponent::StopDown()
{
	if (DownStart >= 0.0)
	{
		DownStart = -1.0;
		SyncVisibility();
	}
}

bool UVHWeaponVisualComponent::GetDownState(UAnimSequence*& OutAnim, float& OutTime) const
{
	if (DownStart < 0.0 || !DownAnim || !GetWorld())
	{
		return false;
	}
	OutAnim = DownAnim;
	OutTime = static_cast<float>(GetWorld()->GetTimeSeconds() - DownStart) * DownRate;
	return true;
}

void UVHWeaponVisualComponent::SetPlayerColorIndex(int32 Index)
{
	PlayerColorIndex = Index;
	const UVHCharacterVisuals& Visuals = UVHSettings::Get().GetCharacterVisuals();
	if (Index < 0 || !Visuals.bShowPlayerRing)
	{
		if (RingMesh)
		{
			RingMesh->SetVisibility(false);
		}
		return;
	}
	USkeletalMeshComponent* Skel = FindTargetMesh();
	AActor* Owner = GetOwner();
	if (!Skel || !Owner)
	{
		return;
	}
	if (!RingMesh)
	{
		RingMesh = NewObject<UStaticMeshComponent>(Owner, TEXT("PlayerRing"));
		RingMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RingMesh->SetCastShadow(false);
		RingMesh->SetReceivesDecals(false);
		RingMesh->RegisterComponent();
	}
	// 발 = 메시 컴포넌트 원점. 회전·애니메이션(구르기 몸 회전은 뼈)을 따라가지 않게 루트에 붙이고 발 높이만 맞춘다
	USceneComponent* Root = Owner->GetRootComponent();
	const float FeetZ = (Skel->GetAttachParent() == Root) ? Skel->GetRelativeLocation().Z : 0.f;
	RingMesh->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
	RingMesh->SetStaticMesh(Visuals.PlayerRingMesh.LoadSynchronous());
	const float S = Visuals.PlayerRingDiameter / 100.f;		// 엔진 기본 판(Plane) = 100cm
	RingMesh->SetRelativeLocation(FVector(0.f, 0.f, FeetZ + 1.5f));
	RingMesh->SetRelativeRotation(FRotator::ZeroRotator);
	RingMesh->SetRelativeScale3D(FVector(S, S, 1.f));
	if (UMaterialInterface* Mat = Visuals.PlayerRingMaterial.LoadSynchronous())
	{
		if (UMaterialInstanceDynamic* MID = RingMesh->CreateDynamicMaterialInstance(0, Mat))
		{
			MID->SetVectorParameterValue(TEXT("Color"), Visuals.PlayerColor(Index));
		}
	}
	RingMesh->SetVisibility(true);
}

void UVHWeaponVisualComponent::SetRingVisible(bool bVisible)
{
	if (RingMesh)
	{
		RingMesh->SetVisibility(bVisible && PlayerColorIndex >= 0);
	}
}

void UVHWeaponVisualComponent::SyncVisibility()
{
	if (!WeaponMesh)
	{
		return;
	}
	// 캐릭터가 숨으면(쓰러짐·은신·연막) 무기도 숨는다 → 캐릭터 쪽 코드를 고칠 필요가 없다
	const AActor* Owner = GetOwner();
	const USkeletalMeshComponent* Skel = FindTargetMesh();
	const bool bShow = CurrentWeapon != 0 && WeaponMesh->GetStaticMesh() && Owner && !Owner->IsHidden() && (!Skel || Skel->IsVisible()) && !bHiddenByAction && !bHiddenByRoll && !IsDownPlaying();
	if (WeaponMesh->IsVisible() != bShow)
	{
		WeaponMesh->SetVisibility(bShow);
	}
}

void UVHWeaponVisualComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (IsRolling() && GetWorld() && GetWorld()->GetTimeSeconds() - RollStart >= RollDuration)
	{
		StopRoll();	// 키 프레임 마지막(1) = 선 자세이므로 끝에서 바로 끊어도 튀지 않는다
	}
	UpdateRollHide();
	UpdateBodyMontage();
	if (IsActionPlaying())
	{
		const float T = GetActionTime();
		if (!bEffectFired && T >= ActionEffectTime)
		{
			bEffectFired = true;
			// 던지기·설치: 연출 순간에 무기가 손을 떠난다 (날아가는 수류탄·바닥의 지뢰는 서버 엔티티로 그린다)
			const EVHWeaponCategory Category = UVHWeaponCatalog::CategoryOf(ActionWeapon);
			if (Category == EVHWeaponCategory::Thrown || Category == EVHWeaponCategory::Placed)
			{
				bHiddenByAction = true;
			}
			OnAnyActionEffect.Broadcast(this, ActionWeapon);
		}
		const bool bFrozen = DebugFreezeTime >= 0.f && IsLocalPawn();
		if (!bFrozen && T >= ActionDuration)
		{
			EndAction();
		}
	}
	const bool bLocal = IsLocalPawn();
	const bool bHeld = (bLocal && DebugHold >= 0) ? DebugHold > 0 : bAttackHeld;
	const bool bLock = (bLocal && DebugRailLock >= 0) ? DebugRailLock > 0 : bRailgunLock;
	const FVHCharacterVisual& Rig = UVHSettings::Get().GetCharacterVisuals().Find(CharacterId);
	const float Speed = Rig.PoseBlendSpeed;
	if (IsRailgunCharging() && CurrentWeapon != static_cast<uint8>(EVHWeaponId::Railgun))
	{
		SetRailgunCharging(false);	// 무기가 바뀌면 충전 표시도 끈다 (안전장치. 취소 판단은 UVHServerGameSubsystem)
	}
	// 레일건의 꾹 누르기 자세 = 충전 중일 때만 (2026-11-20: 서버에는 충전이 없고, 공격 신호는 충전이 끝난 뒤 잠깐만 켜져 있다)
	const bool bRailgun = CurrentWeapon == static_cast<uint8>(EVHWeaponId::Railgun);
	const bool bHoldPose = bRailgun ? (IsRailgunCharging() || (bLocal && DebugHold > 0)) : bHeld;
	HoldAlpha = FMath::FInterpTo(HoldAlpha, bHoldPose && Rig.HoldMotions.Contains(static_cast<EVHWeaponId>(CurrentWeapon)) ? 1.f : 0.f, DeltaTime, Speed);
	LockAlpha = FMath::FInterpTo(LockAlpha, bLock ? 1.f : 0.f, DeltaTime, Speed);
	// 끝난 반동 지우기 + 화면 흔들림
	if (Kicks.Num() > 0 || bCameraShaken)
	{
		const float Now = static_cast<float>(GetWorld()->GetTimeSeconds());
		const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
		Kicks.RemoveAll([&](const FRecoilKick& K)
		{
			const FVHWeaponVisual* Row = Catalog.Find(K.Weapon);
			const float Keep = Row ? FMath::Max(Row->Recoil.TotalSeconds(), Row->Recoil.CameraShakeSeconds) : 0.f;
			return Now - static_cast<float>(K.Start) > Keep + 0.05f;
		});
		UpdateCameraShake(Now);
	}
	SyncVisibility();
	UpdateTickRate();
}


#if !UE_BUILD_SHIPPING
namespace
{
	void ForEachWeaponVisual(const UWorld* World, TFunctionRef<void(UVHWeaponVisualComponent*)> Fn)
	{
		for (TObjectIterator<UVHWeaponVisualComponent> It; It; ++It)
		{
			if (It->GetWorld() == World && It->HasBegunPlay())
			{
				Fn(*It);
			}
		}
	}

	// vh.Weapon N : 내 캐릭터가 든 무기를 N번으로 보여 준다(서버 없이 모양 확인). vh.Weapon -1 = 서버 값으로
	FAutoConsoleCommandWithWorldAndArgs GVHWeaponCommand(TEXT("vh.Weapon"),
		TEXT("개발용: 내 캐릭터가 든 무기 표시를 N번(1~10)으로 고정한다. -1이면 서버 값으로 돌아간다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const int32 Id = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : -1;
			UVHWeaponVisualComponent::DebugLocalWeapon = FMath::Clamp(Id, -1, 10);
			ForEachWeaponVisual(World, [Id](UVHWeaponVisualComponent* Comp)
			{
				const APawn* Pawn = Cast<APawn>(Comp->GetOwner());
				if (Pawn && Pawn->IsLocallyControlled() && Id >= 0)
				{
					Comp->SetWeapon(static_cast<uint8>(Id));
				}
			});
			UE_LOG(LogVHWeaponVisual, Log, TEXT("[무기 표시] 내 캐릭터 무기 고정: %d"), UVHWeaponVisualComponent::DebugLocalWeapon);
		}));

	// vh.WeaponRefresh : 데이터 에셋 값을 고친 뒤 든 무기를 다시 붙인다
	FAutoConsoleCommandWithWorldAndArgs GVHWeaponRefreshCommand(TEXT("vh.WeaponRefresh"),
		TEXT("개발용: DA_VHWeaponCatalog·DA_VHCharacterVisuals 값을 고친 뒤 든 무기를 다시 붙인다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			int32 Count = 0;
			ForEachWeaponVisual(World, [&Count](UVHWeaponVisualComponent* Comp) { Comp->Refresh(); ++Count; });
			UE_LOG(LogVHWeaponVisual, Log, TEXT("[무기 표시] 다시 붙임: %d개"), Count);
		}));

	UVHWeaponVisualComponent* FindLocalVisual(UWorld* World)
	{
		UVHWeaponVisualComponent* Found = nullptr;
		ForEachWeaponVisual(World, [&Found](UVHWeaponVisualComponent* Comp)
		{
			const APawn* Pawn = Cast<APawn>(Comp->GetOwner());
			if (!Found && Pawn && Pawn->IsLocallyControlled())
			{
				Found = Comp;
			}
		});
		return Found;
	}

	// vh.WeaponAnim N [시간] : 내 캐릭터가 N번 무기를 들고 공격 동작을 재생한다(서버 없이). 시간(초)을 주면 그 순간에 멈춘다 → 화면 찍기용
	// vh.WeaponAnim off : 멈춤 해제
	FAutoConsoleCommandWithWorldAndArgs GVHWeaponAnimCommand(TEXT("vh.WeaponAnim"),
		TEXT("개발용: vh.WeaponAnim N [초] — 내 캐릭터가 N번(1~10) 무기 공격 동작을 재생한다. 초를 주면 그 순간에 멈춘다. off = 멈춤 해제."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UVHWeaponVisualComponent* Comp = FindLocalVisual(World);
			if (!Comp || Args.Num() == 0)
			{
				return;
			}
			if (Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
			{
				UVHWeaponVisualComponent::DebugFreezeTime = -1.f;
				return;
			}
			const int32 Id = FMath::Clamp(FCString::Atoi(*Args[0]), 1, 10);
			UVHWeaponVisualComponent::DebugFreezeTime = Args.Num() > 1 ? FMath::Max(0.f, FCString::Atof(*Args[1])) : -1.f;
			UVHWeaponVisualComponent::DebugLocalWeapon = Id;
			Comp->SetWeapon(static_cast<uint8>(Id));
			Comp->PlayAttack(static_cast<uint8>(Id));
			// 시험용 다른 플레이어 표시(vh.ProxyTest)도 같은 동작 (다른 플레이어 화면과 같은 코드인지 확인)
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->ActorHasTag(TEXT("VHTestProxy")))
				{
					if (UVHWeaponVisualComponent* Other = It->FindComponentByClass<UVHWeaponVisualComponent>())
					{
						Other->SetWeapon(static_cast<uint8>(Id));
						Other->PlayAttack(static_cast<uint8>(Id));
					}
				}
			}
			UE_LOG(LogVHWeaponVisual, Log, TEXT("[무기 동작] 무기 %d 공격 동작 재생 (멈춤 %.2f초)"), Id, UVHWeaponVisualComponent::DebugFreezeTime);
		}));

	// vh.WeaponHold 1/0/-1 : 내 캐릭터의 꾹 누르기 자세(화염방사기 겨누기·레일건 충전)를 강제로 켜고 끈다. -1 = 서버 값
	FAutoConsoleCommandWithWorldAndArgs GVHWeaponHoldCommand(TEXT("vh.WeaponHold"),
		TEXT("개발용: vh.WeaponHold 1/0/-1 — 꾹 누르기 자세를 강제로 켜고 끈다 (-1 = 서버 값)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UVHWeaponVisualComponent::DebugHold = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), -1, 1) : -1;
			if (UVHWeaponVisualComponent* Comp = FindLocalVisual(World))
			{
				Comp->SetAttackHeld(false);
			}
		}));

	// vh.RailLock 1/0/-1 : 내 캐릭터의 레일건 발사 잠금(버팀) 자세를 강제로 켜고 끈다
	FAutoConsoleCommandWithWorldAndArgs GVHRailLockCommand(TEXT("vh.RailLock"),
		TEXT("개발용: vh.RailLock 1/0/-1 — 레일건 버팀 자세를 강제로 켜고 끈다 (-1 = 서버 값)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UVHWeaponVisualComponent::DebugRailLock = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), -1, 1) : -1;
			if (UVHWeaponVisualComponent* Comp = FindLocalVisual(World))
			{
				Comp->SetRailgunLock(false);
			}
		}));

	// vh.Aim 각도 / vh.Aim off : 내 캐릭터의 조준 방향(월드 Yaw, 0 = 화면 위)을 고정한다 → 화면 비교용 (마우스 위치와 관계없이)
	FAutoConsoleCommandWithWorldAndArgs GVHAimCommand(TEXT("vh.Aim"),
		TEXT("개발용: vh.Aim 각도 — 내 캐릭터의 조준 방향을 고정한다(0 = 화면 위, 90 = 오른쪽). off = 풀기."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UVHWeaponVisualComponent* Comp = FindLocalVisual(World);
			UVHAimComponent* Aim = Comp && Comp->GetOwner() ? Comp->GetOwner()->FindComponentByClass<UVHAimComponent>() : nullptr;
			if (!Aim || Args.Num() == 0)
			{
				return;
			}
			if (Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
			{
				Aim->SetTurnLocked(false);
				return;
			}
			Aim->SetTurnLocked(false);
			Aim->SnapLookYaw(FCString::Atof(*Args[0]));
			Aim->SetTurnLocked(true);
		}));

	// vh.Cam 거리 피치 요 / vh.Cam off : 내 캐릭터 카메라를 잠깐 옮긴다 (가까이 찍기·비교 이미지용). 요 = 월드 기준
	FAutoConsoleCommandWithWorldAndArgs GVHCamCommand(TEXT("vh.Cam"),
		TEXT("개발용: vh.Cam 거리 피치 요 [높이] — 카메라를 옮긴다(예: vh.Cam 260 -15 180). off = 원래 탑뷰."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UVHWeaponVisualComponent* Comp = FindLocalVisual(World);
			AActor* Owner = Comp ? Comp->GetOwner() : nullptr;
			USpringArmComponent* Arm = Owner ? Owner->FindComponentByClass<USpringArmComponent>() : nullptr;
			if (!Arm || Args.Num() == 0)
			{
				return;
			}
			if (Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
			{
				if (UVHAimComponent* Aim = Owner->FindComponentByClass<UVHAimComponent>())
				{
					Aim->ApplyTopDownCamera();
				}
				return;
			}
			Arm->TargetArmLength = FCString::Atof(*Args[0]);
			Arm->SetWorldRotation(FRotator(Args.Num() > 1 ? FCString::Atof(*Args[1]) : -60.f, Args.Num() > 2 ? FCString::Atof(*Args[2]) : 0.f, 0.f));
			Arm->TargetOffset = FVector(0.f, 0.f, Args.Num() > 3 ? FCString::Atof(*Args[3]) : 0.f);
			Arm->bEnableCameraLag = false;
		}));

	// vh.Ring N : 내 캐릭터 발밑 링 색 (서버 없이 확인용, -1 = 끔)
	FAutoConsoleCommandWithWorldAndArgs GVHRingCommand(TEXT("vh.Ring"),
		TEXT("개발용: vh.Ring N — 내 캐릭터 발밑 링을 N번째 플레이어 색으로(-1 = 끔)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UVHWeaponVisualComponent* Comp = FindLocalVisual(World))
			{
				Comp->SetPlayerColorIndex(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0);
			}
		}));

	// vh.Down / vh.Down off : 내 캐릭터 쓰러짐 동작 재생 (숨기지 않음, 확인용)
	FAutoConsoleCommandWithWorldAndArgs GVHDownCommand(TEXT("vh.Down"),
		TEXT("개발용: vh.Down — 내 캐릭터 쓰러짐 동작을 재생한다(숨기지 않음). off = 끄기."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UVHWeaponVisualComponent* Comp = FindLocalVisual(World))
			{
				if (Args.Num() > 0 && Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
				{
					Comp->StopDown();
				}
				else
				{
					Comp->PlayDown();
				}
			}
		}));

	// vh.Char N : 내 캐릭터 모습을 캐릭터 번호 N(도적1~3)으로 바꾼다 (서버 없이 캐릭터별 모습·무기 손 위치 확인). 서버 게임이 시작되면 서버 값으로 다시 맞춰진다
	FAutoConsoleCommandWithWorldAndArgs GVHCharCommand(TEXT("vh.Char"),
		TEXT("개발용: vh.Char N — 내 캐릭터 모습을 캐릭터 N(1~3)으로. DA_VHCharacterVisuals 의 그 칸(비어 있으면 도적1)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UVHWeaponVisualComponent* Comp = FindLocalVisual(World);
			if (!Comp || Args.Num() == 0)
			{
				UE_LOG(LogTemp, Display, TEXT("[무기 표시] vh.Char: 지금 캐릭터 %d"), Comp ? Comp->GetCharacterId() : -1);
				return;
			}
			const int32 Id = FMath::Max(0, FCString::Atoi(*Args[0]));
			Comp->SetCharacterId(Id);
			const FVHCharacterVisual& Visual = UVHSettings::Get().GetCharacterVisuals().Find(Id);
			UE_LOG(LogTemp, Display, TEXT("[무기 표시] vh.Char %d → 메시 %s"), Id, *Visual.Mesh.ToSoftObjectPath().GetAssetName());
		}));

	// vh.Body 동작 [0|1] : 몸 동작 몽타주 확인 (서버 없이). 내 캐릭터 + vh.ProxyTest 로 세운 다른 플레이어 표시 모두
	//   hit · pickup · drink · throw (한 번) / interact 1|0 · stun 1|0 (계속) / state (지금 상태를 로그로)
	FAutoConsoleCommandWithWorldAndArgs GVHBodyCommand(TEXT("vh.Body"),
		TEXT("개발용: vh.Body hit|pickup|drink|throw|interact 1/0|stun 1/0|state — 몸 동작 몽타주(내 캐릭터 + 시험용 다른 플레이어)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World || Args.Num() == 0)
			{
				return;
			}
			TArray<UVHWeaponVisualComponent*> Targets;
			if (UVHWeaponVisualComponent* Local = FindLocalVisual(World))
			{
				Targets.Add(Local);
			}
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->ActorHasTag(TEXT("VHTestProxy")))
				{
					if (UVHWeaponVisualComponent* V = It->FindComponentByClass<UVHWeaponVisualComponent>())
					{
						Targets.Add(V);
					}
				}
			}
			const FString& What = Args[0];
			const bool bOn = Args.Num() < 2 || FCString::Atoi(*Args[1]) != 0;
			for (UVHWeaponVisualComponent* V : Targets)
			{
				if (What.Equals(TEXT("hit"), ESearchCase::IgnoreCase)) { V->PlayBodyAction(EVHBodyAction::HitReact); }
				else if (What.Equals(TEXT("pickup"), ESearchCase::IgnoreCase)) { V->PlayBodyAction(EVHBodyAction::PickUp); }
				else if (What.Equals(TEXT("drink"), ESearchCase::IgnoreCase)) { V->PlayBodyAction(EVHBodyAction::Drink); }
				else if (What.Equals(TEXT("throw"), ESearchCase::IgnoreCase)) { V->PlayBodyAction(EVHBodyAction::Throw); }
				else if (What.Equals(TEXT("interact"), ESearchCase::IgnoreCase)) { V->SetBodyStates(bOn, false); }
				else if (What.Equals(TEXT("stun"), ESearchCase::IgnoreCase)) { V->SetBodyStates(false, bOn); }
				UE_LOG(LogVHWeaponVisual, Log, TEXT("[몸 동작] vh.Body %s → %s: %s"), *What, *GetNameSafe(V->GetOwner()), *V->GetBodyPhaseName());
			}
		}));

	// vh.RollFreeze U / off : 내 캐릭터의 구르기 동작을 비율 U(0~1)에 멈춰 보여 준다 (자세 확인용)
	FAutoConsoleCommandWithWorldAndArgs GVHRollFreezeCommand(TEXT("vh.RollFreeze"),
		TEXT("개발용: vh.RollFreeze U — 구르기 자세를 비율 U(0~1)에 멈춰 보여 준다. off = 풀기."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UVHWeaponVisualComponent::DebugRollFreeze = (Args.Num() == 0 || Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase)) ? -1.f : FCString::Atof(*Args[0]);
			if (UVHWeaponVisualComponent* Comp = FindLocalVisual(World))
			{
				Comp->SetRollDirection(FVector(1.f, 0.f, 0.f));
			}
		}));

	// vh.RollTest X Y [달리기 1] [촬영 1] [이름] : 내 캐릭터가 (X, Y) 방향으로 1초 걷다가(달리다가) 실제 구르기를 한다.
	// 촬영 1 이면 구르는 동안 slomo 0.05 로 늦추고 진행 0·20·40·60·80·100% 에서 HighResShot 2 (Saved/Screenshots), 로그 [구르기 촬영]
	FAutoConsoleCommandWithWorldAndArgs GVHRollTestCommand(TEXT("vh.RollTest"),
		TEXT("개발용: vh.RollTest X Y [1=달리기] [1=촬영] [이름] — 그 방향으로 1초 이동하다 구른다. 촬영이면 슬로모션으로 6장."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UVHWeaponVisualComponent* Comp = FindLocalVisual(World);
			APawn* Pawn = Comp ? Cast<APawn>(Comp->GetOwner()) : nullptr;
			UVHActionComponent* Action = Pawn ? Pawn->FindComponentByClass<UVHActionComponent>() : nullptr;
			if (!Action || Args.Num() < 2)
			{
				return;
			}
			FVector Dir = FVector(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), 0.f).GetSafeNormal();
			const bool bRun = Args.Num() > 2 && FCString::Atoi(*Args[2]) != 0;
			const bool bShots = Args.Num() > 3 && FCString::Atoi(*Args[3]) != 0;
			const FString Tag = Args.Num() > 4 ? Args[4] : TEXT("roll");
			if (Dir.IsNearlyZero())
			{
				// 0 0 = 앞뒤로 가장 길게 트인 방향(8방향)을 고르고 뒤쪽 벽 가까이로 물러나 출발 → 걷기/달리기 1초 + 구르기 4.5m 가 벽에 막히지 않게
				float Free = 0.f;
				Dir = UVHWeaponVisualComponent::PrepareOpenRun(Pawn, Free);
				UE_LOG(LogVHWeaponVisual, Log, TEXT("[구르기 촬영] %s: 트인 방향 (%.2f, %.2f) 길이 %.0fcm"), *Tag, Dir.X, Dir.Y, Free);
			}
			const double Start = World->GetTimeSeconds();
			TWeakObjectPtr<APawn> WeakPawn(Pawn);
			TWeakObjectPtr<UWorld> WeakWorld(World);
			TWeakObjectPtr<UVHWeaponVisualComponent> WeakComp(Comp);
			struct FState { bool bRolled = false; int32 Shots = 0; double EndAt = -1.0; bool bRestore = false; int32 SavedBlur = 4; };
			TSharedRef<FState> S = MakeShared<FState>();
			S->bRestore = bShots;
			if (IConsoleVariable* Blur = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality")); Blur && bShots)
			{
				S->SavedBlur = Blur->GetInt();
				Blur->Set(0, ECVF_SetByConsole);	// 늦춘 시간·화각 변경 때 번지지 않게
			}
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([=](float) -> bool
			{
				APawn* P = WeakPawn.Get();
				UWorld* W = WeakWorld.Get();
				UVHWeaponVisualComponent* V = WeakComp.Get();
				UVHActionComponent* A = P ? P->FindComponentByClass<UVHActionComponent>() : nullptr;
				if (!P || !W || !V || !A)
				{
					return false;
				}
				const double T = W->GetTimeSeconds() - Start;
				A->SetWantsSprint(bRun);
				P->AddMovementInput(Dir, 1.f);
				// 걷기는 0.5초(걷기 속도 3m/s 에 금방 닿는다), 달리기는 1초(6m/s 까지 가속) 뒤 구른다
				if (!S->bRolled && T >= (bRun ? 1.0 : 0.5))
				{
					S->bRolled = true;
					if (bShots && W->GetWorldSettings())
					{
						W->GetWorldSettings()->SetTimeDilation(0.05f);	// slomo 는 패키징·-game 에서 치트라 직접 늦춘다
					}
					// 촬영 중에는 화각을 좁혀(카메라 위치 그대로 확대) 캐릭터를 크게 찍는다
					APlayerController* ShotPC = Cast<APlayerController>(P->GetController());
					if (bShots && ShotPC && ShotPC->PlayerCameraManager)
					{
						ShotPC->PlayerCameraManager->SetFOV(35.f);
					}
					const FVector From = P->GetActorLocation();
					const bool bOk = A->RequestRoll();
					UE_LOG(LogVHWeaponVisual, Log, TEXT("[구르기 촬영] %s: %s 중 구르기 %s, 속도 %.0f cm/s, 위치 (%.0f, %.0f)"), *Tag, bRun ? TEXT("달리기") : TEXT("걷기"),
						bOk ? TEXT("시작") : TEXT("실패"), P->GetVelocity().Size2D(), From.X, From.Y);
					return bOk;
				}
				if (S->bRolled && bShots && S->Shots < 6)
				{
					float U = 0.f; FVector D; bool bM = false;
					const bool bRolling = V->GetRollState(U, D, bM);
					const float Want = S->Shots / 5.f;
					if ((bRolling && U >= Want) || !bRolling)
					{
						FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("V3Test") / FString::Printf(TEXT("roll_%s_%d.png"), *Tag, S->Shots + 1), false, false);
						FVector2D Screen(-1.f, -1.f);
						if (APlayerController* PC = Cast<APlayerController>(P->GetController()))
						{
							PC->ProjectWorldLocationToScreen(P->GetActorLocation() - FVector(0.f, 0.f, 30.f), Screen);
						}
						UE_LOG(LogVHWeaponVisual, Log, TEXT("[구르기 촬영] %s %d장: 진행 %.2f (%s), 위치 (%.0f, %.0f), 화면 (%.0f, %.0f)"), *Tag, S->Shots + 1, bRolling ? U : 1.f,
							bRolling ? TEXT("구르는 중") : TEXT("끝남"), P->GetActorLocation().X, P->GetActorLocation().Y, Screen.X, Screen.Y);
						++S->Shots;
					}
				}
				if (S->bRolled && !V->IsRolling() && (!bShots || S->Shots >= 6))
				{
					if (S->EndAt < 0.0)
					{
						S->EndAt = T;
					}
					// 마지막 화면 저장이 끝난 뒤(늦춘 시간으로 0.02초 = 실제 약 0.4초) 화각·시간을 되돌린다
					if (S->bRestore && T - S->EndAt > 0.02)
					{
						S->bRestore = false;
						S->EndAt = T;
						if (W->GetWorldSettings())
						{
							W->GetWorldSettings()->SetTimeDilation(1.f);
						}
						if (APlayerController* PC = Cast<APlayerController>(P->GetController()); PC && PC->PlayerCameraManager)
						{
							PC->PlayerCameraManager->UnlockFOV();
						}
						if (IConsoleVariable* Blur = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality")))
						{
							Blur->Set(S->SavedBlur, ECVF_SetByConsole);
						}
					}
					if (!S->bRestore && T - S->EndAt > 0.4)
					{
						A->SetWantsSprint(false);
						return false;
					}
				}
				return true;
			}));
		}));

	// vh.Walk X Y 초 [달리기 1] : 내 캐릭터를 월드 방향(X = 화면 위, Y = 오른쪽)으로 정해진 시간 동안 걷게(달리게) 한다 → 걷기·뛰기 중 무기 확인용
	FAutoConsoleCommandWithWorldAndArgs GVHWalkCommand(TEXT("vh.Walk"),
		TEXT("개발용: vh.Walk X Y 초 [1=달리기] — 내 캐릭터를 그 방향으로 정해진 시간 동안 움직인다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UVHWeaponVisualComponent* Comp = FindLocalVisual(World);
			APawn* Pawn = Comp ? Cast<APawn>(Comp->GetOwner()) : nullptr;
			if (!Pawn || Args.Num() < 3)
			{
				return;
			}
			const FVector Dir = FVector(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), 0.f).GetSafeNormal();
			const double EndTime = World->GetTimeSeconds() + FCString::Atof(*Args[2]);
			const bool bRun = Args.Num() > 3 && FCString::Atoi(*Args[3]) != 0;
			TWeakObjectPtr<APawn> WeakPawn(Pawn);
			TWeakObjectPtr<UWorld> WeakWorld(World);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakPawn, WeakWorld, Dir, EndTime, bRun](float) -> bool
			{
				APawn* P = WeakPawn.Get();
				UWorld* W = WeakWorld.Get();
				UVHActionComponent* Action = P ? P->FindComponentByClass<UVHActionComponent>() : nullptr;
				if (!P || !W || W->GetTimeSeconds() >= EndTime)
				{
					if (Action) { Action->SetWantsSprint(false); }
					return false;
				}
				if (Action) { Action->SetWantsSprint(bRun); }
				P->AddMovementInput(Dir, 1.f);
				return true;
			}));
		}));
}
#endif

