#include "Gameplay/VHWeaponFx.h"

#include "Gameplay/VHDoorSubsystem.h"
#include "Gameplay/VHFxActor.h"
#include "Gameplay/VHWeaponCatalog.h"
#include "Gameplay/VHWeaponVisualComponent.h"
#include "Network/VHSettings.h"
#include "Gameplay/VHCharacterVisuals.h"

#include "Gameplay/VHSoundConfig.h"
#include "Gameplay/VHNetProxy.h"
#include "Network/VHProtocol.h"

#include "Components/AudioComponent.h"
#include "Components/DecalComponent.h"
#include "Components/PointLightComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogVHWeaponFx, Log, All);

float UVHWeaponFxSubsystem::PlaneYawOffset = 0.f;
float UVHWeaponFxSubsystem::PlaneSideSign = 1.f;

namespace VHFx
{
	// [서버 GameData.h] 판정 값 — 이펙트 크기는 이 값과 같게 그린다
	constexpr float BatonRange = 200.f, SwordRange = 300.f, BeamRange = 400.f, MeleeHalfAngle = 60.f;	// W-11·15·16
	constexpr float FlameRange = 600.f, FlameHalfAngle = 15.f;			// W-14
	constexpr float TaserRange = 300.f;									// W-12
	constexpr float RailgunHalfWidth = 25.f, RailgunLockSeconds = 1.f, RailgunEverySeconds = 0.2f;	// W-17
	constexpr float MineDetect = 100.f, MineRadius = 300.f;				// MineDetectCm, MineRadiusCm
	constexpr float StunSeconds = 2.f;									// StunMs
	constexpr float ShotHeight = 100.f;									// ShotHeightCm (가슴 높이)
	constexpr float BodyRadius = 40.f;									// BodyRadiusCm
	constexpr float PistolLength = 3000.f;								// 사거리 무제한 → 화면에는 30m까지
	constexpr float RailgunLength = 6000.f;
	constexpr float GrenadeRelease = 0.2f;								// 던지는 동작에서 손을 떠나는 순간 (DA 동작 기본값)

	FVector FeetOf(const AActor* Actor)
	{
		if (const ACharacter* Character = Cast<ACharacter>(Actor))
		{
			return Character->GetActorLocation() - FVector(0.f, 0.f, Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 96.f);
		}
		return Actor ? Actor->GetActorLocation() : FVector::ZeroVector;
	}

	bool IsMelee(uint8 W) { return W >= 1 && W <= 3; }

	/** 선을 도형 재질(M_VHFxShape)로: 상자 면마다 타원이 그려져 양 끝이 가늘어진다 → 탄 궤적·전기 줄기가 밝고 색이 또렷하다 */
	AVHFxActor* ShapeBeam(UWorld* World, const FVector& From, const FVector& To, float Thickness, const FLinearColor& Color, float Intensity, float Life)
	{
		AVHFxActor* Fx = AVHFxActor::SpawnBeam(World, From, To, Thickness, Color, 0.9f, Life);
		UMaterialInterface* Shape = UVHSettings::Get().GetWeaponCatalog().FxShapeMaterial.LoadSynchronous();
		UStaticMeshComponent* Mesh = Fx ? Fx->FindComponentByClass<UStaticMeshComponent>() : nullptr;
		if (Mesh && Shape)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Shape, Fx);
			M->SetVectorParameterValue(TEXT("Color"), Color);
			M->SetScalarParameterValue(TEXT("FillOpacity"), 1.f);
			M->SetScalarParameterValue(TEXT("EdgeOpacity"), 0.f);
			M->SetScalarParameterValue(TEXT("Intensity"), Intensity);
			Mesh->SetMaterial(0, M);
		}
		return Fx;
	}
	float MeleeRange(uint8 W) { return W == 1 ? BatonRange : (W == 2 ? SwordRange : BeamRange); }

	/** 설치된 지뢰마다: 종류·지난 상태·웅웅 소리 (상태가 바뀌는 순간에만 소리, 감전 지뢰 작동 중 작은 불꽃) */
	struct FMineInfo
	{
		bool bShock = false;
		uint32 State = 0;
		TWeakObjectPtr<UAudioComponent> Hum;
		float NextSpark = 0.f;
	};
	TMap<TWeakObjectPtr<AVHFxActor>, FMineInfo> GMines;
}

UVHWeaponFxSubsystem* UVHWeaponFxSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UVHWeaponFxSubsystem>() : nullptr;
}

bool UVHWeaponFxSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UVHWeaponFxSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ActionHandle = UVHWeaponVisualComponent::OnAnyActionEffect.AddUObject(this, &UVHWeaponFxSubsystem::OnActionEffect);
}

void UVHWeaponFxSubsystem::Deinitialize()
{
	UVHWeaponVisualComponent::OnAnyActionEffect.Remove(ActionHandle);
	Super::Deinitialize();
}

TStatId UVHWeaponFxSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UVHWeaponFxSubsystem, STATGROUP_Tickables);
}

// ───────────────── 도구 ─────────────────

AVHFxActor* UVHWeaponFxSubsystem::Shape(const FVector& Pos, float Yaw, float Diameter, const FVHFxShapeParams& Params, const FLinearColor& Color, float Opacity, float Life)
{
	FVHFxShapeParams P = Params;
	if (PlaneSideSign < 0.f)
	{
		// 평면의 오른쪽(+V)이 월드 왼쪽이면 각 범위를 뒤집는다
		const float Min = P.SweepMin;
		P.SweepMin = -P.SweepMax;
		P.SweepMax = -Min;
	}
	return AVHFxActor::SpawnShape(GetWorld(), Pos, Yaw + PlaneYawOffset, Diameter, P, Color, Opacity, Life);
}

bool UVHWeaponFxSubsystem::UseNiagara(const TSoftObjectPtr<UNiagaraSystem>& System) const
{
	return UVHSettings::Get().GetWeaponCatalog().bUseNiagara && !System.IsNull();
}

UNiagaraComponent* UVHWeaponFxSubsystem::SpawnNiagara(const TSoftObjectPtr<UNiagaraSystem>& System, const FVector& Pos, const FRotator& Rot, const FLinearColor& Color, float Radius, USceneComponent* AttachTo)
{
	UNiagaraSystem* Asset = UseNiagara(System) ? System.LoadSynchronous() : nullptr;
	if (!Asset)
	{
		return nullptr;
	}
	UNiagaraComponent* Comp = AttachTo
		? UNiagaraFunctionLibrary::SpawnSystemAttached(Asset, AttachTo, NAME_None, Pos, Rot, EAttachLocation::KeepWorldPosition, true, true, ENCPoolMethod::None, false)
		: UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), Asset, Pos, Rot, FVector(1.f), true, true, ENCPoolMethod::AutoRelease, false);
	if (Comp)
	{
		// 나이아가라 스프라이트는 색 값이 1보다 커야 빛나 보인다 → 이펙트마다 밝기 배율(HDR)을 곱한다
		const UVHWeaponCatalog& Cat = UVHSettings::Get().GetWeaponCatalog();
		const float Hdr = System == Cat.SmokeNiagara ? 1.f : (System == Cat.FlameNiagara ? 1.8f : (System == Cat.ExplosionNiagara ? 1.2f : 2.5f));	// 화염은 알갱이가 겹쳐 더해지므로 낮게
		Comp->SetVariableLinearColor(TEXT("User.Color"), FLinearColor(Color.R * Hdr, Color.G * Hdr, Color.B * Hdr, Color.A));
		Comp->SetVariableFloat(TEXT("User.Radius"), Radius);
	}
	return Comp;
}

void UVHWeaponFxSubsystem::Bolt(const FVector& From, const FVector& To, const FLinearColor& Color, float Thickness, float Jitter, float Life)
{
	// 지그재그: 6마디, 마디마다 옆·위로 흔든다 (양 끝은 고정)
	const int32 Segments = 6;
	const FVector Dir = (To - From).GetSafeNormal();
	const FVector Side = FVector::CrossProduct(Dir, FVector::UpVector).GetSafeNormal();
	FVector Prev = From;
	for (int32 i = 1; i <= Segments; ++i)
	{
		const float T = static_cast<float>(i) / Segments;
		FVector P = FMath::Lerp(From, To, T);
		if (i < Segments)
		{
			P += Side * FMath::FRandRange(-Jitter, Jitter) + FVector::UpVector * FMath::FRandRange(-Jitter, Jitter) * 0.5f;
		}
		VHFx::ShapeBeam(GetWorld(), Prev, P, Thickness, Color, 6.f, Life);
		Prev = P;
	}
}

void UVHWeaponFxSubsystem::HitSpark(const FLinearColor& Color, const FVector& Pos, float Radius)
{
	// 나이아가라 알갱이(흩어지는 불꽃) + 도형 별 모양 섬광(밝은 바닥에서도 색이 읽히게, 위에서 보이게 눕힘)
	const bool bNiagara = SpawnNiagara(UVHSettings::Get().GetWeaponCatalog().HitSparkNiagara, Pos, FRotator::ZeroRotator, Color, Radius) != nullptr;
	FVHFxShapeParams Star;
	Star.Spikes = 4.f;
	Star.Glow = 0.f;
	Star.FillOpacity = 1.f;
	Star.EdgeOpacity = 0.f;
	Star.Intensity = 8.f;
	Shape(Pos, FMath::FRandRange(0.f, 90.f), Radius * 4.5f, Star, Color, 1.f, 0.26f);
	if (bNiagara)
	{
		return;
	}
	if (AVHFxActor* Ball = AVHFxActor::SpawnFx(GetWorld(), Pos, EVHFxShape::Sphere, FVector(Radius * 0.6f), Color, 0.7f, 0.15f))
	{
		Ball->SetGrow(1.6f);
	}
}

void UVHWeaponFxSubsystem::DoorHitSpark(const FVector& Pos, const FLinearColor& Color)
{
	// 닫힌 문 = 벽처럼 막힌 자리 (서버 2026-11-24): 무기 색 불꽃 + 나무 부스러기 색 작은 불꽃 + 짧은 빛
	HitSpark(Color, Pos, 16.f);
	HitSpark(FLinearColor(0.85f, 0.6f, 0.35f), Pos + FVector(0.f, 0.f, 6.f), 10.f);
	Flash(Pos, Color, 2500.f, 220.f, 0.12f);
}

// ───────────────── 동작의 연출 순간 ─────────────────

void UVHWeaponFxSubsystem::OnActionEffect(UVHWeaponVisualComponent* Who, uint8 WeaponId)
{
	if (!Who || Who->GetWorld() != GetWorld() || !Who->IsWeaponShown())
	{
		return;
	}
	const AActor* Owner = Who->GetOwner();
	const float Yaw = Owner->GetActorRotation().Yaw;
	if (VHFx::IsMelee(WeaponId))
	{
		// 동작의 연출 순간 = 칼날이 부채꼴 오른쪽 끝을 지날 때. 궤적은 동작의 Effect Duration(재생 빠르기 반영) 동안 왼쪽 끝까지 지나간다.
		// 높이는 칼(봉)을 휘두르는 손 높이쯤. 근접 3종 모두 수평 베기 (2026-11-17 사용자 결정)
		float Sweep = 0.f;
		const FVHCharacterVisual& Rig = UVHSettings::Get().GetCharacterVisuals().Find(Who->GetCharacterId());
		if (const FVHWeaponMotion* Motion = Rig.AttackMotions.Find(static_cast<EVHWeaponId>(WeaponId)))
		{
			Sweep = Motion->EffectDuration / FMath::Max(Who->GetActionRate(), 0.01f);
		}
		// 2026-11-19: 동작의 Effect Time = 휘두르는 빠른 구간의 시작, Effect Duration = 그 구간 길이 (삼단봉은 끝 = 내려쳐 맞는 순간)
		const float Fast = Sweep > 0.f ? Sweep : (WeaponId == 3 ? 0.08f : 0.1f);
		MeleeTrail(Who, WeaponId, Fast + 0.03f);
		MeleeArc(WeaponId, VHFx::FeetOf(Owner) + FVector(0.f, 0.f, 6.f), Yaw, Fast);
		if (WeaponId == 3)
		{
			// 빔 소드 날 잔상: 휘두르는 동안 0.02초마다 날 모양을 남긴다
			FTimedFx T;
			T.Kind = FTimedFx::Afterimage;
			T.Visual = Who;
			T.Color = UVHSettings::Get().GetWeaponCatalog().FxColor(WeaponId);
			T.Every = 0.02f;
			T.Until = GetWorld()->GetTimeSeconds() + Fast + 0.04f;
			Timed.Add(T);
		}
		return;
	}
	FVector Muzzle;
	if (!Who->GetMuzzleLocation(Muzzle))
	{
		return;
	}
	switch (WeaponId)
	{
	case 4: MuzzleFlash(WeaponId, Muzzle, Yaw, 30.f); break;	// 권총
	case 7: MuzzleFlash(WeaponId, Muzzle, Yaw, 22.f); break;	// 테이저건
	case 6:
	{
		// 레일건: 큰 섬광 + 총구 충격파 링(수직으로 선 고리가 커지며 사라짐) + 바닥 충격파
		MuzzleFlash(WeaponId, Muzzle, Yaw, 80.f);
		const FLinearColor Color = UVHSettings::Get().GetWeaponCatalog().FxColor(WeaponId);
		FVHFxShapeParams Ring;
		Ring.FillOpacity = 0.f;
		Ring.EdgeOpacity = 1.f;
		Ring.EdgeWidth = 0.08f;
		Ring.Intensity = 6.f;
		if (AVHFxActor* R = AVHFxActor::SpawnShape(GetWorld(), Muzzle + FRotator(0.f, Yaw, 0.f).Vector() * 20.f, Yaw, 60.f, Ring, Color, 1.f, 0.35f))
		{
			R->SetActorRotation(FRotator(90.f, Yaw, 0.f));	// 바닥 평면 → 조준 방향을 보고 세움
			R->SetGrow(3.f);
			R->SetLife(0.35f, 0.3f);
		}
		if (AVHFxActor* G = Shape(VHFx::FeetOf(Owner) + FVector(0.f, 0.f, 5.f), 0.f, 120.f, Ring, Color, 0.8f, 0.4f))
		{
			G->SetGrow(2.5f);
			G->SetLife(0.4f, 0.3f);
		}
		break;
	}
	default: break;
	}
	// 총구 섬광 조명 (무기 표: 밝기 0 이면 없음) + 총구 나이아가라(있으면)
	if (const FVHWeaponFxSet* Set = UVHSettings::Get().GetWeaponCatalog().FindFx(WeaponId))
	{
		Flash(Muzzle, Set->FlashColor, Set->FlashIntensity, Set->FlashRadius, Set->FlashSeconds);
		if (UseNiagara(Set->Muzzle))
		{
			SpawnNiagara(Set->Muzzle, Muzzle, FRotator(0.f, Yaw, 0.f), Set->FlashColor, 30.f);
		}
	}
}

