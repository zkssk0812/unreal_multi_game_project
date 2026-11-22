#include "Gameplay/VHItemPickup.h"

#include "Gameplay/VHInteractionSubsystem.h"
#include "Gameplay/VHServerGameSubsystem.h"
#include "Gameplay/VHWeaponCatalog.h"
#include "Network/VHSettings.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#define LOCTEXT_NAMESPACE "VHItemPickup"

/** 개발용(vh.ShowPickup 의 배율 인자): 0보다 크면 그 아이템 하나만 DA 의 FloorScale 대신 이 배율로 만든다 */
static float GVHDebugPickupScale = 0.f;
/** 개발용(vh.ShowPickup 의 방식 인자): 0 = 데이터 에셋대로, 1 = 3D 모델(눕힘), 2 = 구상안 탑뷰 그림, 3 = 3D 모델(세움) */
static int32 GVHDebugPickupMode = 0;

AVHItemPickup::AVHItemPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	Kind = EVHEntityKind::WeaponPickup;
	// 밟고 지나갈 수 있게: 이동은 막지 않는다 (필드 아이템은 닿아야 먹는다)
	Body->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Body->SetCastShadow(false);
	PromptAnchor->SetRelativeLocation(FVector(0.f, 0.f, 90.f));
}

void AVHItemPickup::InitPickup(EVHEntityKind InKind, int32 InTypeId, int32 InParam)
{
	Kind = InKind;
	TypeId = InTypeId;
	Param = InParam;
}

