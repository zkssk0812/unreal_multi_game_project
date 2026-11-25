#include "Gameplay/VHCeilingSubsystem.h"

#include "Gameplay/VHRoomVolume.h"
#include "Gameplay/VHNetProxy.h"
#include "Gameplay/VHInteractableBase.h"
#include "Gameplay/VHFxActor.h"
#include "Gameplay/VHDoor.h"
#include "Gameplay/VHGameplayConfig.h"
#include "Gameplay/VHAimComponent.h"
#include "Network/VHSettings.h"
#include "MCPTest.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Canvas.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraComponent.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "Algo/BinarySearch.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace
{
	TAutoConsoleVariable<int32> CVarVHCeiling(TEXT("vh.Ceiling"), 1,
		TEXT("천장 가림(방 단위 시야): 1 = 켬(기본), 0 = 끔. 내 구역만 보이고 다른 구역은 회색 천장으로 덮는다."));
	TAutoConsoleVariable<int32> CVarVHCeilingCut(TEXT("vh.CeilingCut"), 1,
		TEXT("내 구역 앞 가림 지우기: 1 = 켬(기본), 0 = 끔(예전처럼 옆 구역 천장·벽이 내 구역 바닥을 가림). 비교용."));
	TAutoConsoleVariable<int32> CVarVHDoorPeek(TEXT("vh.DoorPeek"), 1,
		TEXT("열린 문 너머 엿보기(부채꼴 시야): 1 = 켬(기본), 0 = 끔(열린 문 너머도 덮임)."));
	TAutoConsoleVariable<float> CVarVHCeilingCutZ(TEXT("vh.CeilingCutZ"), -10000.f,
		TEXT("내 구역 판단 높이(cm) 덮어쓰기: 0 = 바닥, 90 = 허리. -10000 = 설정값(DA CeilingCutPlaneZ) 사용."));

	// 조각 두 개가 변(길이 있는 선)으로 맞닿거나 겹치는가 (모서리 한 점만 닿는 것은 이어진 것으로 치지 않는다)
	bool RectsTouch(const FBox2D& A, const FBox2D& B, double Tol = 1.0)
	{
		const double Ox = FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X);
		const double Oy = FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y);
		return (Ox > -Tol && Oy > Tol) || (Oy > -Tol && Ox > Tol);
	}

	// vh.CeilingGo X Y : 내 캐릭터를 월드 좌표 (X, Y)로 순간이동 (높이는 그대로). 천장 시험 화면용
	FAutoConsoleCommandWithWorldAndArgs GVHCeilingGoCommand(TEXT("vh.CeilingGo"),
		TEXT("개발용: vh.CeilingGo X Y — 내 캐릭터를 월드 좌표 (X, Y)cm 로 순간이동한다 (천장 가림 시험용)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			if (!Pawn || Args.Num() < 1)
			{
				return;
			}
			// vh.CeilingGo guard [거리] : 가장 가까운 경비원 옆(기본 1.2m, 화면 아래쪽 = -X)으로 (경비원이 같은 구역에서 보이는지 확인용)
			if (Args[0].Equals(TEXT("guard"), ESearchCase::IgnoreCase))
			{
				const float Gap = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 120.f;
				const AVHNetProxy* Best = nullptr;
				for (TActorIterator<AVHNetProxy> It(World); It; ++It)
				{
					if (It->IsGuard() && (!Best || FVector::Dist2D(It->GetActorLocation(), Pawn->GetActorLocation()) < FVector::Dist2D(Best->GetActorLocation(), Pawn->GetActorLocation())))
					{
						Best = *It;
					}
				}
				if (Best)
				{
					const FVector G = Best->GetActorLocation();
					Pawn->SetActorLocation(FVector(G.X - Gap, G.Y, Pawn->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
					UE_LOG(LogMCPTest, Log, TEXT("[Ceiling] 순간이동: 경비원 %s (%.0f, %.0f) 옆"), *Best->GetDisplayName(), G.X, G.Y);
				}
				return;
			}
			if (Args.Num() < 2)
			{
				return;
			}
			const FVector To(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), Pawn->GetActorLocation().Z);
			Pawn->SetActorLocation(To, false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogMCPTest, Log, TEXT("[Ceiling] 순간이동 (%.0f, %.0f)"), To.X, To.Y);
		}));

	// vh.CeilingShots 장수 간격 [이름] : 게임 시간 간격(초)으로 화면을 연속 저장 → Saved/V3Test/<이름>_NN.png (slomo 와 함께 쓰면 촘촘하게)
	FAutoConsoleCommandWithWorldAndArgs GVHCeilingShotsCommand(TEXT("vh.CeilingShots"),
		TEXT("개발용: vh.CeilingShots 장수 간격 [이름] — 게임 시간 간격(초)마다 화면을 저장한다 (Saved/V3Test/이름_NN.png)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			const int32 Count = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 60) : 6;
			const float Interval = Args.Num() > 1 ? FMath::Max(0.01f, FCString::Atof(*Args[1])) : 0.1f;
			const FString Name = Args.Num() > 2 ? Args[2] : TEXT("ceiling");
			for (int32 i = 0; i < Count; ++i)
			{
				const FString File = FPaths::ProjectSavedDir() / TEXT("V3Test") / FString::Printf(TEXT("%s_%02d.png"), *Name, i);
				TWeakObjectPtr<UWorld> WeakWorld(World);
				FTimerHandle Handle;
				World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([File, WeakWorld, i]()
				{
					FScreenshotRequest::RequestScreenshot(File, true, false);
					const UVHCeilingSubsystem* Ceiling = WeakWorld.IsValid() ? WeakWorld->GetSubsystem<UVHCeilingSubsystem>() : nullptr;
					UE_LOG(LogMCPTest, Log, TEXT("[Ceiling] 화면 저장 %d %s | %s"), i, *FPaths::GetCleanFilename(File), Ceiling ? *Ceiling->Describe() : TEXT("-"));
				}), FMath::Max(0.001f, Interval * i), false);
			}
		}));

	// vh.DoorPeekInfo : 엿보기 상태
	FAutoConsoleCommandWithWorldAndArgs GVHDoorPeekInfoCommand(TEXT("vh.DoorPeekInfo"),
		TEXT("개발용: vh.DoorPeekInfo — 열린 문 너머 엿보기(문마다 너머 구역·각도·광선 길이)를 로그에 쓴다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UVHCeilingSubsystem* Ceiling = World ? World->GetSubsystem<UVHCeilingSubsystem>() : nullptr)
			{
				UE_LOG(LogMCPTest, Log, TEXT("[Peek] %s"), *Ceiling->DescribePeek());
				if (Args.Num() > 0 && Args[0].Equals(TEXT("reset"), ESearchCase::IgnoreCase))
				{
					Ceiling->ResetPeekStats();
				}
			}
		}));

	// vh.AimSweep 시작각 끝각 초 [반복] : 조준 방향을 시작각 → 끝각으로 초 동안 고르게 돌린다(마우스를 돌린 것처럼, 끝나면 그 방향에 고정).
	// 문 너머 시야가 바라보는 방향에 따라 부드럽게 바뀌는지 연속 프레임 시험용. vh.Aim off 로 풀기
	FAutoConsoleCommandWithWorldAndArgs GVHAimSweepCommand(TEXT("vh.AimSweep"),
		TEXT("개발용: vh.AimSweep 시작각 끝각 초 [왕복 횟수] — 조준 방향을 고르게 돌린다(0 = 화면 위, 90 = 오른쪽). vh.Aim off 로 풀기."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			UVHAimComponent* Aim = Pawn ? Pawn->FindComponentByClass<UVHAimComponent>() : nullptr;
			if (!Aim || Args.Num() < 3)
			{
				return;
			}
			const float A0 = FCString::Atof(*Args[0]), A1 = FCString::Atof(*Args[1]);
			const float Dur = FMath::Max(0.05f, FCString::Atof(*Args[2]));
			const int32 Legs = Args.Num() > 3 ? FMath::Clamp(FCString::Atoi(*Args[3]), 1, 200) : 1;
			const double Start = World->GetTimeSeconds();
			TWeakObjectPtr<UVHAimComponent> WeakAim(Aim);
			TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
			UE_LOG(LogMCPTest, Log, TEXT("[Peek] 조준 돌리기 %.0f° → %.0f° (%.2f초 × %d)"), A0, A1, Dur, Legs);
			World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakAim, A0, A1, Dur, Legs, Start, Handle]()
			{
				UVHAimComponent* A = WeakAim.Get();
				UWorld* W = A ? A->GetWorld() : nullptr;
				if (!W)
				{
					return;
				}
				const float T = static_cast<float>(W->GetTimeSeconds() - Start) / Dur;
				const int32 Leg = FMath::Min(FMath::FloorToInt(T), Legs - 1);
				float F = FMath::Clamp(T - Leg, 0.f, 1.f);
				if (Leg % 2 == 1)
				{
					F = 1.f - F;
				}
				A->SetTurnLocked(false);
				A->SnapLookYaw(FMath::Lerp(A0, A1, F));
				A->SetTurnLocked(true);
				if (T >= Legs)
				{
					W->GetTimerManager().ClearTimer(*Handle);
					UE_LOG(LogMCPTest, Log, TEXT("[Peek] 조준 돌리기 끝 (%.0f°)"), Legs % 2 == 1 ? A1 : A0);
				}
			}), 0.001f, true);
		}));

	// vh.CeilingInfo : 지금 상태 (내 구역·숨긴 물체 수)
	FAutoConsoleCommandWithWorldAndArgs GVHCeilingInfoCommand(TEXT("vh.CeilingInfo"),
		TEXT("개발용: vh.CeilingInfo — 천장 가림 상태(내 구역, 덮인 구역, 숨긴 물체 수)를 로그에 쓴다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (const UVHCeilingSubsystem* Ceiling = World ? World->GetSubsystem<UVHCeilingSubsystem>() : nullptr)
			{
				UE_LOG(LogMCPTest, Log, TEXT("[Ceiling] %s"), *Ceiling->Describe());
				// 다른 플레이어·경비원·물체마다: 있는 구역, 천장 숨김, 이름표(이름·HP 글자)가 그려지는가
				for (TActorIterator<AVHNetProxy> It(World); It; ++It)
				{
					const FVector P = It->GetActorLocation();
					UE_LOG(LogMCPTest, Log, TEXT("[Ceiling]   %s %s (%.0f, %.0f) 구역 %s: %s, 이름표 %s"),
						It->IsGuard() ? TEXT("경비원") : TEXT("플레이어"), *It->GetDisplayName(), P.X, P.Y, *Ceiling->GetZoneLabelAt(P),
						Ceiling->IsCovered(*It) ? TEXT("천장에 가려 숨김") : TEXT("보임"), It->IsLabelDrawn() ? TEXT("그려짐") : TEXT("안 그려짐"));
				}
				for (TActorIterator<AVHInteractableBase> It(World); It; ++It)
				{
					if (It->IsHidden())
					{
						continue;	// 원래 숨긴 것(쓰지 않는 금고 후보)
					}
					const FVector P = It->GetActorLocation();
					UE_LOG(LogMCPTest, Log, TEXT("[Ceiling]   물체 %s (%.0f, %.0f) 구역 %s: %s"), *It->GetName(), P.X, P.Y, *Ceiling->GetZoneLabelAt(P),
						Ceiling->IsCovered(*It) ? TEXT("천장에 가려 숨김") : TEXT("보임"));
				}
			}
		}));
}

