#include "Gameplay/VHWeaponCatalog.h"

#include "Network/VHProtocol.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/Texture2D.h"

// 무기 번호는 서버 프로토콜 값을 그대로 쓴다
static_assert(static_cast<uint8>(EVHWeaponId::Baton) == static_cast<uint8>(RobProto::WeaponId::Baton), "WeaponId mismatch");
static_assert(static_cast<uint8>(EVHWeaponId::Pistol) == static_cast<uint8>(RobProto::WeaponId::Pistol), "WeaponId mismatch");
static_assert(static_cast<uint8>(EVHWeaponId::Grenade) == static_cast<uint8>(RobProto::WeaponId::Grenade), "WeaponId mismatch");
static_assert(static_cast<uint8>(EVHWeaponId::ShockMine) == static_cast<uint8>(RobProto::WeaponId::ShockMine), "WeaponId mismatch");
static_assert(static_cast<uint8>(EVHConsumableId::HealKit) == static_cast<uint8>(RobProto::ConsumableId::HealKit), "ConsumableId mismatch");
static_assert(static_cast<uint8>(EVHConsumableId::Jetpack) == static_cast<uint8>(RobProto::ConsumableId::Jetpack), "ConsumableId mismatch");

UVHWeaponCatalog::UVHWeaponCatalog()
{
	// 구상안 v2.2 의 색(sRGB 16진수)을 그대로 적는다 → 내부 계산용(리니어) 색으로 바뀐다
	MeleeGlow = FLinearColor(FColor::FromHex(TEXT("FF2D55")));
	RangedGlow = FLinearColor(FColor::FromHex(TEXT("35D6FF")));
	ThrownGlow = FLinearColor(FColor::FromHex(TEXT("FF9A1F")));
	PlacedGlow = FLinearColor(FColor::FromHex(TEXT("B6FF3A")));
	FlameColor = FLinearColor(1.f, 0.3f, 0.04f);
	FloorArtMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/VaultHeist/Weapons/Materials/M_VHWeaponFloorArt.M_VHWeaponFloorArt")));

	// 소비 아이템 4종 기본값 [구상안 Docs/Design/Consumables_v1, 모델·아이콘 = Tools/WeaponBuild/build_consumables.py]
	struct FDefault { EVHConsumableId Id; const TCHAR* Name; const TCHAR* Hex; };
	const FDefault Defaults[] = {
		{ EVHConsumableId::HealKit, TEXT("1_HealKit"), TEXT("2BF5B0") },
		{ EVHConsumableId::Smoke, TEXT("2_Smoke"), TEXT("B07CFF") },
		{ EVHConsumableId::Flashbang, TEXT("3_Flashbang"), TEXT("FFE45C") },
		{ EVHConsumableId::Jetpack, TEXT("4_Jetpack"), TEXT("4C7DFF") },
	};
	for (const FDefault& D : Defaults)
	{
		FVHConsumableVisual& V = Consumables.AddDefaulted_GetRef();
		V.Id = D.Id;
		V.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(FString::Printf(TEXT("/Game/VaultHeist/Consumables/Meshes/SM_VHC_%s.SM_VHC_%s"), D.Name, D.Name)));
		V.Icon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(FString::Printf(TEXT("/Game/VaultHeist/Consumables/Icons/T_VHC_Icon_%s.T_VHC_Icon_%s"), D.Name, D.Name)));
		V.BandColor = FLinearColor(FColor::FromHex(D.Hex));
		V.FloorScale = 1.5f;	// 모델 1배(약 45cm)는 탑뷰 1080p 에서 30px 남짓이라 1.5배 (무기 바닥 배율과 비슷한 크기)
	}
}

const FVHConsumableVisual* UVHWeaponCatalog::FindConsumable(uint8 ConsumableId) const
{
	if (ConsumableId == 0)
	{
		return nullptr;
	}
	return Consumables.FindByPredicate([ConsumableId](const FVHConsumableVisual& C) { return static_cast<uint8>(C.Id) == ConsumableId; });
}

