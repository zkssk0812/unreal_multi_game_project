#include "Gameplay/VHInteractionConfig.h"

#define LOCTEXT_NAMESPACE "VHInteract"

UVHInteractionConfig::UVHInteractionConfig()
{
	HighlightMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/VaultHeist/Materials/M_VHHighlight.M_VHHighlight")));

	SafeOpenText = LOCTEXT("SafeOpen", "금고 열기");
	TreasurePickupText = LOCTEXT("TreasurePickup", "보물 줍기");
}

#undef LOCTEXT_NAMESPACE
