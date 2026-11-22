#include "Gameplay/VHDoor.h"

#include "Gameplay/VHCeilingSubsystem.h"
#include "Gameplay/VHGameplayConfig.h"
#include "Gameplay/VHNetProxy.h"
#include "Gameplay/VHSoundConfig.h"
#include "Network/VHSettings.h"
#include "MCPTest.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	TAutoConsoleVariable<int32> CVarVHDoors(TEXT("vh.Doors"), 1,
		TEXT("방 입구 문(보이는 연출만): 1 = 켬(기본), 0 = 숨김."));
	TAutoConsoleVariable<int32> CVarVHDoorMarks(TEXT("vh.DoorMarks"), 1,
		TEXT("문 위치 표시(바닥 문턱 띠·화살표, 천장 위 밝은 틈): 1 = 켬(기본), 0 = 끔. 미니맵 문 표시는 따로 늘 보임."));

	constexpr float JambWidth = 5.f;		// 문틀 기둥 폭
	constexpr float HeaderHeight = 5.f;		// 문틀 윗부분 높이
	constexpr float PanelThickness = 4.f;	// 문짝 두께 (벽 가운데)

	FVector CubeScale(float X, float Y, float Z) { return FVector(X, Y, Z) / 100.f; }

	// vh.DoorInfo : 문 수·열린 문
	FAutoConsoleCommandWithWorldAndArgs GVHDoorInfoCommand(TEXT("vh.DoorInfo"),
		TEXT("개발용: vh.DoorInfo [all] — 문 개수와 열린 문(all 이면 전부)을 로그에 쓴다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			const bool bAll = Args.Num() > 0 && Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase);
			int32 Total = 0, Open = 0, Double = 0, Locked = 0;
			for (TActorIterator<AVHDoor> It(World); It; ++It)
			{
				++Total;
				Open += It->GetOpenAlpha() > 0.f ? 1 : 0;
				Double += It->bDouble ? 1 : 0;
				Locked += It->bLocked ? 1 : 0;
				if (bAll || It->GetOpenAlpha() > 0.f)
				{
					UE_LOG(LogMCPTest, Log, TEXT("[Door]   %s"), *It->Describe());
				}
			}
			UE_LOG(LogMCPTest, Log, TEXT("[Door] 문 %d개 (양문 %d, 한 짝 %d, 잠김 %d), 지금 열린 문 %d"), Total, Double, Total - Double, Locked, Open);
		}));
}

AVHDoor::AVHDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	SetCanBeDamaged(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);
	SceneRoot->SetMobility(EComponentMobility::Movable);

	JambL = MakePart(TEXT("JambL"));
	JambR = MakePart(TEXT("JambR"));
	Header = MakePart(TEXT("Header"));
	PanelA = MakePart(TEXT("PanelA"));
	PanelB = MakePart(TEXT("PanelB"));
	HandleA = MakePart(TEXT("HandleA"));
	HandleB = MakePart(TEXT("HandleB"));
	LockLight = MakePart(TEXT("LockLight"));
	MarkStrip = MakeMark(TEXT("MarkStrip"));
	MarkArrow = MakeMark(TEXT("MarkArrow"));
	MarkCeiling = MakeMark(TEXT("MarkCeiling"));
	MarkCeiling->TranslucencySortPriority = 10;	// 천장(불투명) 위에 그린다
}

UStaticMeshComponent* AVHDoor::MakeMark(const TCHAR* Name)
{
	// 문 위치 표시: 엔진 Plane(100x100, UV u = +X, v = +Y) + 반투명 언릿 M_VHDoorMark. 충돌·그림자·반사 없음, 처음엔 숨김
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	C->SetupAttachment(SceneRoot);
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(Plane.Object);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCollisionResponseToAllChannels(ECR_Ignore);
	C->SetGenerateOverlapEvents(false);
	C->SetCanEverAffectNavigation(false);
	C->SetCastShadow(false);
	C->bReceivesDecals = false;
	C->bAffectDynamicIndirectLighting = false;
	C->bAffectDistanceFieldLighting = false;
	C->bVisibleInReflectionCaptures = false;
	C->bVisibleInRealTimeSkyCaptures = false;
	C->SetVisibleInRayTracing(false);
	C->SetVisibility(false);
	return C;
}