const FVHWeaponVisual* UVHWeaponCatalog::Find(uint8 WeaponId) const
{
	if (WeaponId == 0)
	{
		return nullptr;
	}
	return Weapons.FindByPredicate([WeaponId](const FVHWeaponVisual& W) { return static_cast<uint8>(W.Id) == WeaponId; });
}

FLinearColor UVHWeaponCatalog::CategoryColor(EVHWeaponCategory Category) const
{
	switch (Category)
	{
	case EVHWeaponCategory::Ranged:	return RangedGlow;
	case EVHWeaponCategory::Thrown:	return ThrownGlow;
	case EVHWeaponCategory::Placed:	return PlacedGlow;
	default:						return MeleeGlow;
	}
}

EVHWeaponCategory UVHWeaponCatalog::CategoryOf(uint8 WeaponId)
{
	switch (static_cast<EVHWeaponId>(WeaponId))
	{
	case EVHWeaponId::Pistol:
	case EVHWeaponId::Flamethrower:
	case EVHWeaponId::Railgun:
	case EVHWeaponId::Taser:
		return EVHWeaponCategory::Ranged;
	case EVHWeaponId::Grenade:
		return EVHWeaponCategory::Thrown;
	case EVHWeaponId::BlastMine:
	case EVHWeaponId::ShockMine:
		return EVHWeaponCategory::Placed;
	default:
		return EVHWeaponCategory::Melee;
	}
}

FLinearColor UVHWeaponCatalog::GlowColor(uint8 WeaponId, const FLinearColor& Fallback) const
{
	if (WeaponId == 0 || WeaponId > static_cast<uint8>(EVHWeaponId::ShockMine))
	{
		return Fallback;
	}
	const FVHWeaponVisual* Visual = Find(WeaponId);
	return CategoryColor(Visual ? Visual->Category : CategoryOf(WeaponId));
}

float UVHWeaponCatalog::ServerIntervalSeconds(uint8 WeaponId)
{
	// [서버 GameData.h WeaponTypes] 삼단봉 500 · 도검 500 · 빔 소드 333 · 권총 250 · 화염방사기 33 · 레일건 1000 · 테이저 1000 · 수류탄 1000 · 지뢰 1000 ms
	switch (static_cast<EVHWeaponId>(WeaponId))
	{
	case EVHWeaponId::Baton:		return 0.5f;
	case EVHWeaponId::Sword:		return 0.5f;
	case EVHWeaponId::BeamSword:	return 0.333f;
	case EVHWeaponId::Pistol:		return 0.25f;
	case EVHWeaponId::Flamethrower:	return 0.033f;
	case EVHWeaponId::Railgun:		return 1.f;
	case EVHWeaponId::Taser:		return 1.f;
	case EVHWeaponId::Grenade:		return 1.f;
	case EVHWeaponId::BlastMine:	return 1.f;
	case EVHWeaponId::ShockMine:	return 1.f;
	default:						return 1.f;
	}
}

