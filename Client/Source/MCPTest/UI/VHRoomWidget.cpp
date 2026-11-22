#include "UI/VHRoomWidget.h"

#include "Network/VHNetSubsystem.h"
#include "Game/VHGameData.h"
#include "UI/VHCodeWidgets.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBoxSlot.h"
#include "Components/ComboBoxString.h"

namespace
{
	/** 패널 종류에 맞게 자식 칸의 여백·크기를 정한다 */
	void SetupChildSlot(UPanelSlot* PanelSlot, float Padding, bool bFill)
	{
		if (UHorizontalBoxSlot* H = Cast<UHorizontalBoxSlot>(PanelSlot))
		{
			H->SetPadding(FMargin(Padding));
			if (bFill)
			{
				H->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}
		}
		else if (UVerticalBoxSlot* V = Cast<UVerticalBoxSlot>(PanelSlot))
		{
			V->SetPadding(FMargin(Padding));
		}
		else if (UWrapBoxSlot* W = Cast<UWrapBoxSlot>(PanelSlot))
		{
			W->SetPadding(FMargin(Padding));
		}
	}
}

void UVHRoomWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	PrimaryReadyButton->OnClicked.AddDynamic(this, &UVHRoomWidget::HandleReady);
	LeaveButton->OnClicked.AddDynamic(this, &UVHRoomWidget::HandleLeave);
	MapComboBox->OnSelectionChanged.AddDynamic(this, &UVHRoomWidget::HandleMapChanged);
	DifficultyComboBox->OnSelectionChanged.AddDynamic(this, &UVHRoomWidget::HandleDifficultyChanged);
	BuildChoices();
	// 프로토콜 v2.1: 맵·난이도 변경 패킷(CS_ROOM_OPTION)이 없다 → 선택 칸 숨김
	CollapseWithLabel(MapComboBox);
	CollapseWithLabel(DifficultyComboBox);
}

void UVHRoomWidget::BindNetEvents(UVHNetSubsystem& Net)
{
	Net.OnRoomStateChanged.AddDynamic(this, &UVHRoomWidget::HandleRoomStateChanged);
	Net.OnRoomError.AddDynamic(this, &UVHRoomWidget::HandleRoomError);
}

void UVHRoomWidget::BuildChoices()
{
	const UVHGameData& Data = GameData();

	bUpdatingCombos = true;
	MapComboBox->ClearOptions();
	for (const FVHMapDef& Map : Data.Maps)
	{
		MapComboBox->AddOption(Map.DisplayName.ToString());
	}
	DifficultyComboBox->ClearOptions();
	for (EVHDifficulty D : { EVHDifficulty::Easy, EVHDifficulty::Normal, EVHDifficulty::Hard })
	{
		DifficultyComboBox->AddOption(UVHNetText::DifficultyText(D).ToString());
	}
	bUpdatingCombos = false;

	SlotsBox->ClearChildren();
	SlotWidgets.Reset();
	for (int32 i = 0; i < 3; ++i)	// MAX_ROOM_PLAYERS
	{
		UVHRoomSlotWidget* SlotWidget = CreateWidget<UVHRoomSlotWidget>(this, UVHRoomSlotWidget::StaticClass());
		SlotWidget->EnsureBuilt();
		SetupChildSlot(SlotsBox->AddChild(SlotWidget), 6.f, true);
		SlotWidgets.Add(SlotWidget);
	}

	CharacterBox->ClearChildren();
	CharacterButtons.Reset();
	for (const FVHCharacterDef& C : Data.Characters)
	{
		UVHChoiceButtonWidget* Button = CreateWidget<UVHChoiceButtonWidget>(this, UVHChoiceButtonWidget::StaticClass());
		Button->Setup(C.Id, C.DisplayName, C.Description);
		// 얼굴 그림 (DA_VHGameData 의 Portrait). 없으면 "준비 중" — 메시 칸이 빈 캐릭터는 게임에서 도적1 모습 (2026-11-21 도적1~3 모두 그림 있음)
		Button->SetPortrait(C.Portrait.LoadSynchronous(), 132.f, FText::FromString(TEXT("모델 준비 중\n(지금은 도적1 모습)")));
		Button->OnChosen = [this](int32 Id) { OnCharacterChosen(Id); };
		SetupChildSlot(CharacterBox->AddChild(Button), 4.f, true);
		CharacterButtons.Add(Button);
	}

	PerkBox->ClearChildren();
	PerkButtons.Reset();
	for (const FVHPerkDef& P : Data.Perks)
	{
		UVHChoiceButtonWidget* Button = CreateWidget<UVHChoiceButtonWidget>(this, UVHChoiceButtonWidget::StaticClass());
		Button->Setup(P.Id, P.DisplayName, P.Description);
		Button->OnChosen = [this](int32 Id) { OnPerkChosen(Id); };
		SetupChildSlot(PerkBox->AddChild(Button), 4.f, false);
		PerkButtons.Add(Button);
	}
}

