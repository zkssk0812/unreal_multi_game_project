#pragma once

#include "CoreMinimal.h"
#include "UI/VHScreenWidget.h"
#include "VHRoomWidget.generated.h"

class UButton;
class UTextBlock;
class UPanelWidget;
class UComboBoxString;
class UVHRoomSlotWidget;
class UVHChoiceButtonWidget;

/**
 * 대기실(방 내부). 항상 최신 SC_ROOM_STATE를 기준으로 다시 그린다.
 * (방 안의 변경 요청은 개별 성공 응답이 없고, 성공하면 SC_ROOM_STATE가 온다)
 * - 최대 3슬롯: 이름(★방장, (나)), 캐릭터, 퍽, 준비 여부
 * - 캐릭터 선택(다른 사람이 고른 캐릭터는 비활성), 퍽 선택(0~3개, 본인 안에서 중복 불가)
 * - 방장만 맵·난이도 변경, [준비/준비 해제], [나가기]
 * - 게임 시작 버튼 없음: 모든 플레이어가 준비하면 서버가 자동으로 시작(SC_GAME_LOAD)
 */
UCLASS(Abstract)
class UVHRoomWidget : public UVHScreenWidget
{
	GENERATED_BODY()

public:
	virtual void OnScreenShown() override;

protected:
	virtual void NativeOnInitialized() override;
	virtual void BindNetEvents(UVHNetSubsystem& Net) override;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> RoomTitleText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> RoomInfoText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> RoomStatusText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> AutoStartHintText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UPanelWidget> SlotsBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UPanelWidget> CharacterBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UPanelWidget> PerkBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> PerkCountText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UComboBoxString> MapComboBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UComboBoxString> DifficultyComboBox;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> OptionHintText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> PrimaryReadyButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> ReadyButtonText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> LeaveButton;

	/** 준비하려면 골라야 하는 퍽 개수 (사용자 결정 2026-10-16: 캐릭터 + 퍽 3개 전부). 0이면 캐릭터만 필수 */
	UPROPERTY(EditAnywhere, Category = "Room", meta = (ClampMin = 0, ClampMax = 3)) int32 RequiredPerksForReady = 3;

private:
	UFUNCTION() void HandleRoomStateChanged();
	UFUNCTION() void HandleRoomError(const FText& Message);
	UFUNCTION() void HandleMapChanged(FString Selected, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleDifficultyChanged(FString Selected, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleReady();
	UFUNCTION() void HandleLeave();

	void BuildChoices();
	void Refresh();
	void OnCharacterChosen(int32 CharacterId);
	void OnPerkChosen(int32 PerkId);

	UPROPERTY() TArray<TObjectPtr<UVHRoomSlotWidget>> SlotWidgets;
	UPROPERTY() TArray<TObjectPtr<UVHChoiceButtonWidget>> CharacterButtons;
	UPROPERTY() TArray<TObjectPtr<UVHChoiceButtonWidget>> PerkButtons;
	bool bUpdatingCombos = false;
};
