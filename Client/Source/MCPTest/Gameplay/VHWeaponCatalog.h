#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VHWeaponCatalog.generated.h"

class UStaticMesh;
class UTexture2D;
class UMaterialInterface;
class UNiagaraSystem;

/** 무기 번호. 서버 프로토콜(RobProto::WeaponId)과 같은 값이다 (.cpp 에서 값이 같은지 검사한다) */
UENUM(BlueprintType)
enum class EVHWeaponId : uint8
{
	None			UMETA(DisplayName = "없음"),
	Baton			UMETA(DisplayName = "01 삼단봉"),
	Sword			UMETA(DisplayName = "02 도검"),
	BeamSword		UMETA(DisplayName = "03 빔 소드"),
	Pistol			UMETA(DisplayName = "04 권총"),
	Flamethrower	UMETA(DisplayName = "05 화염방사기"),
	Railgun			UMETA(DisplayName = "06 레일건"),
	Taser			UMETA(DisplayName = "07 테이저건"),
	Grenade			UMETA(DisplayName = "08 수류탄"),
	BlastMine		UMETA(DisplayName = "09 폭발 지뢰"),
	ShockMine		UMETA(DisplayName = "10 감전 지뢰"),
};

/** 무기 분류 = 발광 색 [구상안 v2.2: 발광 색 = 분류, 실루엣 = 무기] */
UENUM(BlueprintType)
enum class EVHWeaponCategory : uint8
{
	Melee	UMETA(DisplayName = "근접 (루비 레드)"),
	Ranged	UMETA(DisplayName = "원거리 (아이스 시안)"),
	Thrown	UMETA(DisplayName = "투척 (앰버 오렌지)"),
	Placed	UMETA(DisplayName = "설치 (라임)"),
};

/** 바닥에 떨어진 무기를 무엇으로 보여 줄까 (무기별로 고른다) */
UENUM(BlueprintType)
enum class EVHWeaponFloorMode : uint8
{
	/** 3D 모델을 바닥 자세(FloorOffset)로 놓는다 */
	Model	UMETA(DisplayName = "3D 모델"),
	/** 구상안의 탑뷰 그림을 입힌 납작한 판을 바닥에 눕혀 놓는다 (그림 그대로 보인다) */
	FlatArt	UMETA(DisplayName = "구상안 탑뷰 그림 (납작한 판)"),
};

/**
 * 무기 메시의 기준점 (2026-11-20, 모델 교체 준비). 모델러가 메시에 같은 이름의 소켓을 넣으면 이펙트·소리·손 위치가 그 소켓을 따라간다.
 * 소켓이 없으면 무기 표의 대신 값(…Fallback) → 그것도 0 이면 지금처럼 메시 크기로 계산한 자리. → Docs/무기_모델_제작규격.md
 */
UENUM(BlueprintType)
enum class EVHWeaponPoint : uint8
{
	/** 총구: 총알·빔·불꽃이 나가는 곳 (원거리·레일건·화염방사기·테이저건) */
	Muzzle		UMETA(DisplayName = "Muzzle (총구)"),
	/** 칼끝·봉 끝: 궤적 리본이 따라가는 곳 (근접) */
	BladeTip	UMETA(DisplayName = "BladeTip (칼끝)"),
	/** 칼날이 시작하는 곳 (손잡이와 날 사이) */
	BladeBase	UMETA(DisplayName = "BladeBase (칼날 시작)"),
	/** 오른손이 쥐는 곳. 있으면 이 점이 손에 오도록 모델을 옮긴다 (없으면 피벗) */
	Grip		UMETA(DisplayName = "Grip (손잡이)"),
	/** 왼손(보조 손)이 받치는 곳 (화염방사기·레일건·지뢰). 없으면 DA_VHCharacterVisuals 의 Left Hand On Weapon */
	SupportHand	UMETA(DisplayName = "SupportHand (보조 손)"),
};

