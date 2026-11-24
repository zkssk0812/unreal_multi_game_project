#include "Game/VHGamePlayerController.h"

#include "Gameplay/VHInteractionComponent.h"
#include "Gameplay/VHInteractionSubsystem.h"
#include "Gameplay/VHInteractionConfig.h"
#include "Gameplay/VHTreasureCarrierComponent.h"
#include "Gameplay/VHThiefStateComponent.h"
#include "Gameplay/VHFlashlightComponent.h"
#include "Gameplay/VHInteractionAuthority.h"
#include "Gameplay/VHAimComponent.h"
#include "Gameplay/VHActionComponent.h"
#include "Gameplay/VHStatusEffectComponent.h"
#include "Gameplay/VHGameplayConfig.h"
#include "Gameplay/VHMatchSubsystem.h"
#include "Gameplay/VHServerGameSubsystem.h"
#include "Gameplay/VHReturnPoint.h"
#include "Gameplay/VHLevelViewSettings.h"
#include "Gameplay/VHNetProxy.h"
#include "Network/VHNetSubsystem.h"
#include "GameFramework/PlayerStart.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Containers/Ticker.h"
#include "UObject/UObjectIterator.h"
#include "EngineUtils.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Network/VHSettings.h"
#include "UI/VHGameHUDWidget.h"
#include "UI/VHCodeWidgets.h"
#include "UI/VHCrosshairWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "MCPTest.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "VHGamePC"

void AVHGamePlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalPlayerController())
	{
		return;
	}
	ApplyGameInputMode();

	HUD = CreateWidget<UVHGameHUDWidget>(this, HUDClass ? HUDClass.Get() : UVHGameHUDWidget::StaticClass());
	if (HUD)
	{
		HUD->AddToViewport(0);
	}
	Toast = CreateWidget<UVHToastWidget>(this, UVHToastWidget::StaticClass());
	if (Toast)
	{
		Toast->AddToViewport(10);
		Toast->SetPadding(FMargin(0.f, 84.f, 0.f, 0.f));	// 위 가운데의 남은 시간 칸(HUD)을 가리지 않게 그 아래에 띄운다
	}
	// 조준점: HUD·알림보다 위(Z 100) → 천장·문 가림·상호작용 안내와 겹쳐도 맨 위에 보인다
	Crosshair = CreateWidget<UVHCrosshairWidget>(this, UVHCrosshairWidget::StaticClass());
	if (Crosshair)
	{
		Crosshair->AddToViewport(100);
	}
	UpdateCrosshairCursor();
	// 일시정지 중에도 커서를 되돌려야 하므로(PlayerTick 은 멈춤) 늘 도는 코어 티커로 매 프레임 갱신
	CursorTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		UpdateCrosshairCursor();
		return true;
	}));
	if (UVHInteractionSubsystem* Sub = GetInteractionSubsystem())
	{
		Sub->OnWorldMessage.AddUObject(this, &AVHGamePlayerController::HandleMessage);
	}
	BindPawnEvents();
	ApplyOutlinePostProcess();
	// 한 판 시작: HUD 시계를 돌린다 (시간·금액·반납지점은 서버 값으로 채워진다)
	if (UVHMatchSubsystem* Match = GetWorld()->GetSubsystem<UVHMatchSubsystem>())
	{
		Match->StartMatch();
	}
	ApplyMyDisplayName();
	// 규칙 안내. 시간은 설정값(TimeLimitSeconds)으로
	{
		const int32 Limit = FMath::Max(0, FMath::RoundToInt(UVHSettings::Get().GetGameplayConfig().TimeLimitSeconds));
		const FText Duration = Limit % 60 == 0
			? FText::Format(LOCTEXT("DurationMin", "{0}분"), Limit / 60)
			: FText::Format(LOCTEXT("DurationMinSec", "{0}분 {1}초"), Limit / 60, Limit % 60);
		HandleMessage(FText::Format(LOCTEXT("Welcome", "{0} 동안 가장 많이 반납한 사람이 승리! 금고를 열어 보물을 반납지점(초록 사각형)에 반납하세요  (개발용 명령: 콘솔 ~ → VHHelp)"),
			Duration), false);
	}
}

void AVHGamePlayerController::ApplyGameInputMode()
{
	// 시선 = 마우스 포인터 [기획]. 커서를 보이게 하고, 클릭(공격·상호작용)은 게임 입력으로 받는다.
	SetShowMouseCursor(true);
	DefaultMouseCursor = EMouseCursor::Crosshairs;
	CurrentMouseCursor = EMouseCursor::Crosshairs;
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockInFullscreen);
	SetInputMode(Mode);
}

void AVHGamePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CursorTicker.IsValid())
	{
		FTSTicker::RemoveTicker(CursorTicker);
		CursorTicker.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void AVHGamePlayerController::UpdateCrosshairCursor()
{
	const bool bPaused = GetWorld() && GetWorld()->IsPaused();
	const bool bEnabled = Crosshair && UVHCrosshairWidget::IsEnabledByConfig();
	const bool bShow = bEnabled && !bPaused;
	if (Crosshair)
	{
		const ESlateVisibility Want = bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
		if (Crosshair->GetVisibility() != Want)
		{
			Crosshair->SetVisibility(Want);
		}
	}
	// 조준점이 보이면 운영체제 커서는 숨긴다(None). 꺼짐 = 예전 십자 커서, 일시정지 = 보통 화살표
	const EMouseCursor::Type Cursor = bShow ? EMouseCursor::None : (bPaused ? EMouseCursor::Default : EMouseCursor::Crosshairs);
	if (CurrentMouseCursor != Cursor || DefaultMouseCursor != Cursor)
	{
		CurrentMouseCursor = Cursor;
		DefaultMouseCursor = Cursor;
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().QueryCursor();	// 마우스를 움직이지 않아도 바로 바뀌게
		}
		UE_LOG(LogTemp, Log, TEXT("[조준점] 운영체제 커서 → %s (조준점 %s%s)"), bShow ? TEXT("숨김") : (bPaused ? TEXT("보통 화살표") : TEXT("십자")),
			bEnabled ? TEXT("켜짐") : TEXT("꺼짐"), bPaused ? TEXT(", 일시정지") : TEXT(""));
	}
}

void AVHGamePlayerController::UpdateRotation(float DeltaTime)
{
	// 템플릿의 마우스 시점 입력(IA_Look/IA_MouseLook)은 무시한다(카메라는 탑뷰 고정).
	// 컨트롤 회전 = 커서 방향 ⟪기획 변경 2026-10-26⟫ → 몸이 이 방향으로 돈다(UVHAimComponent가 회전 모드 설정).
	// 이동 방향은 AMCPTestCharacter::DoMove에서 같은 커서 방향 기준으로 계산한다.
	RotationInput = FRotator::ZeroRotator;
	const APawn* MyPawn = GetPawn();
	const UVHAimComponent* Aim = MyPawn ? MyPawn->FindComponentByClass<UVHAimComponent>() : nullptr;
	SetControlRotation(FRotator(0.f, Aim ? Aim->GetAimYaw() : 0.f, 0.f));
}

void AVHGamePlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	BindPawnEvents();
	ApplyOutlinePostProcess();
	// 소리 거리 감쇠는 탑뷰 카메라(높이 십수 m)가 아니라 내 캐릭터 위치 기준 (좌우 방향은 카메라 그대로) — 2026-11-19
	// 이게 없으면 모든 효과음이 카메라 높이만큼 멀어서 작게 들리고, 가까이서만 들리는 반복음(빔 소드 웅)은 아예 안 들렸다
	if (InPawn && IsLocalPlayerController())
	{
		SetAudioListenerAttenuationOverride(InPawn->GetRootComponent(), FVector::ZeroVector);
	}
	if (GetWorld() && GetWorld()->HasBegunPlay() && IsLocalPlayerController())
	{
		ApplyMyDisplayName();
	}
}

void AVHGamePlayerController::ApplyMyDisplayName()
{
	UVHThiefStateComponent* State = GetThiefState();
	const UVHNetSubsystem* Net = UVHNetSubsystem::Get(this);
	if (State && Net && !Net->GetMyName().IsEmpty())
	{
		State->DisplayName = Net->GetMyName();
	}
}

void AVHGamePlayerController::ApplyOutlinePostProcess()
{
	APawn* MyPawn = GetPawn();
	UCameraComponent* Camera = MyPawn ? MyPawn->FindComponentByClass<UCameraComponent>() : nullptr;
	const UVHGameplayConfig& G = UVHSettings::Get().GetGameplayConfig();
	UMaterialInterface* Base = G.OutlineMaterial.LoadSynchronous();
	if (!Camera || !Base)
	{
		return;
	}
	// 테두리는 커스텀 스텐실로 그린다 → 프로젝트 설정 r.CustomDepth=3(스텐실 포함)이 필요. 혹시 꺼져 있으면 켠다
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth")))
	{
		if (CVar->GetInt() < 3)
		{
			CVar->Set(3, ECVF_SetByCode);
		}
	}
	if (!OutlineMID)
	{
		OutlineMID = UMaterialInstanceDynamic::Create(Base, this);
	}
	OutlineMID->SetVectorParameterValue(TEXT("LitColor"), G.OutlineLitColor);
	OutlineMID->SetVectorParameterValue(TEXT("FocusColor"), G.OutlineFocusColor);
	OutlineMID->SetVectorParameterValue(TEXT("BlockedColor"), G.OutlineBlockedColor);
	OutlineMID->SetScalarParameterValue(TEXT("Thickness"), G.OutlineThickness);
	TArray<FWeightedBlendable>& Blendables = Camera->PostProcessSettings.WeightedBlendables.Array;
	if (!Blendables.ContainsByPredicate([this](const FWeightedBlendable& B) { return B.Object == OutlineMID; }))
	{
		Blendables.Add(FWeightedBlendable(1.f, OutlineMID));
	}
}

