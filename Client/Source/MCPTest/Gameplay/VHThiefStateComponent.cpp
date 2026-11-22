#include "Gameplay/VHThiefStateComponent.h"

#include "Gameplay/VHInteractionSubsystem.h"
#include "Gameplay/VHHealthComponent.h"

#include "Gameplay/VHWeaponVisualComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"

UVHThiefStateComponent::UVHThiefStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UVHThiefStateComponent::BeginPlay()
{
	Super::BeginPlay();
	if (DisplayName.IsEmpty())
	{
		DisplayName = GetOwner()->GetActorNameOrLabel();
	}
	EnsureCombatComponents(GetOwner());
	if (UVHInteractionSubsystem* Sub = GetWorld()->GetSubsystem<UVHInteractionSubsystem>())
	{
		Sub->RegisterThief(this);
	}
}

void UVHThiefStateComponent::EnsureCombatComponents(AActor* Owner)
{
	if (!Owner)
	{
		return;
	}
	// 의적 블루프린트(BP_VHTestThief)를 고치지 않고 HP 표시 컴포넌트를 붙인다. 블루프린트에 이미 있으면 그대로 쓴다
	if (!Owner->FindComponentByClass<UVHHealthComponent>())
	{
		UVHHealthComponent* Health = NewObject<UVHHealthComponent>(Owner, TEXT("VHHealth"));
		Health->RegisterComponent();
		Owner->AddInstanceComponent(Health);
	}
}

void UVHThiefStateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UVHInteractionSubsystem* Sub = World->GetSubsystem<UVHInteractionSubsystem>())
		{
			Sub->UnregisterThief(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UVHThiefStateComponent::ApplyStatus(EVHThiefStatus NewStatus)
{
	const EVHThiefStatus Old = Status;
	Status = NewStatus;
	ApplyPresentation();
	if (Old != NewStatus)
	{
		OnStatusChanged.Broadcast(this, Old, NewStatus);
	}
}

void UVHThiefStateComponent::ApplyPresentation()
{
	AActor* Owner = GetOwner();
	const bool bInvisible = Status == EVHThiefStatus::Dead;
	// 쓰러짐: 쓰러지는 애니메이션(DA_VHCharacterVisuals 의 Down Anim)이 있으면 잠깐 보여 준 뒤 숨긴다. 충돌·이동은 바로 끈다
	UVHWeaponVisualComponent* Visual = Owner->FindComponentByClass<UVHWeaponVisualComponent>();
	UWorld* World = GetWorld();
	if (bInvisible && !bDownPresented)
	{
		bDownPresented = true;
		const float Show = Visual ? Visual->PlayDown() : 0.f;
		if (Show > 0.f && World)
		{
			Owner->SetActorHiddenInGame(false);
			TWeakObjectPtr<UVHThiefStateComponent> Self(this);
			World->GetTimerManager().SetTimer(DownHideTimer, FTimerDelegate::CreateWeakLambda(this, [Self]()
			{
				if (Self.IsValid() && Self->Status == EVHThiefStatus::Dead)
				{
					Self->GetOwner()->SetActorHiddenInGame(true);
				}
			}), Show, false);
		}
		else
		{
			Owner->SetActorHiddenInGame(true);
		}
	}
	else if (!bInvisible)
	{
		if (bDownPresented && World)
		{
			World->GetTimerManager().ClearTimer(DownHideTimer);
		}
		bDownPresented = false;
		if (Visual)
		{
			Visual->StopDown();
		}
		Owner->SetActorHiddenInGame(false);
	}
	Owner->SetActorEnableCollision(!bInvisible);

	if (ACharacter* Character = Cast<ACharacter>(Owner))
	{
		UCharacterMovementComponent* Move = Character->GetCharacterMovement();
		if (Status == EVHThiefStatus::Free)
		{
			if (Move->MovementMode == MOVE_None)
			{
				Move->SetMovementMode(MOVE_Walking);
			}
		}
		else
		{
			Move->StopMovementImmediately();
			Move->DisableMovement();
		}
	}
}
