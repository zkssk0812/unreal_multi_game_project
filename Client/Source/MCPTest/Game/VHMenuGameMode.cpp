#include "Game/VHMenuGameMode.h"
#include "Game/VHMenuPlayerController.h"

AVHMenuGameMode::AVHMenuGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AVHMenuPlayerController::StaticClass();	// BP에서 BP_VHMenuPlayerController로 바꾼다
}
