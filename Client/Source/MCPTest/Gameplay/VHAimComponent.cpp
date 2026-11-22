#include "Gameplay/VHAimComponent.h"

#include "Gameplay/VHGameplayConfig.h"
#include "Gameplay/VHFlashlightComponent.h"
#include "Gameplay/VHInteractionRules.h"
#include "Gameplay/VHLevelViewSettings.h"
#include "Gameplay/VHStatusEffectComponent.h"
#include "Gameplay/VHThiefStateComponent.h"
#include "Network/VHSettings.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Kismet/KismetMaterialLibrary.h"

UVHAimComponent::UVHAimComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;	// 이번 프레임 이동·회전이 끝난 뒤 시선을 맞춘다
}

void UVHAimComponent::BeginPlay()
{
	Super::BeginPlay();
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	AActor* Owner = GetOwner();
	AimYaw = Owner->GetActorRotation().Yaw;

	ApplyTopDownCamera();
	ApplyRotationMode(IsLocallyControlledPlayer());

	if (C.bShowLookIndicator)
	{
		CreateIndicator();
	}
}

void UVHAimComponent::ApplyTopDownCamera()
{
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	AActor* Owner = GetOwner();
	if (USpringArmComponent* Arm = Owner->FindComponentByClass<USpringArmComponent>())
	{
		// 탑뷰 고정: 캐릭터·컨트롤러 회전과 상관없이 월드 기준 같은 각도.
		// Yaw = 레벨의 AVHLevelViewSettings(없으면 0 → 화면 위 = +X). L_Mansion_1F는 90(화면 위 = +Y)
		Arm->bUsePawnControlRotation = false;
		Arm->bInheritPitch = false;
		Arm->bInheritYaw = false;
		Arm->bInheritRoll = false;
		Arm->SetUsingAbsoluteRotation(true);
		Arm->SetWorldRotation(FRotator(C.CameraPitch, AVHLevelViewSettings::GetTopViewYaw(GetWorld()), 0.f));
		Arm->TargetArmLength = C.CameraDistance;
		Arm->bDoCollisionTest = false;	// 벽 때문에 카메라가 캐릭터 쪽으로 당겨지지 않게
		Arm->bEnableCameraLag = C.CameraLagSpeed > 0.f;
		Arm->CameraLagSpeed = C.CameraLagSpeed;
		Arm->SocketOffset = FVector::ZeroVector;
		Arm->TargetOffset = FVector::ZeroVector;
	}
	if (UCameraComponent* Camera = Owner->FindComponentByClass<UCameraComponent>())
	{
		Camera->bUsePawnControlRotation = false;
		Camera->SetFieldOfView(C.CameraFOV);
	}
}

void UVHAimComponent::CreateIndicator()
{
	AActor* Owner = GetOwner();
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (!Cone || !Owner->GetRootComponent())
	{
		return;
	}
	LookIndicator = NewObject<UStaticMeshComponent>(Owner, TEXT("VHLookIndicator"));
	LookIndicator->SetStaticMesh(Cone);
	LookIndicator->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LookIndicator->SetCastShadow(false);
	LookIndicator->SetupAttachment(Owner->GetRootComponent());
	LookIndicator->SetUsingAbsoluteRotation(true);
	LookIndicator->SetRelativeScale3D(FVector(0.18f, 0.18f, 0.32f));
	LookIndicator->RegisterComponent();
	if (UMaterialInterface* Base = LookIndicator->GetMaterial(0))
	{
		if (UMaterialInstanceDynamic* MID = LookIndicator->CreateDynamicMaterialInstance(0, Base))
		{
			MID->SetVectorParameterValue(TEXT("Color"), UVHSettings::Get().GetGameplayConfig().LookIndicatorColor);
		}
	}
}

bool UVHAimComponent::IsLocallyControlledPlayer() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn && Pawn->IsLocallyControlled() && Pawn->IsPlayerControlled();
}

bool UVHAimComponent::TraceMouseToFloor(FVector& OutPoint) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	FVector Origin, Dir;
	if (!PC || !PC->DeprojectMousePositionToWorld(Origin, Dir) || Dir.Z > -0.01f)
	{
		return false;
	}
	// 캐릭터 발 높이의 수평면과 만나는 점 = "마우스 포인터가 가리키는 바닥 지점"
	float FloorZ = Pawn->GetActorLocation().Z;
	if (const ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		FloorZ -= Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}
	const float T = (FloorZ - Origin.Z) / Dir.Z;
	OutPoint = Origin + Dir * T;
	return true;
}

void UVHAimComponent::ApplyRotationMode(bool bPlayer)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	Character->bUseControllerRotationYaw = false;
	UCharacterMovementComponent* Move = Character->GetCharacterMovement();
	// 내 캐릭터: 몸 전체가 컨트롤러 방향(= 커서 방향)을 따라 돈다 ⟪기획 변경⟫. 더미 등: 이동 방향으로 돈다
	Move->bOrientRotationToMovement = !bPlayer;
	Move->bUseControllerDesiredRotation = bPlayer;
	// 회전 속도가 음수면 즉시 돈다(엔진 규칙)
	Move->RotationRate = FRotator(0.f, C.BodyTurnSpeed > 0.f ? C.BodyTurnSpeed : -1.f, 0.f);
	bPlayerRotationMode = bPlayer;
}