void UVHWeaponFxSubsystem::MeleeArc(uint8 WeaponId, const FVector& Center, float Yaw, float SweepSeconds)
{
	// 판정 부채꼴(반지름 = 사거리, 반각 60°)을 옅게 + 바깥 테두리 호. 무기마다 모양을 다르게 (2026-11-19 사용자 요청: 무기끼리 구분)
	const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
	const FVHWeaponFxSet* Set = Catalog.FindFx(WeaponId);
	const FLinearColor Color = Catalog.FxColor(WeaponId);
	const float H = VHFx::MeleeHalfAngle;
	const float Diameter = VHFx::MeleeRange(WeaponId) * 2.f;
	const float Sweep = SweepSeconds > 0.f ? SweepSeconds : (WeaponId == 3 ? 0.08f : 0.1f);
	if (WeaponId == 1)
	{
		// 삼단봉: 내려치는 순간(빠른 구간 끝)에 부채꼴 전체가 흰 테두리로 "탁" 번쩍 (쓸고 지나가지 않음) — 둔기
		TWeakObjectPtr<UVHWeaponFxSubsystem> Weak(this);
		const FLinearColor White = Set ? Set->TrailColor : FLinearColor(0.95f, 0.95f, 1.f);
		After(Sweep, [Weak, Center, Yaw, Diameter, H, White, Color]()
		{
			if (!Weak.IsValid()) { return; }
			FVHFxShapeParams P;
			P.HalfAngle = H;
			P.FillOpacity = 0.08f;
			P.EdgeOpacity = 1.f;
			P.EdgeWidth = 0.045f;
			P.Intensity = 6.f;
			if (AVHFxActor* Arc = Weak->Shape(Center, Yaw, Diameter * 0.9f, P, White, 0.9f, 0.18f))
			{
				Arc->SetGrow(1.12f);	// 0.9R → 1.0R 로 퍼지며 사라짐
				Arc->SetLife(0.18f, 0.14f);
			}
			// 가운데 짧은 내려찍기 줄 (봉이 떨어진 자리)
			FVHFxShapeParams Inner;
			Inner.HalfAngle = 8.f;
			Inner.FillOpacity = 0.5f;
			Inner.EdgeOpacity = 0.f;
			Inner.Intensity = 5.f;
			Weak->Shape(Center, Yaw, Diameter * 0.8f, Inner, Color, 0.8f, 0.1f);
		});
		return;
	}
	FVHFxShapeParams P;
	P.HalfAngle = H;
	P.FillOpacity = WeaponId == 3 ? 0.06f : 0.06f;
	P.EdgeOpacity = 1.f;
	P.EdgeWidth = WeaponId == 3 ? 0.12f : 0.05f;
	P.Intensity = WeaponId == 3 ? 8.f : 5.f;
	// 도검: 은빛 채움 + 빨강 테두리가 오른쪽 → 왼쪽으로 / 빔 소드: 넓고 밝은 플라스마 테두리 + 한 박자 늦은 잔상 호
	if (AVHFxActor* Arc = Shape(Center, Yaw, Diameter, P, Color, 0.9f, WeaponId == 3 ? 0.36f : 0.3f))
	{
		Arc->SetSweep(H - 4.f, H, -H, H, Sweep);
	}
	if (WeaponId == 2)
	{
		FVHFxShapeParams Silver;
		Silver.HalfAngle = H;
		Silver.FillOpacity = 0.f;
		Silver.EdgeOpacity = 1.f;
		Silver.EdgeWidth = 0.025f;
		Silver.Intensity = 4.f;
		if (AVHFxActor* Arc = Shape(Center, Yaw, Diameter * 0.94f, Silver, FLinearColor(0.85f, 0.9f, 1.f), 0.9f, 0.26f))
		{
			Arc->SetSweep(H - 3.f, H, -H, H, Sweep);
		}
	}
	else if (WeaponId == 3)
	{
		TWeakObjectPtr<UVHWeaponFxSubsystem> Weak(this);
		After(0.035f, [Weak, Center, Yaw, Diameter, H, Color, Sweep]()
		{
			if (!Weak.IsValid()) { return; }
			FVHFxShapeParams E;
			E.HalfAngle = H;
			E.FillOpacity = 0.f;
			E.EdgeOpacity = 0.7f;
			E.EdgeWidth = 0.2f;
			E.Intensity = 4.f;
			if (AVHFxActor* Arc = Weak->Shape(Center, Yaw, Diameter * 0.97f, E, Color, 0.6f, 0.3f))
			{
				Arc->SetSweep(H - 4.f, H, -H, H, Sweep);
			}
		});
	}
}

void UVHWeaponFxSubsystem::After(float Seconds, TFunction<void()> Fn)
{
	if (Seconds <= 0.f)
	{
		Fn();
		return;
	}
	FTimedFx T;
	T.Kind = FTimedFx::Delayed;
	T.Next = GetWorld()->GetTimeSeconds() + Seconds;
	T.Until = T.Next;
	T.Fn = MoveTemp(Fn);
	Timed.Add(MoveTemp(T));
}

void UVHWeaponFxSubsystem::MeleeTrail(UVHWeaponVisualComponent* Who, uint8 WeaponId, float Seconds)
{
	const FVHWeaponFxSet* Set = UVHSettings::Get().GetWeaponCatalog().FindFx(WeaponId);
	UStaticMeshComponent* Mesh = Who ? Who->GetWeaponMesh() : nullptr;
	if (!Set || !Mesh || !Mesh->GetStaticMesh() || !UseNiagara(Set->Trail))
	{
		return;
	}
	// 날·봉 끝에 붙여 따라가게: BladeTip 소켓 → 무기 표 대신 값 → 메시 +X 끝의 90% (2026-11-20)
	FVector Tip;
	if (!Who->GetWeaponPointLocal(EVHWeaponPoint::BladeTip, Tip))
	{
		return;
	}
	auto Spawn = [&](const TSoftObjectPtr<UNiagaraSystem>& System, const FLinearColor& C, float Hdr, float Width, float Life, int32 SortPriority)
	{
		UNiagaraSystem* Asset = System.LoadSynchronous();
		UNiagaraComponent* Comp = Asset ? UNiagaraFunctionLibrary::SpawnSystemAttached(Asset, Mesh, NAME_None, Tip, FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, true, true, ENCPoolMethod::None, false) : nullptr;
		if (!Comp)
		{
			return;
		}
		Comp->SetTranslucentSortPriority(SortPriority);	// 가는 심 리본이 넓은 번짐 리본 위에 그려지게
		Comp->SetVariableLinearColor(TEXT("User.Color"), FLinearColor(C.R * Hdr, C.G * Hdr, C.B * Hdr, C.A));
		Comp->SetVariableFloat(TEXT("User.Width"), Width);
		Comp->SetVariableFloat(TEXT("User.Life"), Life);
		TWeakObjectPtr<UNiagaraComponent> Weak(Comp);
		After(Seconds, [Weak]() { if (UNiagaraComponent* N = Weak.Get()) { N->Deactivate(); } });
	};
	// 뒤에 넓은 분류 색 리본(Extra: 도검 빨강 테두리·빔 소드 플라스마 번짐) → 위에 가는 심 리본(Trail: 은빛·흰색)
	if (!Set->Extra.IsNull())
	{
		// 밝기 배율이 크면 색이 하얗게 타 버린다 (2026-11-19 화면 확인) → 번짐은 1 근처, 심만 조금 밝게
		Spawn(Set->Extra, UVHSettings::Get().GetWeaponCatalog().FxColor(WeaponId), WeaponId == 3 ? 1.3f : 0.9f, Set->TrailWidth * (WeaponId == 3 ? 2.4f : 1.8f), Set->TrailLife * 0.85f, 0);
	}
	Spawn(Set->Trail, Set->TrailColor, WeaponId == 3 ? 2.2f : 1.4f, Set->TrailWidth, Set->TrailLife, 1);
}

float UVHWeaponFxSubsystem::MeleeHitDelay(uint8 WeaponId) const
{
	// 동작 시작(SC_FIRE 받은 때)부터 날이 부채꼴 가운데를 지나는 때까지. 삼단봉은 내려쳐 맞는 순간
	const FVHCharacterVisual& Rig = UVHSettings::Get().GetCharacterVisuals().Find(0);
	if (const FVHWeaponMotion* Motion = Rig.AttackMotions.Find(static_cast<EVHWeaponId>(WeaponId)))
	{
		const float Len = Motion->Keys.Num() > 0 ? Motion->Keys.Last().Time : 0.f;
		const float Interval = UVHWeaponCatalog::ServerIntervalSeconds(WeaponId);
		const float Rate = (Len > Interval && Interval > 0.f) ? Len / Interval : 1.f;
		return (Motion->EffectTime + Motion->EffectDuration * (WeaponId == 1 ? 1.f : 0.5f)) / Rate;
	}
	return 0.15f;
}

void UVHWeaponFxSubsystem::PlayHitSound(uint8 WeaponId, const FVector& Pos)
{
	const EVHSound Sound = UVHSoundConfig::HitSound(WeaponId);
	if (!VHFx::IsMelee(WeaponId))
	{
		UVHSoundConfig::Play(this, Sound, Pos);
		return;
	}
	TWeakObjectPtr<UVHWeaponFxSubsystem> Weak(this);
	After(MeleeHitDelay(WeaponId), [Weak, Sound, Pos]() { if (Weak.IsValid()) { UVHSoundConfig::Play(Weak.Get(), Sound, Pos); } });
}

void UVHWeaponFxSubsystem::Flash(const FVector& Pos, const FLinearColor& Color, float Intensity, float Radius, float Seconds)
{
	UWorld* World = GetWorld();
	if (!World || Intensity <= 0.f || Seconds <= 0.f)
	{
		return;
	}
	Flashes.RemoveAll([](const FFlashLight& F) { return !F.Actor.IsValid(); });
	if (Flashes.Num() >= 6)
	{
		return;	// 한꺼번에 너무 많으면 빛은 생략 (프레임)
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* A = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Pos), Params);
	if (!A)
	{
		return;
	}
	UPointLightComponent* L = NewObject<UPointLightComponent>(A);
	L->SetMobility(EComponentMobility::Movable);
	A->SetRootComponent(L);
	L->RegisterComponent();
	L->SetWorldLocation(Pos);
	L->SetCastShadows(false);
	L->SetIntensityUnits(ELightUnits::Unitless);	// DA 값 = 단위 없는 밝기 (예: 8000)
	L->SetLightColor(Color);
	L->SetAttenuationRadius(Radius);
	L->SetIntensity(Intensity);
	FFlashLight F;
	F.Actor = A;
	F.Light = L;
	F.Start = World->GetTimeSeconds();
	F.Seconds = Seconds;
	F.Intensity = Intensity;
	Flashes.Add(F);
}

void UVHWeaponFxSubsystem::SpawnDecal(const FVHWeaponFxSet& Set, const FVector& Feet)
{
	UMaterialInterface* M = Set.ImpactDecal.LoadSynchronous();
	if (!M)
	{
		return;
	}
	// 바닥에 아래를 보고 붙인다 (데칼 X 축 = 투영 방향)
	if (UDecalComponent* D = UGameplayStatics::SpawnDecalAtLocation(GetWorld(), M, FVector(40.f, Set.DecalSize, Set.DecalSize), Feet + FVector(0.f, 0.f, 5.f),
		FRotator(-90.f, FMath::FRandRange(0.f, 360.f), 0.f), Set.DecalLife))
	{
		D->SetFadeOut(Set.DecalLife * 0.5f, Set.DecalLife * 0.5f, false);
	}
}