void AVHGamePlayerController::BindPawnEvents()
{
	UVHInteractionComponent* Interaction = GetInteraction();
	if (Interaction && BoundInteraction.Get() != Interaction)
	{
		Interaction->OnMessage.AddUObject(this, &AVHGamePlayerController::HandleMessage);
		BoundInteraction = Interaction;
	}
}

void AVHGamePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!IsLocalPlayerController())
	{
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (GameplayMappingContext)
		{
			Subsystem->AddMappingContext(GameplayMappingContext, 1);
		}
	}
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		// 예전 방식(우클릭·G를 누르고 있는 동안 작업)은 "한 번 누르기" 설정을 껐을 때만 쓴다 [D2: F만 사용]
		if (InteractAction && !UVHSettings::Get().GetInteractionConfig().bPressToInteract)
		{
			EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &AVHGamePlayerController::OnInteractStarted);
			EIC->BindAction(InteractAction, ETriggerEvent::Completed, this, &AVHGamePlayerController::OnInteractCompleted);
		}
		if (SprintAction)
		{
			EIC->BindAction(SprintAction, ETriggerEvent::Started, this, &AVHGamePlayerController::OnSprintStarted);
			EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &AVHGamePlayerController::OnSprintCompleted);
		}
		if (MoveAction)
		{
			EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AVHGamePlayerController::OnMoveTriggered);
		}
		if (JumpAction)
		{
			EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &AVHGamePlayerController::OnJumpStarted);
		}
		if (RollAction)
		{
			EIC->BindAction(RollAction, ETriggerEvent::Started, this, &AVHGamePlayerController::OnRollStarted);
		}
		if (AttackAction)
		{
			// 꾹 누르면 공격 [PvP 규칙 v1 9장]: 누름(Started) → 공격 시작, 뗌(Completed)·취소(Canceled) → 멈춤.
			// IA_VHAttack 에는 트리거가 없다(기본 = 누르고 있는 동안 Triggered) → 떼면 Completed 가 온다
			EIC->BindAction(AttackAction, ETriggerEvent::Started, this, &AVHGamePlayerController::OnAttackStarted);
			EIC->BindAction(AttackAction, ETriggerEvent::Completed, this, &AVHGamePlayerController::OnAttackCompleted);
			EIC->BindAction(AttackAction, ETriggerEvent::Canceled, this, &AVHGamePlayerController::OnAttackCompleted);
		}
		if (FlashlightAction)
		{
			EIC->BindAction(FlashlightAction, ETriggerEvent::Started, this, &AVHGamePlayerController::OnFlashlightStarted);
		}
	}
	else
	{
		UE_LOG(LogMCPTest, Error, TEXT("VHGamePlayerController: Enhanced Input 컴포넌트가 없습니다"));
	}

	// 조작키 [사용자 결정 2026-11-13]: F 상호작용, 마우스 휠 무기 교체, Q 스킬, E 소비 아이템.
	// 입력 에셋을 늘리지 않고 키를 직접 묶었다(키 바꾸기 설정을 만들 때 입력 에셋으로 옮긴다)
	if (InputComponent)
	{
		if (UVHSettings::Get().GetInteractionConfig().bPressToInteract)
		{
			InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AVHGamePlayerController::OnInteractStarted);
			InputComponent->BindKey(EKeys::F, IE_Released, this, &AVHGamePlayerController::OnInteractCompleted);
		}
		InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AVHGamePlayerController::OnSwitchWeaponKey);
		InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AVHGamePlayerController::OnSwitchWeaponKey);
		InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AVHGamePlayerController::OnSkillKey);
		InputComponent->BindKey(EKeys::E, IE_Pressed, this, &AVHGamePlayerController::OnConsumableKey);
	}
}

// ───────────────────────── 입력 전달 ─────────────────────────

void AVHGamePlayerController::OnSwitchWeaponKey()
{
	// 교체 요청만 보낸다(든 칸은 SC_PLAYER_STATUS로 돌아온다)
	if (UVHServerGameSubsystem* Server = GetWorld()->GetSubsystem<UVHServerGameSubsystem>(); Server && Server->IsActive())
	{
		Server->SendSwitchWeapon();
	}
}

