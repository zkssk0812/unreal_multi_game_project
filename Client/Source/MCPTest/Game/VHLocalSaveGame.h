#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "VHLocalSaveGame.generated.h"

/** 이 PC에만 저장되는 값 (마지막 로그인 이름 등). Saved/SaveGames/VHLocal.sav */
UCLASS()
class UVHLocalSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr const TCHAR* SlotName = TEXT("VHLocal");

	UPROPERTY()
	FString LastLoginName;

	/** 메인화면 [실서버로 전환]에 마지막으로 넣은 주소 (host:port) */
	UPROPERTY()
	FString LastServerAddress;
};
