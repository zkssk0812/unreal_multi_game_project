#include "UI/VHUIStyle.h"

#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/EditableTextBox.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace
{
	FLinearColor Hex(const TCHAR* InHex)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(InHex));
	}
}

UVHUIStyle::UVHUIStyle()
{
	// 어두운 저택 분위기의 기본 팔레트 (임시)
	Background		= Hex(TEXT("12141A"));
	Panel			= Hex(TEXT("1C2029"));
	PanelLight		= Hex(TEXT("272C38"));
	Accent			= Hex(TEXT("E2B33C"));
	Text			= Hex(TEXT("F2F2F2"));
	SubText			= Hex(TEXT("A3ABBA"));
	Error			= Hex(TEXT("FF6B6B"));
	Success			= Hex(TEXT("6BD68A"));
	ButtonNormal	= Hex(TEXT("2E3544"));
	ButtonHovered	= Hex(TEXT("3D4659"));
	ButtonPressed	= Hex(TEXT("232936"));
	ButtonDisabled	= Hex(TEXT("1F232C"));
	ButtonPrimary	= Hex(TEXT("9C7420"));
	ButtonSelected	= Hex(TEXT("6E5A24"));
	DisabledText	= Hex(TEXT("5C6270"));

	// 게임 HUD (임시)
	HudPanel			= FLinearColor(0.02f, 0.02f, 0.03f, 0.7f);
	HudHpColor			= Hex(TEXT("E04040"));
	HudShieldColor		= FLinearColor(0.75f, 0.92f, 1.f, 0.85f);
	HudSpColor			= Hex(TEXT("3C8CFF"));
	HudSpExhaustedColor	= Hex(TEXT("7A7F8A"));
	HudBarBackground	= FLinearColor(0.f, 0.f, 0.f, 0.55f);
	HudPortraitColor	= Hex(TEXT("3A4152"));
	HudEmptySlotColor	= FLinearColor(0.2f, 0.21f, 0.24f, 0.8f);
	HudEmptySlotText	= Hex(TEXT("C9CFDA"));
}

int32 UVHUIStyle::FontSize(EVHTextRole Role) const
{
	switch (Role)
	{
	case EVHTextRole::Title:	return TitleSize;
	case EVHTextRole::Heading:	return HeadingSize;
	case EVHTextRole::Body:		return BodySize;
	case EVHTextRole::Small:	return SmallSize;
	case EVHTextRole::Button:	return ButtonSize;
	case EVHTextRole::Big:		return BigSize;
	}
	return BodySize;
}

FSlateFontInfo UVHUIStyle::MakeFont(EVHTextRole Role) const
{
	const bool bBold = Role == EVHTextRole::Title || Role == EVHTextRole::Heading || Role == EVHTextRole::Big || Role == EVHTextRole::Button;
	if (FontFamily)
	{
		return FSlateFontInfo(FontFamily, FontSize(Role), bBold ? FName(TEXT("Bold")) : FName(TEXT("Regular")));
	}
	// 엔진 기본 글꼴. 한글 등은 기본 대체 글꼴로 그려진다.
	return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), FontSize(Role));
}

void UVHUIStyle::ApplyText(UTextBlock* InText, EVHTextRole Role) const
{
	FLinearColor Color = Text;
	if (Role == EVHTextRole::Small) Color = SubText;
	if (Role == EVHTextRole::Title) Color = Accent;
	ApplyText(InText, Role, Color);
}

void UVHUIStyle::ApplyText(UTextBlock* InText, EVHTextRole Role, const FLinearColor& Color) const
{
	if (!InText)
	{
		return;
	}
	InText->SetFont(MakeFont(Role));
	InText->SetColorAndOpacity(FSlateColor(Color));
}

void UVHUIStyle::ApplyButton(UButton* Button, EVHButtonKind Kind) const
{
	if (!Button)
	{
		return;
	}
	const FLinearColor Base = Kind == EVHButtonKind::Primary ? ButtonPrimary
		: (Kind == EVHButtonKind::Selected ? ButtonSelected : ButtonNormal);
	const FLinearColor Hover = Kind == EVHButtonKind::Normal ? ButtonHovered : Base * 1.25f;

	FButtonStyle Style = Button->GetStyle();
	Style.SetNormal(FSlateRoundedBoxBrush(Base, CornerRadius));
	Style.SetHovered(FSlateRoundedBoxBrush(Hover, CornerRadius, Accent, 1.f));
	Style.SetPressed(FSlateRoundedBoxBrush(ButtonPressed, CornerRadius));
	Style.SetDisabled(FSlateRoundedBoxBrush(ButtonDisabled, CornerRadius));
	Style.SetNormalPadding(ButtonPadding);
	Style.SetPressedPadding(ButtonPadding);
	Button->SetStyle(Style);
	Button->SetBackgroundColor(FLinearColor::White);
}

void UVHUIStyle::ApplyPanel(UBorder* Border, bool bLight) const
{
	if (!Border)
	{
		return;
	}
	Border->SetBrush(FSlateRoundedBoxBrush(bLight ? PanelLight : Panel, CornerRadius));
	Border->SetBrushColor(FLinearColor::White);
}

void UVHUIStyle::ApplyBackground(UBorder* Border) const
{
	if (!Border)
	{
		return;
	}
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::Box;
	Brush.TintColor = FSlateColor(Background);
	Border->SetBrush(Brush);
	Border->SetBrushColor(FLinearColor::White);
}

void UVHUIStyle::ApplyTextBox(UEditableTextBox* Box) const
{
	if (!Box)
	{
		return;
	}
	FEditableTextBoxStyle Style = Box->GetWidgetStyle();
	Style.SetFont(MakeFont(EVHTextRole::Body));
	Style.SetBackgroundImageNormal(FSlateRoundedBoxBrush(PanelLight, CornerRadius, SubText, 1.f));
	Style.SetBackgroundImageHovered(FSlateRoundedBoxBrush(PanelLight, CornerRadius, Accent, 1.f));
	Style.SetBackgroundImageFocused(FSlateRoundedBoxBrush(PanelLight, CornerRadius, Accent, 2.f));
	Style.SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(ButtonDisabled, CornerRadius));
	Style.SetForegroundColor(FSlateColor(Text));
	Style.SetFocusedForegroundColor(FSlateColor(Text));
	Style.SetPadding(FMargin(10.f, 8.f));
	Box->SetWidgetStyle(Style);
	// 주의(엔진 동작): UEditableTextBox::SetWidgetStyle은 내부 복사본이 아니라 "넘겨준 인자"의 주소를
	// Slate 위젯에 넘긴다(EditableTextBox.cpp). 위의 Style은 지역 변수라 함수가 끝나면 사라지고,
	// Slate가 사라진 메모리의 글꼴을 읽다가 크래시가 난다(2026-10-16 로그인 화면 크래시 원인).
	// 그래서 위젯 내부에 복사된 WidgetStyle을 다시 넘겨 Slate가 그 주소를 가리키게 한다.
	Box->SetWidgetStyle(Box->GetWidgetStyle());
}