bool UVHCeilingSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UVHCeilingSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UVHCeilingSubsystem, STATGROUP_Tickables);
}

void UVHCeilingSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	BuildZones();
}

void UVHCeilingSubsystem::Deinitialize()
{
	Zones.Reset();
	Hidden.Reset();
	CutMIDs.Reset();
	CutMIDRefs.Reset();
	ZoneTexture = nullptr;
	PeekTarget = nullptr;
	Wedges.Reset();
	DoorList.Reset();
	ZoneMapIds.Reset();
	Super::Deinitialize();
}

bool UVHCeilingSubsystem::IsEnabled() const
{
	return CVarVHCeiling.GetValueOnGameThread() != 0 && UVHSettings::Get().GetGameplayConfig().bCeiling;
}

void UVHCeilingSubsystem::BuildZones()
{
	bBuilt = true;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();

	// 1) 구역: 서버 방 id 가 있는 방 영역. 한 방의 조각이 떨어져 있으면 이어진 것끼리 나눈다
	for (TActorIterator<AVHRoomVolume> It(World); It; ++It)
	{
		const AVHRoomVolume* Room = *It;
		if (Room->ServerRoomId.IsNone())
		{
			continue;
		}
		TArray<FBox2D> Rects;
		Room->GetRoomRects(Rects);
		const int32 N = Rects.Num();
		TArray<int32> Parent;
		Parent.SetNum(N);
		for (int32 i = 0; i < N; ++i)
		{
			Parent[i] = i;
		}
		auto Find = [&Parent](int32 i) { while (Parent[i] != i) { i = Parent[i] = Parent[Parent[i]]; } return i; };
		for (int32 i = 0; i < N; ++i)
		{
			for (int32 j = i + 1; j < N; ++j)
			{
				if (RectsTouch(Rects[i], Rects[j]))
				{
					Parent[Find(i)] = Find(j);
				}
			}
		}
		TMap<int32, TArray<FBox2D>> Groups;
		for (int32 i = 0; i < N; ++i)
		{
			Groups.FindOrAdd(Find(i)).Add(Rects[i]);
		}
		int32 Part = 0;
		for (TPair<int32, TArray<FBox2D>>& G : Groups)
		{
			FZone& Z = Zones.AddDefaulted_GetRef();
			++Part;
			Z.Id = Groups.Num() > 1 ? FName(*FString::Printf(TEXT("%s_%d"), *Room->ServerRoomId.ToString(), Part)) : Room->ServerRoomId;
			Z.Label = Groups.Num() > 1 ? FString::Printf(TEXT("%s %d"), *Room->RoomName.ToString(), Part) : Room->RoomName.ToString();
			Z.Rects = MoveTemp(G.Value);
			Z.Height = Room->GetRoomBox().Max.Z + C.CeilingHeightOffset;
			for (const FBox2D& R : Z.Rects)
			{
				const FBox2D W(R.Min - FVector2D(C.CeilingExpand), R.Max + FVector2D(C.CeilingExpand));
				Z.Wide.Add(W);
				Z.Bounds += W;
			}
			FillSmallHoles(Z);
		}
	}
	if (Zones.Num() == 0)
	{
		return;	// 방 영역이 없는 레벨(메뉴·시험 레벨)
	}
	MeasureWallHeights();
	BuildZoneTexture();

	// 2) 천장 메시: 구역마다 인스턴스 메시 하나 (조각마다 엔진 Plane 100x100 을 늘린 인스턴스) + 재질 인스턴스
	UMaterialInterface* Material = C.CeilingMaterial.LoadSynchronous();
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (!Material || !Plane)
	{
		UE_LOG(LogMCPTest, Warning, TEXT("[Ceiling] 천장 재질(%s) 또는 Plane 메시가 없어 천장 가림을 쓰지 않음"), *C.CeilingMaterial.ToString());
		Zones.Reset();
		return;
	}
	FActorSpawnParameters Params;
	Params.Name = TEXT("VHCeiling");
	Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
	CeilingActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	USceneComponent* Root = NewObject<USceneComponent>(CeilingActor, TEXT("Root"));
	CeilingActor->SetRootComponent(Root);
	Root->RegisterComponent();
	int32 Pieces = 0;
	TArray<FString> Names;
	for (int32 ZoneIndex = 0; ZoneIndex < Zones.Num(); ++ZoneIndex)
	{
		FZone& Z = Zones[ZoneIndex];
		UInstancedStaticMeshComponent* M = NewObject<UInstancedStaticMeshComponent>(CeilingActor, *FString::Printf(TEXT("Ceiling_%s"), *Z.Id.ToString()));
		M->SetupAttachment(Root);
		M->SetMobility(EComponentMobility::Movable);
		M->SetStaticMesh(Plane);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetCanEverAffectNavigation(false);
		// 맵 밝기를 바꾸지 않게: 그림자·간접광·거리장·반사·레이트레이싱에 끼지 않는다
		M->SetCastShadow(false);
		M->bAffectDynamicIndirectLighting = false;
		M->bAffectDistanceFieldLighting = false;
		M->bVisibleInReflectionCaptures = false;
		M->bVisibleInRealTimeSkyCaptures = false;
		M->SetVisibleInRayTracing(false);
		M->bReceivesDecals = false;
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Material, M);
		MID->SetVectorParameterValue(TEXT("Color"), C.CeilingColor);
		MID->SetScalarParameterValue(TEXT("Opacity"), 1.f);
		SetCutParams(MID, static_cast<float>(ZoneIndex + 1));	// 자기 구역 천장은 자기 페이드로만 사라진다
		M->SetMaterial(0, MID);
		M->RegisterComponent();
		CeilingActor->AddInstanceComponent(M);
		for (const FBox2D& W : Z.Wide)
		{
			const FVector2D Center = W.GetCenter();
			const FVector2D Size = W.GetSize();
			M->AddInstance(FTransform(FRotator::ZeroRotator, FVector(Center.X, Center.Y, Z.Height), FVector(Size.X / 100.0, Size.Y / 100.0, 1.0)), true);
			++Pieces;
		}
		Z.Mesh = M;
		Z.Material = MID;
		Names.Add(FString::Printf(TEXT("%s(%d)"), *Z.Label, Z.Rects.Num()));
	}
	UE_LOG(LogMCPTest, Log, TEXT("[Ceiling] 구역 %d개, 천장 조각 %d개, 높이 %.0f: %s"), Zones.Num(), Pieces, Zones[0].Height, *FString::Join(Names, TEXT(", ")));

	// 내 구역 앞 가림 지우기: 문간 가림막, 구역별 소품, 벽·기둥·창틀·문 재질 바꾸기
	AddDoorCurtains();
	CollectProps();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (*It != CeilingActor)
		{
			ApplyCutaway(*It, It->IsA<AVHDoor>() ? 0.f : -1.f);
		}
	}
	TArray<FString> CutNames;
	for (const FCutMID& M : CutMIDs)
	{
		CutNames.Add(FString::Printf(TEXT("%s(%s)"), M.Base.IsValid() ? *M.Base->GetName() : TEXT("?"), M.SelfZone < 0.f ? TEXT("벽") : TEXT("문")));
	}
	UE_LOG(LogMCPTest, Log, TEXT("[Ceiling] 앞 가림 지우기 재질 %d개: %s"), CutMIDs.Num(), *FString::Join(CutNames, TEXT(", ")));

	// 처음 구역은 바로 걷는다 (시작할 때 사라지는 연출 없이)
	UpdateCurrentZone();
	for (int32 i = 0; i < Zones.Num(); ++i)
	{
		Zones[i].Opacity = (IsEnabled() && i != Current) ? 1.f : 0.f;
	}
	UpdateFade(0.f);
	UpdateCutParams(true);
	UpdateProps();
}