UStaticMeshComponent* AVHDoor::MakePart(const TCHAR* Name)
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	C->SetupAttachment(SceneRoot);
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(Cube.Object);
	// 보이는 연출만: 이동·총알·시야·마우스 조준·카메라 어디에도 걸리지 않는다
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCollisionResponseToAllChannels(ECR_Ignore);
	C->SetGenerateOverlapEvents(false);
	C->SetCanEverAffectNavigation(false);
	C->bReceivesDecals = false;
	C->SetCastShadow(false);
	return C;
}

void AVHDoor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Layout();
}

void AVHDoor::Layout()
{
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	UMaterialInterface* Wood = C.DoorPanelMaterial.LoadSynchronous();
	UMaterialInterface* Metal = C.DoorFrameMaterial.LoadSynchronous();
	UMaterialInterface* Lock = C.DoorLockMaterial.LoadSynchronous();

	ClearWidth = Width - 2.f * JambWidth;
	PanelHeight = OpeningHeight - HeaderHeight - 1.f;
	const float Depth = WallThickness + 2.f;	// 문틀은 벽 두 면에서 1cm씩 튀어나온다

	JambL->SetRelativeLocation(FVector(0.f, -Width * 0.5f + JambWidth * 0.5f, OpeningHeight * 0.5f));
	JambR->SetRelativeLocation(FVector(0.f, Width * 0.5f - JambWidth * 0.5f, OpeningHeight * 0.5f));
	JambL->SetRelativeScale3D(CubeScale(Depth, JambWidth, OpeningHeight));
	JambR->SetRelativeScale3D(CubeScale(Depth, JambWidth, OpeningHeight));
	Header->SetRelativeLocation(FVector(0.f, 0.f, OpeningHeight - HeaderHeight * 0.5f));
	Header->SetRelativeScale3D(CubeScale(Depth, Width, HeaderHeight));

	const float PanelW = bDouble ? ClearWidth * 0.5f : ClearWidth;
	PanelA->SetRelativeScale3D(CubeScale(PanelThickness, PanelW, PanelHeight));
	PanelB->SetRelativeScale3D(CubeScale(PanelThickness, PanelW, PanelHeight));
	PanelB->SetVisibility(bDouble);
	// 손잡이: 문짝을 뚫고 양면으로 4cm씩 나온 검은 세로 막대
	HandleA->SetRelativeScale3D(CubeScale(PanelThickness + 8.f, 2.5f, 55.f));
	HandleB->SetRelativeScale3D(CubeScale(PanelThickness + 8.f, 2.5f, 55.f));
	HandleB->SetVisibility(bDouble);
	LockLight->SetRelativeScale3D(CubeScale(PanelThickness + 6.f, 7.f, 9.f));
	LockLight->SetVisibility(bLocked);

	if (Metal)
	{
		JambL->SetMaterial(0, Metal);
		JambR->SetMaterial(0, Metal);
		Header->SetMaterial(0, Metal);
		HandleA->SetMaterial(0, Metal);
		HandleB->SetMaterial(0, Metal);
	}
	if (Wood)
	{
		PanelA->SetMaterial(0, Wood);
		PanelB->SetMaterial(0, Wood);
	}
	if (Lock)
	{
		LockLight->SetMaterial(0, Lock);
	}
	AppliedAlpha = -1.f;
	ApplyAlpha(bLocked ? 0.f : OpenAlpha);
}

