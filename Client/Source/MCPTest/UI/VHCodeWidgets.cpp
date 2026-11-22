#include "UI/VHCodeWidgets.h"

#include "Network/VHSettings.h"
#include "Network/VHNetSubsystem.h"
#include "Game/VHGameData.h"
#include "Gameplay/VHRanking.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Engine/Texture2D.h"

namespace
{
	UHorizontalBoxSlot* AddFill(UHorizontalBox* Box, UWidget* Child, float Fill, FMargin Padding = FMargin(4.f, 0.f))
	{
		UHorizontalBoxSlot* Slot = Box->AddChildToHorizontalBox(Child);
		FSlateChildSize Size(ESlateSizeRule::Fill);
		Size.Value = Fill;
		Slot->SetSize(Size);
		Slot->SetPadding(Padding);
		Slot->SetVerticalAlignment(VAlign_Center);
		return Slot;
	}

	UVerticalBoxSlot* AddRow(UVerticalBox* Box, UWidget* Child, FMargin Padding = FMargin(0.f, 2.f), EHorizontalAlignment HAlign = HAlign_Fill)
	{
		UVerticalBoxSlot* Slot = Box->AddChildToVerticalBox(Child);
		Slot->SetPadding(Padding);
		Slot->SetHorizontalAlignment(HAlign);
		return Slot;
	}
}

// ───────────────────────── 공통 ─────────────────────────

void UVHCodeWidget::EnsureBuilt()
{
	if (bBuilt)
	{
		return;
	}
	if (!WidgetTree)
	{
		Initialize();
	}
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildTree();
	}
	bBuilt = WidgetTree && WidgetTree->RootWidget;
}

TSharedRef<SWidget> UVHCodeWidget::RebuildWidget()
{
	EnsureBuilt();
	return Super::RebuildWidget();
}

const UVHUIStyle& UVHCodeWidget::Style() const
{
	return UVHSettings::Get().GetStyle();
}

UTextBlock* UVHCodeWidget::NewText(EVHTextRole Role, const FText& InText)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(InText);
	Style().ApplyText(Text, Role);
	return Text;
}

// ───────────────────────── 토스트 ─────────────────────────

void UVHToastWidget::BuildTree()
{
	List = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ToastList"));
	WidgetTree->RootWidget = List;
	SetVisibility(ESlateVisibility::HitTestInvisible);	// 알림이 클릭을 막지 않게
}

void UVHToastWidget::AddMessage(const FText& Message, bool bIsError)
{
	EnsureBuilt();
	const UVHUIStyle& S = Style();

	UBorder* Box = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	S.ApplyPanel(Box, true);
	Box->SetPadding(FMargin(22.f, 12.f));
	UTextBlock* Text = NewText(EVHTextRole::Body, Message);
	Text->SetColorAndOpacity(FSlateColor(bIsError ? S.Error : S.Text));
	// 자동 줄바꿈은 가운데 정렬 칸에서 지난 프레임 너비로 끊겨 "(내 반납 / $2,200)"처럼 중간에서 잘린다 → 고정 너비에서만 줄바꿈
	Text->SetAutoWrapText(false);
	Text->SetWrapTextAt(FMath::Max(0.f, S.ToastWrapWidth));
	Text->SetJustification(ETextJustify::Center);
	Box->SetContent(Text);

	AddRow(List, Box, FMargin(0.f, Entries.Num() == 0 ? 40.f : 8.f, 0.f, 0.f), HAlign_Center);

	FEntry Entry;
	Entry.Widget = Box;
	Entry.ExpireAt = FPlatformTime::Seconds() + (bIsError ? 6.0 : 4.0);
	Entries.Add(Entry);

	while (Entries.Num() > 4)
	{
		if (UWidget* Old = Entries[0].Widget.Get())
		{
			Old->RemoveFromParent();
		}
		Entries.RemoveAt(0);
	}
}

void UVHToastWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const double Now = FPlatformTime::Seconds();
	for (int32 i = Entries.Num() - 1; i >= 0; --i)
	{
		if (Entries[i].ExpireAt <= Now)
		{
			if (UWidget* W = Entries[i].Widget.Get())
			{
				W->RemoveFromParent();
			}
			Entries.RemoveAt(i);
		}
	}
}

