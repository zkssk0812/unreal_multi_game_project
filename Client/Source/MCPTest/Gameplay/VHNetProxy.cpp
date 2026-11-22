#include "Gameplay/VHNetProxy.h"

#include "Network/VHProtocol.h"
#include "Network/VHSettings.h"
#include "Gameplay/VHInteractionConfig.h"
#include "Gameplay/VHFlashlightComponent.h"
#include "Gameplay/VHCharacterVisuals.h"
#include "Gameplay/VHWeaponVisualComponent.h"
#include "Gameplay/VHThiefAnimInstance.h"
#include "Gameplay/VHGameplayConfig.h"
#include "UnrealClient.h"
#include "TimerManager.h"
#include "Misc/Paths.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "UObject/ConstructorHelpers.h"

using namespace RobProto;

namespace
{
	UStaticMesh* LoadProxyShape(const TCHAR* Name)
	{
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
	}

	void ProxyTint(UStaticMeshComponent* Mesh, const FLinearColor& Color)
	{
		if (!Mesh || !Mesh->GetMaterial(0))
		{
			return;
		}
		if (UMaterialInstanceDynamic* MID = Mesh->CreateDynamicMaterialInstance(0, Mesh->GetMaterial(0)))
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}
}

AVHNetProxy::AVHNetProxy()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// 다른 의적: 템플릿 마네킹 (캐릭터와 같은 메시, 발 = 액터 위치)
	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// 애니메이션 = 내 캐릭터와 같은 팔 자세 코드(UVHThiefAnimInstance). 그래프가 없어 서기·걷기·달리기를 직접 섞는다
	Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Body->SetAnimInstanceClass(UVHThiefAnimInstance::StaticClass());
	// 메시·애니메이션은 InitProxy 에서 DA_VHCharacterVisuals(내 캐릭터와 같은 표)의 것을 입힌다

	// 든 무기 표시 (내 캐릭터와 같은 컴포넌트). 메시는 이 액터가 직접 입히므로 컴포넌트에는 맡기지 않는다
	WeaponVisual = CreateDefaultSubobject<UVHWeaponVisualComponent>(TEXT("WeaponVisual"));
	WeaponVisual->bApplyCharacterMesh = false;
	WeaponVisual->TargetMeshComponentName = TEXT("Body");

	// 경비원: 임시 기본 도형 (발 = 액터 위치이므로 캡슐 반높이 96만큼 올린다)
	auto MakeShape = [this](const TCHAR* Name, const TCHAR* Shape, const FVector& Loc, const FVector& Scale)
	{
		UStaticMeshComponent* M = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		M->SetupAttachment(Root);
		M->SetStaticMesh(LoadProxyShape(Shape));
		M->SetRelativeLocation(Loc + FVector(0.f, 0.f, 96.f));
		M->SetRelativeScale3D(Scale);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetVisibility(false);
		return M;
	};
	GuardBody = MakeShape(TEXT("GuardBody"), TEXT("Cylinder"), FVector(0.f, 0.f, -18.f), FVector(0.75f, 0.75f, 1.55f));
	GuardHead = MakeShape(TEXT("GuardHead"), TEXT("Sphere"), FVector(0.f, 0.f, 78.f), FVector(0.48f));
	GuardCap = MakeShape(TEXT("GuardCap"), TEXT("Cylinder"), FVector(0.f, 0.f, 104.f), FVector(0.52f, 0.52f, 0.12f));
	GuardNose = MakeShape(TEXT("GuardNose"), TEXT("Cone"), FVector(30.f, 0.f, 78.f), FVector(0.14f, 0.14f, 0.22f));
	GuardNose->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
	GuardGun = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GuardGun"));
	GuardGun->SetupAttachment(Body);
	GuardGun->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GuardGun->SetVisibility(false);

	// 이름표 (TextRender 기본 글꼴은 한글이 없어 영문 표시)
	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Root);
	Label->SetRelativeLocation(FVector(0.f, 0.f, 230.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextBottom);
	Label->SetWorldSize(28.f);
	Label->SetTextRenderColor(FColor(255, 230, 120));

	// 손전등 주변 빛 (다른 의적이 손전등을 켰을 때만, 값은 InitProxy 에서 설정에 맞춘다)
	Aura = CreateDefaultSubobject<UPointLightComponent>(TEXT("FlashlightAura"));
	Aura->SetupAttachment(Root);
	Aura->SetVisibility(false);

	// 손전등 원뿔: 내 손전등과 같은 자리(가슴 앞)·각도. 내 캐릭터는 캡슐 가운데 기준이고 여기는 발 기준이라 캡슐 반높이(96)를 더한다
	Beam = CreateDefaultSubobject<USpotLightComponent>(TEXT("FlashlightBeam"));
	Beam->SetupAttachment(Root);
	Beam->SetRelativeLocation(FVector(UVHFlashlightComponent::DefaultForward, 0.f, 96.f + UVHFlashlightComponent::DefaultUp));
	Beam->SetRelativeRotation(FRotator(UVHFlashlightComponent::DefaultPitch, 0.f, 0.f));
	Beam->SetVisibility(false);
}