void AVHItemPickup::OnConstruction(const FTransform& Transform)
{
	// 무기: DA_VHWeaponCatalog 에 모델이 있으면 그 모델을 바닥 자세로 놓는다 (구상안 v2.2). 없으면 아래 임시 도형
	if (Kind == EVHEntityKind::WeaponPickup)
	{
		const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
		const FVHWeaponVisual* Visual = Catalog.Find(static_cast<uint8>(TypeId));
		// 개발용 덮어쓰기(vh.ShowPickup 의 방식 인자): 1 = 3D 모델(눕힘), 2 = 탑뷰 그림, 3 = 3D 모델(세움)
		const bool bWantArt = Visual && (GVHDebugPickupMode == 2 || (GVHDebugPickupMode == 0 && Visual->FloorMode == EVHWeaponFloorMode::FlatArt));
		UTexture2D* Art = bWantArt ? Visual->FloorArt.LoadSynchronous() : nullptr;
		UMaterialInterface* ArtMaterial = Art ? Catalog.FloorArtMaterial.LoadSynchronous() : nullptr;
		const float Scale = (GVHDebugPickupScale > 0.f ? GVHDebugPickupScale : Catalog.FloorScale) * (Visual ? Visual->FloorScale : 1.f);
		if (Art && ArtMaterial)
		{
			// 구상안 탑뷰 그림을 입힌 납작한 판 (엔진 기본 평면 100×100cm 를 그림 크기로 늘린다). 테두리는 그림의 불투명한 부분을 따라 그려진다
			Body->SetStaticMesh(FindBasicShape(TEXT("Plane")));
			Body->EmptyOverrideMaterials();
			if (UMaterialInstanceDynamic* MID = Body->CreateDynamicMaterialInstance(0, ArtMaterial))
			{
				MID->SetTextureParameterValue(TEXT("Art"), Art);
				MID->SetScalarParameterValue(TEXT("Brightness"), Catalog.FloorArtBrightness);
			}
			const FTransform Floor(FRotator::ZeroRotator, FVector(0.f, 0.f, Catalog.FloorArtHeight),
				FVector(Visual->FloorArtSize.X / 100.f * Scale, Visual->FloorArtSize.Y / 100.f * Scale, 1.f));
			Body->SetRelativeTransform(Floor);
			bWeaponModel = true;
			FloorBase = Floor;
			ModelCenter = FVector::ZeroVector;
			AActor::OnConstruction(Transform);
			return;
		}
		if (UStaticMesh* Mesh = Visual ? Visual->Mesh.LoadSynchronous() : nullptr)
		{
			Body->SetStaticMesh(Mesh);
			Body->EmptyOverrideMaterials();
			for (int32 i = 0; i < Visual->MaterialOverrides.Num() && i < Body->GetNumMaterials(); ++i)
			{
				if (UMaterialInterface* Material = Visual->MaterialOverrides[i].LoadSynchronous())
				{
					Body->SetMaterial(i, Material);
				}
			}
			FTransform Floor = Visual->FloorOffset;
			if (GVHDebugPickupMode == 3)
			{
				Floor.SetRotation(FQuat::Identity);		// 세워서(손에 든 방향 그대로) 놓아 본다
			}
			Floor.SetScale3D(Floor.GetScale3D() * Scale);
			// 모델 보정(회전·크기, 무기 표의 Model 칸)을 먼저 적용한다 (2026-11-20)
			Floor = Visual->MeshToWeapon(Mesh, FVector::OneVector, false) * Floor;
			Body->SetRelativeTransform(Floor);
			// 피벗이 손잡이라 그대로 돌리면 한쪽 끝을 축으로 휘둘린다 → 모델 가운데를 축으로 돌린다 (Tick)
			bWeaponModel = true;
			FloorBase = Floor;
			ModelCenter = Mesh->GetBounds().Origin;
			AActor::OnConstruction(Transform);	// 재질 그대로: 임시 도형용 색 입히기(TintMesh)를 건너뛴다
			return;
		}
	}
	// 소비 아이템: DA_VHWeaponCatalog 의 Consumables 모델 (구상안 Consumables_v1: 실루엣·띠 색 = 종류). 없으면 아래 임시 도형
	if (Kind == EVHEntityKind::ConsumablePickup)
	{
		const FVHConsumableVisual* Visual = UVHSettings::Get().GetWeaponCatalog().FindConsumable(static_cast<uint8>(TypeId));
		if (UStaticMesh* Mesh = Visual ? Visual->Mesh.LoadSynchronous() : nullptr)
		{
			Body->SetStaticMesh(Mesh);
			Body->EmptyOverrideMaterials();
			const FTransform Floor(FRotator::ZeroRotator, FVector(0.f, 0.f, Visual->FloorHeight), FVector(Visual->FloorScale));
			Body->SetRelativeTransform(Floor);
			bWeaponModel = true;	// 모델 가운데를 축으로 제자리에서 돈다 (Tick)
			FloorBase = Floor;
			ModelCenter = Mesh->GetBounds().Origin;
			AActor::OnConstruction(Transform);
			return;
		}
	}
	// 종류별 임시 외형 (메시를 바꾼 블루프린트 자식이면 그대로 둔다)
	if (!Body->GetStaticMesh() || Body->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Engine/BasicShapes/")))
	{
		switch (Kind)
		{
		case EVHEntityKind::ConsumablePickup:
			Body->SetStaticMesh(FindBasicShape(TEXT("Cylinder")));
			Body->SetRelativeScale3D(FVector(0.22f, 0.22f, 0.3f));
			Body->SetRelativeLocation(FVector(0.f, 0.f, 28.f));
			BodyColor = FLinearColor(0.1f, 0.75f, 1.f);
			break;
		case EVHEntityKind::FieldItem:
		{
			// 회복약 소·중·대: 크기로 구분
			const float Scale = TypeId >= 3 ? 0.34f : (TypeId == 2 ? 0.27f : 0.2f);
			Body->SetStaticMesh(FindBasicShape(TEXT("Sphere")));
			Body->SetRelativeScale3D(FVector(Scale));
			Body->SetRelativeLocation(FVector(0.f, 0.f, 26.f));
			BodyColor = FLinearColor(0.15f, 1.f, 0.35f);
			break;
		}
		default:
			Body->SetStaticMesh(FindBasicShape(TEXT("Cube")));
			Body->SetRelativeScale3D(FVector(0.6f, 0.14f, 0.14f));
			Body->SetRelativeLocation(FVector(0.f, 0.f, 26.f));
			BodyColor = FLinearColor(1.f, 0.35f, 0.08f);
			break;
		}
	}
	Super::OnConstruction(Transform);	// BodyColor 로 색을 입힌다
}

void AVHItemPickup::BeginPlay()
{
	Super::BeginPlay();
	BaseBodyZ = Body->GetRelativeLocation().Z;
	TimeOffset = FMath::FRand() * 10.f;
}

void AVHItemPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float T = GetWorld()->GetTimeSeconds() + TimeOffset;
	if (bWeaponModel)
	{
		// 무기 모델: 눕힌 자세 그대로, 모델 가운데를 축으로 제자리에서 돈다
		SpinYaw = FMath::Fmod(SpinYaw + 70.f * DeltaSeconds, 360.f);
		const FQuat Rot = FQuat(FRotator(0.f, SpinYaw, 0.f)) * FloorBase.GetRotation();
		const FVector Center = Rot.RotateVector(ModelCenter * FloorBase.GetScale3D());
		const FVector Base = FloorBase.GetLocation();
		Body->SetRelativeLocationAndRotation(FVector(Base.X - Center.X, Base.Y - Center.Y, Base.Z + FMath::Sin(T * 2.5f) * BobHeight), Rot);
		return;
	}
	FVector Loc = Body->GetRelativeLocation();
	Loc.Z = BaseBodyZ + FMath::Sin(T * 2.5f) * BobHeight;
	Body->SetRelativeLocation(Loc);
	Body->AddRelativeRotation(FRotator(0.f, 70.f * DeltaSeconds, 0.f));
}

