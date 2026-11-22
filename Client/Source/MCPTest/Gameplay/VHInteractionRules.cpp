#include "Gameplay/VHInteractionRules.h"

#define LOCTEXT_NAMESPACE "VHInteract"

// ───────────────────────── 진행도 ─────────────────────────

float FVHHoldProgress::Begin(double Now, float InTotalSeconds)
{
	TotalSeconds = FMath::Max(InTotalSeconds, KINDA_SMALL_NUMBER);
	SavedProgress = FMath::Clamp(SavedProgress, 0.f, 1.f);
	StartTime = Now;
	bRunning = true;
	return (1.f - SavedProgress) * TotalSeconds;
}

void FVHHoldProgress::BeginWithRemaining(double InStartTime, float RemainingSeconds, float InTotalSeconds)
{
	TotalSeconds = FMath::Max(InTotalSeconds, KINDA_SMALL_NUMBER);
	SavedProgress = FMath::Clamp(1.f - RemainingSeconds / TotalSeconds, 0.f, 1.f);
	StartTime = InStartTime;
	bRunning = true;
}

float FVHHoldProgress::GetProgress(double Now) const
{
	if (!bRunning)
	{
		return SavedProgress;
	}
	const float Elapsed = static_cast<float>(FMath::Max(0.0, Now - StartTime));
	return FMath::Clamp(SavedProgress + Elapsed / TotalSeconds, 0.f, 1.f);
}

float FVHHoldProgress::GetRemainingSeconds(double Now) const
{
	return (1.f - GetProgress(Now)) * TotalSeconds;
}

bool FVHHoldProgress::IsComplete(double Now) const
{
	return GetProgress(Now) >= 1.f - KINDA_SMALL_NUMBER;
}

void FVHHoldProgress::Cancel(double Now, bool bKeep)
{
	SavedProgress = bKeep ? GetProgress(Now) : 0.f;
	bRunning = false;
}

void FVHHoldProgress::Finish()
{
	SavedProgress = 1.f;
	bRunning = false;
}

void FVHHoldProgress::Reset()
{
	SavedProgress = 0.f;
	bRunning = false;
	StartTime = 0.0;
}

// ───────────────────────── 소리·대상 점수 ─────────────────────────

float VHRules::TargetScore(float Distance, float AngleDeg, float Radius, float MaxAngleDeg)
{
	if (Radius <= 0.f || Distance > Radius)
	{
		return -1.f;
	}
	if (Distance > KINDA_SMALL_NUMBER && AngleDeg > MaxAngleDeg)
	{
		return -1.f;
	}
	// 거리와 각도를 각각 0~1로 맞춰 더한다. 둘 다 작을수록 우선.
	const float AnglePart = Distance > KINDA_SMALL_NUMBER && MaxAngleDeg > 0.f ? AngleDeg / MaxAngleDeg : 0.f;
	return Distance / Radius + AnglePart;
}

FVector VHRules::AimRelativeMove(float Forward, float Right, float AimYaw, float ForwardScale, float BackwardScale, float StrafeScale)
{
	const FVector2D In(Forward, Right);
	const float Len = In.Size();
	if (Len < KINDA_SMALL_NUMBER)
	{
		return FVector::ZeroVector;
	}
	const FVector2D D = In / Len;	// 방향만
	const float Strength = FMath::Min(Len, 1.f);	// 대각선(W+A)도 세기는 1까지
	// 앞·뒤 성분과 옆 성분의 비율로 배율을 섞는다 (정면 = 앞 배율, 정옆 = 옆 배율)
	const float Scale = D.X * D.X * (D.X >= 0.f ? ForwardScale : BackwardScale) + D.Y * D.Y * StrafeScale;
	// 바라보는 방향의 앞(X)·오른쪽(Y) 축 (언리얼 Yaw는 +X에서 +Y 쪽으로 돈다)
	const FRotator YawRot(0.f, AimYaw, 0.f);
	const FVector Fwd = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
	const FVector Rgt = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);
	return (Fwd * D.X + Rgt * D.Y) * (Strength * Scale);
}

float VHRules::AimYawToPoint(const FVector& From, const FVector& AimPoint, float PreviousYaw, float DeadZone)
{
	const FVector Delta(AimPoint.X - From.X, AimPoint.Y - From.Y, 0.f);
	if (Delta.Size2D() <= FMath::Max(DeadZone, 1.f))
	{
		return PreviousYaw;	// 커서가 캐릭터 바로 위면 방향이 흔들리므로 그대로
	}
	return FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
}

// ───────────────────────── 문구 ─────────────────────────

EVHRejectReason VHInteract::ToRejectReason(EVHInteractBlock Block)
{
	switch (Block)
	{
	case EVHInteractBlock::Busy:			return EVHRejectReason::TargetBusy;
	case EVHInteractBlock::OutOfRange:		return EVHRejectReason::OutOfRange;
	default:								return EVHRejectReason::InvalidState;
	}
}

FText VHInteract::RejectReasonText(EVHRejectReason Reason)
{
	switch (Reason)
	{
	case EVHRejectReason::Cooldown:		return LOCTEXT("RejCooldown", "잠시 후 다시 시도하세요");
	case EVHRejectReason::InvalidState:	return LOCTEXT("RejInvalid", "지금은 할 수 없습니다");
	case EVHRejectReason::OutOfRange:	return LOCTEXT("RejRange", "너무 멉니다");
	case EVHRejectReason::NoFlashlight:	return LOCTEXT("RejNoLight", "손전등이 없습니다");
	case EVHRejectReason::TargetBusy:	return LOCTEXT("RejBusy", "다른 의적이 사용 중입니다");
	case EVHRejectReason::TreasureFull:	return LOCTEXT("RejFull", "더 들 수 없습니다");
	}
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
