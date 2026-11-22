#include "Gameplay/VHTreasureCarrierComponent.h"


UVHTreasureCarrierComponent::UVHTreasureCarrierComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UVHTreasureCarrierComponent::ApplyServerCarried(int32 ServerValue)
{
	const int32 NewValue = FMath::Max(0, ServerValue);
	if (!ServerCarriedValue.IsSet() || ServerCarriedValue.GetValue() != NewValue)
	{
		ServerCarriedValue = NewValue;
		OnCarryChanged.Broadcast(this);
	}
}

int32 UVHTreasureCarrierComponent::GetTotalValue() const
{
	if (ServerCarriedValue.IsSet())
	{
		return ServerCarriedValue.GetValue();	// 들고 있는 가치는 서버 값(SC_MONEY)
	}
	int32 Sum = 0;
	for (const FVHItemData& Item : Items)
	{
		Sum += Item.Value;
	}
	return Sum;
}

void UVHTreasureCarrierComponent::ApplyAdd(const FVHItemData& Item)
{
	Items.Add(Item);
	OnCarryChanged.Broadcast(this);
}

TArray<FVHItemData> UVHTreasureCarrierComponent::ApplyRemoveAll()
{
	TArray<FVHItemData> Removed = MoveTemp(Items);
	Items.Reset();
	OnCarryChanged.Broadcast(this);
	return Removed;
}
