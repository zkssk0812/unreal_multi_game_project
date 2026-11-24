#include "Gameplay/VHDoorSubsystem.h"

#include "Gameplay/VHDoor.h"
#include "Gameplay/VHGameplayConfig.h"
#include "Gameplay/VHInteractionComponent.h"
#include "Gameplay/VHInteractableBase.h"
#include "Gameplay/VHInteractionSubsystem.h"
#include "Gameplay/VHServerGameSubsystem.h"
#include "Gameplay/VHSoundConfig.h"
#include "Gameplay/VHWeaponFx.h"
#include "Network/VHSettings.h"
#include "MCPTest.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

#define LOCTEXT_NAMESPACE "VHDoor"

namespace
{
	/** 로컬 출처: 내 클라. F 를 누르면 바로 바뀐다 (가짜 서버처럼 서버 문이 없을 때. 다른 클라와는 맞춰지지 않음) */
	class FVHLocalDoorStateSource final : public IVHDoorStateSource
	{
	public:
		virtual const TCHAR* GetName() const override { return TEXT("로컬(내 클라)"); }
		virtual bool GetInitialOpen(const AVHDoor& Door) const override { return false; }	// 처음엔 모두 닫힘
		virtual void RequestSetOpen(UVHDoorSubsystem& Sub, AVHDoor& Door, bool bOpen) override
		{
			Sub.ApplyDoorState(&Door, bOpen, TEXT("로컬 F"));
		}
	};

	/**
	 * 서버 출처 (2026-11-24 팀 서버 9afd591): F = CS_ACTION InteractBegin(문 엔티티 id). 바로 바꾸지 않는다 —
	 * 서버가 열고 닫으면 바뀐 틱의 스냅샷(ES_OPENED)이 와서 ApplyServerDoorState → ApplyDoorState. 거절은 SC_ACTION_REJECT
	 */
	class FVHServerDoorStateSource final : public IVHDoorStateSource
	{
	public:
		virtual const TCHAR* GetName() const override { return TEXT("서버(Door 엔티티·ES_OPENED)"); }
		virtual bool GetInitialOpen(const AVHDoor& Door) const override { return false; }	// 서버도 처음엔 모두 닫힘 (게임 시작 엔티티의 상태 비트로 다시 맞춘다)
		virtual bool IsServer() const override { return true; }
		virtual void RequestSetOpen(UVHDoorSubsystem& Sub, AVHDoor& Door, bool bOpen) override
		{
			UVHServerGameSubsystem* Game = Sub.GetWorld() ? Sub.GetWorld()->GetSubsystem<UVHServerGameSubsystem>() : nullptr;
			if (Door.ServerEntityId == 0 || !Game)
			{
				UE_LOG(LogMCPTest, Warning, TEXT("[Door] %s: 서버 문 엔티티가 없어 요청하지 않음"), *Door.Describe());
				return;
			}
			// 서버 InteractBegin 은 "토글"이라, 이미 그 상태면 보내지 않는다 (vh.DoorSet close 가 닫힌 문을 여는 일 방지)
			if (Door.IsOpen() == bOpen)
			{
				UE_LOG(LogMCPTest, Log, TEXT("[Door] %s: 이미 %s → 요청하지 않음"), *Door.Describe(), bOpen ? TEXT("열림") : TEXT("닫힘"));
				return;
			}
			Game->SendDoorInteract(Door.ServerEntityId, bOpen, Door.Describe());
		}
	};