void UVHCeilingSubsystem::FillSmallHoles(FZone& Z)
{
	// 서버 방 조각은 기둥 자리(100x100)를 비워 둔다 → 천장에 구멍이 보인다. 조각 경계 좌표로 격자를 만들어
	// 바깥과 이어지지 않은 빈칸 덩어리 중 작은 것(2m 이하)만 메운다 (복도 고리 안쪽의 방·홀 같은 큰 빈칸은 그대로)
	TArray<double> Xs, Ys;
	for (const FBox2D& W : Z.Wide)
	{
		Xs.AddUnique(W.Min.X); Xs.AddUnique(W.Max.X);
		Ys.AddUnique(W.Min.Y); Ys.AddUnique(W.Max.Y);
	}
	Xs.Sort();
	Ys.Sort();
	const int32 Nx = Xs.Num() - 1, Ny = Ys.Num() - 1;
	if (Nx <= 0 || Ny <= 0)
	{
		return;
	}
	auto Idx = [Ny](int32 i, int32 j) { return i * Ny + j; };
	TArray<uint8> Cell;	// 0 = 빈칸, 1 = 덮임, 2 = 확인함
	Cell.SetNumZeroed(Nx * Ny);
	for (int32 i = 0; i < Nx; ++i)
	{
		for (int32 j = 0; j < Ny; ++j)
		{
			const FVector2D Mid((Xs[i] + Xs[i + 1]) * 0.5, (Ys[j] + Ys[j + 1]) * 0.5);
			for (const FBox2D& W : Z.Wide)
			{
				if (W.IsInside(Mid))
				{
					Cell[Idx(i, j)] = 1;
					break;
				}
			}
		}
	}
	int32 Filled = 0;
	for (int32 i = 0; i < Nx; ++i)
	{
		for (int32 j = 0; j < Ny; ++j)
		{
			if (Cell[Idx(i, j)] != 0)
			{
				continue;
			}
			// 빈칸 덩어리 하나 모으기
			TArray<FIntPoint> Blob, Stack = { FIntPoint(i, j) };
			Cell[Idx(i, j)] = 2;
			bool bOpen = false;
			FBox2D Box(ForceInit);
			while (Stack.Num() > 0)
			{
				const FIntPoint P = Stack.Pop(EAllowShrinking::No);
				Blob.Add(P);
				Box += FVector2D(Xs[P.X], Ys[P.Y]);
				Box += FVector2D(Xs[P.X + 1], Ys[P.Y + 1]);
				const FIntPoint Nb[4] = { {P.X + 1, P.Y}, {P.X - 1, P.Y}, {P.X, P.Y + 1}, {P.X, P.Y - 1} };
				for (const FIntPoint& Q : Nb)
				{
					if (Q.X < 0 || Q.Y < 0 || Q.X >= Nx || Q.Y >= Ny)
					{
						bOpen = true;	// 격자 밖과 이어짐 = 구역 바깥
						continue;
					}
					if (Cell[Idx(Q.X, Q.Y)] == 0)
					{
						Cell[Idx(Q.X, Q.Y)] = 2;
						Stack.Add(Q);
					}
				}
			}
			const FVector2D Size = Box.GetSize();
			if (!bOpen && Size.X <= 200.0 && Size.Y <= 200.0)
			{
				for (const FIntPoint& P : Blob)
				{
					Z.Wide.Add(FBox2D(FVector2D(Xs[P.X], Ys[P.Y]), FVector2D(Xs[P.X + 1], Ys[P.Y + 1])));
				}
				++Filled;
			}
		}
	}
	if (Filled > 0)
	{
		UE_LOG(LogMCPTest, Log, TEXT("[Ceiling] %s: 안쪽 작은 빈칸(기둥 자리) %d곳 메움"), *Z.Label, Filled);
	}
}

void UVHCeilingSubsystem::MeasureWallHeights()
{
	// 구역 둘레 벽의 윗면 높이를 잰다: 조각 가장자리 바깥 10~40cm(벽 속)를 위에서 아래로 쏜다 (숨긴 액터 = 원래 천장·조명 기구는 건너뜀).
	// 점마다 가장 높은 값, 구역은 그 중앙값 (문 자리 = 바닥은 뺀다). 중앙 홀처럼 벽이 높은(800) 구역은 천장도 높다.
	// 다른 구역과 맞닿은 조각 변도 재지만, 벽이 계단 모양(홀 쪽 800·복도 쪽 400)이라 가까운 쪽 높이가 나온다
	UWorld* World = GetWorld();
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VHCeilingMeasure), true);
	auto TopAt = [World, &Params](double X, double Y) -> float
	{
		// 숨긴 액터(원래 천장 메시·조명 기구)·캐릭터에 맞으면 무시 목록에 넣고 다시 쏜다 (막는 물체에서 추적이 멈추므로)
		for (int32 Try = 0; Try < 8; ++Try)
		{
			FHitResult H;
			if (!World->LineTraceSingleByChannel(H, FVector(X, Y, 2000.0), FVector(X, Y, -10.0), ECC_Visibility, Params))
			{
				return 0.f;
			}
			const AActor* A = H.GetActor();
			const UPrimitiveComponent* P = H.GetComponent();
			if ((A && (A->IsHidden() || A->IsA<APawn>())) || (P && (!P->IsVisible() || P->bHiddenInGame)))
			{
				if (A)
				{
					Params.AddIgnoredActor(A);
				}
				else
				{
					Params.AddIgnoredComponent(P);
				}
				continue;
			}
			return static_cast<float>(H.ImpactPoint.Z);
		}
		return 0.f;
	};
	int32 Traces = 0;
	TArray<FString> Tall;
	for (FZone& Z : Zones)
	{
		TArray<float> Tops;
		auto Sample = [&](double X, double Y, double Nx, double Ny)
		{
			float Best = 0.f;
			for (double Off = 10.0; Off <= 40.0; Off += 10.0)
			{
				Best = FMath::Max(Best, TopAt(X + Nx * Off, Y + Ny * Off));
				++Traces;
			}
			if (Best > 200.f)
			{
				Tops.Add(Best);
			}
		};
		for (const FBox2D& R : Z.Rects)
		{
			for (double X = R.Min.X + 25.0; X < R.Max.X; X += 50.0)
			{
				Sample(X, R.Min.Y, 0, -1);
				Sample(X, R.Max.Y, 0, 1);
			}
			for (double Y = R.Min.Y + 25.0; Y < R.Max.Y; Y += 50.0)
			{
				Sample(R.Min.X, Y, -1, 0);
				Sample(R.Max.X, Y, 1, 0);
			}
		}
		if (Tops.Num() >= 4)
		{
			Tops.Sort();
			const float Median = Tops[Tops.Num() / 2];
			Z.Height = Median + C.CeilingHeightOffset;
			if (Median > 450.f)
			{
				Tall.Add(FString::Printf(TEXT("%s %.0f"), *Z.Label, Median));
			}
		}
	}
	UE_LOG(LogMCPTest, Log, TEXT("[Ceiling] 벽 높이 측정: 추적 %d번, 400보다 높은 구역 [%s]"), Traces, *FString::Join(Tall, TEXT(", ")));
}

float UVHCeilingSubsystem::DistToRects(const TArray<FBox2D>& Rects, const FVector2D& P)
{
	double Best = TNumericLimits<double>::Max();
	for (const FBox2D& R : Rects)
	{
		const double Dx = FMath::Max3(R.Min.X - P.X, 0.0, P.X - R.Max.X);
		const double Dy = FMath::Max3(R.Min.Y - P.Y, 0.0, P.Y - R.Max.Y);
		Best = FMath::Min(Best, FMath::Sqrt(Dx * Dx + Dy * Dy));
		if (Best <= 0.0)
		{
			break;
		}
	}
	return static_cast<float>(Best);
}

FVector UVHCeilingSubsystem::GetViewerLocation() const
{
	// 카메라가 따라가는 대상 (쓰러져 숨겨져도 내 폰이 뷰 타깃. 관전이 생기면 그 대상)
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC && PC->PlayerCameraManager)
	{
		if (const AActor* Target = PC->PlayerCameraManager->GetViewTarget())
		{
			return Target->GetActorLocation();
		}
	}
	if (PC && PC->GetPawn())
	{
		return PC->GetPawn()->GetActorLocation();
	}
	return FVector(TNumericLimits<float>::Max());
}

void UVHCeilingSubsystem::UpdateCurrentZone()
{
	const FVector V = GetViewerLocation();
	if (V.X >= TNumericLimits<float>::Max())
	{
		return;
	}
	const FVector2D P(V.X, V.Y);
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	// 지금 구역에서 여유 거리 안이면 그대로 (문가 깜빡임 방지)
	if (Current != INDEX_NONE && DistToRects(Zones[Current].Rects, P) <= C.CeilingSwitchMargin)
	{
		return;
	}
	int32 Inside = INDEX_NONE;
	for (int32 i = 0; i < Zones.Num(); ++i)
	{
		if (Zones[i].Bounds.IsInside(P) && DistToRects(Zones[i].Rects, P) <= 0.f)
		{
			Inside = i;
			break;
		}
	}
	if (Inside == INDEX_NONE && Current == INDEX_NONE)
	{
		// 처음: 어느 조각에도 없으면(벽 옆·문 가운데) 가장 가까운 구역 (1.5m 안)
		float BestDist = 150.f;
		for (int32 i = 0; i < Zones.Num(); ++i)
		{
			const float D = DistToRects(Zones[i].Rects, P);
			if (D < BestDist)
			{
				BestDist = D;
				Inside = i;
			}
		}
	}
	if (Inside != INDEX_NONE && Inside != Current)
	{
		UE_LOG(LogMCPTest, Log, TEXT("[Ceiling] 내 구역: %s → %s (위치 %.0f, %.0f, 시각 %.2f)"),
			Current != INDEX_NONE ? *Zones[Current].Label : TEXT("없음"), *Zones[Inside].Label, P.X, P.Y, GetWorld()->GetTimeSeconds());
		Previous = Current;
		Current = Inside;
	}
}

void UVHCeilingSubsystem::UpdateFade(float DeltaTime)
{
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	const bool bEnabled = IsEnabled();
	const float Step = C.CeilingFadeSeconds > 0.f ? DeltaTime / C.CeilingFadeSeconds : 1.f;
	for (int32 i = 0; i < Zones.Num(); ++i)
	{
		FZone& Z = Zones[i];
		const float Target = (bEnabled && i != Current) ? 1.f : 0.f;
		Z.Opacity = FMath::FInterpConstantTo(Z.Opacity, Target, 1.f, Step);
		if (Z.Opacity == Z.AppliedOpacity)
		{
			continue;
		}
		Z.AppliedOpacity = Z.Opacity;
		UInstancedStaticMeshComponent* M = Z.Mesh.Get();
		if (!M)
		{
			continue;
		}
		// 완전히 걷히면 그리지 않는다
		M->SetVisibility(Z.Opacity > 0.f);
		if (UMaterialInstanceDynamic* MID = Z.Material.Get())
		{
			MID->SetScalarParameterValue(TEXT("Opacity"), Z.Opacity);
		}
	}
}

bool UVHCeilingSubsystem::IsPointCovered(const FVector2D& P) const
{
	if (!IsEnabled())
	{
		return false;
	}
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	if (Current != INDEX_NONE && DistToRects(Zones[Current].Wide, P) <= C.CeilingVisibleMargin)
	{
		return false;	// 내 구역 안 또는 문가
	}
	// 어떤 구역 안이고, 그 구역의 천장이 반 넘게 덮였으면 숨김 (덮이기·걷히기 중간에 함께 바뀐다). 어느 구역에도 없으면(바깥) 보임
	bool bAny = false;
	for (const FZone& Z : Zones)
	{
		if (!Z.Bounds.IsInside(P) || DistToRects(Z.Wide, P) > 0.f)
		{
			continue;
		}
		bAny = true;
		if (Z.Opacity < 0.5f)
		{
			return false;
		}
	}
	return bAny && !IsPointPeeked(P);	// 열린 문 너머 부채꼴 안이면 보임 [2026-11-24]
}

