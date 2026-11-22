#pragma once

#include "CoreMinimal.h"
#include "UI/VHCodeWidgets.h"
#include "VHGameHUDWidget.generated.h"

class UCanvasPanel;
class UBorder;
class UImage;
class UProgressBar;
class UTextBlock;
class UVerticalBox;
class AVHInteractableBase;
class UTexture2D;
class UVHMinimapWidget;

/**
 * 게임 중 HUD (임시 디자인, 코드로 구성) [PvP 규칙 v1 14장]. 기획 화면비 16:9, 색·크기는 UVHUIStyle(DA_VHUIStyle)의 HUD 항목.
 *
 *  배치는 기획 UI 수정본(Docs/Design/기획_2026-11/UI_수정본.png)을 따른다.
 *  - 좌상단: 미니맵 (UVHMinimapWidget: 반납지점 위치·남은 시간, 퍽별 표시)
 *  - 위 가운데: 남은 시간(1분 이하 빨강) / 우상단: 순위표(N위 닉네임 반납 금액, 나는 강조, 공동 순위)
 *  - 우측 가운데: 들고 있는 가치 (0이면 흐리게)
 *  - 좌하단: 캐릭터 이미지 · 닉네임 + 상태이상(이름만) · HP 바(위쪽 띠 = 보호막) · SP 바(탈진 회색) · 스킬 칸 · 퍽 3칸
 *  - 아래 가운데: 소비 아이템 · 무기1(지금 든 무기, 크게) · 무기2 · 남은 탄(든 무기)
 *  - 화면 전체: 섬광(흰색 덮개)
 *  - 상단 가운데 아래: 알림 영역 (Match 알림 최근 N개, N초 뒤 사라짐: 보물 획득·처치·금고 열림)
 *  - 화면 가운데: 사망 — 리스폰 카운트다운·사유·손실 금액 / 시간 종료 등 배너
 *  - 월드 위: 상호작용 안내·진행 바, 다른 오브젝트 진행도(옅은 바), 반납지점 남은 시간, 의적 머리 위 닉네임(더미는 HP, 나는 리스폰 무적)
 * 디자인이 정해지면 같은 정보를 쓰는 위젯 블루프린트로 교체하면 된다.
 */
UCLASS()
class MCPTEST_API UVHGameHUDWidget : public UVHCodeWidget
{
	GENERATED_BODY()

protected:
	virtual void BuildTree() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	/** 리스폰 뒤 몇 초 동안: 내 캐릭터 둘레에 떨어뜨린 보물 쪽 화살표 + 보물 위 ◇ 표시 (위젯 트리 없이 직접 그림) */
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	void UpdatePrompt();
	/** 커서를 올린 물품의 정보창 ("권총/무기/대미지 10", 금고·상자·필드 아이템은 이름만) */
	void UpdateInfo();
	void UpdateIdleBars();
	void UpdateTopRight();
	void UpdateCarry();
	void UpdatePlayerPanel();
	void UpdateNotices();
	void UpdateCenter();
	void UpdateDevLine();
	void UpdateWorldLabels();
	bool PlaceAt(UWidget* Widget, const FVector& WorldLocation) const;

	UPROPERTY() TObjectPtr<UCanvasPanel> Canvas;

	// 대상 안내
	UPROPERTY() TObjectPtr<UBorder> PromptBox;
	UPROPERTY() TObjectPtr<UTextBlock> PromptText;
	UPROPERTY() TObjectPtr<UProgressBar> PromptBar;

	// 커서 정보창 (품질별 색)
	UPROPERTY() TObjectPtr<UBorder> InfoBox;
	UPROPERTY() TObjectPtr<UTextBlock> InfoText;
	UPROPERTY() TObjectPtr<UImage> InfoIcon;			// 소비 아이템: 아이콘 (HUD 소비 칸과 같은 그림)
	UPROPERTY() TObjectPtr<UBorder> InfoAccent;		// 소비 아이템: 왼쪽 색 띠

	// 옅은 진행 바 (재사용 풀)
	UPROPERTY() TArray<TObjectPtr<UProgressBar>> IdleBars;

	// 월드 위 작은 글자 (반납지점 남은 시간·머리 위 이름 등, 재사용 풀)
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> WorldLabels;

	// 좌상단: 미니맵 (반납지점 위치·남은 시간, 퍽별 표시)
	UPROPERTY() TObjectPtr<UVHMinimapWidget> Minimap;

	// 위 가운데: 남은 시간 / 우상단: 순위표
	UPROPERTY() TObjectPtr<UBorder> TimeBox;
	UPROPERTY() TObjectPtr<UTextBlock> TimeText;
	UPROPERTY() TObjectPtr<UBorder> TopRightBox;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> RankLines;

