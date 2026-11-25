#include "Gameplay/VHDoor.h"

#include "Gameplay/VHCeilingSubsystem.h"
#include "Gameplay/VHDoorSubsystem.h"
#include "Gameplay/VHGameplayConfig.h"
#include "Gameplay/VHNetProxy.h"
#include "Gameplay/VHSoundConfig.h"
#include "Network/VHSettings.h"
#include "MCPTest.h"

#include "Components/BoxComponent.h"
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
	OutlineProxy = MakePart(TEXT("OutlineProxy"));
	OutlineProxy->SetRenderInMainPass(false);
	OutlineProxy->SetRenderInDepthPass(false);
	OutlineProxy->bVisibleInReflectionCaptures = false;
	OutlineProxy->bVisibleInRealTimeSkyCaptures = false;
	OutlineProxy->bAffectDynamicIndirectLighting = false;
	OutlineProxy->bAffectDistanceFieldLighting = false;
	OutlineProxy->SetVisibleInRayTracing(false);
	MarkStrip = MakeMark(TEXT("MarkStrip"));
	MarkArrow = MakeMark(TEXT("MarkArrow"));
	MarkCeiling = MakeMark(TEXT("MarkCeiling"));
	MarkCeiling->TranslucencySortPriority = 10;	// 천장(불투명) 위에 그린다

	// 닫힌 문 통과 막기: Pawn 채널만 막는 상자 (총알·시야·마우스 조준·카메라·배치 점검(WorldStatic)에는 걸리지 않게 WorldDynamic)
	Blocker = CreateDefaultSubobject<UBoxComponent>(TEXT("Blocker"));
	Blocker->SetupAttachment(SceneRoot);
	Blocker->SetMobility(EComponentMobility::Movable);
	Blocker->SetCollisionObjectType(ECC_WorldDynamic);
	Blocker->SetCollisionResponseToAllChannels(ECR_Ignore);
	Blocker->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Blocker->SetGenerateOverlapEvents(false);
	Blocker->SetCanEverAffectNavigation(false);
	Blocker->SetHiddenInGame(true);
	Blocker->CanCharacterStepUpOn = ECB_No;
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
	// 테두리 전용 문 자리 상자 = 닫힌 문짝 전체 크기 (열린 문이 빛날 때 문짝 대신)
	OutlineProxy->SetRelativeLocation(FVector(0.f, 0.f, PanelHeight * 0.5f + 0.5f));
	OutlineProxy->SetRelativeScale3D(CubeScale(PanelThickness, ClearWidth, PanelHeight));
	LockLight->SetVisibility(bLocked);
	// 통과 막기 상자: 문틀 안쪽 구멍 전체 (벽 두께 + 앞뒤 2cm)
	Blocker->SetRelativeLocation(FVector(0.f, 0.f, OpeningHeight * 0.5f));
	Blocker->SetBoxExtent(FVector(WallThickness * 0.5f + 2.f, Width * 0.5f, OpeningHeight * 0.5f));

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
	// 처음 상태 (지금은 모두 닫힘) — 상태 출처(UVHDoorSubsystem)에게 묻는다
	if (UVHDoorSubsystem* Doors = GetWorld() ? GetWorld()->GetSubsystem<UVHDoorSubsystem>() : nullptr)
	{
		bWantOpen = Doors->GetInitialOpen(*this);
	}
	OpenAlpha = bWantOpen ? 1.f : 0.f;
	AppliedAlpha = -1.f;
	ApplyAlpha(OpenAlpha);
	UpdateBlocker();

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

	// 3) [기획 확인 2026-11-25] 열림/닫힘에 따른 색·밝기 차이 없음 — BeginPlay 에서 정한 한 가지 색(잠긴 문만 빨강) 그대로.
	//    (예전 2026-11-24: 열린 문 하늘색·밝게 / 닫힌 문 흰 회색·어둡게 → 문 상태로 남의 위치가 드러나서 뺌)
}

void AVHDoor::SetFocusOutline(bool bOn)
{
	if (bOn == bFocusOutline)
	{
		return;
	}
	bFocusOutline = bOn;
	ApplyOutlineParts();
}