void UVHWeaponFxSubsystem::MeleeImpact(uint8 WeaponId, const FVector& Body, const FVector& Feet)
{
	UWorld* World = GetWorld();
	const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
	const FVHWeaponFxSet* Set = Catalog.FindFx(WeaponId);
	const FLinearColor ImpactColor = Set ? Set->ImpactColor : Catalog.FxColor(WeaponId);
	if (Set && UseNiagara(Set->Impact))
	{
		SpawnNiagara(Set->Impact, Body, FRotator::ZeroRotator, ImpactColor, WeaponId == 1 ? 30.f : 25.f);
	}
	if (WeaponId == 1)
	{
		// 삼단봉: 발밑 충격 먼지 고리 + 회색 먼지 + 머리 위로 튀는 작은 별
		FVHFxShapeParams Ring;
		Ring.FillOpacity = 0.f;
		Ring.EdgeOpacity = 1.f;
		Ring.EdgeWidth = 0.12f;
		Ring.Intensity = 0.8f;
		if (AVHFxActor* R = Shape(Feet + FVector(0.f, 0.f, 4.f), 0.f, 70.f, Ring, FLinearColor(0.75f, 0.7f, 0.62f), 0.8f, 0.4f))
		{
			R->SetGrow(2.4f);
			R->SetLife(0.4f, 0.3f);
		}
		for (int32 k = 0; k < 5; ++k)
		{
			const FVector P = Feet + FRotator(0.f, k * 72.f + FMath::FRandRange(-20.f, 20.f), 0.f).Vector() * FMath::FRandRange(25.f, 45.f) + FVector(0.f, 0.f, 12.f);
			FVHFxShapeParams Dust;
			Dust.Glow = 0.5f;
			Dust.Intensity = 0.3f;
			if (AVHFxActor* D = Shape(P, 0.f, FMath::FRandRange(30.f, 44.f), Dust, FLinearColor(0.55f, 0.5f, 0.44f), 0.7f, 0.5f))
			{
				D->SetGrow(1.9f);
				D->SetLife(0.5f, 0.4f);
			}
		}
		for (int32 k = 0; k < 4; ++k)
		{
			FVHFxShapeParams Star;
			Star.Spikes = 5.f;
			Star.FillOpacity = 1.f;
			Star.EdgeOpacity = 0.f;
			Star.Intensity = 6.f;
			const FVector From = Body + FVector(0.f, 0.f, 50.f);
			const FVector To = From + FRotator(0.f, k * 90.f + FMath::FRandRange(-25.f, 25.f), 0.f).Vector() * FMath::FRandRange(45.f, 70.f) - FVector(0.f, 0.f, 20.f);
			if (AVHFxActor* S = Shape(From, FMath::FRandRange(0.f, 72.f), 26.f, Star, FLinearColor(1.f, 0.9f, 0.3f), 1.f, 0.5f))
			{
				S->SetArc(From, To, 40.f, 0.45f);
				S->SetSpin(FRotator(0.f, 540.f, 0.f));
				S->SetLife(0.5f, 0.2f);
			}
		}
		return;
	}
	if (WeaponId == 2)
	{
		// 도검: 금속 불꽃(흰·노랑) + 몸을 가로지르는 짧은 베기 줄
		if (!(Set && UseNiagara(Set->Impact)))
		{
			HitSpark(FLinearColor(1.f, 0.85f, 0.45f), Body, 26.f);
		}
		const FVector Side = FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).Vector();
		VHFx::ShapeBeam(World, Body - Side * 45.f + FVector(0.f, 0.f, 25.f), Body + Side * 45.f - FVector(0.f, 0.f, 25.f), 5.f, FLinearColor(0.9f, 0.95f, 1.f), 8.f, 0.09f);
		return;
	}
	// 빔 소드: 불똥(주황) + 플라스마 빛 + 바닥 그을음 자국
	HitSpark(FLinearColor(1.f, 0.45f, 0.1f), Body, 30.f);
	FVHFxShapeParams Glow;
	Glow.Glow = 1.6f;
	Glow.Intensity = 6.f;
	if (AVHFxActor* G = Shape(Body, 0.f, 70.f, Glow, ImpactColor, 0.9f, 0.18f))
	{
		G->SetGrow(1.5f);
	}
	if (Set)
	{
		SpawnDecal(*Set, Feet);
		Flash(Body, Set->FlashColor, Set->FlashIntensity, Set->FlashRadius, Set->FlashSeconds);
	}
}

void UVHWeaponFxSubsystem::MuzzleFlash(uint8 WeaponId, const FVector& Pos, float Yaw, float Diameter)
{
	const FLinearColor Color = UVHSettings::Get().GetWeaponCatalog().FxColor(WeaponId);
	FVHFxShapeParams Star;
	Star.Spikes = WeaponId == 6 ? 6.f : 4.f;
	Star.FillOpacity = 1.f;
	Star.EdgeOpacity = 0.f;
	Star.Intensity = 10.f;
	Shape(Pos + FRotator(0.f, Yaw, 0.f).Vector() * Diameter * 0.25f, Yaw, Diameter, Star, Color, 1.f, WeaponId == 6 ? 0.15f : 0.06f);
	FVHFxShapeParams Glow;
	Glow.Glow = 2.f;
	Glow.Intensity = 6.f;
	Shape(Pos, Yaw, Diameter * 0.7f, Glow, FLinearColor::White, 0.9f, WeaponId == 6 ? 0.12f : 0.05f);
}

// ───────────────── 서버 이벤트 ─────────────────

void UVHWeaponFxSubsystem::OnFire(uint8 WeaponId, const FVector& Feet, float Yaw, const FVector* HitFeet, UVHWeaponVisualComponent* Shooter)
{
	UWorld* World = GetWorld();
	const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
	const FLinearColor Color = Catalog.FxColor(WeaponId);
	const FVector Dir = FRotator(0.f, Yaw, 0.f).Vector();
	const FVector Chest = Feet + FVector(0.f, 0.f, VHFx::ShotHeight);
	const bool bShooterShown = Shooter && Shooter->IsWeaponShown();
	FVector Muzzle = Chest;
	if (bShooterShown)
	{
		Shooter->GetMuzzleLocation(Muzzle);
	}
	// 직선이 끝나는 곳: 맞은 대상(가슴) > 벽·닫힌 문 > 사거리.
	// 닫힌 문은 서버도 벽처럼 막는다(2026-11-24) → 문에서 멈추고 문에 맞은 불꽃·소리 (bDoorHit)
	bool bDoorHit = false;
	auto LineEnd = [&](float Range) -> FVector
	{
		if (HitFeet)
		{
			return *HitFeet + FVector(0.f, 0.f, VHFx::ShotHeight);
		}
		FVector End = Chest + Dir * Range;
		FHitResult Hit;
		FCollisionQueryParams Query(FName(TEXT("VHWeaponFx")), false, Shooter ? Shooter->GetOwner() : nullptr);
		if (World->LineTraceSingleByChannel(Hit, Chest, End, ECC_WorldStatic, Query))
		{
			End = Hit.ImpactPoint;
		}
		const UVHDoorSubsystem* Doors = World->GetSubsystem<UVHDoorSubsystem>();
		FVector DoorAt;
		float DoorT = 0.f;
		if (Doors && Doors->TraceClosedDoor(Chest, End, DoorAt, DoorT))
		{
			End = DoorAt;
			bDoorHit = true;
			Doors->PlayDoorHitFx(DoorAt, Color);
			UE_LOG(LogVHWeaponFx, Log, TEXT("[문 막음] 무기 %u 직선이 닫힌 문 (%.0f, %.0f)에서 멈춤"), WeaponId, DoorAt.X, DoorAt.Y);
		}
		return End;
	};

	switch (WeaponId)
	{
	case 1: case 2: case 3:
		// 근접: 궤적은 동작의 연출 순간에 그린다. 쏜 사람이 안 보이면(동작이 없으면) 바로 그린다
		if (!bShooterShown)
		{
			MeleeArc(WeaponId, Feet + FVector(0.f, 0.f, 6.f), Yaw);
		}
		break;
	case 4:
	{
		// 권총: 가는 탄 궤적 0.08초 (총구 → 맞은 대상/벽)
		const FVector End = LineEnd(VHFx::PistolLength);
		VHFx::ShapeBeam(World, Muzzle, End, 6.f, Color, 8.f, 0.08f);
		VHFx::ShapeBeam(World, Muzzle, End, 2.5f, FLinearColor(1.f, 0.95f, 0.8f), 10.f, 0.05f);	// 밝은 심
		if (!HitFeet && !bDoorHit)
		{
			HitSpark(FLinearColor(1.f, 0.8f, 0.4f), End, 14.f);	// 벽에 맞은 자리 작은 불꽃 (문은 문 불꽃을 이미 냈다)
		}
		break;
	}
	case 7:
	{
		// 테이저: 지그재그 전기 줄기 2가닥, 0.3초 동안 모양이 바뀐다 (사거리 300cm)
		FTimedFx T;
		T.Kind = FTimedFx::TaserBolt;
		T.A = Muzzle;
		T.B = LineEnd(VHFx::TaserRange);
		T.Color = Color;
		T.Every = 0.06f;
		T.Until = World->GetTimeSeconds() + 0.3f;
		Timed.Add(T);
		break;
	}
	case 6:
	{
		// 레일건: 폭 50cm(판정) 빔이 벽을 뚫고 맵 끝까지, 1초 동안 0.2초마다 밝아진다 (판정 6번)
		const FVector Start = Chest;
		const FVector End = Chest + Dir * VHFx::RailgunLength;
		if (AVHFxActor* Wide = AVHFxActor::SpawnBeam(World, Start, End, VHFx::RailgunHalfWidth * 2.f, Color, 0.45f, VHFx::RailgunLockSeconds))
		{
			Wide->SetPulse(1.f / VHFx::RailgunEverySeconds);
		}
		VHFx::ShapeBeam(World, Start, Start + Dir * 1500.f, 16.f, FLinearColor::White, 6.f, VHFx::RailgunLockSeconds);	// 가운데 밝은 심 (총구 쪽 15m)
		// 판정 폭 그대로의 바닥 띠 (위에서 폭이 읽히게)
		if (AVHFxActor* Floor = AVHFxActor::SpawnBeam(World, Feet + FVector(0.f, 0.f, 4.f), Feet + FVector(0.f, 0.f, 4.f) + Dir * VHFx::RailgunLength, VHFx::RailgunHalfWidth * 2.f, Color, 0.2f, VHFx::RailgunLockSeconds))
		{
			Floor->SetActorScale3D(Floor->GetActorScale3D() * FVector(1.f, 1.f, 0.04f));
		}
		break;
	}
	default:
		break;	// 화염방사기: 꾹 누르는 동안 그린다(UpdateHeldWeapons). 수류탄·지뢰: 투척·엔티티로 그린다
	}
}

void UVHWeaponFxSubsystem::OnDamage(uint8 WeaponId, AActor* Target, const FVector& TargetFeet, bool bTargetIsBox)
{
	const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
	const FLinearColor Color = WeaponId == 0 ? FLinearColor(1.f, 0.25f, 0.25f) : Catalog.FxColor(WeaponId);	// 경비원 기관총 = 빨강
	const FVector Body = TargetFeet + FVector(0.f, 0.f, bTargetIsBox ? 50.f : VHFx::ShotHeight);
	const float Now = GetWorld()->GetTimeSeconds();
	switch (WeaponId)
	{
	case 1: case 2: case 3:
	{
		// 근접: 날이 맞는 순간(동작 기준)에 맞춰 늦게. 대상이 그새 움직이면 그 자리
		TWeakObjectPtr<UVHWeaponFxSubsystem> Weak(this);
		TWeakObjectPtr<AActor> WeakTarget(Target);
		const float Up = bTargetIsBox ? 50.f : VHFx::ShotHeight;
		After(MeleeHitDelay(WeaponId), [Weak, WeakTarget, WeaponId, TargetFeet, Up]()
		{
			if (!Weak.IsValid()) { return; }
			const AActor* T = WeakTarget.Get();
			if (T && T->IsHidden()) { return; }
			const FVector F = T ? VHFx::FeetOf(T) : TargetFeet;
			Weak->MeleeImpact(WeaponId, F + FVector(0.f, 0.f, Up), F);
		});
		break;
	}
	case 7: case 10:
	{
		// 감전 (테이저·감전 지뢰): 경직 2초 동안 몸을 감싸는 전기 줄기 + 지지직 반복 소리
		HitSpark(Color, Body, 25.f);
		if (Target && Target->GetRootComponent())
		{
			if (UAudioComponent* A = UVHSoundConfig::PlayLoop(this, EVHSound::StunLoop, Target->GetRootComponent()))
			{
				TWeakObjectPtr<UAudioComponent> WeakA(A);
				After(VHFx::StunSeconds, [WeakA]() { if (UAudioComponent* C = WeakA.Get()) { C->FadeOut(0.2f, 0.f); } });
			}
		}
		FTimedFx T;
		T.Kind = FTimedFx::Stun;
		T.Target = Target;
		T.A = TargetFeet;
		T.Color = Color;
		T.Every = 0.07f;
		T.Until = Now + VHFx::StunSeconds;
		Timed.Add(T);
		break;
	}
	case 5:
	{
		// 불붙음 (화염): 마지막으로 맞은 뒤 0.5초까지. 이미 타고 있으면 늘린다
		for (FTimedFx& T : Timed)
		{
			if (T.Kind == FTimedFx::Burn && T.Target.Get() == Target && Target)
			{
				T.Until = Now + 0.5f;
				return;
			}
		}
		FTimedFx T;
		T.Kind = FTimedFx::Burn;
		T.Target = Target;
		T.A = TargetFeet;
		T.Color = Color;
		T.Every = 0.1f;
		T.Until = Now + 0.5f;
		Timed.Add(T);
		break;
	}
	case 8: case 9:
		HitSpark(Color, Body, 30.f);	// 폭발에 맞음 (폭발 자체는 OnExplosion)
		break;
	default:
		HitSpark(Color, Body, WeaponId == 6 ? 35.f : 25.f);
		if (WeaponId == 6)
		{
			// 레일건에 맞음: 폭발 섬광 (빛 + 흰 구가 퍼짐)
			Flash(Body, FLinearColor(0.6f, 0.85f, 1.f), 12000.f, 350.f, 0.1f);
			if (AVHFxActor* Ball = AVHFxActor::SpawnFx(GetWorld(), Body, EVHFxShape::Sphere, FVector(40.f), FLinearColor(0.85f, 0.95f, 1.f), 0.8f, 0.14f))
			{
				Ball->SetGrow(2.2f);
			}
		}
		break;
	}
}

