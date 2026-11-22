#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Network/VHNetTypes.h"
#include "UI/VHUIStyle.h"
#include "VHCodeWidgets.generated.h"

class UBorder;
class UButton;
class UTextBlock;
class UVerticalBox;
class UHorizontalBox;
class UImage;
class USizeBox;
class UTexture2D;

/**
 * C++에서 위젯 트리를 직접 만드는 작은 위젯들의 공통 부모.
 * (방 목록 한 줄, 캐릭터 선택 버튼처럼 개수가 바뀌며 반복되는 것은 코드로 만드는 편이 단순하다.)
 * 위젯 블루프린트 없이 CreateWidget(Owner, StaticClass())로 바로 만들 수 있다.
 */
UCLASS(Abstract)
class UVHCodeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 트리가 아직 없으면 만든다. 데이터를 넣는 함수들은 먼저 이걸 부른다 */
	void EnsureBuilt();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void BuildTree() {}
	const UVHUIStyle& Style() const;

	UTextBlock* NewText(EVHTextRole Role, const FText& InText);
	bool bBuilt = false;
};

/** 화면 위쪽 알림(토스트). 여러 개가 쌓이고 몇 초 뒤 사라진다 */
UCLASS()
class UVHToastWidget : public UVHCodeWidget
{
	GENERATED_BODY()

public:
	void AddMessage(const FText& Message, bool bIsError);

protected:
	virtual void BuildTree() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	struct FEntry
	{
		TWeakObjectPtr<UWidget> Widget;
		double ExpireAt = 0.0;
	};
	UPROPERTY() TObjectPtr<UVerticalBox> List;
	TArray<FEntry> Entries;
};

/** 방 목록 한 줄 (누르면 선택) */
UCLASS()
class UVHRoomRowWidget : public UVHCodeWidget
{
	GENERATED_BODY()

public:
	void SetRoom(const FVHRoomSummary& InRoom, bool bInSelected);
	int64 GetRoomId() const { return Room.RoomId; }

	TFunction<void(int64 RoomId)> OnSelected;

	/** 표 머리글용 한 줄을 만든다(누를 수 없음) */
	static UVHRoomRowWidget* MakeHeader(UWidget* Owner);

protected:
	virtual void BuildTree() override;

private:
	UFUNCTION() void HandleClicked();
	void Refresh();

	UPROPERTY() TObjectPtr<UButton> Button;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> Cells;
	FVHRoomSummary Room;
	bool bSelected = false;
	bool bHeader = false;
};

/** 캐릭터·퍽 고르기 버튼 (제목 + 부제, 캐릭터는 위에 얼굴 그림) */
UCLASS()
class UVHChoiceButtonWidget : public UVHCodeWidget
{
	GENERATED_BODY()

public:
	void Setup(int32 InId, const FText& InTitle, const FText& InTooltip);
	void SetState(bool bInSelected, bool bInEnabled, const FText& InSubtitle);
	/** 제목 위에 그림 칸(높이 Height px)을 보인다. Texture 가 없으면 어두운 칸 + Placeholder 글자 (DA_VHGameData 의 Portrait) */
	void SetPortrait(UTexture2D* Texture, float Height, const FText& Placeholder);
	int32 GetId() const { return Id; }

	TFunction<void(int32 Id)> OnChosen;

protected:
	virtual void BuildTree() override;

private:
	UFUNCTION() void HandleClicked();
	void Refresh();

	UPROPERTY() TObjectPtr<UButton> Button;
	UPROPERTY() TObjectPtr<UTextBlock> TitleText;
	UPROPERTY() TObjectPtr<UTextBlock> SubText;
	UPROPERTY() TObjectPtr<USizeBox> PortraitBox;
	UPROPERTY() TObjectPtr<UImage> PortraitImage;
	UPROPERTY() TObjectPtr<UTextBlock> PortraitText;
	int32 Id = 0;
	FText Title;
	FText Subtitle;
	bool bSelected = false;
	bool bEnabledState = true;
};

/** 대기실 플레이어 한 칸 */
UCLASS()
class UVHRoomSlotWidget : public UVHCodeWidget
{
	GENERATED_BODY()

public:
	/** Member가 nullptr이면 빈자리 */
	void SetMember(const FVHRoomMember* Member, bool bIsHost, bool bIsMe);

protected:
	virtual void BuildTree() override;

private:
	UPROPERTY() TObjectPtr<UBorder> Panel;
	UPROPERTY() TObjectPtr<UTextBlock> NameText;
	UPROPERTY() TObjectPtr<UTextBlock> CharacterText;
	UPROPERTY() TObjectPtr<UTextBlock> PerkText;
	UPROPERTY() TObjectPtr<UTextBlock> ReadyText;
};

/** 결과창 플레이어 한 명 카드: 순위·승패(1위 = 승리)를 크게, 이름, 반납 금액, 처치/사망, 연 금고, 반납 보물 [PvP] */
UCLASS()
class UVHPlayerResultCard : public UVHCodeWidget
{
	GENERATED_BODY()

public:
	/**
	 * bTied = 같은 등수가 여럿(공동 순위), bShowCombat = 쓰러뜨림·쓰러짐 값이 있음,
	 * bShowBest = 최고 기록 줄을 보여 준다(서버 결과·샘플. 서버 없이 직접 플레이한 결과에는 기록이 없다)
	 */
	void SetResult(const FVHPlayerResult& Result, bool bTied, bool bShowCombat, bool bShowBest);

protected:
	virtual void BuildTree() override;

private:
	UPROPERTY() TObjectPtr<UBorder> Panel;
	UPROPERTY() TObjectPtr<UTextBlock> OutcomeText;
	UPROPERTY() TObjectPtr<UTextBlock> NameText;
	UPROPERTY() TObjectPtr<UTextBlock> DetailText;
	UPROPERTY() TObjectPtr<UTextBlock> ScoreText;
	/** 최고 기록 줄: "최고 기록 5,200$" / "★ 최고 기록 갱신! 5,200$" (서버 v3.0 best_record·new_record) */
	UPROPERTY() TObjectPtr<UTextBlock> BestText;
};