void UVHRoomWidget::OnScreenShown()
{
	SetStatus(RoomStatusText, FText::GetEmpty());
	Refresh();
}

void UVHRoomWidget::Refresh()
{
	const UVHNetSubsystem* Net = GetNet();
	if (!Net)
	{
		return;
	}
	const UVHGameData& Data = GameData();
	const UVHUIStyle& S = Style();
	const FVHRoomState& Room = Net->GetRoomState();

	if (!Room.bValid)
	{
		RoomTitleText->SetText(FText::FromString(TEXT("대기실")));
		RoomInfoText->SetText(FText::FromString(TEXT("방 정보를 받는 중...")));
		PrimaryReadyButton->SetIsEnabled(false);
		return;
	}

	const FVHRoomMember* Me = Net->GetMyMember();
	const bool bHost = Net->IsHost();
	const bool bMeReady = Me && Me->bReady;
	const bool bWaiting = Room.State == EVHRoomState::Waiting;

	RoomTitleText->SetText(FText::FromString(Room.Name));
	RoomInfoText->SetText(FText::FromString(FString::Printf(TEXT("맵: %s   ·   인원 %d/3   ·   %s"),
		*Data.MapName(Data.DefaultMapId).ToString(), Room.Members.Num(), *UVHNetText::RoomStateText(Room.State).ToString())));

	// 슬롯: 서버의 slot 번호 위치에 그린다
	for (int32 i = 0; i < SlotWidgets.Num(); ++i)
	{
		const FVHRoomMember* Member = Room.Members.FindByPredicate([i](const FVHRoomMember& M) { return M.Slot == i; });
		SlotWidgets[i]->SetMember(Member, Member && Member->AccountId == Room.HostAccountId,
			Member && Member->AccountId == Net->GetMyAccountId());
	}

	// 캐릭터: 다른 사람이 고른 캐릭터는 고를 수 없다(중복 불가)
	for (UVHChoiceButtonWidget* Button : CharacterButtons)
	{
		const FVHRoomMember* Owner = Room.Members.FindByPredicate([Button](const FVHRoomMember& M) { return M.CharacterId == Button->GetId(); });
		const bool bMine = Owner && Me && Owner->AccountId == Me->AccountId;
		const bool bTakenByOther = Owner && !bMine;
		const FText Sub = bTakenByOther ? FText::FromString(FString::Printf(TEXT("%s 선택"), *Owner->Name))
			: (bMine ? FText::FromString(TEXT("내 캐릭터")) : FText::GetEmpty());
		Button->SetState(bMine, Me && bWaiting && !bMeReady && !bTakenByOther, Sub);
	}

	// 퍽: 최대 3개, 다른 플레이어와는 겹쳐도 된다
	const int32 MyPerkCount = Me ? Me->Perks.Num() : 0;
	for (UVHChoiceButtonWidget* Button : PerkButtons)
	{
		const bool bMine = Me && Me->Perks.Contains(Button->GetId());
		const bool bCanAdd = MyPerkCount < Data.MaxPerks;
		Button->SetState(bMine, Me && bWaiting && !bMeReady && (bMine || bCanAdd), FText::GetEmpty());
	}
	PerkCountText->SetText(FText::FromString(FString::Printf(TEXT("퍽 %d/%d개 선택 (다른 플레이어와 겹쳐도 됨)"), MyPerkCount, Data.MaxPerks)));

	if (OptionHintText)
	{
		SetStatus(OptionHintText, FText::FromString(bHost ? TEXT("★ 내가 방장입니다.") : TEXT("")));
	}

	// 준비 버튼
	const bool bHasCharacter = Me && Me->CharacterId != 0;
	const int32 NeedPerks = FMath::Clamp(RequiredPerksForReady, 0, Data.MaxPerks);
	const bool bHasPerk = MyPerkCount >= NeedPerks;
	PrimaryReadyButton->SetIsEnabled(Me && bWaiting && (bMeReady || (bHasCharacter && bHasPerk)));
	ReadyButtonText->SetText(FText::FromString(bMeReady ? TEXT("준비 해제") : TEXT("준비")));
	S.ApplyButton(PrimaryReadyButton, bMeReady ? EVHButtonKind::Selected : EVHButtonKind::Primary);

	int32 ReadyCount = 0;
	for (const FVHRoomMember& M : Room.Members)
	{
		ReadyCount += M.bReady ? 1 : 0;
	}
	FString Hint;
	if (!bWaiting)
	{
		Hint = TEXT("모두 준비 완료! 게임을 불러오는 중...");
	}
	else if (!bHasCharacter)
	{
		Hint = NeedPerks > 0 ? FString::Printf(TEXT("캐릭터와 퍽 %d개를 고른 뒤 [준비]를 누르세요."), NeedPerks) : FString(TEXT("캐릭터를 고른 뒤 [준비]를 누르세요."));
	}
	else if (!bHasPerk)
	{
		Hint = FString::Printf(TEXT("퍽을 %d개 모두 골라야 준비할 수 있습니다. (현재 %d/%d)"), NeedPerks, MyPerkCount, NeedPerks);
	}
	else
	{
		Hint = FString::Printf(TEXT("모든 플레이어가 준비하면 게임이 자동으로 시작됩니다. (준비 %d/%d)"), ReadyCount, Room.Members.Num());
	}
	SetStatus(AutoStartHintText, FText::FromString(Hint), false, !bWaiting);
	LeaveButton->SetIsEnabled(bWaiting);
}

