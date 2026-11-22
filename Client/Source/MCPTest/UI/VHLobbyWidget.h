#pragma once

#include "CoreMinimal.h"
#include "UI/VHScreenWidget.h"
#include "VHLobbyWidget.generated.h"

class UButton;
class UTextBlock;
class UEditableTextBox;
class UScrollBox;
class UComboBoxString;
class UWidget;

/**
 * 로비(방 목록).
 * - CS_ROOM_LIST로 받은 방(최대 20개)을 표로 보여 준다: 방 이름, 방장, 인원 n/3, 난이도, 상태
 * - [새로고침](연타 방지), [방 만들기](이름·맵·난이도), [입장], 방 이름 검색(클라이언트 필터)
 * - [빠른 찾기]: 대기중·빈자리 방에 차례로 입장 시도, 없으면 기본 설정으로 방 만들기
 * - [뒤로] = 로그아웃(연결 종료) 후 메인화면
 * - 로그인 응답의 내 전적 표시
 */
UCLASS(Abstract)
class UVHLobbyWidget : public UVHScreenWidget
{
	GENERATED_BODY()

public:
	virtual void OnScreenShown() override;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void BindNetEvents(UVHNetSubsystem& Net) override;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> UserNameText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> StatsText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> LobbyStatusText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> RoomCountText;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEditableTextBox> SearchTextBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UScrollBox> RoomListBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> RefreshButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CreateRoomButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> PrimaryQuickFindButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> PrimaryJoinButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> BackButton;

	// 방 만들기 창
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidget> CreateRoomPanel;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEditableTextBox> RoomNameTextBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UComboBoxString> MapComboBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UComboBoxString> DifficultyComboBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> CreateHintText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> PrimaryCreateConfirmButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CreateCancelButton;

	/** 새로고침 버튼 연타 방지 시간(초) */
	UPROPERTY(EditAnywhere, Category = "Lobby") float RefreshCooldownSeconds = 1.0f;

private:
	UFUNCTION() void HandleRefresh();
	UFUNCTION() void HandleOpenCreate();
	UFUNCTION() void HandleQuickFind();
	UFUNCTION() void HandleJoin();
	UFUNCTION() void HandleBack();
	UFUNCTION() void HandleSearchChanged(const FText& Text);
	UFUNCTION() void HandleRoomNameChanged(const FText& Text);
	UFUNCTION() void HandleCreateConfirm();
	UFUNCTION() void HandleCreateCancel();

	UFUNCTION() void HandleRoomListUpdated();
	UFUNCTION() void HandleRoomRequestResult(bool bSuccess, const FText& Message);
	UFUNCTION() void HandleStatusMessage(const FText& Message);
	UFUNCTION() void HandleLeftRoom(const FText& Message);

	void RebuildList();
	void UpdateButtons();
	void UpdateStats();
	bool ValidateRoomName(FText& OutHint) const;
	void EndRefreshCooldown();

	int64 SelectedRoomId = 0;
	bool bRefreshCooling = false;
	FTimerHandle RefreshCooldownTimer;
};
