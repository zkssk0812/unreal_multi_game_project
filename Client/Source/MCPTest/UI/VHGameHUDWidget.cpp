#include "UI/VHGameHUDWidget.h"
#include "UI/VHMinimapWidget.h"
#include "UI/VHCrosshairWidget.h"
#include "MCPTest.h"

#include "Gameplay/VHInteractionComponent.h"
#include "Gameplay/VHInteractionSubsystem.h"
#include "Gameplay/VHInteractionConfig.h"
#include "Gameplay/VHInteractableBase.h"
#include "Gameplay/VHTreasureCarrierComponent.h"
#include "Gameplay/VHThiefStateComponent.h"
#include "Gameplay/VHFlashlightComponent.h"
#include "Gameplay/VHActionComponent.h"
#include "Gameplay/VHStatusEffectComponent.h"
#include "Gameplay/VHMatchSubsystem.h"
#include "Gameplay/VHServerGameSubsystem.h"
#include "Gameplay/VHReturnPoint.h"
#include "Gameplay/VHRoomVolume.h"
#include "Gameplay/VHGameplayConfig.h"
#include "Gameplay/VHDoor.h"
#include "Gameplay/VHDoorSubsystem.h"
#include "Gameplay/VHWeaponCatalog.h"
#include "Gameplay/VHHealthComponent.h"
#include "Gameplay/VHRanking.h"
#include "Game/VHGameData.h"
#include "Network/VHNetSubsystem.h"
#include "EngineUtils.h"
#include "Network/VHSettings.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"

#define LOCTEXT_NAMESPACE "VHHud"

namespace
{
	constexpr int32 IdleBarPoolSize = 12;
	constexpr int32 WorldLabelPoolSize = 16;
	/** 순위표 줄 수 (최대 인원 3 + 여유) */
	constexpr int32 RankLinePoolSize = 4;
	/** 알림 영역 최대 줄 수 (실제 개수는 UVHUIStyle.HudNoticeCount) */
	constexpr int32 NoticePoolSize = 6;
	const TCHAR* KeyLabel = TEXT("[F]");

	UVerticalBoxSlot* AddLine(UVerticalBox* Box, UWidget* Child, FMargin Padding = FMargin(0.f, 2.f))
	{
		UVerticalBoxSlot* Slot = Box->AddChildToVerticalBox(Child);
		Slot->SetPadding(Padding);
		return Slot;
	}

	UHorizontalBoxSlot* AddCell(UHorizontalBox* Box, UWidget* Child, FMargin Padding = FMargin(0.f, 0.f, 8.f, 0.f))
	{
		UHorizontalBoxSlot* Slot = Box->AddChildToHorizontalBox(Child);
		Slot->SetPadding(Padding);
		Slot->SetVerticalAlignment(VAlign_Fill);
		return Slot;
	}

	FText Meters(float Cm)
	{
		FNumberFormattingOptions Opt;
		Opt.MinimumFractionalDigits = 1;
		Opt.MaximumFractionalDigits = 1;
		return FText::AsNumber(Cm / 100.f, &Opt);
	}

	FText Seconds(float S, int32 Digits)
	{
		FNumberFormattingOptions Opt;
		Opt.MinimumFractionalDigits = Digits;
		Opt.MaximumFractionalDigits = Digits;
		return FText::AsNumber(S, &Opt);
	}

	/** 안내 시간: 딱 떨어지면 정수("10초"), 아니면 소수 한 자리("9.1초" — 유용한 도구 작업 속도, 2026-11-27) */
	FText SecondsAuto(float S)
	{
		return Seconds(S, FMath::Abs(S - FMath::RoundToFloat(S)) < 0.05f ? 0 : 1);
	}

	FText MoneyText(int32 Value)
	{
		return FText::FromString(VHRanking::Money(Value));
	}

	/** 바 배경색 지정 (bTransparent면 배경 없음 → 다른 바 위에 겹칠 때) */
	void StyleBar(UProgressBar* Bar, const FLinearColor& Fill, const FLinearColor& Background, bool bTransparent)
	{
		FProgressBarStyle BarStyle = Bar->GetWidgetStyle();
		if (bTransparent)
		{
			BarStyle.BackgroundImage.DrawAs = ESlateBrushDrawType::NoDrawType;
		}
		else
		{
			BarStyle.BackgroundImage.TintColor = FSlateColor(Background);
		}
		Bar->SetWidgetStyle(BarStyle);
		Bar->SetFillColorAndOpacity(Fill);
	}
}

// ───────────────────────── 위젯 트리 ─────────────────────────

