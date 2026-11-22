#include "Gameplay/VHCharacterDevLibrary.h"

#include "Animation/Skeleton.h"

int32 UVHCharacterDevLibrary::SetSharedSkeletonRetargeting(USkeleton* Skeleton, FName RootBone)
{
	if (!Skeleton)
	{
		return 0;
	}
	const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
	int32 Changed = 0;
	Skeleton->Modify();
	for (int32 i = 0; i < Ref.GetRawBoneNum(); ++i)
	{
		const EBoneTranslationRetargetingMode::Type Want = Ref.GetBoneName(i) == RootBone
			? EBoneTranslationRetargetingMode::AnimationScaled
			: EBoneTranslationRetargetingMode::Skeleton;
		if (Skeleton->GetBoneTranslationRetargetingMode(i) != Want)
		{
			Skeleton->SetBoneTranslationRetargetingMode(i, Want, false);
			++Changed;
		}
	}
	if (Changed > 0)
	{
		Skeleton->MarkPackageDirty();
	}
	return Changed;
}

FString UVHCharacterDevLibrary::DescribeRetargeting(USkeleton* Skeleton)
{
	if (!Skeleton)
	{
		return TEXT("(없음)");
	}
	const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
	TArray<FString> Parts;
	for (int32 i = 0; i < Ref.GetRawBoneNum(); ++i)
	{
		const UEnum* Enum = StaticEnum<EBoneTranslationRetargetingMode::Type>();
		Parts.Add(FString::Printf(TEXT("%s=%s"), *Ref.GetBoneName(i).ToString(),
			Enum ? *Enum->GetNameStringByValue(Skeleton->GetBoneTranslationRetargetingMode(i)) : TEXT("?")));
	}
	return FString::Join(Parts, TEXT(", "));
}
