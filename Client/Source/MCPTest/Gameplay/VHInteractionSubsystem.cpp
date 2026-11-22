#include "Gameplay/VHInteractionSubsystem.h"

#include "Gameplay/VHInteractableBase.h"
#include "Gameplay/VHInteractionAuthority.h"
#include "Gameplay/VHInteractionComponent.h"
#include "Gameplay/VHThiefStateComponent.h"
#include "Gameplay/VHTreasureCarrierComponent.h"
#include "Gameplay/VHDroppedTreasure.h"
#include "Gameplay/VHFlashlightComponent.h"
#include "Gameplay/VHInteractionConfig.h"
#include "Network/VHSettings.h"
#include "MCPTest.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"

#define LOCTEXT_NAMESPACE "VHInteract"

bool UVHInteractionSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer);
}

bool UVHInteractionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UVHInteractionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 기본 담당은 "무엇을 할 수 있는가"만 계산한다(판정 없음). 서버와 게임 중이면 UVHServerGameSubsystem 이 서버용 담당으로 바꾼다
	Authority = NewObject<UVHLocalInteractionAuthority>(this);
	Authority->Init(this);
	DroppedTreasureClass = AVHDroppedTreasure::StaticClass();
	if (UClass* Custom = UVHSettings::Get().GetInteractionConfig().DroppedTreasureClass.LoadSynchronous())
	{
		DroppedTreasureClass = Custom;
	}
}

void UVHInteractionSubsystem::SetAuthority(UVHInteractionAuthority* NewAuthority)
{
	if (NewAuthority)
	{
		Authority = NewAuthority;
		Authority->Init(this);
	}
}

void UVHInteractionSubsystem::Deinitialize()
{
	Interactables.Reset();
	Thieves.Reset();
	Authority = nullptr;
	Super::Deinitialize();
}