void UVHGameHUDWidget::BuildTree()
{
	const UVHUIStyle& S = Style();
	const float M = S.HudMargin;
	SetVisibility(ESlateVisibility::HitTestInvisible);	// 게임 입력을 막지 않게

	Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("HudCanvas"));
	WidgetTree->RootWidget = Canvas;

	auto NewPanel = [this, &S](FName Name, FMargin BoxPadding) -> UBorder*
	{
		UBorder* Box = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
		S.ApplyPanel(Box, false);
		Box->SetBrushColor(S.HudPanel);
		Box->SetPadding(BoxPadding);
		return Box;
	};
	auto NewBar = [this](FName Name) -> UProgressBar*
	{
		return WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), Name);
	};
	auto Sized = [this](UWidget* Child, float Width, float Height) -> USizeBox*
	{
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		if (Width > 0.f) { Box->SetWidthOverride(Width); }
		if (Height > 0.f) { Box->SetHeightOverride(Height); }
		Box->SetContent(Child);
		return Box;
	};
	auto Place = [this](UWidget* Widget, FAnchors Anchors, FVector2D Alignment, FVector2D Position) -> UCanvasPanelSlot*
	{
		UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(Widget);
		CanvasSlot->SetAutoSize(true);
		CanvasSlot->SetAnchors(Anchors);
		CanvasSlot->SetAlignment(Alignment);
		CanvasSlot->SetPosition(Position);
		return CanvasSlot;
	};

	// ── 옅은 진행 바 풀 (다른 의적이 여는 중·진행도가 남은 금고) ──
	for (int32 i = 0; i < IdleBarPoolSize; ++i)
	{
		UProgressBar* Bar = NewBar(NAME_None);
		Bar->SetFillColorAndOpacity(FLinearColor(1.f, 0.75f, 0.15f, 0.55f));
		Bar->SetRenderOpacity(0.6f);
		UCanvasPanelSlot* BarCanvasSlot = Canvas->AddChildToCanvas(Bar);
		BarCanvasSlot->SetSize(FVector2D(90.f, 8.f));
		BarCanvasSlot->SetAlignment(FVector2D(0.5f, 1.f));
		Bar->SetVisibility(ESlateVisibility::Collapsed);
		IdleBars.Add(Bar);
	}

	// ── 월드 위 작은 글자 풀 (반납지점 남은 시간·머리 위 이름) ──
	for (int32 i = 0; i < WorldLabelPoolSize; ++i)
	{
		UTextBlock* Label = NewText(EVHTextRole::Small, FText::GetEmpty());
		Label->SetJustification(ETextJustify::Center);
		Label->SetShadowOffset(FVector2D(1.f, 1.f));
		Label->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f));
		UCanvasPanelSlot* LabelSlot = Canvas->AddChildToCanvas(Label);
		LabelSlot->SetAutoSize(true);
		LabelSlot->SetAlignment(FVector2D(0.5f, 1.f));
		Label->SetVisibility(ESlateVisibility::Collapsed);
		WorldLabels.Add(Label);
	}

	// 배치는 기획 UI 수정본(Docs/Design/기획_2026-11/UI_수정본.png)을 따른다:
	// 좌상단 미니맵 · 위 가운데 남은 시간 · 우상단 순위 · 오른쪽 가운데 들고 있는 가치 · 좌하단 캐릭터(HP·SP·스킬·퍽) · 아래 가운데 소비 아이템·무기

	// ── 좌상단: 미니맵 ──
	Minimap = WidgetTree->ConstructWidget<UVHMinimapWidget>(UVHMinimapWidget::StaticClass(), TEXT("Minimap"));
	Place(Sized(Minimap, S.HudMinimapWidth, S.HudMinimapHeight), FAnchors(0.f, 0.f), FVector2D(0.f, 0.f), FVector2D(M, M));

	// ── 위 가운데: 남은 시간 (1분 이하 빨강) ──
	TimeBox = NewPanel(TEXT("TimeBox"), FMargin(22.f, 8.f));
	TimeText = NewText(EVHTextRole::Big, FText::GetEmpty());
	TimeText->SetJustification(ETextJustify::Center);
	TimeBox->SetContent(TimeText);
	Place(TimeBox, FAnchors(0.5f, 0.f), FVector2D(0.5f, 0.f), FVector2D(0.f, M));
	TimeBox->SetVisibility(ESlateVisibility::Collapsed);

	// ── 우상단: 순위표 (N위 닉네임 반납 금액, 내 줄은 강조색) ──
	TopRightBox = NewPanel(TEXT("TopRightBox"), FMargin(18.f, 10.f));
	{
		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		TopRightBox->SetContent(Col);
		for (int32 i = 0; i < RankLinePoolSize; ++i)
		{
			UTextBlock* Line = NewText(EVHTextRole::Body, FText::GetEmpty());
			AddLine(Col, Line, FMargin(0.f, 1.f))->SetHorizontalAlignment(HAlign_Right);
			Line->SetVisibility(ESlateVisibility::Collapsed);
			RankLines.Add(Line);
		}
	}
	Place(TopRightBox, FAnchors(1.f, 0.f), FVector2D(1.f, 0.f), FVector2D(-M, M));
	TopRightBox->SetVisibility(ESlateVisibility::Collapsed);

	// ── 우측 가운데: 들고 있는 가치 ──
	CarryBox = NewPanel(TEXT("CarryBox"), FMargin(18.f, 10.f));
	CarryText = NewText(EVHTextRole::Heading, FText::GetEmpty());
	CarryText->SetJustification(ETextJustify::Right);
	CarryBox->SetContent(CarryText);
	Place(CarryBox, FAnchors(1.f, 0.5f), FVector2D(1.f, 0.5f), FVector2D(-M, 0.f));

	// 칸 하나 (어두운 바탕 + 가운데 글자, 칸보다 긴 글자는 잘라서 칸 밖으로 넘치지 않게)
	auto NewSlot = [this, &S](EVHTextRole Role, const FText& Text, UTextBlock*& OutText) -> UBorder*
	{
		UBorder* Box = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Box->SetBrushColor(S.HudEmptySlotColor);
		Box->SetPadding(FMargin(6.f, 3.f));
		Box->SetHorizontalAlignment(HAlign_Center);
		Box->SetVerticalAlignment(VAlign_Center);
		Box->SetClipping(EWidgetClipping::ClipToBounds);
		OutText = NewText(Role, Text);
		OutText->SetJustification(ETextJustify::Center);
		OutText->SetColorAndOpacity(FSlateColor(S.HudEmptySlotText));
		Box->SetContent(OutText);
		return Box;
	};

	// ── 좌하단: 내 캐릭터 (이미지 · 닉네임 · 상태이상 · HP · SP · 스킬 · 퍽 3칸) ──
	PlayerBox = NewPanel(TEXT("PlayerBox"), FMargin(12.f));
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		PlayerBox->SetContent(Row);

		// 캐릭터 이미지 자리: DA_VHGameData 의 Portrait 가 있으면 이미지, 없으면 단색 칸 + 캐릭터 이름
		PortraitBox = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PortraitBox"));
		PortraitBox->SetBrushColor(S.HudPortraitColor);
		PortraitBox->SetPadding(FMargin(0.f));
		UOverlay* PortraitOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
		PortraitBox->SetContent(PortraitOverlay);
		PortraitImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PortraitImage"));
		PortraitImage->SetVisibility(ESlateVisibility::Collapsed);
		if (UOverlaySlot* ImgSlot = PortraitOverlay->AddChildToOverlay(PortraitImage))
		{
			ImgSlot->SetHorizontalAlignment(HAlign_Fill);
			ImgSlot->SetVerticalAlignment(VAlign_Fill);
		}
		PortraitText = NewText(EVHTextRole::Small, FText::GetEmpty());
		PortraitText->SetJustification(ETextJustify::Center);
		PortraitText->SetAutoWrapText(true);
		PortraitText->SetShadowOffset(FVector2D(1.f, 1.f));
		PortraitText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f));
		if (UOverlaySlot* TextSlot = PortraitOverlay->AddChildToOverlay(PortraitText))
		{
			TextSlot->SetHorizontalAlignment(HAlign_Center);
			TextSlot->SetVerticalAlignment(VAlign_Bottom);
			TextSlot->SetPadding(FMargin(4.f));
		}
		AddCell(Row, Sized(PortraitBox, S.HudPortraitSize, S.HudPortraitSize), FMargin(0.f, 0.f, 12.f, 0.f))->SetVerticalAlignment(VAlign_Top);

		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		AddCell(Row, Col, FMargin(0.f));

		// 닉네임 + 상태이상 (HP 바 위. 지속 시간은 표시하지 않는다 [기획 UI 메모])
		UHorizontalBox* NameRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		AddLine(Col, NameRow, FMargin(0.f, 0.f, 0.f, 4.f));
		NameText = NewText(EVHTextRole::Heading, FText::GetEmpty());
		AddCell(NameRow, NameText, FMargin(0.f, 0.f, 12.f, 0.f))->SetVerticalAlignment(VAlign_Center);
		StatusText = NewText(EVHTextRole::Small, FText::GetEmpty());
		StatusText->SetShadowOffset(FVector2D(1.f, 1.f));
		StatusText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f));
		AddCell(NameRow, StatusText, FMargin(0.f))->SetVerticalAlignment(VAlign_Center);

		// HP 바(빨강). 보호막은 HP 바 위쪽에 얇은 띠로 (HP 바를 가리지 않게). 길이 = 보호막 / 최대 HP
		UOverlay* HpOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
		HpBar = NewBar(TEXT("HpBar"));
		StyleBar(HpBar, S.HudHpColor, S.HudBarBackground, false);
		ShieldBar = NewBar(TEXT("ShieldBar"));
		StyleBar(ShieldBar, S.HudShieldColor, FLinearColor::Transparent, true);
		if (UOverlaySlot* HpSlot = HpOverlay->AddChildToOverlay(HpBar))
		{
			HpSlot->SetHorizontalAlignment(HAlign_Fill);
			HpSlot->SetVerticalAlignment(VAlign_Fill);
		}
		if (UOverlaySlot* ShieldSlot = HpOverlay->AddChildToOverlay(Sized(ShieldBar, 0.f, 6.f)))
		{
			ShieldSlot->SetHorizontalAlignment(HAlign_Fill);
			ShieldSlot->SetVerticalAlignment(VAlign_Top);
		}
		HpText = NewText(EVHTextRole::Small, FText::GetEmpty());
		HpText->SetShadowOffset(FVector2D(1.f, 1.f));
		HpText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f));
		if (UOverlaySlot* HpTextSlot = HpOverlay->AddChildToOverlay(HpText))
		{
			HpTextSlot->SetHorizontalAlignment(HAlign_Center);
			HpTextSlot->SetVerticalAlignment(VAlign_Center);
		}
		AddLine(Col, Sized(HpOverlay, 0.f, S.HudBarHeight + 6.f), FMargin(0.f, 2.f));

		// SP 바(파랑, 탈진이면 회색)
		UOverlay* SpOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
		SpBar = NewBar(TEXT("SpBar"));
		StyleBar(SpBar, S.HudSpColor, S.HudBarBackground, false);
		if (UOverlaySlot* SpSlot = SpOverlay->AddChildToOverlay(SpBar))
		{
			SpSlot->SetHorizontalAlignment(HAlign_Fill);
			SpSlot->SetVerticalAlignment(VAlign_Fill);
		}
		SpText = NewText(EVHTextRole::Small, FText::GetEmpty());
		SpText->SetShadowOffset(FVector2D(1.f, 1.f));
		SpText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f));
		if (UOverlaySlot* SpTextSlot = SpOverlay->AddChildToOverlay(SpText))
		{
			SpTextSlot->SetHorizontalAlignment(HAlign_Center);
			SpTextSlot->SetVerticalAlignment(VAlign_Center);
		}
		AddLine(Col, Sized(SpOverlay, 0.f, S.HudBarHeight), FMargin(0.f, 2.f, 0.f, 6.f));

		// 스킬 칸 + 퍽 3칸 (한 줄) [기획 UI 수정본]
		UHorizontalBox* Slots = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		AddLine(Col, Slots, FMargin(0.f));
		UTextBlock* NewSkillText = nullptr;
		SkillBox = NewSlot(EVHTextRole::Small, LOCTEXT("NoSkill", "스킬 —"), NewSkillText);
		SkillText = NewSkillText;
		// 두 줄(이름 + 상태)이 칸 안에 들어가게: 작은 글자, 줄 간격 좁게, 좌우 여백 줄임 [2026-11-17 사용자 지적: "Q 준비"·"유용한 도구"가 칸 밖으로 넘침]
		auto Compact = [&S](UBorder* Box, UTextBlock* Text)
		{
			FSlateFontInfo Font = Text->GetFont();
			Font.Size = S.HudSmallSlotFontSize;
			Text->SetFont(Font);
			Text->SetLineHeightPercentage(0.92f);
			Text->SetShadowOffset(FVector2D::ZeroVector);	// 그림자가 글자 옆에 회색으로 겹쳐 보이지 않게
			Box->SetPadding(FMargin(3.f, 2.f));
		};
		Compact(SkillBox, SkillText);
		AddCell(Slots, Sized(SkillBox, S.HudSkillSlotWidth, S.HudSmallSlotHeight));
		for (int32 i = 0; i < 3; ++i)
		{
			UTextBlock* Text = nullptr;
			UBorder* Box = NewSlot(EVHTextRole::Small, FText::GetEmpty(), Text);
			Compact(Box, Text);
			AddCell(Slots, Sized(Box, S.HudPerkSlotWidth, S.HudSmallSlotHeight), FMargin(0.f, 0.f, i < 2 ? 4.f : 0.f, 0.f));
			PerkBoxes.Add(Box);
			PerkTexts.Add(Text);
		}
	}
	Place(PlayerBox, FAnchors(0.f, 1.f), FVector2D(0.f, 1.f), FVector2D(M, -M));

	// ── 아래 가운데: 소비 아이템 · 무기1(지금 든 무기, 크게) · 무기2(작게) + 남은 탄 수 [기획 UI 수정본] ──
	WeaponPanel = NewPanel(TEXT("WeaponPanel"), FMargin(10.f));
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		WeaponPanel->SetContent(Row);
		UTextBlock* NewConsumableText = nullptr;
		ConsumableBox = NewSlot(EVHTextRole::Small, LOCTEXT("NoConsumable", "소비 —"), NewConsumableText);
		ConsumableText = NewConsumableText;
		{
			// 아이콘(구상안 Consumables_v1 옆모습) + "[E] 이름" + 아래쪽 발광 띠 색. 빈 칸이면 글자만
			// 윗줄 = [E] + 아이콘, 아랫줄 = 이름 ("1회용 제트팩"처럼 긴 이름도 칸 안에 들어가게 키 글자는 아이콘 옆으로)
			UVerticalBox* ConsumableCol = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
			UHorizontalBox* IconRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			ConsumableKey = NewText(EVHTextRole::Small, LOCTEXT("ConsumableKey", "[E]"));
			ConsumableKey->SetVisibility(ESlateVisibility::Collapsed);
			if (UHorizontalBoxSlot* KeySlot = IconRow->AddChildToHorizontalBox(ConsumableKey))
			{
				KeySlot->SetVerticalAlignment(VAlign_Center);
				KeySlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
			}
			ConsumableIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
			ConsumableIcon->SetVisibility(ESlateVisibility::Collapsed);
			if (UHorizontalBoxSlot* IconSlot = IconRow->AddChildToHorizontalBox(Sized(ConsumableIcon, S.HudConsumableIconSize, S.HudConsumableIconSize)))
			{
				IconSlot->SetVerticalAlignment(VAlign_Center);
			}
			AddLine(ConsumableCol, IconRow, FMargin(0.f))->SetHorizontalAlignment(HAlign_Center);
			AddLine(ConsumableCol, ConsumableText, FMargin(0.f))->SetHorizontalAlignment(HAlign_Center);
			ConsumableStripe = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			ConsumableStripe->SetVisibility(ESlateVisibility::Collapsed);
			AddLine(ConsumableCol, Sized(ConsumableStripe, S.HudConsumableSlotWidth - 8.f, 3.f), FMargin(0.f, 2.f, 0.f, 0.f))->SetHorizontalAlignment(HAlign_Center);
			ConsumableBox->SetContent(ConsumableCol);
		}
		AddCell(Row, Sized(ConsumableBox, S.HudConsumableSlotWidth + 16.f, 76.f));
		// 무기 아이콘 (DA_VHWeaponCatalog 의 Icon, 구상안 v2.2 옆모습. 가로:세로 = 2:1). 아이콘이 없는 무기는 글자만 보인다
		auto NewWeaponIcon = [this, &Sized](float Height) -> USizeBox*
		{
			UImage* Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
			Icon->SetVisibility(ESlateVisibility::Collapsed);
			WeaponIcons.Add(Icon);
			return Sized(Icon, Height * 2.f, Height);
		};
		// 무기1: 지금 손에 든 무기 (아이콘을 크게, 그 아래 이름)
		UTextBlock* MainText = nullptr;
		UBorder* MainBox = NewSlot(EVHTextRole::Small, FText::GetEmpty(), MainText);
		{
			UVerticalBox* MainCol = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
			AddLine(MainCol, NewWeaponIcon(S.HudWeaponIconHeight), FMargin(0.f))->SetHorizontalAlignment(HAlign_Center);
			AddLine(MainCol, MainText, FMargin(0.f))->SetHorizontalAlignment(HAlign_Center);
			MainBox->SetContent(MainCol);
		}
		AddCell(Row, Sized(MainBox, S.HudMainWeaponWidth, 76.f));
		WeaponBoxes.Add(MainBox);
		WeaponTexts.Add(MainText);
		// 무기2(휠로 바꿀 무기: 작은 아이콘 + 이름) + 그 아래 남은 탄 수(무기1의 탄만 표시)
		UVerticalBox* Side = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		AddCell(Row, Side, FMargin(0.f));
		UTextBlock* SubText = nullptr;
		UBorder* SubBox = NewSlot(EVHTextRole::Small, FText::GetEmpty(), SubText);
		{
			UHorizontalBox* SubRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			if (UHorizontalBoxSlot* IconSlot = SubRow->AddChildToHorizontalBox(NewWeaponIcon(S.HudSubWeaponIconHeight)))
			{
				IconSlot->SetVerticalAlignment(VAlign_Center);
				IconSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
			}
			if (UHorizontalBoxSlot* TextSlot = SubRow->AddChildToHorizontalBox(SubText))
			{
				TextSlot->SetVerticalAlignment(VAlign_Center);
			}
			SubBox->SetContent(SubRow);
		}
		AddLine(Side, Sized(SubBox, S.HudWeaponSlotWidth, 35.f), FMargin(0.f, 0.f, 0.f, 6.f));
		WeaponBoxes.Add(SubBox);
		WeaponTexts.Add(SubText);
		UTextBlock* NewAmmoText = nullptr;
		UBorder* AmmoBox = NewSlot(EVHTextRole::Small, FText::GetEmpty(), NewAmmoText);
		AmmoText = NewAmmoText;
		AddLine(Side, Sized(AmmoBox, S.HudWeaponSlotWidth, 35.f), FMargin(0.f));
	}
	Place(WeaponPanel, FAnchors(0.5f, 1.f), FVector2D(0.5f, 1.f), FVector2D(0.f, -M));

	// ── 알림 영역 (상단 가운데 아래) ──
	{
		UVerticalBox* NoticeList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("NoticeList"));
		for (int32 i = 0; i < NoticePoolSize; ++i)
		{
			UBorder* Box = NewPanel(NAME_None, FMargin(16.f, 6.f));
			UTextBlock* Text = NewText(EVHTextRole::Body, FText::GetEmpty());
			Text->SetJustification(ETextJustify::Center);
			Box->SetContent(Text);
			AddLine(NoticeList, Box, FMargin(0.f, 3.f))->SetHorizontalAlignment(HAlign_Center);
			Box->SetVisibility(ESlateVisibility::Collapsed);
			NoticeBoxes.Add(Box);
			NoticeTexts.Add(Text);
		}
		Place(NoticeList, FAnchors(0.5f, 0.2f), FVector2D(0.5f, 0.f), FVector2D::ZeroVector);
	}

	// ── 화면 가운데: 사망 화면 ──
	DeathBox = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DeathBox"));
	DeathBox->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.6f));
	DeathBox->SetPadding(FMargin(40.f, 20.f));
	{
		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		DeathBox->SetContent(Col);
		DeathTitle = NewText(EVHTextRole::Big, FText::GetEmpty());
		DeathTitle->SetColorAndOpacity(FSlateColor(S.Error));
		AddLine(Col, DeathTitle)->SetHorizontalAlignment(HAlign_Center);
		DeathReason = NewText(EVHTextRole::Heading, FText::GetEmpty());
		AddLine(Col, DeathReason, FMargin(0.f, 6.f, 0.f, 2.f))->SetHorizontalAlignment(HAlign_Center);
		DeathLoss = NewText(EVHTextRole::Heading, FText::GetEmpty());
		DeathLoss->SetColorAndOpacity(FSlateColor(S.Error));
		AddLine(Col, DeathLoss, FMargin(0.f, 8.f, 0.f, 2.f))->SetHorizontalAlignment(HAlign_Center);
		DeathCarried = NewText(EVHTextRole::Heading, FText::GetEmpty());
		AddLine(Col, DeathCarried, FMargin(0.f, 2.f))->SetHorizontalAlignment(HAlign_Center);
		DeathHint = NewText(EVHTextRole::Body, FText::GetEmpty());
		DeathHint->SetColorAndOpacity(FSlateColor(S.MinimapTreasure));
		AddLine(Col, DeathHint, FMargin(0.f, 6.f, 0.f, 0.f))->SetHorizontalAlignment(HAlign_Center);
	}
	Place(DeathBox, FAnchors(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FVector2D::ZeroVector);
	DeathBox->SetVisibility(ESlateVisibility::Collapsed);

	// ── 화면 가운데 전체 메시지 (시간 종료·금고 열림) ──
	BannerBox = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BannerBox"));
	BannerBox->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.55f));
	BannerBox->SetPadding(FMargin(40.f, 24.f));
	BannerText = NewText(EVHTextRole::Big, FText::GetEmpty());
	BannerText->SetJustification(ETextJustify::Center);
	BannerText->SetColorAndOpacity(FSlateColor(S.Success));
	BannerBox->SetContent(BannerText);
	Place(BannerBox, FAnchors(0.5f, 0.35f), FVector2D(0.5f, 0.5f), FVector2D::ZeroVector);
	BannerBox->SetVisibility(ESlateVisibility::Collapsed);

	// ── 대상 안내 ──
	PromptBox = NewPanel(TEXT("PromptBox"), FMargin(14.f, 8.f));
	PromptBox->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.78f));
	{
		UVerticalBox* PromptCol = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		PromptBox->SetContent(PromptCol);
		PromptText = NewText(EVHTextRole::Body, FText::GetEmpty());
		PromptText->SetJustification(ETextJustify::Center);
		AddLine(PromptCol, PromptText)->SetHorizontalAlignment(HAlign_Center);
		PromptBar = NewBar(NAME_None);
		PromptBar->SetFillColorAndOpacity(FLinearColor(1.f, 0.75f, 0.15f));
		AddLine(PromptCol, PromptBar, FMargin(0.f, 6.f, 0.f, 2.f));
	}
	UCanvasPanelSlot* PromptSlot = Canvas->AddChildToCanvas(PromptBox);
	PromptSlot->SetAutoSize(true);
	PromptSlot->SetAlignment(FVector2D(0.5f, 1.f));
	PromptBox->SetVisibility(ESlateVisibility::Collapsed);

	// ── 커서 정보창 (물품 위, 품질별 색) ──
	// 색을 그대로 내야 하므로(반투명 흰색 등) 패널 붓 대신 기본 붓(흰색)에 색만 입힌다
	InfoBox = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("InfoBox"));
	InfoBox->SetPadding(FMargin(12.f, 6.f));
	InfoText = NewText(EVHTextRole::Body, FText::GetEmpty());
	InfoText->SetJustification(ETextJustify::Center);
	{
		// [색 띠][아이콘][글자]: 소비 아이템은 HUD 소비 칸과 같은 아이콘·띠 색. 나머지는 글자만 (띠·아이콘 숨김)
		UHorizontalBox* InfoRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		InfoAccent = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		InfoAccent->SetVisibility(ESlateVisibility::Collapsed);
		if (UHorizontalBoxSlot* AccentSlot = InfoRow->AddChildToHorizontalBox(Sized(InfoAccent, S.InfoAccentWidth, 0.f)))
		{
			AccentSlot->SetVerticalAlignment(VAlign_Fill);	// 띠가 있을 때는 창 여백을 없애 왼쪽 가장자리에 붙인다 (UpdateInfo)
			AccentSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		}
		InfoIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		InfoIcon->SetVisibility(ESlateVisibility::Collapsed);
		if (UHorizontalBoxSlot* IconSlot = InfoRow->AddChildToHorizontalBox(Sized(InfoIcon, S.InfoIconSize, S.InfoIconSize)))
		{
			IconSlot->SetVerticalAlignment(VAlign_Center);
			IconSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
		}
		if (UHorizontalBoxSlot* TextSlot = InfoRow->AddChildToHorizontalBox(InfoText))
		{
			TextSlot->SetVerticalAlignment(VAlign_Center);
			TextSlot->SetPadding(FMargin(0.f, 6.f));
		}
		InfoBox->SetContent(InfoRow);
		InfoBox->SetPadding(FMargin(12.f, 0.f));
	}
	UCanvasPanelSlot* InfoSlot = Canvas->AddChildToCanvas(InfoBox);
	InfoSlot->SetAutoSize(true);
	InfoSlot->SetAlignment(FVector2D(0.5f, 1.f));
	InfoBox->SetVisibility(ESlateVisibility::Collapsed);

	// ── 오른쪽 아래: 조작 안내 + 개발용 상태 (좌하단 캐릭터 칸과 겹치지 않게) ──
	{
		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("HelpCol"));
		// 아래 가운데의 무기 칸과 겹치지 않게 두 줄로
		HelpText = NewText(EVHTextRole::Small, LOCTEXT("Help", "[WASD] 이동  [Shift] 달리기  [Space] 구르기  [F] 상호작용\n[좌클릭 꾹] 공격  [휠] 무기 교체  [Q] 스킬  [E] 소비 아이템"));
		HelpText->SetJustification(ETextJustify::Right);
		HelpText->SetColorAndOpacity(FSlateColor(S.SubText));
		HelpText->SetShadowOffset(FVector2D(1.f, 1.f));
		AddLine(Col, HelpText, FMargin(0.f))->SetHorizontalAlignment(HAlign_Right);
		DevText = NewText(EVHTextRole::Small, FText::GetEmpty());
		DevText->SetColorAndOpacity(FSlateColor(S.SubText));
		DevText->SetShadowOffset(FVector2D(1.f, 1.f));
		AddLine(Col, DevText, FMargin(0.f))->SetHorizontalAlignment(HAlign_Right);
		Place(Col, FAnchors(1.f, 1.f), FVector2D(1.f, 1.f), FVector2D(-M, -M));
	}

	// ── 섬광: 화면 전체를 덮는 흰색 (맨 위). 섬광탄에 맞으면 새하얗게 → 3초에 걸쳐 원래대로 [기획 I-33] ──
	FlashOverlay = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FlashOverlay"));
	FlashOverlay->SetBrushColor(FLinearColor(1.f, 1.f, 1.f, 0.f));
	if (UCanvasPanelSlot* FlashSlot = Canvas->AddChildToCanvas(FlashOverlay))
	{
		FlashSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		FlashSlot->SetOffsets(FMargin(0.f));
		FlashSlot->SetZOrder(100);
	}
	FlashOverlay->SetVisibility(ESlateVisibility::Collapsed);
}

void UVHGameHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	{
		const UVHServerGameSubsystem* Game = GetWorld() ? GetWorld()->GetSubsystem<UVHServerGameSubsystem>() : nullptr;
		const float Flash = Game && Game->IsActive() ? Game->GetFlashAlpha() : 0.f;
		FlashOverlay->SetVisibility(Flash > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		FlashOverlay->SetBrushColor(FLinearColor(1.f, 1.f, 1.f, Flash));
	}
	{
		// 시험 확인용: 5초마다 미니맵에 무엇을 그렸는지 로그로 남긴다
		static double LastMinimapLog = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (Minimap && Now - LastMinimapLog > 5.0)
		{
			LastMinimapLog = Now;
			const UVHMinimapWidget::FDrawStats& M = Minimap->GetLastStats();
			UE_LOG(LogMCPTest, Log, TEXT("[미니맵] 방 %d, 반납지점 %d, 경비원 %d, 다른 플레이어 %d, 보물 방향 %d, 섬광으로 숨김 %d, 떨어뜨린 내 보물 %d, 문 %d(잠김 %d, 열림 %d)"),
				M.Rooms, M.ReturnPoints, M.Guards, M.Thieves, M.bTreasureArrow ? 1 : 0, M.bHiddenByFlash ? 1 : 0, M.bMyTreasure ? 1 : 0, M.Doors, M.LockedDoors, M.OpenDoors);
		}
	}
	UpdatePrompt();
	UpdateInfo();
	UpdateIdleBars();
	UpdateTopRight();
	UpdateCarry();
	UpdatePlayerPanel();
	UpdateNotices();
	UpdateCenter();
	UpdateDevLine();
	UpdateWorldLabels();
}