bool AVHNetProxy::bDebugHideOtherLights = false;

namespace
{
	// vh.ProxyTest N [dx dy [캐릭터]] / off : 서버 없이 내 옆에 "다른 플레이어 표시"(AVHNetProxy)를 세운다 → 다른 플레이어 화면과 같은 코드로 무기·동작 확인.
	// vh.WeaponAnim N [초] 를 치면 이 표시도 같은 공격 동작을 재생한다(멈춤 포함)
	FAutoConsoleCommandWithWorldAndArgs GVHProxyTestCommand(TEXT("vh.ProxyTest"),
		TEXT("개발용: vh.ProxyTest N [dx dy [캐릭터]] — 내 옆(cm)에 무기 N을 든 다른 플레이어 표시를 세운다(캐릭터 번호 기본 0 = 도적1). off = 치우기."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			for (TActorIterator<AVHNetProxy> It(World); It; ++It)
			{
				if (It->ActorHasTag(TEXT("VHTestProxy")))
				{
					It->Destroy();
				}
			}
			UVHWeaponVisualComponent::bDebugFreezeProxies = false;
			APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
			if (!Pawn || Args.Num() == 0 || Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
			{
				return;
			}
			const uint8 Weapon = static_cast<uint8>(FMath::Clamp(FCString::Atoi(*Args[0]), 0, 10));
			const FVector Offset(Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.f, Args.Num() > 2 ? FCString::Atof(*Args[2]) : 180.f, 0.f);
			const float HalfHeight = Pawn->GetSimpleCollisionHalfHeight();
			const FVector Feet = Pawn->GetActorLocation() - FVector(0.f, 0.f, HalfHeight) + Offset;
			const float Yaw = Pawn->GetActorRotation().Yaw;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (AVHNetProxy* Proxy = World->SpawnActor<AVHNetProxy>(AVHNetProxy::StaticClass(), Feet, FRotator(0.f, Yaw, 0.f), Params))
			{
				Proxy->Tags.Add(TEXT("VHTestProxy"));
				Proxy->InitProxy(60000, static_cast<uint8>(RobProto::EntityKind::Thief), TEXT("Proxy"), Args.Num() > 3 ? FCString::Atoi(*Args[3]) : 0);
				Proxy->SnapTo(Feet, Yaw);
				Proxy->PushSnapshot(FPlatformTime::Seconds(), Feet, Yaw, 0, 100, Weapon);
				Proxy->SetPlayerColorIndex(1);
				UVHWeaponVisualComponent::bDebugFreezeProxies = true;
			}
		}));
}

