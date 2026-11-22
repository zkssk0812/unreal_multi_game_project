#pragma once

#include "CoreMinimal.h"
#include "UI/VHScreenWidget.h"
#include "VHLoginWidget.generated.h"

class UButton;
class UTextBlock;
class UEditableTextBox;

/**
 * 로그인: 이름만 입력(비밀번호 없음).
 * 클라이언트 규칙: 영문자만 2~12자 → 입력할 때마다 검사해 안내 문구를 바꾸고, 위반이면 [확인] 비활성.
 * [확인] → 서버 연결 → CS_LOGIN → SC_LOGIN 결과(실패 코드별 한국어 안내).
 * 마지막 이름은 SaveGame에 저장해 다음 실행 때 미리 채운다.
 */
UCLASS(Abstract)
class UVHLoginWidget : public UVHScreenWidget
{
	GENERATED_BODY()

public:
	virtual void OnScreenShown() override;

protected:
	virtual void NativeOnInitialized() override;
	virtual void BindNetEvents(UVHNetSubsystem& Net) override;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEditableTextBox> NameTextBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> NameHintText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> PrimaryConfirmButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> BackButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> LoginStatusText;

private:
	UFUNCTION() void HandleTextChanged(const FText& Text);
	UFUNCTION() void HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	UFUNCTION() void HandleConfirm();
	UFUNCTION() void HandleBack();
	UFUNCTION() void HandleLoginResult(bool bSuccess, const FText& Message);

	bool Revalidate();
	void SetBusy(bool bInBusy);
	bool bBusy = false;
};
