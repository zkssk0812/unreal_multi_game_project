#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VHNetProxy.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UAnimSequence;
class UPointLightComponent;
class USpotLightComponent;
class UVHWeaponVisualComponent;

/**
 * 서버 스냅샷(SC_SNAPSHOT)을 따라 움직이는 표시용 액터 (실서버 모드 전용).
 *  - 다른 플레이어: 캐릭터 메시(DA_VHCharacterVisuals) + 든 무기 + 이름표(이름·HP·상태). 속도에 따라 서기/걷기/달리기를 섞고,
 *    무기를 들면 내 캐릭터와 같은 팔 자세(UVHThiefAnimInstance)
 *    쓰러짐·접속 끊김·은신(ES_CLOAKED)이면 화면에서 숨긴다 (서버는 숨길 대상도 모두 보내므로 클라가 숨긴다)
 *  - 경비원: 기본 도형(빨간 몸통·머리·모자·코) + HP. 경직·자가 수리 중이면 몸을 기울인다
 * 게임 판정은 하지 않는다(충돌 없음). 위치는 스냅샷 두 개 사이를 보간해 약 100ms 늦게 그린다.
 */
UCLASS(NotBlueprintable)
class MCPTEST_API AVHNetProxy : public AActor
{
	GENERATED_BODY()

public:
	AVHNetProxy();

	/** Kind = RobProto::EntityKind (0 의적, 1 경비원). InCharacterId = 방에서 고른 캐릭터 번호(의적만, 0 = 기본 모습) */
	void InitProxy(uint16 InEntityId, uint8 InKind, const FString& InName, int32 InCharacterId = 0);
	/** 스냅샷 하나 (Pos = 발 위치 cm, Yaw 도) */
	void PushSnapshot(double ClientSeconds, const FVector& Pos, float Yaw, uint32 State, uint16 Hp, uint8 Weapon);
	/** 처음 놓을 때·순간이동: 보간 없이 바로 */
	void SnapTo(const FVector& Pos, float Yaw);

	uint16 GetEntityId() const { return EntityId; }
	const FString& GetDisplayName() const { return DisplayName; }
	bool IsGuard() const { return Kind != 0; }
	/** 이름표(이름·HP 글자)가 지금 화면에 그려지는가 (상태 숨김·천장 숨김 모두 반영) */
	bool IsLabelDrawn() const;
	uint32 GetState() const { return State; }
	uint16 GetHp() const { return Hp; }
	uint8 GetWeapon() const { return Weapon; }
	UVHWeaponVisualComponent* GetWeaponVisual() const { return WeaponVisual; }
	/** 지금 화면에서 숨겨야 하는 상태인가 (쓰러짐·접속 끊김·은신) */
	bool IsHiddenByState() const;
	UPointLightComponent* GetAura() const { return Aura; }
	USpotLightComponent* GetBeam() const { return Beam; }

	/** 플레이어 구분 색 (게임 시작 때 의적 순서). 발밑 링 + 이름표 글자 색 */
	void SetPlayerColorIndex(int32 Index);

	/** 손전등 원뿔·주변 빛: 서버 v3.0에서 손전등이 삭제되어 항상 끈다 (개발 명령이 부르므로 함수는 남김) */
	void RefreshLights();

	/** 개발용(VHOtherLights 0): 다른 의적의 손전등 원뿔·주변 빛을 강제로 숨긴다 → 프레임 비교용 */
	static bool bDebugHideOtherLights;

	/** 보간 지연(초). 스냅샷 주기 50ms의 2배 */
	static constexpr double InterpDelay = 0.1;

protected:
	virtual void Tick(float DeltaSeconds) override;

	void UpdateLabel();
	void UpdateAnimation(float Speed);

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> GuardBody;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> GuardHead;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> GuardCap;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> GuardNose;
	/** 로봇 경비원이 든 기관총 (DA_VHCharacterVisuals 의 Guard → Gun Mesh) */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> GuardGun;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Label;
	/** 든 무기 표시 (스냅샷의 weapon 값). 내 캐릭터와 같은 컴포넌트 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UVHWeaponVisualComponent> WeaponVisual;
	/** 다른 의적이 손전등을 켜면(ES_FLASHLIGHT) 켜지는 주변 빛. 값은 내 손전등 주변 빛과 같다 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Aura;
	/**
	 * 다른 의적의 손전등 원뿔 (보이기만, 판정 없음). 몸(루트)에 붙어 있어 서버가 보낸 시선 방향(yaw)을 따라 돈다.
	 * 일반 스포트라이트라 내 상호작용·테두리 판정(UVHFlashlightComponent::IsBoxLit)과 관계없다.
	 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USpotLightComponent> Beam;

	UPROPERTY() TObjectPtr<UAnimSequence> IdleAnim;
	UPROPERTY() TObjectPtr<UAnimSequence> WalkAnim;
	UPROPERTY() TObjectPtr<UAnimSequence> RunAnim;
	UPROPERTY() TObjectPtr<UAnimSequence> GuardAttackAnim;
	UPROPERTY() TObjectPtr<UAnimSequence> GuardStunAnim;
	/** 경비원을 로봇 스켈레탈 메시로 그리는가 (아니면 예전 기본 도형) */
	bool bGuardSkeletal = false;
	/** 쓰러지는 모습을 보여 주기 시작한 시각(FPlatformTime)과 보여 줄 시간. 음수 = 아님 */
	double DownShownAt = -1.0;
	float DownShowSeconds = 0.f;
	bool bDownHidden = false;
	FRotator BodyBaseRotation = FRotator(0.f, -90.f, 0.f);

	struct FSample
	{
		double Time = 0.0;
		FVector Pos = FVector::ZeroVector;
		float Yaw = 0.f;
	};
	TArray<FSample> Samples;

	uint16 EntityId = 0;
	uint8 Kind = 0;
	uint32 State = 0;
	uint16 Hp = 0;
	uint8 Weapon = 0;		// RobProto::WeaponId
	int32 CharacterId = 0;	// 방에서 고른 캐릭터 번호 (DA_VHCharacterVisuals 의 칸)
	FString DisplayName;
	FString LastLabel;
	FVector LastDrawPos = FVector::ZeroVector;
	float SmoothedSpeed = 0.f;
	/** 화면 위 실제 이동(수평 cm/s, 부드럽게) — 이동 블렌드스페이스 방향 */
	FVector SmoothedVelocity = FVector::ZeroVector;
	/** 구르기(ES_DODGING)가 켜진 스냅샷의 받은 시각. 화면 보간이 그 시점에 닿으면 구르기 동작을 시작한다 (음수 = 없음) */
	double PendingRollAt = -1.0;
	/** 구르기 동작 시작 시각(FPlatformTime). 처음 잠깐은 화면의 실제 이동 방향으로 구르는 방향을 고친다 */
	double RollShownAt = -1.0;

	void StartRollFromSamples();
};