void AVHDoor::ApplyAlpha(float Alpha)
{
	if (Alpha == AppliedAlpha)
	{
		return;
	}
	AppliedAlpha = Alpha;
	const float E = Alpha * Alpha * (3.f - 2.f * Alpha);	// 부드럽게 시작·멈춤
	const float Z = PanelHeight * 0.5f + 0.5f;
	const float HandleZ = 105.f;
	if (bDouble)
	{
		const float Half = ClearWidth * 0.5f;
		const float Travel = Half + JambWidth + 2.f;	// 문틀 뒤 벽 속까지
		const float YA = -Half * 0.5f - Travel * E;
		const float YB = Half * 0.5f + Travel * E;
		PanelA->SetRelativeLocation(FVector(0.f, YA, Z));
		PanelB->SetRelativeLocation(FVector(0.f, YB, Z));
		HandleA->SetRelativeLocation(FVector(0.f, YA + Half * 0.5f - 10.f, HandleZ));
		HandleB->SetRelativeLocation(FVector(0.f, YB - Half * 0.5f + 10.f, HandleZ));
		LockLight->SetRelativeLocation(FVector(0.f, YA + Half * 0.5f - 10.f, HandleZ + 45.f));
	}
	else
	{
		const float S = SlideSign >= 0.f ? 1.f : -1.f;
		const float Y = S * (ClearWidth + JambWidth + 2.f) * E;
		PanelA->SetRelativeLocation(FVector(0.f, Y, Z));
		// 손잡이는 벽에서 먼 쪽 끝 (문짝이 들어가는 반대쪽)
		HandleA->SetRelativeLocation(FVector(0.f, Y - S * (ClearWidth * 0.5f - 12.f), HandleZ));
		LockLight->SetRelativeLocation(FVector(0.f, Y - S * (ClearWidth * 0.5f - 12.f), HandleZ + 45.f));
	}
}

void AVHDoor::BeginPlay()
{
	Super::BeginPlay();
	Layout();
	// 천장 가림의 "내 구역 앞 벽 자르기" 재질로 바꾼다 (자기 위치로는 판단하지 않음: 문은 두 구역 사이에 있다)
	if (UVHCeilingSubsystem* Ceiling = GetWorld() ? GetWorld()->GetSubsystem<UVHCeilingSubsystem>() : nullptr)
	{
		Ceiling->ApplyCutaway(this, 0.f);
	}
	CheckTimer = FMath::FRand() * 0.1f;	// 문마다 확인 시점을 흩뜨린다

	// 문 위치 표시 재질: 표시마다 인스턴스 (색·밝기·모양). 잠긴 문은 빨강, 바닥은 화살표 대신 X
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	if (UMaterialInterface* Mark = C.DoorMarkMaterial.LoadSynchronous())
	{
		const FLinearColor Color = bLocked ? C.DoorMarkLockedColor : C.DoorMarkColor;
		auto Make = [&](UStaticMeshComponent* Comp, float Shape, float Intensity, float Opacity)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Mark, this);
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			MID->SetScalarParameterValue(TEXT("Shape"), Shape);
			MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
			MID->SetScalarParameterValue(TEXT("Opacity"), Opacity);
			Comp->SetMaterial(0, MID);
			return MID;
		};
		StripMID = Make(MarkStrip, 0.f, C.DoorMarkIntensity, C.DoorMarkOpacity);
		ArrowMID = Make(MarkArrow, bLocked ? 2.f : 1.f, C.DoorMarkIntensity, C.DoorMarkOpacity);
		CeilingMID = Make(MarkCeiling, 3.f, C.CeilingDoorMarkIntensity, C.CeilingDoorMarkOpacity);
	}
	MarkSide = 0;
	bCeilingMarkOn = false;
	MarkBoostApplied = -1.f;
}

