#include "Gameplay/VHFxActor.h"

#include "Gameplay/VHGameplayConfig.h"
#include "Gameplay/VHWeaponCatalog.h"
#include "Network/VHSettings.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

AVHFxActor::AVHFxActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(false);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);
}

UMaterialInterface* AVHFxActor::GetFxMaterial()
{
	return UVHSettings::Get().GetGameplayConfig().FxMaterial.LoadSynchronous();
}

AVHFxActor* AVHFxActor::SpawnFx(UWorld* World, const FVector& Location, EVHFxShape Shape, const FVector& SizeCm,
	const FLinearColor& Color, float Opacity, float InLife, float Yaw)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AVHFxActor* Fx = World->SpawnActor<AVHFxActor>(AVHFxActor::StaticClass(), Location, FRotator(0.f, Yaw, 0.f), Params);
	if (Fx)
	{
		Fx->InitFx(Shape, SizeCm, Color, Opacity);
		if (InLife > 0.f)
		{
			Fx->SetLife(InLife);
		}
	}
	return Fx;
}

void AVHFxActor::InitFx(EVHFxShape Shape, const FVector& SizeCm, const FLinearColor& Color, float Opacity)
{
	// 엔진 기본 도형은 모두 100cm 크기 (원기둥: 지름 100·높이 100, 구: 지름 100, 상자: 한 변 100)
	const TCHAR* Path = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	switch (Shape)
	{
	case EVHFxShape::Disc:
		BaseScale = FVector(SizeCm.X / 100.f, SizeCm.X / 100.f, 0.02f);
		break;
	case EVHFxShape::Sphere:
		Path = TEXT("/Engine/BasicShapes/Sphere.Sphere");
		BaseScale = FVector(SizeCm.X / 100.f);
		break;
	case EVHFxShape::Cylinder:
		BaseScale = FVector(SizeCm.X / 100.f, SizeCm.X / 100.f, SizeCm.Z / 100.f);
		break;
	case EVHFxShape::Box:
		Path = TEXT("/Engine/BasicShapes/Cube.Cube");
		BaseScale = SizeCm / 100.f;
		break;
	case EVHFxShape::Plane:
		Path = TEXT("/Engine/BasicShapes/Plane.Plane");
		BaseScale = FVector(SizeCm.X / 100.f, SizeCm.X / 100.f, 1.f);
		break;
	}
	Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Path));
	SetActorScale3D(BaseScale);

	UMaterialInterface* ShapeMat = Shape == EVHFxShape::Plane ? UVHSettings::Get().GetWeaponCatalog().FxShapeMaterial.LoadSynchronous() : nullptr;
	if (UMaterialInterface* Base = ShapeMat ? ShapeMat : GetFxMaterial())
	{
		Material = UMaterialInstanceDynamic::Create(Base, this);
		Mesh->SetMaterial(0, Material);
	}
	else
	{
		// 연출 머티리얼이 없으면 기본 도형 머티리얼(불투명)에 색만 입힌다
		Material = Mesh->CreateDynamicMaterialInstance(0, Mesh->GetMaterial(0));
	}
	SetFxColor(Color, Opacity);
}

AVHFxActor* AVHFxActor::SpawnShape(UWorld* World, const FVector& Location, float Yaw, float Diameter, const FVHFxShapeParams& Shape,
	const FLinearColor& Color, float Opacity, float InLife)
{
	AVHFxActor* Fx = SpawnFx(World, Location, EVHFxShape::Plane, FVector(Diameter), Color, Opacity, InLife, Yaw);
	if (Fx && Fx->Material)
	{
		UMaterialInstanceDynamic* M = Fx->Material;
		M->SetScalarParameterValue(TEXT("HalfAngle"), Shape.HalfAngle);
		M->SetScalarParameterValue(TEXT("SweepMin"), Shape.SweepMin);
		M->SetScalarParameterValue(TEXT("SweepMax"), Shape.SweepMax);
		M->SetScalarParameterValue(TEXT("FillOpacity"), Shape.FillOpacity);
		M->SetScalarParameterValue(TEXT("EdgeOpacity"), Shape.EdgeOpacity);
		M->SetScalarParameterValue(TEXT("EdgeWidth"), Shape.EdgeWidth);
		M->SetScalarParameterValue(TEXT("Inner"), Shape.Inner);
		M->SetScalarParameterValue(TEXT("Glow"), Shape.Glow);
		M->SetScalarParameterValue(TEXT("Spikes"), Shape.Spikes);
		M->SetScalarParameterValue(TEXT("Intensity"), Shape.Intensity);
	}
	return Fx;
}

void AVHFxActor::SetSweep(float FromMin, float FromMax, float ToMin, float ToMax, float Seconds)
{
	bSweep = true;
	SweepFrom = FVector4f(FromMin, FromMax, 0.f, 0.f);
	SweepTo = FVector4f(ToMin, ToMax, 0.f, 0.f);
	SweepTime = FMath::Max(Seconds, 0.001f);
	SetShapeScalar(TEXT("SweepMin"), FromMin);
	SetShapeScalar(TEXT("SweepMax"), FromMax);
}