// ───────────────────────── 방 목록 한 줄 ─────────────────────────

void UVHRoomRowWidget::BuildTree()
{
	const UVHUIStyle& S = Style();
	Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("RowButton"));
	WidgetTree->RootWidget = Button;
	S.ApplyButton(Button);
	Button->OnClicked.AddDynamic(this, &UVHRoomRowWidget::HandleClicked);

	UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	// 버튼 내용은 기본이 가운데 정렬이라, 표처럼 보이도록 가로로 꽉 채운다
	if (UButtonSlot* LineSlot = Cast<UButtonSlot>(Button->SetContent(Line)))
	{
		LineSlot->SetHorizontalAlignment(HAlign_Fill);
		LineSlot->SetVerticalAlignment(VAlign_Center);
	}
	const float Fills[] = { 3.2f, 1.6f, 0.8f, 0.9f, 0.9f };
	for (float Fill : Fills)
	{
		UTextBlock* Cell = NewText(EVHTextRole::Body, FText::GetEmpty());
		Cell->SetClipping(EWidgetClipping::ClipToBounds);
		AddFill(Line, Cell, Fill);
		Cells.Add(Cell);
	}
	Refresh();
}

UVHRoomRowWidget* UVHRoomRowWidget::MakeHeader(UWidget* Owner)
{
	UVHRoomRowWidget* Row = CreateWidget<UVHRoomRowWidget>(Owner, UVHRoomRowWidget::StaticClass());
	Row->bHeader = true;
	Row->EnsureBuilt();
	Row->Refresh();
	return Row;
}

void UVHRoomRowWidget::SetRoom(const FVHRoomSummary& InRoom, bool bInSelected)
{
	Room = InRoom;
	bSelected = bInSelected;
	EnsureBuilt();
	Refresh();
}

void UVHRoomRowWidget::Refresh()
{
	if (!Button || Cells.Num() < 5)
	{
		return;
	}
	const UVHUIStyle& S = Style();
	if (bHeader)
	{
		const TCHAR* Labels[] = { TEXT("방 이름"), TEXT("방장"), TEXT("인원"), TEXT("맵"), TEXT("상태") };
		for (int32 i = 0; i < 5; ++i)
		{
			Cells[i]->SetText(FText::FromString(Labels[i]));
			S.ApplyText(Cells[i], EVHTextRole::Small);
		}
		Button->SetIsEnabled(false);
		FButtonStyle BS = Button->GetStyle();
		FSlateBrush Empty;
		Empty.DrawAs = ESlateBrushDrawType::NoDrawType;
		BS.SetDisabled(Empty);
		Button->SetStyle(BS);
		return;
	}

	Cells[0]->SetText(FText::FromString(Room.Name));
	Cells[1]->SetText(FText::FromString(Room.HostName));
	Cells[2]->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), Room.PlayerCount, Room.MaxPlayers)));
	{
		const UVHGameData& Data = UVHSettings::Get().GetGameData();
		Cells[3]->SetText(Data.MapName(Data.DefaultMapId));	// v2.1: 방 목록에 맵 정보 없음 → 기본 맵
	}
	Cells[4]->SetText(UVHNetText::RoomStateText(Room.State));

	const bool bJoinable = Room.CanJoin();
	for (UTextBlock* Cell : Cells)
	{
		S.ApplyText(Cell, EVHTextRole::Body, bJoinable ? S.Text : S.DisabledText);
	}
	if (Room.State == EVHRoomState::Waiting && !bJoinable)
	{
		Cells[2]->SetColorAndOpacity(FSlateColor(S.Error));	// 가득 참
	}
	S.ApplyButton(Button, bSelected ? EVHButtonKind::Selected : EVHButtonKind::Normal);
}

void UVHRoomRowWidget::HandleClicked()
{
	if (!bHeader && OnSelected)
	{
		OnSelected(Room.RoomId);
	}
}

// ───────────────────────── 선택 버튼 ─────────────────────────