void AVHNetProxy::InitProxy(uint16 InEntityId, uint8 InKind, const FString& InName, int32 InCharacterId)
{
	EntityId = InEntityId;
	Kind = InKind;
	DisplayName = InName;
	CharacterId = InCharacterId;
	const bool bGuard = Kind == static_cast<uint8>(EntityKind::Guard);
	// 로봇 경비원 모델이 있으면 스켈레탈 메시로 (DA_VHCharacterVisuals → Guard). 없으면 예전 기본 도형
	const FVHGuardVisual& GuardVisual = UVHSettings::Get().GetCharacterVisuals().Guard;
	USkeletalMesh* GuardMesh = bGuard ? GuardVisual.Mesh.LoadSynchronous() : nullptr;
	bGuardSkeletal = GuardMesh != nullptr;
	Body->SetVisibility(!bGuard || bGuardSkeletal);
	for (UStaticMeshComponent* M : { GuardBody.Get(), GuardHead.Get(), GuardCap.Get(), GuardNose.Get() })
	{
		M->SetVisibility(bGuard && !bGuardSkeletal);
	}
	if (bGuardSkeletal)
	{
		Body->SetSkeletalMeshAsset(GuardMesh);
		if (Body->GetAnimClass() != UVHThiefAnimInstance::StaticClass())
		{
			Body->SetAnimInstanceClass(UVHThiefAnimInstance::StaticClass());
		}
		IdleAnim = GuardVisual.IdleAnim.LoadSynchronous();
		WalkAnim = GuardVisual.WalkAnim.LoadSynchronous();
		RunAnim = GuardVisual.RunAnim.LoadSynchronous();
		GuardAttackAnim = GuardVisual.AttackAnim.LoadSynchronous();
		GuardStunAnim = GuardVisual.StunAnim.LoadSynchronous();
		if (UVHThiefAnimInstance* Anim = Cast<UVHThiefAnimInstance>(Body->GetAnimInstance()))
		{
			Anim->SetBlendSpaceOverride(nullptr, true);	// 경비원은 의적 블렌드스페이스를 쓰지 않는다 (서기·걷기·달리기)
			Anim->SetLocomotionSpeeds(GuardVisual.RunSpeed, GuardVisual.WalkAnimSpeed, GuardVisual.RunAnimSpeed);
		}
		if (UStaticMesh* Gun = GuardVisual.GunMesh.LoadSynchronous())
		{
			GuardGun->SetStaticMesh(Gun);
			GuardGun->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, GuardVisual.GunBone);
			GuardGun->SetRelativeTransform(GuardVisual.GunOffset);
			GuardGun->SetVisibility(true);
		}
		Body->SetCastShadow(UVHSettings::Get().GetInteractionConfig().bThiefCastsShadow);
		Label->SetTextRenderColor(FColor(255, 90, 80));
	}
	else if (bGuard)
	{
		ProxyTint(GuardBody, FLinearColor(0.55f, 0.08f, 0.06f));
		ProxyTint(GuardHead, FLinearColor(0.6f, 0.45f, 0.3f));
		ProxyTint(GuardCap, FLinearColor(0.05f, 0.05f, 0.08f));
		ProxyTint(GuardNose, FLinearColor(0.6f, 0.45f, 0.3f));
		Label->SetTextRenderColor(FColor(255, 90, 80));
	}
	else
	{
		// 캐릭터 모습: 방에서 고른 캐릭터 번호의 메시·애니메이션 (칸이 없거나 메시가 비면 기본 = 도적1). 내 캐릭터와 같은 표를 본다
		const FVHCharacterVisual& Visual = UVHSettings::Get().GetCharacterVisuals().Find(CharacterId);
		UVHCharacterVisuals::ApplyMesh(Body, Visual, false);
		// 확인용: 모든 클라이언트에서 같은 사람이 같은 캐릭터로 보이는지 (서버 EntitySpawn.type_id = 방에서 고른 캐릭터)
		UE_LOG(LogTemp, Display, TEXT("[캐릭터] 다른 플레이어 엔티티 %d '%s' 캐릭터 %d → 메시 %s"), EntityId, *InName, CharacterId, *Visual.Mesh.ToSoftObjectPath().GetAssetName());
		IdleAnim = Visual.IdleAnim.LoadSynchronous();
		WalkAnim = Visual.WalkAnim.LoadSynchronous();
		RunAnim = Visual.RunAnim.LoadSynchronous();
		if (Body->GetAnimClass() != UVHThiefAnimInstance::StaticClass())
		{
			Body->SetAnimInstanceClass(UVHThiefAnimInstance::StaticClass());
		}
		WeaponVisual->SetCharacterId(CharacterId);
		WeaponVisual->SetWeapon(Weapon);
		// 다른 의적도 내 캐릭터와 같게: 몸 그림자 끔(설정), 손전등 주변 빛 (발 = 액터 위치)
		Body->SetCastShadow(UVHSettings::Get().GetInteractionConfig().bThiefCastsShadow);
		UVHFlashlightComponent::ConfigureAura(Aura, 0.f);
		UVHFlashlightComponent::ConfigureBeam(Beam);
	}
	UpdateLabel();
	UpdateAnimation(0.f);
}