void AVHDoor::UpdateMarks()
{
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	const UVHCeilingSubsystem* Ceiling = GetWorld() ? GetWorld()->GetSubsystem<UVHCeilingSubsystem>() : nullptr;
	const bool bOn = C.bDoorMarks && CVarVHDoorMarks.GetValueOnGameThread() != 0 && bEnabledApplied && StripMID != nullptr;
	const FName Cur = Ceiling ? Ceiling->GetCurrentZoneId() : NAME_None;

	// 1) 내 구역의 문: 바닥 문턱 띠(벽 가운데 → 내 구역 쪽) + 바깥쪽을 가리키는 화살표 (벽·문이 지워진 카메라 쪽 문에도 보인다)
	int32 Side = 0;
	if (bOn && !Cur.IsNone())
	{
		Side = Cur == ZoneA ? -1 : (Cur == ZoneB ? 1 : 0);
	}
	if (Side != MarkSide)
	{
		MarkSide = Side;
		MarkStrip->SetVisibility(Side != 0);
		MarkArrow->SetVisibility(Side != 0);
		if (Side != 0)
		{
			const float S = static_cast<float>(Side);
			const float Depth = WallThickness * 0.5f + C.DoorMarkStripDepth;
			// 띠: Plane 을 90° 돌려 u = 문 폭, v = 깊이 (모양 0 은 v 방향 가장자리가 부드럽다)
			MarkStrip->SetRelativeLocationAndRotation(FVector(S * Depth * 0.5f, 0.f, 1.5f), FRotator(0.f, 90.f, 0.f));
			MarkStrip->SetRelativeScale3D(FVector((Width - 2.f * JambWidth) / 100.f, Depth / 100.f, 1.f));
			// 화살표: 모양의 +u 가 바깥(다른 구역) 쪽 = 로컬 -S 방향
			MarkArrow->SetRelativeLocationAndRotation(FVector(S * (WallThickness * 0.5f + C.DoorMarkArrowOffset), 0.f, 1.6f), FRotator(0.f, S > 0.f ? 180.f : 0.f, 0.f));
			MarkArrow->SetRelativeScale3D(FVector(C.DoorMarkArrowSize / 100.f, C.DoorMarkArrowSize / 100.f, 1.f));
		}
	}

	// 2) 다른 구역(천장에 덮인 방)끼리의 문: 천장 가장자리(벽 윗면)에 밝은 틈. 덮인 구역 안은 비치지 않는다(벽 위에 얹은 표시)
	//    내 구역의 문도 카메라 쪽 벽(지워지는 벽)이 아니면 벽 위에 틈을 함께 켠다 → 복도에서 어느 방에 들어갈 수 있는지 천장 가장자리로도 보인다
	bool bCameraSide = false;
	if (Side != 0)
	{
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (PC && PC->PlayerCameraManager)
		{
			const FVector Fwd = PC->PlayerCameraManager->GetCameraRotation().Vector().GetSafeNormal2D();
			const FVector ToMine = GetActorForwardVector().GetSafeNormal2D() * static_cast<float>(Side);	// 문 → 내 구역 쪽
			bCameraSide = FVector::DotProduct(Fwd, ToMine) > 0.5f;	// 내 구역이 문 너머(카메라가 보는 쪽)에 있음 = 카메라 쪽 벽의 문
		}
	}
	const bool bCeil = bOn && Ceiling && Ceiling->IsEnabled() && !Cur.IsNone() && (Side == 0 || !bCameraSide);
	if (bCeil != bCeilingMarkOn)
	{
		bCeilingMarkOn = bCeil;
		MarkCeiling->SetVisibility(bCeil);
		if (bCeil)
		{
			const float H = FMath::Max3(Ceiling->GetZoneCeilingHeight(ZoneA), Ceiling->GetZoneCeilingHeight(ZoneB), 398.f);
			MarkCeiling->SetRelativeLocationAndRotation(FVector(0.f, 0.f, H + 5.f), FRotator(0.f, 90.f, 0.f));
			MarkCeiling->SetRelativeScale3D(FVector(Width / 100.f, (WallThickness + C.CeilingDoorMarkExtra) / 100.f, 1.f));
		}
	}

	// 3) 열리면 바닥 표시를 조금 더 밝게
	if (Side != 0)
	{
		const float Boost = 1.f + (C.DoorMarkOpenBoost - 1.f) * OpenAlpha;
		if (Boost != MarkBoostApplied)
		{
			MarkBoostApplied = Boost;
			for (UMaterialInstanceDynamic* MID : { StripMID.Get(), ArrowMID.Get() })
			{
				MID->SetScalarParameterValue(TEXT("Intensity"), C.DoorMarkIntensity * Boost);
				MID->SetScalarParameterValue(TEXT("Opacity"), FMath::Min(1.f, C.DoorMarkOpacity * Boost));
			}
		}
	}
}