int32 UVHGameHUDWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	// 리스폰 뒤 몇 초 동안 떨어뜨린 보물 쪽을 가리킨다 (보물이 사라지면 Game 이 0을 돌려줘 바로 사라짐)
	const UWorld* World = GetWorld();
	const UVHServerGameSubsystem* Game = World ? World->GetSubsystem<UVHServerGameSubsystem>() : nullptr;
	const float Remaining = Game ? Game->GetDroppedTreasureHintRemaining() : 0.f;
	APlayerController* PC = GetOwningPlayer();
	const APawn* Pawn = GetOwningPlayerPawn();
	FVector TreasurePos;
	int32 Value = 0;
	if (Remaining <= 0.f || !PC || !Pawn || !Game->GetMyDroppedTreasure(TreasurePos, Value))
	{
		return LayerId;
	}
	// 멀리 있는 보물은 카메라 뒤쪽 평면에 있어 화면 좌표가 안 나올 수 있다 → 방향은 내 캐릭터에서 보물 쪽으로 3m 떨어진 점을 투영해 얻는다
	const FVector MyLoc = Pawn->GetActorLocation();
	const FVector Toward = (TreasurePos - MyLoc).GetSafeNormal2D();
	FVector2D Me, Ahead, Tr(-10000.0, -10000.0);
	if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, MyLoc, Me, false)
		|| !UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, MyLoc + Toward * 300.f, Ahead, false))
	{
		return LayerId;
	}
	const bool bProjected = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, TreasurePos + FVector(0.f, 0.f, 20.f), Tr, false);
	const UVHUIStyle& S = UVHSettings::Get().GetStyle();
	const float Alpha = FMath::Clamp(Remaining / 0.6f, 0.f, 1.f);		// 마지막 0.6초 동안 흐려지며 사라짐
	const float Pulse = 0.85f + 0.15f * FMath::Sin(static_cast<float>(FPlatformTime::Seconds()) * 6.f);
	FLinearColor Color = S.MinimapTreasure;
	Color.A = Alpha;
	const FLinearColor Shadow(0.f, 0.f, 0.f, 0.55f * Alpha);
	const FVector2D View = AllottedGeometry.GetLocalSize();
	const float K = FMath::Max(S.DroppedTreasureHintScale, 0.5f);	// 화살표·글자 크기 배율 (DA_VHUIStyle)
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", FMath::RoundToInt(15.f * K));
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	auto Lines = [&](const TArray<FVector2f>& Points, const FLinearColor& C, float Thickness, int32 Layer)
	{
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, C, true, Thickness);
	};
	auto Label = [&](const FString& Text, const FVector2D& Center, int32 Layer)
	{
		const FVector2D Size = Measure->Measure(Text, Font);
		const FVector2D TopLeft = Center - Size * 0.5;
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(FVector2f(Size + FVector2D(14.0, 6.0) * K), FSlateLayoutTransform(FVector2f(TopLeft - FVector2D(7.0, 3.0) * K))),
			FCoreStyle::Get().GetBrush("GenericWhiteBox"), ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.6f * Alpha));
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, AllottedGeometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(TopLeft))),
			Text, Font, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, Alpha));
	};
	auto Diamond = [&](const FVector2D& C, float H, int32 Layer)
	{
		TArray<FVector2f> P = { FVector2f(C + FVector2D(0, -H)), FVector2f(C + FVector2D(H, 0)), FVector2f(C + FVector2D(0, H)), FVector2f(C + FVector2D(-H, 0)), FVector2f(C + FVector2D(0, -H)) };
		Lines(P, Shadow, 7.f * K, Layer);
		Lines(P, Color, 3.5f * K, Layer + 1);
	};

	const float Meters = FVector::Dist2D(Pawn->GetActorLocation(), TreasurePos) / 100.f;
	const FString Text = FString::Printf(TEXT("내 보물 %s · %.0fm"), *VHRanking::Money(Value), Meters);
	const bool bOnScreen = bProjected && Tr.X > 20.0 && Tr.Y > 20.0 && Tr.X < View.X - 20.0 && Tr.Y < View.Y - 20.0;
	const float Radius = S.DroppedTreasureHintRadius * K;	// 화살이 길어진 만큼 몸에서 더 띄운다
	const FVector2D To = bOnScreen ? Tr - Me : (Ahead - Me).GetSafeNormal() * 10000.0;

	// 보물이 화면 안이면 그 위에 ◇
	if (bOnScreen)
	{
		Diamond(Tr, 16.f * K * Pulse, LayerId + 1);
	}
	// 내 캐릭터 둘레의 화살표 + 글자 (보물이 바로 옆이면 ◇만)
	if (To.Size() > Radius + 30.0 * K)
	{
		const FVector2D Dir = To.GetSafeNormal();
		const FVector2D Side(-Dir.Y, Dir.X);
		const FVector2D Tip = Me + Dir * Radius * Pulse;
		const FVector2D Back = Tip - Dir * 80.0 * K;
		TArray<FVector2f> Shaft = { FVector2f(Back), FVector2f(Tip) };
		TArray<FVector2f> Head = { FVector2f(Tip - Dir * 28.0 * K + Side * 22.0 * K), FVector2f(Tip), FVector2f(Tip - Dir * 28.0 * K - Side * 22.0 * K) };
		Lines(Shaft, Shadow, 14.f * K, LayerId + 1);
		Lines(Head, Shadow, 14.f * K, LayerId + 1);
		Lines(Shaft, Color, 8.f * K, LayerId + 2);
		Lines(Head, Color, 8.f * K, LayerId + 2);
		// 글자는 화살 끝 바깥쪽에 (위·아래로 향하면 더 띄워 화살과 겹치지 않게)
		Label(Text, Tip + Dir * (44.0 + FMath::Abs(Dir.Y) * 14.0) * K, LayerId + 3);
	}
	else if (bOnScreen)
	{
		Label(Text, Tr + FVector2D(0.0, -38.0) * K, LayerId + 3);
	}
	return LayerId + 5;
}

// ───────────────────────── 우상단: 시간·내 반납·반납지점·순위표 ─────────────────────────