void AVHNetProxy::SnapTo(const FVector& Pos, float Yaw)
{
	Samples.Reset();
	SetActorLocationAndRotation(Pos, FRotator(0.f, Yaw, 0.f));
	LastDrawPos = Pos;
}

void AVHNetProxy::PushSnapshot(double ClientSeconds, const FVector& Pos, float Yaw, uint32 InState, uint16 InHp, uint8 InWeapon)
{
	FSample S;
	S.Time = ClientSeconds;
	S.Pos = Pos;
	S.Yaw = Yaw;
	Samples.Add(S);
	if (Samples.Num() > 20)
	{
		Samples.RemoveAt(0, Samples.Num() - 20);
	}
	if (State != InState || Hp != InHp || Weapon != InWeapon)
	{
		// 구르기 시작(ES_DODGING 이 새로 켜짐): 화면은 InterpDelay 만큼 늦게 그리므로 이 스냅샷 시점이 화면에 올 때 시작한다
		if (Kind == static_cast<uint8>(EntityKind::Thief) && (InState & ES_DODGING) && !(State & ES_DODGING) && !(InState & ES_DOWNED))
		{
			PendingRollAt = ClientSeconds;
		}
		if ((InState & ES_DOWNED) && WeaponVisual)
		{
			PendingRollAt = -1.0;
			WeaponVisual->StopRoll();
		}
		// 쓰러짐: 쓰러지는 애니메이션을 잠깐 보여 준 뒤 숨긴다 (리스폰 대기 중 숨김은 그대로)
		if (Kind == static_cast<uint8>(EntityKind::Thief) && WeaponVisual)
		{
			if ((InState & ES_DOWNED) && !(State & ES_DOWNED))
			{
				DownShowSeconds = WeaponVisual->PlayDown();
				DownShownAt = DownShowSeconds > 0.f ? FPlatformTime::Seconds() + InterpDelay : -1.0;
				bDownHidden = false;
			}
			else if (!(InState & ES_DOWNED) && (State & ES_DOWNED))
			{
				WeaponVisual->StopDown();
				DownShownAt = -1.0;
				bDownHidden = false;
			}
		}
		State = InState;
		Hp = InHp;
		Weapon = InWeapon;
		if (Kind == static_cast<uint8>(EntityKind::Thief))
		{
			// 다른 플레이어가 든 무기 (스냅샷의 weapon). 사제 테이저건을 든 상태(ES_TASER_READY, 2026-11-22)면 테이저건 모델(무기 7번 모양)
			WeaponVisual->SetWeapon((State & ES_TASER_READY) != 0 ? static_cast<uint8>(WeaponId::Taser) : Weapon);
			WeaponVisual->SetAttackHeld((State & ES_ATTACKING) != 0 && (State & ES_DOWNED) == 0);	// 꾹 누르기 자세
			WeaponVisual->SetRailgunLock((State & ES_RAILGUN_LOCK) != 0);						// 레일건 버팀 자세
			// 몸 동작: 금고·상자 작업(무릎 꿇고 살피기), 경직(감전)
			const bool bDown = (State & ES_DOWNED) != 0;
			WeaponVisual->SetBodyStates((State & ES_INTERACTING) != 0 && !bDown, (State & ES_STUNNED) != 0 && !bDown);
		}
		UpdateLabel();
	}
}

