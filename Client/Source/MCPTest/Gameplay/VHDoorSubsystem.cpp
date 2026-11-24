#include "Gameplay/VHDoorSubsystem.h"

#include "Gameplay/VHDoor.h"
#include "Gameplay/VHGameplayConfig.h"
#include "Gameplay/VHInteractionComponent.h"
#include "Gameplay/VHInteractableBase.h"
#include "Network/VHSettings.h"
#include "MCPTest.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

#define LOCTEXT_NAMESPACE "VHDoor"

namespace
{
	/** 지금 쓰는 출처: 내 클라. F 를 누르면 바로 바뀐다 (다른 클라와는 맞춰지지 않음 — 서버 상태가 생기면 바꿔 끼운다) */
	class FVHLocalDoorStateSource final : public IVHDoorStateSource
	{
	public:
		virtual const TCHAR* GetName() const override { return TEXT("로컬(내 클라)"); }
		virtual bool GetInitialOpen(const AVHDoor& Door) const override { return false; }	// 처음엔 모두 닫힘
		virtual void RequestSetOpen(UVHDoorSubsystem& Sub, AVHDoor& Door, bool bOpen) override
		{
			Sub.ApplyDoorState(&Door, bOpen, TEXT("로컬 F"));
		}
	};

	// vh.PressF : F 키를 한 번 누른 것과 같은 길 (상호작용 컴포넌트 InteractPressed → 물품이 없으면 문)
	FAutoConsoleCommandWithWorldAndArgs GVHPressFCommand(TEXT("vh.PressF"),
		TEXT("개발용: vh.PressF — F 키를 한 번 누른 것처럼 (상자·아이템·반납이 F 대상이 아니면 앞의 문을 열고 닫는다)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			UVHInteractionComponent* I = Pawn ? Pawn->FindComponentByClass<UVHInteractionComponent>() : nullptr;
			if (!I)
			{
				return;
			}
			const UVHDoorSubsystem* Sub = World->GetSubsystem<UVHDoorSubsystem>();
			const AVHDoor* Door = Sub ? Sub->FindDoorFor(Pawn) : nullptr;
			UE_LOG(LogMCPTest, Log, TEXT("[Door] vh.PressF: 물품 대상 %s, 앞의 문 %s, 내 위치 (%.0f, %.0f)"), *GetNameSafe(I->GetFocusTarget()),
				Door ? *Door->Describe() : TEXT("없음"), Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y);
			I->InteractPressed();
			I->InteractReleased();
		}));

	// vh.DoorSet open|close|toggle all|near|<문 id> : 개발용 문 열기/닫기 (상태 출처를 거친다)
	FAutoConsoleCommandWithWorldAndArgs GVHDoorSetCommand(TEXT("vh.DoorSet"),
		TEXT("개발용: vh.DoorSet open|close|toggle [all|near|문 id] — 문을 열거나 닫는다(기본 near = 내 캐릭터에서 가장 가까운 문)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UVHDoorSubsystem* Sub = World ? World->GetSubsystem<UVHDoorSubsystem>() : nullptr;
			if (!Sub || Args.Num() < 1)
			{
				return;
			}
			const FString Op = Args[0].ToLower();
			const FString Which = Args.Num() > 1 ? Args[1] : TEXT("near");
			const APlayerController* PC = World->GetFirstPlayerController();
			const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			TArray<AVHDoor*> Targets;
			if (Which.Equals(TEXT("all"), ESearchCase::IgnoreCase))
			{
				for (const TWeakObjectPtr<AVHDoor>& D : Sub->GetDoors())
				{
					if (D.IsValid())
					{
						Targets.Add(D.Get());
					}
				}
			}
			else if (Which.Equals(TEXT("near"), ESearchCase::IgnoreCase))
			{
				AVHDoor* Best = nullptr;
				for (const TWeakObjectPtr<AVHDoor>& D : Sub->GetDoors())
				{
					if (D.IsValid() && Pawn && (!Best || FVector::Dist2D(D->GetActorLocation(), Pawn->GetActorLocation()) < FVector::Dist2D(Best->GetActorLocation(), Pawn->GetActorLocation())))
					{
						Best = D.Get();
					}
				}
				if (Best)
				{
					Targets.Add(Best);
				}
			}
			else
			{
				for (const TWeakObjectPtr<AVHDoor>& D : Sub->GetDoors())
				{
					if (D.IsValid() && D->ServerId.ToString().Equals(Which, ESearchCase::IgnoreCase))
					{
						Targets.Add(D.Get());
					}
				}
			}
			for (AVHDoor* D : Targets)
			{
				if (D->bLocked)
				{
					continue;
				}
				const bool bOpen = Op == TEXT("open") ? true : (Op == TEXT("close") ? false : !D->IsOpen());
				Sub->RequestSetOpen(D, bOpen);
			}
			UE_LOG(LogMCPTest, Log, TEXT("[Door] vh.DoorSet %s %s → 문 %d개"), *Op, *Which, Targets.Num());
		}));
}

bool UVHDoorSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UVHDoorSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	EnsureSource();
	CollectDoors();
}

void UVHDoorSubsystem::Deinitialize()
{
	Doors.Reset();
	Source.Reset();
	Super::Deinitialize();
}

