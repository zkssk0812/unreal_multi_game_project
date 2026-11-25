#include "Gameplay/VHInteractionComponent.h"

#include "Gameplay/VHInteractableBase.h"
#include "Gameplay/VHInteractionSubsystem.h"
#include "Gameplay/VHInteractionAuthority.h"
#include "Gameplay/VHInteractionConfig.h"
#include "Gameplay/VHInteractionRules.h"
#include "Gameplay/VHThiefStateComponent.h"
#include "Gameplay/VHAimComponent.h"
#include "Gameplay/VHActionComponent.h"
#include "Gameplay/VHFlashlightComponent.h"
#include "Gameplay/VHServerGameSubsystem.h"
#include "Gameplay/VHCeilingSubsystem.h"
#include "Gameplay/VHDoorSubsystem.h"
#include "Gameplay/VHDoor.h"
#include "Gameplay/VHStatusEffectComponent.h"
#include "Network/VHSettings.h"
#include "MCPTest.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

UVHInteractionComponent::UVHInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UVHInteractionComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UVHInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsLocallyControlledPlayer())
	{
		if (UVHInteractionSubsystem* Sub = GetSubsystem())
		{
			for (AVHInteractableBase* T : Sub->GetInteractables())
			{
				T->SetOutline(EVHOutline::None);
			}
		}
		if (UVHDoorSubsystem* Doors = GetWorld() ? GetWorld()->GetSubsystem<UVHDoorSubsystem>() : nullptr)
		{
			Doors->SetFocusDoor(nullptr);
		}
	}
	Super::EndPlay(EndPlayReason);
}

UVHInteractionSubsystem* UVHInteractionComponent::GetSubsystem() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UVHInteractionSubsystem>() : nullptr;
}

bool UVHInteractionComponent::IsLocallyControlledPlayer() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn && Pawn->IsLocallyControlled() && Pawn->IsPlayerControlled();
}

void UVHInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsLocallyControlledPlayer())
	{
		return;
	}
	UVHInteractionSubsystem* Sub = GetSubsystem();
	if (!Sub)
	{
		return;
	}
	// 내 눈에 보이는 물품 = 내 손전등 빛 안(가려지지 않음) + 조명과 관계없이 보이는 물품(반납지점) [팀 결정]
	TSet<AVHInteractableBase*> Visible;
	// 손전등을 끈 설정(D3, 2026-11-13)에서는 빛 조건 대신 거리(OutlineRange, 3m) 안의 물품이 보이는 것으로 친다
	const UVHInteractionConfig& VisCfg = UVHSettings::Get().GetInteractionConfig();
	const FVector MyLocation = GetOwner()->GetActorLocation();
	const UVHFlashlightComponent* Light = GetOwner()->FindComponentByClass<UVHFlashlightComponent>();
	for (AVHInteractableBase* T : Sub->GetInteractables())
	{
		// 천장으로 덮인 다른 구역의 물품은 보이지 않으므로 후보·테두리에서 뺀다 (UVHCeilingSubsystem)
		if (!T || UVHCeilingSubsystem::IsActorCovered(T))
		{
			continue;
		}
		// 손전등을 끈 설정: 반납지점도 다른 물품처럼 3m 안일 때만 후보 (멀리서도 테두리가 켜져 있지 않게)
		const bool bSeen = VisCfg.bUseFlashlight
			? (T->IsAlwaysVisible() || (Light && Light->IsBoxLit(T->GetInteractBounds(), T)))
			: T->GetInteractDistance(MyLocation) <= VisCfg.OutlineRange;
		if (bSeen)
		{
			Visible.Add(T);
		}
	}
	HoverTarget = FindHoverTarget();
	TickAutoReturn();

	if (IsApproaching())
	{
		TickApproach();
	}
	if (IsInteracting())
	{
		// 진행 중에는 대상 고정(도중에 빛이 벗어나도 취소하지 않음 [사용자 결정]). 안내 문구만 최신으로
		FocusOffer = Sub->GetAuthority()->Evaluate(this, ActiveTarget.Get());
	}
	else
	{
		UpdateFocus(Visible);
	}
	UpdateOutlines(Visible);
	UpdateDoorOutline();
}

