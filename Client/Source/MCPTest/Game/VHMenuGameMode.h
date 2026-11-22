#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "VHMenuGameMode.generated.h"

/** 메뉴 레벨 전용 게임 모드: 폰 없이 메뉴 플레이어 컨트롤러만 쓴다 */
UCLASS()
class AVHMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AVHMenuGameMode();
};
