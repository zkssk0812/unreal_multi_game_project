#pragma once

#include "CoreMinimal.h"
#include "MCPTestPlayerController.h"
#include "VHGamePlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UVHGameHUDWidget;
class UVHToastWidget;
class UVHInteractionComponent;
class UVHTreasureCarrierComponent;
class UVHThiefStateComponent;
class UVHActionComponent;
class UVHInteractionSubsystem;
class AVHInteractableBase;
struct FInputActionValue;

/**
 * 게임 레벨용 플레이어 컨트롤러.
 *
 * 입력을 받아 조종 중인 캐릭터의 컴포넌트와 서버 전송 담당(UVHServerGameSubsystem)에 전달만 한다. 규칙 판정은 서버가 한다.
 * (캐릭터 클래스에는 아무것도 추가하지 않는다 → 진짜 캐릭터가 정해지면 컴포넌트만 붙이면 됨)
 *   F               → UVHInteractionComponent (상호작용)
 *   Shift / Space   → UVHActionComponent (달리기 / 구르기) + 구르기 시작을 서버에 알림
 *   좌클릭 꾹       → 공격 시작·끝을 서버에 알림 (AttackStart / AttackStop)
 *   마우스 휠·Q·E   → 무기 교체·스킬·소비 아이템 요청을 서버에 보냄
 *   IA_Move / IA_Jump → 진행 중인 작업 취소 알림
 *
 * 개발용 콘솔 명령(VH로 시작)을 제공한다. 콘솔에서 VHHelp.
 */
UCLASS()
class MCPTEST_API AVHGamePlayerController : public AMCPTestPlayerController
{
	GENERATED_BODY()

public:
	// ── 개발용 콘솔 명령 ──
	UFUNCTION(Exec) void VHHelp();
	/** 안내에 보이는 상호작용 시간 배율 (0.1 = 10배 빠르게, 1 = 원래대로) */
	UFUNCTION(Exec) void VHFastInteract(float Scale = 0.1f);
	/** 상호작용 키를 N초 동안 누르고 있는 것처럼 (0이면 누르기만, VHInteractUp으로 뗌) */
	UFUNCTION(Exec) void VHHoldInteract(float Seconds = 1.f);
	UFUNCTION(Exec) void VHInteractUp();
	/**
	 * 이름에 Name이 들어간 곳으로 순간이동. 찾는 순서: 상호작용 오브젝트(앞에 섬) → 의적(옆에 섬)
	 * → PlayerStart → 아무 액터 라벨(위에 섬). 예: VHGoto Safe_03, VHGoto PlayerStart
	 */
	UFUNCTION(Exec) void VHGoto(const FString& Name);
	/** 개발용: 가장 가까운 경비원 앞 Distance(cm)로 가서 그쪽을 보고 공격 (실서버 공격·경직 확인용) */
	UFUNCTION(Exec) void VHAttack(float Distance = 80.f);
	/**
	 * 이름에 Name이 들어간 오브젝트 바로 옆으로 가서 상호작용 키를 Seconds초 누른다 (시선·손전등 무시, 개발용).
	 * 반납지점은 가운데에서 50cm 거리에 선다. 실서버 판정 확인용 (예: VHUse Safe_03 31, VHUse ReturnPoint 0.3)
	 */
	UFUNCTION(Exec) void VHUse(const FString& Name, float Seconds = 0.3f);
	/** 손전등 켜기/끄기 (R 키와 같음) */
	UFUNCTION(Exec) void VHFlashlight();
	/** 화면의 내 스테미나를 값으로 (0이면 탈진. 서버 값과 5 넘게 다르면 서버 값으로 돌아온다) */
	UFUNCTION(Exec) void VHStamina(float Value = 100.f);
	/** 서버가 보낸 것처럼 내 상태를 흉내 낸다(내 화면만): knockback, jetpack, railgun, sticky, cloak, stun, flash, smoke. 서버와 게임 중일 때만 */
	UFUNCTION(Exec) void VHFake(const FString& Name, float Seconds = 3.f);
	/** 맵 달빛 밝기(lux)를 바로 바꿔 본다 (저장 안 됨 → 정한 값은 DA_VHGameplayConfig 의 Map Moonlight Intensity) */
	UFUNCTION(Exec) void VHMapLight(float Lux = 0.12f);
	/** 모든 의적 손전등 밝기(칸델라)를 바로 바꿔 본다 (저장 안 됨 → DA_VHInteractionConfig 의 Flashlight Intensity) */
	UFUNCTION(Exec) void VHFlashPower(float Candela = 60.f);
	/** 손전등 주변 빛 밝기·반경(cm, 0이면 그대로)을 바로 바꿔 본다 (저장 안 됨 → DA_VHInteractionConfig 의 Flashlight Aura) */
	UFUNCTION(Exec) void VHAura(float Candela = 3.f, float Radius = 0.f);
	/** 다른 의적의 손전등 원뿔·주변 빛 보이기(1)/강제로 숨기기(0) — 프레임 비교용 */
	UFUNCTION(Exec) void VHOtherLights(int32 bShow = 1);
	/** N초 동안 평균 프레임(FPS)과 가장 느린 프레임을 재서 화면·로그에 출력 */
	UFUNCTION(Exec) void VHFps(float Seconds = 5.f);

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	/** 탑뷰: 마우스로 카메라를 돌리지 않는다. 컨트롤 회전을 0으로 고정 → 이동이 월드 축 기준(W = +X) */
	virtual void UpdateRotation(float DeltaTime) override;