void AVHGamePlayerController::OnSkillKey()
{
	// 도적1(사제 테이저건)은 들기/넣기, 다른 스킬은 바로 사용 (UVHServerGameSubsystem::PressSkillKey)
	if (UVHServerGameSubsystem* Server = GetWorld()->GetSubsystem<UVHServerGameSubsystem>(); Server && Server->IsActive())
	{
		Server->PressSkillKey();
	}
}

void AVHGamePlayerController::OnConsumableKey()
{
	if (UVHServerGameSubsystem* Server = GetWorld()->GetSubsystem<UVHServerGameSubsystem>(); Server && Server->IsActive())
	{
		Server->SendUseConsumable();
	}
}

void AVHGamePlayerController::OnInteractStarted()
{
	BindPawnEvents();
	if (UVHInteractionComponent* I = GetInteraction())
	{
		I->InteractPressed();
	}
}

void AVHGamePlayerController::OnInteractCompleted()
{
	if (UVHInteractionComponent* I = GetInteraction())
	{
		I->InteractReleased();
	}
}

void AVHGamePlayerController::OnSprintStarted()
{
	if (UVHActionComponent* A = GetAction())
	{
		A->SetWantsSprint(true);
	}
}

void AVHGamePlayerController::OnSprintCompleted()
{
	if (UVHActionComponent* A = GetAction())
	{
		A->SetWantsSprint(false);
	}
}

void AVHGamePlayerController::OnRollStarted()
{
	// 길게 누르는 상호작용 중이면 Space는 취소만 한다 [유지]. 아니면 구르기
	UVHInteractionComponent* I = GetInteraction();
	if (I && (I->IsInteracting() || I->IsApproaching()))
	{
		I->NotifyActionInput();
		return;
	}
	if (UVHActionComponent* A = GetAction())
	{
		const bool bWasRolling = A->IsRolling();
		A->RequestRoll();
		// 실서버 판정: 구르기 시작을 서버에 알린다(SP 소비·무적 판정은 서버, 이동은 클라)
		UVHServerGameSubsystem* Server = GetWorld()->GetSubsystem<UVHServerGameSubsystem>();
		if (Server && Server->IsActive() && !bWasRolling && A->IsRolling() && GetPawn())
		{
			Server->SendDodge();
		}
	}
}

void AVHGamePlayerController::OnMoveTriggered(const FInputActionValue& Value)
{
	if (UVHInteractionComponent* I = GetInteraction())
	{
		I->NotifyMoveInput();
	}
}

void AVHGamePlayerController::OnJumpStarted()
{
	if (UVHInteractionComponent* I = GetInteraction())
	{
		I->NotifyActionInput();
	}
}

void AVHGamePlayerController::OnFlashlightStarted()
{
	// 손전등은 꺼 둔다 [사용자 결정 2026-11-13, D3]. DA_VHInteractionConfig → Use Flashlight를 켜면 예전처럼 동작
	if (!UVHSettings::Get().GetInteractionConfig().bUseFlashlight)
	{
		return;
	}
	if (UVHFlashlightComponent* Light = GetPawn() ? GetPawn()->FindComponentByClass<UVHFlashlightComponent>() : nullptr)
	{
		Light->RequestToggle();
	}
}

void AVHGamePlayerController::VHFlashlight()
{
	OnFlashlightStarted();
}

void AVHGamePlayerController::HandleMessage(const FText& Message, bool bIsError)
{
	if (Toast && !Message.IsEmpty())
	{
		Toast->AddMessage(Message, bIsError);
	}
}

// ───────────────────────── 도우미 ─────────────────────────

UVHInteractionComponent* AVHGamePlayerController::GetInteraction() const
{
	return GetPawn() ? GetPawn()->FindComponentByClass<UVHInteractionComponent>() : nullptr;
}

UVHTreasureCarrierComponent* AVHGamePlayerController::GetCarrier() const
{
	return GetPawn() ? GetPawn()->FindComponentByClass<UVHTreasureCarrierComponent>() : nullptr;
}

UVHThiefStateComponent* AVHGamePlayerController::GetThiefState() const
{
	return GetPawn() ? GetPawn()->FindComponentByClass<UVHThiefStateComponent>() : nullptr;
}

UVHActionComponent* AVHGamePlayerController::GetAction() const
{
	return GetPawn() ? GetPawn()->FindComponentByClass<UVHActionComponent>() : nullptr;
}

UVHInteractionSubsystem* AVHGamePlayerController::GetInteractionSubsystem() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UVHInteractionSubsystem>() : nullptr;
}

