#include "Gameplay/VHReturnPoint.h"

#include "Gameplay/VHGameplayConfig.h"
#include "Gameplay/VHInteractionConfig.h"
#include "Gameplay/VHInteractionSubsystem.h"
#include "Network/VHSettings.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

AVHReturnPoint::AVHReturnPoint()
{
	PrimaryActorTick.bCanEverTick = true;

	Kind = EVHEntityKind::ReturnPoint;
	bAlwaysVisible = true;	// 조명과 관계없이 항상 보임 [팀 결정]
	BodyColor = FLinearColor(0.05f, 1.f, 0.2f);

	// 1m × 1m 바닥 정사각형 (Plane 기본 도형 = 100 × 100). 밟고 지나갈 수 있게 충돌 없음
	Body->SetStaticMesh(FindBasicShape(TEXT("Plane")));
	Body->SetRelativeLocation(FVector(0.f, 0.f, 2.f));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(false);

	// 테두리 4변 (조금 더 밝게) — 정사각형 구획이 잘 보이도록
	for (int32 i = 0; i < 4; ++i)
	{
		UStaticMeshComponent* Edge = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Edge%d"), i));
		Edge->SetupAttachment(SceneRoot);
		Edge->SetStaticMesh(FindBasicShape(TEXT("Cube")));
		Edge->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Edge->SetCastShadow(false);
		Edges.Add(Edge);
	}

	PromptAnchor->SetRelativeLocation(FVector(0.f, 0.f, 70.f));
}

double AVHReturnPoint::GetNow() const
{
	if (const UVHInteractionSubsystem* Sub = GetInteractionSubsystem())
	{
		return Sub->GetNow();
	}
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

void AVHReturnPoint::BeginPlay()
{
	Super::BeginPlay();
	const UVHGameplayConfig& G = UVHSettings::Get().GetGameplayConfig();

	// 크기는 데이터(ReturnPointSize)로
	const float Size = G.ReturnPointSize;
	Body->SetRelativeScale3D(FVector(Size / 100.f, Size / 100.f, 1.f));
	const float Half = Size * 0.5f;
	const float Thick = 6.f;
	const FVector EdgeLoc[4] = { FVector(Half, 0.f, 4.f), FVector(-Half, 0.f, 4.f), FVector(0.f, Half, 4.f), FVector(0.f, -Half, 4.f) };
	const FVector EdgeScale[4] = {
		FVector(Thick / 100.f, (Size + Thick) / 100.f, 0.04f), FVector(Thick / 100.f, (Size + Thick) / 100.f, 0.04f),
		FVector((Size + Thick) / 100.f, Thick / 100.f, 0.04f), FVector((Size + Thick) / 100.f, Thick / 100.f, 0.04f) };
	for (int32 i = 0; i < Edges.Num(); ++i)
	{
		Edges[i]->SetRelativeLocation(EdgeLoc[i]);
		Edges[i]->SetRelativeScale3D(EdgeScale[i]);
	}

	// 조명과 관계없이 보이는 단색(스스로 빛남) 머티리얼
	if (UMaterialInterface* Unlit = G.UnlitColorMaterial.LoadSynchronous())
	{
		ColorMID = UMaterialInstanceDynamic::Create(Unlit, this);
		Body->SetMaterial(0, ColorMID);
		for (UStaticMeshComponent* Edge : Edges)
		{
			Edge->SetMaterial(0, ColorMID);
		}
	}
	if (SpawnTime <= 0.0)
	{
		SpawnTime = GetNow();
	}
	UpdateColor();
}

void AVHReturnPoint::InitPoint(double InSpawnTime)
{
	SpawnTime = InSpawnTime;
	bServerDriven = true;	// InitPoint 는 서버 엔티티로 만들 때만 부른다 → 빨강은 서버 신호로만
}

void AVHReturnPoint::SetServerWarning(bool bWarning)
{
	bServerDriven = true;
	if (bWarning == bServerWarning)
	{
		return;
	}
	bServerWarning = bWarning;
	if (bWarning)
	{
		// 서버는 등장 55초에 경고를 켠다 → 화면의 남은 시간("곧 이동 N초")을 그 시각에 맞춘다
		SpawnTime = GetNow() - UVHSettings::Get().GetGameplayConfig().ReturnPointRedSeconds;
	}
	UE_LOG(LogTemp, Log, TEXT("[반납지점] %s 서버 경고 %s: 남은 %.1f초"), *GetName(), bWarning ? TEXT("켜짐 → 빨강") : TEXT("꺼짐"), GetRemainingSeconds());
	UpdateColor();
}

float AVHReturnPoint::GetAge() const
{
	return static_cast<float>(FMath::Max(0.0, GetNow() - SpawnTime));
}

float AVHReturnPoint::GetRemainingSeconds() const
{
	return FMath::Max(0.f, UVHSettings::Get().GetGameplayConfig().ReturnPointLifetimeSeconds - GetAge());
}

bool AVHReturnPoint::IsRed() const
{
	if (bServerDriven)
	{
		return bServerWarning;	// 서버 신호 기준 (2026-11-22: 서버가 55초 = 남은 5초에 경고)
	}
	return GetAge() >= UVHSettings::Get().GetGameplayConfig().ReturnPointRedSeconds;
}

void AVHReturnPoint::UpdateColor()
{
	const UVHGameplayConfig& G = UVHSettings::Get().GetGameplayConfig();
	bShownRed = IsRed();
	const FLinearColor Color = bShownRed ? G.ReturnPointRed * G.ReturnPointRedGlow : G.ReturnPointGreen * G.ReturnPointGlow;
	if (ColorMID)
	{
		ColorMID->SetVectorParameterValue(TEXT("Color"), Color);
	}
	else
	{
		TintMesh(Body, Color);
	}
}

void AVHReturnPoint::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsRed() != bShownRed)
	{
		UE_LOG(LogTemp, Log, TEXT("[반납지점] %s %s: 등장 %.1f초, 남은 %.1f초"), *GetName(), IsRed() ? TEXT("빨강으로") : TEXT("초록으로"), GetAge(), GetRemainingSeconds());
		UpdateColor();	// 서버 없이 놓인 반납지점: 등장 55초(남은 5초)가 지나면 빨간색 (서버 반납지점은 SetServerWarning)
	}
}

float AVHReturnPoint::GetInteractDistance(const FVector& From) const
{
	// 서버와 같이 사각형 "가운데"에서의 수평 거리 (원 범위)
	return FVector::Dist2D(GetActorLocation(), From);
}

float AVHReturnPoint::GetInteractRange() const
{
	return UVHSettings::Get().GetInteractionConfig().ReturnPointInteractRadius;
}
