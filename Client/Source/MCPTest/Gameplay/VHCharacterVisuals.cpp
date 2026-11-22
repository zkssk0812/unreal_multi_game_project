#include "Gameplay/VHCharacterVisuals.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

FVHCharacterVisual::FVHCharacterVisual()
{
	// 마네킹(키 약 180cm, 어깨 높이 약 140cm, 팔 길이 약 55cm) 기준. 오른손 = +Y. Roll 90 = 옆모습이 위(카메라)를 보게 눕힘
	// 근접: 오른손을 몸 앞 오른쪽으로 내밀고 날 끝이 조준 쪽, 바깥으로 조금 벌림
	MeleePose = FVHHoldPose(FRotator(8.f, 18.f, 90.f), FVector(40.f, 26.f, 108.f), FVector(-0.3, 0.6, -0.75));
	// 원거리(권총·테이저): 오른팔을 조준 방향으로 뻗는다
	RangedPose = FVHHoldPose(FRotator(0.f, 0.f, 90.f), FVector(54.f, 17.f, 128.f), FVector(-0.1, 0.5, -0.85));
	// 투척: 수류탄을 오른손에 쥐고 가슴 옆에
	ThrownPose = FVHHoldPose(FRotator(0.f, 0.f, 0.f), FVector(34.f, 27.f, 118.f), FVector(-0.3, 0.7, -0.65));
	// 설치: 지뢰를 두 손으로 몸 앞에 평평하게
	// 설치: 지뢰를 두 손으로 몸 앞에 평평하게. 오른손은 지뢰 오른쪽 가장자리(무기 쪽 Grip Offset 으로 맞춤), 왼손은 왼쪽 가장자리
	PlacedPose = FVHHoldPose(FRotator(0.f, 0.f, 0.f), FVector(44.f, 0.f, 102.f), FVector(-0.2, 0.75, -0.6), true, FVector(0.0, -19.0, 2.0));
	// 긴 총: 두 손으로 허리~가슴 높이에서 겨눈다 (오른손 = 손잡이, 왼손 = 총열 아래)
	WeaponPoses.Add(EVHWeaponId::Flamethrower, FVHHoldPose(FRotator(0.f, 0.f, 90.f), FVector(40.f, 24.f, 104.f), FVector(-0.5, 0.6, -0.6), true, FVector(34.0, 0.0, 0.0)));
	WeaponPoses.Add(EVHWeaponId::Railgun, FVHHoldPose(FRotator(0.f, 0.f, 90.f), FVector(46.f, 24.f, 100.f), FVector(-0.5, 0.6, -0.6), true, FVector(38.0, 0.0, 0.0)));

	// ── 무기 사용 동작 (키 프레임). 시간은 초, 마지막 키 = 동작 길이. 서버 공격 간격(GameData.h)보다 짧게 잡았다 ──
	using K = FVHMotionKey;
	auto Motion = [](float Effect, std::initializer_list<FVHMotionKey> Keys, float ShakeCm = 0.f, float ShakeHz = 0.f)
	{
		FVHWeaponMotion M;
		M.Keys = Keys;
		M.EffectTime = Effect;
		M.ShakeCm = ShakeCm;
		M.ShakeHz = ShakeHz;
		return M;
	};
	const FVector O = FVector::ZeroVector;
	// 삼단봉 내려치기 (간격 0.5초): 머리 위로 들었다가 앞으로 내려친다
	AttackMotions.Add(EVHWeaponId::Baton, Motion(0.17f, { K(0.f, O), K(0.09f, FVector(-14, 4, 30), FRotator(65, 10, 0), 8.f, -8.f),
		K(0.18f, FVector(16, -6, -24), FRotator(-35, -8, 0), 0.f, 14.f), K(0.30f, FVector(12, -4, -18), FRotator(-28, -6, 0), 0.f, 10.f), K(0.45f, O) }));
	// 도검 베기 (간격 0.5초): 오른쪽에서 왼쪽으로 크게 가로 베기
	AttackMotions.Add(EVHWeaponId::Sword, Motion(0.15f, { K(0.f, O), K(0.08f, FVector(-10, 22, 8), FRotator(0, 70, 0), 25.f, 0.f),
		K(0.20f, FVector(6, -38, -4), FRotator(0, -80, 0), -28.f, 6.f), K(0.32f, FVector(4, -32, -4), FRotator(0, -70, 0), -22.f, 4.f), K(0.45f, O) }));
	// 빔 소드 베기 (간격 0.333초): 더 빠른 사선 베기 (오른쪽 위 → 왼쪽 아래)
	AttackMotions.Add(EVHWeaponId::BeamSword, Motion(0.11f, { K(0.f, O), K(0.05f, FVector(-8, 20, 14), FRotator(20, 65, 0), 22.f, 0.f),
		K(0.14f, FVector(8, -36, -10), FRotator(-20, -80, 0), -26.f, 8.f), K(0.21f, FVector(6, -30, -8), FRotator(-15, -70, 0), -20.f, 6.f), K(0.30f, O) }));
	// 권총 (간격 0.25초): 짧은 반동
	// (탑뷰에서는 위로 들리는 각도가 잘 안 보여서 손이 뒤로 밀리는 거리를 크게 잡았다)
	AttackMotions.Add(EVHWeaponId::Pistol, Motion(0.f, { K(0.f, O), K(0.03f, FVector(-13, 2, 5), FRotator(28, 4, 0), 3.f, -3.f), K(0.20f, O) }));
	// 테이저건 (간격 1초): 짧은 반동 + 손떨림 한 번
	AttackMotions.Add(EVHWeaponId::Taser, Motion(0.f, { K(0.f, O), K(0.03f, FVector(-12, 2, 5), FRotator(24, 4, 0), 3.f, -3.f), K(0.10f, FVector(-7, -1, 3), FRotator(10, -3, 0)), K(0.30f, O) }));
	// 레일건 (발사 1초 잠금): 큰 반동으로 뒤로 젖혔다가 버팀 자세로 (그 뒤는 RailgunLockMotion)
	AttackMotions.Add(EVHWeaponId::Railgun, Motion(0.f, { K(0.f, O), K(0.04f, FVector(-14, 0, 2), FRotator(5, 0, 0), 0.f, -6.f), K(0.30f, FVector(-5, 0, 0), FRotator(2, 0, 0), 0.f, -2.f) }));
	RailgunLockMotion = Motion(0.f, { K(0.f, FVector(-5, 0, 0), FRotator(2, 0, 0), 0.f, -2.f) }, 0.8f, 22.f);
	// 수류탄 (간격 1초): 뒤로 젖혀 들었다가 앞으로 던지고 따라 나간다. 0.2초에 손을 떠난다
	AttackMotions.Add(EVHWeaponId::Grenade, Motion(0.20f, { K(0.f, O), K(0.11f, FVector(-30, 12, 30), FRotator(40, 0, 0), 28.f, -10.f),
		K(0.20f, FVector(28, -4, 24), FRotator(-25, 0, 0), -18.f, 8.f), K(0.38f, FVector(22, -14, -4), FRotator(-40, 0, 0), -24.f, 14.f), K(0.60f, O) }));
	// 지뢰 2종 (간격 1초): 허리를 숙여 발밑에 내려놓는다. 0.32초에 바닥에 닿는다 (서버는 지뢰를 발 위치에 만든다)
	const FVHWeaponMotion Place = Motion(0.32f, { K(0.f, O), K(0.14f, FVector(-6, 0, -30), FRotator::ZeroRotator, 0.f, 22.f),
		K(0.30f, FVector(-20, 0, -70), FRotator::ZeroRotator, 0.f, 48.f), K(0.40f, FVector(-20, 0, -70), FRotator::ZeroRotator, 0.f, 48.f), K(0.65f, O) });
	AttackMotions.Add(EVHWeaponId::BlastMine, Place);
	AttackMotions.Add(EVHWeaponId::ShockMine, Place);
	// 꾹 누르는 동안: 화염방사기는 앞으로 내밀어 겨누고 떨림, 레일건은 충전하며 몸을 낮춘다
	HoldMotions.Add(EVHWeaponId::Flamethrower, Motion(0.f, { K(0.f, FVector(10, 0, -6), FRotator(-6, 0, 0), 0.f, 4.f) }, 1.0f, 16.f));
	HoldMotions.Add(EVHWeaponId::Railgun, Motion(0.f, { K(0.f, FVector(-4, -2, -4), FRotator(-4, 0, 0), 0.f, 4.f) }, 0.5f, 30.f));
}