void AVHGamePlayerController::DebugPrint(const FString& Line) const
{
	UE_LOG(LogMCPTest, Display, TEXT("%s"), *Line);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Cyan, Line);
	}
}

// ───────────────────────── 개발용 콘솔 명령 ─────────────────────────

void AVHGamePlayerController::VHHelp()
{
	const TCHAR* Lines[] = {
		TEXT("(대미지·금액·시간 같은 규칙 값은 서버가 정하므로 바꾸는 명령이 없습니다)"),
		TEXT("VHFastInteract [배율]   : 안내에 보이는 상호작용 시간 배율(0.1=10배 빠르게, 1=원래)"),
		TEXT("VHHoldInteract [초]     : 상호작용 키를 N초 누르고 있기 / VHInteractUp"),
		TEXT("VHGoto 이름             : 오브젝트·PlayerStart·액터 라벨로 순간이동 (예: VHGoto Safe_1)"),
		TEXT("VHAttack [거리cm]       : 가장 가까운 경비원 앞으로 가서 1번 공격"),
		TEXT("VHUse 이름 초          : 오브젝트 옆으로 가서 상호작용 키를 N초 누름"),
		TEXT("VHFlashlight            : 손전등 켜기/끄기 (R, 지금은 꺼 둔 기능)"),
		TEXT("VHStamina 값            : 화면의 내 스테미나 설정 (서버 값과 5 넘게 다르면 서버 값으로 돌아옴)"),
		TEXT("VHMapLight lux          : 맵 달빛 밝기 바로 바꾸기 (원래 0.12, 저장 안 됨)"),
		TEXT("VHFlashPower 칸델라     : 손전등 밝기 바로 바꾸기 (기본 60, 저장 안 됨)"),
		TEXT("VHAura 밝기 [반경cm]     : 손전등 주변 빛 바로 바꾸기 (기본 3, 저장 안 됨)"),
		TEXT("VHOtherLights 1|0       : 다른 의적 손전등 원뿔·주변 빛 보이기/숨기기 (프레임 비교용)"),
		TEXT("VHFps [초]              : 평균 FPS·가장 느린 프레임 측정"),
	};
	// 화면 메시지는 아래에서 위로 쌓이므로 거꾸로 출력
	for (int32 i = UE_ARRAY_COUNT(Lines) - 1; i >= 0; --i)
	{
		DebugPrint(Lines[i]);
	}
	DebugPrint(TEXT("── 개발용 명령 (콘솔 ~ 키) ──"));
}

void AVHGamePlayerController::VHFastInteract(float Scale)
{
	if (UVHInteractionSubsystem* Sub = GetInteractionSubsystem())
	{
		Sub->DebugDurationScale = FMath::Clamp(Scale, 0.01f, 10.f);
		DebugPrint(FString::Printf(TEXT("상호작용 시간 배율 = %.2f"), Sub->DebugDurationScale));
	}
}

void AVHGamePlayerController::OnAttackStarted()
{
	// 길게 누르는 상호작용 중이면 좌클릭은 취소만 한다(공격 입력 = 취소 조건 [기획]). 아니면 공격
	UVHInteractionComponent* I = GetInteraction();
	if (I && (I->IsInteracting() || I->IsApproaching()))
	{
		I->NotifyActionInput();
		return;
	}
	// 서버 판정(v3.0): 누르기 시작만 알린다. 누르는 동안 서버가 무기 간격마다 공격하고 결과를 SC_FIRE·SC_DAMAGE 로 보낸다.
	// 화면의 공격·피격 표시는 그 패킷을 받아서 그린다(UVHServerGameSubsystem) → 서버 연사 속도와 화면이 같다
	UVHServerGameSubsystem* Server = GetWorld()->GetSubsystem<UVHServerGameSubsystem>();
	if (Server && Server->IsActive() && GetPawn())
	{
		Server->SendAttackHeld(true);
	}
}

void AVHGamePlayerController::OnAttackCompleted()
{
	// 손을 뗐음을 알린다 (AttackStop)
	if (UVHServerGameSubsystem* Server = GetWorld()->GetSubsystem<UVHServerGameSubsystem>(); Server && Server->IsActive())
	{
		Server->SendAttackHeld(false);
	}
}

void AVHGamePlayerController::VHHoldInteract(float Seconds)
{
	OnInteractStarted();
	GetWorldTimerManager().ClearTimer(HoldTimer);
	if (Seconds > 0.f)
	{
		GetWorldTimerManager().SetTimer(HoldTimer, this, &AVHGamePlayerController::VHInteractUp, Seconds, false);
	}
	DebugPrint(FString::Printf(TEXT("상호작용 키 누름 (%s)"), Seconds > 0.f ? *FString::Printf(TEXT("%.1f초 뒤 뗌"), Seconds) : TEXT("VHInteractUp 으로 뗌")));
}

