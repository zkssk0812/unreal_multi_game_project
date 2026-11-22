#include "UI/VHLoginWidget.h"

#include "Network/VHNetSubsystem.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"

void UVHLoginWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	NameTextBox->OnTextChanged.AddDynamic(this, &UVHLoginWidget::HandleTextChanged);
	NameTextBox->OnTextCommitted.AddDynamic(this, &UVHLoginWidget::HandleTextCommitted);
	PrimaryConfirmButton->OnClicked.AddDynamic(this, &UVHLoginWidget::HandleConfirm);
	BackButton->OnClicked.AddDynamic(this, &UVHLoginWidget::HandleBack);
	NameTextBox->SetHintText(FText::FromString(TEXT("영문 이름 (2~12자)")));
}

void UVHLoginWidget::BindNetEvents(UVHNetSubsystem& Net)
{
	Net.OnLoginResult.AddDynamic(this, &UVHLoginWidget::HandleLoginResult);
}

void UVHLoginWidget::OnScreenShown()
{
	const UVHNetSubsystem* Net = GetNet();
	if (Net && NameTextBox->GetText().IsEmpty())
	{
		NameTextBox->SetText(FText::FromString(Net->LoadLastLoginName()));
	}
	const bool bConnecting = Net && Net->GetPhase() != EVHNetPhase::Disconnected;
	SetBusy(bConnecting);
	SetStatus(LoginStatusText, bConnecting ? FText::FromString(TEXT("서버에 연결하는 중...")) : FText::GetEmpty());
	Revalidate();
	NameTextBox->SetKeyboardFocus();
}

bool UVHLoginWidget::Revalidate()
{
	const FString Name = NameTextBox->GetText().ToString();
	FText Reason;
	const bool bValid = UVHNetSubsystem::ValidateLoginName(Name, Reason);
	if (Name.IsEmpty())
	{
		SetStatus(NameHintText, FText::FromString(TEXT("영문자(A~Z, a~z)만, 2~12자. 공백·숫자·특수문자는 쓸 수 없습니다.")));
	}
	else
	{
		SetStatus(NameHintText, Reason, !bValid, bValid);
	}
	PrimaryConfirmButton->SetIsEnabled(bValid && !bBusy);
	return bValid;
}

void UVHLoginWidget::SetBusy(bool bInBusy)
{
	bBusy = bInBusy;
	NameTextBox->SetIsReadOnly(bBusy);
	Revalidate();
}

void UVHLoginWidget::HandleTextChanged(const FText& Text)
{
	Revalidate();
	if (!bBusy)
	{
		SetStatus(LoginStatusText, FText::GetEmpty());
	}
}

void UVHLoginWidget::HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		HandleConfirm();
	}
}

void UVHLoginWidget::HandleConfirm()
{
	if (bBusy || !Revalidate())
	{
		return;
	}
	UVHNetSubsystem* Net = GetNet();
	if (!Net)
	{
		return;
	}
	SetBusy(true);
	SetStatus(LoginStatusText, FText::FromString(FString::Printf(TEXT("서버에 연결하는 중... (%s)"), *Net->GetTransportDescription())));
	Net->ConnectAndLogin(NameTextBox->GetText().ToString());
}

void UVHLoginWidget::HandleBack()
{
	if (UVHNetSubsystem* Net = GetNet())
	{
		Net->Disconnect();	// 연결 중이었다면 취소
	}
	GoTo(EVHScreen::MainMenu);
}

void UVHLoginWidget::HandleLoginResult(bool bSuccess, const FText& Message)
{
	SetBusy(false);
	SetStatus(LoginStatusText, Message, !bSuccess, bSuccess);
	if (!bSuccess)
	{
		NameTextBox->SetKeyboardFocus();
	}
	// 성공하면 단계가 Lobby로 바뀌고 컨트롤러가 로비 화면으로 바꾼다.
}
