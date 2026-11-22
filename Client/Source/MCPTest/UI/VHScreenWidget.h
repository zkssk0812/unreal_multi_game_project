#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/VHUIStyle.h"
#include "VHScreenWidget.generated.h"

class UVHNetSubsystem;
class UVHGameData;
class AVHMenuPlayerController;
class UTextBlock;
class UBorder;

UENUM(BlueprintType)
enum class EVHScreen : uint8
{
	None,
	MainMenu,
	Login,
	Lobby,
	Room,
	Result
};

/**
 * 메뉴 화면 위젯의 공통 부모 (C++ 로직 + 블루프린트 배치).
 * WBP_* 위젯 블루프린트가 이 클래스를 상속하고, 디자이너에서 정해진 이름의 위젯을 배치하면
 * meta=(BindWidget)로 C++ 변수에 자동 연결된다.
 *
 * 공통 스타일: bApplyCommonStyle이면 화면이 만들어질 때(디자이너 미리보기 포함)
 * 위젯 트리의 버튼·글자·입력칸에 UVHUIStyle 값을 적용한다.
 *  - 이름에 "Title"이 들어간 글자 → 제목, "Heading" → 구역 제목, "Hint"/"Status"/"Small" → 작은 글씨
 *  - "Background"라는 이름의 Border → 화면 배경, 이름에 "Panel"이 들어간 Border → 패널
 *  - 이름이 "Primary"로 시작하거나 "Primary"가 들어간 버튼 → 강조 버튼
 */
UCLASS(Abstract)
class UVHScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 화면이 보일 때(처음 만들어질 때 포함) 호출 */
	virtual void OnScreenShown() {}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	bool bApplyCommonStyle = true;

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** 서브시스템 델리게이트 연결/해제 (파생 클래스에서 구현) */
	virtual void BindNetEvents(UVHNetSubsystem& Net) {}
	virtual void UnbindNetEvents(UVHNetSubsystem& Net);

	void ApplyCommonStyle();

	UVHNetSubsystem* GetNet() const;
	AVHMenuPlayerController* GetMenuPC() const;
	const UVHUIStyle& Style() const;
	const UVHGameData& GameData() const;

	/** 상태 문구 표시 (bIsError면 빨간색) */
	void SetStatus(UTextBlock* Target, const FText& Message, bool bIsError = false, bool bIsSuccess = false) const;

	/** 위젯을 숨기고, 같은 줄 바로 앞의 글자(예: "맵", "난이도" 라벨)도 숨긴다 */
	static void CollapseWithLabel(UWidget* Widget);
	void GoTo(EVHScreen Screen) const;
};