void UVHInteractionComponent::UpdateDoorOutline()
{
	// F 대상 문 테두리 빛 [기획 확인 2026-11-25]: InteractPressed 가 문을 고르는 조건과 같다
	//  = 작업·다가가기·구르기·경직·리스폰 대기가 아니고, 물품이 F 대상이 아니고(없거나 지금 할 수 없음),
	//    고르는 거리(DoorPickRange 130cm) 안의 문을 지금 열고 닫을 수 있을 때(잠긴 문·문간에 서서 닫기 제외)
	UVHDoorSubsystem* Doors = GetWorld() ? GetWorld()->GetSubsystem<UVHDoorSubsystem>() : nullptr;
	if (!Doors)
	{
		return;
	}
	APawn* Pawn = Cast<APawn>(GetOwner());
	const UVHActionComponent* Action = GetOwner()->FindComponentByClass<UVHActionComponent>();
	const UVHThiefStateComponent* ThiefState = GetOwner()->FindComponentByClass<UVHThiefStateComponent>();
	const UVHStatusEffectComponent* Status = GetOwner()->FindComponentByClass<UVHStatusEffectComponent>();
	const bool bCanAct = !IsInteracting() && !IsApproaching() && !(Action && Action->IsBusy())
		&& !(ThiefState && !ThiefState->IsFree()) && !(Status && Status->IsStunned())
		&& (!FocusTarget.IsValid() || !FocusOffer.IsAvailable());
	AVHDoor* Door = bCanAct ? Doors->FindDoorFor(Pawn) : nullptr;
	if (Door && !UVHDoorSubsystem::CanToggleNow(*Door, Pawn))
	{
		Door = nullptr;
	}
	Doors->SetFocusDoor(Door);
}

void UVHInteractionComponent::UpdateOutlines(const TSet<AVHInteractableBase*>& Visible)
{
	UVHInteractionSubsystem* Sub = GetSubsystem();
	AVHInteractableBase* Shown = IsInteracting() ? ActiveTarget.Get() : FocusTarget.Get();
	const bool bLightMode = UVHSettings::Get().GetInteractionConfig().bUseFlashlight;
	const AVHInteractableBase* Hover = HoverTarget.Get();
	for (AVHInteractableBase* T : Sub->GetInteractables())
	{
		EVHOutline State = EVHOutline::None;
		// 3m 안의 물품 + 커서를 올린 물품(거리와 관계없음)에 "상호작용 가능 표시" [기획 2026-11 조작키 2.2]
		if (Visible.Contains(T) || T == Hover)
		{
			if (T == Shown)
			{
				// 현재 상호작용 대상: 조금 더 밝은 노랑, 지금은 할 수 없으면 회색
				State = IsInteracting() || FocusOffer.IsAvailable() ? EVHOutline::Focus : EVHOutline::Blocked;
			}
			else if ((bLightMode && T->IsAlwaysVisible()) || (IsPressTarget(T) && Sub->GetAuthority()->Evaluate(this, T).bHasOffer))
			{
				State = EVHOutline::Lit;	// F로 상호작용할 수 있는 물품 (빈 금고·필드 아이템처럼 할 게 없는 것은 제외)
			}
		}
		T->SetOutline(State);
	}
}

bool UVHInteractionComponent::IsPressTarget(const AVHInteractableBase* Target) const
{
	if (!Target)
	{
		return false;
	}
	// 자동 반납이면 반납지점은 F로 고르지 않는다 (범위에 들어가면 알아서 반납)
	return !(UVHSettings::Get().GetInteractionConfig().bAutoReturn && Target->GetKind() == EVHEntityKind::ReturnPoint);
}

