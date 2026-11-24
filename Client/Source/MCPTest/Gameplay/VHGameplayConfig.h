#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Gameplay/VHGameplayTypes.h"
#include "VHGameplayConfig.generated.h"

class UMaterialInterface;
class UMaterialParameterCollection;
class AVHReturnPoint;
class AVHSafe;

/** 문 열림 상태가 오는 곳 [2026-11-24] */
UENUM(BlueprintType)
enum class EVHDoorStateSource : uint8
{
	Local	UMETA(DisplayName = "내 클라 (늘 로컬)"),
	Server	UMETA(DisplayName = "서버 (서버가 문을 보내면 서버, 아니면 로컬)"),
};

/** 열린 문 너머 보이는 범위를 정하는 방법 [2026-11-24] */
UENUM(BlueprintType)
enum class EVHDoorPeekMode : uint8
{
	Aperture			UMETA(DisplayName = "문틈 각도만"),
	ApertureAndFacing	UMETA(DisplayName = "문틈 + 바라보는 방향"),
};

/**
 * 카메라·이동·표시용 수치 모음 (DA_VHGameplayConfig).
 * 규칙 판정에 쓰는 값(대미지·시간·금액)은 서버 v3.0 값을 따른다. 여기에는 화면 표시용 값과 클라가 스스로 적용하는 값만 둔다.
 * 상호작용 안내 시간·손전등은 DA_VHInteractionConfig.
 * [제안]이 붙은 값은 기획에 없어서 임시로 정한 값. 길이는 언리얼 단위(cm), 시간은 초.
 */
