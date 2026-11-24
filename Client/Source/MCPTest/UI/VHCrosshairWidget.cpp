#include "UI/VHCrosshairWidget.h"

#include "Gameplay/VHInteractionSubsystem.h"
#include "Gameplay/VHServerGameSubsystem.h"
#include "Gameplay/VHAimComponent.h"
#include "Gameplay/VHSoundConfig.h"
#include "Gameplay/VHWeaponCatalog.h"
#include "Network/VHProtocol.h"
#include "Network/VHSettings.h"
#include "UI/VHUIStyle.h"
#include "MCPTest.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "GenericPlatform/ICursor.h"
#include "Misc/Paths.h"
#include "Containers/Ticker.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogVHCrosshair, Log, All);

namespace VHCrosshairDev
{
	// 개발 명령 vh.Crosshair 로 바꾸는 값 (모든 조준점 공통)
	int32 Enabled = -1;						// -1 = DA_VHUIStyle bCrosshairEnabled, 0 끔, 1 켬
	int32 PinMode = 0;						// 0 = 마우스, 1 = 내 캐릭터 + (dx, dy) cm 고정, 2 = 가장 가까운 다른 플레이어(없으면 경비원)
	FVector2D PinOffset = FVector2D(300.0, 0.0);
	int32 WeaponOverride = -1;				// -1 = 서버 무기, 0~10 = 그 무기 모양으로 보기만, 100 = 사제 테이저건
	int32 Gallery = 0;						// 1 = 모든 모양·상태를 화면에 나란히 (비교 촬영용)
	TWeakObjectPtr<UVHCrosshairWidget> Active;
}

namespace
{
	/** 한 획: 어두운 테두리를 먼저 모두 그리고, 밝은 본체를 그 위에 그린다 */
	struct FStroke
	{
		TArray<FVector2f> Points;
		bool bClosed = false;
		FLinearColor Color = FLinearColor::White;
		float Thickness = 4.f;
	};

	void AddArc(TArray<FVector2f>& Out, const FVector2D& C, float R, float A0Deg, float A1Deg)
	{
		// 화면 각도: 0 = 오른쪽, + = 시계 방향 (화면 y 가 아래로)
		const float Span = A1Deg - A0Deg;
		const int32 N = FMath::Clamp(FMath::CeilToInt(FMath::Abs(Span) * R / 240.f), 6, 96);
		for (int32 i = 0; i <= N; ++i)
		{
			const float A = FMath::DegreesToRadians(A0Deg + Span * i / N);
			Out.Add(FVector2f(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R));
		}
	}

	FStroke Circle(const FVector2D& C, float R, const FLinearColor& Color, float T)
	{
		FStroke S;
		AddArc(S.Points, C, R, 0.f, 360.f);
		S.Points.Pop();
		S.bClosed = true;
		S.Color = Color;
		S.Thickness = T;
		return S;
	}

	FStroke Arc(const FVector2D& C, float R, float A0, float A1, const FLinearColor& Color, float T)
	{
		FStroke S;
		AddArc(S.Points, C, R, A0, A1);
		S.Color = Color;
		S.Thickness = T;
		return S;
	}

	FStroke Seg(const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float T)
	{
		FStroke S;
		S.Points = { FVector2f(A), FVector2f(B) };
		S.Color = Color;
		S.Thickness = T;
		return S;
	}

	/** 십자 4획: 가운데에서 Gap 부터 Len 까지 (위·아래·왼쪽·오른쪽) */
	void AddCross(TArray<FStroke>& Out, const FVector2D& C, float Gap, float Len, const FLinearColor& Color, float T)
	{
		const FVector2D Dirs[4] = { FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1) };
		for (const FVector2D& D : Dirs)
		{
			Out.Add(Seg(C + D * Gap, C + D * Len, Color, T));
		}
	}

	FLinearColor WithAlpha(FLinearColor C, float A)
	{
		C.A *= A;
		return C;
	}

	float MaxAmmoOf(uint8 WeaponId)
	{
		// [서버 GameData.h WeaponTypes ammo] 0 = 무한
		switch (static_cast<EVHWeaponId>(WeaponId))
		{
		case EVHWeaponId::BeamSword:	return 6.f;
		case EVHWeaponId::Pistol:		return 12.f;
		case EVHWeaponId::Flamethrower:	return 90.f;
		case EVHWeaponId::Railgun:		return 2.f;
		case EVHWeaponId::Taser:		return 1.f;
		case EVHWeaponId::Grenade:		return 1.f;
		case EVHWeaponId::BlastMine:	return 1.f;
		case EVHWeaponId::ShockMine:	return 1.f;
		default:						return 0.f;
		}
	}

	const TCHAR* ShapeName(EVHCrosshairShape Shape)
	{
		switch (Shape)
		{
		case EVHCrosshairShape::Brackets:	return TEXT("괄호");
		case EVHCrosshairShape::Cross:		return TEXT("십자");
		case EVHCrosshairShape::RailRing:	return TEXT("레일건 링");
		case EVHCrosshairShape::Cone:		return TEXT("부채꼴");
		case EVHCrosshairShape::Area:		return TEXT("착탄 원");
		case EVHCrosshairShape::Lightning:	return TEXT("번개");
		default:							return TEXT("자동");
		}
	}

	FVector FeetOf(const APawn* Pawn)
	{
		const ACharacter* Character = Cast<ACharacter>(Pawn);
		const float Half = Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 96.f;
		return Pawn ? Pawn->GetActorLocation() - FVector(0.f, 0.f, Half) : FVector::ZeroVector;
	}
}