void UVHDoorSubsystem::EnsureSource()
{
	if (Source)
	{
		return;
	}
	const EVHDoorStateSource Want = UVHSettings::Get().GetGameplayConfig().DoorStateSource;
	if (Want == EVHDoorStateSource::Server)
	{
		// 프로토콜에 문 상태가 아직 없다 → 서버용 출처를 만들 때까지 로컬로
		UE_LOG(LogMCPTest, Warning, TEXT("[Door] 문 상태 출처 = 서버로 설정됐지만 프로토콜에 문 상태가 아직 없어 로컬로 동작합니다"));
	}
	Source = MakeUnique<FVHLocalDoorStateSource>();
}

const TCHAR* UVHDoorSubsystem::GetSourceName() const
{
	return Source ? Source->GetName() : TEXT("없음");
}

void UVHDoorSubsystem::CollectDoors()
{
	if (bCollected || !GetWorld())
	{
		return;
	}
	bCollected = true;
	int32 Locked = 0;
	for (TActorIterator<AVHDoor> It(GetWorld()); It; ++It)
	{
		Doors.Add(*It);
		Locked += It->bLocked ? 1 : 0;
	}
	if (Doors.Num() > 0)
	{
		UE_LOG(LogMCPTest, Log, TEXT("[Door] 문 %d개 (잠김 %d), 상태 출처 %s, 처음엔 모두 닫힘, F로 열고 닫기"), Doors.Num(), Locked, GetSourceName());
	}
}

bool UVHDoorSubsystem::GetInitialOpen(const AVHDoor& Door)
{
	EnsureSource();
	return !Door.bLocked && Source->GetInitialOpen(Door);
}

void UVHDoorSubsystem::RequestSetOpen(AVHDoor* Door, bool bOpen)
{
	EnsureSource();
	if (Door && !Door->bLocked)
	{
		Source->RequestSetOpen(*this, *Door, bOpen);
	}
}

void UVHDoorSubsystem::ApplyDoorState(FName DoorId, bool bOpen, const TCHAR* Why)
{
	CollectDoors();
	for (const TWeakObjectPtr<AVHDoor>& D : Doors)
	{
		if (D.IsValid() && D->ServerId == DoorId)
		{
			ApplyDoorState(D.Get(), bOpen, Why);
			return;
		}
	}
	UE_LOG(LogMCPTest, Warning, TEXT("[Door] 모르는 문 id %s (%s)"), *DoorId.ToString(), Why);
}

void UVHDoorSubsystem::ApplyDoorState(AVHDoor* Door, bool bOpen, const TCHAR* Why)
{
	if (!Door || Door->bLocked || Door->IsOpen() == bOpen)
	{
		return;
	}
	Door->SetOpenState(bOpen);
	UE_LOG(LogMCPTest, Log, TEXT("[Door] %s %s (%s, 시각 %.2f)"), *Door->Describe(), bOpen ? TEXT("열림") : TEXT("닫힘"), Why, GetWorld()->GetTimeSeconds());
	OnDoorStateChanged.Broadcast(Door, bOpen);
}

AVHDoor* UVHDoorSubsystem::FindDoorFor(const APawn* Pawn) const
{
	if (!Pawn)
	{
		return nullptr;
	}
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	AVHDoor* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<AVHDoor>& Weak : Doors)
	{
		AVHDoor* D = Weak.Get();
		if (!D || !D->IsDoorEnabled())
		{
			continue;
		}
		const FVector L = D->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
		const float Depth = FMath::Abs(L.X) - D->WallThickness * 0.5f;
		const float Side = FMath::Abs(L.Y) - D->Width * 0.5f;
		if (Depth > C.DoorInteractDepth || Side > C.DoorInteractSideMargin || L.Z < -300.f || L.Z > 400.f)
		{
			continue;
		}
		const float Score = FMath::Max(0.f, Depth) + FMath::Max(0.f, Side) * 2.f;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = D;
		}
	}
	return Best;
}

bool UVHDoorSubsystem::IsPawnInDoorway(const AVHDoor& Door, const APawn* Pawn)
{
	if (!Pawn)
	{
		return false;
	}
	float Radius = 35.f;
	if (const UCapsuleComponent* Cap = Pawn->FindComponentByClass<UCapsuleComponent>())
	{
		Radius = Cap->GetScaledCapsuleRadius();
	}
	const FVector L = Door.GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
	return FMath::Abs(L.X) < Door.WallThickness * 0.5f + Radius + 2.f && FMath::Abs(L.Y) < Door.Width * 0.5f + Radius;
}

bool UVHDoorSubsystem::TryToggle(APawn* Pawn, AVHDoor* Door, FText& OutMessage)
{
	if (!Door)
	{
		return false;
	}
	if (Door->bLocked)
	{
		OutMessage = LOCTEXT("Locked", "잠긴 문입니다 — 열 수 없습니다");
		UE_LOG(LogMCPTest, Log, TEXT("[Door] %s 잠긴 문 F → 거절"), *Door->Describe());
		return false;
	}
	const bool bOpen = !Door->IsOpen();
	if (!bOpen && IsPawnInDoorway(*Door, Pawn))
	{
		OutMessage = LOCTEXT("InDoorway", "문간에 서 있어서 닫을 수 없습니다 — 한 걸음 비키세요");
		UE_LOG(LogMCPTest, Log, TEXT("[Door] %s 문간에 서서 닫기 → 거절"), *Door->Describe());
		return false;
	}
	RequestSetOpen(Door, bOpen);
	return true;
}

#undef LOCTEXT_NAMESPACE
