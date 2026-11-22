#include "UI/VHScreenWidget.h"

#include "Network/VHNetSubsystem.h"
#include "Network/VHSettings.h"
#include "Game/VHGameData.h"
#include "Game/VHMenuPlayerController.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Components/EditableTextBox.h"

void UVHScreenWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	if (bApplyCommonStyle)
	{
		ApplyCommonStyle();
	}
}

void UVHScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (UVHNetSubsystem* Net = GetNet())
	{
		BindNetEvents(*Net);
	}
}

void UVHScreenWidget::NativeDestruct()
{
	if (UVHNetSubsystem* Net = GetNet())
	{
		UnbindNetEvents(*Net);
	}
	Super::NativeDestruct();
}

void UVHScreenWidget::UnbindNetEvents(UVHNetSubsystem& Net)
{
	// 이 위젯이 연결한 모든 핸들러를 한 번에 해제
	Net.OnPhaseChanged.RemoveAll(this);
	Net.OnLoginResult.RemoveAll(this);
	Net.OnConnectionLost.RemoveAll(this);
	Net.OnRoomListUpdated.RemoveAll(this);
	Net.OnRoomRequestResult.RemoveAll(this);
	Net.OnStatusMessage.RemoveAll(this);
	Net.OnRoomStateChanged.RemoveAll(this);
	Net.OnRoomError.RemoveAll(this);
	Net.OnLeftRoom.RemoveAll(this);
	Net.OnGameEnd.RemoveAll(this);
}

void UVHScreenWidget::ApplyCommonStyle()
{
	if (!WidgetTree)
	{
		return;
	}
	const UVHUIStyle& S = Style();
	WidgetTree->ForEachWidget([&S](UWidget* W)
	{
		const FString Name = W->GetName();
		if (UTextBlock* Text = Cast<UTextBlock>(W))
		{
			// 버튼 안의 글자는 버튼 글씨로
			const bool bInButton = W->GetParent() && W->GetParent()->IsA<UButton>();
			EVHTextRole Role = bInButton ? EVHTextRole::Button : EVHTextRole::Body;
			if (Name.Contains(TEXT("Title"))) Role = EVHTextRole::Title;
			else if (Name.Contains(TEXT("Heading"))) Role = EVHTextRole::Heading;
			else if (Name.Contains(TEXT("Big"))) Role = EVHTextRole::Big;
			else if (Name.Contains(TEXT("Hint")) || Name.Contains(TEXT("Status")) || Name.Contains(TEXT("Small"))) Role = EVHTextRole::Small;
			S.ApplyText(Text, Role);
		}
		else if (UButton* Button = Cast<UButton>(W))
		{
			S.ApplyButton(Button, Name.Contains(TEXT("Primary")) ? EVHButtonKind::Primary : EVHButtonKind::Normal);
		}
		else if (UEditableTextBox* Box = Cast<UEditableTextBox>(W))
		{
			S.ApplyTextBox(Box);
		}
		else if (UBorder* Border = Cast<UBorder>(W))
		{
			if (Name.Equals(TEXT("Background")))
			{
				S.ApplyBackground(Border);
			}
			else if (Name.Contains(TEXT("Panel")))
			{
				S.ApplyPanel(Border, Name.Contains(TEXT("Light")));
			}
		}
	});
}

UVHNetSubsystem* UVHScreenWidget::GetNet() const
{
	return IsDesignTime() ? nullptr : UVHNetSubsystem::Get(this);
}

AVHMenuPlayerController* UVHScreenWidget::GetMenuPC() const
{
	return Cast<AVHMenuPlayerController>(GetOwningPlayer());
}

const UVHUIStyle& UVHScreenWidget::Style() const
{
	return UVHSettings::Get().GetStyle();
}

const UVHGameData& UVHScreenWidget::GameData() const
{
	return UVHSettings::Get().GetGameData();
}

void UVHScreenWidget::SetStatus(UTextBlock* Target, const FText& Message, bool bIsError, bool bIsSuccess) const
{
	if (!Target)
	{
		return;
	}
	const UVHUIStyle& S = Style();
	Target->SetText(Message);
	Target->SetColorAndOpacity(FSlateColor(bIsError ? S.Error : (bIsSuccess ? S.Success : S.SubText)));
}

void UVHScreenWidget::GoTo(EVHScreen Screen) const
{
	if (AVHMenuPlayerController* PC = GetMenuPC())
	{
		PC->ShowScreen(Screen);
	}
}

void UVHScreenWidget::CollapseWithLabel(UWidget* Widget)
{
	if (!Widget)
	{
		return;
	}
	Widget->SetVisibility(ESlateVisibility::Collapsed);
	UPanelWidget* Parent = Widget->GetParent();
	const int32 Index = Parent ? Parent->GetChildIndex(Widget) : INDEX_NONE;
	if (Index > 0)
	{
		if (UTextBlock* Label = Cast<UTextBlock>(Parent->GetChildAt(Index - 1)))
		{
			Label->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}
