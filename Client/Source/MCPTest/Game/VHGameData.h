#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Network/VHNetTypes.h"
#include "VHGameData.generated.h"

class UTexture2D;

/** 캐릭터 한 명의 표시 정보. Id는 서버의 character_id와 같아야 한다 */
USTRUCT(BlueprintType)
struct FVHCharacterDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Id = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (MultiLine = true)) FText Description;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UTexture2D> Portrait;
};

/** 퍽(특성) 하나의 표시 정보. Id는 서버의 perk id와 같아야 한다 */
USTRUCT(BlueprintType)
struct FVHPerkDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Id = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (MultiLine = true)) FText Description;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UTexture2D> Icon;
};

USTRUCT(BlueprintType)
struct FVHMapDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Id = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
};

/**
 * 캐릭터·퍽·맵 목록 (기획 미정 → 임시 데이터).
 * 이름·설명·이미지는 DA_VHGameData 에셋에서 바꾸면 되고 코드는 고칠 필요 없다.
 * Id 값은 서버 팀원과 맞춰야 한다.
 */
UCLASS(BlueprintType)
class UVHGameData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UVHGameData();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Characters")
	TArray<FVHCharacterDef> Characters;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perks")
	TArray<FVHPerkDef> Perks;

	/** 1인당 최대 퍽 수 (프로토콜 MAX_PERKS = 3) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perks")
	int32 MaxPerks = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maps")
	TArray<FVHMapDef> Maps;

	/** 프로토콜 mode_id (가정: 1 = VaultHeist) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maps")
	int32 ModeId = 1;

	/** 빠른 찾기로 방을 새로 만들 때 쓰는 기본값 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maps")
	int32 DefaultMapId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maps")
	EVHDifficulty DefaultDifficulty = EVHDifficulty::Normal;

	const FVHCharacterDef* FindCharacter(int32 Id) const;
	const FVHPerkDef* FindPerk(int32 Id) const;
	FText CharacterName(int32 Id) const;
	FText PerkName(int32 Id) const;
	FText MapName(int32 Id) const;
};
