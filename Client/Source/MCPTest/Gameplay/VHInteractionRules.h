#pragma once

#include "CoreMinimal.h"
#include "Gameplay/VHInteractionTypes.h"
#include "VHInteractionRules.generated.h"

/**
 * 길게 누르기 진행도 (순수 로직, 자동화 테스트 대상).
 *
 * 진행도는 0~1 비율로 저장한다. 시간 값(30초 등)이 바뀌어도 "몇 % 열렸는지"는 유지된다.
 *   현재 진행도 = 저장된 진행도 + (지금 - 시작 시각) / 전체 시간
 * 서버 방식(SC_INTERACT의 start_server_time_ms + duration_ms)과 같은 정보로 다시 만들 수 있다.
 */
USTRUCT(BlueprintType)
struct MCPTEST_API FVHHoldProgress
{
	GENERATED_BODY()

	/** 진행 중이 아닐 때의 진행도(0~1). 취소 후 남은 진행도 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	float SavedProgress = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	bool bRunning = false;

	double StartTime = 0.0;
	float TotalSeconds = 1.f;

	/** 시작. 끝날 때까지 남은 시간(초)을 돌려준다 (= SC_INTERACT.duration_ms에 해당) */
	float Begin(double Now, float InTotalSeconds);

	/** 서버가 알려준 시작 시각·남은 시간으로 시작 (서버 판정용) */
	void BeginWithRemaining(double InStartTime, float RemainingSeconds, float InTotalSeconds);

	float GetProgress(double Now) const;
	float GetRemainingSeconds(double Now) const;
	bool IsComplete(double Now) const;

	/** 취소. bKeep=false면 진행도 0으로 */
	void Cancel(double Now, bool bKeep);

	/** 완료 처리(진행도 1) */
	void Finish();

	void Reset();
};

/** 대상 고르기·조준 계산 (순수 함수, 자동화 테스트 대상) */
namespace VHRules
{
	/**
	 * 상호작용 대상 점수(작을수록 우선). 반경 밖이거나 각도 밖이면 음수(제외).
	 * Distance: 캐릭터 ~ 대상 가장자리 거리, AngleDeg: 바라보는 방향과 대상 방향의 각도
	 * 대상 안쪽(거리 0)에 서 있으면 각도와 상관없이 후보가 된다(바닥의 보물 등).
	 */
	MCPTEST_API float TargetScore(float Distance, float AngleDeg, float Radius, float MaxAngleDeg);

	/**
	 * 커서 기준 이동 ⟪기획 변경 2026-10-26⟫: 이동 입력(Forward = W/S, Right = D/A, 각 -1~1)을
	 * 바라보는 방향(AimYaw) 기준 월드 이동 벡터로 바꾼다. 길이 = 입력 세기(최대 1) × 방향별 속도 배율.
	 * 대각선은 방향만 합치고 세기는 1을 넘지 않는다. 배율은 앞·뒤·옆 사이를 방향 비율로 섞는다.
	 */
	MCPTEST_API FVector AimRelativeMove(float Forward, float Right, float AimYaw,
		float ForwardScale = 1.f, float BackwardScale = 1.f, float StrafeScale = 1.f);

	/** 커서가 가리키는 점으로 바라볼 방향(Yaw). 캐릭터에서 DeadZone 안이면 PreviousYaw 유지 */
	MCPTEST_API float AimYawToPoint(const FVector& From, const FVector& AimPoint, float PreviousYaw, float DeadZone);
}
