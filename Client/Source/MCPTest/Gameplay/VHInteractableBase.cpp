#include "Gameplay/VHInteractableBase.h"

#include "Gameplay/VHInteractionSubsystem.h"
#include "Gameplay/VHInteractionConfig.h"
#include "Gameplay/VHInteractionComponent.h"
#include "Network/VHSettings.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AVHInteractableBase::AVHInteractableBase()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(SceneRoot);
	Body->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

	PromptAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("PromptAnchor"));
	PromptAnchor->SetupAttachment(SceneRoot);
	PromptAnchor->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
}

UStaticMesh* AVHInteractableBase::FindBasicShape(const TCHAR* Name)
{
	// 엔진 기본 도형(/Engine/BasicShapes). 블루프린트에서 다른 메시로 바꿀 수 있다.
	const FString Path = FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name);
	return LoadObject<UStaticMesh>(nullptr, *Path);
}

void AVHInteractableBase::TintMesh(UStaticMeshComponent* Mesh, const FLinearColor& Color)
{
	if (!Mesh || !Mesh->GetStaticMesh())
	{
		return;
	}
	UMaterialInterface* Base = Mesh->GetMaterial(0);
	if (!Base)
	{
		return;
	}
	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Base);
	if (!MID)
	{
		MID = Mesh->CreateDynamicMaterialInstance(0, Base);
	}
	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("Color"), Color);
	}
}

void AVHInteractableBase::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	TintMesh(Body, BodyColor);
}

void AVHInteractableBase::BeginPlay()
{
	Super::BeginPlay();
	if (UVHInteractionSubsystem* Sub = GetInteractionSubsystem())
	{
		Sub->RegisterInteractable(this);
	}
}

void AVHInteractableBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UVHInteractionSubsystem* Sub = GetInteractionSubsystem())
	{
		Sub->UnregisterInteractable(this);
	}
	Super::EndPlay(EndPlayReason);
}

UVHInteractionSubsystem* AVHInteractableBase::GetInteractionSubsystem() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UVHInteractionSubsystem>() : nullptr;
}

FBox AVHInteractableBase::GetInteractBounds() const
{
	// 본체 + 충돌이 있는 보이는 부품들의 경계
	FBox Box(ForceInit);
	const UStaticMeshComponent* BodyComp = Body;
	ForEachComponent<UPrimitiveComponent>(false, [&Box, BodyComp](const UPrimitiveComponent* Prim)
	{
		if (Prim->IsRegistered() && Prim->IsVisible() && (Prim == BodyComp || Prim->IsCollisionEnabled()))
		{
			Box += Prim->Bounds.GetBox();
		}
	});
	if (!Box.IsValid)
	{
		Box = FBox::BuildAABB(GetActorLocation(), FVector(10.f));
	}
	return Box;
}

FVector AVHInteractableBase::GetPromptLocation() const
{
	return PromptAnchor ? PromptAnchor->GetComponentLocation() : GetActorLocation();
}

void AVHInteractableBase::SetActiveUser(UVHInteractionComponent* User)
{
	ActiveUser = User;
}

bool AVHInteractableBase::ShouldShowIdleProgress() const
{
	return !ActiveUser.IsValid() && HoldProgress.SavedProgress > 0.f && HoldProgress.SavedProgress < 1.f;
}

void AVHInteractableBase::SetOutline(EVHOutline State)
{
	if (OutlineState == State)
	{
		return;
	}
	OutlineState = State;
	// 커스텀 뎁스에 그리고 스텐실 값(1 빛 안 / 2 현재 대상 / 3 불가)을 넣는다 → M_VHOutline 이 그 가장자리에 색을 칠함
	const bool bOn = State != EVHOutline::None;
	const int32 Stencil = static_cast<int32>(State);
	ForEachComponent<UMeshComponent>(false, [bOn, Stencil](UMeshComponent* Mesh)
	{
		if (Mesh->IsVisible())
		{
			Mesh->SetRenderCustomDepth(bOn);
			Mesh->SetCustomDepthStencilValue(Stencil);
		}
		else
		{
			Mesh->SetRenderCustomDepth(false);
		}
	});
}

float AVHInteractableBase::GetInteractDistance(const FVector& From) const
{
	// 높이는 무시하고(탑뷰) 상자 가장자리까지 수평 거리
	const FVector Closest = GetInteractBounds().GetClosestPointTo(From);
	return FVector::Dist2D(Closest, From);
}

float AVHInteractableBase::GetInteractRange() const
{
	return UVHSettings::Get().GetInteractionConfig().SearchRadius;
}

FVector AVHInteractableBase::GetWorkPoint() const
{
	return GetInteractBounds().GetCenter();
}

float AVHInteractableBase::GetWorkApproachDistance() const
{
	return UVHSettings::Get().GetInteractionConfig().WorkApproachDistance;
}

FVHInfoCard AVHInteractableBase::GetInfoCard() const
{
	FVHInfoCard Card;
	switch (Kind)
	{
	case EVHEntityKind::Safe:			Card.Text = NSLOCTEXT("VHInfo", "Safe", "금고"); break;
	case EVHEntityKind::Box:			Card.Text = NSLOCTEXT("VHInfo", "Box", "상자"); break;
	case EVHEntityKind::ReturnPoint:	Card.Text = NSLOCTEXT("VHInfo", "ReturnPoint", "반납지점"); break;
	default: break;
	}
	return Card;
}
