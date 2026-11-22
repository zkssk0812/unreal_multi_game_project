#include "Gameplay/VHHealthComponent.h"

#include "Gameplay/VHGameplayConfig.h"
#include "Network/VHSettings.h"

UVHHealthComponent::UVHHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UVHHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bUseConfigValues)
	{
		MaxHP = UVHSettings::Get().GetGameplayConfig().ThiefStats.MaxHP;
	}
	MaxHP = FMath::Max(1.f, MaxHP);
	HP = MaxHP;
	Shield = 0.f;
}

void UVHHealthComponent::SetHP(float Value)
{
	HP = FMath::Clamp(Value, 0.f, MaxHP);
	OnHealthChanged.Broadcast(this);
}

void UVHHealthComponent::SetShield(float Value)
{
	Shield = FMath::Max(0.f, Value);
	OnHealthChanged.Broadcast(this);
}