AVHInteractableBase* UVHInteractionComponent::FindHoverTarget() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	const UVHInteractionSubsystem* Sub = GetSubsystem();
	if (!PC || !Sub)
	{
		return nullptr;
	}
	// 커서의 화면 위치. 개발용 봇은 실제 마우스 대신 지정한 물품의 화면 위치를 쓴다(아래 판정은 같다)
	FVector2D Cursor;
	if (bDebugHover)
	{
		const AVHInteractableBase* At = DebugHoverAt.Get();
		if (!At || !PC->ProjectWorldLocationToScreen(At->GetInteractBounds().GetCenter(), Cursor))
		{
			return nullptr;
		}
	}
	else
	{
		float X = 0.f, Y = 0.f;
		if (!PC->GetMousePosition(X, Y))
		{
			return nullptr;
		}
		Cursor = FVector2D(X, Y);
	}

	const float Pad = UVHSettings::Get().GetInteractionConfig().HoverPaddingPixels;
	AVHInteractableBase* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	for (AVHInteractableBase* T : Sub->GetInteractables())
	{
		if (!T || T->IsHidden() || UVHCeilingSubsystem::IsActorCovered(T))
		{
			continue;
		}
		// 물품 경계 상자의 꼭짓점 8개를 화면에 비춰 사각형을 만든다
		const FBox Box = T->GetInteractBounds();
		FBox2D Rect(ForceInit);
		bool bAllOnScreen = true;
		for (int32 i = 0; i < 8 && bAllOnScreen; ++i)
		{
			const FVector Corner((i & 1) ? Box.Max.X : Box.Min.X, (i & 2) ? Box.Max.Y : Box.Min.Y, (i & 4) ? Box.Max.Z : Box.Min.Z);
			FVector2D Screen;
			bAllOnScreen = PC->ProjectWorldLocationToScreen(Corner, Screen);
			Rect += Screen;
		}
		if (!bAllOnScreen || !Rect.ExpandBy(Pad).IsInside(Cursor))
		{
			continue;
		}
		// 겹치면 사각형 가운데가 커서에 더 가까운 것
		const float DistSq = FVector2D::DistSquared(Rect.GetCenter(), Cursor);
		if (DistSq < BestDistSq)
		{
			Best = T;
			BestDistSq = DistSq;
		}
	}
	return Best;
}

void UVHInteractionComponent::TickAutoReturn()
{
	const UVHInteractionConfig& C = UVHSettings::Get().GetInteractionConfig();
	UVHInteractionSubsystem* Sub = GetSubsystem();
	if (!C.bAutoReturn || !Sub || IsInteracting())
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastAutoReturnTime < C.AutoReturnRetrySeconds)
	{
		return;	// 방금 보낸 요청의 응답을 기다린다
	}
	const FVector Location = GetOwner()->GetActorLocation();
	for (AVHInteractableBase* T : Sub->GetInteractables())
	{
		if (!T || T->GetKind() != EVHEntityKind::ReturnPoint || T->GetInteractDistance(Location) > T->GetInteractRange())
		{
			continue;
		}
		// 반납할 것이 있고 지금 할 수 있을 때만 (없으면 조용히 지나간다)
		if (Sub->GetAuthority()->Evaluate(this, T).IsAvailable())
		{
			LastAutoReturnTime = Now;
			UE_LOG(LogMCPTest, Log, TEXT("[Interact] 자동 반납 요청: %s (거리 %.0fcm)"), *GetNameSafe(T), T->GetInteractDistance(Location));
			Sub->GetAuthority()->RequestBegin(this, T);
		}
		return;
	}
}

// ───────────────────────── 대상 찾기 ─────────────────────────