FVHCrosshairSpec UVHWeaponCatalog::ResolveCrosshair(uint8 WeaponId, bool bSkillTaser) const
{
	// 자동 값: [서버 GameData.h] WeaponTypes 의 rangeCm·halfAngleDeg, GrenadeRadiusCm 600, MineRadiusCm 300, SkillTaserRangeCm 300
	struct FAuto { EVHCrosshairShape Shape; float Range; float Half; float Area; float Kick; float Recover; };
	FAuto A{ EVHCrosshairShape::Brackets, 0.f, 0.f, 0.f, 6.f, 0.16f };
	FVHCrosshairSpec Spec;
	if (bSkillTaser)
	{
		Spec = SkillTaserCrosshair;
		A = { EVHCrosshairShape::Lightning, 300.f, 0.f, 0.f, 12.f, 0.22f };
	}
	else
	{
		const FVHWeaponVisual* Visual = WeaponId != 0 ? Find(WeaponId) : nullptr;
		Spec = Visual ? Visual->Crosshair : (WeaponId == 0 ? UnarmedCrosshair : FVHCrosshairSpec());
		switch (static_cast<EVHWeaponId>(WeaponId))
		{
		case EVHWeaponId::Baton:		A = { EVHCrosshairShape::Brackets, 200.f, 0.f, 0.f, 7.f, 0.16f }; break;
		case EVHWeaponId::Sword:		A = { EVHCrosshairShape::Brackets, 300.f, 0.f, 0.f, 8.f, 0.18f }; break;
		case EVHWeaponId::BeamSword:	A = { EVHCrosshairShape::Brackets, 400.f, 0.f, 0.f, 9.f, 0.18f }; break;
		case EVHWeaponId::Pistol:		A = { EVHCrosshairShape::Cross, 0.f, 0.f, 0.f, 9.f, 0.14f }; break;
		case EVHWeaponId::Flamethrower:	A = { EVHCrosshairShape::Cone, 600.f, 15.f, 0.f, 3.f, 0.10f }; break;
		case EVHWeaponId::Railgun:		A = { EVHCrosshairShape::RailRing, 0.f, 0.f, 0.f, 26.f, 0.45f }; break;
		case EVHWeaponId::Taser:		A = { EVHCrosshairShape::Cross, 300.f, 0.f, 0.f, 13.f, 0.22f }; break;
		case EVHWeaponId::Grenade:		A = { EVHCrosshairShape::Area, 600.f, 0.f, 600.f, 10.f, 0.25f }; break;
		case EVHWeaponId::BlastMine:	A = { EVHCrosshairShape::Area, 0.f, 0.f, 300.f, 8.f, 0.25f }; break;	// 지뢰는 발밑에 설치 → 사거리 없음
		case EVHWeaponId::ShockMine:	A = { EVHCrosshairShape::Area, 0.f, 0.f, 300.f, 8.f, 0.25f }; break;
		default: break;	// 맨손
		}
	}
	if (Spec.Shape == EVHCrosshairShape::Auto) { Spec.Shape = A.Shape; }
	if (Spec.RangeCm < 0.f) { Spec.RangeCm = A.Range; }
	if (Spec.HalfAngleDeg < 0.f) { Spec.HalfAngleDeg = A.Half; }
	if (Spec.AreaRadiusCm < 0.f) { Spec.AreaRadiusCm = A.Area; }
	if (Spec.KickPx < 0.f) { Spec.KickPx = A.Kick; }
	if (Spec.KickRecoverSeconds < 0.f) { Spec.KickRecoverSeconds = A.Recover; }
	Spec.KickRecoverSeconds = FMath::Max(Spec.KickRecoverSeconds, 0.02f);
	Spec.SizeScale = FMath::Max(Spec.SizeScale, 0.5f);
	return Spec;
}

void FVHWeaponRecoil::Evaluate(float T, float& OutMuzzle, float& OutBody) const
{
	// Lyra MM_Pistol_Fire 모양 (Tools/External/lyra_recoil.json): 총구 들림은 0.067초에 가장 크고(빠르게 올라감) 0.3초에 거의 0(처음 빨리 내려옴),
	// 손이 뒤로 밀리는 것은 0.133초에 가장 크다(느리게 시작 → 빠르게 → 끝에 천천히 = smoothstep 모양).
	OutMuzzle = 0.f;
	OutBody = 0.f;
	const float Total = TotalSeconds();
	if (T < 0.f || T >= Total)
	{
		return;
	}
	const float R = FMath::Max(RiseSeconds, 0.005f);
	if (T < R)
	{
		OutMuzzle = FMath::Sin(HALF_PI * T / R);
	}
	else
	{
		const float U = FMath::Clamp((T - R) / FMath::Max(Total - R, 0.01f), 0.f, 1.f);
		OutMuzzle = FMath::Square(1.f - U);
	}
	const float RB = R * FMath::Max(KickLag, 1.f);
	if (T < RB)
	{
		OutBody = FMath::SmoothStep(0.f, 1.f, T / RB);
	}
	else
	{
		OutBody = 1.f - FMath::SmoothStep(0.f, 1.f, (T - RB) / FMath::Max(RecoverSeconds, 0.01f));
	}
}