void UVHWeaponFxSubsystem::OnProjectile(bool bFlash, const FVector& FromFeet, const FVector& To, float FlightSeconds, UVHWeaponVisualComponent* Thrower)
{
	UWorld* World = GetWorld();
	const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
	const FVector Land = To + FVector(0.f, 0.f, 10.f);
	if (bFlash)
	{
		// 섬광탄: 지금 그대로 (흰 공 + 떨어질 자리)
		const FVector From = FromFeet + FVector(0.f, 0.f, 120.f);
		if (AVHFxActor* Fx = AVHFxActor::SpawnFx(World, From, EVHFxShape::Sphere, FVector(26.f), FLinearColor(1.f, 1.f, 0.9f), 1.f))
		{
			Fx->SetArc(From, Land, 180.f, FMath::Max(0.05f, FlightSeconds));
			Projectiles.Add({ Fx, true });
		}
		AVHFxActor::SpawnFx(World, To + FVector(0.f, 0.f, 3.f), EVHFxShape::Disc, FVector(80.f), FLinearColor(1.f, 1.f, 0.8f), 0.5f, FMath::Max(0.05f, FlightSeconds));
		return;
	}
	// 수류탄: 던지는 동작이 손을 떠나는 순간(0.2초)에 손 위치에서 3D 모델이 돌며 출발 → 서버 비행 시간 끝에 도착
	const bool bAnim = Thrower && Thrower->IsActionPlaying() && Thrower->GetActionWeapon() == 8 && Thrower->IsWeaponShown();
	// 손을 떠나는 순간 = DA_VHCharacterVisuals 의 수류탄 동작 Effect Time (2026-11-20: 핀 뽑기 → 젖히기 → 던지기로 바꿔 0.29초)
	float Release = VHFx::GrenadeRelease;
	if (bAnim)
	{
		const FVHCharacterVisual& Rig = UVHSettings::Get().GetCharacterVisuals().Find(Thrower->GetCharacterId());
		if (const FVHWeaponMotion* Motion = Rig.AttackMotions.Find(EVHWeaponId::Grenade); Motion && Motion->EffectTime > 0.f)
		{
			Release = Motion->EffectTime;
		}
	}
	const float Delay = bAnim ? FMath::Max(0.f, Release - Thrower->GetActionTime()) / FMath::Max(Thrower->GetActionRate(), 0.01f) : 0.f;
	FTimedFx T;
	T.Kind = FTimedFx::Projectile;
	T.Visual = Thrower;
	T.A = FromFeet + FVector(0.f, 0.f, 130.f);
	T.B = Land;
	T.Param = FMath::Max(0.05f, FlightSeconds - Delay);
	T.Next = World->GetTimeSeconds() + Delay;
	T.Until = T.Next + 0.01f;
	Timed.Add(T);
	// 날아가는 "휙휙" (날아가는 길 가운데에서, 손을 떠나는 순간에. 서버는 도착하면 바로 터져서 튕기는 소리는 없다, A-14)
	const FVector FlightMid = (T.A + Land) * 0.5f;
	if (Delay > 0.f)
	{
		TWeakObjectPtr<UVHWeaponFxSubsystem> WeakThis(this);
		After(Delay, [WeakThis, FlightMid]() { if (WeakThis.IsValid()) { UVHSoundConfig::Play(WeakThis.Get(), EVHSound::GrenadeFlight, FlightMid); } });
	}
	else
	{
		UVHSoundConfig::Play(this, EVHSound::GrenadeFlight, FlightMid);
	}
	// 떨어질 자리: 폭발 판정 반지름(600cm) 원을 옅게
	FVHFxShapeParams Ring;
	Ring.FillOpacity = 0.05f;
	Ring.EdgeOpacity = 0.6f;
	Ring.EdgeWidth = 0.02f;
	Ring.Intensity = 3.f;
	Shape(To + FVector(0.f, 0.f, 4.f), 0.f, 600.f * 2.f, Ring, Catalog.ThrownGlow, 0.8f, FMath::Max(0.05f, FlightSeconds));
}

void UVHWeaponFxSubsystem::OnExplosion(int32 Kind, const FVector& Pos, float Radius)
{
	UWorld* World = GetWorld();
	const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
	const bool bFlash = Kind == 3;
	// 날아가던 것이 터졌다 → 그 종류의 가장 오래된 비행체를 지운다
	Projectiles.RemoveAll([](const FProjectile& P) { return !P.Fx.IsValid(); });
	for (int32 i = 0; i < Projectiles.Num(); ++i)
	{
		if (Projectiles[i].bFlash == bFlash)
		{
			Projectiles[i].Fx->Destroy();
			Projectiles.RemoveAt(i);
			break;
		}
	}
	if (bFlash)
	{
		// 섬광탄: 지금 그대로 (흰색)
		AVHFxActor::SpawnFx(World, Pos + FVector(0.f, 0.f, 4.f), EVHFxShape::Disc, FVector(Radius * 2.f), FLinearColor::White, 0.35f, 0.6f);
		if (AVHFxActor* Ball = AVHFxActor::SpawnFx(World, Pos + FVector(0.f, 0.f, 40.f), EVHFxShape::Sphere, FVector(Radius * 0.5f), FLinearColor::White, 0.6f, 0.45f))
		{
			Ball->SetGrow(3.f);
		}
		return;
	}
	const FLinearColor Color = Kind == 0 ? Catalog.ThrownGlow : Catalog.PlacedGlow;
	// 바닥 범위 원: 판정 반지름 그대로
	FVHFxShapeParams Ring;
	Ring.FillOpacity = Kind == 2 ? 0.f : 0.12f;	// 감전 지뢰는 바닥 채우기 대신 번개 줄기로 구분 (사용자 요청 2026-11-17)
	Ring.EdgeOpacity = 1.f;
	Ring.EdgeWidth = 0.035f;
	Ring.Intensity = 5.f;
	if (AVHFxActor* Fx = Shape(Pos + FVector(0.f, 0.f, 5.f), 0.f, Radius * 2.f, Ring, Color, 0.9f, 0.8f))
	{
		Fx->SetLife(0.8f, 0.6f);
	}
	// 충격파 링 (2026-11-19): 가운데서 판정 반지름까지 0.3초에 퍼지며 사라지는 밝은 고리 + 섬광 조명
	{
		FVHFxShapeParams Wave;
		Wave.FillOpacity = 0.f;
		Wave.EdgeOpacity = 1.f;
		Wave.EdgeWidth = 0.06f;
		Wave.Intensity = 7.f;
		const FLinearColor WaveColor = Kind == 2 ? FLinearColor(0.6f, 0.85f, 1.f) : FLinearColor(1.f, 0.85f, 0.6f);
		if (AVHFxActor* W = Shape(Pos + FVector(0.f, 0.f, 12.f), 0.f, Radius * 0.4f, Wave, WaveColor, 1.f, 0.32f))
		{
			W->SetGrow(5.f);	// 0.2R → 1.0R (지름 0.4R → 2R)
			W->SetLife(0.32f, 0.2f);
		}
		Flash(Pos + FVector(0.f, 0.f, 120.f), Kind == 2 ? FLinearColor(0.45f, 0.7f, 1.f) : FLinearColor(1.f, 0.6f, 0.25f),
			Kind == 0 ? 60000.f : 35000.f, Radius * 1.6f, Kind == 2 ? 0.25f : 0.18f);
	}
	if (Kind == 2)
	{
		// 감전 지뢰: 판정 반지름 안에서 번개 줄기가 여러 갈래로 튄다 (0.6초 동안 0.06초마다 모양이 바뀜)
		FTimedFx T;
		T.Kind = FTimedFx::ShockBolts;
		T.A = Pos + FVector(0.f, 0.f, 30.f);
		T.Radius = Radius;
		T.Color = Color;
		T.Every = 0.06f;
		T.Until = World->GetTimeSeconds() + 0.6f;
		Timed.Add(T);
		FVHFxShapeParams Glow;
		Glow.Glow = 2.f;
		Glow.Intensity = 6.f;
		Shape(Pos + FVector(0.f, 0.f, 8.f), 0.f, 120.f, Glow, FLinearColor(0.85f, 1.f, 0.6f), 1.f, 0.3f);
		return;
	}
	// 폭발(수류탄·폭발 지뢰 같은 모양, 바닥 원 색만 다름): 주황 불덩이 + 어두운 연기 + 나이아가라 불꽃(주황). 모두 판정 반지름 안쪽
	const FLinearColor Fire(1.f, 0.32f, 0.02f);
	SpawnNiagara(Catalog.ExplosionNiagara, Pos + FVector(0.f, 0.f, 20.f), FRotator::ZeroRotator, Fire, Radius);
	FVHFxShapeParams Ball;
	Ball.Glow = 0.6f;
	Ball.Intensity = 1.3f;
	if (AVHFxActor* Fx = Shape(Pos + FVector(0.f, 0.f, 30.f), 0.f, Radius * 0.55f, Ball, Fire, 1.f, 0.55f))
	{
		Fx->SetGrow(1.5f);	// 지름 0.55R → 0.83R
	}
	FVHFxShapeParams Core;
	Core.Glow = 1.6f;
	Core.Intensity = 3.f;
	Shape(Pos + FVector(0.f, 0.f, 40.f), 0.f, Radius * 0.3f, Core, FLinearColor(1.f, 0.85f, 0.35f), 1.f, 0.3f);
	for (int32 k = 0; k < 7; ++k)
	{
		// 어두운 연기: 불덩이 둘레에서 천천히 커지며 흐려진다
		const FVector P = Pos + FRotator(0.f, k * 51.f + FMath::FRandRange(-15.f, 15.f), 0.f).Vector() * Radius * FMath::FRandRange(0.1f, 0.38f) + FVector(0.f, 0.f, 50.f + k);
		FVHFxShapeParams Smoke;
		Smoke.Glow = 0.45f;
		Smoke.Intensity = 0.f;
		if (AVHFxActor* Fx = Shape(P, 0.f, Radius * FMath::FRandRange(0.26f, 0.36f), Smoke, FLinearColor(0.03f, 0.025f, 0.02f), 0.9f, 1.4f))
		{
			Fx->SetLife(1.4f, 1.1f);
			Fx->SetGrow(1.6f);
		}
	}
}

AVHFxActor* UVHWeaponFxSubsystem::SpawnMine(bool bShock, const FVector& Pos, float Yaw)
{
	UWorld* World = GetWorld();
	const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
	const uint8 Id = bShock ? 10 : 9;
	const FVHWeaponVisual* Visual = Catalog.Find(Id);
	UStaticMesh* Model = Visual ? Visual->Mesh.LoadSynchronous() : nullptr;
	const float Scale = Catalog.FloorScale * (Visual ? Visual->FloorScale : 1.f);
	AVHFxActor* Mine = Model ? AVHFxActor::SpawnModel(World, Model, Visual->MeshToWeapon(Model, FVector::OneVector, false)
		* FTransform(FRotator(0.f, Yaw, 0.f), Pos + FVector(0.f, 0.f, 1.f), FVector(Scale))) : nullptr;
	if (!Mine)
	{
		// 모델이 없으면 예전처럼 작은 원기둥
		return AVHFxActor::SpawnFx(World, Pos + FVector(0.f, 0.f, 5.f), EVHFxShape::Cylinder, FVector(44.f, 44.f, 10.f), Catalog.PlacedGlow, 0.4f);
	}
	if (Visual)
	{
		for (int32 i = 0; i < Visual->MaterialOverrides.Num(); ++i)
		{
			if (UMaterialInterface* M = Visual->MaterialOverrides[i].LoadSynchronous())
			{
				Mine->FindComponentByClass<UStaticMeshComponent>()->SetMaterial(i, M);
			}
		}
	}
	// 경고 링: 감지 반지름(100cm) 그대로
	FVHFxShapeParams Ring;
	Ring.FillOpacity = 0.06f;
	Ring.EdgeOpacity = 1.f;
	Ring.EdgeWidth = 0.08f;
	Ring.Intensity = 5.f;
	if (AVHFxActor* RingFx = Shape(Pos + FVector(0.f, 0.f, 3.f), 0.f, VHFx::MineDetect * 2.f, Ring, Catalog.PlacedGlow, 0.35f, 0.f))
	{
		Mine->AddChildFx(RingFx);
		MineRings.Add(Mine, RingFx);
		VHFx::GMines.FindOrAdd(Mine).bShock = bShock;
		RingFx->SetActorHiddenInGame(true);
	}
	// 내려놓는 동작에서 손이 바닥에 닿는 순간(약 0.32초)에 보인다
	Mine->SetActorHiddenInGame(true);
	TWeakObjectPtr<AVHFxActor> WeakMine(Mine);
	TWeakObjectPtr<UVHWeaponFxSubsystem> WeakThis(this);
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakMine, WeakThis]()
	{
		if (AVHFxActor* M = WeakMine.Get())
		{
			M->SetActorHiddenInGame(false);
			if (UVHWeaponFxSubsystem* Self = WeakThis.Get())
			{
				if (const TWeakObjectPtr<AVHFxActor>* Ring = Self->MineRings.Find(WeakMine); Ring && Ring->IsValid())
				{
					(*Ring)->SetActorHiddenInGame(false);
				}
			}
		}
	}), 0.3f, false);
	return Mine;
}