FText AVHItemPickup::GetItemName() const
{
	switch (Kind)
	{
	case EVHEntityKind::ConsumablePickup:	return FText::FromString(UVHServerGameSubsystem::ConsumableName(static_cast<RobProto::ConsumableId>(TypeId)));
	case EVHEntityKind::FieldItem:			return FText::FromString(UVHServerGameSubsystem::FieldItemName(static_cast<RobProto::FieldItemId>(TypeId)));
	default:								return FText::FromString(UVHServerGameSubsystem::WeaponName(static_cast<RobProto::WeaponId>(TypeId)));
	}
}

FVHInfoCard AVHItemPickup::GetInfoCard() const
{
	// 기획 그림(오브젝트_상호작용_예시): "권총/무기/대미지 10", "회복 키트/소비/HP 30 회복". 필드 아이템은 이름만
	FVHInfoCard Card;
	switch (Kind)
	{
	case EVHEntityKind::WeaponPickup:
		Card.Text = FText::Format(LOCTEXT("WeaponCard", "{0}/무기/대미지 {1}"), GetItemName(),
			FText::FromString(UVHServerGameSubsystem::WeaponDamageText(static_cast<RobProto::WeaponId>(TypeId))));
		break;
	case EVHEntityKind::ConsumablePickup:
		Card.Text = FText::Format(LOCTEXT("ConsumableCard", "{0}/소비/{1}"), GetItemName(),
			FText::FromString(UVHServerGameSubsystem::ConsumableEffectText(static_cast<RobProto::ConsumableId>(TypeId))));
		// HUD 소비 칸과 같은 디자인: 아이콘 + 발광 띠 색
		if (const FVHConsumableVisual* Visual = UVHSettings::Get().GetWeaponCatalog().FindConsumable(static_cast<uint8>(TypeId)))
		{
			Card.Icon = Visual->Icon.LoadSynchronous();
			Card.Accent = Visual->BandColor;
		}
		// 소비 아이템은 1개만 든다: 같은 종류를 들고 있으면 주워도 바뀌지 않는다고 미리 알려 준다
		if (const UWorld* World = GetWorld())
		{
			if (const UVHServerGameSubsystem* Server = World->GetSubsystem<UVHServerGameSubsystem>(); Server && Server->IsSameAsHeldConsumable(this))
			{
				Card.Text = FText::Format(LOCTEXT("ConsumableCardHeld", "{0} (이미 들고 있음)"), Card.Text);
			}
		}
		break;
	default:
		Card.Text = GetItemName();
		break;
	}
	return Card;
}

