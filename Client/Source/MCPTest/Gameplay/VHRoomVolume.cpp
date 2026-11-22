#include "Gameplay/VHRoomVolume.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AVHRoomVolume::AVHRoomVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	Area = CreateDefaultSubobject<UBoxComponent>(TEXT("Area"));
	RootComponent = Area;
	Area->SetBoxExtent(FVector(500.f, 500.f, 150.f));
	Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Area->SetHiddenInGame(true);
	Area->ShapeColor = FColor(80, 200, 255);
	Area->SetLineThickness(4.f);
}

FBox AVHRoomVolume::GetRoomBox() const
{
	return Area->Bounds.GetBox();
}

void AVHRoomVolume::GetRoomRects(TArray<FBox2D>& OutRects) const
{
	OutRects.Reset();
	if (Rects.Num() > 0)
	{
		OutRects = Rects;
		return;
	}
	const FBox Box = GetRoomBox();
	OutRects.Add(FBox2D(FVector2D(Box.Min.X, Box.Min.Y), FVector2D(Box.Max.X, Box.Max.Y)));
}

bool AVHRoomVolume::ContainsPoint(const FVector& Point, float Slack) const
{
	if (Rects.Num() > 0)
	{
		for (const FBox2D& R : Rects)
		{
			if (Point.X >= R.Min.X - Slack && Point.X <= R.Max.X + Slack && Point.Y >= R.Min.Y - Slack && Point.Y <= R.Max.Y + Slack)
			{
				return true;
			}
		}
		return false;
	}
	const FBox Box = GetRoomBox();
	return Point.X >= Box.Min.X - Slack && Point.X <= Box.Max.X + Slack && Point.Y >= Box.Min.Y - Slack && Point.Y <= Box.Max.Y + Slack;
}

FString AVHRoomVolume::GetRoomLabel() const
{
	return RoomName.IsEmpty() ? GetActorNameOrLabel() : RoomName.ToString();
}

bool AVHRoomVolume::FindRandomFloorPoint(float HalfSize, float WallMargin, FVector& OutPoint, int32 Tries) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const FBox Box = GetRoomBox();
	const float Inset = HalfSize + WallMargin;
	const float MinX = Box.Min.X + Inset, MaxX = Box.Max.X - Inset;
	const float MinY = Box.Min.Y + Inset, MaxY = Box.Max.Y - Inset;
	if (MinX > MaxX || MinY > MaxY)
	{
		return false;
	}
	FCollisionQueryParams Params(TEXT("VHRoomFloor"), false);
	const FCollisionObjectQueryParams Blockers(FCollisionObjectQueryParams::AllObjects);
	for (int32 i = 0; i < Tries; ++i)
	{
		const FVector Top(FMath::FRandRange(MinX, MaxX), FMath::FRandRange(MinY, MaxY), Box.Max.Z);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Top, Top - FVector(0.f, 0.f, Box.GetSize().Z + 400.f), ECC_Visibility, Params))
		{
			continue;
		}
		if (Hit.ImpactNormal.Z < 0.8f || Hit.ImpactPoint.Z > Box.Min.Z + 60.f)
		{
			continue;	// 바닥이 아니다(금고·가구 윗면 등)
		}
		// 그 자리(1m 정사각형 × 키 높이)에 막는 것이 없어야 한다
		const FVector Probe = Hit.ImpactPoint + FVector(0.f, 0.f, 100.f);
		if (World->OverlapAnyTestByObjectType(Probe, FQuat::Identity, Blockers,
			FCollisionShape::MakeBox(FVector(HalfSize + 10.f, HalfSize + 10.f, 90.f)), Params))
		{
			continue;
		}
		OutPoint = Hit.ImpactPoint;
		return true;
	}
	return false;
}

AVHRoomVolume* AVHRoomVolume::FindRoomAt(const UWorld* World, const FVector& Point)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AVHRoomVolume> It(World); It; ++It)
	{
		if (It->ContainsPoint(Point))
		{
			return *It;
		}
	}
	// 서버 맵의 방은 벽·가구에서 25cm 떨어진 칸만 담는다. 문턱이나 벽에 붙어 선 자리는 가까운 방으로 본다
	for (TActorIterator<AVHRoomVolume> It(World); It; ++It)
	{
		if (It->Rects.Num() > 0 && It->ContainsPoint(Point, 40.f))
		{
			return *It;
		}
	}
	return nullptr;
}
