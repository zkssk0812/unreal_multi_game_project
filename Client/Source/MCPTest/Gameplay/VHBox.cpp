#include "Gameplay/VHBox.h"

#include "Gameplay/VHGameplayConfig.h"
#include "Gameplay/VHInteractionConfig.h"
#include "Network/VHSettings.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

#define LOCTEXT_NAMESPACE "VHBox"

AVHBox::AVHBox()
{
	Kind = EVHEntityKind::Box;
	BodyColor = ValuableBoxColor;	// 종류를 모를 때 = 예전 나무색

	// 바닥에 놓인 80cm 상자 (Cube 기본 도형 = 100cm). 몸으로 밀고 지나갈 수 없다
	Body->SetStaticMesh(FindBasicShape(TEXT("Cube")));
	Body->SetRelativeScale3D(FVector(BoxSize / 100.f));
	Body->SetRelativeLocation(FVector(0.f, 0.f, BoxSize * 0.5f));

	// 띠·위 표시: 충돌 없음 (상호작용 테두리는 보이는 메시 모두에 그려진다)
	Band = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Band"));
	Band->SetupAttachment(SceneRoot);
	Band->SetStaticMesh(FindBasicShape(TEXT("Cube")));
	Band->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Band->SetVisibility(false);

	Emblem = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Emblem"));
	Emblem->SetupAttachment(SceneRoot);
	Emblem->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Emblem->SetVisibility(false);

	PromptAnchor->SetRelativeLocation(FVector(0.f, 0.f, BoxSize + 50.f));
}

void AVHBox::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyLook();
}

void AVHBox::SetBoxType(uint8 InType)
{
	BoxType = InType;
	ApplyLook();
}

void AVHBox::ApplyLook()
{
	const float S = BoxSize / 100.f;
	if (IsWeaponBox())
	{
		// 무기 상자: 낮고 긴 철제 상자 (가로 1.3배, 높이 0.75배) + 하늘색 띠 두 줄 느낌의 가운데 띠 + 뚜껑 위 가로 막대
		BodyColor = WeaponBoxColor;
		Body->SetRelativeScale3D(FVector(S * 1.3f, S, S * 0.75f));
		Body->SetRelativeLocation(FVector(0.f, 0.f, BoxSize * 0.375f));
		Band->SetRelativeScale3D(FVector(S * 0.2f, S * 1.04f, S * 0.78f));
		Band->SetRelativeLocation(FVector(0.f, 0.f, BoxSize * 0.375f));
		Emblem->SetStaticMesh(FindBasicShape(TEXT("Cube")));
		Emblem->SetRelativeScale3D(FVector(S * 0.75f, S * 0.14f, S * 0.08f));
		Emblem->SetRelativeLocation(FVector(0.f, 0.f, BoxSize * 0.75f + 3.f));
		Emblem->SetRelativeRotation(FRotator::ZeroRotator);
		PromptAnchor->SetRelativeLocation(FVector(0.f, 0.f, BoxSize * 0.75f + 50.f));
	}
	else if (IsValuableBox())
	{
		// 환금 상자: 나무 상자 + 금색 띠 + 뚜껑 위 금화
		BodyColor = ValuableBoxColor;
		Body->SetRelativeScale3D(FVector(S));
		Body->SetRelativeLocation(FVector(0.f, 0.f, BoxSize * 0.5f));
		Band->SetRelativeScale3D(FVector(S * 1.04f, S * 0.2f, S * 1.03f));
		Band->SetRelativeLocation(FVector(0.f, 0.f, BoxSize * 0.5f));
		Emblem->SetStaticMesh(FindBasicShape(TEXT("Cylinder")));
		Emblem->SetRelativeScale3D(FVector(S * 0.42f, S * 0.42f, S * 0.06f));
		Emblem->SetRelativeLocation(FVector(0.f, 0.f, BoxSize + 4.f));
		Emblem->SetRelativeRotation(FRotator::ZeroRotator);
		PromptAnchor->SetRelativeLocation(FVector(0.f, 0.f, BoxSize + 50.f));
	}
	const bool bTyped = IsWeaponBox() || IsValuableBox();
	Band->SetVisibility(bTyped);
	Emblem->SetVisibility(bTyped);
	TintMesh(Body, BodyColor);
	if (bTyped)
	{
		// 띠·위 표시 = 조명과 관계없이 보이는 단색(반납지점과 같은 M_VHUnlitColor). 엔진 기본 도형 재질은 Color 파라미터가 먹지 않는다(2026-11-24 확인)
		const FLinearColor Accent = IsWeaponBox() ? WeaponBandColor : ValuableBandColor;
		UMaterialInterface* Unlit = UVHSettings::Get().GetGameplayConfig().UnlitColorMaterial.LoadSynchronous();
		for (UStaticMeshComponent* Part : { Band.Get(), Emblem.Get() })
		{
			if (Unlit)
			{
				UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Unlit, this);
				MID->SetVectorParameterValue(TEXT("Color"), Accent);
				Part->SetMaterial(0, MID);
			}
			else
			{
				TintMesh(Part, Accent);
			}
		}
	}
}

FText AVHBox::GetBoxName() const
{
	return IsWeaponBox() ? LOCTEXT("WeaponBox", "무기 상자") : (IsValuableBox() ? LOCTEXT("ValuableBox", "환금 상자") : LOCTEXT("Box", "상자"));
}

FVHInfoCard AVHBox::GetInfoCard() const
{
	FVHInfoCard Card;
	if (IsWeaponBox())
	{
		Card.Text = LOCTEXT("WeaponBoxInfo", "무기 상자 · 무기 또는 소비 아이템");
		Card.Accent = WeaponBandColor;
	}
	else if (IsValuableBox())
	{
		Card.Text = LOCTEXT("ValuableBoxInfo", "환금 상자 · 환금 아이템");
		Card.Accent = ValuableBandColor;
	}
	else
	{
		Card.Text = LOCTEXT("BoxInfo", "상자");
	}
	return Card;
}

float AVHBox::GetWorkApproachDistance() const
{
	return UVHSettings::Get().GetInteractionConfig().BoxApproachDistance;
}

#undef LOCTEXT_NAMESPACE