	/** 게임 중 마우스 커서 표시 + 클릭이 게임 입력으로 가게 */
	void ApplyGameInputMode();

	/** 조종 중인 캐릭터 카메라에 테두리(외곽선) 포스트 프로세스 머티리얼을 붙인다 */
	void ApplyOutlinePostProcess();

	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> OutlineMID;

	void OnInteractStarted();
	void OnInteractCompleted();
	/** 마우스 휠: 무기 교체 / Q: 스킬 / E: 소비 아이템 (키를 직접 묶음) */
	void OnSwitchWeaponKey();
	void OnSkillKey();
	void OnConsumableKey();
	void OnSprintStarted();
	void OnSprintCompleted();
	void OnMoveTriggered(const FInputActionValue& Value);
	void OnJumpStarted();
	void OnRollStarted();
	/** 좌클릭 누름 → 꾹 누르는 동안 공격 (서버에 AttackStart) */
	void OnAttackStarted();
	/** 좌클릭 뗌(Completed)·취소(Canceled) → 공격 멈춤 */
	void OnAttackCompleted();
	void OnFlashlightStarted();
	/** 내 의적 표시 이름 = 로그인 이름 (알림용) */
	void ApplyMyDisplayName();

	void HandleMessage(const FText& Message, bool bIsError);
	void BindPawnEvents();

	UVHInteractionComponent* GetInteraction() const;
	UVHTreasureCarrierComponent* GetCarrier() const;
	UVHThiefStateComponent* GetThiefState() const;
	UVHActionComponent* GetAction() const;
	UVHInteractionSubsystem* GetInteractionSubsystem() const;
	void DebugPrint(const FString& Line) const;

	virtual void OnPossess(APawn* InPawn) override;

	UPROPERTY(EditAnywhere, Category = "Input|VaultHeist")
	TObjectPtr<UInputMappingContext> GameplayMappingContext;

	UPROPERTY(EditAnywhere, Category = "Input|VaultHeist")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditAnywhere, Category = "Input|VaultHeist")
	TObjectPtr<UInputAction> SprintAction;

	/** 손전등 켜기/끄기 (R) */
	UPROPERTY(EditAnywhere, Category = "Input|VaultHeist")
	TObjectPtr<UInputAction> FlashlightAction;

	/** 이동 입력이 들어오면 길게 누르기를 취소하기 위해 */
	UPROPERTY(EditAnywhere, Category = "Input|VaultHeist")
	TObjectPtr<UInputAction> MoveAction;

	/** 템플릿 점프 입력(점프는 없음). 들어오면 길게 누르기만 취소 */
	UPROPERTY(EditAnywhere, Category = "Input|VaultHeist")
	TObjectPtr<UInputAction> JumpAction;

	/** 구르기 (Space) [팀 결정] */
	UPROPERTY(EditAnywhere, Category = "Input|VaultHeist")
	TObjectPtr<UInputAction> RollAction;

	/** 공격 (마우스 좌클릭) [기획] */
	UPROPERTY(EditAnywhere, Category = "Input|VaultHeist")
	TObjectPtr<UInputAction> AttackAction;

	UPROPERTY(EditAnywhere, Category = "UI")
	TSubclassOf<UVHGameHUDWidget> HUDClass;

	UPROPERTY(Transient)
	TObjectPtr<UVHGameHUDWidget> HUD;

	UPROPERTY(Transient)
	TObjectPtr<UVHToastWidget> Toast;

	TWeakObjectPtr<UVHInteractionComponent> BoundInteraction;
	FTimerHandle HoldTimer;

	/** 개발용 도우미 */
	AVHInteractableBase* FindInteractableByName(const FString& Name) const;
	void PlaceInFront(APawn* Who, const AVHInteractableBase* Target) const;
};
