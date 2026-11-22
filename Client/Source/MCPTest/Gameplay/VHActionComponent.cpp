#include "Gameplay/VHActionComponent.h"
#include "Gameplay/VHInteractableBase.h"

#include "Gameplay/VHGameplayConfig.h"
#include "Gameplay/VHInteractionComponent.h"
#include "Gameplay/VHInteractionRules.h"
#include "Gameplay/VHInteractionSubsystem.h"
#include "Gameplay/VHStatusEffectComponent.h"
#include "Gameplay/VHThiefStateComponent.h"
#include "Gameplay/VHAimComponent.h"
#include "Gameplay/VHInteractionAuthority.h"
#include "Gameplay/VHInteractionConfig.h"
#include "Gameplay/VHWeaponVisualComponent.h"
#include "Network/VHSettings.h"
#include "MCPTest.h"

#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"

#define LOCTEXT_NAMESPACE "VHAction"

namespace
{
	// 개발용 로그: 구르기 시작 위치 (이동 거리 확인)
	TMap<const UVHActionComponent*, FVector> GRollStartLocations;
}

UVHActionComponent::UVHActionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;	// 이동 계산 전에 속도·구르기 입력을 정한다
}

void UVHActionComponent::BeginPlay()
{
	Super::BeginPlay();
	Stats = bUseCustomStats ? CustomStats : UVHSettings::Get().GetGameplayConfig().ThiefStats;
	Stamina = Stats.MaxStamina;

	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Movement = Character->GetCharacterMovement();
		// 점프는 없다 [팀 결정]. Space는 구르기
		Character->JumpMaxCount = 0;
		// 이 컴포넌트가 정한 속도·구르기 입력으로 이번 프레임 이동을 계산하게 한다
		Movement->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
		SavedMaxAcceleration = Movement->MaxAcceleration;
		if (Character->GetMesh())
		{
			MeshBaseScale = Character->GetMesh()->GetRelativeScale3D();
		}
	}
}

void UVHActionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bRolling)
	{
		EndRoll();
	}
	Super::EndPlay(EndPlayReason);
}

double UVHActionComponent::GetNow() const
{
	if (const UVHInteractionSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UVHInteractionSubsystem>() : nullptr)
	{
		return Sub->GetNow();
	}
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

UVHStatusEffectComponent* UVHActionComponent::GetStatus() const
{
	return GetOwner()->FindComponentByClass<UVHStatusEffectComponent>();
}

bool UVHActionComponent::IsExhausted() const
{
	return bExhausted;
}

void UVHActionComponent::SetExhausted(bool bOn)
{
	if (bExhausted == bOn)
	{
		return;
	}
	bExhausted = bOn;
	UE_LOG(LogMCPTest, Log, TEXT("[Action] %s 탈진 %s (스테미나 %.1f)"), *GetNameSafe(GetOwner()), bOn ? TEXT("시작") : TEXT("해제"), Stamina);
	if (UVHStatusEffectComponent* Status = GetStatus())
	{
		Status->SetExhausted(bOn);
	}
	if (bOn)
	{
		PostMessage(LOCTEXT("Exhausted", "탈진! 스테미나가 다 찰 때까지 달리기·구르기를 할 수 없습니다"));
	}
}

void UVHActionComponent::SpendStamina(float Amount)
{
	Stamina = FMath::Max(0.f, Stamina - Amount);
	if (Stamina <= KINDA_SMALL_NUMBER)
	{
		Stamina = 0.f;
		SetExhausted(true);	// 0pt가 되면 탈진 [기획]
	}
}

void UVHActionComponent::DebugSetStamina(float Value)
{
	Stamina = FMath::Clamp(Value, 0.f, Stats.MaxStamina);
	if (Stamina <= KINDA_SMALL_NUMBER)
	{
		SetExhausted(true);
	}
	else if (Stamina >= Stats.MaxStamina)
	{
		SetExhausted(false);
	}
}

bool UVHActionComponent::CanAct() const
{
	const UVHThiefStateComponent* State = GetOwner()->FindComponentByClass<UVHThiefStateComponent>();
	if (State && !State->IsFree())
	{
		return false;
	}
	const UVHStatusEffectComponent* Status = GetStatus();
	return !(Status && Status->IsStunned());
}

void UVHActionComponent::PostMessage(const FText& Message) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsPlayerControlled())
	{
		return;
	}
	if (const UVHInteractionComponent* Interaction = GetOwner()->FindComponentByClass<UVHInteractionComponent>())
	{
		Interaction->OnMessage.Broadcast(Message, true);
	}
}