bool UVHCrosshairWidget::IsEnabledByConfig()
{
	return VHCrosshairDev::Enabled >= 0 ? VHCrosshairDev::Enabled != 0 : UVHSettings::Get().GetStyle().bCrosshairEnabled;
}

UVHCrosshairWidget* UVHCrosshairWidget::GetActive()
{
	return VHCrosshairDev::Active.Get();
}

void UVHCrosshairWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);	// 클릭은 그대로 게임으로 (조준점은 그리기만)
	VHCrosshairDev::Active = this;
	BindServer();
	// 시험 옵션 -VHCrosshairFollow: 봇 화면에는 마우스가 없으므로 조준점을 가장 가까운 다른 플레이어에 고정 (= vh.Crosshair follow)
	if (FParse::Param(FCommandLine::Get(), TEXT("VHCrosshairFollow")))
	{
		VHCrosshairDev::PinMode = 2;
	}
	// 시험 옵션 -VHSetRes=1920x1080wf: 게임에 들어오면 화면 크기를 바꾼다 (창 모드 1920x1080 은 작업 표시줄 때문에 줄어든다 → 테두리 없는 전체 화면으로 실제 1080p 촬영)
	FString SetRes;
	if (FParse::Value(FCommandLine::Get(), TEXT("VHSetRes="), SetRes) && GEngine && GetWorld())
	{
		GEngine->Exec(GetWorld(), *(TEXT("r.SetRes ") + SetRes));
	}
}

void UVHCrosshairWidget::NativeDestruct()
{
	if (UVHServerGameSubsystem* Server = BoundServer.Get())
	{
		Server->OnMyShot.Remove(ShotHandle);
		Server->OnMyHitConfirmed.Remove(HitHandle);
	}
	BoundServer.Reset();
	if (VHCrosshairDev::Active.Get() == this)
	{
		VHCrosshairDev::Active.Reset();
	}
	Super::NativeDestruct();
}

void UVHCrosshairWidget::BindServer()
{
	UWorld* World = GetWorld();
	UVHServerGameSubsystem* Server = World ? World->GetSubsystem<UVHServerGameSubsystem>() : nullptr;
	if (!Server || BoundServer.Get() == Server)
	{
		return;
	}
	ShotHandle = Server->OnMyShot.AddUObject(this, &UVHCrosshairWidget::HandleMyShot);
	HitHandle = Server->OnMyHitConfirmed.AddUObject(this, &UVHCrosshairWidget::HandleMyHit);
	BoundServer = Server;
}

void UVHCrosshairWidget::HandleMyShot(uint8 WeaponId)
{
	// 반동 벌어짐: 순간 벌어지고(쌓이되 최대 배수까지) NativeTick 에서 부드럽게 돌아온다. 판정과 무관
	const UVHUIStyle& S = UVHSettings::Get().GetStyle();
	const float Add = State.Spec.KickPx * S.CrosshairKickScale;
	const float Before = KickPx;
	KickPx = FMath::Min(KickPx + Add, State.Spec.KickPx * S.CrosshairKickScale * FMath::Max(S.CrosshairKickMaxStack, 1.f));
	++ShotsSeen;
	// 화염방사기(초당 30번)는 로그를 0.5초에 한 번만
	const double Now = FPlatformTime::Seconds();
	if (static_cast<EVHWeaponId>(WeaponId) != EVHWeaponId::Flamethrower || Now - LastShotLogTime > 0.5)
	{
		LastShotLogTime = Now;
		UE_LOG(LogVHCrosshair, Log, TEXT("[조준점] 반동 %s: %.1f → %.1fpx (+%.1f, 복귀 %.2f초)"),
			UVHServerGameSubsystem::WeaponName(static_cast<RobProto::WeaponId>(WeaponId)), Before, KickPx, Add, State.Spec.KickRecoverSeconds);
	}
}

void UVHCrosshairWidget::HandleMyHit(bool bKill, uint8 TargetKind)
{
	// 서버 판정(SC_DAMAGE / SC_DOWNED)으로만 온다. 쓰러뜨림이 맞힘을 덮는다 (같은 틱에 둘 다 오면 쓰러뜨림 색)
	const double Now = FPlatformTime::Seconds();
	const UVHUIStyle& S = UVHSettings::Get().GetStyle();
	const bool bKillShowing = bMarkerKill && MarkerStartReal > 0.0 && Now - MarkerStartReal < S.KillMarkerSeconds;
	if (!bKill && bKillShowing)
	{
		return;
	}
	MarkerStartReal = Now;
	bMarkerKill = bKill;
	bKill ? ++KillsSeen : ++HitsSeen;
	UVHSoundConfig::Play(this, bKill ? EVHSound::KillMarker : EVHSound::HitMarker, FVector::ZeroVector);
	UE_LOG(LogVHCrosshair, Log, TEXT("[조준점] 히트 마커 표시: %s (대상 종류 %u, 조준점 %s, 맞힘 %d·쓰러뜨림 %d)"),
		bKill ? TEXT("쓰러뜨림") : TEXT("맞힘"), TargetKind, bHasCursor ? *State.Center.ToString() : TEXT("화면 밖"), HitsSeen, KillsSeen);
	// 시험 옵션 -VHCrosshairShots: 처음 맞힘 4번·쓰러뜨림 3번은 그 프레임을 화면으로 저장 (Saved/V3Test/ch_hit_<봇>_<번호>_<hit|kill>.png)
	static const bool bShots = FParse::Param(FCommandLine::Get(), TEXT("VHCrosshairShots"));
	if (bShots && ((bKill && KillsSeen <= 3) || (!bKill && HitsSeen <= 4)))
	{
		FString Bot;
		FParse::Value(FCommandLine::Get(), TEXT("VHBot="), Bot);
		const FString File = FPaths::ProjectSavedDir() / TEXT("V3Test") / FString::Printf(TEXT("ch_hit_%s_%02d_%s.png"), *Bot, HitsSeen + KillsSeen, bKill ? TEXT("kill") : TEXT("hit"));
		FScreenshotRequest::RequestScreenshot(File, true, false);
		UE_LOG(LogVHCrosshair, Log, TEXT("[조준점] 히트 마커 화면 저장 %s"), *FPaths::GetCleanFilename(File));
	}
}

void UVHCrosshairWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	BindServer();

	const UVHUIStyle& S = UVHSettings::Get().GetStyle();
	const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
	APlayerController* PC = GetOwningPlayer();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const UVHServerGameSubsystem* Server = BoundServer.Get();
	const bool bServer = Server && Server->IsActive();

	// ── 무기 → 모양 ──
	bool bSkillTaser = bServer && Server->IsSkillTaserHeld();
	uint8 Weapon = bServer ? Server->GetCrosshairWeapon() : 0;
	if (VHCrosshairDev::WeaponOverride >= 0)
	{
		bSkillTaser = VHCrosshairDev::WeaponOverride == 100;
		Weapon = bSkillTaser ? 0 : static_cast<uint8>(FMath::Clamp(VHCrosshairDev::WeaponOverride, 0, 10));
	}
	const FVHCrosshairSpec Spec = Catalog.ResolveCrosshair(Weapon, bSkillTaser);
	if (Weapon != ShownWeapon || bSkillTaser != bShownSkillTaser)
	{
		UE_LOG(LogVHCrosshair, Log, TEXT("[조준점] 무기 %s → 모양 %s (사거리 %.0fcm, 반동 %.0fpx)"),
			bSkillTaser ? TEXT("사제 테이저건") : UVHServerGameSubsystem::WeaponName(static_cast<RobProto::WeaponId>(Weapon)),
			ShapeName(Spec.Shape), Spec.RangeCm, Spec.KickPx);
		ShownWeapon = Weapon;
		bShownSkillTaser = bSkillTaser;
	}

	// ── 조준점 위치 (위젯 좌표 = 1080p 기준 UI 단위) + 커서가 가리키는 바닥 지점 ──
	const FVector2D View = MyGeometry.GetLocalSize();
	const FVector Feet = FeetOf(Pawn);
	FVector2D Center = FVector2D::ZeroVector;
	FVector AimWorld = Feet;
	bool bCursor = false;
	if (VHCrosshairDev::PinMode != 0 && Pawn && PC)
	{
		FVector Pin = Feet + FVector(VHCrosshairDev::PinOffset.X, VHCrosshairDev::PinOffset.Y, 0.f);
		if (VHCrosshairDev::PinMode == 2 && Server)
		{
			// 가장 가까운 다른 플레이어(없으면 경비원)
			float Best = TNumericLimits<float>::Max();
			bool bBestThief = false;
			for (const TPair<uint16, UVHServerGameSubsystem::FEntity>& Pair : Server->GetEntities())
			{
				const UVHServerGameSubsystem::FEntity& E = Pair.Value;
				const bool bThief = E.Kind == RobProto::EntityKind::Thief;
				if (E.Id == Server->GetSelfEntityId() || !(bThief || E.Kind == RobProto::EntityKind::Guard) || (bBestThief && !bThief))
				{
					continue;
				}
				const float D = FVector::Dist2D(E.Pos, Feet);
				if ((bThief && !bBestThief) || D < Best)
				{
					Best = D;
					bBestThief = bThief;
					Pin = FVector(E.Pos.X, E.Pos.Y, Feet.Z);
				}
			}
		}
		AimWorld = Pin;
		bCursor = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, Pin, Center, false);
	}
	else if (FSlateApplication::IsInitialized())
	{
		Center = MyGeometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
		bCursor = Center.X >= 0.0 && Center.Y >= 0.0 && Center.X <= View.X && Center.Y <= View.Y;
		const UVHAimComponent* Aim = Pawn ? Pawn->FindComponentByClass<UVHAimComponent>() : nullptr;
		AimWorld = Aim ? Aim->GetAimPoint() : Feet;
	}
	bHasCursor = bCursor;

	// 화면에서 내 캐릭터 → 조준점 방향 (부채꼴 방향)
	FVector2D PawnScreen;
	FVector2D Dir(0.0, -1.0);
	if (PC && Pawn && UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, Feet, PawnScreen, false) && (Center - PawnScreen).SizeSquared() > 4.0)
	{
		Dir = (Center - PawnScreen).GetSafeNormal();
	}

	// ── 반동 벌어짐 복귀 (게임 시간: 느린 화면(slomo)에서도 같은 모양으로 확인할 수 있게) ──
	const float Dilation = GetWorld() ? UGameplayStatics::GetGlobalTimeDilation(GetWorld()) : 1.f;
	const float Dt = InDeltaTime * Dilation;
	KickPx *= FMath::Exp(-Dt * 3.f / Spec.KickRecoverSeconds);	// RecoverSeconds 에 약 95% 돌아옴
	if (KickPx < 0.05f)
	{
		KickPx = 0.f;
	}

	// ── 상태 ──
	FDrawState D;
	D.Center = Center;
	D.Spec = Spec;
	D.Dir = Dir;
	D.Kick = KickPx;
	const float Distance = Pawn ? FVector::Dist2D(Feet, AimWorld) : 0.f;
	LastRangeDistance = Distance;
	if (Spec.RangeCm > 0.f && Pawn)
	{
		const float Limit = Spec.RangeCm + (Spec.Shape == EVHCrosshairShape::Area ? 0.f : S.CrosshairRangeSlackCm);
		D.bOutOfRange = Distance > Limit;
		if (D.bOutOfRange && Spec.Shape == EVHCrosshairShape::Area && PC)
		{
			// 수류탄: 서버는 커서 쪽 최대 6m 에 떨어뜨린다 → 실제 떨어질 자리
			const FVector To = Feet + (AimWorld - Feet).GetSafeNormal2D() * Spec.RangeCm;
			D.bLanding = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, FVector(To.X, To.Y, Feet.Z), D.Landing, false);
		}
	}
	if (bServer)
	{
		D.bDisabled = Server->HasSelfState(RobProto::ES_DOWNED | RobProto::ES_STUNNED);
		if (Weapon == static_cast<uint8>(EVHWeaponId::Railgun) && !bSkillTaser)
		{
			bool bBeam = false;
			float Charge = 0.f;
			if (Server->GetMyRailgunCharge(Charge, bBeam))
			{
				D.bCharging = !bBeam;
				D.Charge = Charge;
			}
			D.bLocked = Server->HasSelfState(RobProto::ES_RAILGUN_LOCK) || bBeam;
		}
		const double Now = FPlatformTime::Seconds();
		// 상태 글자: 탄 적음 / 탄 없음 / 잠금 / 스킬 쿨타임(쿨타임 중 Q 를 눌렀을 때 2초)
		const int32 Ammo = Server->GetMyActiveAmmo();
		const float MaxAmmo = MaxAmmoOf(Weapon);
		// 레일건 충전·빔 중 마지막 탄이면 서버 무기는 이미 삼단봉 → 조준점 무기와 서버 무기가 다를 때는 탄 글자를 쓰지 않는다
		const FVHServerPlayerStatus& St = Server->GetMyStatus();
		const uint8 ServerWeapon = St.WeaponId[FMath::Clamp(St.ActiveSlot, 0, 1)];
		if (!bSkillTaser && MaxAmmo > 0.f && Weapon != 0 && Weapon == ServerWeapon)
		{
			if (Ammo <= 0)
			{
				D.Status = TEXT("탄 없음");
			}
			else if (MaxAmmo > 1.f && S.CrosshairLowAmmo > 0 && Ammo <= S.CrosshairLowAmmo)
			{
				D.Status = FString::Printf(TEXT("탄 %d"), Ammo);
			}
		}
		if (D.bLocked && !D.bCharging)
		{
			D.Status = D.Status.IsEmpty() ? FString(TEXT("잠금")) : D.Status + TEXT(" · 잠금");
		}
		const float SkillCool = Server->GetSkillCooldownRemaining();
		if (SkillCool > 0.f && Server->GetSkillCooldownNoticeTime() > 0.0 && Now - Server->GetSkillCooldownNoticeTime() < 2.0)
		{
			D.Status = FString::Printf(TEXT("Q %d초"), FMath::CeilToInt(SkillCool));
			D.Cooldown = FMath::Clamp(SkillCool / 30.f, 0.f, 1.f);	// 서버 SkillCooldownMs 30초
		}
		else
		{
			// 1초 무기: 쏜 뒤 다음 공격까지 (서버 공격 간격과 같은 표시용 값)
			const float Interval = UVHWeaponCatalog::ServerIntervalSeconds(Weapon);
			const double Since = Now - Server->GetMyLastShotTime();
			if (Weapon != 0 && Weapon != static_cast<uint8>(EVHWeaponId::Railgun) && Server->GetMyLastShotWeapon() == Weapon
				&& Interval >= S.CrosshairCooldownMinSeconds && Since < Interval)
			{
				D.Cooldown = FMath::Clamp(1.f - static_cast<float>(Since / Interval), 0.f, 1.f);
			}
		}
	}
	// 히트 마커 (실제 시간)
	if (MarkerStartReal > 0.0)
	{
		const float Life = bMarkerKill ? S.KillMarkerSeconds : S.HitMarkerSeconds;
		const float T = static_cast<float>(FPlatformTime::Seconds() - MarkerStartReal) / FMath::Max(Life, 0.05f);
		if (T < 1.f)
		{
			D.MarkerT = T;
			D.bMarkerKill = bMarkerKill;
		}
		else
		{
			MarkerStartReal = -1.0;
		}
	}
	State = D;
}