	// vh.DoorBind : 서버 문 짝짓기 결과
	FAutoConsoleCommandWithWorldAndArgs GVHDoorBindCommand(TEXT("vh.DoorBind"),
		TEXT("개발용: vh.DoorBind — 서버 Door 엔티티와 클라 문의 짝짓기 결과를 로그에 쓴다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (const UVHDoorSubsystem* Sub = World ? World->GetSubsystem<UVHDoorSubsystem>() : nullptr)
			{
				Sub->LogServerBindSummary();
			}
		}));

	// vh.PressF : F 키를 한 번 누른 것과 같은 길 (상호작용 컴포넌트 InteractPressed → 물품이 없으면 문)
	FAutoConsoleCommandWithWorldAndArgs GVHPressFCommand(TEXT("vh.PressF"),
		TEXT("개발용: vh.PressF — F 키를 한 번 누른 것처럼 (상자·아이템·반납이 F 대상이 아니면 앞의 문을 열고 닫는다)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			UVHInteractionComponent* I = Pawn ? Pawn->FindComponentByClass<UVHInteractionComponent>() : nullptr;
			if (!I)
			{
				return;
			}
			const UVHDoorSubsystem* Sub = World->GetSubsystem<UVHDoorSubsystem>();
			const AVHDoor* Door = Sub ? Sub->FindDoorFor(Pawn) : nullptr;
			UE_LOG(LogMCPTest, Log, TEXT("[Door] vh.PressF: 물품 대상 %s, 앞의 문 %s, 내 위치 (%.0f, %.0f)"), *GetNameSafe(I->GetFocusTarget()),
				Door ? *Door->Describe() : TEXT("없음"), Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y);
			I->InteractPressed();
			I->InteractReleased();
		}));

	// vh.DoorSet open|close|toggle all|near|<문 id> : 개발용 문 열기/닫기 (상태 출처를 거친다)
	FAutoConsoleCommandWithWorldAndArgs GVHDoorSetCommand(TEXT("vh.DoorSet"),
		TEXT("개발용: vh.DoorSet open|close|toggle [all|near|문 id] — 문을 열거나 닫는다(기본 near = 내 캐릭터에서 가장 가까운 문)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UVHDoorSubsystem* Sub = World ? World->GetSubsystem<UVHDoorSubsystem>() : nullptr;
			if (!Sub || Args.Num() < 1)
			{
				return;
			}
			const FString Op = Args[0].ToLower();
			const FString Which = Args.Num() > 1 ? Args[1] : TEXT("near");
			const APlayerController* PC = World->GetFirstPlayerController();
			const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			TArray<AVHDoor*> Targets;
			if (Which.Equals(TEXT("all"), ESearchCase::IgnoreCase))
			{
				for (const TWeakObjectPtr<AVHDoor>& D : Sub->GetDoors())
				{
					if (D.IsValid())
					{
						Targets.Add(D.Get());
					}
				}
			}
			else if (Which.Equals(TEXT("near"), ESearchCase::IgnoreCase))
			{
				AVHDoor* Best = nullptr;
				for (const TWeakObjectPtr<AVHDoor>& D : Sub->GetDoors())
				{
					if (D.IsValid() && Pawn && (!Best || FVector::Dist2D(D->GetActorLocation(), Pawn->GetActorLocation()) < FVector::Dist2D(Best->GetActorLocation(), Pawn->GetActorLocation())))
					{
						Best = D.Get();
					}
				}
				if (Best)
				{
					Targets.Add(Best);
				}
			}
			else
			{
				for (const TWeakObjectPtr<AVHDoor>& D : Sub->GetDoors())
				{
					if (D.IsValid() && D->ServerId.ToString().Equals(Which, ESearchCase::IgnoreCase))
					{
						Targets.Add(D.Get());
					}
				}
			}
			for (AVHDoor* D : Targets)
			{
				if (D->bLocked)
				{
					continue;
				}
				const bool bOpen = Op == TEXT("open") ? true : (Op == TEXT("close") ? false : !D->IsOpen());
				Sub->RequestSetOpen(D, bOpen);
			}
			UE_LOG(LogMCPTest, Log, TEXT("[Door] vh.DoorSet %s %s → 문 %d개"), *Op, *Which, Targets.Num());
		}));
}

bool UVHDoorSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UVHDoorSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	EnsureSource();
	CollectDoors();
}

void UVHDoorSubsystem::Deinitialize()
{
	Doors.Reset();
	Source.Reset();
	Super::Deinitialize();
}

void UVHDoorSubsystem::EnsureSource()
{
	if (Source)
	{
		return;
	}
	// 처음엔 로컬. 서버가 Door 엔티티를 보내면(BindServerDoor) 설정이 Server 일 때 서버 출처로 바꾼다
	Source = MakeUnique<FVHLocalDoorStateSource>();
}

const TCHAR* UVHDoorSubsystem::GetSourceName() const
{
	return Source ? Source->GetName() : TEXT("없음");
}