void UVHWeaponFxSubsystem::ApplyMineState(AVHFxActor* Mine, uint32 State)
{
	constexpr uint32 Armed = 1u << 18, Triggered = 1u << 19;	// RobProto ES_ARMED, ES_TRIGGERED
	const TWeakObjectPtr<AVHFxActor>* RingPtr = MineRings.Find(Mine);
	AVHFxActor* Ring = RingPtr ? RingPtr->Get() : nullptr;
	if (!Mine || !Ring)
	{
		return;
	}
	const FLinearColor Color = UVHSettings::Get().GetWeaponCatalog().PlacedGlow;
	// 소리 (2026-11-19): 상태가 바뀌는 순간만 — 감지 = "삑삑삑"(0.5초 경고), 감전 지뢰 작동 = 낮은 웅웅 반복(작게)
	VHFx::FMineInfo& Info = VHFx::GMines.FindOrAdd(Mine);
	const uint32 Was = Info.State;
	Info.State = State;
	if ((State & Triggered) && !(Was & Triggered))
	{
		UVHSoundConfig::Play(this, EVHSound::MineWarn, Mine->GetActorLocation());
		if (UAudioComponent* Hum = Info.Hum.Get()) { Hum->FadeOut(0.1f, 0.f); }
	}
	else if ((State & Armed) && !(State & Triggered) && Info.bShock && !Info.Hum.IsValid())
	{
		Info.Hum = UVHSoundConfig::PlayLoop(this, EVHSound::ShockMineHum, Mine->GetRootComponent());
	}
	if (State & Triggered)
	{
		// 감지됨: 0.5초 동안 빠르게 빨강 깜빡 + 폭발 판정 반지름(300cm) 링
		Ring->SetFxColor(FLinearColor(1.f, 0.12f, 0.08f), 1.f);
		Ring->SetPulse(8.f);
		if (!MineBigRings.Contains(Mine))
		{
			FVHFxShapeParams Big;
			Big.FillOpacity = 0.08f;
			Big.EdgeOpacity = 1.f;
			Big.EdgeWidth = 0.03f;
			Big.Intensity = 5.f;
			if (AVHFxActor* BigRing = Shape(Mine->GetActorLocation() + FVector(0.f, 0.f, 3.f), 0.f, VHFx::MineRadius * 2.f, Big, Color, 0.9f, 0.f))
			{
				BigRing->SetPulse(8.f);
				Mine->AddChildFx(BigRing);
				MineBigRings.Add(Mine, BigRing);
			}
		}
	}
	else if (State & Armed)
	{
		Ring->SetFxColor(Color, 0.9f);	// 작동 중: 1초 주기 깜빡임
		Ring->SetPulse(1.f);
	}
	else
	{
		Ring->SetFxColor(Color, 0.3f);	// 작동 전 3초: 흐리게
		Ring->SetPulse(0.5f);
	}
}

AVHFxActor* UVHWeaponFxSubsystem::SpawnSmoke(const FVector& Pos, float Radius, float Life)
{
	// 바닥의 옅은 원(연막 반지름 그대로) + 나이아가라 연기. 없으면 예전 원기둥
	FVHFxShapeParams Ring;
	Ring.FillOpacity = 0.12f;
	Ring.EdgeOpacity = 0.5f;
	Ring.EdgeWidth = 0.03f;
	Ring.Intensity = 0.6f;
	const FLinearColor Grey(0.55f, 0.57f, 0.6f);
	AVHFxActor* Root = Shape(Pos + FVector(0.f, 0.f, 3.f), 0.f, Radius * 2.f, Ring, Grey, 0.8f, Life);
	if (!Root)
	{
		return nullptr;
	}
	if (Life > 0.f)
	{
		Root->SetLife(Life, 1.5f);
	}
	if (!SpawnNiagara(UVHSettings::Get().GetWeaponCatalog().SmokeNiagara, Pos, FRotator::ZeroRotator, Grey, Radius, Root->GetRootComponent()))
	{
		if (AVHFxActor* Cyl = AVHFxActor::SpawnFx(GetWorld(), Pos + FVector(0.f, 0.f, 130.f), EVHFxShape::Cylinder, FVector(Radius * 2.f, Radius * 2.f, 260.f), Grey, 0.55f, Life))
		{
			Root->AddChildFx(Cyl);
		}
	}
	return Root;
}

// ───────────────── 매 프레임 ─────────────────

void UVHWeaponFxSubsystem::Tick(float DeltaTime)
{
	UpdateHeldWeapons(DeltaTime);
	UpdateTimed(DeltaTime);
	UpdateLoops(DeltaTime);
}

void UVHWeaponFxSubsystem::UpdateLoops(float DeltaTime)
{
	UWorld* World = GetWorld();
	const float Now = World->GetTimeSeconds();
	// 섬광 조명: 밝기가 시간에 따라 줄어들고 끝나면 없앤다
	for (int32 i = Flashes.Num() - 1; i >= 0; --i)
	{
		FFlashLight& F = Flashes[i];
		const float T = (Now - F.Start) / FMath::Max(F.Seconds, 0.01f);
		UPointLightComponent* L = F.Light.Get();
		if (!L || T >= 1.f)
		{
			if (AActor* A = F.Actor.Get()) { A->Destroy(); }
			Flashes.RemoveAt(i);
			continue;
		}
		L->SetIntensity(F.Intensity * FMath::Square(1.f - T));
	}
	// 감전 지뢰 작동 중: 지뢰 위 작은 푸른 불꽃 (0.5초마다), 없어진 지뢰는 표에서 뺀다
	for (auto It = VHFx::GMines.CreateIterator(); It; ++It)
	{
		AVHFxActor* M = It.Key().Get();
		if (!M)
		{
			It.RemoveCurrent();
			continue;
		}
		VHFx::FMineInfo& Info = It.Value();
		if (Info.bShock && (Info.State & (1u << 18)) && Now >= Info.NextSpark && M->GetWorld() == World)
		{
			Info.NextSpark = Now + 0.5f;
			HitSpark(FLinearColor(0.5f, 0.8f, 1.f), M->GetActorLocation() + FVector(FMath::FRandRange(-12.f, 12.f), FMath::FRandRange(-12.f, 12.f), 14.f), 8.f);
		}
	}
	// 빔 소드 웅—: 들고 보이는 동안 반복 (0.25초마다 확인)
	HumScanTimer -= DeltaTime;
	if (HumScanTimer > 0.f)
	{
		return;
	}
	HumScanTimer = 0.25f;
	for (auto It = Hums.CreateIterator(); It; ++It)
	{
		UVHWeaponVisualComponent* V = It.Key().Get();
		UAudioComponent* A = It.Value().Get();
		if (!V || !A || V->GetWeapon() != 3 || !V->IsWeaponShown())
		{
			if (A) { A->FadeOut(0.2f, 0.f); }
			It.RemoveCurrent();
		}
	}
	if (!UVHSoundConfig::Has(EVHSound::BeamHum))
	{
		return;
	}
	for (TObjectIterator<UVHWeaponVisualComponent> It; It; ++It)
	{
		UVHWeaponVisualComponent* V = *It;
		if (!IsValid(V) || V->GetWorld() != World || !IsValid(V->GetOwner()) || V->GetOwner()->IsActorBeingDestroyed()
			|| V->GetWeapon() != 3 || !V->IsWeaponShown() || Hums.Contains(V))
		{
			continue;
		}
		if (UAudioComponent* A = UVHSoundConfig::PlayLoop(this, EVHSound::BeamHum, V->GetWeaponMesh()))
		{
			Hums.Add(V, A);
		}
	}
}

void UVHWeaponFxSubsystem::UpdateHeldWeapons(float DeltaTime)
{
	UWorld* World = GetWorld();
	// 꾹 누르는 무기 찾기 (0.1초마다: 화염방사기·레일건을 꾹 누르는 캐릭터)
	HeldScanTimer -= DeltaTime;
	if (HeldScanTimer <= 0.f)
	{
		HeldScanTimer = 0.05f;
		for (TObjectIterator<UVHWeaponVisualComponent> It; It; ++It)
		{
			UVHWeaponVisualComponent* V = *It;
			if (V->GetWorld() != World || V->GetHoldAlpha() < 0.3f || !V->IsWeaponShown())
			{
				continue;
			}
			const uint8 W = V->GetWeapon();
			if (W != 5 || Held.ContainsByPredicate([V](const FHeldFx& H) { return H.Visual.Get() == V; }))
			{
				continue;	// (레일건 충전은 OnRailgunChargeChanged 가 바로 시작한다: 클릭 순간 소리가 나야 해서 0.05초 검사를 기다리지 않음)
			}
			FHeldFx H;
			H.Visual = V;
			H.Weapon = W;
			if (W == 5)
			{
				// 화염방사기 소리: 시작 한 번 + 누르는 동안 반복 (끝은 아래에서 뗄 때)
				UVHSoundConfig::Play(this, EVHSound::FlameStart, V->GetOwner()->GetActorLocation());
				H.Loop = UVHSoundConfig::PlayLoop(this, EVHSound::FlameLoop, V->GetWeaponMesh());
			}
			Held.Add(H);
		}
	}
	const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
	for (int32 i = Held.Num() - 1; i >= 0; --i)
	{
		FHeldFx& H = Held[i];
		UVHWeaponVisualComponent* V = H.Visual.Get();
		// 레일건: 충전 중인 동안만 (발사(SC_FIRE)·취소 순간에 충전 표시가 꺼진다). 화염방사기: 꾹 누르는 자세가 켜져 있는 동안
		const bool bKeep = V && V->GetWeapon() == H.Weapon && V->IsWeaponShown()
			&& (H.Weapon == 6 ? V->IsRailgunCharging() : V->GetHoldAlpha() >= 0.3f);
		if (!bKeep)
		{
			EndHeldFx(H);
			Held.RemoveAt(i);
			continue;
		}
		H.Age += DeltaTime;
		const AActor* Owner = V->GetOwner();
		const float Yaw = Owner->GetActorRotation().Yaw;
		const FVector Feet = VHFx::FeetOf(Owner);
		FVector Muzzle;
		V->GetMuzzleLocation(Muzzle);
		if (H.Weapon == 5)
		{
			// 화염방사기: 판정 부채꼴(발 위치에서 600cm, 반각 15°)을 옅게 + 총구에서 나가는 불꽃 줄기
			const FVector FanCenter(Feet.X, Feet.Y, Muzzle.Z - 30.f);
			if (!H.Fan.IsValid())
			{
				FVHFxShapeParams P;
				// 도형 방식: 판정 부채꼴 자체를 불꽃 색으로 일렁이게 (불꽃 줄기의 몸통)
				// 나이아가라 불꽃이 있으면 판정 범위만 옅은 테두리로 (2026-11-20: 꽉 찬 주황 부채꼴이 불꽃을 가려 솜뭉치처럼 보였다)
				const bool bNiagaraFlame = UseNiagara(Catalog.FlameNiagara);
				P.HalfAngle = VHFx::FlameHalfAngle;
				P.FillOpacity = bNiagaraFlame ? 0.06f : 0.6f;
				P.EdgeOpacity = bNiagaraFlame ? 0.45f : 1.f;
				P.EdgeWidth = bNiagaraFlame ? 0.02f : 0.05f;
				P.Inner = 0.12f;
				P.Intensity = bNiagaraFlame ? 1.2f : 1.6f;
				H.Fan = Shape(FanCenter, Yaw, VHFx::FlameRange * 2.f, P, FLinearColor(0.9f, 0.22f, 0.01f), bNiagaraFlame ? 0.6f : 0.95f, 0.f);
				if (AVHFxActor* F = H.Fan.Get())
				{
					F->SetPulse(9.f);
				}
			}
			if (AVHFxActor* F = H.Fan.Get())
			{
				F->SetActorLocationAndRotation(FanCenter, FRotator(0.f, Yaw + PlaneYawOffset, 0.f));
			}
			const float Length = FMath::Max(100.f, VHFx::FlameRange - FVector::Dist2D(Feet, Muzzle));
			if (!H.Niagara.IsValid() && UseNiagara(Catalog.FlameNiagara))
			{
				H.Niagara = SpawnNiagara(Catalog.FlameNiagara, Muzzle, FRotator(0.f, Yaw, 0.f), Catalog.FlameColor, Length);
			}
			if (UNiagaraComponent* N = H.Niagara.Get())
			{
				N->SetWorldLocationAndRotation(Muzzle, FRotator(0.f, Yaw, 0.f));
				N->SetVariableFloat(TEXT("User.Radius"), Length);
				N->SetVariableVec3(TEXT("User.Dir"), FVector::ForwardVector);	// 뿜는 방향: 이미터 공간(Local) 앞 = 컴포넌트 회전(조준) 방향. 월드 방향을 넣으면 두 번 돌아간다 (2026-11-19 화면 확인)
			}
			else if (H.Age >= H.NextFlicker)
			{
				// 도형: 총구에서 앞으로 흘러가는 밝은 불꽃 덩어리 (가운데가 밝은 원, 판정 부채꼴 안쪽). 멀수록 크고 흐리다
				H.NextFlicker = H.Age + 0.025f;
				for (int32 k = 0; k < 3; ++k)
				{
					// 총구에 가까울수록(가운데) 밝은 노랑-흰색, 멀수록 진한 주황 (판정 부채꼴 안쪽)
					const float T = FMath::FRand();
					const float D = FMath::Lerp(0.06f, 0.95f, T) * Length;
					const float Spread = FMath::FRandRange(-0.6f, 0.6f) * VHFx::FlameHalfAngle * (1.f - 0.5f * T);
					const FVector P = Muzzle + FRotator(0.f, Yaw + Spread, 0.f).Vector() * D;
					FVHFxShapeParams G;
					G.Glow = 1.2f;
					G.Intensity = FMath::Lerp(2.4f, 1.3f, T);
					const FLinearColor C = T < 0.5f ? FMath::Lerp(FLinearColor(1.f, 0.95f, 0.75f), FLinearColor(1.f, 0.6f, 0.12f), T * 2.f)
						: FMath::Lerp(FLinearColor(1.f, 0.6f, 0.12f), FLinearColor(0.85f, 0.25f, 0.02f), (T - 0.5f) * 2.f);
					if (AVHFxActor* Puff = Shape(P + FVector(0.f, 0.f, 4.f), 0.f, 26.f + D * 0.2f, G, C, 1.f, 0.16f))
					{
						Puff->SetGrow(1.6f);
					}
				}
				{
					// 그을음: 바깥쪽·가장자리에 반투명 어두운 덩어리 (밝은 바닥에서 불꽃 윤곽이 보이게)
					const float T = FMath::FRandRange(0.4f, 1.f);
					const float D = T * Length;
					const float Spread = (FMath::RandBool() ? 1.f : -1.f) * FMath::FRandRange(0.55f, 0.9f) * VHFx::FlameHalfAngle;
					const FVector P = Muzzle + FRotator(0.f, Yaw + Spread, 0.f).Vector() * D;
					FVHFxShapeParams G;
					G.Glow = 0.5f;
					G.Intensity = 0.f;
					if (AVHFxActor* Soot = Shape(P, 0.f, 34.f + D * 0.24f, G, FLinearColor(0.04f, 0.03f, 0.02f), 0.85f, 0.3f))
					{
						Soot->SetLife(0.3f, 0.25f);
						Soot->SetGrow(1.7f);
					}
				}
			}
		}
		else
		{
			UpdateRailgunCharge(H, Muzzle, Yaw, V->GetRailgunChargeProgress(), DeltaTime);
		}
	}
}