void UVHCeilingSubsystem::SetActorCovered(AActor* Actor, bool bCovered)
{
	if (bCovered)
	{
		TArray<TWeakObjectPtr<USceneComponent>>& List = Hidden.FindOrAdd(Actor);
		// 이 액터와 붙어 있는 액터(무기·연출)의 컴포넌트 전부. 나중에 생긴 컴포넌트도 잡히도록 매 틱 확인한다
		TArray<AActor*> Actors = { Actor };
		Actor->GetAttachedActors(Actors, false, true);
		for (AActor* A : Actors)
		{
			TInlineComponentArray<USceneComponent*> Comps(A);
			for (USceneComponent* Comp : Comps)
			{
				if (!Comp->bHiddenInGame)
				{
					Comp->SetHiddenInGame(true);
					List.Add(Comp);
				}
			}
		}
	}
	else if (TArray<TWeakObjectPtr<USceneComponent>>* List = Hidden.Find(Actor))
	{
		for (const TWeakObjectPtr<USceneComponent>& Comp : *List)
		{
			if (USceneComponent* C = Comp.Get())
			{
				C->SetHiddenInGame(false);
			}
		}
		Hidden.Remove(Actor);
	}
}

void UVHCeilingSubsystem::UpdateActors()
{
	UWorld* World = GetWorld();
	// 지워진 액터 정리
	for (auto It = Hidden.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	// 다른 플레이어·경비원, 금고·상자·아이템·반납지점: 발 위치(액터 위치) 기준
	for (TActorIterator<AVHNetProxy> It(World); It; ++It)
	{
		SetActorCovered(*It, IsPointCovered(FVector2D(It->GetActorLocation())));
	}
	for (TActorIterator<AVHInteractableBase> It(World); It; ++It)
	{
		SetActorCovered(*It, IsPointCovered(FVector2D(It->GetActorLocation())));
	}
	// 연출(폭발·끈끈이·지뢰·연막·빔): 범위 전체가 덮인 구역 안일 때만 숨긴다 (문을 넘어 내 구역으로 오는 빔은 보임)
	for (TActorIterator<AVHFxActor> It(World); It; ++It)
	{
		if (It->GetAttachParentActor())
		{
			continue;	// 붙어 있는 연출은 주인과 같이
		}
		FVector Origin, Extent;
		It->GetActorBounds(false, Origin, Extent);
		const FVector2D O(Origin), E(Extent);
		const bool bCovered = IsPointCovered(O)
			&& IsPointCovered(O + FVector2D(E.X, E.Y)) && IsPointCovered(O + FVector2D(-E.X, E.Y))
			&& IsPointCovered(O + FVector2D(E.X, -E.Y)) && IsPointCovered(O + FVector2D(-E.X, -E.Y));
		SetActorCovered(*It, bCovered);
	}
}

void UVHCeilingSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bBuilt || Zones.Num() == 0)
	{
		return;
	}
	const bool bEnabled = IsEnabled();
	if (bEnabled != bWasEnabled)
	{
		UE_LOG(LogMCPTest, Log, TEXT("[Ceiling] 천장 가림 %s"), bEnabled ? TEXT("켬") : TEXT("끔"));
		bWasEnabled = bEnabled;
	}
	UpdateCurrentZone();
	UpdateFade(DeltaTime);
	UpdateCutParams();
	UpdatePeek();
	UpdateProps();
	UpdateActors();
	UpdateSilhouette();
}

void UVHCeilingSubsystem::UpdateSilhouette()
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	const bool bOn = IsEnabled() && C.bSilhouette;

	// 1) 내 캐릭터 몸·든 무기·발밑 원: 커스텀 뎁스에 그린다. 스텐실은 0 (1~3 은 상호작용 테두리 M_VHOutline 몫)
	//    무기를 바꾸면 새 컴포넌트가 생기므로 매 틱 확인한다 (바뀐 것만 설정)
	TArray<AActor*> Actors = { Pawn };
	Pawn->GetAttachedActors(Actors, false, true);
	for (AActor* A : Actors)
	{
		TInlineComponentArray<UPrimitiveComponent*> Prims(A);
		for (UPrimitiveComponent* P : Prims)
		{
			if (P->bRenderCustomDepth != bOn || (bOn && P->CustomDepthStencilValue != 0))
			{
				P->SetRenderCustomDepth(bOn);
				P->SetCustomDepthStencilValue(0);
			}
		}
	}

	// 2) 카메라 후처리: 가려진 내 픽셀만 반투명 색 + 윤곽선
	UCameraComponent* Camera = Pawn->FindComponentByClass<UCameraComponent>();
	if (!Camera)
	{
		return;
	}
	if (!SilhouetteMID)
	{
		UMaterialInterface* Base = C.SilhouetteMaterial.LoadSynchronous();
		if (!Base)
		{
			return;
		}
		SilhouetteMID = UMaterialInstanceDynamic::Create(Base, this);
		SilhouetteMID->SetVectorParameterValue(TEXT("Color"), C.SilhouetteColor);
		SilhouetteMID->SetScalarParameterValue(TEXT("FillOpacity"), C.SilhouetteFillOpacity);
		SilhouetteMID->SetScalarParameterValue(TEXT("EdgeOpacity"), C.SilhouetteEdgeOpacity);
		// 테두리와 같은 스텐실 버퍼가 필요하다 (r.CustomDepth=3, 플레이어 컨트롤러도 켠다)
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth")))
		{
			if (CVar->GetInt() < 3)
			{
				CVar->Set(3, ECVF_SetByCode);
			}
		}
	}
	TArray<FWeightedBlendable>& Blendables = Camera->PostProcessSettings.WeightedBlendables.Array;
	FWeightedBlendable* Found = Blendables.FindByPredicate([this](const FWeightedBlendable& B) { return B.Object == SilhouetteMID; });
	if (!Found)
	{
		Blendables.Add(FWeightedBlendable(bOn ? 1.f : 0.f, SilhouetteMID));
	}
	else
	{
		Found->Weight = bOn ? 1.f : 0.f;
	}
	if (bOn != bSilhouetteOn || SilhouettePawn.Get() != Pawn)
	{
		UE_LOG(LogMCPTest, Log, TEXT("[Ceiling] 가려진 내 캐릭터 실루엣 %s (%s)"), bOn ? TEXT("켬") : TEXT("끔"), *Pawn->GetName());
		bSilhouetteOn = bOn;
		SilhouettePawn = Pawn;
	}
}

bool UVHCeilingSubsystem::IsCovered(const AActor* Actor) const
{
	return Actor && Hidden.Contains(TWeakObjectPtr<AActor>(const_cast<AActor*>(Actor)));
}

bool UVHCeilingSubsystem::IsActorCovered(const AActor* Actor)
{
	const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	const UVHCeilingSubsystem* Ceiling = World ? World->GetSubsystem<UVHCeilingSubsystem>() : nullptr;
	return Ceiling && Ceiling->IsCovered(Actor);
}

FString UVHCeilingSubsystem::GetCurrentZoneLabel() const
{
	return Current != INDEX_NONE ? Zones[Current].Label : FString();
}

FString UVHCeilingSubsystem::GetZoneLabelAt(const FVector& Location) const
{
	const FVector2D P(Location.X, Location.Y);
	for (const FZone& Z : Zones)
	{
		if (Z.Bounds.IsInside(P) && DistToRects(Z.Wide, P) <= 0.f)
		{
			return Z.Label;
		}
	}
	return TEXT("바깥");
}

FString UVHCeilingSubsystem::Describe() const
{
	int32 Covered = 0;
	FString Fading;
	for (const FZone& Z : Zones)
	{
		Covered += Z.Opacity >= 1.f ? 1 : 0;
		if (Z.Opacity > 0.f && Z.Opacity < 1.f)
		{
			Fading += FString::Printf(TEXT(" %s=%.2f"), *Z.Label, Z.Opacity);
		}
	}
	const FVector V = GetViewerLocation();
	int32 HiddenProps = 0;
	for (const FZone& Z : Zones)
	{
		HiddenProps += Z.PropMode == 1 ? Z.Props.Num() : 0;
	}
	return FString::Printf(TEXT("%s, 내 구역 %s (%.0f, %.0f), 덮인 구역 %d/%d, 바뀌는 중[%s ], 숨긴 물체 %d, 앞 가림 지우기 %s(높이 %.0f, A=%.0f/%.2f B=%.0f/%.2f), 숨긴 소품 %d/%d"),
		IsEnabled() ? TEXT("켬") : TEXT("끔"), *GetCurrentZoneLabel(), V.X, V.Y, Covered, Zones.Num(), *Fading, Hidden.Num(),
		SentA > 0.f ? TEXT("켬") : TEXT("끔"), SentZ, SentA, SentAA, SentB, SentBA, HiddenProps, PropCount);
}

// ───────────────────────── 내 구역 앞 가림 지우기 [2026-11-21 (4)] ─────────────────────────
// 카메라가 비스듬히(-65°) 내려다보므로 카메라 쪽 벽 가까이의 내 구역 바닥은 옆 구역 천장(벽 높이)과 벽에 가려진다(띠 폭 = 벽 높이 / tan65° ≈ 1.9m).
// 재질(M_VHCeiling·M_VHWallFadeTex 의 ZoneCut 노드)이 "카메라 → 이 픽셀" 선을 CutPlaneZ 높이까지 늘린 점의 구역을 구역 지도 텍스처에서 읽어,
// 그 점이 내 구역이면 이 픽셀을 지운다(디더). 천장은 자기 구역이면 지우지 않고(자기 페이드), 벽·기둥은 자기 자리가 내 구역이면 지우지 않는다(방 안 기둥).
// 그래서 내 구역은 바닥까지 다 보이고, 다른 구역 안은 여전히 천장에 덮인다(덮인 구역 소품은 숨겨 지운 자리로 비치지 않게).