FName FVHWeaponVisual::SocketName(EVHWeaponPoint Point)
{
	switch (Point)
	{
	case EVHWeaponPoint::Muzzle:		return TEXT("Muzzle");
	case EVHWeaponPoint::BladeTip:		return TEXT("BladeTip");
	case EVHWeaponPoint::BladeBase:		return TEXT("BladeBase");
	case EVHWeaponPoint::Grip:			return TEXT("Grip");
	case EVHWeaponPoint::SupportHand:	return TEXT("SupportHand");
	default:							return NAME_None;
	}
}

bool FVHWeaponVisual::PointLocal(const UStaticMesh* InMesh, EVHWeaponPoint Point, FVector& Out, bool* OutFromSocket) const
{
	if (OutFromSocket)
	{
		*OutFromSocket = false;
	}
	if (!InMesh)
	{
		return false;
	}
	// 1) 모델러가 넣은 소켓
	if (const UStaticMeshSocket* Socket = InMesh->FindSocket(SocketName(Point)))
	{
		Out = Socket->RelativeLocation;
		if (OutFromSocket)
		{
			*OutFromSocket = true;
		}
		return true;
	}
	// 2) 무기 표의 대신 값
	const FVector* Fallback = Point == EVHWeaponPoint::Muzzle ? &MuzzleFallback
		: Point == EVHWeaponPoint::BladeTip ? &BladeTipFallback
		: Point == EVHWeaponPoint::BladeBase ? &BladeBaseFallback : nullptr;
	if (Fallback && !Fallback->IsNearlyZero())
	{
		Out = *Fallback;
		return true;
	}
	// 3) 자동: 메시 크기로 (2026-11-19 까지의 계산과 같다. 앞 방향은 ModelRotation 을 되돌린 +X)
	if (Point == EVHWeaponPoint::Grip)
	{
		Out = FVector::ZeroVector;	// 약속: 피벗 = 손잡이
		return true;
	}
	if (Point == EVHWeaponPoint::SupportHand)
	{
		return false;				// 캐릭터 쪽 값(Left Hand On Weapon)을 쓴다
	}
	const FBox Box = InMesh->GetBoundingBox();
	const FVector C = Box.GetCenter();
	const FVector E = Box.GetExtent();
	const FVector F = ModelRotation.UnrotateVector(FVector::ForwardVector).GetSafeNormal();
	const FVector Front = C + F * FVector::DotProduct(F.GetAbs(), E);	// 앞 끝 가운데
	const float FrontDist = FVector::DotProduct(Front, F);
	switch (Point)
	{
	case EVHWeaponPoint::BladeTip:	Out = Front - F * (0.1f * FrontDist); break;	// 앞 끝의 90%
	case EVHWeaponPoint::BladeBase:	Out = Front - F * (0.75f * FrontDist); break;	// 앞 끝의 25%
	default:						Out = Front; break;
	}
	return true;
}

FTransform FVHWeaponVisual::MeshToWeapon(const UStaticMesh* InMesh, const FVector& DisplayScale, bool bToGrip) const
{
	const FQuat R = ModelRotation.Quaternion();
	// 표시 배율(무기 좌표의 길이 X·두께 Y·Z)을 모델 좌표 축으로 옮긴다 (회전 보정이 90° 단위일 때 정확)
	FVector LocalScale;
	for (int32 i = 0; i < 3; ++i)
	{
		FVector Axis = FVector::ZeroVector;
		Axis[i] = 1.0;
		LocalScale[i] = ModelScale * FVector::DotProduct(R.RotateVector(Axis).GetAbs(), DisplayScale);
	}
	FVector Translation = FVector::ZeroVector;
	FVector Grip;
	bool bSocket = false;
	if (bToGrip && PointLocal(InMesh, EVHWeaponPoint::Grip, Grip, &bSocket) && bSocket)
	{
		Translation = -(DisplayScale * R.RotateVector(Grip * ModelScale));
	}
	return FTransform(R, Translation, LocalScale);
}

FLinearColor UVHWeaponCatalog::FxColor(uint8 WeaponId) const
{
	return static_cast<EVHWeaponId>(WeaponId) == EVHWeaponId::Flamethrower ? FlameColor : GlowColor(WeaponId);
}
