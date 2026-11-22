#include "UI/VHLobbyWidget.h"

#include "Network/VHNetSubsystem.h"
#include "Network/VHPacket.h"
#include "Game/VHGameData.h"
#include "UI/VHCodeWidgets.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/ComboBoxString.h"
#include "TimerManager.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/World.h"

void UVHLobbyWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	RefreshButton->OnClicked.AddDynamic(this, &UVHLobbyWidget::HandleRefresh);
	CreateRoomButton->OnClicked.AddDynamic(this, &UVHLobbyWidget::HandleOpenCreate);
	PrimaryQuickFindButton->OnClicked.AddDynamic(this, &UVHLobbyWidget::HandleQuickFind);
	PrimaryJoinButton->OnClicked.AddDynamic(this, &UVHLobbyWidget::HandleJoin);
	BackButton->OnClicked.AddDynamic(this, &UVHLobbyWidget::HandleBack);
	SearchTextBox->OnTextChanged.AddDynamic(this, &UVHLobbyWidget::HandleSearchChanged);
	RoomNameTextBox->OnTextChanged.AddDynamic(this, &UVHLobbyWidget::HandleRoomNameChanged);
	PrimaryCreateConfirmButton->OnClicked.AddDynamic(this, &UVHLobbyWidget::HandleCreateConfirm);
	CreateCancelButton->OnClicked.AddDynamic(this, &UVHLobbyWidget::HandleCreateCancel);
	SearchTextBox->SetHintText(FText::FromString(TEXT("방 이름 검색")));

	// 콤보박스 항목: 맵(데이터 에셋) / 난이도
	MapComboBox->ClearOptions();
	for (const FVHMapDef& Map : GameData().Maps)
	{
		MapComboBox->AddOption(Map.DisplayName.ToString());
	}
	DifficultyComboBox->ClearOptions();
	for (EVHDifficulty D : { EVHDifficulty::Easy, EVHDifficulty::Normal, EVHDifficulty::Hard })
	{
		DifficultyComboBox->AddOption(UVHNetText::DifficultyText(D).ToString());
	}
	CreateRoomPanel->SetVisibility(ESlateVisibility::Collapsed);
	// 프로토콜 v2.1: 방 만들기는 이름만 보낸다(맵·난이도 선택 없음) → 선택 칸 숨김
	CollapseWithLabel(MapComboBox);
	CollapseWithLabel(DifficultyComboBox);
}

void UVHLobbyWidget::NativeDestruct()
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshCooldownTimer);
	}
	Super::NativeDestruct();
}

void UVHLobbyWidget::BindNetEvents(UVHNetSubsystem& Net)
{
	Net.OnRoomListUpdated.AddDynamic(this, &UVHLobbyWidget::HandleRoomListUpdated);
	Net.OnRoomRequestResult.AddDynamic(this, &UVHLobbyWidget::HandleRoomRequestResult);
	Net.OnStatusMessage.AddDynamic(this, &UVHLobbyWidget::HandleStatusMessage);
	Net.OnLeftRoom.AddDynamic(this, &UVHLobbyWidget::HandleLeftRoom);
}

void UVHLobbyWidget::OnScreenShown()
{
	UpdateStats();
	RebuildList();
	SetStatus(LobbyStatusText, FText::GetEmpty());
	if (UVHNetSubsystem* Net = GetNet())
	{
		if (Net->RequestRoomList(0))
		{
			SetStatus(LobbyStatusText, FText::FromString(TEXT("방 목록을 불러오는 중...")));
		}
	}
}

void UVHLobbyWidget::UpdateStats()
{
	const UVHNetSubsystem* Net = GetNet();
	if (!Net)
	{
		return;
	}
	UserNameText->SetText(FText::FromString(FString::Printf(TEXT("%s 님"), *Net->GetMyName())));
	// 프로토콜 v2.1에는 전적이 없다 → 연결 정보만 표시
	StatsText->SetText(FText::FromString(FString::Printf(TEXT("접속: %s\n\n내 전적은 아직 서버에서 제공하지 않습니다."),
		*Net->GetTransportDescription())));
}