int32 UVHCeilingSubsystem::ZoneIndexAt(const FVector2D& P) const
{
	for (int32 i = 0; i < Zones.Num(); ++i)
	{
		if (Zones[i].Bounds.IsInside(P) && DistToRects(Zones[i].Wide, P) <= 0.f)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void UVHCeilingSubsystem::BuildZoneTexture()
{
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	FBox2D B(ForceInit);
	for (const FZone& Z : Zones)
	{
		B += Z.Bounds;
	}
	B = B.ExpandBy(100.0);
	const double Cell = FMath::Max(2.0, static_cast<double>(C.CeilingZoneMapCell));
	const int32 W = FMath::Clamp(FMath::CeilToInt(B.GetSize().X / Cell), 1, 4096);
	const int32 H = FMath::Clamp(FMath::CeilToInt(B.GetSize().Y / Cell), 1, 4096);
	// 칸 하나 = B8G8R8A8 (메모리 순서 B,G,R,A).
	//  R = 넓은 구역(Wide: 서버 조각을 벽 면까지 넓힌 것) — "선을 늘린 점이 내 구역인가" 판단
	//  G = 좁은 구역(서버 조각 + 메운 기둥 자리) — 벽·기둥 픽셀의 "자기 자리" 판단 (넓은 구역은 문가에서 벽 속까지 들어가 벽 윗면이 남았다)
	TArray<uint8> Data;
	Data.SetNumZeroed(W * H * 4);
	auto Fill = [&](const FBox2D& R, int32 Channel, uint8 Id)
	{
		// 칸 가운데가 조각 안이면 그 구역
		const int32 X0 = FMath::Max(0, FMath::CeilToInt((R.Min.X - B.Min.X) / Cell - 0.5));
		const int32 X1 = FMath::Min(W - 1, FMath::FloorToInt((R.Max.X - B.Min.X) / Cell - 0.5));
		const int32 Y0 = FMath::Max(0, FMath::CeilToInt((R.Min.Y - B.Min.Y) / Cell - 0.5));
		const int32 Y1 = FMath::Min(H - 1, FMath::FloorToInt((R.Max.Y - B.Min.Y) / Cell - 0.5));
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				Data[(Y * W + X) * 4 + Channel] = Id;
			}
		}
	};
	for (int32 i = 0; i < Zones.Num(); ++i)
	{
		const uint8 Id = static_cast<uint8>(FMath::Min(i + 1, 255));
		const FZone& Z = Zones[i];
		for (int32 k = 0; k < Z.Wide.Num(); ++k)
		{
			Fill(Z.Wide[k], 2, Id);										// R
			Fill(k < Z.Rects.Num() ? Z.Rects[k] : Z.Wide[k], 1, Id);	// G: 앞쪽 = 서버 조각, 뒤쪽 = FillSmallHoles 가 더한 기둥 자리
		}
	}
	ZoneTexture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, TEXT("VHZoneMap"));
	if (!ZoneTexture)
	{
		return;
	}
	ZoneTexture->SRGB = false;
	ZoneTexture->Filter = TF_Nearest;
	ZoneTexture->AddressX = TA_Clamp;
	ZoneTexture->AddressY = TA_Clamp;
	ZoneTexture->NeverStream = true;
	FTexture2DMipMap& Mip = ZoneTexture->GetPlatformData()->Mips[0];
	void* Dest = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Dest, Data.GetData(), Data.Num());
	Mip.BulkData.Unlock();
	ZoneTexture->UpdateResource();
	ZoneMapParam = FVector4(B.Min.X, B.Min.Y, 1.0 / Cell, 0.0);
	ZoneSizeParam = FVector4(W, H, 0.0, 0.0);

	// 엿보기용: 넓은 구역(R) CPU 사본 + 같은 칸의 마스크 렌더 타깃 (흰 = 열린 문 너머로 보이는 바닥)
	ZoneMapSize = FIntPoint(W, H);
	ZoneMapIds.SetNumUninitialized(W * H);
	for (int32 i = 0; i < W * H; ++i)
	{
		ZoneMapIds[i] = Data[i * 4 + 2];
	}
	// 마스크는 구역 지도보다 촘촘하게 (부채꼴 가장자리가 계단 모양으로 보이지 않게). 재질은 0~1 좌표로 읽으므로 배율과 무관
	PeekScale = FMath::Clamp(C.DoorPeekMaskScale, 1, FMath::Max(1, 4096 / FMath::Max(W, H)));
	PeekTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, W * PeekScale, H * PeekScale, RTF_R8, FLinearColor::Black);
	if (PeekTarget)
	{
		PeekTarget->Filter = TF_Bilinear;
		PeekTarget->AddressX = TA_Clamp;
		PeekTarget->AddressY = TA_Clamp;
		PeekTarget->UpdateResourceImmediate(true);
	}
	UE_LOG(LogMCPTest, Log, TEXT("[Ceiling] 구역 지도 %dx%d (칸 %.0fcm, 원점 %.0f, %.0f)"), W, H, Cell, B.Min.X, B.Min.Y);
}

void UVHCeilingSubsystem::SetCutParams(UMaterialInstanceDynamic* MID, float SelfZone) const
{
	if (!MID)
	{
		return;
	}
	if (ZoneTexture)
	{
		MID->SetTextureParameterValue(TEXT("ZoneTex"), ZoneTexture);
	}
	MID->SetVectorParameterValue(TEXT("ZoneMap"), FLinearColor(ZoneMapParam.X, ZoneMapParam.Y, ZoneMapParam.Z, ZoneMapParam.W));
	MID->SetVectorParameterValue(TEXT("ZoneSize"), FLinearColor(ZoneSizeParam.X, ZoneSizeParam.Y, 0.f, 0.f));
	MID->SetScalarParameterValue(TEXT("SelfZone"), SelfZone);
	MID->SetScalarParameterValue(TEXT("CutZoneA"), FMath::Max(0.f, SentA));
	MID->SetScalarParameterValue(TEXT("CutAlphaA"), FMath::Max(0.f, SentAA));
	MID->SetScalarParameterValue(TEXT("CutZoneB"), FMath::Max(0.f, SentB));
	MID->SetScalarParameterValue(TEXT("CutAlphaB"), FMath::Max(0.f, SentBA));
	MID->SetScalarParameterValue(TEXT("CutPlaneZ"), SentZ > -10000.f ? SentZ : 0.f);
	if (PeekTarget)
	{
		MID->SetTextureParameterValue(TEXT("PeekTex"), PeekTarget);
		MID->SetScalarParameterValue(TEXT("PeekOn"), 1.f);
	}
}

void UVHCeilingSubsystem::ApplyCutaway(AActor* Actor, float SelfZone)
{
	if (!Actor || !ZoneTexture || Zones.Num() == 0)
	{
		return;	// 구역이 아직 없으면 BuildZones 가 나중에 모든 액터를 처리한다
	}
	TInlineComponentArray<UMeshComponent*> Meshes(Actor);
	for (UMeshComponent* Mesh : Meshes)
	{
		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
		{
			UMaterialInterface* Mat = Mesh->GetMaterial(Slot);
			if (!Mat || CutMIDs.ContainsByPredicate([Mat](const FCutMID& M) { return M.MID == Mat; }))
			{
				continue;
			}
			UTexture* Dummy = nullptr;
			if (!Mat->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("ZoneTex")), Dummy))
			{
				continue;	// 가림 지우기 계산이 없는 재질 (바닥·유리·가구 등)
			}
			FCutMID* Found = CutMIDs.FindByPredicate([Mat, SelfZone](const FCutMID& M) { return M.Base.Get() == Mat && M.SelfZone == SelfZone; });
			if (!Found)
			{
				FCutMID& New = CutMIDs.AddDefaulted_GetRef();
				New.Base = Mat;
				New.SelfZone = SelfZone;
				New.MID = UMaterialInstanceDynamic::Create(Mat, this);
				CutMIDRefs.Add(New.MID);
				SetCutParams(New.MID, SelfZone);
				Found = &New;
			}
			Mesh->SetMaterial(Slot, Found->MID);
		}
	}
}

void UVHCeilingSubsystem::UpdateCutParams(bool bForce)
{
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	const bool bCut = IsEnabled() && C.bCeilingCutaway && CVarVHCeilingCut.GetValueOnGameThread() != 0;
	float A = 0.f, AA = 0.f, B = 0.f, BA = 0.f;
	if (bCut && Current != INDEX_NONE)
	{
		A = static_cast<float>(Current + 1);
		AA = 1.f - Zones[Current].Opacity;	// 내 구역 천장이 걷힌 만큼 앞 가림도 지운다
	}
	if (bCut && Previous != INDEX_NONE && Previous != Current && Zones.IsValidIndex(Previous) && Zones[Previous].Opacity < 1.f)
	{
		B = static_cast<float>(Previous + 1);
		BA = 1.f - Zones[Previous].Opacity;	// 직전 구역: 천장이 다시 덮이는 만큼 지우기를 거둔다
	}
	const float CVarZ = CVarVHCeilingCutZ.GetValueOnGameThread();
	const float Z = CVarZ > -9999.f ? CVarZ : C.CeilingCutPlaneZ;
	if (!bForce && A == SentA && AA == SentAA && B == SentB && BA == SentBA && Z == SentZ)
	{
		return;
	}
	SentA = A; SentAA = AA; SentB = B; SentBA = BA; SentZ = Z;
	auto Send = [&](UMaterialInstanceDynamic* MID)
	{
		MID->SetScalarParameterValue(TEXT("CutZoneA"), A);
		MID->SetScalarParameterValue(TEXT("CutAlphaA"), AA);
		MID->SetScalarParameterValue(TEXT("CutZoneB"), B);
		MID->SetScalarParameterValue(TEXT("CutAlphaB"), BA);
		MID->SetScalarParameterValue(TEXT("CutPlaneZ"), Z);
	};
	for (FZone& Zone : Zones)
	{
		if (UMaterialInstanceDynamic* MID = Zone.Material.Get())
		{
			Send(MID);
		}
	}
	for (const FCutMID& M : CutMIDs)
	{
		if (M.MID)
		{
			Send(M.MID);
		}
	}
}