void UVHDoorSubsystem::CollectDoors()
{
	if (bCollected || !GetWorld())
	{
		return;
	}
	bCollected = true;
	int32 Locked = 0;
	for (TActorIterator<AVHDoor> It(GetWorld()); It; ++It)
	{
		Doors.Add(*It);
		Locked += It->bLocked ? 1 : 0;
		if (!It->bLocked)
		{
			InteriorByServerOrder.Add(*It);
		}
	}
	// 서버 문 번호 = 맵 colliders.static 의 안쪽 above_door 순서 = wall_NNN 오름차순 (맵 v10 확인: 37개, 순서 같음)
	InteriorByServerOrder.Sort([](const TWeakObjectPtr<AVHDoor>& A, const TWeakObjectPtr<AVHDoor>& B)
	{
		// FName 비교는 "wall_105" 를 "wall" + 숫자로 쪼개 비교해 순서가 틀린다 → 문자열로 비교 (2026-11-24 시험에서 발견)
		return A->ServerId.ToString() < B->ServerId.ToString();
	});
	if (Doors.Num() > 0)
	{
		UE_LOG(LogMCPTest, Log, TEXT("[Door] 문 %d개 (잠김 %d), 상태 출처 %s, 처음엔 모두 닫힘, F로 열고 닫기"), Doors.Num(), Locked, GetSourceName());
	}
}

bool UVHDoorSubsystem::GetInitialOpen(const AVHDoor& Door)
{
	EnsureSource();
	return !Door.bLocked && Source->GetInitialOpen(Door);
}

void UVHDoorSubsystem::RequestSetOpen(AVHDoor* Door, bool bOpen)
{
	EnsureSource();
	if (Door && !Door->bLocked)
	{
		Source->RequestSetOpen(*this, *Door, bOpen);
	}
}

void UVHDoorSubsystem::ApplyDoorState(FName DoorId, bool bOpen, const TCHAR* Why)
{
	CollectDoors();
	for (const TWeakObjectPtr<AVHDoor>& D : Doors)
	{
		if (D.IsValid() && D->ServerId == DoorId)
		{
			ApplyDoorState(D.Get(), bOpen, Why);
			return;
		}
	}
	UE_LOG(LogMCPTest, Warning, TEXT("[Door] 모르는 문 id %s (%s)"), *DoorId.ToString(), Why);
}

void UVHDoorSubsystem::ApplyDoorState(AVHDoor* Door, bool bOpen, const TCHAR* Why)
{
	if (!Door || Door->bLocked || Door->IsOpen() == bOpen)
	{
		return;
	}
	Door->SetOpenState(bOpen);
	UE_LOG(LogMCPTest, Log, TEXT("[Door] %s %s (%s, 시각 %.2f)"), *Door->Describe(), bOpen ? TEXT("열림") : TEXT("닫힘"), Why, GetWorld()->GetTimeSeconds());
	OnDoorStateChanged.Broadcast(Door, bOpen);
}

AVHDoor* UVHDoorSubsystem::FindDoorFor(const APawn* Pawn) const
{
	if (!Pawn)
	{
		return nullptr;
	}
	const UVHGameplayConfig& C = UVHSettings::Get().GetGameplayConfig();
	AVHDoor* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<AVHDoor>& Weak : Doors)
	{
		AVHDoor* D = Weak.Get();
		if (!D || !D->IsDoorEnabled())
		{
			continue;
		}
		const FVector L = D->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
		const float Depth = FMath::Abs(L.X) - D->WallThickness * 0.5f;
		const float Side = FMath::Abs(L.Y) - D->Width * 0.5f;
		if (Depth > C.DoorInteractDepth || Side > C.DoorInteractSideMargin || L.Z < -300.f || L.Z > 400.f)
		{
			continue;
		}
		// 서버 문: 서버는 문 가운데에서 150cm(2D) 안만 받는다 → 그 밖이면 고르지 않는다 (DoorServerRange = 150 − 여유)
		if (D->ServerEntityId != 0 && IsServerSource() && FVector::Dist2D(D->GetActorLocation(), Pawn->GetActorLocation()) > C.DoorServerRange)
		{
			continue;
		}
		const float Score = FMath::Max(0.f, Depth) + FMath::Max(0.f, Side) * 2.f;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = D;
		}
	}
	return Best;
}

bool UVHDoorSubsystem::IsPawnInDoorway(const AVHDoor& Door, const APawn* Pawn)
{
	if (!Pawn)
	{
		return false;
	}
	// 서버 DoorOccupied 와 같은 범위 (정호님 답변 2026-11-25): 문 위 벽(above_door) 바닥 사각형 = 벽 두께 × 문 폭 을
	// 사방으로 몸 반지름(40cm, GameData::BodyRadiusCm)만큼 넓힌 범위 안에 몸 중심(2D)이 있으면 문간 점유. 경계 포함(서버도 <=)
	const float R = UVHSettings::Get().GetGameplayConfig().DoorOccupyRadius;
	const FVector L = Door.GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
	return FMath::Abs(L.X) <= Door.WallThickness * 0.5f + R && FMath::Abs(L.Y) <= Door.Width * 0.5f + R;
}