void AVHFxActor::SetShapeScalar(FName Name, float Value)
{
	if (Material)
	{
		Material->SetScalarParameterValue(Name, Value);
	}
}

AVHFxActor* AVHFxActor::SpawnModel(UWorld* World, UStaticMesh* Model, const FTransform& Transform, float InLife)
{
	if (!World || !Model)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AVHFxActor* Fx = World->SpawnActor<AVHFxActor>(AVHFxActor::StaticClass(), Transform, Params);
	if (Fx)
	{
		Fx->bModel = true;
		Fx->Mesh->SetStaticMesh(Model);
		Fx->BaseScale = Transform.GetScale3D();
		Fx->SetActorScale3D(Fx->BaseScale);
		if (InLife > 0.f)
		{
			Fx->SetLife(InLife, 0.f);
		}
	}
	return Fx;
}

void AVHFxActor::TintModel(const FLinearColor& Color, float Opacity)
{
	UMaterialInterface* Base = GetFxMaterial();
	if (!Base)
	{
		return;
	}
	Material = UMaterialInstanceDynamic::Create(Base, this);
	for (int32 i = 0; i < Mesh->GetNumMaterials(); ++i)
	{
		Mesh->SetMaterial(i, Material);
	}
	bModel = false;	// 이제 재질의 불투명도로 흐려진다
	SetFxColor(Color, Opacity);
}

void AVHFxActor::AddChildFx(AVHFxActor* Child)
{
	if (Child)
	{
		ChildFx.Add(Child);
	}
}

void AVHFxActor::Destroyed()
{
	for (const TWeakObjectPtr<AVHFxActor>& Child : ChildFx)
	{
		if (AVHFxActor* C = Child.Get())
		{
			C->Destroy();
		}
	}
	ChildFx.Reset();
	Super::Destroyed();
}

void AVHFxActor::SetFxColor(const FLinearColor& Color, float Opacity)
{
	BaseOpacity = Opacity;
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Material->SetScalarParameterValue(TEXT("Opacity"), Opacity);
	}
}

void AVHFxActor::SetLife(float Seconds, float FadeSeconds)
{
	Life = FMath::Max(0.f, Seconds);
	Fade = FMath::Clamp(FadeSeconds, 0.f, Life);
	Age = 0.f;
}

void AVHFxActor::SetArc(const FVector& From, const FVector& To, float Height, float Seconds)
{
	bArc = true;
	ArcFrom = From;
	ArcTo = To;
	ArcHeight = Height;
	SetLife(Seconds, 0.f);
	SetActorLocation(From);
}

AVHFxActor* AVHFxActor::SpawnBeam(UWorld* World, const FVector& From, const FVector& To, float Thickness, const FLinearColor& Color, float Opacity, float InLife)
{
	const FVector Delta = To - From;
	const float Length = Delta.Size();
	if (Length < 1.f)
	{
		return nullptr;
	}
	AVHFxActor* Fx = SpawnFx(World, (From + To) * 0.5f, EVHFxShape::Box, FVector(Length, Thickness, Thickness), Color, Opacity, InLife);
	if (Fx)
	{
		Fx->SetActorRotation(Delta.Rotation());
	}
	return Fx;
}

void AVHFxActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (bArc && Life > 0.f)
	{
		const float T = FMath::Clamp(Age / Life, 0.f, 1.f);
		FVector Pos = FMath::Lerp(ArcFrom, ArcTo, T);
		Pos.Z += ArcHeight * 4.f * T * (1.f - T);	// 가운데(T = 0.5)에서 가장 높다
		SetActorLocation(Pos);
	}
	if (!Spin.IsZero())
	{
		AddActorLocalRotation(Spin * DeltaSeconds);
	}
	if (bSweep)
	{
		const float S = FMath::Clamp(Age / SweepTime, 0.f, 1.f);
		const float Ease = 1.f - FMath::Square(1.f - S);	// 빠르게 시작해서 끝에서 느려진다 (휘두르기)
		SetShapeScalar(TEXT("SweepMin"), FMath::Lerp(SweepFrom.X, SweepTo.X, Ease));
		SetShapeScalar(TEXT("SweepMax"), FMath::Lerp(SweepFrom.Y, SweepTo.Y, Ease));
		if (S >= 1.f)
		{
			bSweep = false;
		}
	}

	float Opacity = BaseOpacity;
	if (PulseHz > 0.f)
	{
		Opacity *= 0.55f + 0.45f * FMath::Sin(Age * PulseHz * 2.f * PI);
	}
	if (Life > 0.f)
	{
		if (Age >= Life)
		{
			Destroy();
			return;
		}
		const float Alpha = Age / Life;
		if (!FMath::IsNearlyEqual(GrowTo, 1.f))
		{
			SetActorScale3D(BaseScale * FMath::Lerp(1.f, GrowTo, Alpha));
		}
		if (Fade > 0.f && Life - Age < Fade)
		{
			Opacity *= (Life - Age) / Fade;
		}
	}
	if (Material && !bModel && (PulseHz > 0.f || Life > 0.f))
	{
		Material->SetScalarParameterValue(TEXT("Opacity"), Opacity);
	}
}
