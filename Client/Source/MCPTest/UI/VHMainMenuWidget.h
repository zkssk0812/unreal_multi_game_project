#pragma once

#include "CoreMinimal.h"
#include "UI/VHScreenWidget.h"
#include "VHMainMenuWidget.generated.h"

class UButton;
class UTextBlock;
class UEditableTextBox;
class UWidget;

/**
 * 메인화면: 게임 타이틀, [시작하기] → 로그인, [설정](준비 중), [종료].
 * 기획서상 프로그램 종료는 메인화면에서만 가능하다.
 */
UCLASS(Abstract)
class UVHMainMenuWidget : public UVHScreenWidget
{
	GENERATED_BODY()

public:
	virtual void OnScreenShown() override;

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> PrimaryStartButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SettingsButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> QuitButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> InfoStatusText;
	/** 개발용: 샘플 결과창 바로 띄우기 (Shipping 빌드에서는 숨김) */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> DevSampleResultButton;

	/** 개발용 서버 전환 줄 (Shipping 빌드에서는 숨김): [주소 입력칸] [실서버로 전환] [가짜 서버로] */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> DevServerRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UEditableTextBox> DevServerAddressBox;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> DevUseServerButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> DevUseMockButton;
	/** 클라 프로토콜 버전 표시 (서버 시작 로그 protocol 0x... 와 비교용) */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ClientVersionText;
	/** 타이틀 아래 한 줄 소개. PvP 규칙 문구를 제한 시간 설정값(TimeLimitSeconds)으로 코드에서 채운다 */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TaglineText;

	virtual void NativeDestruct() override;

private:
	UFUNCTION() void HandleStart();
	UFUNCTION() void HandleSettings();
	UFUNCTION() void HandleQuit();
	UFUNCTION() void HandleDevSampleResult();
	UFUNCTION() void HandleDevUseServer();
	UFUNCTION() void HandleDevUseMock();
	UFUNCTION() void HandleDevAddressCommitted(const FText& Text, ETextCommit::Type CommitType);

	/** "접속 대상" 문구·버튼 상태를 지금 서버 모드에 맞게 */
	void RefreshServerInfo();
	FDelegateHandle ServerModeChangedHandle;
};