/**
 * 쏠 때 반동 (2026-11-20, 보이는 연출만). 손에 든 무기·팔(손 IK 목표)·상체(허리 뼈)에 더한다.
 * 서버 판정·조준 방향·총알/빔이 나가는 방향은 바꾸지 않는다 (이펙트는 발사 순간의 총구 소켓·조준 yaw 를 쓴다).
 * 곡선 모양은 Lyra MM_Pistol_Fire·MM_Rifle_Fire 에서 뽑은 것(Tools/External/lyra_recoil.json): 총구가 먼저 튀고(RiseSeconds),
 * 손·상체는 조금 늦게(× KickLag) 가장 뒤로 갔다가 RecoverSeconds 동안 제자리로 돌아온다.
 */
USTRUCT(BlueprintType)
struct FVHWeaponRecoil
{
	GENERATED_BODY()

	/** 총이 뒤로 튀는 거리 (cm, 손·팔이 같이 밀린다). 0 이고 총구 들림도 0 이면 반동 없음 → 예전 키 프레임 동작(DA_VHCharacterVisuals) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 0, Units = "cm"))
	float KickBackCm = 0.f;

	/** 총구가 들리는 각도 (도) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 0, Units = "deg"))
	float MuzzleUpDeg = 0.f;

	/** 총구가 들릴 때 손이 같이 올라가는 높이 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 0, Units = "cm"))
	float KickUpCm = 0.f;

	/** 발사마다 좌우로 무작위로 틀어지는 각도 (도, ±) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 0, Units = "deg"))
	float SideDeg = 0.f;

	/** 상체가 뒤로 젖혀지는 각도 (도) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 0, Units = "deg"))
	float SpineBackDeg = 0.f;

	/** 상체(허리)가 비틀리는 각도 (도, Lyra 라이플처럼 오른쪽 어깨가 밀린다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (Units = "deg"))
	float SpineTwistDeg = 0.f;

	/** 총구가 가장 많이 들릴 때까지 (초). 짧을수록 날카롭다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 0.01, Units = "s"))
	float RiseSeconds = 0.05f;

	/** 손·상체가 가장 뒤로 가는 때 = RiseSeconds × 이 값 (Lyra 권총 2.0: 총구가 먼저, 몸이 뒤따른다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 1))
	float KickLag = 1.6f;

	/** 제자리로 돌아오는 시간 (초). 길수록 무겁다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 0.02, Units = "s"))
	float RecoverSeconds = 0.18f;

	/** 연사할 때 반동이 쌓이는 최대 배수 (1 = 쌓이지 않음) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 1))
	float MaxStack = 1.4f;

	// ── 쏘는 동안 계속 (화염방사기): 튀는 반동 대신 잔잔한 떨림 ──

	/** 꾹 누르는 동안 손 떨림 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Sustain", meta = (ClampMin = 0, Units = "cm"))
	float SustainShakeCm = 0.f;

	/** 꾹 누르는 동안 총구 떨림 (도) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Sustain", meta = (ClampMin = 0, Units = "deg"))
	float SustainShakeDeg = 0.f;

	/** 떨림 빠르기 (Hz) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Sustain", meta = (ClampMin = 0))
	float SustainShakeHz = 12.f;

	// ── 화면 흔들림 (내 캐릭터만, 탑뷰라 아주 약하게) ──

	/** 화면이 흔들리는 크기 (cm, 카메라 위치). 0 = 없음 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Camera", meta = (ClampMin = 0, Units = "cm"))
	float CameraShakeCm = 0.f;

	/** 화면 흔들림 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Camera", meta = (ClampMin = 0.02, Units = "s"))
	float CameraShakeSeconds = 0.12f;

	bool HasKick() const { return KickBackCm > 0.f || MuzzleUpDeg > 0.f; }
	/** 한 발 반동이 끝나는 시간 (초) */
	float TotalSeconds() const { return RiseSeconds * KickLag + RecoverSeconds; }
	/** 한 발의 곡선 (T = 쏜 뒤 시간). OutMuzzle = 총구 들림(0~1, 먼저), OutBody = 손·상체 밀림(0~1, 늦게) */
	void Evaluate(float T, float& OutMuzzle, float& OutBody) const;
};

/**
 * 게임 중 마우스 조준점 모양 (2026-11-25, 가시성 우선: 모든 모양이 지름 48px 이상·선 3px 이상 + 어두운 테두리).
 * 크기·굵기·색·테두리·투명도 공통 값은 DA_VHUIStyle 의 HUD|Crosshair, 무기별 모양·사거리·반동은 여기(DA_VHWeaponCatalog).
 */