UCLASS(BlueprintType)
class MCPTEST_API UVHGameplayConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UVHGameplayConfig();

	// ── 카메라 [팀 결정: 탑뷰, 회전 없음] ──

	/** 내려다보는 각도 (-90 = 바로 위, -60 = 비스듬히) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = -89, ClampMax = -20, Units = "deg"))
	float CameraPitch = -65.f;

	/** 캐릭터에서 카메라까지 거리 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = 200, Units = "cm"))
	float CameraDistance = 1500.f;

	/** 시야각 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = 30, ClampMax = 120, Units = "deg"))
	float CameraFOV = 70.f;

	/** 카메라가 부드럽게 따라오는 속도 (0이면 바로 따라옴) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = 0))
	float CameraLagSpeed = 10.f;

	// ── 벽 가림 처리: 카메라와 내 캐릭터 사이의 벽을 원 모양으로 비워 보이게 한다 [사용자 결정 2026-10-26] ──

	/** 켜면 내 캐릭터 앞 벽이 투명해진다 (벽 재질이 M_VHWallFade일 때만) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Occlusion")
	bool bWallOcclusionFade = true;

	/** 투명하게 만드는 반경 (카메라→캐릭터 선에서의 거리) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Occlusion", meta = (ClampMin = 0, Units = "cm"))
	float OcclusionRadius = 150.f;

	/** 벽 재질이 읽는 파라미터 모음 (PlayerPos·Radius·Enabled) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Occlusion")
	TSoftObjectPtr<UMaterialParameterCollection> OcclusionCollection =
		TSoftObjectPtr<UMaterialParameterCollection>(FSoftObjectPath(TEXT("/Game/VaultHeist/Materials/MPC_VHOcclusion.MPC_VHOcclusion")));

	// ── 천장 가림 (방 단위 시야) [사용자 요청 2026-11-21] ──
	// 내가 있는 구역(방 하나 또는 복도)만 보이고 나머지 구역은 벽 높이의 회색 천장으로 덮는다. 구역 = 레벨의 방 영역(AVHRoomVolume, 서버 맵 rooms 와 같음).
	// 덮인 구역 안의 다른 플레이어·경비원·아이템·금고·연출도 화면에서 숨긴다(판정은 서버 그대로, 보이는 것만). 콘솔 vh.Ceiling 0/1 로 끄고 켠다

	/** 천장 가림을 쓰는가 (콘솔 vh.Ceiling 0 이면 이 값과 관계없이 끔) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling")
	bool bCeiling = true;

	/** 천장 재질 (언릿·마스크, Color·Opacity 파라미터). 가려진 내 캐릭터는 아래 실루엣으로 보인다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling")
	TSoftObjectPtr<UMaterialInterface> CeilingMaterial =
		TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/VaultHeist/Materials/M_VHCeiling.M_VHCeiling")));

	/** 천장 색 (조명 영향 없음) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling")
	FLinearColor CeilingColor = FLinearColor(0.30f, 0.31f, 0.33f);

	/** 구역이 바뀔 때 천장이 사라지거나 덮이는 시간 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (ClampMin = 0, Units = "s"))
	float CeilingFadeSeconds = 0.2f;

	/** 문가 깜빡임 방지: 지금 구역에서 이만큼 벗어나야(그리고 다른 구역 안에 들어가야) 구역이 바뀐다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (ClampMin = 0, Units = "cm"))
	float CeilingSwitchMargin = 40.f;

	/** 서버 방 조각(이동 가능 칸)은 벽에서 25~30cm 떨어져 있다 → 천장을 이만큼 넓혀 벽 면까지 덮는다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (ClampMin = 0, Units = "cm"))
	float CeilingExpand = 30.f;

	/** 천장 높이 = 방 영역 위쪽(벽 높이 400) + 이 값. 벽 윗면보다 살짝 낮춰 구역 경계의 벽 선이 보이게 한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (Units = "cm"))
	float CeilingHeightOffset = -2.f;

	/** 내 구역 경계에서 이 거리 안의 물체는 덮인 구역에 있어도 보인다 (문가에 선 사람) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (ClampMin = 0, Units = "cm"))
	float CeilingVisibleMargin = 30.f;

	/**
	 * 내 캐릭터가 천장·벽에 가려지면 실루엣(반투명 색 + 윤곽선)만 비쳐 보이게 한다 [사용자 요청 2026-11-21 후속: 원 구멍 대신].
	 * 내 캐릭터 메시를 커스텀 뎁스(스텐실 0)에 그리고, 후처리 재질이 "가려진 내 픽셀"만 칠한다 → 옆 구역 안은 보이지 않는다
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling")
	bool bSilhouette = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling")
	TSoftObjectPtr<UMaterialInterface> SilhouetteMaterial =
		TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/VaultHeist/Materials/M_VHSilhouette.M_VHSilhouette")));

	/** 실루엣 색 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling")
	FLinearColor SilhouetteColor = FLinearColor(0.55f, 0.85f, 1.f);

	/** 실루엣 안쪽 불투명도 / 가장자리 불투명도 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (ClampMin = 0, ClampMax = 1))
	float SilhouetteFillOpacity = 0.45f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (ClampMin = 0, ClampMax = 1))
	float SilhouetteEdgeOpacity = 0.95f;

	/**
	 * 내 구역 앞 가림 지우기 [사용자 요청 2026-11-21 (4)]: 카메라 → 화면 픽셀 선을 CeilingCutPlaneZ 높이까지 늘린 점이 내 구역이면
	 * 그 픽셀을 가리는 다른 구역 천장·벽·문을 지운다(구역 지도 텍스처). 덮인 구역의 소품(가구)은 숨긴다(그림자·간접광은 그대로).
	 * 콘솔 vh.CeilingCut 0/1 로 끄고 켠다(비교용)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling")
	bool bCeilingCutaway = true;

	/** 선을 늘려 구역을 판단하는 높이 (0 = 바닥, 90 = 허리). 바닥이 내 구역 바닥을 벽 밑까지 다 보여 준다 (콘솔 vh.CeilingCutZ 로 비교) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (Units = "cm"))
	float CeilingCutPlaneZ = 0.f;

	/** 구역 지도 텍스처 한 칸 크기 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (ClampMin = 2, Units = "cm"))
	float CeilingZoneMapCell = 10.f;

	// ── 문 위치 표시 [사용자 요청 2026-11-21 (5)]: 내 구역 문 = 바닥 문턱 띠 + 바깥쪽 화살표, 다른 구역 문 = 천장 위 밝은 틈, 미니맵 = 방 사이 문 다리.
	//    잠긴 바깥 문은 모두 빨강(바닥은 화살표 대신 X). 콘솔 vh.DoorMarks 0/1

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling")
	bool bDoorMarks = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling")
	TSoftObjectPtr<UMaterialInterface> DoorMarkMaterial =
		TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/VaultHeist/Materials/M_VHDoorMark.M_VHDoorMark")));

	/** 문 표시 색 (바닥 띠·화살표·천장 틈) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling")
	FLinearColor DoorMarkColor = FLinearColor(0.15f, 0.7f, 1.f);

	/** 잠긴 문 표시 색 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling")
	FLinearColor DoorMarkLockedColor = FLinearColor(1.f, 0.12f, 0.08f);

	/** 바닥 표시 밝기(발광 배율) / 불투명도 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (ClampMin = 0))
	float DoorMarkIntensity = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (ClampMin = 0, ClampMax = 1))
	float DoorMarkOpacity = 0.85f;

	/** 문이 열리면 바닥 표시 밝기·불투명도에 곱하는 값 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (ClampMin = 1))
	float DoorMarkOpenBoost = 1.5f;

	/** 바닥 문턱 띠 깊이 (벽 가운데에서 내 구역 쪽으로) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (ClampMin = 5, Units = "cm"))
	float DoorMarkStripDepth = 45.f;

	/** 화살표 크기 / 벽 면에서 화살표 가운데까지 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (ClampMin = 10, Units = "cm"))
	float DoorMarkArrowSize = 90.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (ClampMin = 0, Units = "cm"))
	float DoorMarkArrowOffset = 80.f;

	/** 천장 위 밝은 틈: 밝기 / 불투명도 / 폭(벽을 가로지르는 방향, 벽 두께에 더함) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (ClampMin = 0))
	float CeilingDoorMarkIntensity = 1.8f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (ClampMin = 0, ClampMax = 1))
	float CeilingDoorMarkOpacity = 0.85f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (ClampMin = 0, Units = "cm"))
	float CeilingDoorMarkExtra = 40.f;

	/** 미니맵 문 표시 색 / 잠긴 문 색 / 굵기(픽셀) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling")
	FLinearColor MinimapDoorColor = FLinearColor(0.2f, 0.8f, 1.f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling")
	FLinearColor MinimapDoorLockedColor = FLinearColor(1.f, 0.15f, 0.1f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (ClampMin = 1))
	float MinimapDoorThickness = 3.f;

	/** 닫힌 문 표시 색 (바닥 띠·화살표·천장 틈) [2026-11-24 F 문]. 열린 문 = DoorMarkColor */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling")
	FLinearColor DoorMarkClosedColor = FLinearColor(0.9f, 0.9f, 0.95f);
	/** 닫힌 문 표시 밝기 배율 (열린 문 대비) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling", meta = (ClampMin = 0))
	float DoorMarkClosedIntensityScale = 0.45f;
	/** 미니맵 닫힌 문 색 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Ceiling")
	FLinearColor MinimapDoorClosedColor = FLinearColor(0.78f, 0.78f, 0.82f, 1.f);

	// ── 방 입구 문 [사용자 요청 2026-11-21 (4), 2026-11-24: F로 열고 닫기] ──
	//    처음엔 모두 닫힘. 문 앞에서 F(상자·아이템·반납이 F 대상이 아닐 때만) = 열기/닫기. 잠긴 바깥 문은 안 열림.
	//    문 상태는 UVHDoorSubsystem 한 곳에서 바뀐다 (DoorStateSource: 지금 = 내 클라, 나중 = 서버 메시지)

	/**
	 * 문 상태가 오는 곳. Server(기본) = 서버가 Door 엔티티를 보내면 F 는 서버로 요청만 하고 열림은 서버 스냅샷(ES_OPENED)을 따른다
	 * (2026-11-24 팀 서버). 서버 문이 없으면(가짜 서버) 로컬. Local = 늘 내 클라에서 바로 (다른 클라와 안 맞음)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	EVHDoorStateSource DoorStateSource = EVHDoorStateSource::Server;

	/** 서버 문: 문 가운데에서 이 거리(2D) 안에서만 F 로 고른다. 서버 판정 GameData::DoorRangeCm 150 에서 위치 차이 여유를 뺀 값 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door", meta = (ClampMin = 30, Units = "cm"))
	float DoorServerRange = 140.f;

	/**
	 * 문간 점유 범위: 문 위 벽 바닥 사각형(벽 두께 × 문 폭)을 사방으로 이만큼 넓힌 범위에 몸 중심이 있으면 닫을 수 없다.
	 * 서버 GameData::BodyRadiusCm(40)와 같아야 한다 (정호님 답변 2026-11-25: 서버 DoorOccupied 와 같은 범위)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door", meta = (ClampMin = 0, Units = "cm"))
	float DoorOccupyRadius = 40.f;

	/** F로 문을 고를 수 있는 거리: 벽 면에서 앞뒤로 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door", meta = (ClampMin = 30, Units = "cm"))
	float DoorInteractDepth = 150.f;

	/** F로 문을 고를 수 있는 거리: 문 폭 바깥으로 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door", meta = (ClampMin = 0, Units = "cm"))
	float DoorInteractSideMargin = 40.f;

	/** 닫힌 문은 내 캐릭터가 지나가지 못한다 (클라에서만 막음. 서버 판정은 그대로) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	bool bDoorBlockClosed = true;

	/** 문이 이만큼(0~1) 열리면 지나갈 수 있다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door", meta = (ClampMin = 0, ClampMax = 1))
	float DoorPassAlpha = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door", meta = (ClampMin = 0.05, Units = "s"))
	float DoorOpenSeconds = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door", meta = (ClampMin = 0.05, Units = "s"))
	float DoorCloseSeconds = 0.5f;

	// ── 열린 문 너머 엿보기 (부채꼴 시야) [사용자 요청 2026-11-24] ──
	//    내 구역의 열린 문마다: 내 위치에서 문틈 양 끝을 지나는 두 선 사이만큼 문 너머 구역이 보인다(벽에 막힌 곳은 안 보임).
	//    콘솔 vh.DoorPeek 0/1

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Peek")
	bool bDoorPeek = true;

	/** 보이는 범위를 정하는 방법: 문틈 각도만(지금) / 문틈 ∩ 바라보는 방향(마우스) 부채꼴 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Peek")
	EVHDoorPeekMode DoorPeekMode = EVHDoorPeekMode::Aperture;

	/** DoorPeekMode = 바라보는 방향일 때 부채꼴 반각 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Peek", meta = (ClampMin = 5, ClampMax = 90, Units = "deg"))
	float DoorPeekFacingHalfAngle = 60.f;

	/** 문 하나당 광선 수 (벽에 막히는 모양을 이만큼 잘게) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Peek", meta = (ClampMin = 4, ClampMax = 256))
	int32 DoorPeekRays = 64;

	/** 문 너머로 보이는 최대 거리 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Peek", meta = (ClampMin = 100, Units = "cm"))
	float DoorPeekMaxDistance = 3000.f;

	/** 엿보기 마스크 해상도 배율 (구역 지도 한 칸을 이만큼 나눔. 3 = 약 3cm 칸, 가장자리가 매끈해진다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Peek", meta = (ClampMin = 1, ClampMax = 4))
	int32 DoorPeekMaskScale = 3;

	/** 문짝 재질 (훈연 오크, 벽과 같은 벽 투명 재질) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	TSoftObjectPtr<UMaterialInterface> DoorPanelMaterial =
		TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/VaultHeist/Materials/MansionShell/MI_Shell_Wall_SmokedOak.MI_Shell_Wall_SmokedOak")));

	/** 문틀·손잡이 재질 (검은 금속, 창틀과 같은 것) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	TSoftObjectPtr<UMaterialInterface> DoorFrameMaterial =
		TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/VaultHeist/Materials/MansionShell/MI_Shell_Metal_Black.MI_Shell_Metal_Black")));

	/** 잠긴 문 표시 재질 (빨강) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	TSoftObjectPtr<UMaterialInterface> DoorLockMaterial =
		TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/VaultHeist/Materials/MansionShell/MI_VHDoorLock.MI_VHDoorLock")));

	// ── 맵 밝기 [교수님 피드백 2026-10-30: 손전등 없이도 방 구조가 보이게, 손전등 쪽은 여전히 확실히 밝게] ──
	// 레벨에 VH Level View Settings 액터가 있는 레벨(L_Mansion_1F)에서만 적용한다. 노출은 레벨 후처리에서 고정이라
	// 달빛을 올려도 손전등 빛의 밝기는 그대로다. 플레이 중 콘솔: VHMapLight 값

	/** 켜면 게임 시작 시 레벨의 달빛(Directional Light) 밝기를 아래 값으로 바꾼다. 끄면 레벨에 저장된 값 그대로 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
	bool bOverrideMapMoonlight = true;

	/**
	 * 맵 조명 밝기 (lux). 5 = 일반 밝기 [사용자 결정 2026-11-13, D3: 손전등을 끄고 맵을 일반 밝기로].
	 * 레벨의 빛 색도 흰색에 가깝게 바꿨다(L_Mansion_1F의 Directional Light). 예전 값: 0.8 = 어둑함(2026-10-30), 0.12 = 손전등 없으면 거의 안 보임
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting", meta = (ClampMin = 0, Units = "lux"))
	float MapMoonlightIntensity = 5.f;

	// ── 시선·이동 기준 ⟪기획 변경 2026-10-26: 팀 결정⟫ ──
	// 캐릭터의 "앞"은 항상 마우스 커서 방향. 몸 전체가 커서가 가리키는 바닥 지점을 바라보고(손전등도),
	// WASD는 그 방향 기준으로 움직인다(W 앞 / S 뒤 / A 왼쪽 / D 오른쪽). 카메라는 탑뷰 고정.
	// (게임_시스템.pdf 4장의 "W = 월드 X축 고정, 상체만 마우스 ±90°"를 대체)

	/** 몸이 커서 쪽으로 도는 속도. 0이면 즉시 [가정: 거의 즉시] */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim", meta = (ClampMin = 0, Units = "deg/s"))
	float BodyTurnSpeed = 1800.f;

	/** 커서가 캐릭터에서 이 거리 안이면 마지막 방향을 유지한다(흔들림 방지) [가정] */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim", meta = (ClampMin = 0, Units = "cm"))
	float AimDeadZone = 100.f;

	/**
	 * 이동 기준 [사용자 결정 2026-11-13, D1]. 켜면 WASD = 화면(월드) 축 고정: W 화면 위 / S 아래 / A 왼쪽 / D 오른쪽.
	 * 끄면 커서 방향 기준(W = 커서 쪽, 2026-10-26 방식). 어느 쪽이든 시선·공격 방향은 커서다.
	 * 플레이 중 콘솔: vh.MoveMode 1(화면 축) / 2(커서 기준) / 0(이 설정값)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	bool bScreenAxisMove = true;

	/** 방향별 이동 속도 배율 (1 = 기본 속도, 커서 기준 이동일 때만 쓴다). 앞(W) [가정: 모두 1] */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = 0, ClampMax = 2))
	float ForwardSpeedScale = 1.f;

	/** 뒤(S)로 움직일 때 속도 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = 0, ClampMax = 2))
	float BackwardSpeedScale = 1.f;

	/** 옆(A·D)으로 움직일 때 속도 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = 0, ClampMax = 2))
	float StrafeSpeedScale = 1.f;

	/** 머리 위에 시선 방향 화살표 표시 (상체 애니메이션 대신) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim")
	bool bShowLookIndicator = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim")
	FLinearColor LookIndicatorColor = FLinearColor(1.f, 0.85f, 0.3f);

	// ── 의적 능력치 (이동 속도·SP·최대 HP. 서버 GameData.h 와 같은 값으로 맞춰 둔다) ──

	/** 의적 기본 능력치 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	FVHCharacterStats ThiefStats;

	// ── 상태이상 ──

	/** 탈진 중 스테미나 회복 배율 (0.5 = 50% 감소) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status", meta = (ClampMin = 0, ClampMax = 1))
	float ExhaustedRegenMultiplier = 0.5f;

	// ── 서버가 상태만 알려 주고 클라가 스스로 적용하는 이동 속도 [서버 v3.0 T-64. 서버 GameData.h 와 같은 값] ──

	/** 끈끈이 함정 위(ES_SLOWED): 걷기·달리기 속도 배율 (0.5 = −50%) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status|ClientApplied", meta = (ClampMin = 0, ClampMax = 1))
	float StickySpeedScale = 0.5f;

	/** 은신 중(ES_CLOAKED): 이동 속도 배율 (0.2 = −80%). 은신 중에는 달리기·구르기도 못 한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status|ClientApplied", meta = (ClampMin = 0, ClampMax = 1))
	float CloakSpeedScale = 0.2f;

	/** 운동화 퍽(4번): 달리기 속도 배율 (1.2 = +20%) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status|ClientApplied", meta = (ClampMin = 1, ClampMax = 2))
	float SneakersRunScale = 1.2f;

	/** 제트팩(ES_JETPACK, 3초): 커서 방향으로 날아가는 속도 (기획 10m/s). 이동은 클라가, 부딪침 판정은 서버가 한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status|ClientApplied", meta = (ClampMin = 0, Units = "cm/s"))
	float JetpackSpeed = 1000.f;

	/** 밀려남(SC_KNOCKBACK): 서버가 알려 준 거리(3m)를 이 시간에 걸쳐 밀려난다 [제안] */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status|ClientApplied", meta = (ClampMin = 0.05, Units = "s"))
	float KnockbackSeconds = 0.25f;

	// ── 스킬·상태 연출 (임시 도형. 색·투명도만 여기서 고친다) ──

	/** 연출용 반투명 머티리얼 (파라미터 Color·Opacity) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx")
	TSoftObjectPtr<UMaterialInterface> FxMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/VaultHeist/Materials/M_VHFx.M_VHFx")));

	/** 끈끈이 함정 바닥 색 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx")
	FLinearColor StickyTrapColor = FLinearColor(0.35f, 0.9f, 0.2f);

	/** 은신 중 내 캐릭터 색 (반투명) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx")
	FLinearColor CloakSelfColor = FLinearColor(0.45f, 0.8f, 1.f);

	/** 은신 중 내 캐릭터 불투명도 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx", meta = (ClampMin = 0, ClampMax = 1))
	float CloakSelfOpacity = 0.3f;

	/** 연막 색 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx")
	FLinearColor SmokeColor = FLinearColor(0.3f, 0.3f, 0.33f);

	/** 연막 불투명도 (높을수록 안이 안 보인다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx", meta = (ClampMin = 0, ClampMax = 1))
	float SmokeOpacity = 0.6f;

	/** 연막 안에 있을 때 내 캐릭터 색 (윤곽만 보이게 반투명) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx")
	FLinearColor SmokeSelfColor = FLinearColor(1.f, 1.f, 1.f);

	/** 섬광에 맞았을 때 화면이 하얗게 된 뒤 원래대로 돌아오는 시간 (기획 3초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx", meta = (ClampMin = 0.1, Units = "s"))
	float FlashFadeSeconds = 3.f;

	// ── 게임 규칙 [기획 PDF 2장] ──

	/** 제한 시간 5분 [서버 v3.0 기준]. 서버가 보낸 duration_ms 가 오면 그 값을 쓰고, 이 값은 그 전의 시계·가짜 서버·샘플 결과용 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match", meta = (ClampMin = 10, Units = "s"))
	float TimeLimitSeconds = 300.f;

	/** 금고 열림 같은 전체 공지를 화면 가운데 배너로 보여주는 시간 [제안] */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match", meta = (ClampMin = 0, Units = "s"))
	float NoticeBannerSeconds = 3.f;

	/** 쓰러짐 창 표시용: 잃는 반납 금액 비율(%). 실제 손실은 서버가 계산해 SC_DOWNED 로 보낸다 [서버 GameData LossPctByPlayer·LossPctByGuard] */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match|Downed", meta = (ClampMin = 0, ClampMax = 100))
	int32 DownLossPctByPlayer = 10;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match|Downed", meta = (ClampMin = 0, ClampMax = 100))
	int32 DownLossPctByGuard = 40;

	// ── 반납지점 (서버가 만들고 없앤다. 아래는 표시용) ──

	/**
	 * 서버 없이 놓인 반납지점(시험 레벨)만 쓰는 값: 등장 후 이 시각에 스스로 빨개진다 [사용자 요청 2026-11-21: 경고 5초].
	 * 서버가 만든 반납지점은 빨강 = 서버 경고 비트(ES_WARNING), 남은 시간 = 서버가 알려 준 timer_ms 라 이 값을 쓰지 않는다
	 * (2026-11-23 팀 서버: 수명 30초, 경고 25초)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ReturnPoint", meta = (Units = "s"))
	float ReturnPointRedSeconds = 25.f;

	/** 서버 없이 놓인 반납지점의 수명 (서버가 만든 것은 서버 timer_ms 로 센다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ReturnPoint", meta = (Units = "s"))
	float ReturnPointLifetimeSeconds = 30.f;

	/** 정사각형 한 변 1m */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ReturnPoint", meta = (ClampMin = 10, Units = "cm"))
	float ReturnPointSize = 100.f;

	/** 반납 작업 시간의 안내용 값. 실제 시간은 서버가 SC_INTERACT duration_ms 로 알려 준다 (2026-11-23 팀 서버 4초, 작업 속도 퍽과 관계없음) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ReturnPoint", meta = (ClampMin = 0, Units = "s"))
	float ReturnSeconds = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ReturnPoint")
	FLinearColor ReturnPointGreen = FLinearColor(0.05f, 1.f, 0.2f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ReturnPoint")
	FLinearColor ReturnPointRed = FLinearColor(0.85f, 0.f, 0.f);

	/** 반납지점 밝기 (조명과 관계없이 보이도록 스스로 빛나는 정도). 초록 상태에 쓴다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ReturnPoint", meta = (ClampMin = 0))
	float ReturnPointGlow = 2.f;

	/** 빨간 상태 밝기. 빨강을 너무 밝게(>1) 빛나게 하면 톤매핑·블룸 때문에 살구색으로 보여서 따로 낮게 둔다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ReturnPoint", meta = (ClampMin = 0))
	float ReturnPointRedGlow = 1.f;

	/** 조명과 관계없이 보이는 단색 머티리얼 (Color 벡터 파라미터) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ReturnPoint")
	TSoftObjectPtr<UMaterialInterface> UnlitColorMaterial;

	/** 반납지점으로 만들 클래스(외형을 바꾼 블루프린트). 비우면 C++ 기본 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ReturnPoint")
	TSoftClassPtr<AVHReturnPoint> ReturnPointClass;

	/** 서버가 보낸 위치에 만드는 금고 클래스(새 맵: 레벨에 금고 자리가 없다). 비우면 C++ 기본 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Safe")
	TSoftClassPtr<AVHSafe> SafeClass = TSoftClassPtr<AVHSafe>(FSoftObjectPath(TEXT("/Game/VaultHeist/Blueprints/BP_VHSafe.BP_VHSafe_C")));

	// ── 테두리 표시 [팀 결정: 진짜 외곽선, 손전등 빛 안에서만] ──

	/** 포스트 프로세스 외곽선 머티리얼 (커스텀 스텐실 사용) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Outline")
	TSoftObjectPtr<UMaterialInterface> OutlineMaterial;

	/** 손전등 빛 안에 들어온 물품 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Outline")
	FLinearColor OutlineLitColor = FLinearColor(1.f, 0.88f, 0.f, 1.f);	// 노란색 [기획 2026-11 조작키 2장]

	/** 현재 상호작용 대상 (더 진한 색) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Outline")
	FLinearColor OutlineFocusColor = FLinearColor(1.f, 1.f, 0.f, 1.f);	// F 대상도 노란색(조금 더 밝게)

	/** 현재 대상이지만 지금은 할 수 없음 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Outline")
	FLinearColor OutlineBlockedColor = FLinearColor(0.55f, 0.55f, 0.55f, 1.f);

	/** 외곽선 두께(픽셀) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Outline", meta = (ClampMin = 1, ClampMax = 8))
	float OutlineThickness = 2.f;

	// ── 문구 ──

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text")
	FText ReturnText;
};