void UVHCrosshairWidget::PaintOne(const FDrawState& D, const FGeometry& Geo, FSlateWindowElementList& Out, int32 Layer) const
{
	const UVHUIStyle& S = UVHSettings::Get().GetStyle();
	const float T = FMath::Max(S.CrosshairThickness, 1.f);
	const float O = FMath::Max(S.CrosshairOutline, 0.f);
	const float Opacity = FMath::Clamp(S.CrosshairOpacity, 0.2f, 1.f);
	const float Diameter = FMath::Max(S.CrosshairMinSize, S.CrosshairSize * D.Spec.SizeScale);
	const float R = Diameter * 0.5f;
	const float K = D.Kick;
	const FVector2D C = D.Center;

	// 본체 색: 쓰러짐·경직 = 회색, 사거리 밖 = 빨강(흐리게 하지 않음), 레일건 잠금 = 주황
	FLinearColor Body = S.CrosshairColor;
	float Alpha = Opacity;
	if (D.bDisabled)
	{
		Body = S.CrosshairDisabledColor;
	}
	else if (D.bLocked)
	{
		Body = S.CrosshairLockedColor;
	}
	else if (D.bOutOfRange)
	{
		Body = S.CrosshairOutOfRangeColor;
		Alpha *= FMath::Clamp(S.CrosshairOutOfRangeOpacity, 0.4f, 1.f);
	}
	Body = WithAlpha(Body, Alpha);

	TArray<FStroke> Strokes;
	bool bCenterDot = true;
	switch (D.Spec.Shape)
	{
	case EVHCrosshairShape::Brackets:
	{
		// 굵은 호 4개 (대각선 쪽), 가운데 점
		const float Rb = R * 0.82f + K;
		for (int32 i = 0; i < 4; ++i)
		{
			const float Mid = 45.f + 90.f * i;
			Strokes.Add(Arc(C, Rb, Mid - 24.f, Mid + 24.f, Body, T));
		}
		break;
	}
	case EVHCrosshairShape::Cross:
		AddCross(Strokes, C, R * 0.3f + K, R + K, Body, T);
		break;
	case EVHCrosshairShape::RailRing:
	{
		const float Ring = R + K;
		AddCross(Strokes, C, R * 0.26f + K * 0.5f, R * 0.68f + K * 0.5f, Body, T);
		if (D.bCharging)
		{
			// 충전: 바탕 원(옅게) + 위에서부터 시계 방향으로 채워지는 링
			Strokes.Add(Circle(C, Ring, WithAlpha(Body, 0.45f), T));
			if (D.Charge > 0.01f)
			{
				Strokes.Add(Arc(C, Ring, -90.f, -90.f + 360.f * D.Charge, WithAlpha(S.CrosshairChargeColor, Alpha), T + 2.f));
			}
		}
		else
		{
			Strokes.Add(Circle(C, Ring, Body, T));
		}
		break;
	}
	case EVHCrosshairShape::Cone:
	{
		// 부채꼴 테두리: 꼭짓점은 조준점보다 내 캐릭터 쪽, 호는 앞쪽 (사거리·각도 느낌)
		const float Len = FMath::Clamp(D.Spec.RangeCm * S.CrosshairConePxPerCm, Diameter, Diameter * 1.6f) + K;
		const float Half = FMath::Clamp(D.Spec.HalfAngleDeg * S.CrosshairConeAngleScale, 10.f, 70.f);
		const FVector2D Apex = C - D.Dir * (Len * 0.5f);
		const float Base = FMath::RadiansToDegrees(FMath::Atan2(D.Dir.Y, D.Dir.X));
		FStroke Fan;
		Fan.Points.Add(FVector2f(Apex));
		AddArc(Fan.Points, Apex, Len, Base - Half, Base + Half);
		Fan.bClosed = true;
		Fan.Color = Body;
		Fan.Thickness = T;
		Strokes.Add(Fan);
		break;
	}
	case EVHCrosshairShape::Area:
	{
		const float Ra = FMath::Clamp(D.Spec.AreaRadiusCm * S.CrosshairAreaPxPerCm, R, FMath::Max(S.CrosshairAreaMaxSize * 0.5f, R)) + K;
		Strokes.Add(Circle(C, Ra, Body, T));
		AddCross(Strokes, C, FMath::Max(5.f, Ra * 0.14f), FMath::Max(R * 0.55f, Ra * 0.42f), Body, T);
		bCenterDot = false;
		if (D.bLanding)
		{
			// 실제로 떨어질 자리 (평소 색 작은 원 + 점)
			const FLinearColor Land = WithAlpha(S.CrosshairColor, Opacity);
			Strokes.Add(Circle(D.Landing, FMath::Max(R * 0.5f, 18.f), Land, T));
			Strokes.Add(Seg(D.Landing - FVector2D(4.0, 0.0), D.Landing + FVector2D(4.0, 0.0), Land, T + 1.f));
		}
		break;
	}
	case EVHCrosshairShape::Lightning:
	{
		// 원 + 번개 (사제 테이저건을 든 상태)
		Strokes.Add(Circle(C, R + K, Body, T));
		const float B = R * 0.78f;
		FStroke Bolt;
		const FVector2D P[] = { FVector2D(0.22, -0.78), FVector2D(-0.26, 0.06), FVector2D(0.1, 0.06), FVector2D(-0.2, 0.8) };
		for (const FVector2D& Q : P)
		{
			Bolt.Points.Add(FVector2f(C + Q * B));
		}
		Bolt.Color = WithAlpha(D.bDisabled ? S.CrosshairDisabledColor : (D.bOutOfRange ? S.CrosshairOutOfRangeColor : S.CrosshairTaserColor), Alpha);
		Bolt.Thickness = T + 1.f;
		Strokes.Add(Bolt);
		bCenterDot = false;
		break;
	}
	default:
		AddCross(Strokes, C, R * 0.3f + K, R + K, Body, T);
		break;
	}

	// 쿨타임 원 (조준점 오른쪽 위, 남은 만큼 호)
	const FVector2D Side = C + FVector2D(R + 14.0, -(R * 0.55));
	if (D.Cooldown > 0.f)
	{
		Strokes.Add(Circle(Side, 10.f, WithAlpha(FLinearColor(0.25f, 0.25f, 0.28f, 1.f), Opacity), 3.f));
		Strokes.Add(Arc(Side, 10.f, -90.f, -90.f + 360.f * D.Cooldown, WithAlpha(S.CrosshairStatusColor, Opacity), 3.f));
	}

	// 히트 마커: 굵은 X (처음에 조금 크게 튀었다가 제자리, 끝 40% 동안 사라짐)
	if (D.MarkerT >= 0.f)
	{
		const float Pop = FMath::Square(1.f - D.MarkerT) * 7.f;
		const float Scale = D.bMarkerKill ? 1.25f : 1.f;
		const float Fade = D.MarkerT < 0.6f ? 1.f : 1.f - (D.MarkerT - 0.6f) / 0.4f;
		const FLinearColor M = WithAlpha(D.bMarkerKill ? S.KillMarkerColor : S.HitMarkerColor, Fade);
		const float In = S.HitMarkerGap * Scale + Pop * 0.5f;
		const float OutR = S.HitMarkerSize * Scale + Pop;
		const float Mt = S.HitMarkerThickness * (D.bMarkerKill ? 1.2f : 1.f);
		const FVector2D Diag[4] = { FVector2D(1, 1), FVector2D(-1, 1), FVector2D(1, -1), FVector2D(-1, -1) };
		for (const FVector2D& Dg : Diag)
		{
			const FVector2D U = Dg.GetSafeNormal();
			Strokes.Add(Seg(C + U * In, C + U * OutR, M, Mt));
		}
	}

	// ── 그리기: 테두리(어둡게, 양쪽 O 만큼 두껍게, 끝도 O 만큼 늘림) → 본체 ──
	const FPaintGeometry PG = Geo.ToPaintGeometry();
	if (O > 0.f)
	{
		for (const FStroke& St : Strokes)
		{
			TArray<FVector2f> P = St.Points;
			if (St.bClosed)
			{
				const FVector2f First = P[0];
				P.Add(First);
			}
			else if (P.Num() >= 2)
			{
				P[0] += (P[0] - P[1]).GetSafeNormal() * O;
				P.Last() += (P.Last() - P[P.Num() - 2]).GetSafeNormal() * O;
			}
			FLinearColor OC = S.CrosshairOutlineColor;
			OC.A *= St.Color.A;
			FSlateDrawElement::MakeLines(Out, Layer, PG, P, ESlateDrawEffect::None, OC, true, St.Thickness + 2.f * O);
		}
	}
	for (const FStroke& St : Strokes)
	{
		TArray<FVector2f> P = St.Points;
		if (St.bClosed)
		{
			const FVector2f First = P[0];
			P.Add(First);
		}
		FSlateDrawElement::MakeLines(Out, Layer + 1, PG, P, ESlateDrawEffect::None, St.Color, true, St.Thickness);
	}
	// 가운데 점 (정확한 조준 지점)
	if (bCenterDot)
	{
		const FSlateBrush* White = FCoreStyle::Get().GetBrush("GenericWhiteBox");
		const float Dot = T;
		const float DotO = T + 2.f * O;
		FLinearColor OC = S.CrosshairOutlineColor;
		OC.A *= Body.A;
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(FVector2f(DotO, DotO), FSlateLayoutTransform(FVector2f(C - FVector2D(DotO * 0.5f)))), White, ESlateDrawEffect::None, OC);
		FSlateDrawElement::MakeBox(Out, Layer + 1, Geo.ToPaintGeometry(FVector2f(Dot, Dot), FSlateLayoutTransform(FVector2f(C - FVector2D(Dot * 0.5f)))), White, ESlateDrawEffect::None, Body);
	}

	// 상태 글자 (조준점 오른쪽 아래, 어두운 외곽선 글꼴)
	if (!D.Status.IsEmpty() || !D.Label.IsEmpty())
	{
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", S.CrosshairStatusFontSize);
		Font.OutlineSettings.OutlineSize = 2;
		Font.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.95f);
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		if (!D.Status.IsEmpty())
		{
			const FVector2D Size = Measure->Measure(D.Status, Font);
			const FVector2D At = C + FVector2D(R + 6.0, R * 0.35);
			FSlateDrawElement::MakeText(Out, Layer + 2, Geo.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(At))), D.Status, Font,
				ESlateDrawEffect::None, WithAlpha(S.CrosshairStatusColor, Opacity));
		}
		if (!D.Label.IsEmpty())
		{
			FSlateFontInfo LabelFont = Font;
			LabelFont.Size = 14;
			const FVector2D Size = Measure->Measure(D.Label, LabelFont);
			const FVector2D At = C + FVector2D(-Size.X * 0.5, 62.0);
			FSlateDrawElement::MakeText(Out, Layer + 2, Geo.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(At))), D.Label, LabelFont,
				ESlateDrawEffect::None, FLinearColor::White);
		}
	}
}