UENUM(BlueprintType)
enum class EVHCrosshairShape : uint8
{
	/** 무기 종류로 정한다 (근접 = 괄호, 총 = 십자, 레일건 = 링, 화염 = 부채꼴, 투척·설치 = 착탄 원) */
	Auto		UMETA(DisplayName = "자동 (무기 종류로)"),
	/** 굵은 괄호(호) 4개가 둘러싼 모양 — 맨손·근접 */
	Brackets	UMETA(DisplayName = "근접: 괄호 4개"),
	/** 굵은 십자(선 4개 + 가운데 틈) — 권총·테이저건 */
	Cross		UMETA(DisplayName = "총: 틈 있는 십자"),
	/** 굵은 십자 + 바깥 큰 원 (충전 1초 동안 원이 채워지고, 잠금 중엔 색이 바뀐다) — 레일건 */
	RailRing	UMETA(DisplayName = "레일건: 십자 + 큰 원 (충전 링)"),
	/** 굵은 부채꼴 테두리 (사거리·각도 느낌, 내 캐릭터 → 커서 방향) — 화염방사기 */
	Cone		UMETA(DisplayName = "화염: 부채꼴 테두리"),
	/** 큰 착탄 원 + 가운데 십자 (원 크기 = 폭발 범위 느낌) — 수류탄·지뢰 */
	Area		UMETA(DisplayName = "투척·설치: 착탄 원 + 십자"),
	/** 원 + 번개 — 사제 테이저건(Q로 든 상태) */
	Lightning	UMETA(DisplayName = "사제 테이저: 번개"),
};

