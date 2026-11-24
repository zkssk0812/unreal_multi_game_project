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

	// ── 게임 중 마우스 조준점 (2026-11-25, 가시성 우선). 숫자는 1080p 기준 px — 다른 해상도에서는 화면 대비 같은 크기(UI 배율)로 그려진다.
	//    무기별 모양·사거리·반동 세기는 DA_VHWeaponCatalog → Weapons → Crosshair ──

	/** 끄면 예전처럼 운영체제 십자 커서를 쓴다 (콘솔 vh.Crosshair 0/1) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair") bool bCrosshairEnabled = true;
	/** 조준점 기본 지름 (px). 무기별 SizeScale 를 곱한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair", meta = (ClampMin = 24)) float CrosshairSize = 60.f;
	/** 조준점 최소 지름 (px). 어떤 모양·배율이라도 이보다 작게 그리지 않는다 (가시성 기준 48px) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair", meta = (ClampMin = 16)) float CrosshairMinSize = 48.f;
	/** 밝은 본체 선 굵기 (px, 가시성 기준 3 이상) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair", meta = (ClampMin = 1)) float CrosshairThickness = 4.f;
	/** 어두운 테두리 두께 (px, 본체 양쪽에 이만큼 더 그린다. 0 = 테두리 없음) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair", meta = (ClampMin = 0)) float CrosshairOutline = 2.f;
	/** 밝은 본체 색 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair") FLinearColor CrosshairColor = FLinearColor(1.f, 1.f, 1.f, 1.f);
	/** 어두운 테두리 색 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair") FLinearColor CrosshairOutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.9f);
	/** 전체 투명도 (1 = 불투명) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair", meta = (ClampMin = 0.2, ClampMax = 1)) float CrosshairOpacity = 1.f;
	/** 커서가 지금 무기의 사거리 밖일 때 본체 색 (흐리게 하지 않고 색을 바꾼다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair") FLinearColor CrosshairOutOfRangeColor = FLinearColor(1.f, 0.22f, 0.16f, 1.f);
	/** 사거리 밖일 때 투명도 (너무 낮추면 안 보인다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair", meta = (ClampMin = 0.4, ClampMax = 1)) float CrosshairOutOfRangeOpacity = 0.9f;
	/** 사거리 여유 (cm). 근접·테이저·화염은 상대 몸 반지름만큼 더 닿으므로 사거리 + 이 값까지는 "사거리 안" (투척은 여유 없음) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair", meta = (ClampMin = 0)) float CrosshairRangeSlackCm = 40.f;
	/** 공격할 수 없을 때(쓰러짐·경직) 본체 색 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair") FLinearColor CrosshairDisabledColor = FLinearColor(0.62f, 0.64f, 0.68f, 1.f);
	/** 레일건 충전 링 색 (1초 동안 원이 채워진다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair") FLinearColor CrosshairChargeColor = FLinearColor(0.2f, 0.85f, 1.f, 1.f);
	/** 레일건 잠금 중(충전·발사 2초, 이동·회전 불가) 본체 색 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair") FLinearColor CrosshairLockedColor = FLinearColor(1.f, 0.62f, 0.08f, 1.f);
	/** 사제 테이저건 번개 색 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair") FLinearColor CrosshairTaserColor = FLinearColor(0.35f, 0.9f, 1.f, 1.f);
	/** 투척·설치 착탄 원: 폭발 반지름 1cm 당 px (수류탄 600cm → 지름 600×2×값). 최소 지름은 CrosshairSize */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair", meta = (ClampMin = 0)) float CrosshairAreaPxPerCm = 0.075f;
	/** 투척·설치 착탄 원의 최대 지름 (px) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair", meta = (ClampMin = 48)) float CrosshairAreaMaxSize = 110.f;
	/** 화염 부채꼴: 사거리 1cm 당 길이 px (최소 CrosshairSize, 최대 ×1.6) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair", meta = (ClampMin = 0)) float CrosshairConePxPerCm = 0.11f;
	/** 화염 부채꼴 각도 배율 (서버 반각 15°는 화면에서 너무 가늘어 보여 넓혀 그린다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair", meta = (ClampMin = 1)) float CrosshairConeAngleScale = 1.6f;
	/** 반동 벌어짐이 연사로 쌓이는 최대 배수 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair", meta = (ClampMin = 1)) float CrosshairKickMaxStack = 2.2f;
	/** 반동 벌어짐 전체 배율 (0 = 반동 표시 없음) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair", meta = (ClampMin = 0)) float CrosshairKickScale = 1.f;

	/** 히트 마커(서버가 내 공격의 피해를 알려 줄 때만): 색 / 쓰러뜨렸을 때 색 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair|HitMarker") FLinearColor HitMarkerColor = FLinearColor(1.f, 1.f, 1.f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair|HitMarker") FLinearColor KillMarkerColor = FLinearColor(1.f, 0.12f, 0.1f, 1.f);
	/** X 한 획의 바깥 끝까지 거리 (px, 가운데에서) / 안쪽 틈 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair|HitMarker", meta = (ClampMin = 8)) float HitMarkerSize = 26.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair|HitMarker", meta = (ClampMin = 0)) float HitMarkerGap = 9.f;
	/** X 선 굵기 (px) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair|HitMarker", meta = (ClampMin = 2)) float HitMarkerThickness = 5.f;
	/** 보이는 시간 (초): 맞힘 / 쓰러뜨림 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair|HitMarker", meta = (ClampMin = 0.05)) float HitMarkerSeconds = 0.28f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair|HitMarker", meta = (ClampMin = 0.05)) float KillMarkerSeconds = 0.6f;
	/** 상자를 맞혔을 때도 히트 마커 (기본: 플레이어·경비원만) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair|HitMarker") bool bHitMarkerOnBoxes = false;

	/** 조준점 옆 상태 표시 (탄 적음·탄 없음·쿨타임) 글자 크기와 색 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair|Status", meta = (ClampMin = 8)) int32 CrosshairStatusFontSize = 15;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair|Status") FLinearColor CrosshairStatusColor = FLinearColor(1.f, 0.85f, 0.25f, 1.f);
	/** 남은 탄이 이 이하면 조준점 옆에 "탄 N" (0 = 표시 안 함). 탄이 무한인 무기는 표시하지 않는다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair|Status", meta = (ClampMin = 0)) int32 CrosshairLowAmmo = 3;
	/** 공격 간격이 이 초 이상인 무기(테이저건·수류탄·지뢰 1초)는 쏜 뒤 다음 공격까지 작은 쿨타임 원을 조준점 옆에 그린다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair|Status", meta = (ClampMin = 0)) float CrosshairCooldownMinSeconds = 0.9f;

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
