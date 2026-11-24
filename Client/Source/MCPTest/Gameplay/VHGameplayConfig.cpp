#include "Gameplay/VHGameplayConfig.h"

#define LOCTEXT_NAMESPACE "VHGameplay"

UVHGameplayConfig::UVHGameplayConfig()
{
	UnlitColorMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/VaultHeist/Materials/M_VHUnlitColor.M_VHUnlitColor")));
	OutlineMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/VaultHeist/Materials/M_VHOutline.M_VHOutline")));

	ReturnText = LOCTEXT("Return", "반납");
}

#undef LOCTEXT_NAMESPACE