void UVHWeaponFxSubsystem::OnRailgunChargeChanged(UVHWeaponVisualComponent* V, bool bCharging)
{
	if (!V)
	{
		return;
	}
	const int32 Index = Held.IndexOfByPredicate([V](const FHeldFx& H) { return H.Visual.Get() == V && H.Weapon == 6; });
	if (!bCharging)
	{
		if (Index != INDEX_NONE)
		{
			EndHeldFx(Held[Index]);	// 바로 끊는다 (발사음·빔이 같은 프레임에 나온다)
			Held.RemoveAt(Index);
		}
		return;
	}
	if (Index != INDEX_NONE || !V->IsWeaponShown())
	{
		return;
	}
	FHeldFx H;
	H.Visual = V;
	H.Weapon = 6;
	// 충전음: 무기에 붙여서 (끊을 수 있게 겹마다 소리 장치). 칸 = 레일건 충전 (DA_VHSoundConfig)
	for (UAudioComponent* A : UVHSoundConfig::PlayAttached(this, EVHSound::RailgunCharge, V->GetWeaponMesh()))
	{
		H.ChargeSounds.Add(A);
	}
	Held.Add(H);
	UE_LOG(LogVHWeaponFx, Log, TEXT("[레일건] 충전 이펙트·소리 시작 (%s, 소리 %d겹)"), *GetNameSafe(V->GetOwner()), H.ChargeSounds.Num());
}

void UVHWeaponFxSubsystem::EndHeldFx(FHeldFx& H)
{
	if (UNiagaraComponent* N = H.Niagara.Get()) { N->Deactivate(); }	// 자동으로 끝나면 풀로 돌아간다
	if (AVHFxActor* F = H.Fan.Get()) { F->Destroy(); }
	if (AVHFxActor* O = H.Orb.Get()) { O->Destroy(); }
	if (AVHFxActor* R = H.Ring.Get()) { R->Destroy(); }
	if (AActor* L = H.Light.Get()) { L->Destroy(); }
	for (FHeldFx::FSpark& S : H.Sparks)
	{
		if (AVHFxActor* A = S.Actor.Get()) { A->Destroy(); }
	}
	H.Sparks.Reset();
	if (UAudioComponent* A = H.Loop.Get())
	{
		A->FadeOut(0.12f, 0.f);
		UVHSoundConfig::Play(this, EVHSound::FlameEnd, A->GetComponentLocation());
	}
	for (const TWeakObjectPtr<UAudioComponent>& C : H.ChargeSounds)
	{
		if (UAudioComponent* A = C.Get())
		{
			A->FadeOut(0.06f, 0.f);	// 충전음은 짧게 끊는다 (발사 순간이면 발사음이 이어받는다)
		}
	}
	H.ChargeSounds.Reset();
}

void UVHWeaponFxSubsystem::UpdateRailgunCharge(FHeldFx& H, const FVector& Muzzle, float Yaw, float Progress, float DeltaTime)
{
	// 레일건 충전 (1초): 총구의 빛 덩어리가 점점 커지고 밝아지며, 주위의 빛 알갱이가 총구로 빨려 들어가고, 고리가 총구로 줄어든다.
	// 탑뷰에서 보이게 도형은 바닥과 나란히 눕힌다. 끝에 가까울수록 빠르고 밝다
	const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
	const FLinearColor Color = Catalog.RangedGlow;
	const float Ease = FMath::Pow(Progress, 0.7f);
	// 1) 가운데 빛 덩어리
	if (!H.Orb.IsValid())
	{
		FVHFxShapeParams P;
		P.Glow = 1.6f;
		P.Intensity = 8.f;
		H.Orb = Shape(Muzzle, Yaw, 40.f, P, Color, 1.f, 0.f);
	}
	if (AVHFxActor* O = H.Orb.Get())
	{
		O->SetActorLocation(Muzzle);
		const float D = FMath::Lerp(10.f, 54.f, Ease) * (1.f + (0.08f + 0.12f * Progress) * FMath::Sin(H.Age * (30.f + 40.f * Progress)));
		O->SetActorScale3D(FVector(D / 100.f, D / 100.f, 1.f));
		O->SetShapeScalar(TEXT("Intensity"), FMath::Lerp(4.f, 14.f, Progress));
	}
	// 2) 총구로 줄어드는 고리 (0.3초마다 새로, 끝에 가까울수록 빠르게)
	{
		const float Cycle = FMath::Lerp(0.34f, 0.16f, Progress);
		const float U = FMath::Fmod(H.Age, Cycle) / Cycle;
		if (!H.Ring.IsValid())
		{
			FVHFxShapeParams P;
			P.FillOpacity = 0.f;
			P.EdgeOpacity = 1.f;
			P.EdgeWidth = 0.1f;
			P.Intensity = 6.f;
			H.Ring = Shape(Muzzle, Yaw, 100.f, P, Color, 0.9f, 0.f);
		}
		if (AVHFxActor* R = H.Ring.Get())
		{
			R->SetActorLocation(Muzzle);
			const float D = FMath::Lerp(FMath::Lerp(110.f, 80.f, Progress), 16.f, U * U);
			R->SetActorScale3D(FVector(D / 100.f, D / 100.f, 1.f));
			R->SetFxColor(Color, FMath::Lerp(0.25f, 0.95f, Progress) * FMath::Sin(PI * FMath::Min(U * 1.3f, 1.f)));
		}
	}
	// 3) 빨려 들어가는 빛 알갱이
	H.NextSpark -= DeltaTime;
	while (H.NextSpark <= 0.f && H.Sparks.Num() < 18)
	{
		H.NextSpark += FMath::Lerp(0.05f, 0.015f, Progress);
		FHeldFx::FSpark S;
		const float Ang = FMath::FRandRange(0.f, 2.f * PI);
		const float Dist = FMath::FRandRange(40.f, 75.f) * FMath::Lerp(1.f, 0.75f, Progress);
		S.From = FVector(FMath::Cos(Ang) * Dist, FMath::Sin(Ang) * Dist, FMath::FRandRange(-12.f, 18.f));
		S.Life = FMath::Lerp(0.26f, 0.16f, Progress);
		FVHFxShapeParams P;
		P.Glow = 1.4f;
		P.Intensity = 9.f;
		S.Actor = Shape(Muzzle + S.From, 0.f, FMath::FRandRange(7.f, 11.f), P, FMath::Lerp(Color, FLinearColor::White, 0.35f), 1.f, 0.f);
		H.Sparks.Add(S);
	}
	for (int32 k = H.Sparks.Num() - 1; k >= 0; --k)
	{
		FHeldFx::FSpark& S = H.Sparks[k];
		S.Age += DeltaTime;
		AVHFxActor* A = S.Actor.Get();
		if (!A || S.Age >= S.Life)
		{
			if (A) { A->Destroy(); }
			H.Sparks.RemoveAt(k);
			continue;
		}
		const float T = S.Age / S.Life;
		A->SetActorLocation(Muzzle + S.From * (1.f - T * T));	// 처음엔 천천히, 총구 가까이에서 빨라진다
	}
	// 4) 총구 빛 (점점 밝게)
	if (!H.Light.IsValid())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AActor* LA = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Muzzle), Params))
		{
			UPointLightComponent* L = NewObject<UPointLightComponent>(LA);
			L->SetMobility(EComponentMobility::Movable);
			LA->SetRootComponent(L);
			L->RegisterComponent();
			L->SetCastShadows(false);
			L->SetIntensityUnits(ELightUnits::Unitless);
			L->SetLightColor(Color);
			L->SetAttenuationRadius(260.f);
			L->SetIntensity(0.f);
			H.Light = LA;
		}
	}
	if (AActor* LA = H.Light.Get())
	{
		LA->SetActorLocation(Muzzle);
		if (UPointLightComponent* L = Cast<UPointLightComponent>(LA->GetRootComponent()))
		{
			L->SetIntensity(FMath::Lerp(200.f, 3500.f, Ease) * (1.f + 0.15f * FMath::Sin(H.Age * 45.f)));
		}
	}
}

void UVHWeaponFxSubsystem::UpdateTimed(float DeltaTime)
{
	UWorld* World = GetWorld();
	const float Now = World->GetTimeSeconds();
	TArray<TFunction<void()>> Due;	// 늦게 부르는 것 (부르는 동안 Timed 가 늘 수 있어 반복문 밖에서 부른다)
	for (int32 i = Timed.Num() - 1; i >= 0; --i)
	{
		FTimedFx& T = Timed[i];
		if (Now >= T.Next)
		{
			T.Next = Now + T.Every;
			switch (T.Kind)
			{
			case FTimedFx::Delayed:
				if (T.Fn) { Due.Add(MoveTemp(T.Fn)); }
				T.Until = 0.f;
				break;
			case FTimedFx::TaserBolt:
				Bolt(T.A, T.B, T.Color, 5.f, 9.f, T.Every * 1.2f);
				Bolt(T.A, T.B, T.Color * 0.8f, 3.f, 14.f, T.Every * 1.2f);
				break;
			case FTimedFx::Stun:
			{
				AActor* Target = T.Target.Get();
				if (Target && Target->IsHidden())
				{
					T.Until = 0.f;	// 숨은 대상(은신·연막·쓰러짐)은 그리지 않는다
					break;
				}
				const FVector Feet = Target ? VHFx::FeetOf(Target) : T.A;
				for (int32 k = 0; k < 2; ++k)
				{
					const FVector A = Feet + FVector(FMath::FRandRange(-1.f, 1.f) * VHFx::BodyRadius, FMath::FRandRange(-1.f, 1.f) * VHFx::BodyRadius, FMath::FRandRange(20.f, 90.f));
					const FVector B = Feet + FVector(FMath::FRandRange(-1.f, 1.f) * VHFx::BodyRadius, FMath::FRandRange(-1.f, 1.f) * VHFx::BodyRadius, FMath::FRandRange(100.f, 175.f));
					Bolt(A, B, T.Color, 5.f, 10.f, T.Every * 1.1f);
				}
				break;
			}
			case FTimedFx::Burn:
			{
				AActor* Target = T.Target.Get();
				if (Target && Target->IsHidden())
				{
					T.Until = 0.f;
					break;
				}
				const FVector Feet = Target ? VHFx::FeetOf(Target) : T.A;
				const FVector P = Feet + FVector(FMath::FRandRange(-25.f, 25.f), FMath::FRandRange(-25.f, 25.f), FMath::FRandRange(20.f, 110.f));
				HitSpark(T.Color, P, 18.f);
				break;
			}
			case FTimedFx::Afterimage:
			{
				UVHWeaponVisualComponent* V = T.Visual.Get();
				UStaticMeshComponent* Mesh = V ? V->GetWeaponMesh() : nullptr;
				if (Mesh && Mesh->GetStaticMesh() && V->IsWeaponShown())
				{
					if (AVHFxActor* Ghost = AVHFxActor::SpawnModel(World, Mesh->GetStaticMesh(), Mesh->GetComponentTransform(), 0.12f))
					{
						Ghost->TintModel(T.Color, 0.45f);
						Ghost->SetLife(0.12f, 0.12f);
					}
				}
				break;
			}
			case FTimedFx::ShockBolts:
				for (int32 k = 0; k < 10; ++k)
				{
					// 가운데에서 바깥으로 튀는 번개 + 중간에서 갈라지는 곁가지 (판정 반지름 안쪽)
					const float Ang = k * 36.f + FMath::FRandRange(-18.f, 18.f);
					const float Len = T.Radius * FMath::FRandRange(0.45f, 0.95f);
					const FVector End = T.A + FRotator(0.f, Ang, 0.f).Vector() * Len - FVector(0.f, 0.f, 20.f);
					Bolt(T.A, End, T.Color, 5.f, 26.f, T.Every * 1.1f);
					const FVector Mid = FMath::Lerp(T.A, End, FMath::FRandRange(0.35f, 0.6f));
					const FVector Branch = Mid + FRotator(0.f, Ang + FMath::FRandRange(-45.f, 45.f), 0.f).Vector() * Len * 0.35f;
					Bolt(Mid, Branch, FLinearColor(0.9f, 1.f, 0.7f), 3.f, 14.f, T.Every * 1.1f);
				}
				break;
			case FTimedFx::Projectile:
			{
				// 수류탄 출발 (손을 떠나는 순간)
				const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
				const FVHWeaponVisual* Visual = Catalog.Find(8);
				UStaticMesh* Model = Visual ? Visual->Mesh.LoadSynchronous() : nullptr;
				FVector From = T.A;
				if (UVHWeaponVisualComponent* V = T.Visual.Get())
				{
					if (UStaticMeshComponent* Hand = V->GetWeaponMesh())
					{
						From = Hand->GetComponentLocation();
					}
				}
				AVHFxActor* Fx = Model
					? AVHFxActor::SpawnModel(World, Model, Visual->MeshToWeapon(Model, FVector::OneVector, false)
						* FTransform(FRotator::ZeroRotator, From, FVector(Catalog.HeldScale * Visual->HeldScale)))
					: AVHFxActor::SpawnFx(World, From, EVHFxShape::Sphere, FVector(26.f), Catalog.ThrownGlow, 1.f);
				if (Fx)
				{
					if (Visual)
					{
						for (int32 m = 0; m < Visual->MaterialOverrides.Num(); ++m)
						{
							if (UMaterialInterface* Mat = Visual->MaterialOverrides[m].LoadSynchronous())
							{
								Fx->FindComponentByClass<UStaticMeshComponent>()->SetMaterial(m, Mat);
							}
						}
					}
					Fx->SetArc(From, T.B, 180.f, T.Param);
					Fx->SetSpin(FRotator(-540.f, 120.f, 0.f));
					Projectiles.Add({ Fx, false });
				}
				T.Until = 0.f;
				break;
			}
			}
		}
		if (Now >= T.Until)
		{
			Timed.RemoveAt(i);
		}
	}
	for (TFunction<void()>& Fn : Due)
	{
		Fn();
	}
}