double UVHInteractionSubsystem::GetNow() const
{
	if (NowOverride.IsSet())
	{
		return NowOverride.GetValue();
	}
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

// ───────────────────────── 등록부 ─────────────────────────

void UVHInteractionSubsystem::RegisterInteractable(AVHInteractableBase* Target)
{
	if (Target && !Interactables.Contains(Target))
	{
		Interactables.Add(Target);
		Target->SetEntityId(NextEntityId++);
	}
}

void UVHInteractionSubsystem::UnregisterInteractable(AVHInteractableBase* Target)
{
	Interactables.Remove(Target);
}

void UVHInteractionSubsystem::RegisterThief(UVHThiefStateComponent* Thief)
{
	if (Thief && !Thieves.Contains(Thief))
	{
		Thieves.Add(Thief);
		Thief->SetEntityId(NextEntityId++);
		if (UVHTreasureCarrierComponent* Carrier = Thief->GetOwner()->FindComponentByClass<UVHTreasureCarrierComponent>())
		{
			Carrier->OnCarryChanged.AddWeakLambda(this, [this](UVHTreasureCarrierComponent*)
			{
				OnTeamTreasureChanged.Broadcast();
			});
		}
		OnTeamTreasureChanged.Broadcast();
	}
}

void UVHInteractionSubsystem::UnregisterThief(UVHThiefStateComponent* Thief)
{
	Thieves.Remove(Thief);
	OnTeamTreasureChanged.Broadcast();
}

TArray<AVHInteractableBase*> UVHInteractionSubsystem::GetInteractables() const
{
	TArray<AVHInteractableBase*> Out;
	for (const TWeakObjectPtr<AVHInteractableBase>& Weak : Interactables)
	{
		if (AVHInteractableBase* T = Weak.Get())
		{
			Out.Add(T);
		}
	}
	return Out;
}

TArray<UVHThiefStateComponent*> UVHInteractionSubsystem::GetThieves() const
{
	TArray<UVHThiefStateComponent*> Out;
	for (const TWeakObjectPtr<UVHThiefStateComponent>& Weak : Thieves)
	{
		if (UVHThiefStateComponent* T = Weak.Get())
		{
			Out.Add(T);
		}
	}
	return Out;
}

AVHInteractableBase* UVHInteractionSubsystem::FindByEntityId(int32 EntityId) const
{
	for (AVHInteractableBase* T : GetInteractables())
	{
		if (T->GetEntityId() == EntityId)
		{
			return T;
		}
	}
	return nullptr;
}

// ───────────────────────── 결과 반영 ─────────────────────────

void UVHInteractionSubsystem::ApplyInteract(UVHInteractionComponent* Actor, AVHInteractableBase* Target, EVHInteractState State,
	double StartTime, float DurationSeconds, float TotalSeconds, EVHInteractMode Mode, bool bKeepProgress, const FText& ActionText)
{
	if (Target)
	{
		FVHHoldProgress& Progress = Target->GetHoldProgress();
		switch (State)
		{
		case EVHInteractState::Begin:
			Target->SetActiveUser(Actor);
			if (Mode == EVHInteractMode::Hold)
			{
				Progress.BeginWithRemaining(StartTime, DurationSeconds, TotalSeconds);
			}
			break;
		case EVHInteractState::Cancel:
			Target->SetActiveUser(nullptr);
			if (Mode == EVHInteractMode::Hold)
			{
				Progress.Cancel(GetNow(), bKeepProgress);
			}
			break;
		case EVHInteractState::Complete:
			Target->SetActiveUser(nullptr);
			if (Mode == EVHInteractMode::Hold)
			{
				// 완료 후 진행도는 0으로, 한 번만 쓰는 것(금고 등)은 각자 상태로 막는다
				Progress.Reset();
			}
			break;
		}
	}
	if (Actor)
	{
		Actor->HandleInteractState(Target, State, StartTime, DurationSeconds, Mode, ActionText);
	}
	UE_LOG(LogMCPTest, Log, TEXT("[Interact] %s → %s : %s (%.2fs / 전체 %.2fs)"),
		Actor ? *GetNameSafe(Actor->GetOwner()) : TEXT("?"), *GetNameSafe(Target),
		*UEnum::GetValueAsString(State), DurationSeconds, TotalSeconds);
}

void UVHInteractionSubsystem::ApplyReject(UVHInteractionComponent* Actor, EVHRejectReason Reason, const FText& Detail)
{
	if (Actor)
	{
		Actor->HandleReject(Reason, Detail);
	}
	UE_LOG(LogMCPTest, Log, TEXT("[Interact] 거절: %s (%s)"), *UEnum::GetValueAsString(Reason), *Detail.ToString());
}

void UVHInteractionSubsystem::ApplyThiefStatus(UVHThiefStateComponent* Thief, EVHThiefStatus Status)
{
	if (!Thief)
	{
		return;
	}
	Thief->ApplyStatus(Status);
	OnTeamTreasureChanged.Broadcast();
}

FString UVHInteractionSubsystem::GetThiefLabel(const UVHThiefStateComponent* Thief)
{
	if (!Thief)
	{
		return TEXT("?");
	}
	if (!Thief->DisplayName.IsEmpty())
	{
		return Thief->DisplayName;
	}
	// 로그인 이름이 없을 때(방 없이 혼자 ▶ 플레이): 내 의적은 "나", 그 밖은 액터 이름
	const APawn* Pawn = Cast<APawn>(Thief->GetOwner());
	if (Pawn && Pawn->IsPlayerControlled())
	{
		return TEXT("나");
	}
	return GetNameSafe(Thief->GetOwner());
}

AVHDroppedTreasure* UVHInteractionSubsystem::SpawnDroppedTreasure(const FVector& Location, const FVHItemData& Item)
{
	UWorld* World = GetWorld();
	if (!World || !DroppedTreasureClass)
	{
		return nullptr;
	}
	// 물건 값을 BeginPlay 전에 넣어야 하므로 지연 스폰
	const FTransform Transform(FRotator::ZeroRotator, Location);
	AVHDroppedTreasure* Treasure = World->SpawnActorDeferred<AVHDroppedTreasure>(DroppedTreasureClass, Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Treasure)
	{
		Treasure->SetItem(Item);
		Treasure->FinishSpawning(Transform);
	}
	return Treasure;
}

void UVHInteractionSubsystem::ApplyFlashlight(UVHFlashlightComponent* Light, bool bOn)
{
	if (Light)
	{
		Light->ApplyOn(bOn);
	}
}

void UVHInteractionSubsystem::ApplyFlashlightReject(UVHFlashlightComponent* Light, EVHRejectReason Reason)
{
	UE_LOG(LogMCPTest, Log, TEXT("[Flashlight] 거절: %s"), *UEnum::GetValueAsString(Reason));
	// 연타 방지 거절은 조용히 무시, 나머지는 안내
	if (Reason != EVHRejectReason::Cooldown && Light && Light->GetOwner())
	{
		if (UVHInteractionComponent* I = Light->GetOwner()->FindComponentByClass<UVHInteractionComponent>())
		{
			I->HandleReject(Reason, FText::GetEmpty());
		}
	}
}

void UVHInteractionSubsystem::PostMessage(const FText& Message, bool bIsError)
{
	OnWorldMessage.Broadcast(Message, bIsError);
}

#undef LOCTEXT_NAMESPACE
