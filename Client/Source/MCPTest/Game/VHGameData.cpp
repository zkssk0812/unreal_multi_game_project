#include "Game/VHGameData.h"

UVHGameData::UVHGameData()
{
	// 서버 v3.0 기준 (Content/GameData.h): 서버는 캐릭터 1~5를 받지만 플레이어 캐릭터는 3명으로 확정(2026-11-20 사용자) → 도적1~3만. 스킬은 캐릭터에 고정, 퍽 1~7.
	// 이름·설명·이미지는 DA_VHGameData 에셋에서 바꾸면 된다. Id 는 서버 값과 같아야 한다.
	auto AddCharacter = [this](int32 Id, const TCHAR* Name, const TCHAR* Desc)
	{
		FVHCharacterDef& C = Characters.AddDefaulted_GetRef();
		C.Id = Id;
		C.DisplayName = FText::FromString(Name);
		C.Description = FText::FromString(Desc);
	};
	AddCharacter(1, TEXT("도적1"), TEXT("스킬(Q): 사제 테이저건 — 3m 안의 대상 하나를 2초 경직. 쿨타임 30초"));
	AddCharacter(2, TEXT("도적2"), TEXT("스킬(Q): 끈끈이 투척 — 1초 뒤 내 뒤 1m에 반경 3m 함정(10초). 위에 있으면 이동 속도 50% 감소(나 포함). 쿨타임 30초"));
	AddCharacter(3, TEXT("도적3"), TEXT("스킬(Q): 카멜레온 은신 — 20초 동안 안 보임(다시 누르면 해제). 이동 속도 80% 감소, 공격·상호작용·피격 시 해제. 쿨타임 30초"));

	auto AddPerk = [this](int32 Id, const TCHAR* Name, const TCHAR* Desc)
	{
		FVHPerkDef& P = Perks.AddDefaulted_GetRef();
		P.Id = Id;
		P.DisplayName = FText::FromString(Name);
		P.Description = FText::FromString(Desc);
	};
	AddPerk(1, TEXT("방탄조끼"), TEXT("보호막 20 (리스폰할 때마다 다시 채워짐)"));
	AddPerk(2, TEXT("기사회생"), TEXT("한 번 HP 1로 버팀 (리스폰할 때마다 다시 채워짐)"));
	AddPerk(3, TEXT("유용한 도구"), TEXT("작업 속도 10% 증가 (금고·상자)"));
	AddPerk(4, TEXT("운동화"), TEXT("달리기 속도 20% 증가"));
	AddPerk(5, TEXT("보물탐색"), TEXT("누군가 보물을 획득하면 미니맵에 방향 표시"));
	AddPerk(6, TEXT("로봇탐지"), TEXT("미니맵에 경비원 위치 표시"));
	AddPerk(7, TEXT("생체감지"), TEXT("미니맵에 다른 플레이어 위치 표시"));

	auto AddMap = [this](int32 Id, const TCHAR* Name)
	{
		FVHMapDef& M = Maps.AddDefaulted_GetRef();
		M.Id = Id;
		M.DisplayName = FText::FromString(Name);
	};
	AddMap(1, TEXT("바바라의 저택"));
	AddMap(2, TEXT("테스트 맵(임시)"));
}

const FVHCharacterDef* UVHGameData::FindCharacter(int32 Id) const
{
	return Characters.FindByPredicate([Id](const FVHCharacterDef& C) { return C.Id == Id; });
}

const FVHPerkDef* UVHGameData::FindPerk(int32 Id) const
{
	return Perks.FindByPredicate([Id](const FVHPerkDef& P) { return P.Id == Id; });
}

FText UVHGameData::CharacterName(int32 Id) const
{
	if (Id == 0)
	{
		return FText::FromString(TEXT("미선택"));
	}
	if (const FVHCharacterDef* C = FindCharacter(Id))
	{
		return C->DisplayName;
	}
	return FText::FromString(FString::Printf(TEXT("캐릭터 #%d"), Id));
}

FText UVHGameData::PerkName(int32 Id) const
{
	if (const FVHPerkDef* P = FindPerk(Id))
	{
		return P->DisplayName;
	}
	return FText::FromString(FString::Printf(TEXT("퍽 #%d"), Id));
}

FText UVHGameData::MapName(int32 Id) const
{
	for (const FVHMapDef& M : Maps)
	{
		if (M.Id == Id)
		{
			return M.DisplayName;
		}
	}
	return FText::FromString(FString::Printf(TEXT("맵 #%d"), Id));
}