void UVHRoomWidget::HandleRoomStateChanged()
{
	Refresh();
}

void UVHRoomWidget::HandleRoomError(const FText& Message)
{
	SetStatus(RoomStatusText, Message, true);
	if (UVHNetSubsystem* Net = GetNet())
	{
		Net->ShowToast(Message, true);
	}
	Refresh();
}

void UVHRoomWidget::OnCharacterChosen(int32 CharacterId)
{
	UVHNetSubsystem* Net = GetNet();
	const FVHRoomMember* Me = Net ? Net->GetMyMember() : nullptr;
	if (!Me || Me->CharacterId == CharacterId)
	{
		return;
	}
	SetStatus(RoomStatusText, FText::GetEmpty());
	Net->SelectCharacter(CharacterId);
}

void UVHRoomWidget::OnPerkChosen(int32 PerkId)
{
	UVHNetSubsystem* Net = GetNet();
	const FVHRoomMember* Me = Net ? Net->GetMyMember() : nullptr;
	if (!Me)
	{
		return;
	}
	// 서버의 최신 상태를 기준으로 켜고/끈 목록을 보낸다
	TArray<int32> Perks = Me->Perks;
	if (Perks.Contains(PerkId))
	{
		Perks.Remove(PerkId);
	}
	else
	{
		if (Perks.Num() >= GameData().MaxPerks)
		{
			SetStatus(RoomStatusText, FText::FromString(TEXT("퍽은 최대 3개까지 고를 수 있습니다.")), true);
			return;
		}
		Perks.Add(PerkId);
	}
	SetStatus(RoomStatusText, FText::GetEmpty());
	Net->SelectPerks(Perks);
}

void UVHRoomWidget::HandleMapChanged(FString Selected, ESelectInfo::Type SelectionType)
{
	UVHNetSubsystem* Net = GetNet();
	if (bUpdatingCombos || SelectionType == ESelectInfo::Direct || !Net || !Net->IsHost())
	{
		return;
	}
	// v2.1: 맵 변경 패킷 없음 (선택 칸은 숨김)
}

void UVHRoomWidget::HandleDifficultyChanged(FString Selected, ESelectInfo::Type SelectionType)
{
	UVHNetSubsystem* Net = GetNet();
	if (bUpdatingCombos || SelectionType == ESelectInfo::Direct || !Net || !Net->IsHost())
	{
		return;
	}
	// v2.1: 난이도 변경 패킷 없음 (선택 칸은 숨김)
}

void UVHRoomWidget::HandleReady()
{
	UVHNetSubsystem* Net = GetNet();
	const FVHRoomMember* Me = Net ? Net->GetMyMember() : nullptr;
	if (Me)
	{
		Net->SetReady(!Me->bReady);
	}
}

void UVHRoomWidget::HandleLeave()
{
	if (UVHNetSubsystem* Net = GetNet())
	{
		if (Net->LeaveRoom())
		{
			SetStatus(RoomStatusText, FText::FromString(TEXT("방에서 나가는 중...")));
		}
	}
}