FVHMotionKey FVHWeaponMotion::Evaluate(float Time) const
{
	if (Keys.Num() == 0)
	{
		return FVHMotionKey();
	}
	if (Time <= Keys[0].Time || Keys.Num() == 1)
	{
		return Keys[0];
	}
	for (int32 i = 1; i < Keys.Num(); ++i)
	{
		const FVHMotionKey& A = Keys[i - 1];
		const FVHMotionKey& B = Keys[i];
		if (Time <= B.Time)
		{
			const float Alpha = FMath::InterpEaseInOut(0.f, 1.f, FMath::Clamp((Time - A.Time) / FMath::Max(B.Time - A.Time, 0.001f), 0.f, 1.f), 2.f);
			FVHMotionKey Out;
			Out.Time = Time;
			Out.HandOffset = FMath::Lerp(A.HandOffset, B.HandOffset, Alpha);
			Out.WeaponRotation = FRotator(FQuat::Slerp(A.WeaponRotation.Quaternion(), B.WeaponRotation.Quaternion(), Alpha));
			Out.SpineYaw = FMath::Lerp(A.SpineYaw, B.SpineYaw, Alpha);
			Out.SpinePitch = FMath::Lerp(A.SpinePitch, B.SpinePitch, Alpha);
			return Out;
		}
	}
	return Keys.Last();
}