void AVHNetProxy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 스냅샷 보간: 지금보다 InterpDelay 앞선 시점을 두 샘플 사이에서 찾는다
	const double RenderTime = FPlatformTime::Seconds() - InterpDelay;
	if (Samples.Num() > 0)
	{
		FVector Pos = Samples.Last().Pos;
		float Yaw = Samples.Last().Yaw;
		for (int32 i = Samples.Num() - 1; i > 0; --i)
		{
			const FSample& A = Samples[i - 1];
			const FSample& B = Samples[i];
			if (A.Time <= RenderTime && RenderTime <= B.Time)
			{
				const float Alpha = static_cast<float>((RenderTime - A.Time) / FMath::Max(B.Time - A.Time, 0.001));
				Pos = FMath::Lerp(A.Pos, B.Pos, Alpha);
				Yaw = FMath::Lerp(FRotator(0.f, A.Yaw, 0.f), FRotator(0.f, B.Yaw, 0.f), Alpha).Yaw;
				break;
			}
		}
		if (Samples[0].Time > RenderTime)
		{
			Pos = Samples[0].Pos;
			Yaw = Samples[0].Yaw;
		}
		SetActorLocationAndRotation(Pos, FRotator(0.f, Yaw, 0.f));
	}

	const FVector Now = GetActorLocation();
	const float Speed = DeltaSeconds > 0.f ? FVector::Dist2D(Now, LastDrawPos) / DeltaSeconds : 0.f;
	const FVector RawVel = DeltaSeconds > 0.f ? (Now - LastDrawPos) / DeltaSeconds : FVector::ZeroVector;
	if (PendingRollAt >= 0.0 && RenderTime >= PendingRollAt)
	{
		PendingRollAt = -1.0;
		StartRollFromSamples();
	}
	// 구르기 처음 0.15초: 화면에서 실제로 움직이는 방향으로 구르는 방향을 다듬는다 (구르기 속도 9m/s 라 걷기·달리기와 구분된다)
	if (RollShownAt >= 0.0 && WeaponVisual && WeaponVisual->IsRolling() && FPlatformTime::Seconds() - RollShownAt < 0.15 && Speed > 400.f)
	{
		WeaponVisual->SetRollDirection(Now - LastDrawPos);
	}
	LastDrawPos = Now;
	SmoothedSpeed = FMath::FInterpTo(SmoothedSpeed, Speed, DeltaSeconds, 8.f);
	SmoothedVelocity = FMath::VInterpTo(SmoothedVelocity, FVector(RawVel.X, RawVel.Y, 0.0), DeltaSeconds, 8.f);
	UpdateAnimation(SmoothedSpeed);

	// 쓰러지는 모습을 다 보여 줬으면 숨긴다
	if (DownShownAt >= 0.0 && !bDownHidden && FPlatformTime::Seconds() - DownShownAt >= DownShowSeconds)
	{
		bDownHidden = true;
		UpdateLabel();
	}

	// 경직된 경비원은 몸을 기울여 표시
	if (Kind == static_cast<uint8>(EntityKind::Guard) && bGuardSkeletal)
	{
		const float Target = (State & (ES_STUNNED | ES_REPAIRING)) ? 20.f : 0.f;
		const float Cur = static_cast<float>(FRotator(Body->GetRelativeRotation()).Pitch);
		const float Next = FMath::FInterpTo(Cur, Target, DeltaSeconds, 10.f);
		// 메시 컴포넌트는 Yaw -90 이라 Pitch 가 액터의 옆(좌우) 기울기가 된다
		Body->SetRelativeRotation(FRotator(Next, BodyBaseRotation.Yaw, 0.f));
	}
	else if (Kind == static_cast<uint8>(EntityKind::Guard))
	{
		const float Target = (State & (ES_STUNNED | ES_REPAIRING)) ? 25.f : 0.f;
		FRotator R = GuardBody->GetRelativeRotation();
		R.Roll = FMath::FInterpTo(R.Roll, Target, DeltaSeconds, 10.f);
		GuardBody->SetRelativeRotation(R);
	}

	// 이름표는 카메라를 바라본다
	if (const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		const FVector ToCam = Cam->GetCameraLocation() - Label->GetComponentLocation();
		Label->SetWorldRotation(ToCam.Rotation());
	}
}