void UVHInteractionComponent::UpdateFocus(const TSet<AVHInteractableBase*>& Visible)
{
	UVHInteractionSubsystem* Sub = GetSubsystem();
	if (!Sub)
	{
		return;
	}
	const UVHInteractionConfig& C = UVHSettings::Get().GetInteractionConfig();
	const AActor* Owner = GetOwner();
	const FVector Location = Owner->GetActorLocation();
	// 대상은 시선(마우스) 방향 기준으로 고른다 [팀 결정]. 시선 컴포넌트가 없으면 몸통 방향
	FVector Forward = Owner->GetActorForwardVector();
	if (const UVHAimComponent* Aim = Owner->FindComponentByClass<UVHAimComponent>())
	{
		Forward = Aim->GetLookDirection();
	}
	Forward.Z = 0.f;
	Forward.Normalize();

	AVHInteractableBase* Best = nullptr;
	FVHInteractOffer BestOffer;
	float BestScore = TNumericLimits<float>::Max();

	for (AVHInteractableBase* Target : Sub->GetInteractables())
	{
		// 빛 안에서만 상호작용 [사용자 결정]: 내 손전등에 보이는 물품·반납지점만 후보
		if (!Target || !Visible.Contains(Target) || !IsPressTarget(Target))
		{
			continue;
		}
		const FBox Box = Target->GetInteractBounds();
		// 높이는 무시하고(탑뷰) 수평 거리로 본다. 반납지점은 가운데에서 원 범위(서버와 같은 100cm)
		const float Distance = Target->GetInteractDistance(Location);
		float Range = Target->GetInteractRange();
		float MaxAngle = C.MaxFacingAngle;
		// 한 번 누르기 방식 [D2]: 3m(OutlineRange) 안이면 후보, 방향은 우선순위에만 쓴다. 반납지점처럼 자기 거리가 있는 것은 그대로
		const bool bWideRange = C.bPressToInteract && FMath::IsNearlyEqual(Range, C.SearchRadius);
		if (C.bPressToInteract)
		{
			MaxAngle = 180.f;
			if (bWideRange)
			{
				Range = C.OutlineRange;
			}
		}
		if (Distance > Range)
		{
			continue;
		}
		FVector ToTarget = Box.GetCenter() - Location;
		ToTarget.Z = 0.f;
		const float AngleDeg = ToTarget.IsNearlyZero() ? 0.f
			: FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, ToTarget.GetSafeNormal()), -1.f, 1.f)));
		float Score = VHRules::TargetScore(Distance, AngleDeg, Range, MaxAngle);
		if (Score < 0.f)
		{
			continue;
		}
		if (C.bPressToInteract)
		{
			// [기획 2026-11 조작키 2.2·2.3] 가장 가까운 것, 단 커서를 올린 물품이 3m 안에 있으면 그것을 먼저
			Score = Target == HoverTarget.Get() ? Distance - 100000.f : Distance;
			// 들고 있는 것과 같은 종류 소비 아이템은 주워도 바뀌는 것이 없다 → 다른 대상이 있으면 그쪽을 먼저 (커서를 올리면 그래도 대상이 되고 F에 "이미 들고 있음" 알림)
			if (const UVHServerGameSubsystem* Server = GetWorld()->GetSubsystem<UVHServerGameSubsystem>(); Server && Server->IsSameAsHeldConsumable(Target))
			{
				Score += 50000.f;
			}
		}
		if (Score >= BestScore)
		{
			continue;
		}
		const FVHInteractOffer Offer = Sub->GetAuthority()->Evaluate(this, Target);
		if (!Offer.bHasOffer)
		{
			continue;
		}
		// 바로 줍는 것은 서버 줍기 거리(3m, 아이템 중심 기준)보다 조금 안쪽에서만
		if (bWideRange && Offer.Mode == EVHInteractMode::Tap && Distance > C.PickupRange)
		{
			continue;
		}
		Best = Target;
		BestOffer = Offer;
		BestScore = Score;
	}
	SetFocus(Best, BestOffer);
}

void UVHInteractionComponent::SetFocus(AVHInteractableBase* NewTarget, const FVHInteractOffer& Offer)
{
	// 테두리 색은 UpdateOutlines 가 매 프레임 정한다
	FocusTarget = NewTarget;
	FocusOffer = Offer;
	bFocusAvailable = Offer.IsAvailable();
}

// ───────────────────────── 입력 ─────────────────────────

void UVHInteractionComponent::InteractPressed()
{
	const UVHInteractionConfig& C = UVHSettings::Get().GetInteractionConfig();
	UVHInteractionSubsystem* Sub = GetSubsystem();
	if (C.bPressToInteract)
	{
		// 한 번 누르기 방식 [D2]: 작업 중이거나 다가가는 중에 다시 누르면 취소
		if (IsInteracting() || IsApproaching())
		{
			StopApproach();
			if (IsInteracting() && Sub)
			{
				Sub->GetAuthority()->RequestCancel(this);
			}
			bInteractHeld = false;
			return;
		}
	}
	bInteractHeld = true;
	if (IsInteracting())
	{
		return;
	}
	// 구르기·공격 중에는 상호작용하지 않는다
	const UVHActionComponent* Action = GetOwner()->FindComponentByClass<UVHActionComponent>();
	AVHInteractableBase* Target = FocusTarget.Get();
	const bool bBusy = Action && Action->IsBusy();
	// 문 [2026-11-24]: 상자·아이템·금고·반납이 F 대상이 아닐 때(없거나 지금 할 수 없을 때)만 앞의 문을 열고 닫는다
	if (!bBusy && (!Sub || !Target || !FocusOffer.IsAvailable()))
	{
		APawn* Pawn = Cast<APawn>(GetOwner());
		UVHDoorSubsystem* Doors = GetWorld() ? GetWorld()->GetSubsystem<UVHDoorSubsystem>() : nullptr;
		if (AVHDoor* Door = Doors ? Doors->FindDoorFor(Pawn) : nullptr)
		{
			FText Why;
			if (!Doors->TryToggle(Pawn, Door, Why))
			{
				OnMessage.Broadcast(Why, true);
			}
			if (C.bPressToInteract)
			{
				bInteractHeld = false;
			}
			return;
		}
	}
	if (bBusy || !Sub || !Target || !FocusOffer.IsAvailable())
	{
		if (!bBusy && Sub && Target)
		{
			// 할 수 없는 이유는 이미 회색 문구로 보이지만, 눌렀을 때 한 번 더 알려준다
			OnMessage.Broadcast(FocusOffer.BlockText, true);
		}
		if (C.bPressToInteract)
		{
			bInteractHeld = false;
		}
		return;
	}
	if (C.bPressToInteract)
	{
		if (FocusOffer.Mode == EVHInteractMode::Hold)
		{
			// 금고·상자: 작업 지점에서 멀면 먼저 다가간다 (도착하면 TickApproach가 요청)
			if (FVector::Dist2D(GetOwner()->GetActorLocation(), Target->GetWorkPoint()) > Target->GetWorkApproachDistance())
			{
				ApproachTarget = Target;
				ApproachStartTime = GetWorld()->GetTimeSeconds();
				UE_LOG(LogMCPTest, Log, TEXT("[Interact] %s 쪽으로 다가가는 중"), *GetNameSafe(Target));
				return;
			}
		}
		else
		{
			bInteractHeld = false;	// 줍기·반납은 누르는 즉시 끝난다
		}
	}
	Sub->GetAuthority()->RequestBegin(this, Target);
}