FString UVHDoorSubsystem::DescribeDoorway(const AVHDoor& Door, const APawn* Pawn)
{
	if (!Pawn)
	{
		return FString();
	}
	const float R = UVHSettings::Get().GetGameplayConfig().DoorOccupyRadius;
	const FVector L = Door.GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
	const float LimX = Door.WallThickness * 0.5f + R, LimY = Door.Width * 0.5f + R;
	// 넓힌 범위 경계까지 남은 거리: 양수 = 안쪽, 음수 = 바깥 (두 축 중 작은 쪽)
	const float Inside = FMath::Min(LimX - FMath::Abs(L.X), LimY - FMath::Abs(L.Y));
	return FString::Printf(TEXT("문 로컬 (%.1f, %.1f), 넓힌 범위 |x|<=%.0f |y|<=%.0f → 경계까지 %+.1fcm (%s)"), L.X, L.Y, LimX, LimY, Inside,
		Inside >= 0.f ? TEXT("문간 안") : TEXT("문간 밖"));
}

bool UVHDoorSubsystem::TryToggle(APawn* Pawn, AVHDoor* Door, FText& OutMessage)
{
	if (!Door)
	{
		return false;
	}
	if (Door->bLocked)
	{
		OutMessage = LOCTEXT("Locked", "잠긴 문입니다 — 열 수 없습니다");
		UE_LOG(LogMCPTest, Log, TEXT("[Door] %s 잠긴 문 F → 거절"), *Door->Describe());
		return false;
	}
	const bool bOpen = !Door->IsOpen();
	if (!bOpen && IsPawnInDoorway(*Door, Pawn))
	{
		OutMessage = LOCTEXT("InDoorway", "문간에 서 있어서 닫을 수 없습니다 — 한 걸음 비키세요");
		UE_LOG(LogMCPTest, Log, TEXT("[Door] %s 문간에 서서 닫기 → 클라에서 거절 (%s)"), *Door->Describe(), *DescribeDoorway(*Door, Pawn));
		return false;
	}
	if (!bOpen)
	{
		UE_LOG(LogMCPTest, Log, TEXT("[Door] 닫기 요청 보냄 (%s)"), *DescribeDoorway(*Door, Pawn));
	}
	RequestSetOpen(Door, bOpen);
	return true;
}

// ───────────────────────── 서버 문 ─────────────────────────

AVHDoor* UVHDoorSubsystem::BindServerDoor(uint16 EntityId, uint16 TypeId, const FVector& Pos, float Yaw, bool bOpen)
{
	CollectDoors();
	EnsureSource();
	if (!Source->IsServer() && UVHSettings::Get().GetGameplayConfig().DoorStateSource == EVHDoorStateSource::Server)
	{
		Source = MakeUnique<FVHServerDoorStateSource>();
		UE_LOG(LogMCPTest, Log, TEXT("[Door] 서버가 문 엔티티를 보냄 → 문 상태 출처 = %s"), GetSourceName());
	}
	// 1) type_id 순서로 고른 문, 2) 위치(문 가운데 30cm)·방향(yaw 0 = ±X 로 지나감)으로 확인, 어긋나면 위치로 다시 찾는다
	auto Matches = [&Pos, Yaw](const AVHDoor* D, float& OutDist, float& OutYawDiff)
	{
		OutDist = FVector::Dist2D(D->GetActorLocation(), Pos);
		// 문 로컬 X(벽을 뚫는 방향) = 지나가는 방향. 180° 뒤집힌 것은 같은 문
		OutYawDiff = FMath::Abs(FRotator::NormalizeAxis(D->GetActorRotation().Yaw - Yaw));
		OutYawDiff = FMath::Min(OutYawDiff, 180.f - OutYawDiff);
		return OutDist <= 30.f && OutYawDiff <= 10.f;
	};
	AVHDoor* Door = InteriorByServerOrder.IsValidIndex(TypeId) ? InteriorByServerOrder[TypeId].Get() : nullptr;
	float Dist = 0.f, YawDiff = 0.f;
	if (!Door || Door->ServerEntityId != 0 || !Matches(Door, Dist, YawDiff))
	{
		const FString Was = Door ? FString::Printf(TEXT("%s(거리 %.0fcm, 방향 차 %.0f°)"), *Door->ServerId.ToString(), Dist, YawDiff) : FString(TEXT("없음"));
		AVHDoor* Best = nullptr;
		float BestDist = TNumericLimits<float>::Max();
		for (const TWeakObjectPtr<AVHDoor>& Weak : InteriorByServerOrder)
		{
			AVHDoor* D = Weak.Get();
			float DD = 0.f, YD = 0.f;
			if (D && D->ServerEntityId == 0 && Matches(D, DD, YD) && DD < BestDist)
			{
				Best = D;
				BestDist = DD;
			}
		}
		BindProblems.Add(FString::Printf(TEXT("서버 문 %u (엔티티 %u, 위치 %.0f,%.0f yaw %.0f): 순서상 %s → %s"), TypeId, EntityId, Pos.X, Pos.Y, Yaw, *Was,
			Best ? *FString::Printf(TEXT("위치로 %s"), *Best->ServerId.ToString()) : TEXT("짝 없음")));
		UE_LOG(LogMCPTest, Warning, TEXT("[Door] 짝 어긋남: %s"), *BindProblems.Last());
		Door = Best;
	}
	if (!Door)
	{
		return nullptr;
	}
	Door->ServerEntityId = EntityId;
	Door->ServerDoorIndex = TypeId;
	ServerDoors.Add(EntityId, Door);
	if (Door->IsOpen() != bOpen)
	{
		ApplyDoorState(Door, bOpen, TEXT("서버 시작 상태"));
	}
	return Door;
}

