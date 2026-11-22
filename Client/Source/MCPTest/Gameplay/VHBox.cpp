#include "Gameplay/VHBox.h"

#include "Gameplay/VHInteractionConfig.h"
#include "Network/VHSettings.h"

#include "Components/StaticMeshComponent.h"

AVHBox::AVHBox()
{
	Kind = EVHEntityKind::Box;
	BodyColor = FLinearColor(0.45f, 0.27f, 0.1f);	// 나무색

	// 바닥에 놓인 80cm 상자 (Cube 기본 도형 = 100cm). 몸으로 밀고 지나갈 수 없다
	Body->SetStaticMesh(FindBasicShape(TEXT("Cube")));
	Body->SetRelativeScale3D(FVector(BoxSize / 100.f));
	Body->SetRelativeLocation(FVector(0.f, 0.f, BoxSize * 0.5f));

	PromptAnchor->SetRelativeLocation(FVector(0.f, 0.f, BoxSize + 50.f));
}

float AVHBox::GetWorkApproachDistance() const
{
	return UVHSettings::Get().GetInteractionConfig().BoxApproachDistance;
}
