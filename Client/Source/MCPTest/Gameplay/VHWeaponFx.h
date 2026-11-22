#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VHWeaponFx.generated.h"

class AVHFxActor;
class UNiagaraComponent;
class UNiagaraSystem;
class UVHWeaponVisualComponent;
struct FVHFxShapeParams;
struct FVHWeaponFxSet;

/**
 * 무기 사용 이펙트 (표시만, 판정 없음) [이펙트 표: Docs/Design/무기_이펙트_표_2026-11-17.md].
 *
 *  - 무기 동작의 연출 순간(UVHWeaponVisualComponent::OnAnyActionEffect): 근접 휘두르기 궤적, 빔 소드 날 잔상, 총구 불꽃
 *  - 서버 이벤트(UVHServerGameSubsystem 이 부른다): SC_FIRE 탄 궤적·전기 줄기·레일건 빔, SC_DAMAGE 피격 불꽃·감전·불붙음,
 *    SC_PROJECTILE 날아가는 수류탄(3D 모델), SC_EXPLOSION 폭발, 지뢰(3D 모델 + 경고 링)·연막 엔티티
 *  - 계속 그리는 것(매 프레임): 화염방사기 불꽃 줄기(꾹 누르는 동안), 레일건 충전 빛, 감전·불붙음
 *
 * 판정 범위를 보여 주는 도형(부채꼴·링·빔 폭)은 서버 GameData.h 값 그대로 그린다. 폭발·연막·화염·피격 불꽃은
 * DA_VHWeaponCatalog 의 나이아가라(엔진 기본 템플릿으로 만듦)를 쓰고, 꺼져 있거나 없으면 도형으로 그린다.
 */
UCLASS()
class MCPTEST_API UVHWeaponFxSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UVHWeaponFxSubsystem* Get(const UObject* WorldContext);
	/** 지금 그리는 꾹 누르기 이펙트 수 (화염방사기 불꽃·레일건 충전) — 연타 시험용 */
	int32 GetHeldFxCount() const { return Held.Num(); }

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// ── 서버 이벤트 ──

	/** SC_FIRE (무기 공격 한 번). Feet = 쏜 사람 발 위치(서버 origin), HitFeet = 처음 맞은 대상 발 위치(없으면 nullptr) */
	void OnFire(uint8 WeaponId, const FVector& Feet, float Yaw, const FVector* HitFeet, UVHWeaponVisualComponent* Shooter);
	/** SC_DAMAGE. Target = 맞은 대상 액터(내 캐릭터·다른 플레이어·경비원, 없으면 nullptr), TargetFeet = 발 위치 */
	void OnDamage(uint8 WeaponId, AActor* Target, const FVector& TargetFeet, bool bTargetIsBox);
	/** SC_PROJECTILE. bFlash = 섬광탄. Thrower 의 던지는 동작이 손을 떠나는 순간에 출발한다 */
	void OnProjectile(bool bFlash, const FVector& FromFeet, const FVector& To, float FlightSeconds, UVHWeaponVisualComponent* Thrower);
	/** SC_EXPLOSION. Kind = RobProto::ExplosionKind (0 수류탄, 1 폭발 지뢰, 2 감전 지뢰, 3 섬광탄) */
	void OnExplosion(int32 Kind, const FVector& Pos, float Radius);
	/** 설치된 지뢰 (3D 모델 + 경고 링). 반환된 액터를 없애면 링도 같이 없어진다 */
	AVHFxActor* SpawnMine(bool bShock, const FVector& Pos, float Yaw);
	/** 지뢰 상태(RobProto ES_ARMED / ES_TRIGGERED) 표시 */
	void ApplyMineState(AVHFxActor* Mine, uint32 State);
	/** 연막 (반지름 cm, 수명 초. 0 = 엔티티가 사라질 때까지). 반환된 액터를 없애면 연기도 없어진다 */
	AVHFxActor* SpawnSmoke(const FVector& Pos, float Radius, float Life);

	/** 맞은 소리 (SC_DAMAGE). 근접은 휘두르는 동작의 맞는 순간에 맞춰 조금 늦게 낸다 */
	void PlayHitSound(uint8 WeaponId, const FVector& Pos);
	/** 레일건 충전 시작·끝 (UVHWeaponVisualComponent::SetRailgunCharging 이 부른다): 충전음·총구에 모이는 빛을 켜고 끈다 */
	void OnRailgunChargeChanged(UVHWeaponVisualComponent* V, bool bCharging);
	/** 짧은 섬광 조명 (점광원이 Seconds 동안 꺼져 간다) */
	void Flash(const FVector& Pos, const FLinearColor& Color, float Intensity, float Radius, float Seconds);

	/** 바닥에 놓는 평면 도형의 앞·오른쪽 보정 (Plane 메시의 UV 방향. 개발 명령 vh.FxPlane 으로 맞춘다) */
	static float PlaneYawOffset;
	static float PlaneSideSign;