void AVHGamePlayerController::VHUse(const FString& Name, float Seconds)
{
	APawn* MyPawn = GetPawn();
	AVHInteractableBase* T = FindInteractableByName(Name);
	UVHInteractionComponent* I = GetInteraction();
	if (!MyPawn || !T || !I)
	{
		DebugPrint(FString::Printf(TEXT("'%s' 오브젝트를 찾지 못했습니다"), *Name));
		return;
	}
	if (Cast<AVHReturnPoint>(T))
	{
		// 반납지점: 가운데에서 50cm (서버 범위 100cm 안)
		FVector Loc = T->GetActorLocation() + FVector(50.f, 0.f, 0.f);
		Loc.Z = MyPawn->GetActorLocation().Z;
		MyPawn->TeleportTo(Loc, FRotator(0.f, 180.f, 0.f));
	}
	else
	{
		PlaceInFront(MyPawn, T);
	}
	I->DebugPressOn(T);
	GetWorldTimerManager().ClearTimer(HoldTimer);
	GetWorldTimerManager().SetTimer(HoldTimer, this, &AVHGamePlayerController::VHInteractUp, FMath::Max(0.05f, Seconds), false);
	DebugPrint(FString::Printf(TEXT("%s 사용 (%.1f초)"), *T->GetActorNameOrLabel(), Seconds));
}

void AVHGamePlayerController::VHInteractUp()
{
	GetWorldTimerManager().ClearTimer(HoldTimer);
	OnInteractCompleted();
	DebugPrint(TEXT("상호작용 키 뗌"));
}

AVHInteractableBase* AVHGamePlayerController::FindInteractableByName(const FString& Name) const
{
	if (UVHInteractionSubsystem* Sub = GetInteractionSubsystem())
	{
		for (AVHInteractableBase* T : Sub->GetInteractables())
		{
			if (T->GetActorNameOrLabel().Contains(Name) || T->GetName().Contains(Name))
			{
				return T;
			}
		}
	}
	return nullptr;
}

void AVHGamePlayerController::PlaceInFront(APawn* Who, const AVHInteractableBase* Target) const
{
	// 오브젝트 앞면(+X) 앞에 서서 오브젝트를 바라본다
	const FBox Box = Target->GetInteractBounds();
	const FVector Front = Target->GetActorForwardVector().GetSafeNormal2D();
	const float Extent = FMath::Abs(FVector::DotProduct(Box.GetExtent(), Front.GetAbs()));
	FVector Loc = Box.GetCenter() + Front * (Extent + 70.f);
	Loc.Z = Who->GetActorLocation().Z;
	Who->TeleportTo(Loc, (-Front).Rotation());
}