void AVHDoor::ApplyOutlineParts()
{
	// 상자·금고·환금 아이템과 같은 테두리: 커스텀 뎁스 + 스텐실 2(Focus) → M_VHOutline 이 가장자리에 밝은 노랑을 칠한다.
	// 문짝은 닫혀 보일 때(반 넘게 닫힘)만 — 열리면 문짝이 벽 속에 들어가 있어 테두리가 벽 너머로 사선처럼 그려졌다 → 열린 문은 문틀 + 화면에 안 그려지는 "문 자리" 상자(OutlineProxy)
	const bool bPanels = OpenAlpha < 0.5f;
	OutlinePanelsApplied = bPanels ? 1 : 0;
	const int32 Stencil = 2;
	auto Set = [Stencil](UStaticMeshComponent* Part, bool bOn)
	{
		if (Part && Part->bRenderCustomDepth != bOn)
		{
			Part->SetRenderCustomDepth(bOn);
			Part->SetCustomDepthStencilValue(bOn ? Stencil : 0);
		}
	};
	for (UStaticMeshComponent* Part : { JambL.Get(), JambR.Get(), Header.Get() })
	{
		Set(Part, bFocusOutline);
	}
	Set(OutlineProxy.Get(), bFocusOutline && !bPanels);	// 열린 문: 닫힌 문짝 자리 모양으로 (화면에는 안 그려지는 상자)
	for (UStaticMeshComponent* Part : { PanelA.Get(), PanelB.Get(), HandleA.Get(), HandleB.Get() })
	{
		Set(Part, bFocusOutline && bPanels);
	}
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

void AVHDoor::SetOpenState(bool bOpen)
{
	if (bLocked)
	{
		return;
	}
	bWantOpen = bOpen;
}

bool AVHDoor::GetAperture(float& OutY0, float& OutY1) const
{
	// 문짝이 비켜난 만큼 뚫린 틈 (ApplyAlpha 와 같은 계산)
	const float Alpha = bEnabledApplied ? OpenAlpha : 1.f;
	if (bLocked || Alpha <= 0.f)
	{
		return false;
	}
	const float E = Alpha * Alpha * (3.f - 2.f * Alpha);
	const float Half = ClearWidth * 0.5f;
	if (bDouble)
	{
		const float Travel = Half + JambWidth + 2.f;
		const float G = FMath::Min(Half, Travel * E);
		OutY0 = -G;
		OutY1 = G;
	}
	else
	{
		const float S = SlideSign >= 0.f ? 1.f : -1.f;
		const float Y = S * (ClearWidth + JambWidth + 2.f) * E;	// 문짝 가운데
		if (S > 0.f)
		{
			OutY0 = -Half;
			OutY1 = FMath::Min(Half, Y - Half);
		}
		else
		{
			OutY0 = FMath::Max(-Half, Y + Half);
			OutY1 = Half;
		}
	}
	return OutY1 - OutY0 > 2.f;
}

FVector AVHDoor::GetPromptLocation(const FVector& From) const
{
	const FTransform& T = GetActorTransform();
	const float Side = T.InverseTransformPosition(From).X >= 0.f ? 1.f : -1.f;
	return T.TransformPosition(FVector(Side * (WallThickness * 0.5f + 25.f), 0.f, 160.f));
}

void AVHDoor::UpdateBlocker()
{
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	// 닫힘·덜 열림이면 막는다. vh.Doors 0(숨김)이면 막지 않는다. 잠긴 바깥 문은 원래 판(ClosedDoor) 충돌도 남아 있다
	const bool bBlock = C.bDoorBlockClosed && bEnabledApplied && (bLocked || OpenAlpha < C.DoorPassAlpha);
	if (bBlock != bBlocking)
	{
		bBlocking = bBlock;
		Blocker->SetCollisionEnabled(bBlock ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
}

void AVHDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ApplyEnabled(CVarVHDoors.GetValueOnGameThread() != 0);
	UpdateMarks();
	UpdateBlocker();
	if (bLocked)
	{
		return;
	}
	// 상태(bWantOpen)는 UVHDoorSubsystem 이 정한다. 여기서는 문짝을 그 상태로 움직이고 소리만 낸다
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
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
	UpdateBlocker();
	if (bFocusOutline && OutlinePanelsApplied != (OpenAlpha < 0.5f ? 1 : 0))
	{
		ApplyOutlineParts();	// 문짝이 벽 속으로 들어가거나 나오면 문짝 테두리를 끄고 켠다
	}
}

FString AVHDoor::Describe() const
{
	const FVector P = GetActorLocation();
	return FString::Printf(TEXT("%s (%.0f, %.0f) %s %s↔%s 열림 %.2f%s%s"), *ServerId.ToString(), P.X, P.Y, bDouble ? TEXT("양문") : TEXT("한 짝"),
		ZoneA.IsNone() ? TEXT("바깥") : *ZoneA.ToString(), ZoneB.IsNone() ? TEXT("바깥") : *ZoneB.ToString(), OpenAlpha, bLocked ? TEXT(" 잠김") : TEXT(""),
		ServerEntityId != 0 ? *FString::Printf(TEXT(" [서버 문 %d, 엔티티 %u]"), ServerDoorIndex, ServerEntityId) : TEXT(""));
}

bool AVHDoor::IntersectPanel(const FVector& From, const FVector& To, float& OutT) const
{
	// 문 로컬 상자 x ∈ [-벽 두께/2, +벽 두께/2], y ∈ [-폭/2, +폭/2], z ∈ [0, 입구 높이] 와 선분의 교차 (slab)
	const FTransform& T = GetActorTransform();
	const FVector A = T.InverseTransformPosition(From);
	const FVector B = T.InverseTransformPosition(To);
	const FVector D = B - A;
	const FVector Min(-WallThickness * 0.5f, -Width * 0.5f, 0.f);
	const FVector Max(WallThickness * 0.5f, Width * 0.5f, OpeningHeight);
	float T0 = 0.f, T1 = 1.f;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (FMath::Abs(D[Axis]) < KINDA_SMALL_NUMBER)
		{
			if (A[Axis] < Min[Axis] || A[Axis] > Max[Axis])
			{
				return false;
			}
			continue;
		}
		float Ta = (Min[Axis] - A[Axis]) / D[Axis];
		float Tb = (Max[Axis] - A[Axis]) / D[Axis];
		if (Ta > Tb)
		{
			Swap(Ta, Tb);
		}
		T0 = FMath::Max(T0, Ta);
		T1 = FMath::Min(T1, Tb);
		if (T0 > T1)
		{
			return false;
		}
	}
	OutT = T0;
	return true;
}