void UVHActionComponent::SetMoveInputBlocked(bool bBlocked)
{
	if (bMoveBlocked == bBlocked)
	{
		return;
	}
	bMoveBlocked = bBlocked;
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (AController* Controller = Pawn ? Pawn->GetController() : nullptr)
	{
		Controller->SetIgnoreMoveInput(bBlocked);
	}
}

// ───────────────────────── 서버 상태에 따른 이동 효과 ─────────────────────────

void UVHActionComponent::SetMoveSpeedScale(float Scale)
{
	Scale = FMath::Clamp(Scale, 0.f, 1.f);
	if (!FMath::IsNearlyEqual(MoveSpeedScale, Scale))
	{
		UE_LOG(LogMCPTest, Log, TEXT("[Action] %s 이동 속도 배율 %.2f → %.2f (걷기 %.0f, 달리기 %.0f cm/s)"), *GetNameSafe(GetOwner()),
			MoveSpeedScale, Scale, Stats.WalkSpeed * Scale, Stats.RunSpeed * RunSpeedScale * Scale);
		MoveSpeedScale = Scale;
	}
}

void UVHActionComponent::SetRunSpeedScale(float Scale)
{
	Scale = FMath::Max(Scale, 0.f);
	if (!FMath::IsNearlyEqual(RunSpeedScale, Scale))
	{
		UE_LOG(LogMCPTest, Log, TEXT("[Action] %s 달리기 속도 배율 %.2f → %.2f (달리기 %.0f cm/s)"), *GetNameSafe(GetOwner()),
			RunSpeedScale, Scale, Stats.RunSpeed * Scale);
		RunSpeedScale = Scale;
	}
}

void UVHActionComponent::SetSpecialBlocked(bool bBlocked)
{
	bSpecialBlocked = bBlocked;
}

void UVHActionComponent::SetMoveLocked(bool bLocked)
{
	if (bFireLocked == bLocked)
	{
		return;
	}
	bFireLocked = bLocked;
	UE_LOG(LogMCPTest, Log, TEXT("[Action] %s 이동 잠금 %s"), *GetNameSafe(GetOwner()), bLocked ? TEXT("시작") : TEXT("끝"));
	if (bLocked && Movement)
	{
		Movement->StopMovementImmediately();
	}
}

void UVHActionComponent::SetJetpack(bool bOn, float Speed)
{
	if (bJetpack == bOn)
	{
		return;
	}
	bJetpack = bOn;
	JetpackSpeed = Speed;
	UE_LOG(LogMCPTest, Log, TEXT("[Action] %s 제트팩 %s (%.0f cm/s)"), *GetNameSafe(GetOwner()), bOn ? TEXT("시작") : TEXT("끝"), Speed);
	if (bOn && bRolling)
	{
		EndRoll();
	}
}

void UVHActionComponent::StartKnockback(const FVector& Direction, float Distance, float Seconds)
{
	const FVector Dir = FVector(Direction.X, Direction.Y, 0.f).GetSafeNormal();
	if (!Movement || Dir.IsNearlyZero() || Distance <= 0.f || Seconds <= 0.f)
	{
		return;
	}
	if (bRolling)
	{
		EndRoll();
	}
	KnockbackDirection = Dir;
	KnockbackSpeed = Distance / Seconds;
	KnockbackEndTime = GetNow() + Seconds;
	Movement->Velocity = Dir * KnockbackSpeed;
	UE_LOG(LogMCPTest, Log, TEXT("[Action] %s 밀려남 시작: 방향 (%.2f, %.2f), %.0fcm를 %.2f초 동안"), *GetNameSafe(GetOwner()), Dir.X, Dir.Y, Distance, Seconds);
}

bool UVHActionComponent::IsKnockedBack() const
{
	return KnockbackEndTime >= 0.0 && GetNow() < KnockbackEndTime;
}

// ───────────────────────── 구르기 ─────────────────────────