void UVHCeilingSubsystem::AddDoorCurtains()
{
	// 열린 문으로 덮인 구역 안이 비치지 않게: 문 자리마다 양쪽 구역의 벽 면(구역 쪽)에 세로 천장 조각을 세운다.
	// 그 구역 천장과 같은 인스턴스 메시라 함께 덮이고 걷힌다. 한쪽 면만 그리는 재질이라 문 바깥(다른 구역 쪽)에서만 보인다
	int32 Count = 0;
	DoorList.Reset();
	for (TActorIterator<AVHDoor> It(GetWorld()); It; ++It)
	{
		const AVHDoor* Door = *It;
		DoorList.Add(*It);
		const FTransform& T = Door->GetActorTransform();
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FName Id = Side == 0 ? Door->ZoneA : Door->ZoneB;
			const float S = Side == 0 ? -1.f : 1.f;		// 이 구역이 문의 로컬 -X(A) / +X(B) 쪽
			FZone* Z = Id.IsNone() ? nullptr : Zones.FindByPredicate([Id](const FZone& Q) { return Q.Id == Id; });
			UInstancedStaticMeshComponent* M = Z ? Z->Mesh.Get() : nullptr;
			if (!M)
			{
				continue;
			}
			const float H = Z->Height + 2.f;
			const FVector Center = T.TransformPosition(FVector(S * (Door->WallThickness * 0.5f + 0.5f), 0.f, H * 0.5f));
			const FVector Facing = T.TransformVectorNoScale(FVector(-S, 0.f, 0.f));	// 다른 구역 쪽을 본다
			const FRotator Rot = FRotationMatrix::MakeFromZX(Facing, FVector::UpVector).Rotator();
			M->AddInstance(FTransform(Rot, Center, FVector(H / 100.0, (Door->Width + 10.0) / 100.0, 1.0)), true);
			++Z->Curtains;
			++Count;
		}
	}
	UE_LOG(LogMCPTest, Log, TEXT("[Ceiling] 문간 가림막 %d개"), Count);
}

void UVHCeilingSubsystem::CollectProps()
{
	// 구역 안의 소품(가구·식물·조명 기구 = 레벨의 작은 StaticMeshActor). 원래 숨긴 것(펜던트 기구)·큰 것(건물 Shell)은 뺀다.
	// 숨겨도 그림자·간접광은 그대로 남게 한다 → 구역이 바뀌어도 방 밝기가 같다
	PropCount = 0;
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		AStaticMeshActor* A = *It;
		UStaticMeshComponent* SM = A->GetStaticMeshComponent();
		if (A->IsHidden() || !SM || !SM->IsVisible() || SM->bHiddenInGame || !SM->GetStaticMesh())
		{
			continue;
		}
		FVector Origin, Extent;
		A->GetActorBounds(false, Origin, Extent);
		if (Extent.X > 400.0 || Extent.Y > 400.0)
		{
			continue;
		}
		const int32 Zi = ZoneIndexAt(FVector2D(Origin.X, Origin.Y));
		if (Zi == INDEX_NONE)
		{
			continue;
		}
		TInlineComponentArray<UPrimitiveComponent*> Prims(A);
		for (UPrimitiveComponent* P : Prims)
		{
			P->bCastHiddenShadow = P->CastShadow;
			P->bAffectIndirectLightingWhileHidden = true;
			P->MarkRenderStateDirty();
		}
		Zones[Zi].Props.Add(A);
		Zones[Zi].PropBoxes.Add(FBox2D(FVector2D(Origin.X - Extent.X, Origin.Y - Extent.Y), FVector2D(Origin.X + Extent.X, Origin.Y + Extent.Y)));
		++PropCount;
	}
	UE_LOG(LogMCPTest, Log, TEXT("[Ceiling] 구역 소품 %d개 (구역이 다 덮이면 숨김, 그림자·간접광은 남김)"), PropCount);
}

void UVHCeilingSubsystem::UpdateProps()
{
	// 다 덮인 구역의 소품은 숨김. 열린 문 너머로 일부가 보이는 구역은 부채꼴 안의 소품만 보임 [2026-11-24]
	const bool bEnabled = IsEnabled();
	for (int32 i = 0; i < Zones.Num(); ++i)
	{
		FZone& Z = Zones[i];
		const bool bCovered = bEnabled && i != Current && Z.Opacity >= 1.f;
		const int32 Mode = !bCovered ? 0 : (Z.bPeek ? 2 : 1);
		if (Mode == Z.PropMode && Mode != 2)
		{
			continue;
		}
		Z.PropMode = Mode;
		for (int32 k = 0; k < Z.Props.Num(); ++k)
		{
			AActor* Actor = Z.Props[k].Get();
			if (!Actor)
			{
				continue;
			}
			bool bHide = Mode == 1;
			if (Mode == 2)
			{
				const FBox2D& Bx = Z.PropBoxes[k];
				bHide = !(IsPointPeeked(Bx.GetCenter()) || IsPointPeeked(Bx.Min) || IsPointPeeked(Bx.Max)
					|| IsPointPeeked(FVector2D(Bx.Min.X, Bx.Max.Y)) || IsPointPeeked(FVector2D(Bx.Max.X, Bx.Min.Y)));
			}
			if (Actor->IsHidden() != bHide)
			{
				Actor->SetActorHiddenInGame(bHide);
			}
		}
	}
}

int32 UVHCeilingSubsystem::ZoneIdAtMap(const FVector2D& P) const
{
	if (ZoneMapSize.X <= 0)
	{
		return 0;
	}
	const int32 X = FMath::FloorToInt((P.X - ZoneMapParam.X) * ZoneMapParam.Z);
	const int32 Y = FMath::FloorToInt((P.Y - ZoneMapParam.Y) * ZoneMapParam.Z);
	if (X < 0 || Y < 0 || X >= ZoneMapSize.X || Y >= ZoneMapSize.Y)
	{
		return 0;
	}
	return ZoneMapIds[Y * ZoneMapSize.X + X];
}

void UVHCeilingSubsystem::RefreshPassMap()
{
	// 광선이 지나갈 수 있는 칸 지도 = 넓은 구역 지도(R) + 문 자리 덮어쓰기.
	//  - 문 자리(벽 두께 × 문 폭 + 옆 20cm): 닫힌 문·잠긴 문 = 0(벽). 넓은 구역이 문가에서 벽 속까지 들어가 있어도 막는다.
	//  - 열린 문틈(지금 문짝이 비켜난 만큼, 벽 면 바깥 15cm 까지) = PassDoor(255): 이 칸을 거쳐야 다른 구역으로 넘어갈 수 있다.
	if (PassMap.Num() != ZoneMapIds.Num())
	{
		PassMap = ZoneMapIds;
	}
	const double Cell = 1.0 / ZoneMapParam.Z;
	for (const TWeakObjectPtr<AVHDoor>& Weak : DoorList)
	{
		const AVHDoor* D = Weak.Get();
		if (!D)
		{
			continue;
		}
		FTransform T = D->GetActorTransform();
		T.SetScale3D(FVector::OneVector);
		const float HalfT = D->WallThickness * 0.5f;
		const float ExX = HalfT + 15.f;
		const float ExY = D->Width * 0.5f + 20.f;
		float Y0 = 0.f, Y1 = 0.f;
		const bool bOpen = D->GetAperture(Y0, Y1);
		// 문 자리 상자의 월드 경계 → 칸 범위
		FBox2D B(ForceInit);
		for (const FVector2D& Corner : { FVector2D(-ExX, -ExY), FVector2D(ExX, -ExY), FVector2D(-ExX, ExY), FVector2D(ExX, ExY) })
		{
			const FVector P = T.TransformPosition(FVector(Corner.X, Corner.Y, 0.f));
			B += FVector2D(P.X, P.Y);
		}
		const int32 X0 = FMath::Max(0, FMath::FloorToInt((B.Min.X - ZoneMapParam.X) * ZoneMapParam.Z));
		const int32 X1 = FMath::Min(ZoneMapSize.X - 1, FMath::FloorToInt((B.Max.X - ZoneMapParam.X) * ZoneMapParam.Z));
		const int32 Yc0 = FMath::Max(0, FMath::FloorToInt((B.Min.Y - ZoneMapParam.Y) * ZoneMapParam.Z));
		const int32 Yc1 = FMath::Min(ZoneMapSize.Y - 1, FMath::FloorToInt((B.Max.Y - ZoneMapParam.Y) * ZoneMapParam.Z));
		for (int32 Y = Yc0; Y <= Yc1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const FVector Wc(ZoneMapParam.X + (X + 0.5) * Cell, ZoneMapParam.Y + (Y + 0.5) * Cell, T.GetLocation().Z);
				const FVector L = T.InverseTransformPosition(Wc);
				if (FMath::Abs(L.X) > ExX || FMath::Abs(L.Y) > ExY)
				{
					continue;
				}
				const int32 Idx = Y * ZoneMapSize.X + X;
				uint8 V = FMath::Abs(L.X) <= HalfT ? 0 : ZoneMapIds[Idx];
				if (bOpen && L.Y >= Y0 && L.Y <= Y1 && FMath::Abs(L.Y) <= D->Width * 0.5f)
				{
					V = PassDoor;
				}
				PassMap[Idx] = V;
			}
		}
	}
}

