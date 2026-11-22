#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VHSoundConfig.generated.h"

class USoundBase;
class USoundAttenuation;

/** 효과음 종류 (어디서 나는지는 Docs/효과음_목록.md) */
UENUM(BlueprintType)
enum class EVHSound : uint8
{
	None				UMETA(Hidden),
	// 무기 (SC_FIRE: 쏜 사람 위치)
	SwingBaton			UMETA(DisplayName = "휘두르기: 삼단봉"),
	SwingSword			UMETA(DisplayName = "휘두르기: 도검"),
	SwingBeam			UMETA(DisplayName = "휘두르기: 빔 소드"),
	FirePistol			UMETA(DisplayName = "사격: 권총"),
	FireFlame			UMETA(DisplayName = "사격: 화염방사기 (0.1초마다)"),
	FireRailgun			UMETA(DisplayName = "사격: 레일건"),
	FireTaser			UMETA(DisplayName = "사격: 테이저건·사제 테이저"),
	ThrowGrenade		UMETA(DisplayName = "던지기: 수류탄·섬광탄·끈끈이"),
	PlaceMine			UMETA(DisplayName = "설치: 지뢰"),
	GuardGun			UMETA(DisplayName = "사격: 경비원 기관총"),
	// 폭발·상태
	Explosion			UMETA(DisplayName = "폭발: 수류탄·폭발 지뢰"),
	ElectricShock		UMETA(DisplayName = "감전: 감전 지뢰·경직 시작"),
	Flashbang			UMETA(DisplayName = "섬광탄 터짐"),
	Smoke				UMETA(DisplayName = "연막 퍼짐"),
	// 몸
	Hit					UMETA(DisplayName = "맞음"),
	Downed				UMETA(DisplayName = "쓰러짐"),
	Footstep			UMETA(DisplayName = "발소리 (걸음마다)"),
	Roll				UMETA(DisplayName = "구르기"),
	// 상호작용
	SafeDial			UMETA(DisplayName = "금고 다이얼·작업 중 딸깍 (반복)"),
	SafeOpen			UMETA(DisplayName = "금고 열림"),
	BoxOpen				UMETA(DisplayName = "상자 열림(부서짐)"),
	PickUp				UMETA(DisplayName = "줍기"),
	Return				UMETA(DisplayName = "반납 성공"),
	Heal				UMETA(DisplayName = "회복 키트"),
	// 화면
	UIClick				UMETA(DisplayName = "UI 버튼"),
	// ── 2026-11-19 무기별 소리 (무기_연출_계획_2026-11-19.md) ──
	HitBaton			UMETA(DisplayName = "맞음: 삼단봉 (둔탁한 퍽)"),
	HitSword			UMETA(DisplayName = "맞음: 도검 (베는 금속음)"),
	HitBeam				UMETA(DisplayName = "맞음: 빔 소드 (지직)"),
	HitPistol			UMETA(DisplayName = "맞음: 권총"),
	HitRailgun			UMETA(DisplayName = "맞음: 레일건"),
	HitTaser			UMETA(DisplayName = "맞음: 테이저 (지지직)"),
	BeamHum				UMETA(DisplayName = "빔 소드 들고 있음 (반복)"),
	FlameStart			UMETA(DisplayName = "화염방사기 시작"),
	FlameLoop			UMETA(DisplayName = "화염방사기 누르는 동안 (반복)"),
	FlameEnd			UMETA(DisplayName = "화염방사기 끝"),
	RailgunDischarge	UMETA(DisplayName = "레일건 방전 (빔 1초 동안)"),
	GrenadePin			UMETA(DisplayName = "수류탄 핀 뽑기"),
	GrenadeFlight		UMETA(DisplayName = "수류탄 날아가는 소리"),
	MineWarn			UMETA(DisplayName = "지뢰 감지 경고 (0.5초)"),
	ShockMineHum		UMETA(DisplayName = "감전 지뢰 작동 중 (반복)"),
	BlastMineExplosion	UMETA(DisplayName = "폭발 지뢰 폭발"),
	StunLoop			UMETA(DisplayName = "경직 중 지지직 (반복)"),
	// ── 2026-11-20 ──
	RailgunCharge		UMETA(DisplayName = "레일건 충전 (1초, 클릭 → 발사 전)"),
	// ── 2026-11-21 방 입구 문 (보이는 연출만) ──
	DoorOpen			UMETA(DisplayName = "문 열림 (미닫이)"),
	DoorClose			UMETA(DisplayName = "문 닫힘"),
	Count				UMETA(Hidden),
};