/** 무기 하나의 조준점 설정. 음수 = 자동(서버 GameData.h 무기 표와 같은 값) */
USTRUCT(BlueprintType)
struct FVHCrosshairSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crosshair")
	EVHCrosshairShape Shape = EVHCrosshairShape::Auto;

	/**
	 * 사거리 (cm): 커서가 내 캐릭터에서 이보다 멀면 조준점 색이 "사거리 밖" 색으로 바뀐다. 0 = 무제한(권총·레일건·지뢰), 음수 = 자동(서버 값).
	 * 수류탄은 던질 수 있는 최대 거리(6m): 더 멀면 실제 떨어질 자리에 작은 원을 함께 그린다. 판정은 서버가 한다(표시용)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crosshair", meta = (Units = "cm"))
	float RangeCm = -1.f;

	/** 부채꼴 반각 (도, 화염방사기). 음수 = 자동(서버 15°). 화면에는 DA_VHUIStyle CrosshairConeAngleScale 배로 넓혀 그린다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crosshair", meta = (Units = "deg"))
	float HalfAngleDeg = -1.f;

	/** 폭발 반지름 (cm, 투척·설치). 원 크기 = 이 값 × CrosshairAreaPxPerCm (최소 크기는 지킨다). 음수 = 자동(수류탄 600·지뢰 300) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crosshair", meta = (Units = "cm"))
	float AreaRadiusCm = -1.f;

	/** 쏠 때 조준점이 벌어지는 크기 (1080p 기준 px, 판정과 무관한 연출). 음수 = 자동 (권총 작게 · 레일건 크게) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crosshair|Kick")
	float KickPx = -1.f;

	/** 벌어졌다가 제자리로 돌아오는 시간 (초). 음수 = 자동 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crosshair|Kick", meta = (Units = "s"))
	float KickRecoverSeconds = -1.f;

	/** 크기 배율 (DA_VHUIStyle CrosshairSize 에 곱한다. 최소 지름은 CrosshairMinSize 로 지킨다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crosshair", meta = (ClampMin = 0.5))
	float SizeScale = 1.f;
};

/** 무기 하나의 모습. 모델러가 만든 모델로 바꿀 때는 Mesh 칸만 바꾸면 된다 (크기·방향이 약속과 다르면 Model 칸) → Docs/무기_교체_가이드.md */
USTRUCT(BlueprintType)
struct FVHWeaponVisual
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	EVHWeaponId Id = EVHWeaponId::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	EVHWeaponCategory Category = EVHWeaponCategory::Melee;

	/**
	 * 3D 모델. 약속: 실제 크기(cm), +X = 앞(총구·칼끝), +Z = 위, 피벗 = 손잡이(쥐는 곳).
	 * 설치형(지뢰)은 피벗 = 바닥 중앙, +Z = 위. 소켓 Muzzle·BladeTip·BladeBase·Grip·SupportHand 를 넣으면 그 자리를 따른다
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TSoftObjectPtr<UStaticMesh> Mesh;

	// ── 모델 보정: 모델이 약속(cm, +X 앞)과 다르게 왔을 때. 손·바닥·날아가는 모습 모두에 적용된다 ──

	/** 모델 회전 보정 (예: 총구가 +Y 쪽이면 Yaw −90). 소켓·자동 계산 자리도 같이 돈다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FRotator ModelRotation = FRotator::ZeroRotator;

	/** 모델 크기 보정 (1 = 그대로. 미터 단위로 와서 작으면 100, 실제보다 크게 만들어졌으면 1보다 작게) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model", meta = (ClampMin = 0.001))
	float ModelScale = 1.f;

	/** 총구 소켓이 없을 때 쓸 자리 (모델 좌표 cm). 0 = 자동(모델 앞 끝 가운데) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FVector MuzzleFallback = FVector::ZeroVector;

	/** 칼끝 소켓이 없을 때 쓸 자리 (모델 좌표 cm). 0 = 자동(피벗에서 앞 끝까지의 90%) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FVector BladeTipFallback = FVector::ZeroVector;

	/** 칼날 시작 소켓이 없을 때 쓸 자리 (모델 좌표 cm). 0 = 자동(피벗에서 앞 끝까지의 25%) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FVector BladeBaseFallback = FVector::ZeroVector;

	/** 소켓 이름 */
	static FName SocketName(EVHWeaponPoint Point);
	/**
	 * 기준점의 모델 좌표(cm, 메시 그대로의 좌표). 소켓 → 대신 값 → 자동 계산 순서.
	 * OutFromSocket = 소켓에서 왔는가. 반환 false = 이 점을 정할 수 없음(보조 손에 소켓이 없을 때 등)
	 */
	bool PointLocal(const UStaticMesh* InMesh, EVHWeaponPoint Point, FVector& Out, bool* OutFromSocket = nullptr) const;
	/**
	 * 모델 좌표 → 무기 좌표 (+X 앞, cm). ModelRotation·ModelScale 와 표시 배율(DisplayScale: 길이 X, 두께 Y·Z)을 곱하고,
	 * bToGrip 이면 Grip 소켓(있을 때)이 원점에 오게 옮긴다. 손에 붙일 때 = 이것 × 손 보정
	 */
	FTransform MeshToWeapon(const UStaticMesh* InMesh, const FVector& DisplayScale, bool bToGrip) const;

	/** HUD 무기 칸 아이콘 (배경 투명, 가로:세로 = 2:1) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TSoftObjectPtr<UTexture2D> Icon;

	/** 재질 덮어쓰기 (메시의 재질 칸 순서대로. 비워 둔 칸은 메시에 들어 있는 재질 그대로) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TArray<TSoftObjectPtr<UMaterialInterface>> MaterialOverrides;

	// ── 손에 들었을 때 (무기 쪽 보정. 캐릭터 쪽 보정은 DA_VHCharacterVisuals) ──

	/** 손 소켓에 붙일 때의 위치·회전 보정 (이 무기만의 값: 손잡이가 피벗과 조금 다를 때) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Held")
	FTransform GripOffset;

	/** 들었을 때 크기 배율 (전체 배율 HeldScale 에 곱한다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Held", meta = (ClampMin = 0.1))
	float HeldScale = 1.f;

	/**
	 * 들었을 때 두께 배율 (무기의 폭·높이(Y·Z)에만 곱한다. 길이는 그대로).
	 * 칼·봉처럼 가는 무기가 탑뷰에서 선으로만 보일 때 키운다 → 길이(캐릭터 키를 넘지 않게)와 따로 고칠 수 있다
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Held", meta = (ClampMin = 0.1))
	float HeldThickness = 1.f;

	/** 쏠 때 반동 (원거리 무기). 세기·복귀 시간을 여기서 숫자로 고친다 (2026-11-20) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	FVHWeaponRecoil Recoil;

	/** 게임 중 마우스 조준점 (모양·사거리·반동 벌어짐). 공통 크기·색은 DA_VHUIStyle (2026-11-25) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crosshair")
	FVHCrosshairSpec Crosshair;

	// ── 바닥에 떨어져 있을 때 ──

	/** 바닥에서 보여 주는 방식: 3D 모델 / 구상안 탑뷰 그림. 그림(FloorArt)이 비어 있으면 3D 모델로 보여 준다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor")
	EVHWeaponFloorMode FloorMode = EVHWeaponFloorMode::Model;

	/** 구상안 탑뷰 그림 (배경 투명, 가로:세로 = 2:1, 오른쪽 = 총구·칼끝) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor")
	TSoftObjectPtr<UTexture2D> FloorArt;

	/** 탑뷰 그림 한 장이 실제로 차지하는 크기 (cm, 가로 × 세로). 그림 속 무기가 실제 크기로 보이게 맞춘 값 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor")
	FVector2D FloorArtSize = FVector2D(40.0, 20.0);

	/** 바닥에 놓인 자세 (탑뷰에서 옆모습이 보이게 눕히는 회전, 바닥에서 띄우는 높이) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor")
	FTransform FloorOffset;

	/** 바닥에 있을 때 크기 배율 (전체 배율 FloorScale 에 곱한다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor", meta = (ClampMin = 0.1))
	float FloorScale = 1.f;
};

/** 소비 아이템 번호. 서버 프로토콜(RobProto::ConsumableId)과 같은 값이다 */
UENUM(BlueprintType)
enum class EVHConsumableId : uint8
{
	None		UMETA(DisplayName = "없음"),
	HealKit		UMETA(DisplayName = "C1 회복 키트"),
	Smoke		UMETA(DisplayName = "C2 연막탄"),
	Flashbang	UMETA(DisplayName = "C3 섬광탄"),
	Jetpack		UMETA(DisplayName = "C4 제트팩"),
};