#if !UE_BUILD_SHIPPING
namespace
{
	UVHWeaponVisualComponent* LocalVisual(UWorld* World)
	{
		for (TObjectIterator<UVHWeaponVisualComponent> It; It; ++It)
		{
			const APawn* Pawn = Cast<APawn>(It->GetOwner());
			if (It->GetWorld() == World && Pawn && Pawn->IsLocallyControlled())
			{
				return *It;
			}
		}
		return nullptr;
	}

	// vh.Fx N : 서버 없이 내 캐릭터로 N번 무기의 동작 + 이펙트를 재생한다 (화면 찍기용. slomo 0.05 와 같이 쓰면 천천히)
	//   1~3 근접(궤적), 4 권총, 5 화염방사기(2초 꾹), 6 레일건(충전 0.8초 → 발사 + 1초 버팀), 7 테이저, 8 수류탄(앞 4m) + 폭발,
	//   9·10 지뢰(설치 → 1초 뒤 작동 → 2초 뒤 감지 → 2.5초 뒤 폭발), hit N(앞 2m에 N번 무기 피격), smoke(앞 3m 연막 5초)
	FAutoConsoleCommandWithWorldAndArgs GVHFxCommand(TEXT("vh.Fx"),
		TEXT("개발용: vh.Fx N — 내 캐릭터로 N번 무기 동작+이펙트 재생. vh.Fx hit N / vh.Fx smoke."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UVHWeaponFxSubsystem* Fx = UVHWeaponFxSubsystem::Get(World);
			UVHWeaponVisualComponent* V = LocalVisual(World);
			if (!Fx || !V || Args.Num() == 0)
			{
				return;
			}
			AActor* Owner = V->GetOwner();
			const float Yaw = Owner->GetActorRotation().Yaw;
			const FVector Dir = FRotator(0.f, Yaw, 0.f).Vector();
			const FVector Feet = VHFx::FeetOf(Owner);
			FTimerManager& Timers = World->GetTimerManager();
			TWeakObjectPtr<UVHWeaponFxSubsystem> WeakFx(Fx);
			auto Later = [&Timers](float Seconds, TFunction<void()> Fn)
			{
				FTimerHandle H;
				Timers.SetTimer(H, FTimerDelegate::CreateLambda(MoveTemp(Fn)), Seconds, false);
			};
			if (Args[0].Equals(TEXT("hit"), ESearchCase::IgnoreCase))
			{
				const uint8 W = static_cast<uint8>(Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 4);
				Fx->OnDamage(W, nullptr, Feet + Dir * 200.f, false);
				return;
			}
			if (Args[0].Equals(TEXT("smoke"), ESearchCase::IgnoreCase))
			{
				Fx->SpawnSmoke(Feet + Dir * 300.f, 500.f, 5.f);
				return;
			}
			const uint8 W = static_cast<uint8>(FMath::Clamp(FCString::Atoi(*Args[0]), 1, 10));
			UVHWeaponVisualComponent::DebugFreezeTime = -1.f;
			UVHWeaponVisualComponent::DebugLocalWeapon = W;
			V->SetWeapon(W);
			TWeakObjectPtr<UVHWeaponVisualComponent> WeakV(V);
			switch (W)
			{
			case 5:
				UVHWeaponVisualComponent::DebugHold = 1;
				Later(2.f, []() { UVHWeaponVisualComponent::DebugHold = -1; });
				// 맞은 대상 흉내: 앞 3m 에 불붙음
				Later(0.3f, [WeakFx, Feet, Dir]() { if (WeakFx.IsValid()) { WeakFx->OnDamage(5, nullptr, Feet + Dir * 300.f, false); } });
				break;
			case 6:
				UVHWeaponVisualComponent::DebugHold = 1;
				Later(0.8f, [WeakV, WeakFx, Feet, Yaw]()
				{
					UVHWeaponVisualComponent::DebugHold = -1;
					UVHWeaponVisualComponent::DebugRailLock = 1;
					if (WeakV.IsValid() && WeakFx.IsValid())
					{
						WeakV->PlayAttack(6);
						WeakFx->OnFire(6, Feet, Yaw, nullptr, WeakV.Get());
					}
				});
				Later(1.8f, []() { UVHWeaponVisualComponent::DebugRailLock = -1; });
				break;
			case 8:
			{
				V->PlayAttack(8);
				const FVector To = Feet + Dir * 400.f;
				Fx->OnProjectile(false, Feet, To, 1.f, V);
				Later(1.f, [WeakFx, To]() { if (WeakFx.IsValid()) { WeakFx->OnExplosion(0, To, 600.f); } });
				break;
			}
			case 9: case 10:
			{
				V->PlayAttack(W);
				const bool bShock = W == 10;
				TWeakObjectPtr<AVHFxActor> Mine(Fx->SpawnMine(bShock, Feet, Yaw));
				Fx->ApplyMineState(Mine.Get(), 0);
				Later(1.f, [WeakFx, Mine]() { if (WeakFx.IsValid()) { WeakFx->ApplyMineState(Mine.Get(), 1u << 18); } });
				Later(2.f, [WeakFx, Mine]() { if (WeakFx.IsValid()) { WeakFx->ApplyMineState(Mine.Get(), (1u << 18) | (1u << 19)); } });
				Later(2.5f, [WeakFx, Mine, Feet, bShock]()
				{
					if (AVHFxActor* M = Mine.Get()) { M->Destroy(); }
					if (WeakFx.IsValid()) { WeakFx->OnExplosion(bShock ? 2 : 1, Feet, 300.f); }
				});
				break;
			}
			default:
				V->PlayAttack(W);
				Fx->OnFire(W, Feet, Yaw, nullptr, V);
				break;
			}
			UE_LOG(LogVHWeaponFx, Log, TEXT("[무기 이펙트] 무기 %d 재생"), W);
		}));

	// ───────── vh.WeaponTest (2026-11-19): 무기 하나의 동작·소리·이펙트·맞음을 서버 없이 한 번에 ─────────
	FTimerHandle GWeaponTestLoop;
	const TCHAR* const GWeaponNames[] = { TEXT("-"), TEXT("삼단봉"), TEXT("도검"), TEXT("빔 소드"), TEXT("권총"), TEXT("화염방사기"),
		TEXT("레일건"), TEXT("테이저건"), TEXT("수류탄"), TEXT("폭발 지뢰"), TEXT("감전 지뢰") };

	AVHNetProxy* WeaponTestDummy(UWorld* World, const FVector& Feet, float Yaw, uint8 Weapon, bool bCreate)
	{
		for (TActorIterator<AVHNetProxy> It(World); It; ++It)
		{
			if (It->ActorHasTag(TEXT("VHWeaponTestDummy")))
			{
				if (!bCreate) { It->Destroy(); return nullptr; }
				It->SnapTo(Feet, Yaw);
				It->PushSnapshot(FPlatformTime::Seconds(), Feet, Yaw, 0, 100, Weapon);
				return *It;
			}
		}
		if (!bCreate)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AVHNetProxy* Proxy = World->SpawnActor<AVHNetProxy>(AVHNetProxy::StaticClass(), Feet, FRotator(0.f, Yaw, 0.f), Params);
		if (Proxy)
		{
			Proxy->Tags.Add(TEXT("VHWeaponTestDummy"));
			Proxy->InitProxy(60001, static_cast<uint8>(RobProto::EntityKind::Thief), TEXT("Dummy"), 0);
			Proxy->SnapTo(Feet, Yaw);
			Proxy->PushSnapshot(FPlatformTime::Seconds(), Feet, Yaw, 0, 100, Weapon);
			Proxy->SetPlayerColorIndex(2);
		}
		return Proxy;
	}

	struct FWeaponTestOptions
	{
		uint8 Weapon = 1;
		float Hold = 2.f;
		float Dist = 250.f;
		float Slow = 0.f;
		float PauseAt = -1.f;	// 이 시간(초, 게임 시간)에 게임 일시정지 → 촬영용 (vh.WeaponTest off 로 풂)
		bool bProxy = false;
		float Charge = -1.f;	// 레일건 충전 시간 (음수 = 무기 표 값, 0 = 충전 없이 바로). 다른 플레이어(proxy)는 충전이 안 보이므로 0
		float CancelAt = -1.f;	// 레일건 충전을 이 시간에 취소 (무기 교체 흉내: 권총으로 바꿈)
		int32 Burst = 1;		// 연사 횟수 (서버 공격 간격마다. 권총 0.25초)
	};

	void RunWeaponTest(UWorld* World, const FWeaponTestOptions& O)
	{
		UVHWeaponFxSubsystem* Fx = UVHWeaponFxSubsystem::Get(World);
		UVHWeaponVisualComponent* Me = LocalVisual(World);
		if (!Fx || !Me)
		{
			UE_LOG(LogVHWeaponFx, Warning, TEXT("[무기 시험] 게임 레벨 ▶ 중에만 됩니다"));
			return;
		}
		const uint8 W = O.Weapon;
		AActor* MeActor = Me->GetOwner();
		const float MyYaw = MeActor->GetActorRotation().Yaw;
		const FVector MyFeet = VHFx::FeetOf(MeActor);
		const FVector DummyFeet = MyFeet + FRotator(0.f, MyYaw, 0.f).Vector() * O.Dist;
		AVHNetProxy* Dummy = WeaponTestDummy(World, DummyFeet, MyYaw + 180.f, O.bProxy ? W : 0, true);
		if (!Dummy)
		{
			return;
		}
		// 쏘는 쪽 / 맞는 쪽
		UVHWeaponVisualComponent* Shooter = O.bProxy ? Dummy->GetWeaponVisual() : Me;
		AActor* ShooterActor = O.bProxy ? static_cast<AActor*>(Dummy) : MeActor;
		AActor* Target = O.bProxy ? MeActor : static_cast<AActor*>(Dummy);
		UVHWeaponVisualComponent* TargetVisual = O.bProxy ? Me : Dummy->GetWeaponVisual();
		const FVector SFeet = O.bProxy ? DummyFeet : MyFeet;
		const float SYaw = O.bProxy ? MyYaw + 180.f : MyYaw;
		const FVector TFeet = O.bProxy ? MyFeet : DummyFeet;
		if (!O.bProxy)
		{
			UVHWeaponVisualComponent::DebugFreezeTime = -1.f;
			UVHWeaponVisualComponent::DebugLocalWeapon = W;
			Me->SetWeapon(W);
		}
		else if (Shooter)
		{
			Shooter->SetWeapon(W);	// 스냅샷 보간을 기다리지 않고 바로 든다
		}
		if (!Shooter)
		{
			return;
		}
		if (O.Slow > 0.f)
		{
			UGameplayStatics::SetGlobalTimeDilation(World, O.Slow);
		}
		TWeakObjectPtr<UVHWeaponFxSubsystem> WeakFx(Fx);
		TWeakObjectPtr<UVHWeaponVisualComponent> WeakS(Shooter), WeakTV(TargetVisual);
		TWeakObjectPtr<AActor> WeakT(Target), WeakSA(ShooterActor);
		TWeakObjectPtr<AVHNetProxy> WeakDummy(Dummy);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		auto Later = [World](float Seconds, TFunction<void()> Fn)
		{
			FTimerHandle H;
			World->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(MoveTemp(Fn)), FMath::Max(Seconds, 0.001f), false);
		};
		// 맞음: 이펙트 + 소리 + 움찔 (서버 SC_DAMAGE 와 같은 길)
		auto Damage = [WeakFx, WeakT, WeakTV, TFeet](uint8 Wid)
		{
			if (!WeakFx.IsValid()) { return; }
			const FVector F = WeakT.IsValid() ? VHFx::FeetOf(WeakT.Get()) : TFeet;
			WeakFx->OnDamage(Wid, WeakT.Get(), F, false);
			WeakFx->PlayHitSound(Wid, F + FVector(0.f, 0.f, 100.f));
			if (UVHWeaponVisualComponent* TV = WeakTV.Get()) { TV->PlayBodyAction(EVHBodyAction::HitReact); }
		};
		const FVector SPos = SFeet + FVector(0.f, 0.f, 100.f);
		switch (W)
		{
		case 5:
		{
			// 꾹 누르기: 내 캐릭터는 DebugHold, 다른 플레이어 표시는 스냅샷 상태(ES_ATTACKING)
			const float Hold = FMath::Max(0.3f, O.Hold);
			if (!O.bProxy)
			{
				UVHWeaponVisualComponent::DebugHold = 1;
				Later(Hold, []() { UVHWeaponVisualComponent::DebugHold = -1; });
			}
			else
			{
				for (float t = 0.f; t < Hold; t += 0.05f)
				{
					Later(t, [WeakDummy, SFeet, SYaw]() { if (WeakDummy.IsValid()) { WeakDummy->PushSnapshot(FPlatformTime::Seconds(), SFeet, SYaw, RobProto::ES_ATTACKING, 100, 5); } });
				}
				Later(Hold, [WeakDummy, SFeet, SYaw]() { if (WeakDummy.IsValid()) { WeakDummy->PushSnapshot(FPlatformTime::Seconds(), SFeet, SYaw, 0, 100, 5); } });
			}
			// 서버 판정 흉내: 사거리 600cm 안이면 0.25초마다 맞음
			if (O.Dist <= 600.f)
			{
				for (float t = 0.2f; t < Hold; t += 0.25f)
				{
					Later(t, [Damage]() { Damage(5); });
				}
			}
			break;
		}
		case 8:
		{
			Shooter->PlayAttack(8);
			UVHSoundConfig::Play(World, UVHSoundConfig::FireSound(8), SPos);
			const FVector To = TFeet;
			Fx->OnProjectile(false, SFeet, To, 1.f, Shooter);
			Later(1.f, [WeakFx, WeakWorld, To, Damage]()
			{
				if (!WeakFx.IsValid()) { return; }
				UVHSoundConfig::Play(WeakWorld.Get(), EVHSound::Explosion, To);
				WeakFx->OnExplosion(0, To, 600.f);
				Damage(8);
			});
			break;
		}
		case 9: case 10:
		{
			Shooter->PlayAttack(W);
			UVHSoundConfig::Play(World, UVHSoundConfig::FireSound(W), SPos);
			const bool bShock = W == 10;
			const FVector At = SFeet + FRotator(0.f, SYaw, 0.f).Vector() * 60.f;
			TWeakObjectPtr<AVHFxActor> Mine(Fx->SpawnMine(bShock, At, SYaw));
			Fx->ApplyMineState(Mine.Get(), 0);
			// 서버는 3초 뒤 작동 → 감지 → 0.5초 경고 → 폭발. 시험은 1초 뒤 작동, 2.5초 감지, 3초 폭발
			Later(1.f, [WeakFx, Mine]() { if (WeakFx.IsValid()) { WeakFx->ApplyMineState(Mine.Get(), RobProto::ES_ARMED); } });
			Later(2.5f, [WeakFx, Mine]() { if (WeakFx.IsValid()) { WeakFx->ApplyMineState(Mine.Get(), RobProto::ES_ARMED | RobProto::ES_TRIGGERED); } });
			Later(3.f, [WeakFx, WeakWorld, Mine, At, bShock, Damage, W]()
			{
				if (AVHFxActor* M = Mine.Get()) { M->Destroy(); }
				if (!WeakFx.IsValid()) { return; }
				UVHSoundConfig::Play(WeakWorld.Get(), bShock ? EVHSound::ElectricShock
					: (UVHSoundConfig::Has(EVHSound::BlastMineExplosion) ? EVHSound::BlastMineExplosion : EVHSound::Explosion), At);
				WeakFx->OnExplosion(bShock ? 2 : 1, At, 300.f);
				Damage(W);
			});
			break;
		}
		default:
		{
			// 한 번 공격: 동작 + 소리 + SC_FIRE 이펙트(맞은 대상까지) + 맞음
			const bool bInRange = W == 4 || W == 6 || O.Dist <= (W == 1 ? 200.f : W == 2 ? 300.f : W == 3 ? 400.f : 300.f);
			const bool bProxy = O.bProxy;
			auto FireNow = [WeakFx, WeakS, WeakWorld, W, SPos, SFeet, SYaw, TFeet, bInRange, bProxy, Damage, Later]()
			{
				UVHWeaponFxSubsystem* F = WeakFx.Get();
				UVHWeaponVisualComponent* S = WeakS.Get();
				if (!F || !S || !WeakWorld.IsValid())
				{
					return;
				}
				S->SetRailgunCharging(false);
				S->PlayAttack(W);
				UVHSoundConfig::Play(WeakWorld.Get(), UVHSoundConfig::FireSound(W), SPos);
				const FVector HitFeet = TFeet;
				F->OnFire(W, SFeet, SYaw, bInRange ? &HitFeet : nullptr, S);
				if (W == 6 && !bProxy)
				{
					UVHWeaponVisualComponent::DebugRailLock = 1;
					Later(1.f, []() { UVHWeaponVisualComponent::DebugRailLock = -1; });
				}
				if (bInRange)
				{
					if (W == 6)
					{
						for (int32 k = 0; k < 6; ++k) { Later(0.2f * k + 0.01f, [Damage]() { Damage(6); }); }	// 레일건 6번 판정
					}
					else
					{
						Damage(W);
					}
				}
			};
			// 레일건: 내 캐릭터는 충전(소리·빛·자세) → 발사. 실제 게임과 같은 1초 (vh.WeaponTest 6 charge 0 = 바로)
			const float Charge = (W == 6 && !O.bProxy) ? (O.Charge >= 0.f ? O.Charge : UVHSettings::Get().GetWeaponCatalog().RailgunChargeSeconds) : 0.f;
			if (Charge > 0.f)
			{
				Shooter->SetRailgunCharging(true, Charge);
				UE_LOG(LogVHWeaponFx, Log, TEXT("[무기 시험] 레일건 충전 시작 (%.2f초)"), Charge);
				if (O.CancelAt >= 0.f && O.CancelAt < Charge)
				{
					Later(O.CancelAt, [WeakS]()
					{
						if (UVHWeaponVisualComponent* S = WeakS.Get())
						{
							UVHWeaponVisualComponent::DebugLocalWeapon = 4;
							S->SetWeapon(4);	// 무기 교체 흉내 → 충전 취소 (소리·빛이 끊긴다)
							S->SetRailgunCharging(false);
							UE_LOG(LogVHWeaponFx, Log, TEXT("[무기 시험] 레일건 충전 취소 (무기 교체 흉내)"));
						}
					});
				}
				else
				{
					Later(Charge, FireNow);
				}
			}
			else
			{
				FireNow();
				// 연사: 서버 공격 간격마다 다시 (권총 0.25초 → 반동이 쌓이는지 확인)
				for (int32 k = 1; k < O.Burst; ++k)
				{
					Later(UVHWeaponCatalog::ServerIntervalSeconds(W) * k, FireNow);
				}
			}
			break;
		}
		}
		if (O.Slow > 0.f)
		{
			Later(3.5f, [WeakWorld]() { if (WeakWorld.IsValid()) { UGameplayStatics::SetGlobalTimeDilation(WeakWorld.Get(), 1.f); } });
		}
		if (O.PauseAt >= 0.f)
		{
			Later(O.PauseAt, [WeakWorld]() { if (WeakWorld.IsValid()) { UGameplayStatics::SetGamePaused(WeakWorld.Get(), true); } });
		}
		UE_LOG(LogVHWeaponFx, Log, TEXT("[무기 시험] %d %s %s (거리 %.0fcm%s)"), W, GWeaponNames[W], O.bProxy ? TEXT("다른 플레이어 → 나") : TEXT("나 → 허수아비"),
			O.Dist, O.Slow > 0.f ? TEXT(", 느리게") : TEXT(""));
	}

	// vh.WeaponTest N [hold 초] [dist cm] [slow [배율]] [proxy] [loop] / vh.WeaponTest off
	FAutoConsoleCommandWithWorldAndArgs GVHWeaponTestCommand(TEXT("vh.WeaponTest"),
		TEXT("개발용: vh.WeaponTest N(1~10) [hold 초] [dist cm] [slow 배율] [proxy] [loop] [at 초] [charge 초] [cancel 초] [burst N] —앞 허수아비에게 무기 N 동작·소리·이펙트·맞음. 레일건은 충전 1초 뒤 발사(charge 0 = 바로, cancel 0.5 = 0.5초에 취소). off = 멈추고 치우기."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			World->GetTimerManager().ClearTimer(GWeaponTestLoop);
			if (Args.Num() == 0 || Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
			{
				WeaponTestDummy(World, FVector::ZeroVector, 0.f, 0, false);
				UGameplayStatics::SetGamePaused(World, false);
				UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
				UVHWeaponVisualComponent::DebugLocalWeapon = -1;
				if (UVHWeaponVisualComponent* Me = LocalVisual(World))
				{
					Me->SetRailgunCharging(false);
				}
				return;
			}
			FWeaponTestOptions O;
			O.Weapon = static_cast<uint8>(FMath::Clamp(FCString::Atoi(*Args[0]), 1, 10));
			// 기본 거리 = 사거리 안 (삼단봉 200·도검 300·빔 소드 400·테이저 300 cm, 화염 600)
			static const float DefaultDist[] = { 250.f, 150.f, 220.f, 300.f, 400.f, 350.f, 600.f, 230.f, 400.f, 220.f, 220.f };
			O.Dist = DefaultDist[O.Weapon];
			bool bLoop = false;
			for (int32 i = 1; i < Args.Num(); ++i)
			{
				const FString& A = Args[i];
				const bool bHasNum = i + 1 < Args.Num() && Args[i + 1].IsNumeric();
				if (A.Equals(TEXT("hold"), ESearchCase::IgnoreCase) && bHasNum) { O.Hold = FCString::Atof(*Args[++i]); }
				else if (A.Equals(TEXT("dist"), ESearchCase::IgnoreCase) && bHasNum) { O.Dist = FMath::Clamp(FCString::Atof(*Args[++i]), 80.f, 1500.f); }
				else if (A.Equals(TEXT("slow"), ESearchCase::IgnoreCase)) { O.Slow = bHasNum ? FMath::Clamp(FCString::Atof(*Args[++i]), 0.01f, 1.f) : 0.25f; }
				else if (A.Equals(TEXT("at"), ESearchCase::IgnoreCase) && bHasNum) { O.PauseAt = FCString::Atof(*Args[++i]); }
				else if (A.Equals(TEXT("proxy"), ESearchCase::IgnoreCase)) { O.bProxy = true; }
				else if (A.Equals(TEXT("charge"), ESearchCase::IgnoreCase) && bHasNum) { O.Charge = FMath::Clamp(FCString::Atof(*Args[++i]), 0.f, 3.f); }
				else if (A.Equals(TEXT("cancel"), ESearchCase::IgnoreCase) && bHasNum) { O.CancelAt = FCString::Atof(*Args[++i]); }
				else if (A.Equals(TEXT("burst"), ESearchCase::IgnoreCase) && bHasNum) { O.Burst = FMath::Clamp(FCString::Atoi(*Args[++i]), 1, 20); }
				else if (A.Equals(TEXT("loop"), ESearchCase::IgnoreCase)) { bLoop = true; }
			}
			RunWeaponTest(World, O);
			if (bLoop)
			{
				TWeakObjectPtr<UWorld> WeakWorld(World);
				const float Every = O.Weapon >= 8 ? 4.f : (O.Weapon == 5 ? O.Hold + 1.5f : 2.f);
				World->GetTimerManager().SetTimer(GWeaponTestLoop, FTimerDelegate::CreateLambda([WeakWorld, O]()
				{
					if (WeakWorld.IsValid()) { RunWeaponTest(WeakWorld.Get(), O); }
				}), Every, true);
			}
		}));

	// vh.FxPlane 각도 부호 : 바닥 평면 도형의 앞 방향 보정(도)과 좌우 부호(1/-1)
	FAutoConsoleCommand GVHFxPlaneCommand(TEXT("vh.FxPlane"), TEXT("개발용: vh.FxPlane 각도 부호 — 평면 도형의 앞 방향 보정과 좌우 부호."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() > 0) { UVHWeaponFxSubsystem::PlaneYawOffset = FCString::Atof(*Args[0]); }
			if (Args.Num() > 1) { UVHWeaponFxSubsystem::PlaneSideSign = FCString::Atof(*Args[1]) < 0.f ? -1.f : 1.f; }
			UE_LOG(LogVHWeaponFx, Log, TEXT("[무기 이펙트] 평면 보정 %.0f도, 좌우 %.0f"), UVHWeaponFxSubsystem::PlaneYawOffset, UVHWeaponFxSubsystem::PlaneSideSign);
		}));
}
#endif