bool AVHDoor::IsAnyoneNear() const
{
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	const FTransform& T = GetActorTransform();
	const float HalfY = Width * 0.5f + C.DoorOpenSideMargin;
	auto Near = [&](const FVector& WorldPos)
	{
		const FVector L = T.InverseTransformPosition(WorldPos);
		return FMath::Abs(L.X) <= C.DoorOpenDepth && FMath::Abs(L.Y) <= HalfY && L.Z > -200.f && L.Z < 400.f;
	};
	const UWorld* World = GetWorld();
	if (const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
	{
		if (const APawn* Pawn = PC->GetPawn(); Pawn && Near(Pawn->GetActorLocation()))
		{
			return true;
		}
	}
	for (TActorIterator<AVHNetProxy> It(World); It; ++It)
	{
		if (Near(It->GetActorLocation()))
		{
			return true;
		}
	}
	return false;
}

bool AVHDoor::IsAudible() const
{
	// 천장에 덮인 구역끼리의 문(내가 볼 수 없는 문)은 소리를 내지 않는다 — 벽 너머 상대 위치를 문소리로 알리지 않게
	const UVHCeilingSubsystem* Ceiling = GetWorld() ? GetWorld()->GetSubsystem<UVHCeilingSubsystem>() : nullptr;
	if (!Ceiling)
	{
		return true;
	}
	const FTransform& T = GetActorTransform();
	const float Off = WallThickness * 0.5f + 40.f;
	return !Ceiling->IsLocationCovered(T.TransformPosition(FVector(-Off, 0.f, 0.f)))
		|| !Ceiling->IsLocationCovered(T.TransformPosition(FVector(Off, 0.f, 0.f)));
}

void AVHDoor::ApplyEnabled(bool bEnabled)
{
	if (bEnabled == bEnabledApplied)
	{
		return;
	}
	bEnabledApplied = bEnabled;
	SetActorHiddenInGame(!bEnabled);
}

void AVHDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ApplyEnabled(CVarVHDoors.GetValueOnGameThread() != 0);
	UpdateMarks();
	if (bLocked)
	{
		return;
	}
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	const double Now = GetWorld()->GetTimeSeconds();
	CheckTimer -= DeltaSeconds;
	if (CheckTimer <= 0.f)
	{
		CheckTimer = 0.1f;
		if (IsAnyoneNear())
		{
			LastNearTime = Now;
		}
	}
	const bool bWant = (Now - LastNearTime) <= C.DoorCloseDelay;
	if (bWant != bWantOpen)
	{
		bWantOpen = bWant;
		UE_LOG(LogMCPTest, Verbose, TEXT("[Door] %s %s"), *Describe(), bWant ? TEXT("열림 시작") : TEXT("닫힘 시작"));
	}
	const float Target = bWantOpen ? 1.f : 0.f;
	if (OpenAlpha == Target)
	{
		return;
	}
	const float Speed = 1.f / FMath::Max(0.05f, bWantOpen ? C.DoorOpenSeconds : C.DoorCloseSeconds);
	const float Prev = OpenAlpha;
	OpenAlpha = FMath::FInterpConstantTo(OpenAlpha, Target, DeltaSeconds, Speed);
	if (Prev == 0.f && OpenAlpha > 0.f && IsAudible())
	{
		UVHSoundConfig::Play(this, EVHSound::DoorOpen, GetActorLocation() + FVector(0.f, 0.f, 120.f));
	}
	if (Prev > 0.f && OpenAlpha == 0.f && IsAudible())
	{
		UVHSoundConfig::Play(this, EVHSound::DoorClose, GetActorLocation() + FVector(0.f, 0.f, 120.f));
	}
	ApplyAlpha(OpenAlpha);
}

FString AVHDoor::Describe() const
{
	const FVector P = GetActorLocation();
	return FString::Printf(TEXT("%s (%.0f, %.0f) %s %s↔%s 열림 %.2f%s"), *ServerId.ToString(), P.X, P.Y, bDouble ? TEXT("양문") : TEXT("한 짝"),
		ZoneA.IsNone() ? TEXT("바깥") : *ZoneA.ToString(), ZoneB.IsNone() ? TEXT("바깥") : *ZoneB.ToString(), OpenAlpha, bLocked ? TEXT(" 잠김") : TEXT(""));
}