/** 함께 겹쳐 내는 소리 한 겹 (여러 개면 무작위 하나) */
USTRUCT(BlueprintType)
struct FVHSoundLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	TArray<TSoftObjectPtr<USoundBase>> Sounds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = 0))
	float Volume = 1.f;

	/** 칸의 높낮이에 곱한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = 0.1))
	float PitchScale = 1.f;

	/** 칸 소리보다 늦게 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = 0, Units = "s"))
	float Delay = 0.f;

	/**
	 * 높낮이 미끄러짐: 0 이 아니면 재생하는 동안 높낮이 배율이 PitchScale → 이 값으로 바뀐다 (소리 파일은 그대로).
	 * 예: 레일건 방전음 1.6 → 0.7 을 1초 동안 = "위이잉↓" (2026-11-20)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = 0))
	float PitchEndScale = 0.f;

	/** 높낮이가 바뀌는 시간 (초). 처음에 빨리, 끝에 천천히 내려간다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = 0.05, Units = "s"))
	float PitchGlideSeconds = 1.f;
};

/** 효과음 칸 하나: 여러 개를 넣으면 무작위로 하나 (같은 소리 반복 피로를 줄인다) */
USTRUCT(BlueprintType)
struct FVHSoundSlot
{
	GENERATED_BODY()

	/** 소리 파일 (Sound Wave / Sound Cue). 비어 있으면 아무 소리도 나지 않는다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	TArray<TSoftObjectPtr<USoundBase>> Sounds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = 0))
	float Volume = 1.f;

	/** 재생할 때마다 이 범위에서 높낮이를 바꾼다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = 0.1))
	float PitchMin = 0.95f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = 0.1))
	float PitchMax = 1.05f;

	/** 같은 소리를 이 시간(초) 안에 다시 내지 않는다 (같은 위치 근처 기준). 0 = 제한 없음 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = 0, Units = "s"))
	float MinInterval = 0.f;

	/** 켜면 거리와 상관없이 들린다 (UI·내 반납 같은 알림) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	bool b2D = false;

	/** 같이 겹쳐 내는 소리 (예: 레일건 "쾅" + 저음 폭발) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	TArray<FVHSoundLayer> Layers;

	/** 이 칸만 다른 거리 감쇠 (비우면 표 공통 감쇠). 예: 감전 지뢰 웅웅은 가까이서만 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	TSoftObjectPtr<USoundAttenuation> AttenuationOverride;
};

/**
 * 효과음 표 (DA_VHSoundConfig). 칸만 채우면 게임 코드가 알맞은 순간에 재생한다.
 * 출처·라이선스: Docs/효과음_목록.md (CC0 만 쓴다)
 */
UCLASS(BlueprintType)
class MCPTEST_API UVHSoundConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 종류 → 소리 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	TMap<EVHSound, FVHSoundSlot> Sounds;

	/** 거리 감쇠 (3D 소리 공통). 비우면 감쇠 없음 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	TSoftObjectPtr<USoundAttenuation> Attenuation;

	/** 전체 음량 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = 0))
	float MasterVolume = 1.f;

	/** 그 자리에서 소리를 낸다 (칸이 비었으면 아무것도 안 함). b2D 칸이면 위치 무시 */
	static void Play(const UObject* WorldContext, EVHSound Sound, const FVector& Location);

	/** 칸이 채워져 있나 */
	static bool Has(EVHSound Sound);

	/**
	 * 반복 소리를 붙여서 낸다 (빔 소드 웅·화염방사기·감전 지뢰). 칸의 첫 소리 파일은 Looping 이어야 한다(setup_sounds.py 가 켠다).
	 * 끌 때는 반환된 컴포넌트의 FadeOut. Attach 가 없어지면 같이 꺼진다. 칸이 비었으면 nullptr
	 */
	static class UAudioComponent* PlayLoop(const UObject* WorldContext, EVHSound Sound, USceneComponent* Attach);

	/**
	 * 칸의 소리와 겹을 모두 Attach 에 붙여서 내고 소리 장치를 돌려준다 (2026-11-20, 레일건 충전처럼 중간에 끊어야 하는 소리).
	 * 끊을 때는 반환된 컴포넌트마다 FadeOut. 높낮이 미끄러짐 겹도 된다. 늦게 내는 겹(Delay)은 끊기 전에 시작하지 않았으면 내지 않는다
	 */
	static TArray<class UAudioComponent*> PlayAttached(const UObject* WorldContext, EVHSound Sound, USceneComponent* Attach);

	/** 무기 공격 한 번(SC_FIRE)의 소리. 화염방사기는 None (누르는 동안 반복 소리로 낸다) */
	static EVHSound FireSound(uint8 WeaponId);
	/** 이 무기에 맞았을 때 소리 (무기별 칸이 비면 공통 Hit) */
	static EVHSound HitSound(uint8 WeaponId);

	/** 개발용(vh.Sound): 재생할 때마다 로그 */
	static bool bLogPlays;
};
