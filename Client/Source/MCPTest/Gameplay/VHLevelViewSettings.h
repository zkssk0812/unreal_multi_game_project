#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VHLevelViewSettings.generated.h"

/**
 * 레벨마다 다른 화면 설정. 레벨에 하나만 놓는다(없으면 기본값).
 *
 * 탑뷰 카메라 방향(TopViewYaw): 카메라가 바라보는 월드 방향 = 화면 위쪽.
 *  - 0  → 화면 위 = +X (L_InteractionTest 등 기본)
 *  - 90 → 화면 위 = +Y (L_Mansion_1F: 맵을 서버와 같은 좌표(회전 없음)로 두고 화면만 평면도 방향으로 돌림, 2026-10-27 서버 팀원과 결정)
 * 맵·배치 좌표는 그대로 두고 보이는 방향만 바뀐다. 이동은 커서 기준이라 영향 없음.
 */
UCLASS(Blueprintable, meta = (DisplayName = "VH Level View Settings"))
class MCPTEST_API AVHLevelViewSettings : public AActor
{
	GENERATED_BODY()

public:
	AVHLevelViewSettings();

	/** 탑뷰 카메라 Yaw(도). 화면 위쪽이 가리키는 월드 방향 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VaultHeist|Camera", meta = (Units = "deg"))
	float TopViewYaw = 0.f;

	/** 이 월드의 탑뷰 카메라 Yaw (설정 액터가 없으면 0) */
	static float GetTopViewYaw(const UWorld* World);

	/** 켜면 시작할 때 DA_VHGameplayConfig 의 맵 밝기(MapMoonlightIntensity)를 이 레벨 달빛에 적용한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VaultHeist|Lighting")
	bool bApplyConfigLighting = true;

	/** 레벨의 달빛(Directional Light) 밝기를 바꾼다(lux). 바꾼 달빛 수를 돌려준다 */
	static int32 SetMoonlight(UWorld* World, float Lux);

protected:
	virtual void BeginPlay() override;
};