#if !UE_BUILD_SHIPPING
namespace
{
	// vh.ShowPickup N [dx] [dy] [배율] : 내 캐릭터에서 월드 좌표로 (dx, dy)cm 떨어진 곳에 N번 무기를 "보기만 하는" 바닥 아이템으로 놓는다.
	// 화면 위 = +X, 화면 오른쪽 = +Y. 배율을 주면 DA 의 FloorScale 대신 그 값을 쓴다. 서버는 모른다 → 주울 수 없다 (모양·배율 확인용)
	FAutoConsoleCommandWithWorldAndArgs GVHShowPickupCommand(TEXT("vh.ShowPickup"),
		TEXT("개발용: vh.ShowPickup N [dx] [dy] [배율] [방식 1=모델 눕힘 2=탑뷰 그림 3=모델 세움] / vh.ShowPickup clear — 내 캐릭터 옆에 N번(1~10) 무기를 바닥 아이템 모양으로 놓는다. 보기 전용."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			if (!Pawn || Args.Num() < 1)
			{
				return;
			}
			if (Args[0] == TEXT("clear"))	// vh.ShowPickup clear : 이 명령으로 놓은 것만 지운다
			{
				for (TActorIterator<AVHItemPickup> It(World); It; ++It)
				{
					if (It->Tags.Contains(TEXT("VHDebugPickup")))
					{
						It->Destroy();
					}
				}
				return;
			}
			// c1 ~ c4 = 소비 아이템 (회복 키트·연막탄·섬광탄·제트팩)
			const bool bConsumable = Args[0].StartsWith(TEXT("c"), ESearchCase::IgnoreCase);
			const int32 Id = bConsumable ? FMath::Clamp(FCString::Atoi(*Args[0].Mid(1)), 1, 4) : FMath::Clamp(FCString::Atoi(*Args[0]), 1, 10);
			const float Dx = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.f;
			const float Dy = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 150.f;
			FVector Location = Pawn->GetActorLocation() + FVector(Dx, Dy, 0.f);
			Location.Z = Pawn->GetActorLocation().Z - Pawn->GetSimpleCollisionHalfHeight();
			const FTransform Transform(FRotator::ZeroRotator, Location);
			if (AVHItemPickup* Item = World->SpawnActorDeferred<AVHItemPickup>(AVHItemPickup::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
			{
				Item->InitPickup(bConsumable ? EVHEntityKind::ConsumablePickup : EVHEntityKind::WeaponPickup, Id, 0);
				Item->Tags.Add(TEXT("VHDebugPickup"));
				GVHDebugPickupScale = Args.Num() > 3 ? FCString::Atof(*Args[3]) : 0.f;
				GVHDebugPickupMode = Args.Num() > 4 ? FCString::Atoi(*Args[4]) : 0;
				Item->FinishSpawning(Transform);
				GVHDebugPickupScale = 0.f;
				GVHDebugPickupMode = 0;
			}
		}));

	// vh.PointAt dx dy : 마우스 커서를 내 캐릭터에서 월드 좌표로 (dx, dy)cm 떨어진 바닥 위(높이 20cm)로 옮긴다 (정보창 확인·촬영용)
	FAutoConsoleCommandWithWorldAndArgs GVHPointAtCommand(TEXT("vh.PointAt"),
		TEXT("개발용: vh.PointAt dx dy — 커서를 내 캐릭터 기준 (dx, dy)cm 바닥 위로 옮긴다. 화면 위 = +X, 오른쪽 = +Y."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			if (!Pawn || Args.Num() < 2)
			{
				return;
			}
			FVector Point = Pawn->GetActorLocation() + FVector(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), 0.f);
			Point.Z = Pawn->GetActorLocation().Z - Pawn->GetSimpleCollisionHalfHeight() + 20.f;
			FVector2D Screen;
			if (PC->ProjectWorldLocationToScreen(Point, Screen))
			{
				PC->SetMouseLocation(FMath::RoundToInt(Screen.X), FMath::RoundToInt(Screen.Y));
				UE_LOG(LogTemp, Log, TEXT("[개발] 커서 → 화면 (%.0f, %.0f)"), Screen.X, Screen.Y);
			}
		}));

	// vh.Consumable N : HUD 소비 칸 표시만 N번(0~4)으로 바꾼다 (아이콘 확인용. 다음 서버 상태가 오면 서버 값)
	FAutoConsoleCommandWithWorldAndArgs GVHConsumableCommand(TEXT("vh.Consumable"),
		TEXT("개발용: vh.Consumable N — HUD 소비 칸 표시를 N번(0 없음, 1 회복 키트, 2 연막탄, 3 섬광탄, 4 제트팩)으로 바꾼다. 판정과 무관."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UVHServerGameSubsystem* Server = World ? World->GetSubsystem<UVHServerGameSubsystem>() : nullptr)
			{
				Server->DebugSetConsumable(static_cast<uint8>(FMath::Clamp(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0, 0, 4)));
			}
		}));

	// vh.Shot 이름 : 화면(HUD 포함)을 Saved/V3Test/이름.png 로 저장 (-game 에서는 HighResShot 이 안 된다)
	FAutoConsoleCommandWithWorldAndArgs GVHShotCommand(TEXT("vh.Shot"),
		TEXT("개발용: vh.Shot 이름 — 화면을 UI 포함으로 Saved/V3Test/이름.png 에 저장한다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			const FString File = FPaths::ProjectSavedDir() / TEXT("V3Test") / ((Args.Num() > 0 ? Args[0] : FString(TEXT("shot"))) + TEXT(".png"));
			FScreenshotRequest::RequestScreenshot(File, true, false);
			UE_LOG(LogTemp, Log, TEXT("[개발] 화면 저장 요청: %s"), *File);
		}));
}
#endif

#undef LOCTEXT_NAMESPACE