void AVHGamePlayerController::VHGoto(const FString& Name)
{
	APawn* MyPawn = GetPawn();
	AVHInteractableBase* T = FindInteractableByName(Name);
	if (MyPawn && !T)
	{
		// 의적: 그 옆(시선 방향 앞 120cm)으로 가서 그쪽을 본다
		if (UVHInteractionSubsystem* Sub = GetInteractionSubsystem())
		{
			for (UVHThiefStateComponent* Thief : Sub->GetThieves())
			{
				AActor* Other = Thief->GetOwner();
				if (Other && Other != MyPawn && (Thief->DisplayName.Contains(Name, ESearchCase::IgnoreCase) || Other->GetActorNameOrLabel().Contains(Name, ESearchCase::IgnoreCase)))
				{
					const FVector Front = Other->GetActorForwardVector().GetSafeNormal2D();
					const FVector Loc = Other->GetActorLocation() + Front * 120.f;
					const float Yaw = (-Front).Rotation().Yaw;
					MyPawn->SetActorLocationAndRotation(Loc, FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
					if (UVHAimComponent* Aim = MyPawn->FindComponentByClass<UVHAimComponent>())
					{
						Aim->SnapLookYaw(Yaw);
					}
					DebugPrint(FString::Printf(TEXT("%s 앞으로 이동 (좌클릭으로 공격 가능 거리)"), *UVHInteractionSubsystem::GetThiefLabel(Thief)));
					return;
				}
			}
		}
		// 시작 위치 (PlayerStart)
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			if (It->GetActorNameOrLabel().Contains(Name, ESearchCase::IgnoreCase) || It->GetName().Contains(Name, ESearchCase::IgnoreCase))
			{
				MyPawn->SetActorLocationAndRotation(It->GetActorLocation(), FRotator(0.f, It->GetActorRotation().Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
				DebugPrint(FString::Printf(TEXT("%s 로 이동"), *It->GetActorNameOrLabel()));
				return;
			}
		}
		// 그 밖에는 이름(라벨)이 맞는 아무 액터 위로 (예: 바닥 Floor_Hall)
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (*It != MyPawn && It->GetActorNameOrLabel().Contains(Name, ESearchCase::IgnoreCase))
			{
				const float Half = MyPawn->GetSimpleCollisionHalfHeight();
				const FVector Bounds = It->GetComponentsBoundingBox(true).GetCenter();
				MyPawn->SetActorLocation(FVector(Bounds.X, Bounds.Y, It->GetActorLocation().Z + Half + 5.f), false, nullptr, ETeleportType::TeleportPhysics);
				DebugPrint(FString::Printf(TEXT("%s 위로 이동 (%.0f, %.0f)"), *It->GetActorNameOrLabel(), Bounds.X, Bounds.Y));
				return;
			}
		}
	}
	if (!MyPawn || !T)
	{
		DebugPrint(FString::Printf(TEXT("'%s' 오브젝트를 찾지 못했습니다"), *Name));
		return;
	}
	PlaceInFront(MyPawn, T);
	if (UVHAimComponent* Aim = MyPawn->FindComponentByClass<UVHAimComponent>())
	{
		Aim->SnapLookYaw(MyPawn->GetActorRotation().Yaw);
	}
	DebugPrint(FString::Printf(TEXT("%s 앞으로 이동"), *T->GetActorNameOrLabel()));
}

void AVHGamePlayerController::VHAttack(float Distance)
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}
	// 가장 가까운 경비원 발 위치 (서버 엔티티)
	const FVector Me = MyPawn->GetActorLocation();
	bool bFound = false;
	FVector Guard = FVector::ZeroVector;
	FString GuardName;
	auto Consider = [&](const FVector& Foot, const FString& Name)
	{
		if (!bFound || FVector::DistSquared2D(Foot, Me) < FVector::DistSquared2D(Guard, Me))
		{
			bFound = true;
			Guard = Foot;
			GuardName = Name;
		}
	};
	UVHServerGameSubsystem* Server = GetWorld()->GetSubsystem<UVHServerGameSubsystem>();
	if (Server && Server->IsActive())
	{
		for (const TPair<uint16, UVHServerGameSubsystem::FEntity>& Pair : Server->GetEntities())
		{
			if (Pair.Value.Kind == RobProto::EntityKind::Guard)
			{
				Consider(Pair.Value.Pos, FString::Printf(TEXT("Guard %u"), Pair.Key));
			}
		}
	}
	if (!bFound)
	{
		DebugPrint(TEXT("경비원을 찾지 못했습니다"));
		return;
	}
	FVector Dir = (Me - Guard).GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector::ForwardVector;
	}
	const FVector Foot = Guard + Dir * FMath::Clamp(Distance, 30.f, 110.f);
	const float Yaw = (-Dir).Rotation().Yaw;
	MyPawn->SetActorLocationAndRotation(Foot + FVector(0.f, 0.f, MyPawn->GetSimpleCollisionHalfHeight() + 2.f), FRotator(0.f, Yaw, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);
	if (UVHAimComponent* Aim = MyPawn->FindComponentByClass<UVHAimComponent>())
	{
		Aim->SnapLookYaw(Yaw);
	}
	DebugPrint(FString::Printf(TEXT("%s 앞 %.0fcm에서 공격 (yaw %.0f)"), *GuardName, FVector::Dist2D(Foot, Guard), Yaw));
	// 1번만 공격 (누르고 바로 뗌)
	OnAttackStarted();
	OnAttackCompleted();
}

void AVHGamePlayerController::VHMapLight(float Lux)
{
	const int32 Count = AVHLevelViewSettings::SetMoonlight(GetWorld(), Lux);
	DebugPrint(Count > 0
		? FString::Printf(TEXT("맵 달빛 = %.2f lux (원래 0.12). 정했으면 DA_VHGameplayConfig → Map Moonlight Intensity"), Lux)
		: FString(TEXT("이 레벨에 달빛(Directional Light)이 없습니다")));
}

void AVHGamePlayerController::VHFlashPower(float Candela)
{
	int32 Count = 0;
	for (TObjectIterator<UVHFlashlightComponent> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld())
		{
			It->SetIntensity(FMath::Max(0.f, Candela));
			++Count;
		}
	}
	for (TActorIterator<AVHNetProxy> It(GetWorld()); It; ++It)
	{
		if (It->GetBeam())
		{
			It->GetBeam()->SetIntensity(FMath::Max(0.f, Candela));	// 다른 의적의 원뿔도 같은 밝기
			++Count;
		}
	}
	DebugPrint(FString::Printf(TEXT("손전등 밝기 = %.0f cd (%d개). 정했으면 DA_VHInteractionConfig → Flashlight Intensity"), Candela, Count));
}

