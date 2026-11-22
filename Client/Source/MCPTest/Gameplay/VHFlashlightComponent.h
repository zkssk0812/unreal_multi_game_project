#pragma once

#include "CoreMinimal.h"
#include "Components/SpotLightComponent.h"
#include "VHFlashlightComponent.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FVHOnFlashlightChanged, class UVHFlashlightComponent*, bool /*bOn*/);

/**
 * 손전등 컴포넌트 [기획]: R로 켜기/끄기, 앞쪽 원뿔 모양 조명.
 * 스포트라이트 컴포넌트를 그대로 상속했으므로 블루프린트에서 위치(손·가슴 소켓)를 옮기면 된다.
 * 밝기·범위·각도는 DA_VHInteractionConfig 의 Flashlight 항목에서 읽는다.
 * 켜고 끄기 요청은 판정 담당(Authority)을 거친다 (= C_ACTION FlashlightOn/Off, 거절 Cooldown/NoFlashlight).
 */
UCLASS(ClassGroup = (VaultHeist), meta = (BlueprintSpawnableComponent, DisplayName = "VH 손전등"))
class MCPTEST_API UVHFlashlightComponent : public USpotLightComponent
{
	GENERATED_BODY()

public:
	UVHFlashlightComponent();

	/** 키 입력 → 판정 요청 */
	void RequestToggle();

	UFUNCTION(BlueprintPure, Category = "VaultHeist")
	bool IsOn() const { return bOn; }

	/** 이 캐릭터가 손전등을 가지고 있는가 (서버 NoFlashlight 대비, 기본 true) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VaultHeist")
	bool bHasFlashlight = true;

	/** 판정 결과 반영 */
	void ApplyOn(bool bInOn);

	/** 마지막으로 켜고 끈 시각 (연타 방지 판정용) */
	double GetLastToggleTime() const { return LastToggleTime; }

	/**
	 * 이 상자(물품)가 지금 내 손전등 빛 안에 있고 벽 등에 가려지지 않았는가 [팀 결정: 빛 안의 물품만 테두리·상호작용].
	 * 손전등이 꺼져 있으면 false. 상자의 가운데·윗면·옆면 몇 점 중 하나라도 원뿔 안 + 거리 안 + 시야가 트이면 true.
	 */
	bool IsBoxLit(const FBox& Box, const AActor* Target) const;

	FVHOnFlashlightChanged OnFlashlightChanged;

	/** 손전등 주변 빛 (켜면 의적 둘레를 약하게 밝힘, 판정과 무관). 설정에서 끄면 null */
	class UPointLightComponent* GetAura() const { return Aura; }

	/**
	 * 주변 빛을 DA_VHInteractionConfig 값으로 맞춘다. 다른 의적 표시(AVHNetProxy)도 같은 함수를 쓴다.
	 * FootZ = 붙인 부모 기준 발 높이(캐릭터는 -캡슐 반높이, 발 = 액터 위치면 0)
	 */
	static void ConfigureAura(class UPointLightComponent* Light, float FootZ);

	/** 손전등 원뿔(밝기·거리·각도·색·그림자)을 DA_VHInteractionConfig 값으로 맞춘다. 다른 의적 표시의 원뿔도 같은 함수를 쓴다 */
	static void ConfigureBeam(USpotLightComponent* Light);

	/** 이 컴포넌트 기본 위치·각도 (캡슐 가운데 기준). 다른 의적 표시는 발 기준이라 캡슐 반높이를 더해 쓴다 */
	static constexpr float DefaultForward = 30.f;
	static constexpr float DefaultUp = 40.f;
	static constexpr float DefaultPitch = -12.f;

	/** 의적 몸(메시)이 그림자를 만드는지 설정에 맞춘다 (빛 컴포넌트는 건드리지 않음) */
	static void ApplyThiefShadowSetting(AActor* Thief);

protected:
	virtual void BeginPlay() override;

	bool bOn = false;
	double LastToggleTime = -1000.0;

	UPROPERTY(Transient)
	TObjectPtr<class UPointLightComponent> Aura;
};