bool UVHActionComponent::RequestRoll()
{
	if (!Movement || bRolling || !CanAct())
	{
		return false;
	}
	if (bSpecialBlocked)
	{
		PostMessage(LOCTEXT("NoRollCloaked", "은신 중에는 구를 수 없습니다"));
		return false;
	}
	if (bFireLocked || bJetpack || IsKnockedBack())
	{
		return false;	// 레일건 발사·제트팩·밀려나는 중 (서버도 Locked 로 거절한다)
	}
	const UVHInteractionComponent* Interaction = GetOwner()->FindComponentByClass<UVHInteractionComponent>();
	if (Interaction && Interaction->IsInteracting())
	{
		return false;	// 상호작용 중에는 구르지 않는다(길게 누르기는 Space 입력으로 취소만 된다)
	}
	if (bExhausted || Stamina <= 0.f)
	{
		PostMessage(LOCTEXT("NoRollExhausted", "탈진 상태에서는 구를 수 없습니다"));
		return false;
	}
	if (!Movement->IsMovingOnGround())
	{
		return false;
	}

	// 방향: 지금 누르고 있는 이동 키 방향(커서 기준으로 이미 계산된 월드 방향), 없으면 커서 방향(앞) ⟪기획 변경 2026-10-26⟫
	APawn* Pawn = Cast<APawn>(GetOwner());
	FVector Dir = Pawn ? Pawn->GetLastMovementInputVector() : FVector::ZeroVector;
	Dir.Z = 0.f;
	if (Dir.SizeSquared() < 0.01f)
	{
		const UVHAimComponent* Aim = GetOwner()->FindComponentByClass<UVHAimComponent>();
		Dir = Aim ? FRotator(0.f, Aim->GetAimYaw(), 0.f).Vector() : GetOwner()->GetActorForwardVector();
		Dir.Z = 0.f;
	}
	RollDirection = Dir.GetSafeNormal();

	// 남은 스테미나보다 큰 동작도 실행 가능 → 0이 되면 탈진 [이전 근황, 확인 필요]
	const float Before = Stamina;
	SpendStamina(Stats.RollStaminaCost);
	GRollStartLocations.Add(this, GetOwner()->GetActorLocation());
	UE_LOG(LogMCPTest, Log, TEXT("[Action] %s 구르기 시작: 방향 (%.2f, %.2f), 스테미나 %.1f → %.1f"), *GetNameSafe(GetOwner()),
		RollDirection.X, RollDirection.Y, Before, Stamina);

	bRolling = true;
	RollEndTime = GetNow() + Stats.RollSeconds;
	if (UVHStatusEffectComponent* Status = GetStatus())
	{
		Status->SetInvulnerable(true);	// 구르는 중 무적 [기획]
	}
	SetMoveInputBlocked(true);			// 구르는 동안 방향을 바꾸지 않는다
	Movement->MaxAcceleration = 100000.f;	// 바로 9m/s가 되게
	Movement->Velocity = RollDirection * Stats.RollSpeed;
	// 몸은 구르는 동안에도 커서를 바라본다(내 캐릭터). 커서 기준이 아닌 캐릭터(더미)만 구르는 방향으로 돌린다
	if (!Movement->bUseControllerDesiredRotation)
	{
		GetOwner()->SetActorRotation(RollDirection.Rotation());
	}
	// 구르기 동작(앞구르기 한 바퀴): 길이 = 구르기 시간, 방향 = 실제로 굴러가는 방향. 다른 플레이어 화면에서는 ES_DODGING 으로 같은 동작
	if (UVHWeaponVisualComponent* Visual = GetOwner()->FindComponentByClass<UVHWeaponVisualComponent>())
	{
		Visual->PlayRoll(RollDirection, Stats.RollSeconds);
	}
	return true;
}

void UVHActionComponent::EndRoll()
{
	bRolling = false;
	UE_LOG(LogMCPTest, Log, TEXT("[Action] %s 구르기 끝: 이동 거리 %.0fcm"), *GetNameSafe(GetOwner()),
		FVector::Dist2D(GRollStartLocations.FindRef(this), GetOwner()->GetActorLocation()));
	if (UVHStatusEffectComponent* Status = GetStatus())
	{
		Status->SetInvulnerable(false);
	}
	SetMoveInputBlocked(false);
	if (Movement)
	{
		Movement->MaxAcceleration = SavedMaxAcceleration;
	}
	if (UVHWeaponVisualComponent* Visual = GetOwner()->FindComponentByClass<UVHWeaponVisualComponent>())
	{
		Visual->StopRoll();	// 시간이 다 돼서 끝났으면 이미 멈춰 있다. 경직·쓰러짐으로 일찍 끝나면 바로 선 자세로
	}
}

// ───────────────────────── 사망·리스폰 [PvP 규칙 v1 4장] ─────────────────────────

void UVHActionComponent::StopActions()
{
	if (bRolling)
	{
		EndRoll();
	}
	bWantsSprint = false;
	bRunning = false;
}

void UVHActionComponent::ResetForRespawn()
{
	StopActions();
	Stamina = Stats.MaxStamina;
	SetExhausted(false);
	UE_LOG(LogMCPTest, Log, TEXT("[Combat] %s 리스폰: 스테미나 %.0f"), *GetNameSafe(GetOwner()), Stamina);
}

// ───────────────────────── 매 프레임 ─────────────────────────

void UVHActionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	const double Now = GetNow();
	const bool bCanAct = CanAct();

	// ── 구르기 ──
	if (bRolling)
	{
		if (Now >= RollEndTime || !bCanAct)
		{
			EndRoll();
		}
		else if (APawn* Pawn = Cast<APawn>(GetOwner()))
		{
			Pawn->AddMovementInput(RollDirection, 1.f, true);	// 막아 둔 플레이어 입력과 상관없이 굴러간다
		}
	}

	// ── 강제 이동: 밀려남(짧게, 정해진 방향) > 제트팩(커서 방향) ── 플레이어 이동 입력은 무시한다
	const bool bPushed = KnockbackEndTime >= 0.0 && Now < KnockbackEndTime;
	if (KnockbackEndTime >= 0.0 && !bPushed)
	{
		KnockbackEndTime = -1.0;
		UE_LOG(LogMCPTest, Log, TEXT("[Action] %s 밀려남 끝"), *GetNameSafe(GetOwner()));
	}
	const bool bForced = (bPushed || bJetpack) && bCanAct;
	if (bForced)
	{
		if (APawn* Pawn = Cast<APawn>(GetOwner()))
		{
			FVector Dir = KnockbackDirection;
			if (!bPushed)
			{
				const UVHAimComponent* Aim = GetOwner()->FindComponentByClass<UVHAimComponent>();
				Dir = FRotator(0.f, Aim ? Aim->GetAimYaw() : GetOwner()->GetActorRotation().Yaw, 0.f).Vector();
			}
			Pawn->AddMovementInput(Dir, 1.f, true);
		}
	}
	if (!bRolling)
	{
		SetMoveInputBlocked(bForced);
		if (Movement && bForced != bAccelBoosted)
		{
			bAccelBoosted = bForced;
			Movement->MaxAcceleration = bForced ? 100000.f : SavedMaxAcceleration;	// 바로 그 속도가 되게
		}
	}

	// ── 달리기 ── (공격은 SP를 쓰지 않는다)
	const UVHInteractionComponent* Interaction = GetOwner()->FindComponentByClass<UVHInteractionComponent>();
	const bool bInteracting = Interaction && Interaction->IsInteracting();
	const bool bHasMoveInput = Movement && Movement->GetCurrentAcceleration().SizeSquared2D() > 1.f;
	const bool bMoving = GetOwner()->GetVelocity().Size2D() > 10.f;
	const bool bWasRunning = bRunning;
	bRunning = bWantsSprint && bHasMoveInput && bMoving && bCanAct && !bRolling && !bInteracting
		&& !bExhausted && Stamina > 0.f && !bSpecialBlocked && !bForced && !bFireLocked;
	if (bRunning)
	{
		SpendStamina(Stats.RunStaminaPerSecond * DeltaTime);
		bRunning = !bExhausted;
	}
	if (bRunning != bWasRunning)
	{
		UE_LOG(LogMCPTest, Log, TEXT("[Action] %s 달리기 %s (스테미나 %.1f, 속도 %.0fcm/s)"), *GetNameSafe(GetOwner()),
			bRunning ? TEXT("시작") : TEXT("끝"), Stamina, GetOwner()->GetVelocity().Size2D());
	}

	// ── 회복: 특수 동작을 하지 않을 때 [기획]. 탈진 중에는 50% ──
	if (!bRunning && !bRolling)
	{
		const float Rate = Stats.StaminaRegenPerSecond * (bExhausted ? C.ExhaustedRegenMultiplier : 1.f);
		Stamina = FMath::Min(Stats.MaxStamina, Stamina + Rate * DeltaTime);
		if (bExhausted && Stamina >= Stats.MaxStamina - KINDA_SMALL_NUMBER)
		{
			SetExhausted(false);	// 최대치까지 회복되면 해제 [기획]
		}
	}

	// ── 이동 속도 ──
	if (bForced)
	{
		CurrentMaxSpeed = bPushed ? KnockbackSpeed : JetpackSpeed;
	}
	else if (bRolling)
	{
		CurrentMaxSpeed = Stats.RollSpeed;
	}
	else if (bFireLocked)
	{
		CurrentMaxSpeed = 0.f;	// 레일건 발사 중: 이동 불가
	}
	else
	{
		// 끈끈이·은신 감속과 운동화 달리기 증가는 서버가 상태만 알려 주고 클라가 적용한다
		CurrentMaxSpeed = (bRunning ? Stats.RunSpeed * RunSpeedScale : Stats.WalkSpeed) * MoveSpeedScale;
	}
	if (Movement)
	{
		Movement->MaxWalkSpeed = CurrentMaxSpeed;
	}
}

#undef LOCTEXT_NAMESPACE
