#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VHFxActor.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/** 연출용 임시 도형 모양 (엔진 기본 도형) */
enum class EVHFxShape : uint8
{
	Disc,		// 바닥에 깔리는 얇은 원판 (Size.X = 지름)
	Sphere,		// 구 (Size.X = 지름)
	Cylinder,	// 원기둥 (Size.X = 지름, Size.Z = 높이)
	Box,		// 상자 (Size = 길이·폭·높이, 빔·선에 쓴다)
	Plane,		// 바닥에 눕힌 평면 + 도형 재질 M_VHFxShape (Size.X = 지름). 부채꼴·링·섬광
};

/** 평면 도형(M_VHFxShape)의 모양 값. 각도는 도(앞 = 0, 오른쪽 = +) */
struct FVHFxShapeParams
{
	float HalfAngle = 180.f;	// 부채꼴 반각 (180 = 원)
	float SweepMin = -180.f;	// 보이는 각 범위
	float SweepMax = 180.f;
	float FillOpacity = 0.15f;	// 면
	float EdgeOpacity = 1.f;	// 바깥 테두리 띠
	float EdgeWidth = 0.06f;	// 테두리 띠 두께 (반지름 비율)
	float Inner = 0.f;			// 안쪽 구멍 (반지름 비율)
	float Glow = 0.f;			// > 0 이면 가운데가 밝고 바깥으로 흐려지는 원 (섬광)
	float Spikes = 0.f;			// > 0 이면 별 모양 (총구 불꽃)
	float Intensity = 4.f;		// 발광 세기
};

/**
 * 스킬·폭발·함정 같은 연출을 그리는 임시 표시 액터 (기본 도형 + 반투명 머티리얼 M_VHFx).
 * 판정은 하지 않는다(충돌 없음). 아트 에셋이 생기면 이 액터를 쓰는 자리만 바꿔 끼우면 된다.
 *
 *  - 수명(SetLife): 지정한 시간이 지나면 스스로 사라진다. 끝날 때 서서히 흐려진다.
 *  - 커지기(SetGrow): 수명 동안 크기가 1배 → EndScale 배 (폭발)
 *  - 깜빡임(SetPulse): 불투명도가 주기적으로 변한다 (경고)
 */
UCLASS(NotBlueprintable)
class MCPTEST_API AVHFxActor : public AActor
{
	GENERATED_BODY()

public:
	AVHFxActor();

	/** 만들어서 바로 보여 준다. Life = 0이면 없앨 때까지 남는다 */
	static AVHFxActor* SpawnFx(UWorld* World, const FVector& Location, EVHFxShape Shape, const FVector& SizeCm,
		const FLinearColor& Color, float Opacity, float Life = 0.f, float Yaw = 0.f);

	void InitFx(EVHFxShape Shape, const FVector& SizeCm, const FLinearColor& Color, float Opacity);
	void SetLife(float Seconds, float FadeSeconds = 0.3f);
	void SetGrow(float EndScale) { GrowTo = EndScale; }
	void SetPulse(float Hz) { PulseHz = Hz; }
	/** 포물선으로 날아간다 (수류탄·섬광탄): Seconds 동안 From → To, 가운데에서 Height 만큼 떠오른다. 도착하면 사라진다 */
	void SetArc(const FVector& From, const FVector& To, float Height, float Seconds);
	/** 바닥에 평면 도형을 놓는다 (Location = 중심, Yaw = 앞 방향, Diameter = 지름 cm) */
	static AVHFxActor* SpawnShape(UWorld* World, const FVector& Location, float Yaw, float Diameter, const FVHFxShapeParams& Shape,
		const FLinearColor& Color, float Opacity, float Life);
	/** 평면 도형의 보이는 각 범위를 Seconds 동안 From → To 로 넓힌다 (휘두르기 궤적이 팔을 따라 그려지게) */
	void SetSweep(float FromMin, float FromMax, float ToMin, float ToMax, float Seconds);
	/** 도형 대신 3D 모델 (메시의 재질 그대로). 날아가는 수류탄·설치된 지뢰 */
	static AVHFxActor* SpawnModel(UWorld* World, UStaticMesh* Model, const FTransform& Transform, float Life = 0.f);
	/** 3D 모델을 연출 재질(반투명 단색)로 덮는다 (빔 소드 날 잔상) */
	void TintModel(const FLinearColor& Color, float Opacity);
	/** 초당 회전 (도/초, 축별) */
	void SetSpin(const FRotator& DegreesPerSecond) { Spin = DegreesPerSecond; }
	/** 재질의 숫자 파라미터를 바꾼다 (평면 도형) */
	void SetShapeScalar(FName Name, float Value);
	/** 이 액터에 딸린 연출 액터 (같이 사라진다): 지뢰 모델 + 경고 링 */
	void AddChildFx(AVHFxActor* Child);

	/** 선·빔 모양으로 놓는다: From 에서 To 까지 굵기 Thickness 의 상자 */
	static AVHFxActor* SpawnBeam(UWorld* World, const FVector& From, const FVector& To, float Thickness, const FLinearColor& Color, float Opacity, float Life);
	void SetFxColor(const FLinearColor& Color, float Opacity);

	/** 연출용 반투명 머티리얼 (DA_VHGameplayConfig.FxMaterial). 없으면 nullptr */
	static UMaterialInterface* GetFxMaterial();

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual void Destroyed() override;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;

	FVector BaseScale = FVector::OneVector;
	float BaseOpacity = 0.4f;
	float Age = 0.f;
	float Life = 0.f;		// 0 = 수명 없음
	float Fade = 0.3f;
	float GrowTo = 1.f;
	float PulseHz = 0.f;
	bool bArc = false;
	FVector ArcFrom = FVector::ZeroVector;
	FVector ArcTo = FVector::ZeroVector;
	float ArcHeight = 0.f;
	FRotator Spin = FRotator::ZeroRotator;
	bool bSweep = false;
	FVector4f SweepFrom = FVector4f(0.f), SweepTo = FVector4f(0.f);	// (Min, Max)
	float SweepTime = 0.f;
	bool bModel = false;
	UPROPERTY() TArray<TWeakObjectPtr<AVHFxActor>> ChildFx;
};
