#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VHInteractionConfig.generated.h"

class UMaterialInterface;
class AVHDroppedTreasure;

/**
 * 상호작용 안내·표시·손전등 수치 모음 (DA_VHInteractionConfig). 실제 작업 시간·거리 판정은 서버 값을 따른다.
 * 기획이 확정되면 이 에셋의 숫자만 바꾸면 된다. 길이는 언리얼 단위(cm), 시간은 초.
 * 설계 문서 Docs/상호작용_설계_초안.md 의 [기획]/[제안] 값을 기본값으로 넣었다.
 */
UCLASS(BlueprintType)
class MCPTEST_API UVHInteractionConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UVHInteractionConfig();

	// ── 대상 찾기 [제안] ──

	/** 캐릭터 중심에서 대상 가장자리까지 이 거리 안이면 후보 (150 = 1.5m). 아래 "한 번 누르기" 방식을 끄면 쓰인다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeting", meta = (Units = "cm"))
	float SearchRadius = 150.f;

	// ── 상호작용 방식 [사용자 결정 2026-11-13, D2: F 한 번] ──

	/**
	 * 켜면 상호작용 키(F)를 한 번 누르는 방식: 아이템은 바로 줍고, 금고·상자는 다가가서 작업한다.
	 * 이동·공격·구르기·F 다시 누르기로 취소. 끄면 예전 방식(누르고 있는 동안만 작업, SearchRadius 안에서만).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	bool bPressToInteract = true;

	/** 줍기 거리(아이템 가장자리까지). 서버는 아이템 중심까지 300cm로 재므로 조금 안쪽으로 잡는다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = 0, Units = "cm"))
	float PickupRange = 270.f;

	/** 금고·상자: F를 누르면 작업 지점에서 이 거리 안까지 다가간 뒤 작업을 요청한다 (서버 작업 거리 130cm보다 안쪽) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = 0, Units = "cm"))
	float WorkApproachDistance = 70.f;

	/** 다가가기를 포기하는 시간(길이 막혔을 때) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = 0, Units = "s"))
	float WorkApproachTimeout = 3.f;

	/** 상자: 가운데에서 이 거리 안까지 다가간 뒤 작업을 요청한다 (상자 몸통이 길을 막으므로 금고보다 멀리. 서버 작업 거리 = 가운데에서 180cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = 0, Units = "cm"))
	float BoxApproachDistance = 130.f;

	/** 상자 여는 시간 (안내 문구용. 실제 시간은 서버 GameData::BoxOpenMs = 3초, 멈추면 처음부터) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = 0, Units = "s"))
	float BoxOpenSeconds = 3.f;

	/**
	 * 반납 방식. 2026-11-23 팀 서버부터 반납은 F로 시작하는 4초 시간 작업(SC_INTERACT, 한 구역에 한 명)이라 기본은 끔(F 방식).
	 * 켜면 환금 아이템을 들고 반납지점 범위(ReturnPointInteractRadius)에 들어가는 순간 자동으로 반납 작업을 요청한다
	 * (예전 사용자 임시 결정 2026-11-14 — 서버가 즉시 반납이던 때).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	bool bAutoReturn = false;

	/** 자동 반납 요청을 다시 보내기까지의 간격 (응답을 기다리는 동안 매 프레임 보내지 않도록) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = 0.1, Units = "s"))
	float AutoReturnRetrySeconds = 0.5f;

	/** 커서 올리기 판정: 화면에서 물품 둘레를 이만큼(픽셀) 넓혀 잡는다 (작은 아이템도 쉽게 가리키도록) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = 0))
	float HoverPaddingPixels = 14.f;

	/**
	 * 반납지점만 따로: 1m×1m 사각형 "가운데"에서 이 거리(원) 안이면 반납 가능.
	 * 서버(GameData::ReturnPointRangeCm = 100)와 같은 값 [사용자 결정 2026-10-27: 클라를 서버에 맞춤]
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeting", meta = (Units = "cm"))
	float ReturnPointInteractRadius = 100.f;

	/** 바라보는 방향에서 좌우로 이 각도 안이면 후보 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeting", meta = (Units = "deg"))
	float MaxFacingAngle = 75.f;

	// ── 금고 [기획] ──

	/** 금고 여는 시간 안내("금고 열기 (10초)")에 쓰는 표시용 값. 실제 시간은 서버 값(SC_INTERACT duration, 서버 v3.0 = 10초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Safe", meta = (Units = "s"))
	float SafeOpenSeconds = 10.f;

	/** 취소해도 진행도 유지 [기획] */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Safe")
	bool bSafeKeepProgress = true;

	// ── 보물 ──

	/** 보물 줍기(짧게 누르기) 동작 시간 [제안] */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Treasure", meta = (Units = "s"))
	float TreasurePickupSeconds = 0.5f;

	// (환금 아이템의 종류·가치는 서버가 정한다)

	/** 떨어진 보물을 새로 만들 때 쓰는 클래스(외형을 바꾼 블루프린트). 비우면 C++ 기본 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Treasure")
	TSoftClassPtr<AVHDroppedTreasure> DroppedTreasureClass;

	// (이동 속도·스테미나는 2026-10-25부터 DA_VHGameplayConfig 의 ThiefStats 로 옮김)

	// ── 손전등 ──

	/**
	 * 손전등 기능 사용 [사용자 결정 2026-11-13, D3: 끔]. 서버 v3.0에 손전등이 없다.
	 * 끄면 손전등을 켤 수 없고, "빛 안에서만 상호작용" 대신 아래 OutlineRange 안의 물품이 테두리·상호작용 후보가 된다.
	 * (코드 삭제는 기획 확인 뒤. 켜면 예전 방식 그대로 동작)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flashlight")
	bool bUseFlashlight = false;

	/** 손전등을 끈 경우: 이 거리 안의 상호작용 물품에 테두리를 그린다 [기획 2026-11: 3m 안이면 노란 테두리] */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = 0, Units = "cm"))
	float OutlineRange = 300.f;

	/** 켜고 끄는 최소 간격 [제안] */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flashlight", meta = (Units = "s"))
	float FlashlightToggleCooldown = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flashlight", meta = (Units = "cm"))
	float FlashlightRange = 1800.f;

	/** 원뿔 바깥 각도(반각) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flashlight", meta = (Units = "deg"))
	float FlashlightOuterCone = 28.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flashlight", meta = (Units = "deg"))
	float FlashlightInnerCone = 14.f;

	/** 밝기 (언리얼 라이트 단위: 칸델라) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flashlight")
	float FlashlightIntensity = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flashlight")
	FLinearColor FlashlightColor = FLinearColor(1.f, 0.93f, 0.78f);

	/** 게임 시작 시 켜져 있는가 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flashlight")
	bool bFlashlightOnAtStart = false;

	/**
	 * 의적 몸(내 캐릭터·더미·다른 의적 표시)이 그림자를 만드는가 [교수님 피드백 2026-10-30: 끔].
	 * 끄면 손전등 빛에 내 몸 그림자가 생기지 않는다. 벽·가구 그림자는 그대로라 빛이 벽을 뚫지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flashlight")
	bool bThiefCastsShadow = false;

	/**
	 * 다른 의적(실서버의 표시용 액터)의 손전등 원뿔을 보여 준다 [사용자 요청 2026-10-30].
	 * 밝기·거리·각도는 위 Flashlight 값과 같다. 보이기만 하고 내 판정에는 쓰지 않는다. 느리면 끈다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flashlight")
	bool bShowOtherThiefBeam = true;

	// ── 손전등 주변 빛: 손전등을 켜면 의적 둘레를 약하게 밝혀 캐릭터가 보이게 한다 [교수님 피드백 2026-10-30] ──
	// 보이게만 하는 빛이라 상호작용·테두리 판정(손전등 원뿔)과는 관계없다.

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flashlight|Aura")
	bool bFlashlightAura = true;

	/** 바닥에서 빛이 닿는 반경 (의적 발 기준) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flashlight|Aura", meta = (ClampMin = 10, Units = "cm"))
	float FlashlightAuraRadius = 100.f;

	/** 빛을 놓는 높이 (바닥 기준, 머리 위쯤이어야 캐릭터 윗면이 밝다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flashlight|Aura", meta = (ClampMin = 0, Units = "cm"))
	float FlashlightAuraHeight = 220.f;

	/**
	 * 밝기. 손전등(FlashlightIntensity)보다 훨씬 약하게.
	 * 부드러운 감쇠(FalloffExponent > 0)면 단위 없는 값, 0이면 칸델라(실제 빛처럼 거리 제곱으로 어두워짐 → 머리만 하얗게 밝아지기 쉬움)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flashlight|Aura", meta = (ClampMin = 0))
	float FlashlightAuraIntensity = 3.f;

	/** 거리에 따라 어두워지는 정도(클수록 가장자리가 빨리 어두워짐). 0 = 실제 빛처럼(칸델라) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flashlight|Aura", meta = (ClampMin = 0, ClampMax = 16))
	float FlashlightAuraFalloffExponent = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flashlight|Aura")
	FLinearColor FlashlightAuraColor = FLinearColor(1.f, 0.9f, 0.75f);

	/** 벽에 막히게 그림자를 계산 (끄면 벽 옆에서 옆방 바닥이 조금 밝아질 수 있다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flashlight|Aura")
	bool bFlashlightAuraShadows = true;

	// ── 표시 ──

	/**
	 * (2026-10-25부터 사용 안 함) 예전 "면 전체 발광" 덧씌우기 머티리얼.
	 * 테두리는 이제 포스트 프로세스 외곽선(DA_VHGameplayConfig 의 Outline 항목, M_VHOutline)으로 그린다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<UMaterialInterface> HighlightMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FLinearColor HighlightAvailableColor = FLinearColor(1.f, 0.75f, 0.15f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FLinearColor HighlightBlockedColor = FLinearColor(0.45f, 0.45f, 0.45f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FLinearColor TreasureGlowColor = FLinearColor(1.f, 0.7f, 0.1f);

	// ── 문구 (동작 이름) ──

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text") FText SafeOpenText;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text") FText TreasurePickupText;
};
