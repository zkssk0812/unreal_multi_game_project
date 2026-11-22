#include "Gameplay/VHLevelViewSettings.h"

#include "Components/BillboardComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Components/LightComponent.h"
#include "Gameplay/VHGameplayConfig.h"
#include "Network/VHSettings.h"
#include "MCPTest.h"

AVHLevelViewSettings::AVHLevelViewSettings()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
#if WITH_EDITORONLY_DATA
	if (UBillboardComponent* Icon = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Icon")))
	{
		Icon->SetupAttachment(RootComponent);
		Icon->bIsScreenSizeScaled = true;
	}
#endif
}

float AVHLevelViewSettings::GetTopViewYaw(const UWorld* World)
{
	if (World)
	{
		for (TActorIterator<AVHLevelViewSettings> It(const_cast<UWorld*>(World)); It; ++It)
		{
			return It->TopViewYaw;
		}
	}
	return 0.f;
}

void AVHLevelViewSettings::BeginPlay()
{
	Super::BeginPlay();
	const UVHGameplayConfig& G = UVHSettings::Get().GetGameplayConfig();
	if (bApplyConfigLighting && G.bOverrideMapMoonlight)
	{
		const int32 Count = SetMoonlight(GetWorld(), G.MapMoonlightIntensity);
		UE_LOG(LogMCPTest, Log, TEXT("[Lighting] 맵 달빛 %.2f lux 적용 (달빛 %d개)"), G.MapMoonlightIntensity, Count);
	}

	// 개발용(프레임 재기): 실행 옵션 -VHFill=N → 채움광(태그 VHFill)을 N개 중 1개만 켠다. 0이면 전부 끈다.
	// 남긴 것은 N배 밝게 해서 전체 밝기를 비슷하게 맞춘다. 레벨에 굳힐 때는 Tools/MansionBuild/setup_shell_level.py lights fill=N
	int32 FillEvery = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("-VHFill="), FillEvery) && FillEvery >= 0)
	{
		int32 Index = 0, Kept = 0;
		for (TActorIterator<APointLight> It(GetWorld()); It; ++It)
		{
			ULightComponent* Light = It->GetLightComponent();
			if (!Light || !It->ActorHasTag(TEXT("VHFill")) || !Light->IsVisible())
			{
				continue;
			}
			const bool bKeep = FillEvery > 0 && (Index % FillEvery) == 0;
			++Index;
			if (bKeep)
			{
				Light->SetIntensity(Light->Intensity * FillEvery);
				++Kept;
			}
			else
			{
				Light->SetVisibility(false);
			}
		}
		UE_LOG(LogMCPTest, Log, TEXT("[Lighting] -VHFill=%d: 채움광 %d개 중 %d개 켬"), FillEvery, Index, Kept);
	}
}

int32 AVHLevelViewSettings::SetMoonlight(UWorld* World, float Lux)
{
	int32 Count = 0;
	if (World)
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			if (ULightComponent* Light = It->GetLightComponent())
			{
				Light->SetIntensity(FMath::Max(0.f, Lux));
				++Count;
			}
		}
	}
	return Count;
}
