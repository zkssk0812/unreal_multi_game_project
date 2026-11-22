#include "Gameplay/VHStatusEffectComponent.h"

#include "Gameplay/VHInteractionSubsystem.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Materials/MaterialInstanceDynamic.h"

UVHStatusEffectComponent::UVHStatusEffectComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;	// 경직 중에만 틱
}

void UVHStatusEffectComponent::BeginPlay()
{
	Super::BeginPlay();

	// 경직 표시: 머리 위에서 도는 노란 구슬 (기본 도형)
	AActor* Owner = GetOwner();
	if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")); Sphere && Owner->GetRootComponent())
	{
		StunMarker = NewObject<UStaticMeshComponent>(Owner, TEXT("VHStunMarker"));
		StunMarker->SetStaticMesh(Sphere);
		StunMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		StunMarker->SetCastShadow(false);
		StunMarker->SetupAttachment(Owner->GetRootComponent());
		StunMarker->SetRelativeScale3D(FVector(0.16f));
		StunMarker->SetVisibility(false);
		StunMarker->RegisterComponent();
		if (UMaterialInterface* Base = StunMarker->GetMaterial(0))
		{
			if (UMaterialInstanceDynamic* MID = StunMarker->CreateDynamicMaterialInstance(0, Base))
			{
				MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.9f, 0.1f));
			}
		}
	}
}

double UVHStatusEffectComponent::GetNow() const
{
	if (const UVHInteractionSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UVHInteractionSubsystem>() : nullptr)
	{
		return Sub->GetNow();
	}
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

float UVHStatusEffectComponent::GetStunRemaining() const
{
	return bStunned ? FMath::Max(0.f, static_cast<float>(StunEndTime - GetNow())) : 0.f;
}

bool UVHStatusEffectComponent::IsTimedInvulnerable() const
{
	return InvulnerableEndTime >= 0.0 && GetNow() < InvulnerableEndTime;
}

float UVHStatusEffectComponent::GetTimedInvulnerableRemaining() const
{
	return IsTimedInvulnerable() ? FMath::Max(0.f, static_cast<float>(InvulnerableEndTime - GetNow())) : 0.f;
}

void UVHStatusEffectComponent::ApplyTimedInvulnerable(float Seconds)
{
	InvulnerableEndTime = Seconds > 0.f ? GetNow() + Seconds : -1.0;
}

void UVHStatusEffectComponent::ClearAll()
{
	if (bStunned)
	{
		bStunned = false;
		SetMoveInputBlocked(false);
		OnStunChanged.Broadcast(this, false);
	}
	InvulnerableEndTime = -1.0;
	bInvulnerable = false;
	bExhausted = false;
	if (StunMarker)
	{
		StunMarker->SetVisibility(false);
	}
	SetComponentTickEnabled(false);
}

void UVHStatusEffectComponent::SetMoveInputBlocked(bool bBlocked)
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

void UVHStatusEffectComponent::ApplyStun(float Seconds)
{
	bStunned = true;
	StunEndTime = GetNow() + FMath::Max(0.f, Seconds);
	SetMoveInputBlocked(true);
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->GetCharacterMovement()->StopMovementImmediately();
	}
	if (StunMarker)
	{
		StunMarker->SetVisibility(true);
	}
	SetComponentTickEnabled(true);
	OnStunChanged.Broadcast(this, true);
}

void UVHStatusEffectComponent::EndStun()
{
	bStunned = false;
	SetMoveInputBlocked(false);
	if (StunMarker)
	{
		StunMarker->SetVisibility(false);
	}
	SetComponentTickEnabled(false);
	OnStunChanged.Broadcast(this, false);
}

void UVHStatusEffectComponent::UpdateTimers()
{
	if (!bStunned)
	{
		SetComponentTickEnabled(false);
		return;
	}
	if (GetNow() >= StunEndTime)
	{
		EndStun();
	}
}

void UVHStatusEffectComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateTimers();
	if (!bStunned)
	{
		return;
	}
	if (StunMarker)
	{
		float Top = 110.f;
		if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			Top = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 35.f;
		}
		const float Angle = static_cast<float>(GetNow()) * 6.f;
		StunMarker->SetRelativeLocation(FVector(FMath::Cos(Angle) * 30.f, FMath::Sin(Angle) * 30.f, Top));
	}
}
