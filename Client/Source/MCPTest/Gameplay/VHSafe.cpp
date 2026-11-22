#include "Gameplay/VHSafe.h"

#include "Components/StaticMeshComponent.h"

AVHSafe::AVHSafe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Kind = EVHEntityKind::Safe;
	BodyColor = FLinearColor(0.22f, 0.24f, 0.28f);

	// 기본 도형: 1m 정육면체 기준. 본체 90×90×110cm, 앞면(+X)에 문.
	Body->SetStaticMesh(FindBasicShape(TEXT("Cube")));
	Body->SetRelativeLocation(FVector(0.f, 0.f, 55.f));
	Body->SetRelativeScale3D(FVector(0.9f, 0.9f, 1.1f));

	DoorHinge = CreateDefaultSubobject<USceneComponent>(TEXT("DoorHinge"));
	DoorHinge->SetupAttachment(SceneRoot);
	DoorHinge->SetRelativeLocation(FVector(47.f, -45.f, 55.f));

	Door = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Door"));
	Door->SetupAttachment(DoorHinge);
	Door->SetStaticMesh(FindBasicShape(TEXT("Cube")));
	Door->SetRelativeLocation(FVector(2.f, 45.f, 0.f));
	Door->SetRelativeScale3D(FVector(0.05f, 0.88f, 1.05f));
	Door->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	TreasureMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TreasureMesh"));
	TreasureMesh->SetupAttachment(SceneRoot);
	TreasureMesh->SetStaticMesh(FindBasicShape(TEXT("Sphere")));
	TreasureMesh->SetRelativeLocation(FVector(52.f, 0.f, 70.f));
	TreasureMesh->SetRelativeScale3D(FVector(0.28f));
	TreasureMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TreasureMesh->SetVisibility(false);

	PromptAnchor->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
}

void AVHSafe::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	TintMesh(Door, DoorColor);
	TintMesh(TreasureMesh, TreasureColor);
}

void AVHSafe::ApplyOpened()
{
	if (SafeState != EVHSafeState::Closed)
	{
		return;
	}
	SafeState = EVHSafeState::Opened;
	HoldProgress.Finish();
	TreasureMesh->SetVisibility(true);
	SetActorTickEnabled(true);	// 문 열리는 연출
	OnVisualStateChanged();
}

void AVHSafe::ApplyTreasureTaken()
{
	if (SafeState != EVHSafeState::Opened)
	{
		return;
	}
	SafeState = EVHSafeState::Empty;
	TreasureMesh->SetVisibility(false);
	SetOutline(EVHOutline::None);
	OnVisualStateChanged();
}

void AVHSafe::ResetSafe()
{
	SafeState = EVHSafeState::Closed;
	HoldProgress.Reset();
	DoorAlpha = 0.f;
	DoorHinge->SetRelativeRotation(FRotator::ZeroRotator);
	TreasureMesh->SetVisibility(false);
	SetActorTickEnabled(false);
	OnVisualStateChanged();
}

bool AVHSafe::ShouldShowIdleProgress() const
{
	return SafeState == EVHSafeState::Closed && Super::ShouldShowIdleProgress();
}

void AVHSafe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	DoorAlpha = FMath::Min(1.f, DoorAlpha + DeltaSeconds / FMath::Max(0.05f, DoorOpenTime));
	const float Eased = FMath::InterpEaseOut(0.f, 1.f, DoorAlpha, 2.f);
	DoorHinge->SetRelativeRotation(FRotator(0.f, DoorOpenYaw * Eased, 0.f));
	if (DoorAlpha >= 1.f)
	{
		SetActorTickEnabled(false);
	}
}

float AVHSafe::GetWorkApproachDistance() const
{
	// 몸통(반 폭 45cm, 모서리 64cm) + 캐릭터 반지름이면 85~105cm에서 닿는다. 서버 허용은 중심에서 130cm
	return bWorkFromAnySide ? 112.f : Super::GetWorkApproachDistance();
}

FVector AVHSafe::GetWorkPoint() const
{
	if (bWorkFromAnySide)
	{
		return GetActorLocation();
	}
	FVector Point = GetActorLocation() + GetActorForwardVector() * 115.f;
	Point.Z = GetActorLocation().Z;
	return Point;
}