void UVHChoiceButtonWidget::BuildTree()
{
	Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ChoiceButton"));
	WidgetTree->RootWidget = Button;
	Button->OnClicked.AddDynamic(this, &UVHChoiceButtonWidget::HandleClicked);

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Button->SetContent(Col);

	// 그림 칸 (SetPortrait 를 부른 버튼만 보인다): 어두운 바탕 + 그림 (없으면 글자)
	PortraitBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PortraitBox"));
	PortraitBox->SetVisibility(ESlateVisibility::Collapsed);
	UBorder* PortraitBack = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	PortraitBack->SetBrushColor(FLinearColor(0.02f, 0.025f, 0.035f, 0.85f));
	PortraitBack->SetPadding(FMargin(0.f));
	PortraitBox->SetContent(PortraitBack);
	UOverlay* PortraitOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
	PortraitBack->SetContent(PortraitOverlay);
	PortraitImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PortraitImage"));
	if (UOverlaySlot* ImgSlot = PortraitOverlay->AddChildToOverlay(PortraitImage))
	{
		ImgSlot->SetHorizontalAlignment(HAlign_Center);
		ImgSlot->SetVerticalAlignment(VAlign_Center);
	}
	PortraitText = NewText(EVHTextRole::Small, FText::GetEmpty());
	if (UOverlaySlot* TxtSlot = PortraitOverlay->AddChildToOverlay(PortraitText))
	{
		TxtSlot->SetHorizontalAlignment(HAlign_Center);
		TxtSlot->SetVerticalAlignment(VAlign_Center);
	}
	AddRow(Col, PortraitBox, FMargin(0.f, 0.f, 0.f, 4.f), HAlign_Fill);

	TitleText = NewText(EVHTextRole::Button, FText::GetEmpty());
	SubText = NewText(EVHTextRole::Small, FText::GetEmpty());
	AddRow(Col, TitleText, FMargin(0.f), HAlign_Center);
	AddRow(Col, SubText, FMargin(0.f), HAlign_Center);
	Refresh();
}

void UVHChoiceButtonWidget::Setup(int32 InId, const FText& InTitle, const FText& InTooltip)
{
	Id = InId;
	Title = InTitle;
	EnsureBuilt();
	SetToolTipText(InTooltip);
	Refresh();
}

void UVHChoiceButtonWidget::SetState(bool bInSelected, bool bInEnabled, const FText& InSubtitle)
{
	bSelected = bInSelected;
	bEnabledState = bInEnabled;
	Subtitle = InSubtitle;
	EnsureBuilt();
	Refresh();
}