/**
 * 소비 아이템 하나의 모습 [구상안 Docs/Design/Consumables_v1]: 실루엣 = 종류, 발광 띠 색 = 종류.
 * 바닥 모델·HUD 소비 칸 아이콘·바닥 정보창 아이콘이 같은 디자인을 쓴다. 모델러 모델로 바꿀 때는 이 칸들만 바꾼다
 */
USTRUCT(BlueprintType)
struct FVHConsumableVisual
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Consumable")
	EVHConsumableId Id = EVHConsumableId::None;

	/** 바닥 모델. 약속: 원점 = 바닥 가운데, +Z = 위, 탑뷰에서 잘 보이는 크기(cm)로 만든다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Consumable")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** HUD 소비 칸·바닥 정보창 아이콘 (배경 투명, 정사각형) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Consumable")
	TSoftObjectPtr<UTexture2D> Icon;

	/** 발광 띠 색 (모델의 띠 재질과 같은 색. HUD 칸·정보창의 색 띠도 이 색) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Consumable")
	FLinearColor BandColor = FLinearColor::White;

	/** 바닥 모델 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Consumable", meta = (ClampMin = 0.1))
	float FloorScale = 1.f;

	/** 바닥에서 띄우는 높이 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Consumable", meta = (Units = "cm"))
	float FloorHeight = 3.f;
};

/**
 * 무기 하나의 이펙트 묶음 (2026-11-19, Docs/무기_연출_계획_2026-11-19.md). 비어 있는 칸은 예전 도형 방식으로 그린다.
 * 나이아가라 사용자 변수: User.Color, User.Radius(cm), User.Width(cm), User.Life(초), User.BeamEnd(월드 위치) — 있는 것만 쓴다.
 */
USTRUCT(BlueprintType)
struct FVHWeaponFxSet
{
	GENERATED_BODY()

