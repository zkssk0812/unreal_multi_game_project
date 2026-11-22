#pragma once

#include "CoreMinimal.h"

/**
 * 순위 계산 [PvP 규칙 v1 2장: 제한 시간이 끝나면 반납 금액이 가장 많은 사람이 승리].
 * 동점은 [미정] → 사용자 결정 2026-11-06 "공동 순위(임시)": 같은 금액이면 같은 등수, 다음 등수는 건너뛴다(1, 1, 3).
 * 공동 1위는 모두 승리. HUD 순위표·결과창·Mock 결과가 모두 이 함수 하나를 쓴다.
 */
namespace VHRanking
{
	/** Values 각 항목의 등수(1부터, 큰 값이 1등). 입력 순서 그대로 돌려준다. 예: {500, 900, 900} → {3, 1, 1} */
	inline TArray<int32> CompetitionRanks(const TArray<int32>& Values)
	{
		TArray<int32> Ranks;
		Ranks.SetNumZeroed(Values.Num());
		for (int32 i = 0; i < Values.Num(); ++i)
		{
			int32 Higher = 0;
			for (int32 j = 0; j < Values.Num(); ++j)
			{
				if (Values[j] > Values[i])
				{
					++Higher;
				}
			}
			Ranks[i] = Higher + 1;
		}
		return Ranks;
	}

	/** Ranks 안에 Rank 등이 몇 명인지 (2명 이상이면 공동 순위) */
	inline int32 CountRank(const TArray<int32>& Ranks, int32 Rank)
	{
		int32 Count = 0;
		for (int32 R : Ranks)
		{
			Count += R == Rank ? 1 : 0;
		}
		return Count;
	}

	/** 순위표 한 줄의 등수 글자: "1" / 공동이면 "공동 1" */
	inline FString RankLabel(int32 Rank, bool bTied)
	{
		return bTied ? FString::Printf(TEXT("공동 %d"), Rank) : FString::FromInt(Rank);
	}

	/** "$1,200" */
	inline FString Money(int32 Value)
	{
		return FString::Printf(TEXT("$%s"), *FText::AsNumber(Value).ToString());
	}
}
