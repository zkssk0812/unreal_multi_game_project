#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VHCharacterDevLibrary.generated.h"

class USkeleton;

/**
 * 캐릭터 에셋 손질용 함수 (에디터 파이썬 스크립트 Tools/CharacterBuild/setup_character_data.py 가 부른다).
 * 파이썬에는 스켈레톤의 "뼈 이동 리타게팅 모드" 쓰기 함수가 없어서 여기서 연다.
 */
UCLASS()
class MCPTEST_API UVHCharacterDevLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 스켈레톤 하나를 여러 캐릭터 메시가 함께 쓸 때의 표준 설정:
	 * 뿌리 뼈(RootBone, 보통 Hips) = AnimationScaled(골반 높이 비율로 이동), 나머지 뼈 = Skeleton(각 메시 자기 뼈 길이).
	 * → 팔·다리 길이가 다른 캐릭터도 자기 비율 그대로 같은 애니메이션을 쓴다. 바꾼 뼈 수를 돌려준다(저장은 따로).
	 */
	UFUNCTION(BlueprintCallable, Category = "VaultHeist|Dev")
	static int32 SetSharedSkeletonRetargeting(USkeleton* Skeleton, FName RootBone);

	/** 뼈별 이동 리타게팅 모드를 글자로 (확인용): "Hips=AnimationScaled, LeftUpLeg=Skeleton, ..." */
	UFUNCTION(BlueprintCallable, Category = "VaultHeist|Dev")
	static FString DescribeRetargeting(USkeleton* Skeleton);
};