void AVHNetProxy::StartRollFromSamples()
{
	if (!WeaponVisual)
	{
		return;
	}
	// 구르는 방향 = 구르기가 켜진 스냅샷부터 0.25초 안의 위치 변화 (구르는 사람이 보낸 실제 이동). 아직 없으면 직전 이동, 그것도 없으면 바라보는 쪽
	const double RenderTime = FPlatformTime::Seconds() - InterpDelay;
	FVector From = GetActorLocation(), To = From;
	for (const FSample& S : Samples)
	{
		if (S.Time >= RenderTime - 0.06 && S.Time <= RenderTime + 0.25)
		{
			To = S.Pos;
		}
	}
	FVector Dir = (To - From).GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		Dir = (From - LastDrawPos).GetSafeNormal2D();
	}
	if (Dir.IsNearlyZero())
	{
		Dir = GetActorForwardVector();
	}
	const float Seconds = UVHSettings::Get().GetGameplayConfig().ThiefStats.RollSeconds;
	WeaponVisual->PlayRoll(Dir, Seconds);
	RollShownAt = FPlatformTime::Seconds();
	// 개발용(-VHRollShotsProxy): 다른 플레이어가 구르는 장면 저장. 화면 저장은 앞 요청이 끝나야 다음이 처리되므로(0.2초 간격도 빠짐)
	// 구르기 4번에 나눠 0.4초 간격 2장씩 → 합치면 0.1초 간격 6장 (같은 동작이라 이어 볼 수 있다)
	static const bool bShots = FParse::Param(FCommandLine::Get(), TEXT("VHRollShotsProxy"));
	static int32 ShotRolls = 0;
	if (bShots && ShotRolls < 4 && GetWorld())
	{
		const int32 Roll = ++ShotRolls;
		// 구르기 4번: 1번째 0·0.4초, 2번째 0.1·0.5초, 3번째 0.2초, 4번째 0.3초 (비율 0·20·40·60·80·100%)
		static const float Plan[4][2] = { { 0.f, 0.8f }, { 0.2f, 1.f }, { 0.4f, -1.f }, { 0.6f, -1.f } };
		for (int32 i = 0; i < 2; ++i)
		{
			if (Plan[Roll - 1][i] < 0.f)
			{
				continue;
			}
			FTimerHandle Handle;
			const float At = Seconds * Plan[Roll - 1][i];
			const FString File = FPaths::ProjectSavedDir() / TEXT("V3Test") / FString::Printf(TEXT("roll_proxy_%d_t%03d.png"), Roll, FMath::RoundToInt(At * 1000.f));
			TWeakObjectPtr<AVHNetProxy> Self(this);
			GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([File, Self]()
			{
				FScreenshotRequest::RequestScreenshot(File, false, false);
				// 비교 이미지에서 자를 자리: 이 사람의 화면 좌표 (몸 가운데)
				FVector2D Screen(-1.f, -1.f);
				if (APlayerController* PC = Self.IsValid() ? UGameplayStatics::GetPlayerController(Self.Get(), 0) : nullptr)
				{
					PC->ProjectWorldLocationToScreen(Self->GetActorLocation() + FVector(0.f, 0.f, 60.f), Screen);
				}
				UE_LOG(LogTemp, Log, TEXT("[구르기 촬영] 다른 플레이어 %s 화면 (%.0f, %.0f)"), *FPaths::GetCleanFilename(File), Screen.X, Screen.Y);
			}), FMath::Max(At, 0.01f), false);
		}
		UE_LOG(LogTemp, Log, TEXT("[구르기 동작] 다른 플레이어 %s 구르기 화면 저장 %d (방향 %.2f, %.2f)"), *DisplayName, Roll, Dir.X, Dir.Y);
	}
}

void AVHNetProxy::UpdateAnimation(float Speed)
{
	if (Kind != static_cast<uint8>(EntityKind::Thief) && !bGuardSkeletal)
	{
		return;
	}
	if (bGuardSkeletal)
	{
		// 경비원: 사격 중(ES_ATTACKING) 서 있으면 조준 자세. 걷기·달리기는 순찰·추적 속도로
		if (UVHThiefAnimInstance* Anim = Cast<UVHThiefAnimInstance>(Body->GetAnimInstance()))
		{
			Anim->SetLocomotionAnims(IdleAnim, WalkAnim, RunAnim);
			// 경직(테이저) 중이면 감전 동작, 사격 중이면 조준 사격 자세
			UAnimSequence* Override = ((State & ES_STUNNED) && GuardStunAnim) ? GuardStunAnim.Get() : (((State & ES_ATTACKING) && GuardAttackAnim) ? GuardAttackAnim.Get() : nullptr);
			Anim->SetIdleOverride(Override);
			Anim->SetExternalSpeed(Speed);
		}
		return;
	}
	// 서기·걷기·달리기를 속도로 섞고, 든 무기의 팔 자세를 덮는 것은 애니메이션 인스턴스가 한다 (내 캐릭터와 같은 코드)
	if (UVHThiefAnimInstance* Anim = Cast<UVHThiefAnimInstance>(Body->GetAnimInstance()))
	{
		Anim->SetLocomotionAnims(IdleAnim, WalkAnim, RunAnim);
		Anim->SetExternalSpeed(Speed);
		// 이동 블렌드스페이스(앞/뒤/좌/우)는 실제 이동 방향이 필요하다. 크기는 부드럽게 한 속도에 맞춘다
		Anim->SetExternalVelocity(SmoothedVelocity.GetSafeNormal2D() * Speed);
	}
}

