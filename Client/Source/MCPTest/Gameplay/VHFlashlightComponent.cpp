#include "Gameplay/VHFlashlightComponent.h"

#include "Gameplay/VHInteractionConfig.h"
#include "Gameplay/VHInteractionSubsystem.h"
#include "Gameplay/VHInteractionAuthority.h"
#include "Network/VHSettings.h"
#include "Components/PointLightComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"

UVHFlashlightComponent::UVHFlashlightComponent()
{
	// 캐릭터 가슴 높이 앞쪽, 약간 아래를 비춘다 (블루프린트에서 조정 가능)
	SetRelativeLocation(FVector(DefaultForward, 0.f, DefaultUp));
	SetRelativeRotation(FRotator(DefaultPitch, 0.f, 0.f));
	SetIntensityUnits(ELightUnits::Candelas);
	// 벽·가구에 막히도록 그림자는 계산한다. 의적 몸은 그림자를 만들지 않게 따로 끈다(ApplyThiefShadowSetting)
	SetCastShadows(true);
	SetVisibility(false);
}

void UVHFlashlightComponent::BeginPlay()
{
	Super::BeginPlay();
	const UVHInteractionConfig& C = UVHSettings::Get().GetInteractionConfig();
	ConfigureBeam(this);

	AActor* Owner = GetOwner();
	ApplyThiefShadowSetting(Owner);

	// 주변 빛: 손전등(시선 따라 회전)이 아니라 몸(루트)에 붙여 늘 의적 머리 위에 있게 한다
	if (C.bFlashlightAura && Owner && Owner->GetRootComponent())
	{
		Aura = NewObject<UPointLightComponent>(Owner, TEXT("FlashlightAura"));
		Aura->SetupAttachment(Owner->GetRootComponent());
		Aura->RegisterComponent();
		const ACharacter* Character = Cast<ACharacter>(Owner);
		const float FootZ = Character && Character->GetCapsuleComponent() ? -Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
		ConfigureAura(Aura, FootZ);
	}

	ApplyOn(C.bUseFlashlight && C.bFlashlightOnAtStart && bHasFlashlight);
	LastToggleTime = -1000.0;
}

void UVHFlashlightComponent::ConfigureBeam(USpotLightComponent* Light)
{
	if (!Light)
	{
		return;
	}
	const UVHInteractionConfig& C = UVHSettings::Get().GetInteractionConfig();
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(C.FlashlightIntensity);
	Light->SetAttenuationRadius(C.FlashlightRange);
	Light->SetOuterConeAngle(C.FlashlightOuterCone);
	Light->SetInnerConeAngle(C.FlashlightInnerCone);
	Light->SetLightColor(C.FlashlightColor);
	Light->SetCastShadows(true);	// 벽·가구에 막힘 (의적 몸은 그림자를 만들지 않게 메시 쪽에서 끔)
}

void UVHFlashlightComponent::ConfigureAura(UPointLightComponent* Light, float FootZ)
{
	if (!Light)
	{
		return;
	}
	const UVHInteractionConfig& C = UVHSettings::Get().GetInteractionConfig();
	const float R = FMath::Max(10.f, C.FlashlightAuraRadius);
	const float H = FMath::Max(0.f, C.FlashlightAuraHeight);
	Light->SetRelativeLocation(FVector(0.f, 0.f, FootZ + H));
	if (C.FlashlightAuraFalloffExponent > 0.f)
	{
		// 부드러운 감쇠: 머리와 바닥이 비슷하게 밝아져 몸 윤곽이 고르게 보인다
		Light->SetUseInverseSquaredFalloff(false);
		Light->SetLightFalloffExponent(C.FlashlightAuraFalloffExponent);
		Light->SetIntensityUnits(ELightUnits::Unitless);
	}
	else
	{
		Light->SetUseInverseSquaredFalloff(true);
		Light->SetIntensityUnits(ELightUnits::Candelas);
	}
	Light->SetIntensity(C.FlashlightAuraIntensity);
	Light->SetLightColor(C.FlashlightAuraColor);
	// 빛이 머리 위에 있으므로 바닥의 반경 R 지점까지 닿게 거리를 잡는다(가장자리는 부드럽게 사라짐)
	Light->SetAttenuationRadius(FMath::Sqrt(R * R + H * H) * 1.2f);
	Light->SetSourceRadius(20.f);
	Light->SetCastShadows(C.bFlashlightAuraShadows);
	Light->SetSpecularScale(0.3f);
}

void UVHFlashlightComponent::ApplyThiefShadowSetting(AActor* Thief)
{
	if (!Thief || UVHSettings::Get().GetInteractionConfig().bThiefCastsShadow)
	{
		return;	// 켜져 있으면 메시 설정 그대로
	}
	TInlineComponentArray<UPrimitiveComponent*> Prims(Thief);
	for (UPrimitiveComponent* P : Prims)
	{
		P->SetCastShadow(false);
	}
}

void UVHFlashlightComponent::RequestToggle()
{
	if (UVHInteractionSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UVHInteractionSubsystem>() : nullptr)
	{
		Sub->GetAuthority()->RequestFlashlight(this, !bOn);
	}
}

bool UVHFlashlightComponent::IsBoxLit(const FBox& Box, const AActor* Target) const
{
	if (!bOn || !IsVisible() || !Box.IsValid)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	const FVector Origin = GetComponentLocation();
	const FVector Dir = GetForwardVector();
	const float Range = AttenuationRadius;
	const float CosCone = FMath::Cos(FMath::DegreesToRadians(OuterConeAngle));

	const FVector C = Box.GetCenter();
	const FVector E = Box.GetExtent() * 0.8f;
	const FVector Points[] = {
		C, FVector(C.X, C.Y, Box.Max.Z - 2.f),
		C + FVector(E.X, 0.f, 0.f), C - FVector(E.X, 0.f, 0.f), C + FVector(0.f, E.Y, 0.f), C - FVector(0.f, E.Y, 0.f) };

	FCollisionQueryParams Params(TEXT("VHFlashlightLit"), false, GetOwner());
	if (Target)
	{
		Params.AddIgnoredActor(Target);
	}
	for (const FVector& P : Points)
	{
		const FVector To = P - Origin;
		const float Dist = To.Size();
		if (Dist > Range || Dist < KINDA_SMALL_NUMBER)
		{
			continue;
		}
		if (FVector::DotProduct(Dir, To / Dist) < CosCone)
		{
			continue;	// 원뿔 밖
		}
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Origin, P, ECC_Visibility, Params))
		{
			return true;	// 가려지지 않음
		}
	}
	return false;
}

void UVHFlashlightComponent::ApplyOn(bool bInOn)
{
	bOn = bInOn;
	if (const UVHInteractionSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UVHInteractionSubsystem>() : nullptr)
	{
		LastToggleTime = Sub->GetNow();
	}
	SetVisibility(bOn);
	if (Aura)
	{
		Aura->SetVisibility(bOn);	// 주변 빛은 손전등과 함께 켜지고 꺼진다
	}
	OnFlashlightChanged.Broadcast(this, bOn);
}