void UVHCrosshairWidget::BuildGallery(const FVector2D& View, TArray<FDrawState>& Out) const
{
	// 비교 촬영용: 1줄 = 무기별 모양(맨 왼쪽 칸은 비워 둠 → 촬영 뒤 운영체제 십자 커서를 같은 크기로 붙인다), 2줄 = 상태
	const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
	const double Step = 150.0;
	auto Add = [&](int32 Row, int32 Col, int32 Cols, uint8 Weapon, bool bSkill, const TCHAR* Label) -> FDrawState&
	{
		FDrawState D;
		D.Center = FVector2D(View.X * 0.5 + (Col - (Cols - 1) * 0.5) * Step, View.Y * (Row == 0 ? 0.40 : 0.66));
		D.Spec = Catalog.ResolveCrosshair(Weapon, bSkill);
		D.Dir = FVector2D(0.6, -0.8).GetSafeNormal();
		D.Label = Label;
		return Out.Add_GetRef(D);
	};
	Add(0, 0, 8, 0, false, TEXT("지금 커서(OS)")).Spec.Shape = EVHCrosshairShape::Auto;	// 그리지 않음 (표시용 빈칸)
	Out.Last().Kick = -1.f;
	Add(0, 1, 8, 1, false, TEXT("근접·맨손"));
	Add(0, 2, 8, 4, false, TEXT("권총·테이저건"));
	Add(0, 3, 8, 6, false, TEXT("레일건"));
	Add(0, 4, 8, 5, false, TEXT("화염방사기"));
	Add(0, 5, 8, 8, false, TEXT("수류탄"));
	Add(0, 6, 8, 9, false, TEXT("지뢰"));
	Add(0, 7, 8, 0, true, TEXT("사제 테이저건"));

	Add(1, 0, 9, 4, false, TEXT("사거리 밖(총)")).bOutOfRange = true;
	Add(1, 1, 9, 2, false, TEXT("사거리 밖(근접)")).bOutOfRange = true;
	{ FDrawState& D = Add(1, 2, 9, 6, false, TEXT("레일건 충전 60%")); D.bCharging = true; D.Charge = 0.6f; }
	{ FDrawState& D = Add(1, 3, 9, 6, false, TEXT("레일건 잠금")); D.bLocked = true; D.Status = TEXT("잠금"); }
	{ FDrawState& D = Add(1, 4, 9, 8, false, TEXT("수류탄 6m 밖")); D.bOutOfRange = true; D.bLanding = true; D.Landing = D.Center + FVector2D(-34.0, 40.0); }
	{ FDrawState& D = Add(1, 5, 9, 4, false, TEXT("맞힘 X")); D.MarkerT = 0.15f; }
	{ FDrawState& D = Add(1, 6, 9, 4, false, TEXT("쓰러뜨림 X")); D.MarkerT = 0.15f; D.bMarkerKill = true; }
	Add(1, 7, 9, 1, false, TEXT("쓰러짐·경직")).bDisabled = true;
	{ FDrawState& D = Add(1, 8, 9, 4, false, TEXT("탄 적음·쿨타임")); D.Status = TEXT("탄 2"); D.Cooldown = 0.6f; }
}