void UVHInteractionComponent::StopApproach()
{
	ApproachTarget.Reset();
}

void UVHInteractionComponent::TickApproach()
{
	AVHInteractableBase* Target = ApproachTarget.Get();
	APawn* Pawn = Cast<APawn>(GetOwner());
	UVHInteractionSubsystem* Sub = GetSubsystem();
	if (!Target || !Pawn || !Sub || IsInteracting())
	{
		StopApproach();
		return;
	}
	const UVHInteractionConfig& C = UVHSettings::Get().GetInteractionConfig();
	FVector To = Target->GetWorkPoint() - Pawn->GetActorLocation();
	To.Z = 0.f;
	if (To.Size() <= Target->GetWorkApproachDistance())
	{
		StopApproach();
		Sub->GetAuthority()->RequestBegin(this, Target);	// 도착: 작업 요청 (누름 상태는 작업이 끝나거나 취소될 때까지 유지)
		return;
	}
	if (GetWorld()->GetTimeSeconds() - ApproachStartTime > C.WorkApproachTimeout)
	{
		StopApproach();
		bInteractHeld = false;
		OnMessage.Broadcast(NSLOCTEXT("VHInteract", "ApproachFail", "가까이 갈 수 없습니다"), true);
		return;
	}
	Pawn->AddMovementInput(To.GetSafeNormal(), 1.f);
}

void UVHInteractionComponent::InteractReleased()
{
	if (UVHSettings::Get().GetInteractionConfig().bPressToInteract)
	{
		return;	// 한 번 누르기 방식: 키를 떼도 계속한다
	}
	bInteractHeld = false;
	// 길게 누르기만 떼면 취소. 짧게 누르기는 동작이 끝날 때까지 진행
	if (IsInteracting() && ActiveMode == EVHInteractMode::Hold)
	{
		if (UVHInteractionSubsystem* Sub = GetSubsystem())
		{
			Sub->GetAuthority()->RequestCancel(this);
		}
	}
}

void UVHInteractionComponent::NotifyMoveInput()
{
	if (IsApproaching())
	{
		// 다가가는 중에 직접 움직이면(또는 공격·구르기) 그만둔다
		StopApproach();
		bInteractHeld = false;
	}
	if (IsInteracting() && ActiveMode == EVHInteractMode::Hold)
	{
		if (UVHInteractionSubsystem* Sub = GetSubsystem())
		{
			Sub->GetAuthority()->RequestCancel(this);
		}
	}
}

void UVHInteractionComponent::NotifyActionInput()
{
	NotifyMoveInput();
}

// ───────────────────────── 판정 결과 ─────────────────────────

