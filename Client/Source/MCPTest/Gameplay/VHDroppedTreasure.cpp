#include "Gameplay/VHDroppedTreasure.h"

#include "Gameplay/VHInteractionConfig.h"
#include "Network/VHSettings.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"

AVHDroppedTreasure::AVHDroppedTreasure()
{
	PrimaryActorTick.bCanEverTick = true;

	Kind = EVHEntityKind::DroppedTreasure;
	BodyColor = FLinearColor(1.f, 0.72f, 0.05f);

	Body->SetStaticMesh(FindBasicShape(TEXT("Sphere")));
	Body->SetRelativeLocation(FVector(0.f, 0.f, 20.f));
	Body->SetRelativeScale3D(FVector(0.3f));
	// 캐릭터가 밟고 지나갈 수 있게: 이동은 막지 않고, 상호작용 거리 계산용으로만 쓴다
	Body->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(SceneRoot);
	Glow->SetRelativeLocation(FVector(0.f, 0.f, 40.f));
	Glow->SetIntensityUnits(ELightUnits::Candelas);
	Glow->SetIntensity(3.f);
	Glow->SetAttenuationRadius(180.f);
	Glow->SetCastShadows(false);
	Glow->SetLightColor(FLinearColor(1.f, 0.7f, 0.1f));

	PromptAnchor->SetRelativeLocation(FVector(0.f, 0.f, 90.f));

	Item = FVHItemData::MakeTreasure(NSLOCTEXT("VHGameplay", "DefaultTreasure", "보물"), 1000);
}


void AVHDroppedTreasure::OnConstruction(const FTransform& Transform)
{
	// 보물이 아닌 환금 아이템(상자에서 나온 일반·고급·희귀)은 더 작게, 품질 색으로 (임시 외형)
	if (Item.Kind != EVHItemKind::Treasure)
	{
		switch (Item.GetQuality())
		{
		case EVHItemQuality::Advanced:	BodyColor = FLinearColor(0.15f, 0.9f, 0.25f); break;
		case EVHItemQuality::Rare:		BodyColor = FLinearColor(0.15f, 0.45f, 1.f); break;
		default:						BodyColor = FLinearColor(0.9f, 0.9f, 0.9f); break;
		}
		Body->SetRelativeScale3D(FVector(0.2f));
	}
	Super::OnConstruction(Transform);
}

void AVHDroppedTreasure::BeginPlay()
{
	Super::BeginPlay();
	BaseBodyZ = Body->GetRelativeLocation().Z;
	TimeOffset = FMath::FRand() * 10.f;
	Glow->SetLightColor(Item.Kind == EVHItemKind::Treasure ? UVHSettings::Get().GetInteractionConfig().TreasureGlowColor : BodyColor);
}

FText AVHDroppedTreasure::QualityText(EVHItemQuality Quality)
{
	switch (Quality)
	{
	case EVHItemQuality::Advanced:	return NSLOCTEXT("VHInfo", "QAdvanced", "고급");
	case EVHItemQuality::Rare:		return NSLOCTEXT("VHInfo", "QRare", "희귀");
	case EVHItemQuality::Unique:	return NSLOCTEXT("VHInfo", "QUnique", "유일");
	default:						return NSLOCTEXT("VHInfo", "QCommon", "일반");
	}
}

FVHInfoCard AVHDroppedTreasure::GetInfoCard() const
{
	FVHInfoCard Card;
	const EVHItemQuality Quality = Item.GetQuality();
	Card.Text = FText::Format(NSLOCTEXT("VHInfo", "ValuableCard", "{0}/{1}/{2}$"), Item.DisplayName, QualityText(Quality), FText::AsNumber(Item.Value));
	switch (Quality)
	{
	case EVHItemQuality::Advanced:	Card.Tone = EVHInfoTone::Advanced; break;
	case EVHItemQuality::Rare:		Card.Tone = EVHInfoTone::Rare; break;
	case EVHItemQuality::Unique:	Card.Tone = EVHInfoTone::Unique; break;
	default:						Card.Tone = EVHInfoTone::Common; break;
	}
	return Card;
}

void AVHDroppedTreasure::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float T = GetWorld()->GetTimeSeconds() + TimeOffset;
	FVector Loc = Body->GetRelativeLocation();
	Loc.Z = BaseBodyZ + FMath::Sin(T * 2.5f) * BobHeight;
	Body->SetRelativeLocation(Loc);
	Body->AddRelativeRotation(FRotator(0.f, 60.f * DeltaSeconds, 0.f));
}

void AVHDroppedTreasure::SetOutline(EVHOutline State)
{
	Super::SetOutline(State);
	// 대상이 되면 빛을 조금 더 밝게
	Glow->SetIntensity(State == EVHOutline::Focus || State == EVHOutline::Blocked ? 8.f : 3.f);
}