int32 UVHCrosshairWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (VHCrosshairDev::Gallery != 0)
	{
		TArray<FDrawState> List;
		BuildGallery(AllottedGeometry.GetLocalSize(), List);
		for (const FDrawState& D : List)
		{
			if (D.Kick < 0.f)
			{
				// 빈칸(운영체제 커서 자리): 이름만
				FDrawState Label = D;
				Label.Kick = 0.f;
				Label.Spec.Shape = EVHCrosshairShape::Auto;
				FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 14);
				Font.OutlineSettings.OutlineSize = 2;
				const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
				const FVector2D Size = Measure->Measure(D.Label, Font);
				FSlateDrawElement::MakeText(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(D.Center + FVector2D(-Size.X * 0.5, 62.0)))),
					D.Label, Font, ESlateDrawEffect::None, FLinearColor::White);
				continue;
			}
			PaintOne(D, AllottedGeometry, OutDrawElements, LayerId + 1);
		}
	}
	if (bHasCursor)
	{
		PaintOne(State, AllottedGeometry, OutDrawElements, LayerId + 4);
	}
	return LayerId + 8;
}

bool UVHCrosshairWidget::GetScreenBounds(FVector2D& OutCenter, float& OutRadius) const
{
	if (!bHasCursor || GetVisibility() == ESlateVisibility::Collapsed || !IsInViewport())
	{
		return false;
	}
	const UVHUIStyle& S = UVHSettings::Get().GetStyle();
	const float Diameter = FMath::Max(S.CrosshairMinSize, S.CrosshairSize * State.Spec.SizeScale);
	float R = Diameter * 0.5f + State.Kick;
	if (State.Spec.Shape == EVHCrosshairShape::Area)
	{
		R = FMath::Max(R, FMath::Clamp(State.Spec.AreaRadiusCm * S.CrosshairAreaPxPerCm, Diameter * 0.5f, FMath::Max(S.CrosshairAreaMaxSize * 0.5f, Diameter * 0.5f)) + State.Kick);
	}
	else if (State.Spec.Shape == EVHCrosshairShape::Cone)
	{
		R = FMath::Max(R, FMath::Clamp(State.Spec.RangeCm * S.CrosshairConePxPerCm, Diameter, Diameter * 1.6f) * 0.5f + State.Kick);
	}
	OutCenter = State.Center;
	OutRadius = FMath::Max(R, S.HitMarkerSize * 1.25f) + S.CrosshairOutline + S.CrosshairThickness;
	return true;
}