void UVHDoorSubsystem::ApplyServerDoorState(uint16 EntityId, bool bOpen)
{
	if (AVHDoor* Door = FindByServerEntity(EntityId))
	{
		ApplyDoorState(Door, bOpen, TEXT("서버"));
	}
	else
	{
		UE_LOG(LogMCPTest, Warning, TEXT("[Door] 짝 없는 서버 문 엔티티 %u 의 상태 %s"), EntityId, bOpen ? TEXT("열림") : TEXT("닫힘"));
	}
}

AVHDoor* UVHDoorSubsystem::FindByServerEntity(uint16 EntityId) const
{
	const TWeakObjectPtr<AVHDoor>* Found = ServerDoors.Find(EntityId);
	return Found ? Found->Get() : nullptr;
}

void UVHDoorSubsystem::LogServerBindSummary() const
{
	TArray<FString> NoServer;
	int32 Open = 0;
	for (const TWeakObjectPtr<AVHDoor>& Weak : Doors)
	{
		if (const AVHDoor* D = Weak.Get())
		{
			if (D->ServerEntityId == 0)
			{
				NoServer.Add(FString::Printf(TEXT("%s%s"), *D->ServerId.ToString(), D->bLocked ? TEXT("(잠김)") : TEXT("")));
			}
			Open += D->IsOpen() ? 1 : 0;
		}
	}
	UE_LOG(LogMCPTest, Log, TEXT("[Door] 서버 문 짝짓기: 서버 문 %d개 ↔ 클라 문 %d개, 어긋남 %d, 서버에 없는 클라 문 %d개 [%s], 열림 %d, 상태 출처 %s"),
		ServerDoors.Num(), Doors.Num(), BindProblems.Num(), NoServer.Num(), *FString::Join(NoServer, TEXT(", ")), Open, GetSourceName());
	for (const FString& P : BindProblems)
	{
		UE_LOG(LogMCPTest, Warning, TEXT("[Door]   어긋남: %s"), *P);
	}
}

bool UVHDoorSubsystem::TraceClosedDoor(const FVector& From, const FVector& To, FVector& OutHit, float& OutT, AVHDoor** OutDoor) const
{
	float Best = 2.f;
	AVHDoor* BestDoor = nullptr;
	for (const TWeakObjectPtr<AVHDoor>& Weak : Doors)
	{
		AVHDoor* D = Weak.Get();
		float T = 0.f;
		if (D && D->BlocksShots() && D->IntersectPanel(From, To, T) && T < Best)
		{
			Best = T;
			BestDoor = D;
		}
	}
	if (!BestDoor)
	{
		return false;
	}
	OutT = Best;
	OutHit = FMath::Lerp(From, To, Best);
	if (OutDoor)
	{
		*OutDoor = BestDoor;
	}
	return true;
}

void UVHDoorSubsystem::PlayDoorHitFx(const FVector& At, const FLinearColor& Color) const
{
	if (UVHWeaponFxSubsystem* Fx = UVHWeaponFxSubsystem::Get(this))
	{
		Fx->DoorHitSpark(At, Color);
	}
	UVHSoundConfig::Play(this, EVHSound::DoorHit, At);
}

#undef LOCTEXT_NAMESPACE
