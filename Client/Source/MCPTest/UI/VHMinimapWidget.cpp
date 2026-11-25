#include "UI/VHMinimapWidget.h"

#include "UI/VHUIStyle.h"
#include "Gameplay/VHThiefStateComponent.h"
#include "Gameplay/VHInteractableBase.h"
#include "Gameplay/VHInteractionSubsystem.h"
#include "Gameplay/VHMatchSubsystem.h"
#include "Gameplay/VHServerGameSubsystem.h"
#include "Gameplay/VHNetProxy.h"
#include "Gameplay/VHReturnPoint.h"
#include "Gameplay/VHRoomVolume.h"
#include "Gameplay/VHDoor.h"
#include "Gameplay/VHLevelViewSettings.h"
#include "Gameplay/VHGameplayConfig.h"
#include "Network/VHSettings.h"

#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

using namespace RobProto;

void UVHMinimapWidget::CacheRooms() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// 화면 위쪽이 가리키는 월드 방향(탑뷰 카메라 Yaw)에 맞춰 미니맵도 같은 방향으로 그린다
	const float Yaw = FMath::DegreesToRadians(AVHLevelViewSettings::GetTopViewYaw(World));
	AxisUp = FVector2D(FMath::Cos(Yaw), FMath::Sin(Yaw));
	AxisRight = FVector2D(-FMath::Sin(Yaw), FMath::Cos(Yaw));

	Rooms.Reset();
	RoomCount = 0;
	FVector2D Min(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	FVector2D Max(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
	for (TActorIterator<AVHRoomVolume> It(World); It; ++It)
	{
		// 방 하나 = 직사각형 한 개(옛 레벨) 또는 여러 개(서버 맵과 맞춘 레벨: 복도처럼 꺾인 방)
		TArray<FBox2D> Pieces;
		It->GetRoomRects(Pieces);
		for (const FBox2D& Piece : Pieces)
		{
			FRoomRect Rect;
			Rect.Min = FVector2D(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
			Rect.Max = FVector2D(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				const FVector2D P((Corner & 1) ? Piece.Max.X : Piece.Min.X, (Corner & 2) ? Piece.Max.Y : Piece.Min.Y);
				const FVector2D UV(FVector2D::DotProduct(P, AxisRight), FVector2D::DotProduct(P, AxisUp));
				Rect.Min = FVector2D::Min(Rect.Min, UV);
				Rect.Max = FVector2D::Max(Rect.Max, UV);
			}
			Rect.bPiece = Pieces.Num() > 1;
			Rect.bCorridor = !It->bAllowReturnPoint;
			Rooms.Add(Rect);
			Min = FVector2D::Min(Min, Rect.Min);
			Max = FVector2D::Max(Max, Rect.Max);
		}
		++RoomCount;
	}
	if (Rooms.Num() > 0)
	{
		MapMin = Min;
		MapMax = Max;
	}
	// 문 자리: 문 가운데에서 벽을 따라(문의 로컬 Y) 폭만큼
	Doors.Reset();
	for (TActorIterator<AVHDoor> It(World); It; ++It)
	{
		const FVector C = It->GetActorLocation();
		const FVector R = It->GetActorRightVector() * (It->Width * 0.5f);
		auto ToUV = [this](const FVector& P) { const FVector2D XY(P.X, P.Y); return FVector2D(FVector2D::DotProduct(XY, AxisRight), FVector2D::DotProduct(XY, AxisUp)); };
		FDoorMark& D = Doors.AddDefaulted_GetRef();
		D.A = ToUV(C - R);
		D.B = ToUV(C + R);
		D.bLocked = It->bLocked;
		D.Door = *It;
	}
	bRoomsCached = World->HasBegunPlay();
}

int32 UVHMinimapWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const UWorld* World = GetWorld();
	const UVHServerGameSubsystem* Game = World ? World->GetSubsystem<UVHServerGameSubsystem>() : nullptr;
	const UVHMatchSubsystem* Match = World ? World->GetSubsystem<UVHMatchSubsystem>() : nullptr;
	const UVHUIStyle& S = UVHSettings::Get().GetStyle();
	const UVHGameplayConfig& G = UVHSettings::Get().GetGameplayConfig();
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("GenericWhiteBox");
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	FDrawStats Stats;

	auto Box = [&](const FVector2D& Pos, const FVector2D& BoxSize, const FLinearColor& Color, int32 Layer)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(FVector2f(BoxSize), FSlateLayoutTransform(FVector2f(Pos))),
			White, ESlateDrawEffect::None, Color);
	};
	auto Line = [&](const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Thickness, int32 Layer)
	{
		TArray<FVector2f> Points;
		Points.Add(FVector2f(A));
		Points.Add(FVector2f(B));
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Thickness);
	};

	// 바탕
	Box(FVector2D::ZeroVector, Size, S.MinimapBack, LayerId);

	// 섬광에 맞으면 3초 동안 미니맵이 사라진다 [기획 I-33]
	if (Game && Game->IsActive() && Game->IsFlashed())
	{
		Stats.bHiddenByFlash = true;
		LastStats = Stats;
		return LayerId + 1;
	}

	if (!bRoomsCached)
	{
		CacheRooms();
	}
	// 맵 범위를 위젯 안에 비율을 지켜 맞춘다
	const float Pad = 8.f;
	const FVector2D MapSize = FVector2D(FMath::Max(1.0, MapMax.X - MapMin.X), FMath::Max(1.0, MapMax.Y - MapMin.Y));
	const double Scale = FMath::Min((Size.X - Pad * 2.f) / MapSize.X, (Size.Y - Pad * 2.f) / MapSize.Y);
	const FVector2D Origin = (Size - MapSize * Scale) * 0.5;
	auto UVToLocal = [&](const FVector2D& UV) -> FVector2D
	{
		return FVector2D(Origin.X + (UV.X - MapMin.X) * Scale, Origin.Y + (MapMax.Y - UV.Y) * Scale);	// 화면은 아래가 +Y
	};
	auto WorldToLocal = [&](const FVector& P) -> FVector2D
	{
		const FVector2D XY(P.X, P.Y);
		return UVToLocal(FVector2D(FVector2D::DotProduct(XY, AxisRight), FVector2D::DotProduct(XY, AxisUp)));
	};
	auto Dot = [&](const FVector& WorldPos, float Half, const FLinearColor& Color, int32 Layer)
	{
		const FVector2D C = WorldToLocal(WorldPos);
		Box(C - FVector2D(Half, Half), FVector2D(Half * 2.f, Half * 2.f), Color, Layer);
	};

	// 방 (단순 평면도)
	for (const FRoomRect& Room : Rooms)
	{
		const FVector2D A = UVToLocal(FVector2D(Room.Min.X, Room.Max.Y));
		const FVector2D B = UVToLocal(FVector2D(Room.Max.X, Room.Min.Y));
		// 방이 상자 하나면 가장자리를 조금 줄여 방 사이가 벽처럼 보이게 한다. 조각 여러 개면 조각끼리 붙어야 하므로 그대로 그린다
		// (서버 맵의 방은 벽 두께만큼 이미 떨어져 있다). 복도는 조금 어둡게
		const double Inset = Room.bPiece ? 0.0 : 0.5;
		const FLinearColor Color = Room.bCorridor ? S.MinimapRoom * FLinearColor(0.6f, 0.6f, 0.6f, 1.f) : S.MinimapRoom;
		Box(A + FVector2D(Inset, Inset), (B - A) - FVector2D(Inset * 2.0, Inset * 2.0), Color, LayerId + 1);
	}
	Stats.Rooms = RoomCount;

	// 문 자리 [사용자 요청 2026-11-21 (5)]: 방 사이 벽 틈 위에 밝은 선분. 잠긴 바깥 문은 빨강
	for (const FDoorMark& D : Doors)
	{
		// 잠긴 문 = 빨강, 나머지 = 한 가지 색 [기획 확인 2026-11-25: 열림/닫힘 구분 없음 — 미니맵으로 누가 문을 열었는지 알 수 없게]
		const bool bOpenDoor = !D.bLocked && D.Door.IsValid() && D.Door->GetOpenAlpha() > 0.5f;
		const FLinearColor Color = D.bLocked ? G.MinimapDoorLockedColor : G.MinimapDoorColor;
		Stats.OpenDoors += bOpenDoor ? 1 : 0;	// 시험 로그용 (그리는 색과 무관)
		// 미니맵 축척에서 문 폭(2m)은 2px 남짓이라 점처럼 보인다 → 선분을 굵기의 2.5배 이상으로 늘린다
		FVector2D A = UVToLocal(D.A), B = UVToLocal(D.B);
		const FVector2D Mid = (A + B) * 0.5, Dir = (B - A).GetSafeNormal();
		const double Half = FMath::Max((B - A).Size() * 0.5, G.MinimapDoorThickness * 1.25);
		A = Mid - Dir * Half;
		B = Mid + Dir * Half;
		Line(A, B, FLinearColor(0.f, 0.f, 0.f, 0.6f), G.MinimapDoorThickness + 2.f, LayerId + 1);
		Line(A, B, Color, G.MinimapDoorThickness, LayerId + 2);
		++Stats.Doors;
		Stats.LockedDoors += D.bLocked ? 1 : 0;
	}

	// 반납지점: 위치 + 남은 시간(초). 곧 사라지면 빨강 [기획 O-25, U-04]
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 8);
	if (Match)
	{
		for (const AVHReturnPoint* Point : Match->GetReturnPoints())
		{
			const FLinearColor Color = Point->IsRed() ? G.ReturnPointRed : G.ReturnPointGreen;
			const FVector2D C = WorldToLocal(Point->GetActorLocation());
			Box(C - FVector2D(4.0, 4.0), FVector2D(8.0, 8.0), Color, LayerId + 2);
			const FString Seconds = FString::FromInt(FMath::CeilToInt(Point->GetRemainingSeconds()));
			FSlateDrawElement::MakeText(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(FVector2f(30.f, 12.f), FSlateLayoutTransform(FVector2f(C + FVector2D(5.0, -7.0)))),
				Seconds, Font, ESlateDrawEffect::None, FLinearColor::White);
			++Stats.ReturnPoints;
		}
	}

	// 내가 쓰러지며 떨어뜨린 보물: 바닥에 있는 동안 보라색 ◇ (누가 주우면 사라짐). 퍽과 상관없이 내 것이라 보인다
	FVector DroppedPos;
	int32 DroppedValue = 0;
	if (Game && Game->IsActive() && Game->GetMyDroppedTreasure(DroppedPos, DroppedValue))
	{
		const FVector2D C = WorldToLocal(DroppedPos);
		const double H = S.MinimapDroppedTreasureSize * (0.85 + 0.15 * FMath::Sin(FPlatformTime::Seconds() * 5.0));
		TArray<FVector2f> P = { FVector2f(C + FVector2D(0, -H)), FVector2f(C + FVector2D(H, 0)), FVector2f(C + FVector2D(0, H)), FVector2f(C + FVector2D(-H, 0)), FVector2f(C + FVector2D(0, -H)) };
		// 내 표시(+5·+6)보다 위에 그린다: 쓰러져 있는 동안은 보물이 내 바로 밑에 있어 가려졌다
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 7, AllottedGeometry.ToPaintGeometry(), P, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.7f), true, 5.f);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 8, AllottedGeometry.ToPaintGeometry(), P, ESlateDrawEffect::None, S.MinimapTreasure, true, 2.5f);
		Stats.bMyTreasure = true;
	}

	const APawn* Pawn = GetOwningPlayerPawn();
	if (Game && Game->IsActive())
	{
		// 퍽이 있을 때만 보이는 정보. 서버는 모든 위치를 보내므로 여기서 숨긴다.
		// 연막 안·은신·쓰러짐·접속 끊김인 대상은 퍽이 있어도 표시하지 않는다 [기획 I-32: 연막 안은 미니맵에 표기되지 않는다]
		const bool bRobotSense = Game->HasMyPerk(VHPerk::RobotSense);
		const bool bBioSense = Game->HasMyPerk(VHPerk::BioSense);
		for (const TPair<uint16, UVHServerGameSubsystem::FEntity>& Pair : Game->GetEntities())
		{
			const UVHServerGameSubsystem::FEntity& E = Pair.Value;
			if (E.Id == Game->GetSelfEntityId())
			{
				continue;
			}
			const uint32 Hidden = ES_IN_SMOKE | ES_DOWNED | ES_LEFT_GAME | ES_CLOAKED;
			if (E.Kind == EntityKind::Guard && bRobotSense && !(E.State & ES_IN_SMOKE))
			{
				Dot(E.Pos, 3.5f, S.MinimapGuard, LayerId + 4);
				++Stats.Guards;
			}
			else if (E.Kind == EntityKind::Thief && bBioSense && !(E.State & Hidden))
			{
				Dot(E.Pos, 3.5f, S.MinimapThief, LayerId + 4);
				++Stats.Thieves;
			}
		}
	}

	// 나: 위치 + 시선 방향
	if (Pawn)
	{
		const FVector2D Me = WorldToLocal(Pawn->GetActorLocation());
		const FVector Look = Pawn->GetActorForwardVector();
		const FVector2D LookXY(Look.X, Look.Y);
		const FVector2D LookLocal = FVector2D(FVector2D::DotProduct(LookXY, AxisRight), -FVector2D::DotProduct(LookXY, AxisUp)).GetSafeNormal();
		Line(Me, Me + LookLocal * 10.0, S.MinimapMe, 2.f, LayerId + 5);
		Box(Me - FVector2D(4.0, 4.0), FVector2D(8.0, 8.0), S.MinimapMe, LayerId + 6);

		// 보물탐색 퍽: 누군가 보물을 얻은 "방향"만 (위치는 아님) [기획 K-05]
		FVector TreasurePos;
		if (Game && Game->GetTreasureSense(TreasurePos))
		{
			const FVector2D To = WorldToLocal(TreasurePos) - Me;
			if (To.SizeSquared() > 1.0)
			{
				const FVector2D Dir = To.GetSafeNormal();
				const FVector2D Tip = Me + Dir * 30.0;
				const FVector2D Side(-Dir.Y, Dir.X);
				Line(Me + Dir * 12.0, Tip, S.MinimapTreasure, 3.f, LayerId + 5);
				Line(Tip, Tip - Dir * 8.0 + Side * 6.0, S.MinimapTreasure, 3.f, LayerId + 5);
				Line(Tip, Tip - Dir * 8.0 - Side * 6.0, S.MinimapTreasure, 3.f, LayerId + 5);
				Stats.bTreasureArrow = true;
			}
		}
	}
	LastStats = Stats;
	return LayerId + 9;
}