FString UVHCrosshairWidget::Describe() const
{
	FString OsCursor = TEXT("?");
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetPlatformCursor().IsValid())
	{
		const EMouseCursor::Type Type = FSlateApplication::Get().GetPlatformCursor()->GetType();
		OsCursor = Type == EMouseCursor::None ? TEXT("숨김(None)") : FString::Printf(TEXT("보임(%d)"), static_cast<int32>(Type));
	}
	const APlayerController* PC = GetOwningPlayer();
	return FString::Printf(TEXT("보임 %s, 커서 위치 %s %s, 무기 %u%s → %s, 사거리 %.0fcm / 거리 %.0fcm → %s, 반동 %.1fpx, 운영체제 커서 %s, PC 커서 %d, 맞힘 %d·쓰러뜨림 %d·공격 %d"),
		GetVisibility() == ESlateVisibility::Collapsed ? TEXT("아니오") : TEXT("예"), *State.Center.ToString(), bHasCursor ? TEXT("(화면 안)") : TEXT("(화면 밖)"),
		ShownWeapon, bShownSkillTaser ? TEXT("(사제 테이저건)") : TEXT(""), ShapeName(State.Spec.Shape), State.Spec.RangeCm, LastRangeDistance,
		State.bOutOfRange ? TEXT("사거리 밖") : TEXT("사거리 안"), KickPx, *OsCursor, PC ? static_cast<int32>(PC->CurrentMouseCursor.GetValue()) : -1,
		HitsSeen, KillsSeen, ShotsSeen);
}