void UVHInteractionComponent::HandleInteractState(AVHInteractableBase* Target, EVHInteractState State, double StartTime, float DurationSeconds, EVHInteractMode Mode, const FText& ActionText)
{
	switch (State)
	{
	case EVHInteractState::Begin:
		ActiveTarget = Target;
		ActiveMode = Mode;
		ActiveActionText = ActionText;
		ActiveStartTime = StartTime;
		ActiveDuration = DurationSeconds;
		LockMovement(true);
		FaceTarget(Target);
		// 서버 응답이 늦게 와서 그 사이 키를 뗐다면 바로 취소
		if (Mode == EVHInteractMode::Hold && !bInteractHeld && IsLocallyControlledPlayer())
		{
			if (UVHInteractionSubsystem* Sub = GetSubsystem())
			{
				Sub->GetAuthority()->RequestCancel(this);
			}
		}
		break;

	case EVHInteractState::Cancel:
	case EVHInteractState::Complete:
		if (ActiveTarget.Get() == Target || !Target)
		{
			ActiveTarget.Reset();
			LockMovement(false);
		}
		// 다음 대상을 곧바로 다시 고르게
		FocusTarget.Reset();
		bFocusAvailable = false;
		if (UVHSettings::Get().GetInteractionConfig().bPressToInteract)
		{
			bInteractHeld = false;
		}
		break;
	}
}

void UVHInteractionComponent::HandleReject(EVHRejectReason Reason, const FText& Detail)
{
	if (UVHSettings::Get().GetInteractionConfig().bPressToInteract)
	{
		StopApproach();
		bInteractHeld = false;
	}
	OnMessage.Broadcast(Detail.IsEmpty() ? VHInteract::RejectReasonText(Reason) : Detail, true);
}

float UVHInteractionComponent::GetActiveProgress() const
{
	const AVHInteractableBase* Target = ActiveTarget.Get();
	const UVHInteractionSubsystem* Sub = GetSubsystem();
	if (!Target || !Sub)
	{
		return 0.f;
	}
	const double Now = Sub->GetNow();
	if (ActiveMode == EVHInteractMode::Hold)
	{
		return Target->GetHoldProgress().GetProgress(Now);
	}
	return ActiveDuration > 0.f ? FMath::Clamp(static_cast<float>((Now - ActiveStartTime) / ActiveDuration), 0.f, 1.f) : 1.f;
}

float UVHInteractionComponent::GetActiveRemainingSeconds() const
{
	const AVHInteractableBase* Target = ActiveTarget.Get();
	const UVHInteractionSubsystem* Sub = GetSubsystem();
	if (!Target || !Sub)
	{
		return 0.f;
	}
	const double Now = Sub->GetNow();
	if (ActiveMode == EVHInteractMode::Hold)
	{
		return Target->GetHoldProgress().GetRemainingSeconds(Now);
	}
	return FMath::Max(0.f, static_cast<float>(ActiveStartTime + ActiveDuration - Now));
}

// ───────────────────────── 이동 잠금 ─────────────────────────

void UVHInteractionComponent::LockMovement(bool bLock)
{
	if (bMovementLocked == bLock)
	{
		return;
	}
	bMovementLocked = bLock;
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}
	UCharacterMovementComponent* Move = Character->GetCharacterMovement();
	if (bLock)
	{
		// 상호작용 중에는 제자리 (서버 ES_INTERACTING). 이동 불가 = 점프도 불가
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}
	else
	{
		// 사망 등으로 상태가 바뀌었으면 그 상태가 이동을 관리한다
		const UVHThiefStateComponent* State = Character->FindComponentByClass<UVHThiefStateComponent>();
		if (!State || State->IsFree())
		{
			Move->SetMovementMode(MOVE_Walking);
		}
	}
}

void UVHInteractionComponent::FaceTarget(const AVHInteractableBase* Target)
{
	AActor* Owner = GetOwner();
	if (!Target || !Owner)
	{
		return;
	}
	if (const APawn* Pawn = Cast<APawn>(Owner); Pawn && Pawn->IsPlayerControlled())
	{
		return;	// 내 캐릭터는 항상 커서를 바라본다(대상도 커서 방향으로 고른다) ⟪기획 변경 2026-10-26⟫
	}
	FVector To = Target->GetInteractBounds().GetCenter() - Owner->GetActorLocation();
	To.Z = 0.f;
	if (!To.IsNearlyZero())
	{
		Owner->SetActorRotation(To.Rotation());
	}
}

void UVHInteractionComponent::DebugPressOn(AVHInteractableBase* Target)
{
	UVHInteractionSubsystem* Sub = GetSubsystem();
	if (!Sub || !Target || IsInteracting())
	{
		return;
	}
	bInteractHeld = true;
	Sub->GetAuthority()->RequestBegin(this, Target);
}
