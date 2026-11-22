#include "Network/VHSettings.h"
#include "UI/VHUIStyle.h"
#include "Game/VHGameData.h"
#include "Gameplay/VHInteractionConfig.h"
#include "Gameplay/VHGameplayConfig.h"
#include "Gameplay/VHWeaponCatalog.h"
#include "Gameplay/VHSoundConfig.h"
#include "Gameplay/VHCharacterVisuals.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

UVHSettings::UVHSettings()
{
	MenuLevel = FSoftObjectPath(TEXT("/Game/VaultHeist/Maps/L_MainMenu.L_MainMenu"));
	// 게임 레벨: 이전 임시 Lvl_ThirdPerson 대신 상호작용·규칙이 들어간 테스트 레벨 [2026-10-25]
	GameLevel = FSoftObjectPath(TEXT("/Game/VaultHeist/Maps/Mansion/L_Mansion_1F.L_Mansion_1F"));
}

FSoftObjectPath UVHSettings::GetGameLevel() const
{
	// 예: -VHLevel=/Game/VaultHeist/Maps/Mansion/L_Mansion_Shell
	FString Override;
	if (FParse::Value(FCommandLine::Get(), TEXT("-VHLevel="), Override) && !Override.IsEmpty())
	{
		if (!Override.Contains(TEXT(".")))
		{
			Override += TEXT(".") + FPaths::GetBaseFilename(Override);	// /Game/…/L_X → /Game/…/L_X.L_X
		}
		return FSoftObjectPath(Override);
	}
	return GameLevel;
}

uint32 UVHSettings::GetGameLevelMapVersion() const
{
	int32 Override = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("-VHMapVersion="), Override))
	{
		return static_cast<uint32>(FMath::Max(0, Override));
	}
	return static_cast<uint32>(FMath::Max(0, GameLevelMapVersion));
}

bool UVHSettings::IsShellMap() const
{
	return GetGameLevel().GetAssetName().Equals(TEXT("L_Mansion_Shell"), ESearchCase::IgnoreCase);
}

const UVHUIStyle& UVHSettings::GetStyle() const
{
	if (const UVHUIStyle* Style = UIStyle.LoadSynchronous())
	{
		return *Style;
	}
	return *GetDefault<UVHUIStyle>();
}

const UVHGameData& UVHSettings::GetGameData() const
{
	if (const UVHGameData* Data = GameData.LoadSynchronous())
	{
		return *Data;
	}
	return *GetDefault<UVHGameData>();
}

const UVHInteractionConfig& UVHSettings::GetInteractionConfig() const
{
	if (const UVHInteractionConfig* Config = InteractionConfig.LoadSynchronous())
	{
		return *Config;
	}
	return *GetDefault<UVHInteractionConfig>();
}

const UVHWeaponCatalog& UVHSettings::GetWeaponCatalog() const
{
	if (const UVHWeaponCatalog* Catalog = WeaponCatalog.LoadSynchronous())
	{
		return *Catalog;
	}
	return *GetDefault<UVHWeaponCatalog>();
}

const UVHSoundConfig& UVHSettings::GetSoundConfig() const
{
	if (const UVHSoundConfig* Config = SoundConfig.LoadSynchronous())
	{
		return *Config;
	}
	return *GetDefault<UVHSoundConfig>();
}

const UVHCharacterVisuals& UVHSettings::GetCharacterVisuals() const
{
	if (const UVHCharacterVisuals* Visuals = CharacterVisuals.LoadSynchronous())
	{
		return *Visuals;
	}
	return *GetDefault<UVHCharacterVisuals>();
}

const UVHGameplayConfig& UVHSettings::GetGameplayConfig() const
{
	if (const UVHGameplayConfig* Config = GameplayConfig.LoadSynchronous())
	{
		return *Config;
	}
	return *GetDefault<UVHGameplayConfig>();
}