FVHRollMotion::FVHRollMotion()
{
	// 앞구르기 한 바퀴 (시간 = 비율). 0~0.2 숙이며 웅크림 → 0.2~0.7 공처럼 굴러 돎 → 0.7~1 펴며 일어섬
	// 몸 방향: 시작 0.12 안에 구르는 쪽으로 돌고, 끝 0.15 동안 다시 커서 쪽으로
	using K = FVHRollKey;
	Keys = { K(0.f, 0.f, 0.f, 0.f, 0.f), K(0.12f, 0.75f, 30.f, 34.f, 1.f), K(0.22f, 1.f, 95.f, 46.f, 1.f), K(0.62f, 1.f, 290.f, 46.f, 1.f),
		K(0.80f, 0.55f, 345.f, 26.f, 1.f), K(0.88f, 0.25f, 360.f, 10.f, 0.8f), K(1.f, 0.f, 360.f, 0.f, 0.f) };
	SpineBones = { TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03"), TEXT("spine_04"), TEXT("spine_05") };
	HeadBones = { TEXT("neck_01"), TEXT("head") };
}

FVHRollKey FVHRollMotion::Evaluate(float U) const
{
	if (Keys.Num() == 0)
	{
		return FVHRollKey();
	}
	if (U <= Keys[0].Time || Keys.Num() == 1)
	{
		return Keys[0];
	}
	for (int32 i = 1; i < Keys.Num(); ++i)
	{
		const FVHRollKey& A = Keys[i - 1];
		const FVHRollKey& B = Keys[i];
		if (U <= B.Time)
		{
			const float Linear = FMath::Clamp((U - A.Time) / FMath::Max(B.Time - A.Time, 0.001f), 0.f, 1.f);
			const float Ease = FMath::InterpEaseInOut(0.f, 1.f, Linear, 2.f);
			FVHRollKey Out;
			Out.Time = U;
			Out.Tuck = FMath::Lerp(A.Tuck, B.Tuck, Ease);
			Out.Drop = FMath::Lerp(A.Drop, B.Drop, Ease);
			Out.Face = FMath::Lerp(A.Face, B.Face, Ease);
			Out.Flip = FMath::Lerp(A.Flip, B.Flip, Linear);	// 회전은 키마다 멈칫하지 않게 일정한 빠르기
			return Out;
		}
	}
	return Keys.Last();
}

UVHCharacterVisuals::UVHCharacterVisuals()
{
	// 에셋(DA_VHCharacterVisuals)이 없어도 지금 모습 그대로 나오게: 템플릿 마네킹
	Default.Mesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
	Default.IdleAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle")));
	Default.WalkAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.MF_Unarmed_Walk_Fwd")));
	Default.RunAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd.MF_Unarmed_Jog_Fwd")));
	// 플레이어 구분 색: 빨강·파랑·노랑 (밝은 나무 바닥·흰 대리석 위에서도 보이는 진한 색)
	PlayerColors = { FLinearColor(0.95f, 0.12f, 0.10f), FLinearColor(0.10f, 0.40f, 1.0f), FLinearColor(1.0f, 0.78f, 0.05f), FLinearColor(0.15f, 0.85f, 0.35f), FLinearColor(0.75f, 0.25f, 1.0f) };
	PlayerRingMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Plane.Plane")));
	PlayerRingMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/VaultHeist/Characters/Materials/M_VHPlayerRing.M_VHPlayerRing")));
}

FLinearColor UVHCharacterVisuals::PlayerColor(int32 Index) const
{
	if (PlayerColors.Num() == 0 || Index < 0)
	{
		return FLinearColor::White;
	}
	return PlayerColors[Index % PlayerColors.Num()];
}

const FVHCharacterVisual& UVHCharacterVisuals::Find(int32 CharacterId) const
{
	// 칸이 있어도 메시가 비어 있으면(예: 리깅본을 기다리는 도적3) 기본(도적1) 모습 전체를 쓴다 → 메시만 넣으면 그 칸이 살아난다
	if (const FVHCharacterVisual* Found = Characters.Find(CharacterId); Found && !Found->Mesh.IsNull())
	{
		return *Found;
	}
	return Default;
}

const FVHHoldPose& UVHCharacterVisuals::HoldPose(const FVHCharacterVisual& Visual, uint8 WeaponId, uint8 Category)
{
	if (const FVHHoldPose* Own = Visual.WeaponPoses.Find(static_cast<EVHWeaponId>(WeaponId)))
	{
		return *Own;
	}
	switch (Category)
	{
	case 1:		return Visual.RangedPose;
	case 2:		return Visual.ThrownPose;
	case 3:		return Visual.PlacedPose;
	default:	return Visual.MeleePose;
	}
}

void UVHCharacterVisuals::ApplyMesh(USkeletalMeshComponent* Target, const FVHCharacterVisual& Visual, bool bUseAnimClass)
{
	if (!Target)
	{
		return;
	}
	if (USkeletalMesh* Mesh = Visual.Mesh.LoadSynchronous(); Mesh && Target->GetSkeletalMeshAsset() != Mesh)
	{
		Target->SetSkeletalMeshAsset(Mesh);
	}
	if (bUseAnimClass)
	{
		if (UClass* Anim = Visual.AnimClass.LoadSynchronous(); Anim && Target->GetAnimClass() != Anim)
		{
			Target->SetAnimInstanceClass(Anim);
		}
	}
}