bool AVHNetProxy::IsHiddenByState() const
{
	// 연막 안: 플레이어도 경비원도 보이지 않는다 [기획 I-32]. 서버는 위치를 계속 보내므로 클라가 숨긴다
	if (State & ES_IN_SMOKE)
	{
		return true;
	}
	if (Kind != static_cast<uint8>(EntityKind::Thief))
	{
		return false;
	}
	// 쓰러져 리스폰 대기, 접속 끊김, 은신: 다른 클라 화면에서는 보이지 않는다 (쓰러지는 모습을 보여 주는 동안만 예외)
	if ((State & ES_DOWNED) && DownShownAt >= 0.0 && !bDownHidden && !(State & (ES_LEFT_GAME | ES_CLOAKED)))
	{
		return false;
	}
	return (State & (ES_DOWNED | ES_LEFT_GAME | ES_CLOAKED)) != 0;
}

bool AVHNetProxy::IsLabelDrawn() const
{
	return Label && Label->IsVisible() && !Label->bHiddenInGame && !IsHidden();
}

void AVHNetProxy::SetPlayerColorIndex(int32 Index)
{
	if (Kind != static_cast<uint8>(EntityKind::Thief) || !WeaponVisual)
	{
		return;
	}
	WeaponVisual->SetPlayerColorIndex(Index);
	WeaponVisual->SetRingVisible(!IsHiddenByState());
	const UVHCharacterVisuals& Visuals = UVHSettings::Get().GetCharacterVisuals();
	if (Visuals.bTintPlayerLabel && Index >= 0)
	{
		// 이름표 글자: 플레이어 색을 밝게 (어두운 바닥에서도 읽히게)
		const FLinearColor C = FMath::Lerp(Visuals.PlayerColor(Index), FLinearColor::White, 0.25f);
		Label->SetTextRenderColor(C.ToFColor(true));
	}
}

void AVHNetProxy::RefreshLights()
{
	// 서버 v3.0: 손전등 삭제 → 다른 플레이어의 손전등 상태가 오지 않는다. 항상 끈다
	Beam->SetVisibility(false);
	Aura->SetVisibility(false);
}

void AVHNetProxy::UpdateLabel()
{
	FString Text = DisplayName;
	auto Tag = [&Text](const TCHAR* T) { Text += FString::Printf(TEXT(" [%s]"), T); };
	Text += FString::Printf(TEXT("  HP %u"), Hp);
	if (Kind == static_cast<uint8>(EntityKind::Thief))
	{
		if (State & ES_INVULNERABLE) Tag(TEXT("INVULN"));
		if (State & ES_STUNNED) Tag(TEXT("STUN"));
		if (State & ES_SLOWED) Tag(TEXT("SLOW"));
		if (State & ES_FLASHED) Tag(TEXT("FLASH"));
		if (State & ES_EXHAUSTED) Tag(TEXT("TIRED"));
		if (State & ES_INTERACTING) Tag(TEXT("USE"));
		if (State & ES_CARRYING_TREASURE) Tag(TEXT("TREASURE"));
		const bool bHide = IsHiddenByState();
		Body->SetVisibility(!bHide);
		Label->SetVisibility(!bHide);
		WeaponVisual->SetRingVisible(!bHide);
		RefreshLights();
	}
	else
	{
		if (State & ES_REPAIRING) Tag(TEXT("REPAIR"));
		else if (State & ES_STUNNED) Tag(TEXT("STUN"));
		if (State & ES_ATTACKING) Tag(TEXT("FIRE"));
		const bool bHide = IsHiddenByState();
		if (bGuardSkeletal)
		{
			Body->SetVisibility(!bHide);
			GuardGun->SetVisibility(!bHide && GuardGun->GetStaticMesh() != nullptr);
		}
		else
		{
			for (UStaticMeshComponent* M : { GuardBody.Get(), GuardHead.Get(), GuardCap.Get(), GuardNose.Get() })
			{
				M->SetVisibility(!bHide);
			}
		}
		Label->SetVisibility(!bHide);
	}
	if (Text != LastLabel)
	{
		LastLabel = Text;
		Label->SetText(FText::FromString(Text));
	}
}