#if !UE_BUILD_SHIPPING
namespace
{
	// vh.Crosshair … : 조준점 개발 명령 (비교 촬영·봇 시험용. 판정과 무관)
	FAutoConsoleCommandWithWorldAndArgs GVHCrosshairCommand(TEXT("vh.Crosshair"),
		TEXT("개발용 조준점: vh.Crosshair 0|1 (끄기·켜기) · at dx dy (내 캐릭터 + cm 에 고정) · follow (가장 가까운 다른 플레이어에 고정) · free (마우스) · ")
		TEXT("weapon N|skill|auto (모양만 보기) · gallery 0|1 (모든 모양·상태 나란히) · info"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			using namespace VHCrosshairDev;
			const FString A0 = Args.Num() > 0 ? Args[0].ToLower() : FString(TEXT("info"));
			if (A0 == TEXT("0") || A0 == TEXT("1"))
			{
				Enabled = FCString::Atoi(*A0);
			}
			else if (A0 == TEXT("default"))
			{
				Enabled = -1;
			}
			else if (A0 == TEXT("at") && Args.Num() >= 3)
			{
				PinMode = 1;
				PinOffset = FVector2D(FCString::Atof(*Args[1]), FCString::Atof(*Args[2]));
			}
			else if (A0 == TEXT("follow"))
			{
				PinMode = 2;
			}
			else if (A0 == TEXT("free"))
			{
				PinMode = 0;
			}
			else if (A0 == TEXT("weapon") && Args.Num() >= 2)
			{
				const FString W = Args[1].ToLower();
				WeaponOverride = W == TEXT("auto") ? -1 : (W == TEXT("skill") ? 100 : FMath::Clamp(FCString::Atoi(*W), 0, 10));
			}
			else if (A0 == TEXT("gallery"))
			{
				Gallery = Args.Num() >= 2 ? FCString::Atoi(*Args[1]) : 1;
			}
			else if (A0 == TEXT("pausetest") && World)
			{
				// 일시정지 확인: 게임을 멈추고 N초(실제 시간) 뒤 푼다 → 멈춘 동안 운영체제 커서가 보통 화살표로 돌아오는지
				const float Secs = Args.Num() >= 2 ? FMath::Clamp(FCString::Atof(*Args[1]), 0.5f, 30.f) : 3.f;
				UGameplayStatics::SetGamePaused(World, true);
				TWeakObjectPtr<UWorld> WeakWorld(World);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld](float)
				{
					if (WeakWorld.IsValid())
					{
						UGameplayStatics::SetGamePaused(WeakWorld.Get(), false);
						UE_LOG(LogVHCrosshair, Log, TEXT("[조준점] 일시정지 풂 (pausetest)"));
					}
					return false;
				}), Secs);
				UE_LOG(LogVHCrosshair, Log, TEXT("[조준점] 일시정지 %.1f초 (pausetest)"), Secs);
			}
			else if (A0 == TEXT("fx") && Args.Num() >= 2 && World)
			{
				// 비교 촬영 배경용 연출 흉내 (내 화면만, 서버는 모름): flame, grenade, blastmine, railbeam …
				if (UVHServerGameSubsystem* Server = World->GetSubsystem<UVHServerGameSubsystem>())
				{
					Server->DebugFake(Args[1], Args.Num() >= 3 ? FCString::Atof(*Args[2]) : 3.f);
				}
			}
			const UVHCrosshairWidget* W = Active.Get();
			UE_LOG(LogVHCrosshair, Log, TEXT("[조준점] vh.Crosshair %s → 켜짐 %d, 고정 %d (%s), 무기 보기 %d, 갤러리 %d | %s"), *FString::Join(Args, TEXT(" ")),
				UVHCrosshairWidget::IsEnabledByConfig() ? 1 : 0, PinMode, *PinOffset.ToString(), WeaponOverride, Gallery, W ? *W->Describe() : TEXT("조준점 위젯 없음"));
		}));
}
#endif