void UVHLobbyWidget::RebuildList()
{
	const UVHNetSubsystem* Net = GetNet();
	RoomListBox->ClearChildren();
	RoomListBox->AddChild(UVHRoomRowWidget::MakeHeader(this));

	const FString Filter = SearchTextBox->GetText().ToString().TrimStartAndEnd();
	int32 Shown = 0;
	bool bSelectedStillVisible = false;
	if (Net)
	{
		for (const FVHRoomSummary& Room : Net->GetRoomList())
		{
			if (!Filter.IsEmpty() && !Room.Name.Contains(Filter, ESearchCase::IgnoreCase))
			{
				continue;
			}
			UVHRoomRowWidget* Row = CreateWidget<UVHRoomRowWidget>(this, UVHRoomRowWidget::StaticClass());
			Row->SetRoom(Room, Room.RoomId == SelectedRoomId);
			Row->OnSelected = [this](int64 RoomId)
			{
				SelectedRoomId = RoomId;
				RebuildList();
			};
			if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(RoomListBox->AddChild(Row)))
			{
				RowSlot->SetPadding(FMargin(0.f, 2.f));
			}
			bSelectedStillVisible |= Room.RoomId == SelectedRoomId;
			++Shown;
		}
	}
	if (!bSelectedStillVisible)
	{
		SelectedRoomId = 0;
	}
	if (Shown == 0)
	{
		UTextBlock* Empty = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Empty->SetText(FText::FromString(Filter.IsEmpty()
			? TEXT("열린 방이 없습니다. [방 만들기]나 [빠른 찾기]를 눌러 보세요.")
			: TEXT("검색과 일치하는 방이 없습니다.")));
		Style().ApplyText(Empty, EVHTextRole::Small);
		RoomListBox->AddChild(Empty);
	}
	if (RoomCountText && Net)
	{
		RoomCountText->SetText(FText::FromString(FString::Printf(TEXT("대기 중인 방 %d개 (표시 %d개)"), Net->GetRoomListTotal(), Shown)));
	}
	UpdateButtons();
}

void UVHLobbyWidget::UpdateButtons()
{
	const UVHNetSubsystem* Net = GetNet();
	const bool bBusy = !Net || Net->IsRequestInFlight();
	const FVHRoomSummary* Selected = Net ? Net->GetRoomList().FindByPredicate([this](const FVHRoomSummary& R) { return R.RoomId == SelectedRoomId; }) : nullptr;

	PrimaryJoinButton->SetIsEnabled(!bBusy && Selected && Selected->CanJoin());
	PrimaryQuickFindButton->SetIsEnabled(!bBusy);
	CreateRoomButton->SetIsEnabled(!bBusy);
	RefreshButton->SetIsEnabled(!bBusy && !bRefreshCooling);

	FText Hint;
	PrimaryCreateConfirmButton->SetIsEnabled(!bBusy && ValidateRoomName(Hint));
}

void UVHLobbyWidget::HandleRefresh()
{
	UVHNetSubsystem* Net = GetNet();
	if (!Net || bRefreshCooling)
	{
		return;
	}
	if (Net->RequestRoomList(0))
	{
		SetStatus(LobbyStatusText, FText::FromString(TEXT("방 목록을 새로 불러오는 중...")));
	}
	// 연타 방지: 일정 시간 버튼을 잠근다
	bRefreshCooling = true;
	UpdateButtons();
	GetWorld()->GetTimerManager().SetTimer(RefreshCooldownTimer, this, &UVHLobbyWidget::EndRefreshCooldown, RefreshCooldownSeconds, false);
}

void UVHLobbyWidget::EndRefreshCooldown()
{
	bRefreshCooling = false;
	UpdateButtons();
}

void UVHLobbyWidget::HandleOpenCreate()
{
	const UVHNetSubsystem* Net = GetNet();
	RoomNameTextBox->SetText(FText::FromString(Net ? FString::Printf(TEXT("%s의 방"), *Net->GetMyName()) : FString()));
	CreateRoomPanel->SetVisibility(ESlateVisibility::Visible);
	HandleRoomNameChanged(RoomNameTextBox->GetText());
	RoomNameTextBox->SetKeyboardFocus();
}