void UVHGameHUDWidget::UpdateTopRight()
{
	const UVHMatchSubsystem* Match = GetWorld() ? GetWorld()->GetSubsystem<UVHMatchSubsystem>() : nullptr;
	if (!Match || !Match->IsStarted())
	{
		TopRightBox->SetVisibility(ESlateVisibility::Collapsed);
		TimeBox->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	TopRightBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	TimeBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	const UVHUIStyle& S = Style();

	// 위 가운데: 남은 시간 mm:ss (1분 이하 빨강)
	const float RemainF = Match->GetRemainingSeconds();
	const int32 Remain = FMath::CeilToInt(RemainF);
	TimeText->SetText(FText::FromString(FString::Printf(TEXT("%02d:%02d"), Remain / 60, Remain % 60)));
	TimeText->SetColorAndOpacity(FSlateColor(RemainF <= S.HudTimeWarningSeconds ? S.Error : S.Text));

	// 우상단: 순위표 "1위  닉네임  반납 금액" (전원. 내 줄은 강조색, 같은 금액은 공동 순위) [기획 UI 수정본]
	// 반납지점의 위치·남은 시간은 미니맵에, 내 반납 금액은 순위표의 내 줄에 있다
	const TArray<FVHRankEntry> Ranking = Match->GetRanking();
	for (int32 i = 0; i < RankLines.Num(); ++i)
	{
		UTextBlock* Line = RankLines[i];
		if (!Ranking.IsValidIndex(i))
		{
			Line->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		const FVHRankEntry& E = Ranking[i];
		Line->SetText(FText::FromString(FString::Printf(TEXT("%s위  %s  %s"), *VHRanking::RankLabel(E.Rank, E.bTied), *E.Name, *VHRanking::Money(E.Value))));
		Line->SetColorAndOpacity(FSlateColor(E.bIsMe ? S.Accent : S.Text));
		Line->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

// ───────────────────────── 우측 가운데: 들고 있는 가치 ─────────────────────────

void UVHGameHUDWidget::UpdateCarry()
{
	const APawn* Pawn = GetOwningPlayerPawn();
	const UVHTreasureCarrierComponent* Carrier = Pawn ? Pawn->FindComponentByClass<UVHTreasureCarrierComponent>() : nullptr;
	if (!Carrier)
	{
		CarryBox->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	const UVHUIStyle& S = Style();
	const int32 Value = Carrier->GetTotalValue();
	CarryBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	CarryText->SetText(Carrier->GetCount() > 0
		? FText::Format(LOCTEXT("CarryValue", "들고 있는 가치  {0}\n보물 {1}개"), MoneyText(Value), Carrier->GetCount())
		: FText::Format(LOCTEXT("CarryNone", "들고 있는 가치  {0}"), MoneyText(0)));
	CarryText->SetColorAndOpacity(FSlateColor(Value > 0 ? S.Accent : S.Text));
	CarryBox->SetRenderOpacity(Value > 0 ? 1.f : S.HudCarryDimOpacity);
}

// ───────────────────────── 좌하단: 내 캐릭터 ─────────────────────────

void UVHGameHUDWidget::UpdatePlayerPanel()
{
	const APawn* Pawn = GetOwningPlayerPawn();
	const UVHThiefStateComponent* State = Pawn ? Pawn->FindComponentByClass<UVHThiefStateComponent>() : nullptr;
	if (!Pawn)
	{
		PlayerBox->SetVisibility(ESlateVisibility::Collapsed);
		WeaponPanel->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	PlayerBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	WeaponPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
	const UVHUIStyle& S = Style();

	// 닉네임 · 캐릭터 이미지 (방에서 고른 캐릭터. 방 없이 혼자 플레이면 미선택)
	NameText->SetText(FText::FromString(State ? UVHInteractionSubsystem::GetThiefLabel(State) : Pawn->GetName()));
	NameText->SetColorAndOpacity(FSlateColor(S.Accent));
	int32 CharacterId = 0;
	if (const UVHNetSubsystem* Net = UVHNetSubsystem::Get(this))
	{
		if (const FVHRoomMember* MyMember = Net->GetMyMember())
		{
			CharacterId = MyMember->CharacterId;
		}
	}
	if (CharacterId != ShownCharacterId)
	{
		ShownCharacterId = CharacterId;
		const UVHGameData& Data = UVHSettings::Get().GetGameData();
		const FVHCharacterDef* Def = Data.FindCharacter(CharacterId);
		UTexture2D* Portrait = Def ? Def->Portrait.LoadSynchronous() : nullptr;
		if (Portrait)
		{
			PortraitImage->SetBrushFromTexture(Portrait, false);
			PortraitImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			PortraitImage->SetVisibility(ESlateVisibility::Collapsed);
		}
		PortraitText->SetText(Data.CharacterName(CharacterId));
	}

	// HP (빨강) + 보호막 (겹침, 0이면 안 보임)
	const UVHHealthComponent* Health = Pawn->FindComponentByClass<UVHHealthComponent>();
	const bool bDead = State && !State->IsInGame();
	if (Health)
	{
		const float MaxHP = FMath::Max(1.f, Health->GetMaxHP());
		HpBar->SetPercent(Health->GetHPRatio());
		const float Shield = Health->GetShield();
		ShieldBar->SetPercent(FMath::Clamp(Shield / MaxHP, 0.f, 1.f));
		ShieldBar->SetVisibility(Shield > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		HpText->SetText(FText::FromString(Shield > 0.f
			? FString::Printf(TEXT("HP %d / %d  (+보호막 %d)"), FMath::CeilToInt(Health->GetHP()), FMath::RoundToInt(MaxHP), FMath::CeilToInt(Shield))
			: FString::Printf(TEXT("HP %d / %d"), FMath::CeilToInt(Health->GetHP()), FMath::RoundToInt(MaxHP))));
	}
	else
	{
		HpBar->SetPercent(0.f);
		ShieldBar->SetVisibility(ESlateVisibility::Collapsed);
		HpText->SetText(FText::GetEmpty());
	}

	// SP (파랑, 탈진이면 회색)
	if (const UVHActionComponent* Action = Pawn->FindComponentByClass<UVHActionComponent>())
	{
		const bool bExhausted = Action->IsExhausted();
		SpBar->SetPercent(Action->GetMaxStamina() > 0.f ? Action->GetStamina() / Action->GetMaxStamina() : 0.f);
		SpBar->SetFillColorAndOpacity(bExhausted ? S.HudSpExhaustedColor : S.HudSpColor);
		SpText->SetText(FText::FromString(FString::Printf(TEXT("SP %d / %d%s"), FMath::FloorToInt(Action->GetStamina()),
			FMath::RoundToInt(Action->GetMaxStamina()), bExhausted ? TEXT("  (탈진)") : TEXT(""))));
	}

	// 무기1·무기2: 이름 + 남은 횟수(영구는 ∞), 지금 든 무기 강조
	// 서버가 알려 준 내 상태(SC_PLAYER_STATUS)의 무기 2칸을 그대로 보여 준다 (탄 0 = 무제한). 서버 값이 없으면 빈 칸
	const UVHServerGameSubsystem* ServerGame = GetWorld() ? GetWorld()->GetSubsystem<UVHServerGameSubsystem>() : nullptr;
	const bool bServerStatus = ServerGame && ServerGame->IsActive() && ServerGame->GetMyStatus().bValid;
	// 스킬 칸(Q)·소비 아이템 칸(E): 서버가 알려 준 내 상태 그대로. 쿨타임은 받은 시각부터 화면에서 줄여 보인다
	if (bServerStatus)
	{
		const FVHServerPlayerStatus& St = ServerGame->GetMyStatus();
		const float Elapsed = static_cast<float>(FPlatformTime::Seconds() - St.ReceivedSeconds);
		const bool bHasSkill = St.Skill != 0;
		const float Cooldown = FMath::Max(0.f, St.SkillCooldownMs / 1000.f - Elapsed);
		const float Active = FMath::Max(0.f, St.SkillActiveMs / 1000.f - Elapsed);
		const FString SkillName = UVHServerGameSubsystem::SkillName(static_cast<RobProto::SkillId>(St.Skill));
		// 사제 테이저건을 손에 든 동안: "좌클릭 발사" 안내 (다시 Q = 넣기, 쿨타임 없음)
		const bool bTaserHeld = ServerGame->IsSkillTaserHeld();
		SkillText->SetText(FText::FromString(!bHasSkill ? FString(TEXT("스킬 —"))
			: (bTaserHeld ? FString::Printf(TEXT("%s\n좌클릭 발사"), *SkillName)
			: (Active > 0.f ? FString::Printf(TEXT("%s\n%d초"), *SkillName, FMath::CeilToInt(Active))
				: (Cooldown > 0.f ? FString::Printf(TEXT("%s\n%d초"), *SkillName, FMath::CeilToInt(Cooldown)) : FString::Printf(TEXT("%s\nQ 준비"), *SkillName))))));
		// 쿨타임 숫자는 읽혀야 한다 [기획 UI 메모: 스킬을 쓰면 아이콘 위에 쿨타임 숫자] → 칸은 어둡게, 글자는 밝은 회색. 쓰는 중·준비됨은 칸을 강조
		const bool bCooling = bHasSkill && Cooldown > 0.f && Active <= 0.f;
		SkillText->SetColorAndOpacity(FSlateColor(!bHasSkill || bCooling ? S.HudEmptySlotText : ((Active > 0.f || bTaserHeld) ? S.Accent : S.Text)));
		SkillBox->SetBrushColor(bHasSkill && !bCooling ? S.ButtonSelected : S.HudEmptySlotColor);
		const bool bHasConsumable = St.Consumable != 0;
		// 구상안 Consumables_v1: 아이콘 + 발광 띠 색 (바닥 모델·정보창과 같은 디자인). 아이콘이 없으면 예전처럼 두 줄 글자
		const FVHConsumableVisual* ConsumableVisual = UVHSettings::Get().GetWeaponCatalog().FindConsumable(St.Consumable);
		if (LastConsumableIcon != St.Consumable)
		{
			LastConsumableIcon = St.Consumable;
			UTexture2D* Icon = ConsumableVisual ? ConsumableVisual->Icon.LoadSynchronous() : nullptr;
			if (Icon)
			{
				ConsumableIcon->SetBrushFromTexture(Icon, false);
			}
			ConsumableIcon->SetVisibility(Icon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			ConsumableStripe->SetVisibility(ConsumableVisual ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			if (ConsumableVisual)
			{
				ConsumableStripe->SetBrushColor(ConsumableVisual->BandColor);
			}
		}
		const bool bIcon = ConsumableIcon->GetVisibility() != ESlateVisibility::Collapsed;
		ConsumableKey->SetVisibility(bIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		const FString ConsumableName = UVHServerGameSubsystem::ConsumableName(static_cast<RobProto::ConsumableId>(St.Consumable));
		ConsumableText->SetText(FText::FromString(!bHasConsumable ? FString(TEXT("소비 —"))
			: (bIcon ? ConsumableName : FString::Printf(TEXT("[E]\n%s"), *ConsumableName))));
		ConsumableText->SetColorAndOpacity(FSlateColor(bHasConsumable ? S.Text : S.HudEmptySlotText));
		ConsumableBox->SetBrushColor(bHasConsumable ? S.ButtonSelected : S.HudEmptySlotColor);
	}
	// 무기 칸 [기획 UI 수정본]: 큰 칸(WeaponBoxes[0]) = 지금 손에 든 무기, 작은 칸([1]) = 휠로 바꿀 무기, 그 아래 = 든 무기의 남은 탄.
	// 서버는 무기 2칸과 "지금 든 칸"을 알려 준다(SC_PLAYER_STATUS). 탄 0 = 무제한(삼단봉·도검)
	{
		int32 Held = 0;
		uint8 Ids[2] = { 0, 0 };
		int32 Ammo[2] = { 0, 0 };
		if (bServerStatus)
		{
			const FVHServerPlayerStatus& St = ServerGame->GetMyStatus();
			Held = FMath::Clamp(St.ActiveSlot, 0, 1);
			Ids[0] = St.WeaponId[Held];
			Ids[1] = St.WeaponId[Held ^ 1];
			Ammo[0] = St.WeaponAmmo[Held];
			Ammo[1] = St.WeaponAmmo[Held ^ 1];
		}
		const bool bHasMain = Ids[0] != 0;
		const bool bHasSub = Ids[1] != 0;
		// 아이콘: 무기가 바뀐 칸만 다시 입힌다. 표에 아이콘이 없는 무기는 글자만
		bool bHasIcon[2] = { false, false };
		for (int32 i = 0; i < 2 && i < WeaponIcons.Num(); ++i)
		{
			if (LastIconWeapon[i] != Ids[i])
			{
				LastIconWeapon[i] = Ids[i];
				const FVHWeaponVisual* Visual = UVHSettings::Get().GetWeaponCatalog().Find(Ids[i]);
				UTexture2D* Icon = Visual ? Visual->Icon.LoadSynchronous() : nullptr;
				if (Icon)
				{
					WeaponIcons[i]->SetBrushFromTexture(Icon, false);
				}
				WeaponIcons[i]->SetVisibility(Icon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			}
			bHasIcon[i] = WeaponIcons[i]->GetVisibility() != ESlateVisibility::Collapsed;
		}
		WeaponTexts[0]->SetText(FText::FromString(bHasMain ? UVHServerGameSubsystem::WeaponName(static_cast<RobProto::WeaponId>(Ids[0])) : TEXT("무기 —")));
		WeaponTexts[0]->SetColorAndOpacity(FSlateColor(bHasMain ? S.Text : S.HudEmptySlotText));
		WeaponBoxes[0]->SetBrushColor(bHasMain ? S.ButtonSelected : S.HudEmptySlotColor);
		// 아이콘이 있으면 칸이 좁아 이름만, 없으면 예전처럼 "[휠] 이름"
		const FString SubName = bHasSub ? FString(UVHServerGameSubsystem::WeaponName(static_cast<RobProto::WeaponId>(Ids[1]))) : FString(TEXT("무기2 —"));
		WeaponTexts[1]->SetText(FText::FromString(bHasSub && !bHasIcon[1] ? FString::Printf(TEXT("[휠] %s"), *SubName) : SubName));
		WeaponTexts[1]->SetColorAndOpacity(FSlateColor(bHasSub ? S.Text : S.HudEmptySlotText));
		AmmoText->SetText(FText::FromString(!bHasMain ? FString(TEXT("탄 —")) : (Ammo[0] == 0 ? FString(TEXT("탄 ∞")) : FString::Printf(TEXT("탄 %d"), Ammo[0]))));
		AmmoText->SetColorAndOpacity(FSlateColor(bHasMain ? S.Text : S.HudEmptySlotText));
	}
	WeaponPanel->SetRenderOpacity(bDead ? 0.6f : 1.f);

	// 상태이상: HP 바 위(닉네임 옆)에 이름만. 지속 시간은 표시하지 않는다 [기획 UI 메모]
	{
		TArray<FString> Tags;
		const uint32 Bits = ServerGame && ServerGame->IsActive() ? ServerGame->GetSelfState() : 0;
		const UVHStatusEffectComponent* Effects = Pawn->FindComponentByClass<UVHStatusEffectComponent>();
		const UVHActionComponent* ActionForStatus = Pawn->FindComponentByClass<UVHActionComponent>();
		if ((Bits & RobProto::ES_STUNNED) || (Effects && Effects->IsStunned())) { Tags.Add(TEXT("경직")); }
		if (Bits & RobProto::ES_SLOWED) { Tags.Add(TEXT("감속")); }
		if (Bits & RobProto::ES_CLOAKED) { Tags.Add(TEXT("은신")); }
		if ((Bits & RobProto::ES_EXHAUSTED) || (ActionForStatus && ActionForStatus->IsExhausted())) { Tags.Add(TEXT("탈진")); }
		if ((Bits & RobProto::ES_INVULNERABLE) || (Effects && Effects->IsTimedInvulnerable())) { Tags.Add(TEXT("무적")); }
		if (Bits & RobProto::ES_IN_SMOKE) { Tags.Add(TEXT("연막")); }
		if ((Bits & RobProto::ES_FLASHED) || (ServerGame && ServerGame->IsFlashed())) { Tags.Add(TEXT("섬광")); }
		if (Bits & RobProto::ES_RAILGUN_LOCK) { Tags.Add(TEXT("레일건 발사")); }
		if (Bits & RobProto::ES_JETPACK) { Tags.Add(TEXT("제트팩")); }
		StatusText->SetText(FText::FromString(FString::Join(Tags, TEXT(" · "))));
		StatusText->SetColorAndOpacity(FSlateColor(S.Error));
	}

	// 퍽 칸: 방에서 고른 퍽 (최대 3개). 방탄조끼 = 남은 보호막, 기사회생 = 남았는지 (리스폰하면 다시 채워진다)
	const UVHNetSubsystem* PerkNet = UVHNetSubsystem::Get(this);
	const FVHRoomMember* PerkMember = PerkNet ? PerkNet->GetMyMember() : nullptr;
	const UVHGameData& PerkData = UVHSettings::Get().GetGameData();
	for (int32 i = 0; i < PerkBoxes.Num(); ++i)
	{
		const bool bHas = PerkMember && PerkMember->Perks.IsValidIndex(i);
		const int32 PerkId = bHas ? PerkMember->Perks[i] : 0;
		FString Line = bHas ? PerkData.PerkName(PerkId).ToString() : FString(TEXT("퍽 —"));
		bool bSpent = false;
		if (bHas && bServerStatus)
		{
			const FVHServerPlayerStatus& St = ServerGame->GetMyStatus();
			if (PerkId == VHPerk::Vest)
			{
				bSpent = St.Shield <= 0;
				Line += bSpent ? FString(TEXT("\n소진")) : FString::Printf(TEXT("\n보호막 %d"), St.Shield);
			}
			else if (PerkId == VHPerk::DeathSave)
			{
				bSpent = !St.bDeathSaveLeft;
				Line += bSpent ? TEXT("\n사용됨") : TEXT("\n준비");
			}
		}
		PerkTexts[i]->SetText(FText::FromString(Line));
		// 다 쓴 퍽(소진·사용됨)은 칸을 어둡게 하고 글자는 밝은 회색 (어두운 회색 글자는 회색 칸 위에서 겹쳐 보여 안 읽힌다 — 2026-11-17 실서버 화면)
		PerkTexts[i]->SetColorAndOpacity(FSlateColor(!bHas || bSpent ? S.HudEmptySlotText : S.Text));
		PerkBoxes[i]->SetBrushColor(bHas && !bSpent ? S.ButtonSelected : S.HudEmptySlotColor);
	}
	PlayerBox->SetRenderOpacity(bDead ? 0.6f : 1.f);
}

// ───────────────────────── 알림 영역 ─────────────────────────

void UVHGameHUDWidget::UpdateNotices()
{
	const UWorld* World = GetWorld();
	const UVHMatchSubsystem* Match = World ? World->GetSubsystem<UVHMatchSubsystem>() : nullptr;
	const UVHInteractionSubsystem* Sub = World ? World->GetSubsystem<UVHInteractionSubsystem>() : nullptr;
	const UVHUIStyle& S = Style();
	const int32 MaxShown = FMath::Clamp(S.HudNoticeCount, 0, NoticeBoxes.Num());
	const float Life = FMath::Max(0.1f, S.HudNoticeSeconds);

	// 최근 알림부터 거꾸로 모은 뒤, 오래된 것이 위로 오게 보여준다
	TArray<const FVHMatchNotice*> Shown;
	if (Match && Sub)
	{
		const double Now = Sub->GetNow();
		const TArray<FVHMatchNotice>& Notices = Match->GetNotices();
		for (int32 i = Notices.Num() - 1; i >= 0 && Shown.Num() < MaxShown; --i)
		{
			if (Now - Notices[i].Time < Life)
			{
				Shown.Insert(&Notices[i], 0);
			}
		}
		for (int32 i = 0; i < NoticeBoxes.Num(); ++i)
		{
			if (!Shown.IsValidIndex(i))
			{
				NoticeBoxes[i]->SetVisibility(ESlateVisibility::Collapsed);
				continue;
			}
			const float Age = static_cast<float>(Now - Shown[i]->Time);
			NoticeTexts[i]->SetText(Shown[i]->Text);
			NoticeTexts[i]->SetColorAndOpacity(FSlateColor(S.Text));
			NoticeBoxes[i]->SetRenderOpacity(FMath::Clamp((Life - Age) / 0.5f, 0.f, 1.f));	// 마지막 0.5초 동안 흐려짐
			NoticeBoxes[i]->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		return;
	}
	for (UBorder* Box : NoticeBoxes)
	{
		Box->SetVisibility(ESlateVisibility::Collapsed);
	}
}

// ───────────────────────── 화면 가운데: 사망 화면·배너 ─────────────────────────

static TAutoConsoleVariable<int32> CVarOldDeathPanel(TEXT("vh.OldDeathPanel"), 0,
	TEXT("개발용: 1이면 쓰러짐 창을 고치기 전 문구(반납 금액 -$X 한 줄)로 보여 준다 (비교 화면용)"));

void UVHGameHUDWidget::UpdateCenter()
{
	const UVHMatchSubsystem* Match = GetWorld() ? GetWorld()->GetSubsystem<UVHMatchSubsystem>() : nullptr;
	// 전체 화면 메시지 (시간 종료·금고 열림)
	if (Match && Match->IsBannerVisible())
	{
		BannerText->SetText(Match->GetBannerText());
		BannerBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		BannerBox->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 사망 — 리스폰까지 n초 + 사유 + 반납 금액 손실
	const APawn* Pawn = GetOwningPlayerPawn();
	const UVHThiefStateComponent* State = Pawn ? Pawn->FindComponentByClass<UVHThiefStateComponent>() : nullptr;
	const bool bDead = State && !State->IsInGame();
	if (!bDead || !Match)
	{
		DeathBox->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	DeathBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	DeathTitle->SetText(FText::Format(LOCTEXT("DeathTitle", "사망 — 리스폰까지 {0}초"), FMath::CeilToInt(Match->GetRespawnRemaining(State))));
	const FVHThiefMatchStats* St = Match->FindStats(State);
	FText Reason = LOCTEXT("DeathDown", "쓰러짐");
	if (St && !St->LastKillerName.IsEmpty())
	{
		Reason = FText::Format(LOCTEXT("DeathByPlayer", "{0}에게 처치됨"), FText::FromString(St->LastKillerName));
	}
	else if (St && St->bLastDeathByGuard)
	{
		Reason = LOCTEXT("DeathByGuard", "경비원에게 당함");
	}
	DeathReason->SetText(Reason);
	// 개발용(vh.OldDeathPanel 1): 고치기 전 문구 (비교 화면용)
	if (CVarOldDeathPanel.GetValueOnGameThread() != 0)
	{
		DeathLoss->SetText(FText::Format(LOCTEXT("DeathLossOld", "반납 금액  -{0}"), MoneyText(St ? St->LastDeathLoss : 0)));
		DeathLoss->SetColorAndOpacity(FSlateColor(UVHSettings::Get().GetStyle().Error));
		DeathCarried->SetText(FText::GetEmpty());
		DeathHint->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	// 반납한 돈 손실 / 들고 있던 것을 나눠 보여 준다 (서버 규칙: 반납한 돈의 10%·40%를 잃고, 들고 있던 보물은 어느 경우든 쓰러진 자리에 떨어진다.
	// 보물 말고 환금 아이템은 플레이어에게 쓰러지면 비싼 것부터 최대 5개 떨어지고(나머지 사라짐, 2026-11-27) 경비원에게 쓰러지면 사라진다) [서버 WorldCombat Down·WorldItems DropValuables]
	const bool bGuard = St && St->bLastDeathByGuard;
	const UVHGameplayConfig& G = UVHSettings::Get().GetGameplayConfig();
	if (!St || St->LastBankedBefore <= 0)
	{
		DeathLoss->SetText(LOCTEXT("DeathNoBank", "반납한 돈이 없어 잃은 금액 없음"));
		DeathLoss->SetColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f, 1.f)));
	}
	else
	{
		DeathLoss->SetText(FText::Format(LOCTEXT("DeathLossBank", "반납한 돈 손실: -{0} (반납한 돈의 {1}%)"), MoneyText(St->LastDeathLoss),
			bGuard ? G.DownLossPctByGuard : G.DownLossPctByPlayer));
		DeathLoss->SetColorAndOpacity(FSlateColor(UVHSettings::Get().GetStyle().Error));
	}
	const int32 TreasureValue = St ? St->LastDropTreasureValue : 0;
	const int32 Count = St ? St->LastDropValuableCount : 0;
	const FText Treasure = MoneyText(TreasureValue);
	FText Carried;
	if (TreasureValue <= 0 && Count <= 0)
	{
		Carried = LOCTEXT("DeathCarriedNone", "들고 있던 것: 없음");
	}
	else if (bGuard)
	{
		Carried = TreasureValue > 0 && Count > 0
			? FText::Format(LOCTEXT("DeathGuardBoth", "들고 있던 것: 보물 {0}은 쓰러진 자리에 떨어졌고, 환금 아이템 {1}개는 사라졌습니다"), Treasure, Count)
			: TreasureValue > 0
			? FText::Format(LOCTEXT("DeathGuardTreasure", "들고 있던 것: 보물 {0}은 쓰러진 자리에 떨어졌습니다"), Treasure)
			: FText::Format(LOCTEXT("DeathGuardValuables", "들고 있던 것: 환금 아이템 {0}개는 사라졌습니다"), Count);
	}
	else if (Count > G.DownDropMaxValuables)
	{
		// 2026-11-27 팀 서버 16244a7: 플레이어에게 쓰러지면 일반 환금 아이템은 비싼 것부터 최대 5개만 떨어지고 나머지는 사라짐
		const FText Kept = FText::AsNumber(G.DownDropMaxValuables);
		const FText Lost = FText::AsNumber(Count - G.DownDropMaxValuables);
		Carried = TreasureValue > 0
			? FText::Format(LOCTEXT("DeathPlayerBothCap", "들고 있던 것: 보물 {0} + 환금 아이템 {1}개 중 비싼 {2}개를 쓰러진 자리에 떨어뜨렸고, 나머지 {3}개는 사라졌습니다"), Treasure, Count, Kept, Lost)
			: FText::Format(LOCTEXT("DeathPlayerValuablesCap", "들고 있던 것: 환금 아이템 {0}개 중 비싼 {1}개를 쓰러진 자리에 떨어뜨렸고, 나머지 {2}개는 사라졌습니다"), Count, Kept, Lost);
	}
	else
	{
		Carried = TreasureValue > 0 && Count > 0
			? FText::Format(LOCTEXT("DeathPlayerBoth", "들고 있던 것: 보물 {0} + 환금 아이템 {1}개를 쓰러진 자리에 떨어뜨렸습니다"), Treasure, Count)
			: TreasureValue > 0
			? FText::Format(LOCTEXT("DeathPlayerTreasure", "들고 있던 것: 보물 {0}을 쓰러진 자리에 떨어뜨렸습니다"), Treasure)
			: FText::Format(LOCTEXT("DeathPlayerValuables", "들고 있던 것: 환금 아이템 {0}개를 쓰러진 자리에 떨어뜨렸습니다"), Count);
	}
	DeathCarried->SetText(Carried);
	// 떨어진 보물이 아직 바닥에 있으면 위치를 어디서 보는지 알려 준다
	FVector DroppedPos;
	int32 DroppedValue = 0;
	const UVHServerGameSubsystem* Game = GetWorld()->GetSubsystem<UVHServerGameSubsystem>();
	const bool bDropped = Game && Game->GetMyDroppedTreasure(DroppedPos, DroppedValue);
	DeathHint->SetText(bDropped ? LOCTEXT("DeathHint", "떨어진 보물 위치는 미니맵에 보라색 ◇로 표시됩니다 (다른 플레이어가 주우면 사라짐)")
		: (TreasureValue > 0 ? LOCTEXT("DeathHintGone", "떨어뜨린 보물을 이미 누군가 주웠습니다") : FText::GetEmpty()));
	DeathHint->SetVisibility(DeathHint->GetText().IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

// ───────────────────────── 오른쪽 아래: 개발용 상태 한 줄 ─────────────────────────

void UVHGameHUDWidget::UpdateDevLine()
{
	const APawn* Pawn = GetOwningPlayerPawn();
	if (!Pawn)
	{
		DevText->SetText(FText::GetEmpty());
		return;
	}
	TArray<FString> Parts;
	const UVHFlashlightComponent* Light = Pawn->FindComponentByClass<UVHFlashlightComponent>();
	if (Light && UVHSettings::Get().GetInteractionConfig().bUseFlashlight)	// 손전등을 끈 설정(D3)이면 표시하지 않는다
	{
		Parts.Add(Light->IsOn() ? TEXT("손전등 켜짐 [R]") : TEXT("손전등 꺼짐 [R]"));
	}
	if (const UVHStatusEffectComponent* Effects = Pawn->FindComponentByClass<UVHStatusEffectComponent>())
	{
		if (Effects->IsStunned())
		{
			Parts.Add(FString::Printf(TEXT("경직 %.1f초"), Effects->GetStunRemaining()));
		}
	}
	const FVector Loc = Pawn->GetActorLocation();
	const AVHRoomVolume* Room = AVHRoomVolume::FindRoomAt(GetWorld(), Loc);
	Parts.Add(FString::Printf(TEXT("위치 %s (%s, %s)"), Room ? *Room->GetRoomLabel() : TEXT("방 밖"), *Meters(Loc.X).ToString(), *Meters(Loc.Y).ToString()));
	DevText->SetText(FText::FromString(FString::Join(Parts, TEXT("  ·  "))));
}

// ───────────────────────── 월드 위 글자 ─────────────────────────

void UVHGameHUDWidget::UpdateWorldLabels()
{
	const UWorld* World = GetWorld();
	const UVHMatchSubsystem* Match = World ? World->GetSubsystem<UVHMatchSubsystem>() : nullptr;
	const UVHInteractionSubsystem* Sub = World ? World->GetSubsystem<UVHInteractionSubsystem>() : nullptr;
	const UVHGameplayConfig& G = UVHSettings::Get().GetGameplayConfig();
	const UVHUIStyle& S = Style();
	const APawn* MyPawn = GetOwningPlayerPawn();
	int32 Used = 0;
	auto Show = [this, &Used](const FVector& WorldLocation, const FString& Text, const FLinearColor& Color)
	{
		if (Used >= WorldLabels.Num())
		{
			return;
		}
		UTextBlock* Label = WorldLabels[Used];
		if (!PlaceAt(Label, WorldLocation))
		{
			return;
		}
		Label->SetText(FText::FromString(Text));
		Label->SetColorAndOpacity(FSlateColor(Color));
		Label->SetVisibility(ESlateVisibility::HitTestInvisible);
		++Used;
	};

	if (Match)
	{
		// 반납지점 위: 사라지기(이동)까지 남은 시간. 빨간 판 위에 빨간 글자는 안 읽혀서 빨간 상태는 밝은 글자 + "곧 이동"
		for (const AVHReturnPoint* Point : Match->GetReturnPoints())
		{
			const bool bRed = Point->IsRed();
			const int32 Sec = FMath::CeilToInt(Point->GetRemainingSeconds());
			Show(Point->GetActorLocation() + FVector(0.f, 0.f, 40.f),
				bRed ? FString::Printf(TEXT("곧 이동 %d초"), Sec) : FString::Printf(TEXT("반납 %d초"), Sec),
				bRed ? S.Text : G.ReturnPointGreen);
		}
	}

	// 내 의적 머리 위 닉네임(+리스폰 무적). 다른 의적·경비원은 표시 액터(AVHNetProxy)가 이름표를 직접 그린다
	if (Sub)
	{
		for (const UVHThiefStateComponent* Thief : Sub->GetThieves())
		{
			const AActor* Owner = Thief->GetOwner();
			if (!Owner || Owner->IsHidden() || !Thief->IsInGame())
			{
				continue;
			}
			const bool bMine = Owner == MyPawn;
			FString Line = UVHInteractionSubsystem::GetThiefLabel(Thief);
			if (const UVHStatusEffectComponent* Effects = Owner->FindComponentByClass<UVHStatusEffectComponent>(); Effects && Effects->IsTimedInvulnerable())
			{
				Line += FString::Printf(TEXT("\n무적 %d초"), FMath::CeilToInt(Effects->GetTimedInvulnerableRemaining()));
			}
			Show(Owner->GetActorLocation() + FVector(0.f, 0.f, Owner->GetSimpleCollisionHalfHeight() + 45.f), Line, bMine ? S.Accent : S.Text);
		}
	}

	for (int32 i = Used; i < WorldLabels.Num(); ++i)
	{
		WorldLabels[i]->SetVisibility(ESlateVisibility::Collapsed);
	}
}

bool UVHGameHUDWidget::PlaceAt(UWidget* Widget, const FVector& WorldLocation) const
{
	APlayerController* PC = GetOwningPlayer();
	FVector2D ScreenPos;
	if (!PC || !UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, WorldLocation, ScreenPos, false))
	{
		return false;
	}
	if (Widget == PromptBox)
	{
		// 큰 오브젝트에 가까이 서면 안내 위치가 화면 밖으로 나가므로 화면 안으로 붙잡는다
		const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), 0.01f);
		const FVector2D View = UWidgetLayoutLibrary::GetViewportSize(this) / Scale;
		const FVector2D BoxSize = PromptBox->GetDesiredSize();
		ScreenPos.X = FMath::Clamp(ScreenPos.X, BoxSize.X * 0.5f + 10.f, FMath::Max(BoxSize.X * 0.5f + 10.f, View.X - BoxSize.X * 0.5f - 10.f));
		ScreenPos.Y = FMath::Clamp(ScreenPos.Y, BoxSize.Y + 60.f, FMath::Max(BoxSize.Y + 60.f, View.Y - 20.f));
	}
	if (Widget == PromptBox || Widget == InfoBox)
	{
		// 조준점과 겹치면 안내를 조준점 위로(위가 모자라면 아래로) 옮긴다 → 조준점과 [F] 안내 둘 다 읽힌다 (2026-11-25)
		FVector2D Aim;
		float AimR = 0.f;
		const UVHCrosshairWidget* Crosshair = UVHCrosshairWidget::GetActive();
		if (Crosshair && Crosshair->GetOwningPlayer() == PC && Crosshair->GetScreenBounds(Aim, AimR))
		{
			const FVector2D BoxSize = Widget->GetDesiredSize();	// 아래 가운데 기준 (정렬 0.5, 1)
			const float Gap = 6.f;
			const bool bOverlapX = FMath::Abs(ScreenPos.X - Aim.X) < BoxSize.X * 0.5f + AimR;
			const bool bOverlapY = ScreenPos.Y > Aim.Y - AimR && ScreenPos.Y - BoxSize.Y < Aim.Y + AimR;
			if (bOverlapX && bOverlapY)
			{
				const float Above = Aim.Y - AimR - Gap;
				ScreenPos.Y = Above - BoxSize.Y >= 50.f ? Above : Aim.Y + AimR + Gap + BoxSize.Y;
			}
		}
	}
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot))
	{
		CanvasSlot->SetPosition(ScreenPos);
	}
	return true;
}

// ───────────────────────── 상호작용 안내·진행 바 (기존 유지) ─────────────────────────

void UVHGameHUDWidget::UpdatePrompt()
{
	const APawn* Pawn = GetOwningPlayerPawn();
	const UVHInteractionComponent* Interaction = Pawn ? Pawn->FindComponentByClass<UVHInteractionComponent>() : nullptr;
	const UWorld* World = GetWorld();
	// 쓰러져 있는 동안은 안내를 숨긴다 (옆에 떨어진 보물에 "지금은 할 수 없습니다"가 떠서 쓰러짐 창을 가렸다)
	const UVHThiefStateComponent* ThiefState = Pawn ? Pawn->FindComponentByClass<UVHThiefStateComponent>() : nullptr;
	if (!Interaction || !World || (ThiefState && !ThiefState->IsInGame()))
	{
		PromptBox->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	const UVHUIStyle& S = Style();
	const double Now = World->GetTimeSeconds();

	AVHInteractableBase* Target = Interaction->IsInteracting() ? Interaction->GetActiveTarget() : Interaction->GetFocusTarget();
	const FVHInteractOffer& Offer = Interaction->GetFocusOffer();
	// 문 [2026-11-24]: F 대상 물품이 없거나 지금 할 수 없으면 앞의 문 안내 ("[F] 문 열기" / "[F] 문 닫기" / 잠긴 문)
	if (!Interaction->IsInteracting() && !Interaction->IsApproaching() && (!Target || !Offer.IsAvailable()))
	{
		const UVHDoorSubsystem* Doors = World->GetSubsystem<UVHDoorSubsystem>();
		if (const AVHDoor* Door = Doors ? Doors->FindDoorFor(Pawn) : nullptr)
		{
			if (PlaceAt(PromptBox, Door->GetPromptLocation(Pawn->GetActorLocation())))
			{
				PromptBox->SetVisibility(ESlateVisibility::HitTestInvisible);
				const bool bLockedDoor = Door->bLocked;
				// 문간에 서서 열린 문을 닫을 수는 없다 → 테두리 빛과 같이 회색 안내 [2026-11-25]
				const bool bInDoorway = !bLockedDoor && !UVHDoorSubsystem::CanToggleNow(*Door, Pawn);
				const FText DoorLine = bLockedDoor ? LOCTEXT("DoorLocked", "잠긴 문")
					: bInDoorway ? LOCTEXT("DoorInDoorway", "문간에서는 문을 닫을 수 없음")
					: FText::Format(LOCTEXT("DoorPrompt", "{0} {1}"), FText::FromString(KeyLabel),
						Door->IsOpen() ? LOCTEXT("DoorClose", "문 닫기") : LOCTEXT("DoorOpen", "문 열기"));
				PromptText->SetText(DoorLine);
				PromptText->SetColorAndOpacity(FSlateColor(bLockedDoor || bInDoorway ? S.DisabledText : S.Text));
				PromptBar->SetVisibility(ESlateVisibility::Collapsed);
				return;
			}
		}
	}
	if (!Target || !PlaceAt(PromptBox, Target->GetPromptLocation()))
	{
		PromptBox->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	PromptBox->SetVisibility(ESlateVisibility::HitTestInvisible);

	FText Line;
	FLinearColor Color = S.Text;
	float Progress = -1.f;

	if (Interaction->IsInteracting())
	{
		Progress = Interaction->GetActiveProgress();
		const float Remaining = Interaction->GetActiveRemainingSeconds();
		Line = Interaction->GetActiveMode() == EVHInteractMode::Hold
			? FText::Format(LOCTEXT("Holding", "{0} 중...  {1}초"), Interaction->GetActiveActionText(), Seconds(Remaining, 1))
			: FText::Format(LOCTEXT("Doing", "{0} 중..."), Interaction->GetActiveActionText());
	}
	else if (!Offer.IsAvailable())
	{
		Line = Offer.BlockText;
		Color = S.DisabledText;
	}
	else if (Offer.Mode == EVHInteractMode::Hold && UVHSettings::Get().GetInteractionConfig().bPressToInteract)
	{
		// F 한 번 누르기 방식: "[F] 상자 열기 (3초)" / 진행도가 남은 금고는 "(남은 4초)"
		const float Saved = Target->GetHoldProgress().GetProgress(Now);
		if (Saved > 0.f)
		{
			Progress = Saved;
			Line = FText::Format(LOCTEXT("PressResume", "{0} {1} (남은 {2}초)"),
				FText::FromString(KeyLabel), Offer.ActionText, SecondsAuto((1.f - Saved) * Offer.DurationSeconds));
		}
		else
		{
			Line = FText::Format(LOCTEXT("PressPrompt", "{0} {1} ({2}초)"),
				FText::FromString(KeyLabel), Offer.ActionText, SecondsAuto(Offer.DurationSeconds));
		}
	}
	else if (Offer.Mode == EVHInteractMode::Hold)
	{
		const FVHHoldProgress& Hold = Target->GetHoldProgress();
		const float Saved = Hold.GetProgress(Now);
		if (Saved > 0.f)
		{
			Progress = Saved;
			Line = FText::Format(LOCTEXT("HoldResume", "{0} 길게 눌러 {1} (남은 {2}초)"),
				FText::FromString(KeyLabel), Offer.ActionText, SecondsAuto((1.f - Saved) * Offer.DurationSeconds));
		}
		else
		{
			Line = FText::Format(LOCTEXT("HoldPrompt", "{0} 길게 눌러 {1} ({2}초)"),
				FText::FromString(KeyLabel), Offer.ActionText, SecondsAuto(Offer.DurationSeconds));
		}
	}
	else
	{
		Line = FText::Format(LOCTEXT("TapPrompt", "{0} {1}"), FText::FromString(KeyLabel), Offer.ActionText);
	}

	PromptText->SetText(Line);
	PromptText->SetColorAndOpacity(FSlateColor(Color));
	if (Progress >= 0.f)
	{
		PromptBar->SetPercent(Progress);
		PromptBar->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		PromptBar->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UVHGameHUDWidget::UpdateInfo()
{
	const APawn* Pawn = GetOwningPlayerPawn();
	const UVHInteractionComponent* Interaction = Pawn ? Pawn->FindComponentByClass<UVHInteractionComponent>() : nullptr;
	const AVHInteractableBase* Hover = Interaction ? Interaction->GetHoverTarget() : nullptr;
	const FVHInfoCard Card = Hover ? Hover->GetInfoCard() : FVHInfoCard();
	if (!Hover || !Card.IsValid() || !PlaceAt(InfoBox, Hover->GetPromptLocation()))
	{
		InfoBox->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	// 같은 물품에 [F] 안내가 떠 있으면 그 위에 쌓는다
	const AVHInteractableBase* Prompted = Interaction->IsInteracting() ? Interaction->GetActiveTarget() : Interaction->GetFocusTarget();
	if (Prompted == Hover && PromptBox->GetVisibility() != ESlateVisibility::Collapsed)
	{
		if (UCanvasPanelSlot* InfoSlot = Cast<UCanvasPanelSlot>(InfoBox->Slot))
		{
			const UCanvasPanelSlot* PromptSlot = Cast<UCanvasPanelSlot>(PromptBox->Slot);
			const FVector2D PromptPos = PromptSlot ? PromptSlot->GetPosition() : InfoSlot->GetPosition();
			InfoSlot->SetPosition(FVector2D(PromptPos.X, PromptPos.Y - PromptBox->GetDesiredSize().Y - 6.f));
		}
	}
	const UVHUIStyle& S = Style();
	FLinearColor Back = S.InfoCommonColor;
	FLinearColor Fore = S.InfoTextOnLight;
	switch (Card.Tone)
	{
	case EVHInfoTone::Advanced:	Back = S.InfoAdvancedColor; Fore = S.InfoTextOnColor; break;
	case EVHInfoTone::Rare:		Back = S.InfoRareColor; Fore = S.InfoTextOnColor; break;
	case EVHInfoTone::Unique:	Back = S.InfoUniqueColor; Fore = S.InfoTextOnColor; break;
	default: break;
	}
	InfoBox->SetBrushColor(Back);
	InfoText->SetText(Card.Text);
	InfoText->SetColorAndOpacity(FSlateColor(Fore));
	// 소비 아이템: 왼쪽 색 띠 + 아이콘 (HUD 소비 칸과 같은 디자인)
	const bool bAccent = Card.Accent.A > 0.01f;
	InfoAccent->SetVisibility(bAccent ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (bAccent)
	{
		InfoAccent->SetBrushColor(FLinearColor(Card.Accent.R, Card.Accent.G, Card.Accent.B, 1.f));
	}
	InfoBox->SetPadding(FMargin(bAccent ? 0.f : 12.f, 0.f, 12.f, 0.f));
	if (Card.Icon)
	{
		if (InfoIcon->GetBrush().GetResourceObject() != Card.Icon)
		{
			InfoIcon->SetBrushFromTexture(Card.Icon, false);
		}
		InfoIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		InfoIcon->SetVisibility(ESlateVisibility::Collapsed);
	}
	InfoBox->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UVHGameHUDWidget::UpdateIdleBars()
{
	const UWorld* World = GetWorld();
	const UVHInteractionSubsystem* Sub = World ? World->GetSubsystem<UVHInteractionSubsystem>() : nullptr;
	const APawn* Pawn = GetOwningPlayerPawn();
	const UVHInteractionComponent* Interaction = Pawn ? Pawn->FindComponentByClass<UVHInteractionComponent>() : nullptr;
	const AVHInteractableBase* Shown = Interaction
		? (Interaction->IsInteracting() ? Interaction->GetActiveTarget() : Interaction->GetFocusTarget())
		: nullptr;

	int32 Used = 0;
	if (Sub)
	{
		const double Now = Sub->GetNow();
		for (AVHInteractableBase* Target : Sub->GetInteractables())
		{
			if (Used >= IdleBars.Num())
			{
				break;
			}
			if (Target == Shown)
			{
				continue;
			}
			// 진행도가 남았거나(아무도 안 씀) 다른 의적이 여는 중인 오브젝트 (금고 위 남이 연 진행도)
			const FVHHoldProgress& Hold = Target->GetHoldProgress();
			const bool bOtherRunning = Hold.bRunning && Target->GetActiveUser() && Target->GetActiveUser() != Interaction;
			if (!Target->ShouldShowIdleProgress() && !bOtherRunning)
			{
				continue;
			}
			UProgressBar* Bar = IdleBars[Used];
			if (PlaceAt(Bar, Target->GetPromptLocation()))
			{
				Bar->SetPercent(Hold.GetProgress(Now));
				Bar->SetVisibility(ESlateVisibility::HitTestInvisible);
				++Used;
			}
		}
	}
	for (int32 i = Used; i < IdleBars.Num(); ++i)
	{
		IdleBars[i]->SetVisibility(ESlateVisibility::Collapsed);
	}
}

#undef LOCTEXT_NAMESPACE