	/** 날·봉 끝을 따라가는 궤적 리본 (근접). 휘두르는 빠른 구간 동안만 켠다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx") TSoftObjectPtr<UNiagaraSystem> Trail;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx") FLinearColor TrailColor = FLinearColor::White;
	/** 리본 폭(cm)과 꼬리 길이(초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx", meta = (ClampMin = 0)) float TrailWidth = 12.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx", meta = (ClampMin = 0)) float TrailLife = 0.12f;

	/** 총구 섬광 (총구 위치, 조준 방향) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx") TSoftObjectPtr<UNiagaraSystem> Muzzle;
	/** 탄 궤적·빔·번개 (총구 → User.BeamEnd) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx") TSoftObjectPtr<UNiagaraSystem> Beam;
	/** 맞은 자리 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx") TSoftObjectPtr<UNiagaraSystem> Impact;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx") FLinearColor ImpactColor = FLinearColor::White;
	/** 그 밖의 것 (열기 일렁임·충격파 등, 무기마다 코드가 정한 자리) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx") TSoftObjectPtr<UNiagaraSystem> Extra;

	/** 맞은 자리 바닥 자국 (데칼 재질) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx") TSoftObjectPtr<UMaterialInterface> ImpactDecal;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx", meta = (ClampMin = 0)) float DecalSize = 60.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx", meta = (ClampMin = 0)) float DecalLife = 2.f;

	/** 섬광 조명 (점광원, 0 = 없음) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx") FLinearColor FlashColor = FLinearColor(1.f, 0.8f, 0.5f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx", meta = (ClampMin = 0)) float FlashIntensity = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx", meta = (ClampMin = 0)) float FlashRadius = 400.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx", meta = (ClampMin = 0)) float FlashSeconds = 0.06f;
};

/**
 * 무기 10종의 모습을 한곳에 모은 표 (DA_VHWeaponCatalog).
 * 무기 번호 → 메시·아이콘·재질·손 보정·바닥 자세. 분류별 발광 색도 여기 있고 연출(공격 선·폭발·지뢰) 색이 이 값을 읽는다.
 * 판정 수치(대미지·사거리)는 서버 값이라 여기에 없다.
 */
UCLASS(BlueprintType)
class MCPTEST_API UVHWeaponCatalog : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UVHWeaponCatalog();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons", meta = (TitleProperty = "Id"))
	TArray<FVHWeaponVisual> Weapons;

	/** 소비 아이템 4종 (바닥 모델·아이콘·띠 색) [구상안 Docs/Design/Consumables_v1] */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Consumables", meta = (TitleProperty = "Id"))
	TArray<FVHConsumableVisual> Consumables;

	/** 소비 아이템 번호(서버 값)로 찾는다. 표에 없으면 nullptr */
	const FVHConsumableVisual* FindConsumable(uint8 ConsumableId) const;

