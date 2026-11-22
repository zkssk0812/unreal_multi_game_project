#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VHSettings.generated.h"

class UVHUIStyle;
class UVHGameData;
class UVHInteractionConfig;
class UVHGameplayConfig;
class UVHWeaponCatalog;
class UVHSoundConfig;
class UVHCharacterVisuals;

/**
 * 「털어라! 금고!」 프로젝트 설정.
 * 에디터: 편집 → 프로젝트 세팅 → 게임 → "털어라 금고" 에서 바꿀 수 있고, Config/DefaultGame.ini에 저장된다.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "털어라 금고"))
class UVHSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UVHSettings();

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	// ── 네트워크 ──

	/** true면 진짜 서버 대신 인프로세스 Mock 서버를 쓴다 (서버 기능이 구현되면 false로) */
	UPROPERTY(Config, EditAnywhere, Category = "Network")
	bool bUseMockServer = true;

	UPROPERTY(Config, EditAnywhere, Category = "Network")
	FString ServerHost = TEXT("127.0.0.1");

	UPROPERTY(Config, EditAnywhere, Category = "Network", meta = (ClampMin = 1, ClampMax = 65535))
	int32 ServerPort = 9000;

	UPROPERTY(Config, EditAnywhere, Category = "Network", meta = (ClampMin = 1))
	float ConnectTimeoutSeconds = 5.f;

	// ── Mock 서버 ──

	/** 서버 → 클라 바이트를 일부러 작게 쪼개 보내 TCP 재조립을 테스트한다 */
	UPROPERTY(Config, EditAnywhere, Category = "Mock")
	bool bMockSplitPackets = true;

	/** 결과 화면 유지 시간(ms). 지나면 Mock 서버가 자동으로 로비로 돌려보낸다 (게임 길이는 DA_VHGameplayConfig.TimeLimitSeconds) */
	UPROPERTY(Config, EditAnywhere, Category = "Mock", meta = (ClampMin = 3000))
	int32 MockResultScreenMs = 30000;

	/** 방을 만들면 들어오는 가짜 플레이어 수 */
	UPROPERTY(Config, EditAnywhere, Category = "Mock", meta = (ClampMin = 0, ClampMax = 2))
	int32 MockFakePlayers = 2;

	/** 가짜 플레이어가 한 단계(캐릭터·퍽·준비)씩 행동하는 간격(초) */
	UPROPERTY(Config, EditAnywhere, Category = "Mock", meta = (ClampMin = 0.1))
	float MockFakeActionDelaySeconds = 1.5f;

	// ── 콘텐츠 ──

	/** 메뉴(메인화면~결과창) 레벨 */
	UPROPERTY(Config, EditAnywhere, Category = "Content", meta = (AllowedClasses = "/Script/Engine.World"))
	FSoftObjectPath MenuLevel;

	/** SC_GAME_LOAD를 받으면 이동할 게임 레벨 (2026-10-26부터 L_Mansion_1F, Config/DefaultGame.ini에서 지정) */
	UPROPERTY(Config, EditAnywhere, Category = "Content", meta = (AllowedClasses = "/Script/Engine.World"))
	FSoftObjectPath GameLevel;

	/**
	 * GameLevel 이 맞춰진 서버 맵 버전 (SC_GAME_LOAD.map_version 과 다르면 경고).
	 *  - 3 = 옛 블록아웃 레벨 L_Mansion_1F (Docs/ServerHandoff/mansion_1f_server.json v3)
	 *  - 2 = 새 맵 L_Mansion_Shell (서버 커밋 1b53272, Mansion_1F_Collision 기준 — 서버가 붙인 번호가 2)
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Content", meta = (ClampMin = 0))
	int32 GameLevelMapVersion = 3;

	/** 게임 레벨. 실행 옵션 -VHLevel=/Game/경로/레벨이름 이 있으면 그 레벨 (맵을 바꾸기 전 시험용) */
	FSoftObjectPath GetGameLevel() const;

	/** 게임 레벨이 맞춰진 서버 맵 버전. 실행 옵션 -VHMapVersion=N 이 있으면 그 값 */
	uint32 GetGameLevelMapVersion() const;

	/** 게임 레벨이 새 맵(L_Mansion_Shell, Shell 원점 좌표)인가 — 가짜 서버가 샘플 위치를 고를 때 쓴다 */
	bool IsShellMap() const;

	/** UI 색·글자 크기 모음. 비워 두면 C++ 기본값 */
	UPROPERTY(Config, EditAnywhere, Category = "Content")
	TSoftObjectPtr<UVHUIStyle> UIStyle;

	/** 캐릭터·퍽·맵 표시용 데이터. 비워 두면 C++ 기본값 */
	UPROPERTY(Config, EditAnywhere, Category = "Content")
	TSoftObjectPtr<UVHGameData> GameData;

	/** 상호작용·소지·소리·손전등 수치. 비워 두면 C++ 기본값 */
	UPROPERTY(Config, EditAnywhere, Category = "Content")
	TSoftObjectPtr<UVHInteractionConfig> InteractionConfig;

	/** 게임 규칙·능력치·카메라 수치. 비워 두면 C++ 기본값 */
	UPROPERTY(Config, EditAnywhere, Category = "Content")
	TSoftObjectPtr<UVHGameplayConfig> GameplayConfig;

	/** 무기 10종의 모습(메시·아이콘·재질·손 보정)과 분류별 발광 색. 비워 두면 C++ 기본값(모델 없음) */
	UPROPERTY(Config, EditAnywhere, Category = "Content")
	TSoftObjectPtr<UVHWeaponCatalog> WeaponCatalog;

	/** 캐릭터 모습과 무기 드는 방법(소켓 이름·보정). 내 캐릭터와 다른 플레이어가 같이 본다. 비워 두면 C++ 기본값(마네킹) */
	UPROPERTY(Config, EditAnywhere, Category = "Content")
	TSoftObjectPtr<UVHCharacterVisuals> CharacterVisuals;

	/** 효과음 표 (종류별 소리 칸). 비워 두면 소리 없음 */
	UPROPERTY(Config, EditAnywhere, Category = "Content")
	TSoftObjectPtr<UVHSoundConfig> SoundConfig;

	static const UVHSettings& Get() { return *GetDefault<UVHSettings>(); }
	const UVHWeaponCatalog& GetWeaponCatalog() const;
	const UVHCharacterVisuals& GetCharacterVisuals() const;
	const UVHSoundConfig& GetSoundConfig() const;
	const UVHUIStyle& GetStyle() const;
	const UVHGameData& GetGameData() const;
	const UVHInteractionConfig& GetInteractionConfig() const;
	const UVHGameplayConfig& GetGameplayConfig() const;
};
