#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "UI/VHScreenWidget.h"
#include "Network/VHNetTypes.h"
#include "VHMenuPlayerController.generated.h"

/**
 * 메뉴 레벨(L_MainMenu)의 플레이어 컨트롤러. 화면 전환을 담당한다.
 * - UI 전용 입력 모드 + 마우스 커서
 * - 서브시스템의 단계 변화(로그인 성공 → 로비, 방 입장 → 대기실 …)를 듣고 화면을 바꾼다.
 * - 화면 위젯 클래스(WBP_*)는 BP_VHMenuPlayerController에서 지정한다.
 */
UCLASS()
class AVHMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AVHMenuPlayerController();

	UFUNCTION(BlueprintCallable, Category = "VaultHeist|UI")
	void ShowScreen(EVHScreen Screen);

	/** 현재 연결 단계에 맞는 화면 (결과 대기 중이면 결과창) */
	UFUNCTION(BlueprintCallable, Category = "VaultHeist|UI")
	void ShowScreenForCurrentState();

	UFUNCTION(BlueprintPure, Category = "VaultHeist|UI")
	EVHScreen GetCurrentScreen() const { return CurrentScreen; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "Screens") TSubclassOf<UVHScreenWidget> MainMenuScreenClass;
	UPROPERTY(EditDefaultsOnly, Category = "Screens") TSubclassOf<UVHScreenWidget> LoginScreenClass;
	UPROPERTY(EditDefaultsOnly, Category = "Screens") TSubclassOf<UVHScreenWidget> LobbyScreenClass;
	UPROPERTY(EditDefaultsOnly, Category = "Screens") TSubclassOf<UVHScreenWidget> RoomScreenClass;
	UPROPERTY(EditDefaultsOnly, Category = "Screens") TSubclassOf<UVHScreenWidget> ResultScreenClass;

private:
	UFUNCTION() void HandlePhaseChanged(EVHNetPhase NewPhase, EVHNetPhase OldPhase);
	UFUNCTION() void HandleGameEnd();

	TSubclassOf<UVHScreenWidget> ClassFor(EVHScreen Screen) const;

	UPROPERTY() TObjectPtr<UVHScreenWidget> CurrentWidget;
	EVHScreen CurrentScreen = EVHScreen::None;
};