bool UVHAimComponent::CanTurn() const
{
	if (bTurnLocked)
	{
		return false;	// 레일건 충전 1초 + 발사 1초: 회전 불가 (서버 ES_RAILGUN_LOCK)
	}
	const AActor* Owner = GetOwner();
	if (const UVHStatusEffectComponent* Status = Owner->FindComponentByClass<UVHStatusEffectComponent>(); Status && Status->IsStunned())
	{
		return false;
	}
	if (const UVHThiefStateComponent* State = Owner->FindComponentByClass<UVHThiefStateComponent>(); State && !State->IsFree())
	{
		return false;
	}
	return true;
}

float UVHAimComponent::GetLookYaw() const
{
	return GetOwner()->GetActorRotation().Yaw;
}

FVector UVHAimComponent::ComputeMoveVector(float Forward, float Right) const
{
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	if (IsScreenAxisMove())
	{
		// 화면(월드) 축 고정 [사용자 결정 2026-11-13, D1]: W = 화면 위(탑뷰 카메라가 바라보는 방향), D = 화면 오른쪽.
		// 커서가 어디에 있든 이동 방향은 같고, 방향별 속도 배율은 쓰지 않는다. 시선·공격 방향은 그대로 커서.
		return VHRules::AimRelativeMove(Forward, Right, AVHLevelViewSettings::GetTopViewYaw(GetWorld()));
	}
	return VHRules::AimRelativeMove(Forward, Right, AimYaw, C.ForwardSpeedScale, C.BackwardSpeedScale, C.StrafeSpeedScale);
}

static TAutoConsoleVariable<int32> CVarVHMoveMode(TEXT("vh.MoveMode"), 0,
	TEXT("이동 기준. 0 = DA_VHGameplayConfig의 bScreenAxisMove, 1 = 화면(월드) 축 고정, 2 = 커서 방향 기준"));

bool UVHAimComponent::IsScreenAxisMove()
{
	const int32 Mode = CVarVHMoveMode.GetValueOnGameThread();
	return Mode == 1 || (Mode != 2 && UVHSettings::Get().GetGameplayConfig().bScreenAxisMove);
}

void UVHAimComponent::SnapLookYaw(float WorldYaw)
{
	AimYaw = WorldYaw;
	AActor* Owner = GetOwner();
	Owner->SetActorRotation(FRotator(0.f, WorldYaw, 0.f));
	if (const APawn* Pawn = Cast<APawn>(Owner); Pawn && Pawn->GetController())
	{
		Pawn->GetController()->SetControlRotation(FRotator(0.f, WorldYaw, 0.f));
	}
	ApplyFollowers();
}

void UVHAimComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	const AActor* Owner = GetOwner();
	const bool bPlayer = IsLocallyControlledPlayer();
	if (bPlayer != bPlayerRotationMode)
	{
		ApplyRotationMode(bPlayer);	// 빙의가 BeginPlay보다 늦게 끝나는 경우
	}

	if (bPlayer)
	{
		FVector Point;
		if (TraceMouseToFloor(Point))
		{
			AimPoint = Point;
			if (CanTurn())
			{
				// 커서가 캐릭터에서 AimDeadZone 안이면 마지막 방향 유지(흔들림 방지)
				AimYaw = VHRules::AimYawToPoint(Owner->GetActorLocation(), Point, AimYaw, C.AimDeadZone);
			}
		}
		UpdateOcclusion(C);
	}
	else
	{
		AimYaw = Owner->GetActorRotation().Yaw;
	}
	ApplyFollowers();
}

void UVHAimComponent::UpdateOcclusion(const UVHGameplayConfig& C)
{
	// 벽 재질(M_VHWallFade)이 이 값으로 "카메라 → 내 캐릭터" 선 주변의 벽 픽셀을 지운다
	UMaterialParameterCollection* Collection = C.OcclusionCollection.LoadSynchronous();
	UWorld* World = GetWorld();
	if (!Collection || !World)
	{
		return;
	}
	UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, TEXT("Enabled"), C.bWallOcclusionFade ? 1.f : 0.f);
	UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, TEXT("Radius"), C.OcclusionRadius);
	UKismetMaterialLibrary::SetVectorParameterValue(World, Collection, TEXT("PlayerPos"), FLinearColor(GetOwner()->GetActorLocation()));
}

void UVHAimComponent::ApplyFollowers()
{
	const AActor* Owner = GetOwner();
	// 손전등은 몸에 붙어 있어 몸과 함께 돈다 (예전처럼 몸 기준 각도로 따로 돌리지 않는다)

	// 머리 위 시선 화살표 (원뿔 끝이 시선 방향)
	if (LookIndicator)
	{
		float HeadZ = 110.f;
		if (const ACharacter* Character = Cast<ACharacter>(Owner))
		{
			HeadZ = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20.f;
		}
		const FVector Dir = GetLookDirection();
		LookIndicator->SetWorldLocationAndRotation(Owner->GetActorLocation() + FVector(0.f, 0.f, HeadZ) + Dir * 22.f,
			FRotator(-90.f, GetLookYaw(), 0.f));
	}
}

FVector UVHAimComponent::GetLookDirection() const
{
	return FRotator(0.f, GetLookYaw(), 0.f).Vector();
}
