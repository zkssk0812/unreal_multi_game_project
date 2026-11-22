#include "Game/VHMenuPlayerController.h"

#include "Network/VHNetSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogVHMenu, Log, All);

AVHMenuPlayerController::AVHMenuPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}

void AVHMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}

	SetShowMouseCursor(true);
	if (UVHNetSubsystem* Net = UVHNetSubsystem::Get(this))
	{
		Net->OnPhaseChanged.AddDynamic(this, &AVHMenuPlayerController::HandlePhaseChanged);
		Net->OnGameEnd.AddDynamic(this, &AVHMenuPlayerController::HandleGameEnd);
	}
	ShowScreenForCurrentState();
}

void AVHMenuPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UVHNetSubsystem* Net = UVHNetSubsystem::Get(this))
	{
		Net->OnPhaseChanged.RemoveAll(this);
		Net->OnGameEnd.RemoveAll(this);
	}
	Super::EndPlay(EndPlayReason);
}

TSubclassOf<UVHScreenWidget> AVHMenuPlayerController::ClassFor(EVHScreen Screen) const
{
	switch (Screen)
	{
	case EVHScreen::MainMenu:	return MainMenuScreenClass;
	case EVHScreen::Login:		return LoginScreenClass;
	case EVHScreen::Lobby:		return LobbyScreenClass;
	case EVHScreen::Room:		return RoomScreenClass;
	case EVHScreen::Result:		return ResultScreenClass;
	default:					return nullptr;
	}
}

void AVHMenuPlayerController::ShowScreen(EVHScreen Screen)
{
	// 버튼 클릭·키 입력을 처리하는 도중에 그 버튼이 속한 화면을 지우면, 같은 입력이 새 화면의
	// 포커스 위젯으로 이어지거나(예: 로그아웃 Enter → 메인화면 [시작하기]까지 눌림) 지운 위젯을
	// 건드릴 수 있다. 그래서 실제 전환은 다음 틱에 한다. 같은 틱에 여러 번 부르면 마지막 요청이 남는다.
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Screen]()
	{
		if (Screen == CurrentScreen && CurrentWidget)
		{
			CurrentWidget->OnScreenShown();
			return;
		}

		const TSubclassOf<UVHScreenWidget> Class = ClassFor(Screen);
		if (!Class)
		{
			UE_LOG(LogVHMenu, Error, TEXT("화면 %d의 위젯 클래스가 지정되지 않았습니다 (BP_VHMenuPlayerController 확인)."), static_cast<int32>(Screen));
			return;
		}

		if (CurrentWidget)
		{
			CurrentWidget->RemoveFromParent();
			CurrentWidget = nullptr;
		}

		UE_LOG(LogVHMenu, Log, TEXT("화면 전환: %d → %d"), static_cast<int32>(CurrentScreen), static_cast<int32>(Screen));
		CurrentScreen = Screen;
		CurrentWidget = CreateWidget<UVHScreenWidget>(this, Class);
		if (!CurrentWidget)
		{
			return;
		}
		CurrentWidget->AddToViewport(0);
		CurrentWidget->OnScreenShown();

		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(CurrentWidget->TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
	}));
}

void AVHMenuPlayerController::ShowScreenForCurrentState()
{
	const UVHNetSubsystem* Net = UVHNetSubsystem::Get(this);
	if (Net && Net->HasPendingResult())
	{
		ShowScreen(EVHScreen::Result);
		return;
	}
	switch (Net ? Net->GetPhase() : EVHNetPhase::Disconnected)
	{
	case EVHNetPhase::Lobby:	ShowScreen(EVHScreen::Lobby); break;
	case EVHNetPhase::Room:		ShowScreen(EVHScreen::Room); break;
	case EVHNetPhase::Connecting:
	case EVHNetPhase::Anonymous:
	case EVHNetPhase::Authenticating:
		ShowScreen(EVHScreen::Login); break;
	default:					ShowScreen(EVHScreen::MainMenu); break;
	}
}

void AVHMenuPlayerController::HandlePhaseChanged(EVHNetPhase NewPhase, EVHNetPhase OldPhase)
{
	switch (NewPhase)
	{
	case EVHNetPhase::Lobby:
		ShowScreen(EVHScreen::Lobby);
		break;
	case EVHNetPhase::Room:
		ShowScreen(EVHScreen::Room);
		break;
	case EVHNetPhase::Disconnected:
		// 로그인 화면(로그인 실패)이나 메인화면에 있으면 그대로 두고, 그 밖의 화면이면 메인화면으로
		if (CurrentScreen != EVHScreen::Login && CurrentScreen != EVHScreen::MainMenu
			&& !(CurrentScreen == EVHScreen::Result && UVHNetSubsystem::Get(this) && UVHNetSubsystem::Get(this)->HasPendingResult()))
		{
			ShowScreen(EVHScreen::MainMenu);
		}
		break;
	default:
		break;
	}
}

void AVHMenuPlayerController::HandleGameEnd()
{
	ShowScreen(EVHScreen::Result);
}
