#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Fonts/SlateFontInfo.h"
#include "VHUIStyle.generated.h"

class UTextBlock;
class UButton;
class UBorder;
class UEditableTextBox;

UENUM(BlueprintType)
enum class EVHTextRole : uint8
{
	Title,		// 화면 제목
	Heading,	// 구역 제목
	Body,		// 일반 글
	Small,		// 안내·상태 문구
	Button,		// 버튼 글자
	Big			// 결과창 승리/패배
};

UENUM(BlueprintType)
enum class EVHButtonKind : uint8
{
	Normal,
	Primary,	// 강조(시작하기, 확인 등)
	Selected	// 선택된 항목(캐릭터·퍽·방 목록)
};

/**
 * UI 스타일 값 모음 (색·글자 크기·여백).
 * UI 디자인이 아직 미정이므로 값을 여기 한곳에 모아 두고, 나중에 DA_VHUIStyle 에셋에서 바꾼다.
 * 위젯들은 화면이 만들어질 때(디자이너 미리보기 포함) 이 값을 자동으로 적용한다.
 */
UCLASS(BlueprintType)
class UVHUIStyle : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UVHUIStyle();

	/** 글꼴. 비워 두면 엔진 기본 글꼴(한글 대체 글꼴 포함) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Font", meta = (AllowedClasses = "/Script/Engine.Font"))
	TObjectPtr<UObject> FontFamily;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Font") int32 TitleSize = 54;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Font") int32 HeadingSize = 26;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Font") int32 BodySize = 18;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Font") int32 SmallSize = 15;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Font") int32 ButtonSize = 20;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Font") int32 BigSize = 40;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color") FLinearColor Background;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color") FLinearColor Panel;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color") FLinearColor PanelLight;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color") FLinearColor Accent;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color") FLinearColor Text;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color") FLinearColor SubText;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color") FLinearColor Error;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color") FLinearColor Success;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color") FLinearColor ButtonNormal;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color") FLinearColor ButtonHovered;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color") FLinearColor ButtonPressed;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color") FLinearColor ButtonDisabled;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color") FLinearColor ButtonPrimary;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color") FLinearColor ButtonSelected;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color") FLinearColor DisabledText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout") float CornerRadius = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout") FMargin ButtonPadding = FMargin(18.f, 10.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout") FMargin PanelPadding = FMargin(20.f);

	// ── 게임 HUD [PvP 규칙 v1 14장] (임시 디자인. 위치는 16:9 기준 화면 가장자리에서의 여백) ──
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") FLinearColor HudPanel;
	/** HP 바 (빨강) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") FLinearColor HudHpColor;
	/** 보호막 바 (HP 바 위에 겹침, 흰/하늘색) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") FLinearColor HudShieldColor;
	/** SP 바 (파랑) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") FLinearColor HudSpColor;
	/** 탈진 중 SP 바 (회색) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") FLinearColor HudSpExhaustedColor;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") FLinearColor HudBarBackground;
	/** 캐릭터 이미지가 없을 때 칸 색 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") FLinearColor HudPortraitColor;
	/** 빈 칸(스킬 없음 등) 색 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") FLinearColor HudEmptySlotColor;
	/** 빈 칸 안 글자 색 ("스킬 —", "무기2 빈 칸"). 빈 칸 배경 위에서도 읽히는 밝은 회색 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") FLinearColor HudEmptySlotText;
	// ── 커서 정보창 [기획 2026-11: 품질별 반투명 창. 일반 흰색 · 고급 초록 · 희귀 파랑 · 보물 보라] ──
	/** 일반 품질 + 품질이 없는 것(무기·소비 아이템·이름만 나오는 금고·상자·필드 아이템) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Info") FLinearColor InfoCommonColor = FLinearColor(1.f, 1.f, 1.f, 0.78f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Info") FLinearColor InfoAdvancedColor = FLinearColor(0.08f, 0.62f, 0.16f, 0.78f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Info") FLinearColor InfoRareColor = FLinearColor(0.08f, 0.3f, 0.9f, 0.78f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Info") FLinearColor InfoUniqueColor = FLinearColor(0.5f, 0.16f, 0.8f, 0.8f);
	/** 흰색 창 위 글자 / 색 있는 창 위 글자 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Info") FLinearColor InfoTextOnLight = FLinearColor(0.03f, 0.03f, 0.04f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Info") FLinearColor InfoTextOnColor = FLinearColor(1.f, 1.f, 1.f, 1.f);
	/** 소비 아이템 칸 너비 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") float HudConsumableSlotWidth = 104.f;
	/** 소비 아이템 칸의 아이콘 크기 (정사각형, 구상안 Consumables_v1 옆모습) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") float HudConsumableIconSize = 44.f;
	/** 정보창(커서를 올린 물품 위)의 아이콘 크기와 왼쪽 색 띠 두께 (소비 아이템) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Info") float InfoIconSize = 36.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Info") float InfoAccentWidth = 5.f;
	/** 스킬 칸 너비 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") float HudSkillSlotWidth = 84.f;
	/** 스킬·퍽 칸의 높이와 글자 크기 (두 줄: 이름 + 상태. 칸 밖으로 넘치지 않게 작은 글자) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") float HudSmallSlotHeight = 50.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") int32 HudSmallSlotFontSize = 12;
	/** 토스트(가운데 위 알림) 글자를 이 너비에서 줄바꿈 (0 = 줄바꿈 안 함). 자동 줄바꿈 대신 고정 너비라 문장 중간에서 이상하게 끊기지 않는다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout") float ToastWrapWidth = 1100.f;
	/** 화면 가장자리 여백 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") float HudMargin = 24.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") float HudBarWidth = 280.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") float HudBarHeight = 16.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") float HudPortraitSize = 96.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") float HudWeaponSlotWidth = 136.f;
	/** 퍽 칸 하나의 폭 (칸 3개, 스킬 칸 옆) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") float HudPerkSlotWidth = 96.f;
	/** 지금 든 무기(무기1) 칸의 폭 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") float HudMainWeaponWidth = 176.f;
	/** 무기1 칸의 아이콘 높이 (가로는 2배) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") float HudWeaponIconHeight = 52.f;
	/** 무기2 칸의 작은 아이콘 높이 (가로는 2배) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") float HudSubWeaponIconHeight = 27.f;

	// ── 미니맵 (좌상단) ──
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Minimap") float HudMinimapWidth = 300.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Minimap") float HudMinimapHeight = 200.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Minimap") FLinearColor MinimapBack = FLinearColor(0.02f, 0.02f, 0.03f, 0.75f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Minimap") FLinearColor MinimapRoom = FLinearColor(0.45f, 0.47f, 0.52f, 0.85f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Minimap") FLinearColor MinimapMe = FLinearColor(1.f, 0.85f, 0.2f, 1.f);
	/** 로봇탐지 퍽: 경비원 위치 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Minimap") FLinearColor MinimapGuard = FLinearColor(1.f, 0.2f, 0.15f, 1.f);
	/** 생체감지 퍽: 다른 플레이어 위치 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Minimap") FLinearColor MinimapThief = FLinearColor(0.3f, 0.75f, 1.f, 1.f);
	/** 보물탐색 퍽: 누군가 보물을 얻은 쪽 방향 화살표 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Minimap") FLinearColor MinimapTreasure = FLinearColor(0.8f, 0.4f, 1.f, 1.f);
	/** 보물탐색 방향 표시가 남아 있는 시간 [제안] */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Minimap") float MinimapTreasureSeconds = 8.f;
	/** 내가 쓰러지며 떨어뜨린 보물: 미니맵 ◇ 표시 크기(반지름 px). 보물이 바닥에 있는 동안 계속 보인다 (누가 주우면 사라짐) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Minimap") float MinimapDroppedTreasureSize = 7.f;
	/** 리스폰 뒤 화면에 떨어뜨린 보물 쪽 방향 표시를 띄우는 시간(초) [제안] */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") float DroppedTreasureHintSeconds = 8.f;
	/** 방향 표시 화살표가 내 캐릭터에서 떨어진 거리(px) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") float DroppedTreasureHintRadius = 120.f;
	/** 리스폰 뒤 "내 보물 $X · Nm" 화살표·글자 크기 배율 (1 = 예전 크기. 1080p 에서 잘 보이게 1.6) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD", meta = (ClampMin = 0.5)) float DroppedTreasureHintScale = 1.6f;
	/** 남은 시간이 이 이하면 빨간 글자 (1분) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") float HudTimeWarningSeconds = 60.f;
	/** 알림 영역: 최근 몇 개를 보여줄지 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") int32 HudNoticeCount = 3;
	/** 알림 영역: 몇 초 뒤 사라지는지 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") float HudNoticeSeconds = 5.f;
	/** 들고 있는 가치가 0이면 이 불투명도로 흐리게 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD") float HudCarryDimOpacity = 0.4f;

	// ── 적용 도우미 ──
	FSlateFontInfo MakeFont(EVHTextRole Role) const;
	int32 FontSize(EVHTextRole Role) const;
	void ApplyText(UTextBlock* Text, EVHTextRole Role) const;
	void ApplyText(UTextBlock* Text, EVHTextRole Role, const FLinearColor& Color) const;
	void ApplyButton(UButton* Button, EVHButtonKind Kind = EVHButtonKind::Normal) const;
	void ApplyPanel(UBorder* Border, bool bLight = false) const;
	void ApplyBackground(UBorder* Border) const;
	void ApplyTextBox(UEditableTextBox* Box) const;
};