UVHCeilingSubsystem::FPeekRay UVHCeilingSubsystem::CastPeekRay(const FPeekWedge& W, float A, const FVector2D& Viewer, int32 CurMap, TBitArray<>& OutZones) const
{
	FPeekRay R;
	R.Angle = A;
	auto ToWorld = [&W](const FVector2D& Lp)
	{
		const FVector P = W.DoorT.TransformPosition(FVector(Lp.X, Lp.Y, 0.f));
		return FVector2D(P.X, P.Y);
	};
	const FVector2D Dir(W.Fwd * FMath::Cos(A), FMath::Sin(A));
	const float CosA = FMath::Max(0.05f, FMath::Cos(A));
	const float TNear = W.Fn / CosA;
	const float TFar = W.Ff / CosA;
	const FVector2D StartW = ToWorld(W.Eye + Dir * TFar);
	const FVector DirW3 = W.DoorT.TransformVectorNoScale(FVector(Dir.X, Dir.Y, 0.f));
	const FVector2D DW = FVector2D(DirW3.X, DirW3.Y).GetSafeNormal();
	R.Start = StartW;
	R.End = StartW;
	R.Dist = TFar;

	// 1) 내 쪽 가림: 실제 내 위치 → 가까운 벽 면까지가 내 구역 안이어야 한다 (복도 모퉁이 너머 문은 안 보임)
	{
		const FVector2D NearW = ToWorld(W.Eye + Dir * TNear);
		const float Total = FVector2D::Distance(Viewer, NearW);
		const FVector2D DV = Total > 1.f ? (NearW - Viewer) / Total : FVector2D::ZeroVector;
		const float Step = FMath::Max(2.f, static_cast<float>(1.0 / ZoneMapParam.Z));
		for (float S = 50.f; S < Total - 70.f; S += Step)
		{
			if (ZoneIdAtMap(Viewer + DV * S) != CurMap)
			{
				return R;
			}
		}
	}

	// 2) 문 너머: 먼 벽 면부터 칸 하나하나를 정확히 따라간다(격자 DDA — 얇은 벽 모서리 사이로 새지 않음).
	//    벽(0)·닫힌 문(0)에서 멈춘다. 열린 문틈(PassDoor)은 지나가서 그 너머 구역까지 이어진다 — 거리 제한 없음 [2026-11-25].
	//    다른 구역끼리 문틈 없이 바로 맞닿는 곳(넓은 구역이 벽 속에서 만나는 곳)도 벽으로 친다.
	const double Inv = ZoneMapParam.Z;
	const double Gx = (StartW.X - ZoneMapParam.X) * Inv, Gy = (StartW.Y - ZoneMapParam.Y) * Inv;
	int32 Ix = FMath::FloorToInt(Gx), Iy = FMath::FloorToInt(Gy);
	const int32 StepX = DW.X > 0.0 ? 1 : -1, StepY = DW.Y > 0.0 ? 1 : -1;
	const double Big = 1e30;
	const double DeltaX = FMath::Abs(DW.X) > 1e-9 ? 1.0 / FMath::Abs(DW.X) : Big;
	const double DeltaY = FMath::Abs(DW.Y) > 1e-9 ? 1.0 / FMath::Abs(DW.Y) : Big;
	double TMaxX = FMath::Abs(DW.X) > 1e-9 ? ((StepX > 0 ? (Ix + 1) - Gx : Gx - Ix) * DeltaX) : Big;
	double TMaxY = FMath::Abs(DW.Y) > 1e-9 ? ((StepY > 0 ? (Iy + 1) - Gy : Gy - Iy) * DeltaY) : Big;
	double TCell = 0.0;		// 지금 칸에 들어온 거리 (칸 단위)
	double Reach = 0.0;		// 마지막으로 지나갈 수 있었던 칸을 나온 거리 (칸 단위)
	const double SkipCells = 40.0 * Inv;	// 먼 벽 면 근처 처음 40cm 의 벽 칸(반올림 틈)은 건너뛴다
	uint8 Prev = PassDoor;
	bool bEntered = false;
	const int32 MaxCells = (ZoneMapSize.X + ZoneMapSize.Y) * 2;
	for (int32 Guard = 0; Guard < MaxCells; ++Guard)
	{
		if (Ix < 0 || Iy < 0 || Ix >= ZoneMapSize.X || Iy >= ZoneMapSize.Y)
		{
			break;
		}
		const uint8 V = PassMap[Iy * ZoneMapSize.X + Ix];
		const double TExit = FMath::Min(TMaxX, TMaxY);
		if (V == 0)
		{
			if (bEntered || TCell > SkipCells)
			{
				break;
			}
		}
		else
		{
			if (V != PassDoor)
			{
				if (Prev != PassDoor && V != Prev)
				{
					break;	// 다른 구역과 문틈 없이 맞닿음 = 벽
				}
				if (OutZones.IsValidIndex(V - 1))
				{
					OutZones[V - 1] = true;
				}
			}
			Prev = V;
			bEntered = true;
			Reach = TExit;
		}
		// 다음 칸
		TCell = TExit;
		if (TMaxX < TMaxY)
		{
			TMaxX += DeltaX;
			Ix += StepX;
		}
		else
		{
			TMaxY += DeltaY;
			Iy += StepY;
		}
	}
	const float Len = static_cast<float>(Reach / Inv);
	R.End = StartW + DW * Len;
	R.Dist = TFar + Len;
	R.Cells = static_cast<int32>(TCell);
	return R;
}

void UVHCeilingSubsystem::UpdatePeek()
{
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	const bool bOn = IsEnabled() && C.bDoorPeek && CVarVHDoorPeek.GetValueOnGameThread() != 0 && Zones.IsValidIndex(Current) && ZoneMapSize.X > 0 && PeekTarget;
	const FVector V3 = GetViewerLocation();
	const FVector2D V(V3.X, V3.Y);
	const bool bFacing = C.DoorPeekMode == EVHDoorPeekMode::ApertureAndFacing && C.DoorPeekFovDegrees < 359.f;

	// 바라보는 방향 = 내 캐릭터 → 마우스(조준점). 짧게 부드럽게 따라가서(DoorPeekFacingSmoothSeconds) 마우스가 캐릭터 가까이서 튀어도 깜빡이지 않게
	if (bFacing)
	{
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		const UVHAimComponent* Aim = Pawn ? Pawn->FindComponentByClass<UVHAimComponent>() : nullptr;
		const float Target = Aim ? Aim->GetAimYaw() : (Pawn ? Pawn->GetActorRotation().Yaw : 0.f);
		const float Dt = GetWorld()->GetDeltaSeconds();
		if (!bAimSmoothInit || C.DoorPeekFacingSmoothSeconds <= 0.f)
		{
			SmoothAimYaw = Target;
			bAimSmoothInit = true;
		}
		else
		{
			const float K = 1.f - FMath::Exp(-Dt / C.DoorPeekFacingSmoothSeconds);
			SmoothAimYaw = FRotator::NormalizeAxis(SmoothAimYaw + FRotator::NormalizeAxis(Target - SmoothAimYaw) * K);
			if (FMath::Abs(FRotator::NormalizeAxis(Target - SmoothAimYaw)) < 0.05f)
			{
				SmoothAimYaw = Target;
			}
		}
	}

	// 1) 입력이 그대로면(내 위치 2cm·바라보는 방향 0.2°·모든 문의 문틈·설정) 다시 계산하지 않는다
	uint32 InSig = GetTypeHash(bOn ? Current : -1);
	if (bOn)
	{
		InSig = HashCombine(InSig, GetTypeHash(FMath::RoundToInt(V.X * 0.5f)));
		InSig = HashCombine(InSig, GetTypeHash(FMath::RoundToInt(V.Y * 0.5f)));
		InSig = HashCombine(InSig, GetTypeHash(bFacing ? FMath::RoundToInt(SmoothAimYaw * 5.f) : 99999));
		InSig = HashCombine(InSig, GetTypeHash(FMath::RoundToInt(C.DoorPeekFovDegrees * 10.f) + C.DoorPeekMaxRays * 7 + C.DoorPeekRefineDepth * 131
			+ FMath::RoundToInt(C.DoorPeekRayStepDegrees * 100.f) * 17 + FMath::RoundToInt(C.DoorPeekRefineGap)));
		// 열린 문을 지나 너머 구역까지 이어지므로 모든 문의 문틈이 입력이다
		for (const TWeakObjectPtr<AVHDoor>& Weak : DoorList)
		{
			const AVHDoor* D = Weak.Get();
			float Y0 = 0.f, Y1 = 0.f;
			const bool bAp = D && D->GetAperture(Y0, Y1);
			InSig = HashCombine(InSig, GetTypeHash(bAp ? FMath::RoundToInt(Y0) * 1000 + FMath::RoundToInt(Y1) : -77777));
		}
	}
	if (InSig == PeekSignature)
	{
		return;
	}
	PeekSignature = InSig;
	const double T0 = FPlatformTime::Seconds();

	// 2) 내 구역의 열린 문마다 부채꼴: 문 로컬 좌표(x = 벽을 뚫는 방향, y = 문 폭 방향)에서 문틈 양 끝을 지나는 각도 범위
	//    ∩ 바라보는 방향 시야각. 그 사이 광선을 쏴 벽·닫힌 문에 막힐 때까지 늘린다(열린 문은 지나감).
	for (FZone& Z : Zones)
	{
		Z.bPeek = false;
	}
	Wedges.Reset();
	LastPeekRays = 0;
	LastPeekCells = 0;
	if (bOn)
	{
		RefreshPassMap();
		const FName CurId = Zones[Current].Id;
		const int32 CurMap = Current + 1;
		const float HalfFov = FMath::DegreesToRadians(C.DoorPeekFovDegrees * 0.5f);
		TBitArray<> Touched(false, Zones.Num());
		for (const TWeakObjectPtr<AVHDoor>& Weak : DoorList)
		{
			AVHDoor* D = Weak.Get();
			if (!D || (D->ZoneA != CurId && D->ZoneB != CurId))
			{
				continue;
			}
			const bool bMineA = D->ZoneA == CurId;
			const FName OtherId = bMineA ? D->ZoneB : D->ZoneA;
			const int32 Other = OtherId.IsNone() ? INDEX_NONE : Zones.IndexOfByPredicate([OtherId](const FZone& Q) { return Q.Id == OtherId; });
			float Y0 = 0.f, Y1 = 0.f;
			if (Other == INDEX_NONE || Other == Current || !D->GetAperture(Y0, Y1))
			{
				continue;
			}
			FPeekWedge W;
			W.Door = D;
			W.Zone = Other;
			W.DoorT = D->GetActorTransform();
			W.DoorT.SetScale3D(FVector::OneVector);
			const float Mine = bMineA ? -1.f : 1.f;	// 내 구역 쪽 로컬 x 부호
			W.Fwd = -Mine;
			const float HalfT = D->WallThickness * 0.5f;
			const FVector L = W.DoorT.InverseTransformPosition(FVector(V.X, V.Y, W.DoorT.GetLocation().Z));
			FVector2D E(L.X, L.Y);
			if (E.X * Mine < HalfT + 1.f)
			{
				E.X = Mine * (HalfT + 1.f);	// 문간에 서 있으면 눈을 내 쪽 벽 면으로 (부채꼴이 거의 반원이 된다)
			}
			W.Eye = E;
			const float NearX = Mine * HalfT;
			W.FarX = -Mine * HalfT;
			W.Fn = (NearX - E.X) * W.Fwd;	// 눈 → 가까운 벽 면 (앞쪽 거리, > 0)
			W.Ff = (W.FarX - E.X) * W.Fwd;	// 눈 → 먼 벽 면
			// 두 벽 면 모두에서 문틈 안을 지나는 옆 기울기 범위 (벽이 두꺼우면 비스듬할수록 좁아진다)
			const float KMin = FMath::Max((Y0 - E.Y) / W.Fn, (Y0 - E.Y) / W.Ff);
			const float KMax = FMath::Min((Y1 - E.Y) / W.Fn, (Y1 - E.Y) / W.Ff);
			float A0 = FMath::Atan(KMin), A1 = FMath::Atan(KMax);
			W.ApA0 = A0;
			W.ApA1 = A1;
			if (bFacing)
			{
				// 문틈 ∩ 바라보는 방향 시야각 (조준 방향 ± 반각). 각도는 문 앞쪽 기준이라 ±2π 로 옮긴 것 중 가장 많이 겹치는 쪽
				const FVector AimL = W.DoorT.InverseTransformVectorNoScale(FRotator(0.f, SmoothAimYaw, 0.f).Vector());
				const float AimA = FMath::Atan2(AimL.Y, AimL.X * W.Fwd);
				float BestLo = 0.f, BestHi = -1.f;
				for (const float Shift : { -2.f * PI, 0.f, 2.f * PI })
				{
					const float Lo = FMath::Max(A0, AimA + Shift - HalfFov);
					const float Hi = FMath::Min(A1, AimA + Shift + HalfFov);
					if (Hi - Lo > BestHi - BestLo)
					{
						BestLo = Lo;
						BestHi = Hi;
					}
				}
				A0 = BestLo;
				A1 = BestHi;
			}
			if (A1 - A0 < 0.002f)
			{
				continue;
			}
			// 기본 광선: DoorPeekRayStepDegrees 간격 (최소 8개, 최대 DoorPeekMaxRays)
			const int32 N = FMath::Clamp(FMath::CeilToInt(FMath::RadiansToDegrees(A1 - A0) / FMath::Max(0.1f, C.DoorPeekRayStepDegrees)) + 1, 8, FMath::Max(8, C.DoorPeekMaxRays));
			TArray<FPeekRay> Rays;
			Rays.Reserve(N * 2);
			for (int32 i = 0; i < N; ++i)
			{
				Rays.Add(CastPeekRay(W, FMath::Lerp(A0, A1, static_cast<float>(i) / static_cast<float>(N - 1)), V, CurMap, Touched));
			}
			// 가장자리 보정: 이웃 광선 길이가 RefineGap 넘게 다르면 사이에 광선을 더 쏜다 (단계마다 반, 최대 RefineDepth 단계)
			const int32 Budget = FMath::Max(8, C.DoorPeekMaxRays) * 4;
			for (int32 Depth = 0; Depth < C.DoorPeekRefineDepth && Rays.Num() < Budget; ++Depth)
			{
				TArray<FPeekRay> Next;
				Next.Reserve(Rays.Num() * 2);
				bool bAdded = false;
				for (int32 i = 0; i < Rays.Num(); ++i)
				{
					Next.Add(Rays[i]);
					if (i + 1 < Rays.Num() && Rays.Num() + (Next.Num() - i - 1) < Budget
						&& FMath::Abs(Rays[i].Dist - Rays[i + 1].Dist) > C.DoorPeekRefineGap && (Rays[i + 1].Angle - Rays[i].Angle) > 0.0005f)
					{
						Next.Add(CastPeekRay(W, (Rays[i].Angle + Rays[i + 1].Angle) * 0.5f, V, CurMap, Touched));
						bAdded = true;
					}
				}
				Rays = MoveTemp(Next);
				if (!bAdded)
				{
					break;
				}
			}
			bool bAny = false;
			for (const FPeekRay& R : Rays)
			{
				W.Angles.Add(R.Angle);
				W.EndDist.Add(R.Dist);
				W.Starts.Add(R.Start);
				W.Ends.Add(R.End);
				bAny |= R.Dist > W.Ff / FMath::Max(0.05f, FMath::Cos(R.Angle)) + 1.f;
				LastPeekCells += R.Cells;
			}
			LastPeekRays += Rays.Num();
			if (bAny)
			{
				Wedges.Add(MoveTemp(W));
			}
		}
		for (int32 i = 0; i < Zones.Num(); ++i)
		{
			Zones[i].bPeek = i != Current && Touched[i];
		}
	}
	LastPeekMs = static_cast<float>((FPlatformTime::Seconds() - T0) * 1000.0);
	PeekMsAvg = PeekMsAvg <= 0.f ? LastPeekMs : FMath::Lerp(PeekMsAvg, LastPeekMs, 0.05f);
	PeekMsMax = FMath::Max(PeekMsMax, LastPeekMs);
	++PeekUpdates;

	// 3) 마스크 그리기 (부채꼴마다 이웃 광선 둘씩 사각형 = 삼각형 2개)
	UWorld* World = GetWorld();
	if (!PeekTarget || (!bPeekDrawn && Wedges.Num() == 0))
	{
		return;
	}
	UKismetRenderingLibrary::ClearRenderTarget2D(World, PeekTarget, FLinearColor::Black);
	bPeekDrawn = Wedges.Num() > 0;
	++PeekDraws;
	if (!bPeekDrawn)
	{
		return;
	}
	TArray<FCanvasUVTri> Tris;
	const double S = static_cast<double>(PeekScale);
	auto Px = [this, S](const FVector2D& P)
	{
		return FVector2D((P.X - ZoneMapParam.X) * ZoneMapParam.Z * S, (P.Y - ZoneMapParam.Y) * ZoneMapParam.Z * S);
	};
	auto AddTri = [&Tris](const FVector2D& A, const FVector2D& B, const FVector2D& Cc)
	{
		FCanvasUVTri& T = Tris.AddDefaulted_GetRef();
		T.V0_Pos = A;
		T.V1_Pos = B;
		T.V2_Pos = Cc;
		T.V0_Color = T.V1_Color = T.V2_Color = FLinearColor::White;
	};
	for (const FPeekWedge& W : Wedges)
	{
		for (int32 i = 0; i + 1 < W.Starts.Num(); ++i)
		{
			const FVector2D S0 = Px(W.Starts[i]), E0 = Px(W.Ends[i]), S1 = Px(W.Starts[i + 1]), E1 = Px(W.Ends[i + 1]);
			AddTri(S0, E0, E1);
			AddTri(S0, E1, S1);
		}
	}
	UCanvas* Canvas = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, PeekTarget, Canvas, Size, Context);
	if (Canvas)
	{
		Canvas->K2_DrawTriangle(nullptr, Tris);
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);
}