	// 우측 가운데: 들고 있는 가치
	UPROPERTY() TObjectPtr<UBorder> CarryBox;
	UPROPERTY() TObjectPtr<UTextBlock> CarryText;

	// 좌하단: 내 캐릭터
	UPROPERTY() TObjectPtr<UBorder> PlayerBox;
	UPROPERTY() TObjectPtr<UBorder> PortraitBox;
	UPROPERTY() TObjectPtr<UImage> PortraitImage;
	UPROPERTY() TObjectPtr<UTextBlock> PortraitText;
	UPROPERTY() TObjectPtr<UTextBlock> NameText;
	/** 상태이상 (HP 바 위, 이름만) */
	UPROPERTY() TObjectPtr<UTextBlock> StatusText;
	// 아래 가운데: 소비 아이템 · 무기1(지금 든 무기) · 무기2 · 남은 탄
	UPROPERTY() TObjectPtr<UBorder> WeaponPanel;
	UPROPERTY() TObjectPtr<UTextBlock> AmmoText;
	UPROPERTY() TObjectPtr<UProgressBar> HpBar;
	UPROPERTY() TObjectPtr<UProgressBar> ShieldBar;
	UPROPERTY() TObjectPtr<UTextBlock> HpText;
	UPROPERTY() TObjectPtr<UProgressBar> SpBar;
	UPROPERTY() TObjectPtr<UTextBlock> SpText;
	UPROPERTY() TObjectPtr<UBorder> SkillBox;
	UPROPERTY() TObjectPtr<UTextBlock> SkillText;
	UPROPERTY() TObjectPtr<UBorder> ConsumableBox;
	UPROPERTY() TObjectPtr<UTextBlock> ConsumableText;
	UPROPERTY() TObjectPtr<UImage> ConsumableIcon;	// 구상안 Consumables_v1 아이콘
	UPROPERTY() TObjectPtr<UTextBlock> ConsumableKey;	// 아이콘 옆 "[E]"
	UPROPERTY() TObjectPtr<UBorder> ConsumableStripe;	// 아래쪽 발광 띠 색
	uint8 LastConsumableIcon = 255;
	UPROPERTY() TArray<TObjectPtr<UBorder>> WeaponBoxes;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> WeaponTexts;
	/** 무기 아이콘 [0] = 든 무기(큰 칸), [1] = 휠로 바꿀 무기(작은 칸) */
	UPROPERTY() TArray<TObjectPtr<UImage>> WeaponIcons;
	uint8 LastIconWeapon[2] = { 255, 255 };
	// 퍽 칸 3개 (방에서 고른 퍽. 방탄조끼는 남은 보호막, 기사회생은 남았는지 표시)
	UPROPERTY() TArray<TObjectPtr<UBorder>> PerkBoxes;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> PerkTexts;

	// 알림 영역 (재사용 풀)
	UPROPERTY() TArray<TObjectPtr<UBorder>> NoticeBoxes;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> NoticeTexts;

	// 화면 가운데: 사망 화면
	UPROPERTY() TObjectPtr<UBorder> DeathBox;
	UPROPERTY() TObjectPtr<UTextBlock> DeathTitle;
	UPROPERTY() TObjectPtr<UTextBlock> DeathReason;
	UPROPERTY() TObjectPtr<UTextBlock> DeathLoss;		// 반납한 돈 손실
	UPROPERTY() TObjectPtr<UTextBlock> DeathCarried;	// 들고 있던 것 (보물·환금 아이템이 어떻게 됐는지)
	UPROPERTY() TObjectPtr<UTextBlock> DeathHint;		// 떨어진 보물은 미니맵에 표시된다는 안내

	// 화면 가운데 전체 메시지 (시간 종료·금고 열림)
	UPROPERTY() TObjectPtr<UBorder> BannerBox;
	UPROPERTY() TObjectPtr<UTextBlock> BannerText;

	// 섬광: 화면 전체 흰색 덮개
	UPROPERTY() TObjectPtr<UBorder> FlashOverlay;

	// 오른쪽 아래: 조작 안내 + 개발용 상태 한 줄
	UPROPERTY() TObjectPtr<UTextBlock> HelpText;
	UPROPERTY() TObjectPtr<UTextBlock> DevText;

	/** 지금 보여주는 캐릭터 이미지의 캐릭터 ID (바뀔 때만 다시 읽는다) */
	int32 ShownCharacterId = INDEX_NONE;
};