bool UVHLobbyWidget::ValidateRoomName(FText& OutHint) const
{
	const FString Name = RoomNameTextBox->GetText().ToString().TrimStartAndEnd();
	const int32 Bytes = VHPacket::Utf8Length(Name);
	if (Name.IsEmpty())
	{
		OutHint = FText::FromString(TEXT("방 이름을 입력해 주세요."));
		return false;
	}
	if (Bytes > RobProto::MAX_ROOM_NAME_LEN)
	{
		OutHint = FText::FromString(FString::Printf(TEXT("방 이름이 너무 깁니다. (%d/%d바이트, 한글은 글자당 3바이트)"), Bytes, RobProto::MAX_ROOM_NAME_LEN));
		return false;
	}
	OutHint = FText::FromString(FString::Printf(TEXT("%d/%d바이트"), Bytes, RobProto::MAX_ROOM_NAME_LEN));
	return true;
}

void UVHLobbyWidget::HandleRoomNameChanged(const FText& Text)
{
	FText Hint;
	const bool bOk = ValidateRoomName(Hint);
	SetStatus(CreateHintText, Hint, !bOk);
	UpdateButtons();
}

void UVHLobbyWidget::HandleCreateConfirm()
{
	UVHNetSubsystem* Net = GetNet();
	FText Hint;
	if (!Net || !ValidateRoomName(Hint))
	{
		return;
	}
	if (Net->CreateRoom(RoomNameTextBox->GetText().ToString()))
	{
		SetStatus(CreateHintText, FText::FromString(TEXT("방을 만드는 중...")));
		UpdateButtons();
	}
}

void UVHLobbyWidget::HandleCreateCancel()
{
	CreateRoomPanel->SetVisibility(ESlateVisibility::Collapsed);
}

void UVHLobbyWidget::HandleQuickFind()
{
	if (UVHNetSubsystem* Net = GetNet())
	{
		CreateRoomPanel->SetVisibility(ESlateVisibility::Collapsed);
		Net->QuickFind();
		UpdateButtons();
	}
}

void UVHLobbyWidget::HandleJoin()
{
	UVHNetSubsystem* Net = GetNet();
	if (Net && SelectedRoomId != 0 && Net->JoinRoom(SelectedRoomId))
	{
		SetStatus(LobbyStatusText, FText::FromString(TEXT("방에 들어가는 중...")));
		UpdateButtons();
	}
}

void UVHLobbyWidget::HandleBack()
{
	// 로그아웃 = 연결 종료 후 메인화면
	if (UVHNetSubsystem* Net = GetNet())
	{
		Net->Disconnect();
	}
	GoTo(EVHScreen::MainMenu);
}

void UVHLobbyWidget::HandleSearchChanged(const FText& Text)
{
	RebuildList();
}

void UVHLobbyWidget::HandleRoomListUpdated()
{
	RebuildList();
	const UVHNetSubsystem* Net = GetNet();
	if (Net && !Net->IsRequestInFlight())
	{
		SetStatus(LobbyStatusText, FText::FromString(FString::Printf(TEXT("방 목록을 받았습니다. (%d개)"), Net->GetRoomList().Num())));
	}
}

void UVHLobbyWidget::HandleRoomRequestResult(bool bSuccess, const FText& Message)
{
	SetStatus(LobbyStatusText, Message, !bSuccess, bSuccess);
	if (!bSuccess)
	{
		SetStatus(CreateHintText, Message, true);
		if (UVHNetSubsystem* Net = GetNet())
		{
			Net->RequestRoomList(0);	// 실패했으면 목록이 바뀌었을 수 있으니 다시 받는다
		}
	}
	UpdateButtons();
}

void UVHLobbyWidget::HandleStatusMessage(const FText& Message)
{
	SetStatus(LobbyStatusText, Message);
	UpdateButtons();
}

void UVHLobbyWidget::HandleLeftRoom(const FText& Message)
{
	SetStatus(LobbyStatusText, Message);
}