bool UVHCeilingSubsystem::IsPointPeeked(const FVector2D& P) const
{
	// 부채꼴(광선 다발) 안인가: 눈에서 이 점까지의 각도를 찾아 양옆 광선 길이를 이어 본다. 너머 구역 여러 개에 걸쳐도 같은 방법
	for (const FPeekWedge& W : Wedges)
	{
		const int32 N = W.Angles.Num();
		if (N < 2)
		{
			continue;
		}
		const FVector L = W.DoorT.InverseTransformPosition(FVector(P.X, P.Y, W.DoorT.GetLocation().Z));
		const float F = (L.X - W.Eye.X) * W.Fwd;
		if (F <= W.Ff)
		{
			continue;	// 문 너머가 아님
		}
		const float A = FMath::Atan2(L.Y - W.Eye.Y, F);
		if (A < W.Angles[0] || A > W.Angles[N - 1])
		{
			continue;
		}
		// 각도는 오름차순 (가장자리 보정 광선이 사이에 들어가 간격이 고르지 않다) → 이진 탐색
		const int32 Hi = FMath::Clamp(Algo::UpperBound(W.Angles, A), 1, N - 1);
		const int32 Lo = Hi - 1;
		const float T = (A - W.Angles[Lo]) / FMath::Max(1e-6f, W.Angles[Hi] - W.Angles[Lo]);
		// 양옆 광선 길이가 비슷하면 이어서, 크게 다르면(벽 모서리) 짧은 쪽으로 (보정 광선 덕에 그 사이 각도는 아주 좁다)
		const float D0 = W.EndDist[Lo], D1 = W.EndDist[Hi];
		const float Reach = FMath::Abs(D1 - D0) < 60.f ? FMath::Lerp(D0, D1, T) : FMath::Min(D0, D1);
		if (FVector2D::Distance(FVector2D(L.X, L.Y), W.Eye) <= Reach + 20.f)
		{
			return true;
		}
	}
	return false;
}

FString UVHCeilingSubsystem::DescribePeek() const
{
	TArray<FString> Parts;
	for (const FPeekWedge& W : Wedges)
	{
		const AVHDoor* D = W.Door.Get();
		float MaxLen = 0.f;
		int32 Open = 0;
		for (int32 i = 0; i < W.Starts.Num(); ++i)
		{
			MaxLen = FMath::Max(MaxLen, W.EndDist[i]);
			Open += FVector2D::Distance(W.Starts[i], W.Ends[i]) > 0.f ? 1 : 0;
		}
		Parts.Add(FString::Printf(TEXT("문 %s → %s: 문틈 %.0f°~%.0f°, 보이는 각도 %.0f°~%.0f°(폭 %.1f°), 광선 %d/%d, 최대 %.0fcm"),
			D ? *D->ServerId.ToString() : TEXT("?"), *Zones[W.Zone].Label,
			FMath::RadiansToDegrees(W.ApA0), FMath::RadiansToDegrees(W.ApA1),
			FMath::RadiansToDegrees(W.Angles[0]), FMath::RadiansToDegrees(W.Angles.Last()),
			FMath::RadiansToDegrees(W.Angles.Last() - W.Angles[0]), Open, W.Angles.Num(), MaxLen));
	}
	TArray<FString> Peeked;
	for (const FZone& Z : Zones)
	{
		if (Z.bPeek)
		{
			Peeked.Add(Z.Label);
		}
	}
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	return FString::Printf(TEXT("내 구역 %s, 바라보는 방향 %.1f°(시야각 %.0f°, %s), 엿보기 %d곳, 보이는 너머 구역 %d [%s], 광선 %d·칸 %d, 계산 %.3fms(평균 %.3f, 최대 %.3f, %d번), 마스크 %d번%s%s"),
		*GetCurrentZoneLabel(), SmoothAimYaw, C.DoorPeekFovDegrees, C.DoorPeekMode == EVHDoorPeekMode::ApertureAndFacing ? TEXT("문틈+방향") : TEXT("문틈만"),
		Wedges.Num(), Peeked.Num(), *FString::Join(Peeked, TEXT(", ")), LastPeekRays, LastPeekCells, LastPeekMs, PeekMsAvg, PeekMsMax, PeekUpdates, PeekDraws,
		Parts.Num() ? TEXT(": ") : TEXT(""), *FString::Join(Parts, TEXT(" | ")));
}
