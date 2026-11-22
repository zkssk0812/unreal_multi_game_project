#pragma once

#include "CoreMinimal.h"
#include "UI/VHScreenWidget.h"
#include "VHResultWidget.generated.h"

class UButton;
class UTextBlock;
class UPanelWidget;

/**
 * 결과창 (서버 v3.0 SC_GAME_END 기준).
 * - 전체 결과("게임 종료"/중단), 종료 사유, 플레이 시간, 내 최고 기록
 * - 내 순위·승패를 크게 ("1위 — 승리!", 공동 1위도 승리)
 * - 플레이어 카드는 서버가 매긴 순위 순: 순위·이름·반납 금액·최고 기록(갱신 표시)·쓰러뜨림/쓰러짐·반납 보물
 * - [로비로](CS_RETURN) — 결과 뒤에는 대기실이 아니라 로비로 간다(방은 사라진다)
 * - result_screen_ms 카운트다운 (시간이 지나면 서버가 자동으로 로비로 돌려보낸다)
 */
UCLASS(Abstract)
class UVHResultWidget : public UVHScreenWidget
{
	GENERATED_BODY()

public:
	virtual void OnScreenShown() override;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> ResultTitleText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> MyOutcomeBigText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> ResultInfoText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UPanelWidget> PlayersBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> CountdownStatusText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ToLobbyButton;

private:
	UFUNCTION() void HandleToLobby();
	void Return();
	void Fill();

	bool bReturning = false;
	/** 결과창이 뜬 시각 (개발용 봇 옵션 -VHShotResult·-VHBotReturn 의 기준) */
	double ShownSeconds = 0.0;
	bool bDevShotTaken = false;
};