void AVHGamePlayerController::VHOtherLights(int32 bShow)
{
	AVHNetProxy::bDebugHideOtherLights = bShow == 0;
	int32 Count = 0;
	for (TActorIterator<AVHNetProxy> It(GetWorld()); It; ++It)
	{
		It->RefreshLights();
		++Count;
	}
	DebugPrint(FString::Printf(TEXT("다른 의적 손전등 원뿔·주변 빛 = %s (표시 액터 %d개)"), bShow ? TEXT("보임(서버 상태대로)") : TEXT("강제로 숨김"), Count));
}

void AVHGamePlayerController::VHFps(float Seconds)
{
	// 코어 티커로 매 프레임 시간을 모아 평균·가장 느린 프레임을 잰다 (게임 스레드 기준, 에디터 PIE면 에디터 비용 포함)
	struct FStat { double Start = 0.0; double Sum = 0.0; double Worst = 0.0; int32 Frames = 0; };
	TSharedRef<FStat> S = MakeShared<FStat>();
	S->Start = FPlatformTime::Seconds();
	const double Duration = FMath::Clamp(Seconds, 1.f, 60.f);
	TWeakObjectPtr<AVHGamePlayerController> WeakThis(this);
	DebugPrint(FString::Printf(TEXT("프레임 측정 시작 (%.0f초)"), Duration));
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S, Duration, WeakThis](float Dt)
	{
		S->Sum += Dt;
		S->Worst = FMath::Max<double>(S->Worst, Dt);
		++S->Frames;
		if (FPlatformTime::Seconds() - S->Start < Duration)
		{
			return true;
		}
		const double AvgMs = S->Frames > 0 ? S->Sum / S->Frames * 1000.0 : 0.0;
		const FString Line = FString::Printf(TEXT("[Fps] %d프레임, 평균 %.1f FPS (%.2f ms), 가장 느린 프레임 %.1f ms"),
			S->Frames, AvgMs > 0.0 ? 1000.0 / AvgMs : 0.0, AvgMs, S->Worst * 1000.0);
		if (WeakThis.IsValid())
		{
			WeakThis->DebugPrint(Line);
		}
		return false;	// 한 번만
	}));
}

void AVHGamePlayerController::VHAura(float Candela, float Radius)
{
	const UVHInteractionConfig& C = UVHSettings::Get().GetInteractionConfig();
	const float H = C.FlashlightAuraHeight;
	TArray<UPointLightComponent*> Lights;
	for (TObjectIterator<UVHFlashlightComponent> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld() && It->GetAura())
		{
			Lights.Add(It->GetAura());
		}
	}
	for (TActorIterator<AVHNetProxy> It(GetWorld()); It; ++It)
	{
		if (It->GetAura())
		{
			Lights.Add(It->GetAura());
		}
	}
	for (UPointLightComponent* L : Lights)
	{
		L->SetIntensity(FMath::Max(0.f, Candela));
		if (Radius > 0.f)
		{
			L->SetAttenuationRadius(FMath::Sqrt(Radius * Radius + H * H) * 1.2f);	// ConfigureAura 와 같은 계산
		}
	}
	DebugPrint(FString::Printf(TEXT("손전등 주변 빛 밝기 = %.2f, 반경 %s (%d개). 정했으면 DA_VHInteractionConfig → Flashlight Aura"),
		Candela, Radius > 0.f ? *FString::Printf(TEXT("%.0fcm"), Radius) : TEXT("그대로"), Lights.Num()));
}

void AVHGamePlayerController::VHStamina(float Value)
{
	if (UVHActionComponent* A = GetAction())
	{
		A->DebugSetStamina(Value);
		DebugPrint(FString::Printf(TEXT("스테미나 = %.0f%s"), A->GetStamina(), A->IsExhausted() ? TEXT(" (탈진)") : TEXT("")));
	}
}

void AVHGamePlayerController::VHFake(const FString& Name, float Seconds)
{
	UVHServerGameSubsystem* Server = GetWorld()->GetSubsystem<UVHServerGameSubsystem>();
	if (!Server || !Server->IsActive())
	{
		DebugPrint(TEXT("VHFake: 서버(실서버·가짜 서버)와 게임 중일 때만 쓸 수 있습니다"));
		return;
	}
	Server->DebugFake(Name, Seconds);
	DebugPrint(FString::Printf(TEXT("흉내: %s %.1f초 (내 화면만, 서버는 모른다)"), *Name, Seconds));
}

#undef LOCTEXT_NAMESPACE
