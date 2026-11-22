#include "UI/VHMainMenuWidget.h"

#include "Network/VHNetSubsystem.h"
#include "Network/VHSettings.h"
#include "Gameplay/VHGameplayConfig.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Kismet/KismetSystemLibrary.h"

void UVHMainMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	PrimaryStartButton->OnClicked.AddDynamic(this, &UVHMainMenuWidget::HandleStart);
	SettingsButton->OnClicked.AddDynamic(this, &UVHMainMenuWidget::HandleSettings);
	QuitButton->OnClicked.AddDynamic(this, &UVHMainMenuWidget::HandleQuit);
	if (DevSampleResultButton)
	{
#if UE_BUILD_SHIPPING
		DevSampleResultButton->SetVisibility(ESlateVisibility::Collapsed);
#else
		DevSampleResultButton->OnClicked.AddDynamic(this, &UVHMainMenuWidget::HandleDevSampleResult);
#endif
	}

#if UE_BUILD_SHIPPING
	if (DevServerRow)
	{
		DevServerRow->SetVisibility(ESlateVisibility::Collapsed);
	}
#else
	if (DevUseServerButton)
	{
		DevUseServerButton->OnClicked.AddDynamic(this, &UVHMainMenuWidget::HandleDevUseServer);
	}
	if (DevUseMockButton)
	{
		DevUseMockButton->OnClicked.AddDynamic(this, &UVHMainMenuWidget::HandleDevUseMock);
	}
	if (DevServerAddressBox)
	{
		DevServerAddressBox->OnTextCommitted.AddDynamic(this, &UVHMainMenuWidget::HandleDevAddressCommitted);
		// 기본값: 이 PC에 저장된 마지막 주소 → 없으면 지금 설정(기본 127.0.0.1:9000)
		FString Address;
		if (const UVHNetSubsystem* Net = GetNet())
		{
			Address = Net->LoadLastServerAddress();
			if (Address.IsEmpty())
			{
				Address = FString::Printf(TEXT("%s:%d"), *Net->GetServerHost(), Net->GetServerPort());
			}
		}
		DevServerAddressBox->SetText(FText::FromString(Address.IsEmpty() ? FString(TEXT("127.0.0.1:9000")) : Address));
	}
#endif

	// 콘솔(vh.UseServer / vh.UseMock)로 바꿔도 이 화면 문구가 바로 바뀌게
	ServerModeChangedHandle = UVHNetSubsystem::OnServerModeChanged().AddUObject(this, &UVHMainMenuWidget::RefreshServerInfo);
}

void UVHMainMenuWidget::NativeDestruct()
{
	UVHNetSubsystem::OnServerModeChanged().Remove(ServerModeChangedHandle);
	Super::NativeDestruct();
}

void UVHMainMenuWidget::OnScreenShown()
{
	// 한 줄 소개 (PvP) [PvP 규칙 v1 2장]: 시간은 설정값으로. 위젯 에셋에 남은 옛 PvE 문구를 덮어쓴다
	if (TaglineText)
	{
		const int32 Limit = FMath::Max(0, FMath::RoundToInt(UVHSettings::Get().GetGameplayConfig().TimeLimitSeconds));
		const FString Duration = Limit % 60 == 0
			? FString::Printf(TEXT("%d분"), Limit / 60)
			: FString::Printf(TEXT("%d분 %d초"), Limit / 60, Limit % 60);
		TaglineText->SetText(FText::FromString(FString::Printf(TEXT("%s 동안 금고를 털어 가장 많이 반납한 의적이 승리!"), *Duration)));
	}
	RefreshServerInfo();
	PrimaryStartButton->SetKeyboardFocus();
}

void UVHMainMenuWidget::RefreshServerInfo()
{
	const UVHNetSubsystem* Net = GetNet();
	const FString Version = FString::Printf(TEXT("클라 프로토콜 %s"), *UVHNetSubsystem::GetProtocolVersionText());
	if (InfoStatusText)
	{
		const FString Server = Net ? Net->GetTransportDescription() : FString();
		// 버전 글자를 따로 둔 위젯이 없으면 "접속 대상" 아래 줄에 함께 보여 준다
		SetStatus(InfoStatusText, FText::FromString(ClientVersionText
			? FString::Printf(TEXT("접속 대상: %s"), *Server)
			: FString::Printf(TEXT("접속 대상: %s\n%s"), *Server, *Version)));
	}
	if (ClientVersionText)
	{
		ClientVersionText->SetText(FText::FromString(Version));
	}
	if (DevUseMockButton && Net)
	{
		DevUseMockButton->SetIsEnabled(!Net->WillUseMock());
	}
}

void UVHMainMenuWidget::HandleStart()
{
	GoTo(EVHScreen::Login);
}

void UVHMainMenuWidget::HandleSettings()
{
	if (UVHNetSubsystem* Net = GetNet())
	{
		Net->ShowToast(FText::FromString(TEXT("설정 화면은 아직 준비 중입니다.")));
	}
}

void UVHMainMenuWidget::HandleQuit()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UVHMainMenuWidget::HandleDevSampleResult()
{
	if (UVHNetSubsystem* Net = GetNet())
	{
		Net->ShowSampleResult();
	}
}

void UVHMainMenuWidget::HandleDevUseServer()
{
	UVHNetSubsystem* Net = GetNet();
	if (!Net)
	{
		return;
	}
	const FString Address = DevServerAddressBox ? DevServerAddressBox->GetText().ToString().TrimStartAndEnd() : FString();
	FString Host;
	int32 Port = 0;
	if (!Address.IsEmpty() && !UVHNetSubsystem::ParseHostPort(Address, Host, Port))
	{
		Net->ShowToast(FText::FromString(TEXT("주소 형식이 틀렸습니다. 예: 127.0.0.1:9000 또는 192.168.0.10:9000")), true);
		return;
	}
	if (!Address.IsEmpty())
	{
		Net->SaveLastServerAddress(Address);
	}
	UVHNetSubsystem::UseServer(Address);	// 알림·문구 갱신은 OnServerModeChanged 로
}

void UVHMainMenuWidget::HandleDevUseMock()
{
	UVHNetSubsystem::UseMock();
}

void UVHMainMenuWidget::HandleDevAddressCommitted(const FText& Text, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		HandleDevUseServer();
	}
}