	// ── 분류별 발광 색 [구상안 v2.2] ──
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glow") FLinearColor MeleeGlow;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glow") FLinearColor RangedGlow;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glow") FLinearColor ThrownGlow;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glow") FLinearColor PlacedGlow;

	/** 화염방사기 불꽃 색 (몸체 발광선만 원거리 색이고 불꽃은 주황) [구상안 05, 사용자 결정 2026-11-16] */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glow") FLinearColor FlameColor;

	// ── 표시 배율: 모델은 실제 크기로 만들고, 탑뷰에서 잘 보이게 키우는 정도를 여기서 정한다 ──

	/** 손에 든 무기 전체 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scale", meta = (ClampMin = 0.1))
	float HeldScale = 1.f;

	/** 바닥에 떨어진 무기 전체 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scale", meta = (ClampMin = 0.1))
	float FloorScale = 1.f;

	// ── 바닥의 탑뷰 그림 판 (FloorMode = 구상안 탑뷰 그림) ──

	/** 그림 판에 쓰는 재질 (텍스처 파라미터 Art, 숫자 파라미터 Brightness). 조명과 관계없이 그림 그대로 보인다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor Art")
	TSoftObjectPtr<UMaterialInterface> FloorArtMaterial;

	/** 그림 밝기 (1 = 그림 그대로. 맵 조명에 비해 어둡거나 밝아 보이면 고친다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor Art", meta = (ClampMin = 0))
	float FloorArtBrightness = 1.f;

	/** 그림 판을 바닥에서 띄우는 높이 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor Art", meta = (Units = "cm"))
	float FloorArtHeight = 6.f;

	// ── 무기 사용 이펙트 [이펙트 표 Docs/Design/무기_이펙트_표_2026-11-17.md] ──

	/** 판정 범위 도형(부채꼴·링·섬광)을 그리는 재질 M_VHFxShape */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx")
	TSoftObjectPtr<UMaterialInterface> FxShapeMaterial;

	/** 켜면 아래 나이아가라(엔진 기본 템플릿으로 만듦)를 쓰고, 끄거나 비어 있으면 도형 방식으로 그린다 (프레임이 떨어지면 끈다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx")
	bool bUseNiagara = true;

	/** 폭발 꾸밈(불꽃·조각·연기). 사용자 변수 User.Color, User.Radius(판정 반지름 cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx") TSoftObjectPtr<UNiagaraSystem> ExplosionNiagara;
	/** 연막 연기. User.Color, User.Radius */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx") TSoftObjectPtr<UNiagaraSystem> SmokeNiagara;
	/** 화염방사기 불꽃 줄기 (계속). User.Color, User.Radius(길이 cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx") TSoftObjectPtr<UNiagaraSystem> FlameNiagara;
	/** 피격 불꽃. User.Color, User.Radius */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx") TSoftObjectPtr<UNiagaraSystem> HitSparkNiagara;

	/** 무기별 이펙트 묶음 (궤적·총구·빔·피격·자국·섬광). 비어 있으면 위 공용 칸·도형 방식 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx")
	TMap<EVHWeaponId, FVHWeaponFxSet> WeaponFx;

	/**
	 * 레일건 충전 시간 (초) — 개발 명령 vh.WeaponTest 의 충전 연출에만 쓴다.
	 * 2026-11-22부터 실제 게임은 서버가 충전한다(누른 순간 SC_FIRE → 1초 충전 → 판정, 서버 GameData RailgunChargeMs). 클라는 그 시각에 맞춰 연출하므로 이 값은 바꿔도 게임에 영향 없음
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fx|Railgun", meta = (ClampMin = 0, ClampMax = 3, Units = "s"))
	float RailgunChargeSeconds = 1.f;

	// ── 게임 중 마우스 조준점 (2026-11-25). 무기별 값은 Weapons 각 칸의 Crosshair ──

	/** 무기가 없을 때(맨손) 조준점 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crosshair")
	FVHCrosshairSpec UnarmedCrosshair;

	/** 사제 테이저건(도적1 스킬, Q로 든 상태) 조준점. 자동 = 번개, 사거리 3m(서버 SkillTaserRangeCm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crosshair")
	FVHCrosshairSpec SkillTaserCrosshair;

	/**
	 * 이 무기(또는 맨손 0, 사제 테이저건 bSkillTaser)의 조준점 설정을 자동 칸까지 채워서 돌려준다.
	 * 자동 값 = 서버 GameData.h 무기 표(사거리·반각)와 같은 표시용 값
	 */
	FVHCrosshairSpec ResolveCrosshair(uint8 WeaponId, bool bSkillTaser) const;

	/** 무기별 이펙트 묶음 (없으면 nullptr) */
	const FVHWeaponFxSet* FindFx(uint8 WeaponId) const { return WeaponFx.Find(static_cast<EVHWeaponId>(WeaponId)); }

	/** 이 무기의 이펙트 색: 분류 색, 화염방사기는 불꽃 색 */
	FLinearColor FxColor(uint8 WeaponId) const;

	/** 무기 번호(서버 값)로 찾는다. 표에 없으면 nullptr */
	const FVHWeaponVisual* Find(uint8 WeaponId) const;
	FLinearColor CategoryColor(EVHWeaponCategory Category) const;
	/** 이 무기의 분류 발광 색. 표에 없는 번호면 Fallback */
	FLinearColor GlowColor(uint8 WeaponId, const FLinearColor& Fallback = FLinearColor::White) const;
	/** 무기 번호의 분류 (표에 없어도 번호로 정한다) */
	static EVHWeaponCategory CategoryOf(uint8 WeaponId);
	/**
	 * 서버의 공격 간격(초). 서버 Content/GameData.h 의 WeaponTypes.intervalMs 와 같은 값 (레일건은 발사 잠금 1초).
	 * 무기 동작이 이보다 길면 빨리 재생해서 다음 공격과 겹치지 않게 한다. 서버 값이 바뀌면 여기를 고친다
	 */
	static float ServerIntervalSeconds(uint8 WeaponId);
};