private:
	void OnActionEffect(UVHWeaponVisualComponent* Who, uint8 WeaponId);
	/** SweepSeconds = 궤적이 오른쪽 → 왼쪽으로 지나가는 시간 (0 = 기본값) */
	void MeleeArc(uint8 WeaponId, const FVector& Center, float Yaw, float SweepSeconds = 0.f);
	void MuzzleFlash(uint8 WeaponId, const FVector& Pos, float Yaw, float Diameter);
	/** 지그재그 전기 줄기 (도형 상자 여러 개) */
	void Bolt(const FVector& From, const FVector& To, const FLinearColor& Color, float Thickness, float Jitter, float Life);
	void HitSpark(const FLinearColor& Color, const FVector& Pos, float Radius);
	UNiagaraComponent* SpawnNiagara(const TSoftObjectPtr<UNiagaraSystem>& System, const FVector& Pos, const FRotator& Rot, const FLinearColor& Color, float Radius, USceneComponent* AttachTo = nullptr);
	bool UseNiagara(const TSoftObjectPtr<UNiagaraSystem>& System) const;
	AVHFxActor* Shape(const FVector& Pos, float Yaw, float Diameter, const FVHFxShapeParams& Params, const FLinearColor& Color, float Opacity, float Life);
	void UpdateHeldWeapons(float DeltaTime);
	void UpdateTimed(float DeltaTime);
	void UpdateLoops(float DeltaTime);
	/** Seconds 뒤에 한 번 부른다 (게임 시간 = slomo 를 따른다) */
	void After(float Seconds, TFunction<void()> Fn);
	/** 근접: 날 끝 리본 (Seconds 동안 켰다가 끈다. 꼬리는 TrailLife 동안 남는다) */
	void MeleeTrail(UVHWeaponVisualComponent* Who, uint8 WeaponId, float Seconds);
	/** 근접에 맞은 자리: 삼단봉 먼지·별, 도검 불꽃, 빔 소드 그을음·불똥 */
	void MeleeImpact(uint8 WeaponId, const FVector& Body, const FVector& Feet);
	void SpawnDecal(const FVHWeaponFxSet& Set, const FVector& Feet);
	/** 근접: 휘두르는 동작이 맞는 순간까지 남은 시간 (동작이 없으면 기본값) */
	float MeleeHitDelay(uint8 WeaponId) const;

	/** 빔 소드 들고 있는 동안 웅— (무기 표시마다) */
	TMap<TWeakObjectPtr<UVHWeaponVisualComponent>, TWeakObjectPtr<class UAudioComponent>> Hums;
	float HumScanTimer = 0.f;

	struct FFlashLight
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<class UPointLightComponent> Light;
		float Start = 0.f, Seconds = 0.1f, Intensity = 0.f;
	};
	TArray<FFlashLight> Flashes;

	FDelegateHandle ActionHandle;

	/** 시간이 걸리는 이펙트 (전기 줄기 깜빡임, 감전, 불붙음, 날 잔상) */
	struct FTimedFx
	{
		enum EKind : uint8 { TaserBolt, Stun, Burn, Afterimage, ShockBolts, Projectile, Delayed } Kind = TaserBolt;
		/** Delayed: Next 시각에 한 번 부른다 */
		TFunction<void()> Fn;
		TWeakObjectPtr<AActor> Target;
		TWeakObjectPtr<UVHWeaponVisualComponent> Visual;
		FVector A = FVector::ZeroVector, B = FVector::ZeroVector;
		FLinearColor Color = FLinearColor::White;
		float Until = 0.f, Next = 0.f, Every = 0.05f, Radius = 0.f, Param = 0.f;
	};
	TArray<FTimedFx> Timed;

	/** 화염방사기·레일건 충전 (꾹 누르는 동안 무기마다) */
	struct FHeldFx
	{
		TWeakObjectPtr<UVHWeaponVisualComponent> Visual;
		TWeakObjectPtr<UNiagaraComponent> Niagara;
		TWeakObjectPtr<AVHFxActor> Fan;
		TWeakObjectPtr<AVHFxActor> Orb;
		TWeakObjectPtr<class UAudioComponent> Loop;
		float Age = 0.f;
		float NextFlicker = 0.f;
		uint8 Weapon = 0;
		// 레일건 충전 (2026-11-20): 충전음(겹마다 소리 장치), 총구로 모여드는 빛 알갱이, 줄어드는 고리, 점점 밝아지는 빛
		TArray<TWeakObjectPtr<class UAudioComponent>> ChargeSounds;
		struct FSpark
		{
			TWeakObjectPtr<AVHFxActor> Actor;
			FVector From = FVector::ZeroVector;	// 총구 기준 출발 위치 (월드 방향)
			float Age = 0.f;
			float Life = 0.25f;
		};
		TArray<FSpark> Sparks;
		float NextSpark = 0.f;
		TWeakObjectPtr<AVHFxActor> Ring;
		TWeakObjectPtr<AActor> Light;
	};
	/** 레일건 충전 이펙트 한 프레임 (Progress 0~1) */
	void UpdateRailgunCharge(FHeldFx& H, const FVector& Muzzle, float Yaw, float Progress, float DeltaTime);
	/** 꾹 누르기 이펙트를 끝낸다 (불꽃·도형·소리) */
	void EndHeldFx(FHeldFx& H);
	TArray<FHeldFx> Held;
	float HeldScanTimer = 0.f;

	/** 날아가는 수류탄·섬광탄 (폭발이 오면 가장 오래된 같은 종류를 지운다) */
	struct FProjectile { TWeakObjectPtr<AVHFxActor> Fx; bool bFlash = false; };
	TArray<FProjectile> Projectiles;

	/** 지뢰 → 경고 링 */
	TMap<TWeakObjectPtr<AVHFxActor>, TWeakObjectPtr<AVHFxActor>> MineRings;
	TMap<TWeakObjectPtr<AVHFxActor>, TWeakObjectPtr<AVHFxActor>> MineBigRings;
};