void UVHChoiceButtonWidget::SetPortrait(UTexture2D* Texture, float Height, const FText& Placeholder)
{
	EnsureBuilt();
	PortraitBox->SetHeightOverride(Height);
	PortraitBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (Texture)
	{
		// 그림 비율 그대로 높이에 맞춘다 (폭은 칸 가운데)
		// (SetDesiredSizeOverride 만 쓰면 칸 높이로 늘어나고 폭은 그대로라 그림이 좁게 눌렸다 → 붓 크기를 직접 정하고 가운데 정렬)
		const float Aspect = Texture->GetSizeY() > 0 ? static_cast<float>(Texture->GetSizeX()) / Texture->GetSizeY() : 1.f;
		FSlateBrush Brush;
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = FVector2D(Height * Aspect, Height);
		Brush.DrawAs = ESlateBrushDrawType::Image;
		PortraitImage->SetBrush(Brush);
		PortraitImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		PortraitText->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		PortraitImage->SetVisibility(ESlateVisibility::Collapsed);
		PortraitText->SetText(Placeholder);
		PortraitText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	Refresh();
}

void UVHChoiceButtonWidget::Refresh()
{
	if (!Button)
	{
		return;
	}
	const UVHUIStyle& S = Style();
	if (PortraitImage)
	{
		// 고를 수 없는 캐릭터(다른 사람이 고름)는 그림을 어둡게
		PortraitImage->SetColorAndOpacity(bEnabledState || bSelected ? FLinearColor::White : FLinearColor(0.35f, 0.35f, 0.35f, 1.f));
	}
	TitleText->SetText(Title);
	SubText->SetText(Subtitle);
	SubText->SetVisibility(Subtitle.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	S.ApplyButton(Button, bSelected ? EVHButtonKind::Selected : EVHButtonKind::Normal);
	Button->SetIsEnabled(bEnabledState);
	S.ApplyText(TitleText, EVHTextRole::Button, bEnabledState || bSelected ? (bSelected ? S.Accent : S.Text) : S.DisabledText);
}

void UVHChoiceButtonWidget::HandleClicked()
{
	if (OnChosen)
	{
		OnChosen(Id);
	}
}

// ───────────────────────── 대기실 한 칸 ─────────────────────────

void UVHRoomSlotWidget::BuildTree()
{
	Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SlotPanel"));
	WidgetTree->RootWidget = Panel;
	Style().ApplyPanel(Panel, true);
	Panel->SetPadding(FMargin(18.f, 16.f));

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(Col);
	NameText = NewText(EVHTextRole::Heading, FText::GetEmpty());
	CharacterText = NewText(EVHTextRole::Body, FText::GetEmpty());
	PerkText = NewText(EVHTextRole::Small, FText::GetEmpty());
	PerkText->SetAutoWrapText(true);
	ReadyText = NewText(EVHTextRole::Heading, FText::GetEmpty());
	AddRow(Col, NameText, FMargin(0.f, 0.f, 0.f, 8.f));
	AddRow(Col, CharacterText, FMargin(0.f, 2.f));
	AddRow(Col, PerkText, FMargin(0.f, 2.f, 0.f, 10.f));
	AddRow(Col, ReadyText, FMargin(0.f, 4.f));
}

void UVHRoomSlotWidget::SetMember(const FVHRoomMember* Member, bool bIsHost, bool bIsMe)
{
	EnsureBuilt();
	const UVHUIStyle& S = Style();
	const UVHGameData& Data = UVHSettings::Get().GetGameData();

	if (!Member)
	{
		NameText->SetText(FText::FromString(TEXT("빈자리")));
		S.ApplyText(NameText, EVHTextRole::Heading, S.DisabledText);
		CharacterText->SetText(FText::FromString(TEXT("다른 의적을 기다리는 중...")));
		S.ApplyText(CharacterText, EVHTextRole::Body, S.DisabledText);
		PerkText->SetText(FText::GetEmpty());
		ReadyText->SetText(FText::GetEmpty());
		S.ApplyPanel(Panel, false);
		return;
	}

	FString Name = Member->Name;
	if (bIsHost) Name = FString::Printf(TEXT("★ %s"), *Name);
	if (bIsMe) Name += TEXT(" (나)");
	NameText->SetText(FText::FromString(Name));
	S.ApplyText(NameText, EVHTextRole::Heading, bIsMe ? S.Accent : S.Text);

	CharacterText->SetText(FText::FromString(FString::Printf(TEXT("캐릭터: %s"), *Data.CharacterName(Member->CharacterId).ToString())));
	S.ApplyText(CharacterText, EVHTextRole::Body, Member->CharacterId == 0 ? S.SubText : S.Text);

	TArray<FString> PerkNames;
	for (int32 P : Member->Perks)
	{
		PerkNames.Add(Data.PerkName(P).ToString());
	}
	PerkText->SetText(FText::FromString(FString::Printf(TEXT("퍽: %s"), PerkNames.Num() > 0 ? *FString::Join(PerkNames, TEXT(", ")) : TEXT("없음"))));

	ReadyText->SetText(FText::FromString(Member->bReady ? TEXT("준비 완료") : TEXT("준비 중")));
	S.ApplyText(ReadyText, EVHTextRole::Heading, Member->bReady ? S.Success : S.SubText);
	S.ApplyPanel(Panel, true);
}

// ───────────────────────── 결과 카드 ─────────────────────────

void UVHPlayerResultCard::BuildTree()
{
	Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CardPanel"));
	WidgetTree->RootWidget = Panel;
	Style().ApplyPanel(Panel, true);
	Panel->SetPadding(FMargin(22.f, 18.f));

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(Col);
	OutcomeText = NewText(EVHTextRole::Big, FText::GetEmpty());
	NameText = NewText(EVHTextRole::Heading, FText::GetEmpty());
	DetailText = NewText(EVHTextRole::Body, FText::GetEmpty());
	ScoreText = NewText(EVHTextRole::Heading, FText::GetEmpty());
	BestText = NewText(EVHTextRole::Body, FText::GetEmpty());
	AddRow(Col, OutcomeText, FMargin(0.f, 0.f, 0.f, 6.f), HAlign_Center);
	AddRow(Col, NameText, FMargin(0.f, 0.f, 0.f, 8.f), HAlign_Center);
	AddRow(Col, ScoreText, FMargin(0.f, 0.f, 0.f, 4.f), HAlign_Center);
	AddRow(Col, BestText, FMargin(0.f, 0.f, 0.f, 12.f), HAlign_Center);
	// 통계도 제목처럼 가운데 정렬
	DetailText->SetJustification(ETextJustify::Center);
	AddRow(Col, DetailText, FMargin(0.f, 2.f), HAlign_Center);
}

void UVHPlayerResultCard::SetResult(const FVHPlayerResult& R, bool bTied, bool bShowCombat, bool bShowBest)
{
	EnsureBuilt();
	const UVHUIStyle& S = Style();
	const UVHGameData& Data = UVHSettings::Get().GetGameData();
	const bool bIsMe = R.bIsMe;

	// PvP: 승패는 반납 금액 순위. 1위(공동 1위 포함) = 승리 [PvP 규칙 v1 2장, 사용자 결정: 공동 순위 임시]
	const bool bWin = R.Rank == 1;
	OutcomeText->SetText(FText::FromString(FString::Printf(TEXT("%s위 · %s"), *VHRanking::RankLabel(R.Rank, bTied), bWin ? TEXT("승리") : TEXT("패배"))));
	S.ApplyText(OutcomeText, EVHTextRole::Big, bWin ? S.Success : S.Error);

	NameText->SetText(FText::FromString(FString::Printf(TEXT("%s%s · %s"), *R.Name, bIsMe ? TEXT(" (나)") : TEXT(""),
		*Data.CharacterName(R.CharacterId).ToString())));
	S.ApplyText(NameText, EVHTextRole::Heading, bIsMe ? S.Accent : S.Text);

	// 서버 v3.0 결과: 쓰러뜨림·쓰러짐, 반납한 보물
	const FString NL(TEXT("\n"));
	FString Detail = bShowCombat
		? FString::Printf(TEXT("쓰러뜨림 / 쓰러짐: %d / %d"), R.Kills, R.Deaths)
		: FString(TEXT("쓰러뜨림 / 쓰러짐: -"));
	Detail += NL +FString::Printf(TEXT("반납한 보물: %d개"), R.TreasureEscaped);

	// 최고 기록 (서버 best_record = 이 판까지의 최고 기록, new_record = 이 판으로 갱신). 갱신이면 눈에 띄는 색
	if (!bShowBest)
	{
		BestText->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		BestText->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (R.bNewRecord)
		{
			BestText->SetText(FText::FromString(FString::Printf(TEXT("★ 최고 기록 갱신! %s"), *VHRanking::Money(R.BestRecord))));
			S.ApplyText(BestText, EVHTextRole::Body, S.Success);
		}
		else
		{
			BestText->SetText(FText::FromString(R.BestRecord > 0
				? FString::Printf(TEXT("최고 기록 %s"), *VHRanking::Money(R.BestRecord))
				: FString(TEXT("최고 기록 없음"))));
			S.ApplyText(BestText, EVHTextRole::Body, S.SubText);
		}
	}
	if (R.bLeftEarly)
	{
		Detail += NL +FString(TEXT("(중도 이탈)"));
	}
	DetailText->SetText(FText::FromString(Detail));

	// 반납 금액 = 점수
	ScoreText->SetText(FText::FromString(FString::Printf(TEXT("반납 %s"), *VHRanking::Money(R.ValueDelivered))));
	S.ApplyText(ScoreText, EVHTextRole::Heading, S.Accent);
	S.ApplyPanel(Panel, true);
}
