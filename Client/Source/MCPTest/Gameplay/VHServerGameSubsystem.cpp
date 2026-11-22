#include "Gameplay/VHServerGameSubsystem.h"
#include "Gameplay/VHCharacterVisuals.h"
#include "Gameplay/VHSoundConfig.h"

#include "Gameplay/VHNetProxy.h"
#include "Gameplay/VHFxActor.h"
#include "Gameplay/VHActionComponent.h"
#include "Gameplay/VHAimComponent.h"
#include "Gameplay/VHHealthComponent.h"
#include "Gameplay/VHMatchSubsystem.h"
#include "Gameplay/VHInteractionSubsystem.h"
#include "Gameplay/VHInteractionComponent.h"
#include "Gameplay/VHInteractableBase.h"
#include "Gameplay/VHThiefStateComponent.h"
#include "Gameplay/VHStatusEffectComponent.h"
#include "Gameplay/VHTreasureCarrierComponent.h"
#include "Gameplay/VHGameplayConfig.h"
#include "Gameplay/VHGameplayTypes.h"
#include "Gameplay/VHSafe.h"
#include "Gameplay/VHReturnPoint.h"
#include "Gameplay/VHDroppedTreasure.h"
#include "Gameplay/VHBox.h"
#include "Misc/FileHelper.h"
#include "Gameplay/VHItemPickup.h"
#include "Gameplay/VHRanking.h"
#include "Gameplay/VHRoomVolume.h"
#include "Network/VHNetSubsystem.h"
#include "Network/VHPacket.h"
#include "Network/VHSettings.h"
#include "UI/VHUIStyle.h"
#include "MCPTest.h"
#include "MCPTestCharacter.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "DrawDebugHelpers.h"

#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Gameplay/VHWeaponVisualComponent.h"
#include "Gameplay/VHWeaponFx.h"
#include "Gameplay/VHWeaponCatalog.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogVHServerGame, Log, All);

#define LOCTEXT_NAMESPACE "VHServerGame"

using namespace RobProto;

namespace
{
	constexpr double MoveSendInterval = 1.0 / MOVE_SEND_HZ;	// 최대 20Hz
	constexpr double IdleSendInterval = 1.0;					// 멈춰 있어도 1초에 한 번
	constexpr float MatchDistance = 300.f;						// 서버 금고 ↔ 레벨 금고 짝짓기 거리(2D)
	const FName ServerSpawnedTag(TEXT("VHServerSpawned"));		// 서버 위치에 만든 금고(레벨에 놓인 금고와 구분)
	constexpr double OwnActionWindowSeconds = 0.5;				// 내가 보낸 행동 뒤 이 시간 안의 Cancel은 내가 일으킨 것으로 본다
	constexpr float CancelGraceSeconds = 0.15f;					// 길게 누르기가 이만큼 남았을 때 손을 떼면 취소를 미룬다
	constexpr double CancelWaitSeconds = 0.6;					// 미룬 취소: 이 시간 안에 서버 완료가 없으면 취소 전송
	constexpr float ToolsWorkSpeed = 1.1f;						// 유용한 도구 퍽: 작업 속도 1.1배
	// 표시용 범위 (서버 GameData.h 와 같은 값. 판정은 서버가 한다)
	constexpr float StickyRadiusCm = 300.f;						// 끈끈이 함정 반경
	constexpr float SmokeRadiusCm = 500.f;						// 연막 반경
	constexpr float SkillTaserRangeCm = 300.f;					// 사제 테이저건 사거리
	constexpr float StunSeconds = 2.f;							// 경직 (테이저·감전 지뢰)
	// 레일건 시간 (서버 GameData.h RailgunChargeMs·RailgunBeamMs 와 같은 값. 충전 연출·빔을 서버 판정 시각에 맞춘다)
	constexpr double RailgunChargeSeconds = 1.0;				// SC_FIRE(누른 순간) → 첫 판정
	constexpr double RailgunBeamSeconds = 1.0;					// 첫 판정 → 마지막 판정 (200ms × 6)
	constexpr double RailgunAwaitFireSeconds = 0.6;				// 내 공격 신호 뒤 이 시간 안에 SC_FIRE 가 없으면 끝
	/**
	 * 개발용(-VHBotPickTest) 연막탄 줍기 시험 단계. 게임(월드)이 바뀌어도 이어지도록 파일 전역
	 * 0 = ① 빈손으로 연막탄 줍기, 1 = ② 다른 소비 아이템을 든 채 연막탄, 2 = ③ 연막탄을 든 채 연막탄(서버로 보냄),
	 * 3 = ③-2 같은 경우를 클라가 막는지(알림), 4 = 끝
	 */
	int32 GPickTestStage = 0;
	constexpr uint8 SmokeId = static_cast<uint8>(ConsumableId::Smoke);
	/** 개발용 -VHBotDownTest=Guard,Player: 보물을 든 채 쓰러질 경우를 차례로 (Guard = 경비원에게, Player = 다른 플레이어에게) */
	const TArray<FString>& BotDownTestCases()
	{
		static TArray<FString> Cases;
		static bool bRead = false;
		if (!bRead)
		{
			bRead = true;
			FString Value;
			if (FParse::Value(FCommandLine::Get(), TEXT("VHBotDownTest="), Value, false))	// 쉼표에서 끊지 않는다
			{
				Value.ParseIntoArray(Cases, TEXT(","));
			}
		}
		return Cases;
	}
	/** 줍기 시험: 알림이 HUD에 그려진 뒤(0.4초) 화면을 Saved/V3Test/pick_<이름>_<태그>.png 로 저장 */
	void PickTestShot(UWorld* World, const FString& Tag)
	{
		if (!World || !FParse::Param(FCommandLine::Get(), TEXT("VHBotPickTest")))
		{
			return;
		}
		FString Name;
		FParse::Value(FCommandLine::Get(), TEXT("VHBot="), Name);
		const FString File = FPaths::ProjectSavedDir() / TEXT("V3Test") / FString::Printf(TEXT("pick_%s_%s.png"), *Name, *Tag);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([File]()
		{
			FScreenshotRequest::RequestScreenshot(File, true, false);
			UE_LOG(LogVHServerGame, Log, TEXT("[줍기시험] 화면 저장 %s"), *FPaths::GetCleanFilename(File));
		}), 0.4f, false);
	}
	/**
	 * 그 자리에 "레벨에 처음부터 놓인" 막는 것(벽·가구·닫힌 문)이 있나.
	 * 게임 중에 생긴 액터(상자·반납지점·떨어진 아이템·다른 금고·연출)는 세지 않는다. 이들은 클라마다 생기는 때가 달라질 수 있어서,
	 * 넣으면 같은 금고가 클라마다 다른 방향으로 보일 수 있다. 레벨과 위치만으로 정해지면 모든 클라의 결과가 같다.
	 */
	bool IsBlockedByLevel(const UWorld* World, const FVector& Point, float Radius)
	{
		if (!World)
		{
			return false;
		}
		TArray<FOverlapResult> Hits;
		World->OverlapMultiByObjectType(Hits, Point, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
			FCollisionShape::MakeSphere(Radius), FCollisionQueryParams(FName(TEXT("VHLevelBlock")), false));
		for (const FOverlapResult& Hit : Hits)
		{
			const AActor* Actor = Hit.GetActor();
			if (Actor && Actor->IsNetStartupActor())	// 레벨과 함께 불러온 액터만 (게임 중 SpawnActor 한 것은 false)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * 서버 위치에 만드는 금고의 문 방향: 서버 yaw부터 90°씩 돌려 보며 문 앞(115cm)이 레벨에 막히지 않는 첫 방향.
	 * 입력은 금고 위치·서버 yaw·레벨뿐이다(내 위치·시각·난수·다른 엔티티를 쓰지 않는다) → 모든 클라에서 같은 방향.
	 * 네 방향이 다 막혀 있으면 서버 yaw 그대로.
	 */
	float ChooseSafeYaw(const UWorld* World, const FVector& SafePos, float ServerYaw, FString* OutChecked = nullptr)
	{
		float Chosen = FRotator::NormalizeAxis(ServerYaw);
		bool bFound = false;
		for (int32 i = 0; i < 4; ++i)
		{
			const float Try = FRotator::NormalizeAxis(ServerYaw + 90.f * i);
			const FVector Front = SafePos + FRotator(0.f, Try, 0.f).Vector() * 115.f + FVector(0.f, 0.f, 90.f);
			const bool bBlocked = IsBlockedByLevel(World, Front, 45.f);
			if (OutChecked)
			{
				*OutChecked += FString::Printf(TEXT(" %.0f°=%s"), Try, bBlocked ? TEXT("막힘") : TEXT("빔"));
			}
			if (!bBlocked && !bFound)
			{
				Chosen = Try;
				bFound = true;
				if (!OutChecked)
				{
					break;	// 로그가 필요 없으면 첫 빈 방향에서 끝낸다
				}
			}
		}
		return Chosen;
	}

	/** 다른 플레이어의 이 상태가 바뀌면 로그로 남긴다 (시험 확인용) */
	constexpr uint32 LoggedOtherBits = ES_STUNNED | ES_SLOWED | ES_CLOAKED | ES_IN_SMOKE | ES_FLASHED | ES_RAILGUN_LOCK | ES_JETPACK | ES_TASER_READY;

	FVector ToVector(const Vec3& V) { return FVector(V.x, V.y, V.z); }

	/** 즉시 끝나는 상호작용(줍기·반납): SC_INTERACT 없이 SC_PICKUP·SC_MONEY 로 결과가 온다 */
	bool IsInstantKind(EntityKind K)
	{
		return K == EntityKind::Valuable || K == EntityKind::WeaponPickup || K == EntityKind::ConsumablePickup || K == EntityKind::ReturnPoint;
	}

	/** 서버 RejectReason → 클라 거절 이유 + 안내 문구 */
	EVHRejectReason ToLocalReject(RejectReason R, FText& OutDetail)
	{
		switch (R)
		{
		case RejectReason::InvalidState:	return EVHRejectReason::InvalidState;
		case RejectReason::Cooldown:		return EVHRejectReason::Cooldown;
		case RejectReason::NoStamina:		OutDetail = LOCTEXT("NoStamina", "스테미나가 부족합니다"); return EVHRejectReason::InvalidState;
		case RejectReason::OutOfRange:		return EVHRejectReason::OutOfRange;
		case RejectReason::TargetBusy:		return EVHRejectReason::TargetBusy;
		case RejectReason::NotAvailable:	OutDetail = LOCTEXT("NotAvailable", "지금은 쓸 수 없습니다"); return EVHRejectReason::InvalidState;
		case RejectReason::Downed:			OutDetail = LOCTEXT("RejDowned", "쓰러진 동안에는 할 수 없습니다"); return EVHRejectReason::InvalidState;
		case RejectReason::Stunned:			OutDetail = LOCTEXT("RejStunned", "경직 중에는 할 수 없습니다"); return EVHRejectReason::InvalidState;
		case RejectReason::Locked:			OutDetail = LOCTEXT("RejLocked", "지금 동작이 끝난 뒤에 할 수 있습니다"); return EVHRejectReason::InvalidState;
		case RejectReason::HpFull:			OutDetail = LOCTEXT("RejHpFull", "HP가 가득 차 있습니다"); return EVHRejectReason::InvalidState;
		case RejectReason::OnlyOneWeapon:	OutDetail = LOCTEXT("RejOneWeapon", "무기가 하나뿐이라 바꿀 수 없습니다"); return EVHRejectReason::InvalidState;
		}
		return EVHRejectReason::InvalidState;
	}
}

const TCHAR* UVHServerGameSubsystem::KindName(EntityKind K)
{
	switch (K)
	{
	case EntityKind::Thief: return TEXT("플레이어");
	case EntityKind::Guard: return TEXT("경비원");
	case EntityKind::Safe: return TEXT("금고");
	case EntityKind::ReturnPoint: return TEXT("반납지점");
	case EntityKind::Valuable: return TEXT("환금아이템");
	case EntityKind::WeaponPickup: return TEXT("무기아이템");
	case EntityKind::ConsumablePickup: return TEXT("소비아이템");
	case EntityKind::FieldItem: return TEXT("필드아이템");
	case EntityKind::Box: return TEXT("상자");
	case EntityKind::StickyTrap: return TEXT("끈끈이");
	case EntityKind::Mine: return TEXT("지뢰");
	case EntityKind::Smoke: return TEXT("연막");
	}
	return TEXT("?");
}

const TCHAR* UVHServerGameSubsystem::WeaponName(WeaponId Id)
{
	switch (Id)
	{
	case WeaponId::None: return TEXT("없음");
	case WeaponId::Baton: return TEXT("삼단봉");
	case WeaponId::Sword: return TEXT("도검");
	case WeaponId::BeamSword: return TEXT("빔 소드");
	case WeaponId::Pistol: return TEXT("권총");
	case WeaponId::Flamethrower: return TEXT("화염방사기");
	case WeaponId::Railgun: return TEXT("레일건");
	case WeaponId::Taser: return TEXT("테이저건");
	case WeaponId::Grenade: return TEXT("수류탄");
	case WeaponId::BlastMine: return TEXT("폭발 지뢰");
	case WeaponId::ShockMine: return TEXT("감전 지뢰");
	}
	return TEXT("?");
}

const TCHAR* UVHServerGameSubsystem::ConsumableName(ConsumableId Id)
{
	switch (Id)
	{
	case ConsumableId::HealKit: return TEXT("회복 키트");
	case ConsumableId::Smoke: return TEXT("연막탄");
	case ConsumableId::Flashbang: return TEXT("섬광탄");
	case ConsumableId::Jetpack: return TEXT("1회용 제트팩");
	default: break;
	}
	return TEXT("없음");
}

const TCHAR* UVHServerGameSubsystem::FieldItemName(FieldItemId Id)
{
	switch (Id)
	{
	case FieldItemId::PotionS: return TEXT("회복약(소)");
	case FieldItemId::PotionM: return TEXT("회복약(중)");
	case FieldItemId::PotionL: return TEXT("회복약(대)");
	default: break;
	}
	return TEXT("필드 아이템");
}

const TCHAR* UVHServerGameSubsystem::WeaponDamageText(WeaponId Id)
{
	// 서버 GameData.h WeaponTypes 와 기획 무기 표의 대미지 (표시용. 판정은 서버)
	switch (Id)
	{
	case WeaponId::Baton: return TEXT("10");
	case WeaponId::Sword: return TEXT("25");
	case WeaponId::BeamSword: return TEXT("40");
	case WeaponId::Pistol: return TEXT("10");
	case WeaponId::Flamethrower: return TEXT("2");
	case WeaponId::Railgun: return TEXT("20×6");
	case WeaponId::Taser: return TEXT("1");
	case WeaponId::Grenade: return TEXT("40~120");
	case WeaponId::BlastMine: return TEXT("20~80");
	case WeaponId::ShockMine: return TEXT("1~20");
	default: break;
	}
	return TEXT("-");
}

const TCHAR* UVHServerGameSubsystem::ConsumableEffectText(ConsumableId Id)
{
	switch (Id)
	{
	case ConsumableId::HealKit: return TEXT("HP 30 회복");
	case ConsumableId::Smoke: return TEXT("시야 방해");
	case ConsumableId::Flashbang: return TEXT("시야 방해");
	case ConsumableId::Jetpack: return TEXT("이동");
	default: break;
	}
	return TEXT("-");
}

const TCHAR* UVHServerGameSubsystem::SkillName(SkillId Id)
{
	switch (Id)
	{
	case SkillId::Taser: return TEXT("테이저");
	case SkillId::Sticky: return TEXT("끈끈이");
	case SkillId::Cloak: return TEXT("은신");
	default: break;
	}
	return TEXT("없음");
}

FString UVHServerGameSubsystem::WithJosa(const FString& Word, const TCHAR* WithBatchim, const TCHAR* Without)
{
	if (Word.IsEmpty())
	{
		return Word;
	}
	const TCHAR Last = Word[Word.Len() - 1];
	int32 Final = 0;	// 받침 번호 (0 = 없음, 8 = ㄹ)
	if (Last >= 0xAC00 && Last <= 0xD7A3)
	{
		Final = (Last - 0xAC00) % 28;
	}
	const bool bRo = FCString::Strcmp(WithBatchim, TEXT("으로")) == 0;
	const bool bUse = Final != 0 && !(bRo && Final == 8);
	return Word + (bUse ? WithBatchim : Without);
}

// ───────────────────────── 서버 판정 담당 ─────────────────────────

FVHInteractOffer UVHServerInteractionAuthority::Evaluate(const UVHInteractionComponent* By, const AVHInteractableBase* Target) const
{
	// v3.0: 열린 금고에는 상호작용이 없다 (보물은 금고 앞에 따로 생기는 환금 아이템 엔티티를 줍는다)
	if (const AVHSafe* Safe = Cast<AVHSafe>(Target))
	{
		if (Safe->GetSafeState() != EVHSafeState::Closed)
		{
			return FVHInteractOffer();
		}
	}
	FVHInteractOffer Offer = Super::Evaluate(By, Target);
	// 반납: "들고 있는 가치"는 서버 값(SC_MONEY)으로 판단한다
	if (Offer.bHasOffer && Cast<AVHReturnPoint>(Target))
	{
		const UVHServerGameSubsystem* G = Game.Get();
		const UWorld* World = G ? G->GetWorld() : nullptr;
		const UVHMatchSubsystem* Match = World ? World->GetSubsystem<UVHMatchSubsystem>() : nullptr;
		const FVHServerMoneyRow* Mine = Match ? Match->GetMyServerMoney() : nullptr;
		if (Mine && Mine->Carried > 0)
		{
			Offer.Block = EVHInteractBlock::None;
			Offer.BlockText = FText::GetEmpty();
		}
		else
		{
			Offer.Blocked(EVHInteractBlock::NotAvailable, LOCTEXT("NothingToReturn", "반납할 환금 아이템이 없습니다"));
		}
	}
	return Offer;
}

void UVHServerInteractionAuthority::RequestBegin(UVHInteractionComponent* By, AVHInteractableBase* Target)
{
	UVHInteractionSubsystem* Sub = GetSubsystem();
	UVHServerGameSubsystem* G = Game.Get();
	if (!Sub || !G || !By || !Target)
	{
		return;
	}
	// 안내 문구용 로컬 규칙으로 명백히 안 되는 것만 미리 거른다. 나머지는 서버가 판정
	const FVHInteractOffer Offer = Evaluate(By, Target);
	if (!Offer.bHasOffer)
	{
		Sub->ApplyReject(By, EVHRejectReason::InvalidState, FText::GetEmpty());
		return;
	}
	if (!Offer.IsAvailable())
	{
		Sub->ApplyReject(By, VHInteract::ToRejectReason(Offer.Block), Offer.BlockText);
		return;
	}
	G->SendInteractBegin(By, Target, Offer);
}

void UVHServerInteractionAuthority::RequestCancel(UVHInteractionComponent* By)
{
	if (UVHServerGameSubsystem* G = Game.Get())
	{
		G->SendInteractCancel(By);
	}
}

// ───────────────────────── 수명 ─────────────────────────

bool UVHServerGameSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UVHServerGameSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UVHServerGameSubsystem, STATGROUP_Tickables);
}

void UVHServerGameSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	UVHNetSubsystem* N = UVHNetSubsystem::Get(&InWorld);
	if (!N || !N->IsServerAuthoritativeGame() || UVHNetSubsystem::IsMenuWorld(&InWorld))
	{
		return;
	}
	Net = N;
	bActive = true;
	PacketHandle = N->OnGamePacket.AddUObject(this, &UVHServerGameSubsystem::HandlePacket);
	UE_LOG(LogVHServerGame, Log, TEXT("서버 판정(v3.0)으로 게임 레벨 시작: %s (서버 패킷 대기)"), *N->GetTransportDescription());

	// 상호작용 요청을 서버로 보내는 담당으로 바꾼다
	if (UVHInteractionSubsystem* Sub = InWorld.GetSubsystem<UVHInteractionSubsystem>())
	{
		ServerAuthority = NewObject<UVHServerInteractionAuthority>(this);
		ServerAuthority->Game = this;
		Sub->SetAuthority(ServerAuthority);
	}

	// SC_GAME_START가 레벨보다 먼저 왔으면 바로 적용
	const FVHGameStartInfo& Start = N->GetGameStartInfo();
	if (Start.bValid)
	{
		SC_GameStart P = VHPacket::Make<SC_GameStart>(PacketId::SC_GAME_START);
		P.self_entity_id = Start.SelfEntityId;
		P.duration_ms = Start.DurationMs;
		P.entity_count = static_cast<uint8>(FMath::Min(Start.Entities.Num(), MAX_ENTITIES));
		for (int32 i = 0; i < P.entity_count; ++i)
		{
			P.entities[i] = Start.Entities[i];
		}
		ApplyGameStart(P);
	}
}

void UVHServerGameSubsystem::Deinitialize()
{
	if (UVHNetSubsystem* N = Net.Get())
	{
		N->OnGamePacket.Remove(PacketHandle);
	}
	Super::Deinitialize();
}

// ───────────────────────── 도우미 ─────────────────────────

APawn* UVHServerGameSubsystem::GetLocalPawn() const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	return PC ? PC->GetPawn() : nullptr;
}

float UVHServerGameSubsystem::GetLookYaw() const
{
	const APawn* Pawn = GetLocalPawn();
	if (!Pawn)
	{
		return 0.f;
	}
	// 시선 = 마우스 커서 방향. 서버는 이 값을 공격·스킬 방향으로 쓴다
	const UVHAimComponent* Aim = Pawn->FindComponentByClass<UVHAimComponent>();
	return FRotator::NormalizeAxis(Aim ? Aim->GetLookYaw() : Pawn->GetActorRotation().Yaw);
}

FVector UVHServerGameSubsystem::GetAimPoint() const
{
	const APawn* Pawn = GetLocalPawn();
	const UVHAimComponent* Aim = Pawn ? Pawn->FindComponentByClass<UVHAimComponent>() : nullptr;
	if (!Aim)
	{
		return FVector::ZeroVector;
	}
	// 커서가 가리키는 바닥 지점 (투척 조준). 서버 좌표는 바닥 z = 0 기준
	const FVector P = Aim->GetAimPoint();
	return FVector(P.X, P.Y, 0.f);
}

UVHInteractionSubsystem* UVHServerGameSubsystem::GetInteraction() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UVHInteractionSubsystem>() : nullptr;
}

UVHMatchSubsystem* UVHServerGameSubsystem::GetMatch() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UVHMatchSubsystem>() : nullptr;
}

UVHInteractionComponent* UVHServerGameSubsystem::GetMyInteraction() const
{
	const APawn* Pawn = GetLocalPawn();
	return Pawn ? Pawn->FindComponentByClass<UVHInteractionComponent>() : nullptr;
}

UVHThiefStateComponent* UVHServerGameSubsystem::GetMyThief() const
{
	const APawn* Pawn = GetLocalPawn();
	return Pawn ? Pawn->FindComponentByClass<UVHThiefStateComponent>() : nullptr;
}

UVHTreasureCarrierComponent* UVHServerGameSubsystem::GetMyCarrier() const
{
	const APawn* Pawn = GetLocalPawn();
	return Pawn ? Pawn->FindComponentByClass<UVHTreasureCarrierComponent>() : nullptr;
}

AVHInteractableBase* UVHServerGameSubsystem::ObjectFor(uint16 Id) const
{
	const FEntity* E = Entities.Find(Id);
	return E ? E->Object.Get() : nullptr;
}

uint16 UVHServerGameSubsystem::FindEntityIdFor(const AVHInteractableBase* Object) const
{
	for (const TPair<uint16, FEntity>& Pair : Entities)
	{
		if (Object && Pair.Value.Object.Get() == Object)
		{
			return Pair.Key;
		}
	}
	return 0;
}

bool UVHServerGameSubsystem::IsSameAsHeldConsumable(const AVHInteractableBase* Object) const
{
	if (!bActive || !MyStatus.bValid || MyStatus.Consumable == 0 || !Object || Object->GetKind() != EVHEntityKind::ConsumablePickup)
	{
		return false;
	}
	const AVHItemPickup* Item = Cast<AVHItemPickup>(Object);
	return Item && Item->GetTypeId() == MyStatus.Consumable;
}

float UVHServerGameSubsystem::GetPawnHalfHeight(const APawn* Pawn) const
{
	const ACharacter* C = Cast<ACharacter>(Pawn);
	return C && C->GetCapsuleComponent() ? C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 96.f;
}

FString UVHServerGameSubsystem::NameForPlayer(uint64 PlayerId) const
{
	if (const UVHNetSubsystem* N = Net.Get())
	{
		if (const FVHRoomMember* M = N->GetRoomState().FindMember(static_cast<int64>(PlayerId)))
		{
			return M->Name;
		}
	}
	return FString::Printf(TEXT("Player %llu"), PlayerId);
}

int32 UVHServerGameSubsystem::CharacterForPlayer(uint64 PlayerId) const
{
	if (const UVHNetSubsystem* N = Net.Get())
	{
		if (const FVHRoomMember* M = N->GetRoomState().FindMember(static_cast<int64>(PlayerId)))
		{
			return M->CharacterId;
		}
	}
	return 0;
}

void UVHServerGameSubsystem::UpdateMyWeaponVisual()
{
	// 내 캐릭터가 든 무기: 서버가 알려 준 무기 2칸 중 지금 든 칸. 캐릭터 번호는 방에서 고른 것
	const APawn* Pawn = GetLocalPawn();
	UVHWeaponVisualComponent* Visual = Pawn ? Pawn->FindComponentByClass<UVHWeaponVisualComponent>() : nullptr;
	if (!Visual)
	{
		return;
	}
	const UVHNetSubsystem* N = Net.Get();
	if (const FVHRoomMember* Me = N ? N->GetMyMember() : nullptr)
	{
		Visual->SetCharacterId(Me->CharacterId);
	}
	// 사제 테이저건을 든 동안(서버 ES_TASER_READY)은 테이저건 모델(무기 7번 모양)을 손에 든다. 다른 플레이어 화면도 같은 비트로 든 모습을 보인다(AVHNetProxy)
	Visual->SetWeapon(bSkillTaserHeld ? static_cast<uint8>(WeaponId::Taser)
		: (MyStatus.bValid ? MyStatus.WeaponId[FMath::Clamp(MyStatus.ActiveSlot, 0, 1)] : 0));
	// 꾹 누르기 자세(화염방사기 겨누기): 내 캐릭터는 내가 보낸 공격 시작/끝으로 바로 (다른 플레이어는 스냅샷의 ES_ATTACKING)
	ApplyMyAttackHeldVisual();
}

FString UVHServerGameSubsystem::NameOfEntity(uint16 Id) const
{
	if (Id == 0)
	{
		return TEXT("-");
	}
	const FEntity* E = Entities.Find(Id);
	if (!E)
	{
		return FString::Printf(TEXT("#%u"), Id);
	}
	return E->Kind == EntityKind::Thief ? NameForPlayer(E->Owner)
		: (E->Kind == EntityKind::Guard ? FString::Printf(TEXT("경비원 %u"), Id) : FString::Printf(TEXT("%s %u"), KindName(E->Kind), Id));
}

float UVHServerGameSubsystem::GetMyWorkSpeed() const
{
	return HasMyPerk(VHPerk::Tools) ? ToolsWorkSpeed : 1.f;
}

bool UVHServerGameSubsystem::HasMyPerk(int32 PerkId) const
{
	const UVHNetSubsystem* N = Net.Get();
	const FVHRoomMember* Me = N ? N->GetMyMember() : nullptr;
	return Me && Me->Perks.Contains(PerkId);
}

void UVHServerGameSubsystem::UpdateSelfSpeed()
{
	const APawn* Pawn = GetLocalPawn();
	UVHActionComponent* Action = Pawn ? Pawn->FindComponentByClass<UVHActionComponent>() : nullptr;
	if (!Action)
	{
		return;
	}
	// 서버는 플레이어 위치를 검사하지 않고 상태 비트만 알려 준다 → 느려지는 것은 클라가 스스로 적용한다 (서버 T-64)
	const UVHGameplayConfig& G = UVHSettings::Get().GetGameplayConfig();
	float Scale = 1.f;
	if (HasSelfState(ES_SLOWED))
	{
		Scale *= G.StickySpeedScale;
	}
	if (HasSelfState(ES_CLOAKED))
	{
		Scale *= G.CloakSpeedScale;
	}
	Action->SetMoveSpeedScale(Scale);
	Action->SetSpecialBlocked(HasSelfState(ES_CLOAKED));	// 은신 중 달리기·구르기 불가 (서버도 거절한다)
	Action->SetRunSpeedScale(HasMyPerk(VHPerk::Sneakers) ? G.SneakersRunScale : 1.f);
}

void UVHServerGameSubsystem::SetSelfGhost(int32 Mode)
{
	if (SelfGhostMode == Mode)
	{
		return;
	}
	const ACharacter* Character = Cast<ACharacter>(GetLocalPawn());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Mesh)
	{
		return;
	}
	// 먼저 원래 머티리얼로 되돌린 다음, 필요하면 반투명 머티리얼을 입힌다
	for (int32 i = 0; i < SavedSelfMaterials.Num(); ++i)
	{
		Mesh->SetMaterial(i, SavedSelfMaterials[i]);
	}
	SavedSelfMaterials.Reset();
	SelfGhostMode = 0;
	UMaterialInterface* Base = Mode != 0 ? AVHFxActor::GetFxMaterial() : nullptr;
	UVHWeaponVisualComponent* WeaponVisual = Character->FindComponentByClass<UVHWeaponVisualComponent>();
	if (!Base)
	{
		if (WeaponVisual)
		{
			WeaponVisual->SetOverrideMaterial(nullptr);
		}
		return;
	}
	const UVHGameplayConfig& G = UVHSettings::Get().GetGameplayConfig();
	for (int32 i = 0; i < Mesh->GetNumMaterials(); ++i)
	{
		SavedSelfMaterials.Add(Mesh->GetMaterial(i));
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
		MID->SetVectorParameterValue(TEXT("Color"), Mode == 1 ? G.CloakSelfColor : G.SmokeSelfColor);
		MID->SetScalarParameterValue(TEXT("Opacity"), Mode == 1 ? G.CloakSelfOpacity : 0.45f);
		Mesh->SetMaterial(i, MID);
	}
	if (WeaponVisual)
	{
		// 든 무기도 몸과 같은 반투명으로
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
		MID->SetVectorParameterValue(TEXT("Color"), Mode == 1 ? G.CloakSelfColor : G.SmokeSelfColor);
		MID->SetScalarParameterValue(TEXT("Opacity"), Mode == 1 ? G.CloakSelfOpacity : 0.45f);
		WeaponVisual->SetOverrideMaterial(MID);
	}
	SelfGhostMode = Mode;
}

bool UVHServerGameSubsystem::GetTreasureSense(FVector& OutPos) const
{
	if (TreasureSenseSeconds < 0.0 || !HasMyPerk(VHPerk::TreasureSense)
		|| FPlatformTime::Seconds() - TreasureSenseSeconds > UVHSettings::Get().GetStyle().MinimapTreasureSeconds)
	{
		return false;
	}
	OutPos = TreasureSensePos;
	return true;
}

bool UVHServerGameSubsystem::GetMyDroppedTreasure(FVector& OutPos, int32& OutValue) const
{
	const FEntity* E = MyDroppedTreasureId != 0 ? Entities.Find(MyDroppedTreasureId) : nullptr;
	if (!E)
	{
		return false;
	}
	OutPos = E->Pos;
	OutValue = MyDroppedTreasureValue;
	return true;
}

float UVHServerGameSubsystem::GetDroppedTreasureHintRemaining() const
{
	if (MyDroppedTreasureId == 0 || HasSelfState(ES_DOWNED))
	{
		return 0.f;
	}
	return FMath::Max(0.f, static_cast<float>(DroppedHintUntil - FPlatformTime::Seconds()));
}

float UVHServerGameSubsystem::GetFlashAlpha() const
{
	if (FlashStartSeconds < 0.0)
	{
		return 0.f;
	}
	const float Fade = UVHSettings::Get().GetGameplayConfig().FlashFadeSeconds;
	return FMath::Clamp(1.f - static_cast<float>(FPlatformTime::Seconds() - FlashStartSeconds) / Fade, 0.f, 1.f);
}

void UVHServerGameSubsystem::SpawnFireFx(const FVector& Origin, float Yaw, float Range, float HalfAngle, const FLinearColor& Color, float Life, float Thickness, bool bThroughWalls)
{
	UWorld* World = GetWorld();
	auto Ray = [&](float RayYaw, float RayThickness)
	{
		const FVector Dir = FRotator(0.f, RayYaw, 0.f).Vector();
		FVector End = Origin + Dir * Range;
		if (!bThroughWalls)
		{
			// 화면의 선이 벽을 뚫고 지나가 보이지 않게 벽에서 끊는다 (판정은 서버가 이미 했다)
			FHitResult Hit;
			FCollisionQueryParams Query(FName(TEXT("VHFireFx")), false, GetLocalPawn());
			if (World->LineTraceSingleByChannel(Hit, Origin, End, ECC_WorldStatic, Query))
			{
				End = Hit.ImpactPoint;
			}
		}
		AVHFxActor::SpawnBeam(World, Origin, End, RayThickness, Color, 0.8f, Life);
	};
	if (HalfAngle <= 0.f)
	{
		Ray(Yaw, Thickness);
		return;
	}
	// 부채꼴: 부챗살 5개로 범위를 보여 준다
	const int32 Rays = 5;
	for (int32 i = 0; i < Rays; ++i)
	{
		Ray(Yaw - HalfAngle + 2.f * HalfAngle * i / (Rays - 1), Thickness);
	}
}

void UVHServerGameSubsystem::OnProjectile(const SC_Projectile& P)
{
	const FVector From = ToVector(P.from) + FVector(0.f, 0.f, 120.f);
	const FVector To = ToVector(P.to) + FVector(0.f, 0.f, 10.f);
	UE_LOG(LogVHServerGame, Log, TEXT("[서버] 투척 %s 종류 %d (%.0f, %.0f) → (%.0f, %.0f) %ums"),
		*NameOfEntity(P.thrower), (int32)P.kind, P.from.x, P.from.y, P.to.x, P.to.y, P.flight_ms);
	// 수류탄(3D 모델, 던지는 동작이 손을 떠나는 순간 출발)·섬광탄(흰 공)이 포물선으로 날아간다. 벽·사람에 먼저 닿으면 서버가 그 자리에서 SC_EXPLOSION 을 보낸다
	const bool bFlash = P.kind == ExplosionKind::Flashbang;
	// 몸 동작: 섬광탄을 던진 사람 (수류탄은 무기 공격 동작 SC_FIRE 가 따로 있다)
	if (bFlash)
	{
		if (UVHWeaponVisualComponent* Thrower = VisualOfEntity(P.thrower))
		{
			Thrower->PlayBodyAction(EVHBodyAction::Throw);
		}
	}
	if (UVHWeaponFxSubsystem* WeaponFx = UVHWeaponFxSubsystem::Get(this))
	{
		UVHWeaponVisualComponent* Thrower = nullptr;
		if (P.thrower == SelfEntityId)
		{
			const APawn* Pawn = GetLocalPawn();
			Thrower = Pawn ? Pawn->FindComponentByClass<UVHWeaponVisualComponent>() : nullptr;
		}
		else if (const FEntity* E = Entities.Find(P.thrower); E && E->Proxy.IsValid())
		{
			Thrower = E->Proxy->GetWeaponVisual();
		}
		WeaponFx->OnProjectile(bFlash, ToVector(P.from), ToVector(P.to), P.flight_ms / 1000.f, Thrower);
		return;
	}
	const FLinearColor ThrownGlow = UVHSettings::Get().GetWeaponCatalog().ThrownGlow;	// 수류탄 = 투척 분류 색(앰버), 섬광탄은 흰색 그대로
	if (AVHFxActor* Fx = AVHFxActor::SpawnFx(GetWorld(), From, EVHFxShape::Sphere, FVector(26.f),
		bFlash ? FLinearColor(1.f, 1.f, 0.9f) : ThrownGlow, 1.f))
	{
		Fx->SetArc(From, To, 180.f, FMath::Max(0.05f, P.flight_ms / 1000.f));
		FProjectileFx Entry;
		Entry.Fx = Fx;
		Entry.Kind = P.kind;
		Projectiles.Add(Entry);
	}
	// 떨어질 자리 표시 (옅은 원)
	AVHFxActor::SpawnFx(GetWorld(), ToVector(P.to) + FVector(0.f, 0.f, 3.f), EVHFxShape::Disc, FVector(80.f),
		bFlash ? FLinearColor(1.f, 1.f, 0.8f) : ThrownGlow, 0.5f, FMath::Max(0.05f, P.flight_ms / 1000.f));
}

void UVHServerGameSubsystem::OnExplosion(ExplosionKind Kind, const FVector& Pos, float Radius)
{
	UVHSoundConfig::Play(this, Kind == ExplosionKind::ShockMine ? EVHSound::ElectricShock : (Kind == ExplosionKind::Flashbang ? EVHSound::Flashbang
		: (Kind == ExplosionKind::BlastMine && UVHSoundConfig::Has(EVHSound::BlastMineExplosion) ? EVHSound::BlastMineExplosion : EVHSound::Explosion)), Pos);
	static const TCHAR* Names[] = { TEXT("수류탄"), TEXT("폭발 지뢰"), TEXT("감전 지뢰"), TEXT("섬광탄") };
	UE_LOG(LogVHServerGame, Log, TEXT("[서버] 폭발 %s 위치 (%.0f, %.0f) 반지름 %.0f"), Names[FMath::Clamp<int32>(static_cast<int32>(Kind), 0, 3)], Pos.X, Pos.Y, Radius);
	if (UVHWeaponFxSubsystem* WeaponFx = UVHWeaponFxSubsystem::Get(this))
	{
		WeaponFx->OnExplosion(static_cast<int32>(Kind), Pos, Radius);	// 판정 반지름 그대로의 원 + 폭발 꾸밈 [무기 이펙트 표]
		return;
	}

	// 날아가던 것이 터졌다 → 그 종류의 가장 오래된 비행체를 지운다
	Projectiles.RemoveAll([](const FProjectileFx& E) { return !E.Fx.IsValid(); });
	for (int32 i = 0; i < Projectiles.Num(); ++i)
	{
		if (Projectiles[i].Kind == Kind)
		{
			Projectiles[i].Fx->Destroy();
			Projectiles.RemoveAt(i);
			break;
		}
	}

	const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
	FLinearColor Color = Catalog.ThrownGlow;	// 수류탄: 투척 분류 색(앰버). 지뢰 2종 = 설치 분류 색(라임), 섬광탄 = 흰색
	switch (Kind)
	{
	case ExplosionKind::BlastMine:	Color = Catalog.PlacedGlow; break;
	case ExplosionKind::ShockMine:	Color = Catalog.PlacedGlow; break;
	case ExplosionKind::Flashbang:	Color = FLinearColor(1.f, 1.f, 1.f); break;
	default: break;
	}
	// 바닥의 범위 원(판정 반지름 그대로) + 가운데에서 퍼지는 구
	AVHFxActor::SpawnFx(GetWorld(), Pos + FVector(0.f, 0.f, 4.f), EVHFxShape::Disc, FVector(Radius * 2.f), Color, 0.35f, 0.6f);
	if (AVHFxActor* Ball = AVHFxActor::SpawnFx(GetWorld(), Pos + FVector(0.f, 0.f, 40.f), EVHFxShape::Sphere, FVector(Radius * 0.5f), Color, 0.6f, 0.45f))
	{
		Ball->SetGrow(3.f);
	}
}

void UVHServerGameSubsystem::ApplyFxState(FEntity& E)
{
	AVHFxActor* Fx = Cast<AVHFxActor>(E.Fx.Get());
	if (!Fx || E.Kind != EntityKind::Mine)
	{
		return;
	}
	if (UVHWeaponFxSubsystem* WeaponFx = UVHWeaponFxSubsystem::Get(this))
	{
		WeaponFx->ApplyMineState(Fx, E.State);	// 3D 모델 + 경고 링 (감지 100cm, 감지되면 폭발 300cm)
		return;
	}
	// 지뢰: 설치 후 3초는 흐리게(작동 전) → 작동 중 진하게 → 감지되면 0.5초 동안 빠르게 깜빡인 뒤 폭발(SC_EXPLOSION)
	const bool bShock = static_cast<WeaponId>(E.TypeId) == WeaponId::ShockMine;
	const FLinearColor Color = UVHSettings::Get().GetWeaponCatalog().PlacedGlow;	// 지뢰 = 설치 분류 색(라임). 폭발·감전은 모양(구상안: 원 / 육각)으로 구분
	(void)bShock;
	if (E.State & ES_TRIGGERED)
	{
		Fx->SetFxColor(Color, 1.f);
		Fx->SetPulse(8.f);
	}
	else if (E.State & ES_ARMED)
	{
		Fx->SetFxColor(Color, 0.9f);
		Fx->SetPulse(0.f);
	}
	else
	{
		Fx->SetFxColor(Color, 0.4f);
		Fx->SetPulse(1.f);
	}
}

float UVHServerGameSubsystem::GetRespawnRemaining() const
{
	return RespawnAtSeconds > 0.0 ? FMath::Max(0.f, static_cast<float>(RespawnAtSeconds - FPlatformTime::Seconds())) : 0.f;
}

FVHItemData UVHServerGameSubsystem::ItemForValuable(uint16 Quality, uint32 Value)
{
	// 서버 v3.0: 환금 아이템은 품질과 가치만 있다(이름 없음). 유일(Unique) = 보물
	FVHItemData Item;
	Item.Value = static_cast<int32>(Value);
	switch (static_cast<ValuableQuality>(Quality))
	{
	case ValuableQuality::Unique:
		Item.Kind = EVHItemKind::Treasure;
		Item.DisplayName = LOCTEXT("ValTreasure", "보물");
		break;
	case ValuableQuality::Rare:
		Item.Kind = EVHItemKind::Vase;
		Item.Quality = EVHItemQuality::Rare;
		Item.DisplayName = LOCTEXT("Valuable", "환금 아이템");
		break;
	case ValuableQuality::Advanced:
		Item.Kind = EVHItemKind::Vase;
		Item.Quality = EVHItemQuality::Advanced;
		Item.DisplayName = LOCTEXT("Valuable", "환금 아이템");
		break;
	default:
		Item.Kind = EVHItemKind::Vase;
		Item.Quality = EVHItemQuality::Common;
		Item.DisplayName = LOCTEXT("Valuable", "환금 아이템");
		break;
	}
	return Item;
}

// ───────────────────────── 받기 ─────────────────────────

void UVHServerGameSubsystem::HandlePacket(uint16 Type, const uint8* Data, int32 Size)
{
	switch (static_cast<PacketId>(Type))
	{
	case PacketId::SC_GAME_START:
	{
		SC_GameStart P;
		if (VHPacket::ReadVariable(Data, Size, P, MAX_ENTITIES, [](const SC_GameStart& X) { return X.entity_count; },
			[](uint8 C) { return SC_GameStart_Size(C); }))
		{
			ApplyGameStart(P);
		}
		break;
	}
	case PacketId::SC_SNAPSHOT:
	{
		SC_Snapshot P;
		if (VHPacket::ReadVariable(Data, Size, P, MAX_ENTITIES, [](const SC_Snapshot& X) { return X.entity_count; },
			[](uint8 C) { return SC_Snapshot_Size(C); }))
		{
			ApplySnapshot(P);
		}
		else
		{
			UE_LOG(LogVHServerGame, Error, TEXT("SC_SNAPSHOT 크기 오류 %d"), Size);
		}
		break;
	}
	case PacketId::SC_ENTITY_SPAWN:
	{
		SC_EntitySpawn P;
		if (VHPacket::ReadVariable(Data, Size, P, MAX_ENTITIES, [](const SC_EntitySpawn& X) { return X.count; },
			[](uint8 C) { return SC_EntitySpawn_Size(C); }))
		{
			for (int32 i = 0; i < P.count; ++i)
			{
				AddEntity(P.entities[i], false);
			}
		}
		break;
	}
	case PacketId::SC_ENTITY_DESPAWN:
	{
		SC_EntityDespawn P;
		if (VHPacket::ReadVariable(Data, Size, P, MAX_ENTITIES, [](const SC_EntityDespawn& X) { return X.count; },
			[](uint8 C) { return SC_EntityDespawn_Size(C); }))
		{
			for (int32 i = 0; i < P.count; ++i)
			{
				RemoveEntity(P.entity_ids[i]);
			}
		}
		break;
	}
	default:
		HandleEvent(Type, Data, Size);
		break;
	}
}

void UVHServerGameSubsystem::ApplyGameStart(const SC_GameStart& P)
{
	if (bStarted)
	{
		return;	// 레벨 시작 때 저장분으로 이미 적용함
	}
	SelfEntityId = P.self_entity_id;
	DurationSeconds = P.duration_ms / 1000.f;
	for (int32 i = 0; i < P.entity_count; ++i)
	{
		AddEntity(P.entities[i], true);
	}
	bStarted = true;
	ApplyPlayerColors();

	int32 CountByKind[16] = {};
	for (const TPair<uint16, FEntity>& Pair : Entities)
	{
		++CountByKind[FMath::Clamp<int32>(static_cast<int32>(Pair.Value.Kind), 0, 15)];
	}
	UE_LOG(LogVHServerGame, Log, TEXT("SC_GAME_START 적용: 내 엔티티 %u, 제한 %.0f초, 엔티티 %d개 (플레이어 %d, 경비원 %d, 금고 %d, 반납지점 %d, 상자 %d, 필드아이템 %d)"),
		SelfEntityId, DurationSeconds, P.entity_count,
		CountByKind[(int32)EntityKind::Thief], CountByKind[(int32)EntityKind::Guard], CountByKind[(int32)EntityKind::Safe],
		CountByKind[(int32)EntityKind::ReturnPoint], CountByKind[(int32)EntityKind::Box], CountByKind[(int32)EntityKind::FieldItem]);

	if (UVHMatchSubsystem* Match = GetMatch())
	{
		// 제한 시간은 서버 값 (v3.0 = 5분)
		Match->ApplyServerTime(DurationSeconds, DurationSeconds);
		// SC_MONEY 는 금액이 바뀔 때만 온다 → 순위표에 처음부터 전원이 보이도록 0원으로 채운다
		TArray<FVHServerMoneyRow> Rows;
		for (const TPair<uint16, FEntity>& Pair : Entities)
		{
			if (Pair.Value.Kind == EntityKind::Thief)
			{
				FVHServerMoneyRow& Row = Rows.AddDefaulted_GetRef();
				Row.EntityId = Pair.Key;
				Row.Name = NameForPlayer(Pair.Value.Owner);
				Row.bIsMe = Pair.Key == SelfEntityId;
			}
		}
		Match->ApplyServerMoney(Rows);
	}
	if (UVHTreasureCarrierComponent* Carrier = GetMyCarrier())
	{
		Carrier->ApplyServerCarried(0);
	}
}

void UVHServerGameSubsystem::ApplyPlayerColors()
{
	TArray<uint16> Thieves;
	for (const TPair<uint16, FEntity>& Pair : Entities)
	{
		if (Pair.Value.Kind == EntityKind::Thief)
		{
			Thieves.Add(Pair.Key);
		}
	}
	Thieves.Sort();
	for (int32 i = 0; i < Thieves.Num(); ++i)
	{
		const FEntity& E = Entities[Thieves[i]];
		if (Thieves[i] == SelfEntityId)
		{
			UVHWeaponVisualComponent* Visual = GetLocalPawn() ? GetLocalPawn()->FindComponentByClass<UVHWeaponVisualComponent>() : nullptr;
			if (Visual)
			{
				Visual->SetPlayerColorIndex(i);
			}
			else if (UWorld* World = GetWorld())
			{
				// 레벨 시작 직후라 내 캐릭터가 아직 없으면 잠시 뒤 다시
				FTimerHandle Retry;
				World->GetTimerManager().SetTimer(Retry, FTimerDelegate::CreateWeakLambda(this, [this]() { ApplyPlayerColors(); }), 0.5f, false);
			}
		}
		else if (E.Proxy.IsValid())
		{
			E.Proxy->SetPlayerColorIndex(i);
		}
	}
	UE_LOG(LogVHServerGame, Log, TEXT("[플레이어 색] 의적 %d명에 색 지정 (엔티티 번호 순서)"), Thieves.Num());
}

void UVHServerGameSubsystem::AddEntity(const EntitySpawn& S, bool bFromStart)
{
	FEntity& E = Entities.FindOrAdd(S.entity_id);
	E.Id = S.entity_id;
	E.Kind = S.kind;
	E.TypeId = S.type_id;
	E.Owner = S.owner_player_id;
	E.Pos = ToVector(S.pos);
	E.Yaw = S.yaw;
	E.State = S.state;
	E.TimerMs = S.timer_ms;
	E.Param = S.param;

	UE_LOG(LogVHServerGame, Log, TEXT("엔티티 %s %u: %s type %u 위치 (%.0f, %.0f, %.0f) 상태 0x%06x timer %ums param %u%s"),
		bFromStart ? TEXT("시작") : TEXT("등장"), E.Id, KindName(E.Kind), E.TypeId, E.Pos.X, E.Pos.Y, E.Pos.Z, E.State, E.TimerMs, E.Param,
		E.Id == SelfEntityId ? TEXT(" (나)") : TEXT(""));

	UVHInteractionSubsystem* Sub = GetInteraction();
	switch (E.Kind)
	{
	case EntityKind::Thief:
		if (E.Id == SelfEntityId)
		{
			// 내 캐릭터: 서버가 정한 시작 위치로 (발 위치 + 캡슐 반높이)
			if (APawn* Pawn = GetLocalPawn())
			{
				Pawn->TeleportTo(E.Pos + FVector(0.f, 0.f, GetPawnHalfHeight(Pawn) + 2.f), FRotator(0.f, E.Yaw, 0.f));
				LastSentPos = E.Pos;
			}
			if (UVHThiefStateComponent* Me = GetMyThief())
			{
				Me->SetEntityId(E.Id);
			}
			SelfState = E.State;
			break;
		}
		[[fallthrough]];
	case EntityKind::Guard:
		if (!E.Proxy.IsValid())
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (AVHNetProxy* Proxy = GetWorld()->SpawnActor<AVHNetProxy>(AVHNetProxy::StaticClass(), E.Pos, FRotator(0.f, E.Yaw, 0.f), Params))
			{
				const FString Name = E.Kind == EntityKind::Thief ? NameForPlayer(E.Owner) : FString::Printf(TEXT("Guard %u"), E.Id);
				Proxy->InitProxy(E.Id, static_cast<uint8>(E.Kind), Name, E.Kind == EntityKind::Thief ? CharacterForPlayer(E.Owner) : 0);
				Proxy->SnapTo(E.Pos, E.Yaw);
				Proxy->PushSnapshot(FPlatformTime::Seconds(), E.Pos, E.Yaw, E.State, E.Hp, static_cast<uint8>(E.Weapon));
				E.Proxy = Proxy;
			}
		}
		break;

	case EntityKind::Safe:
		// 금고: 레벨에 놓인 금고 후보와 위치로 짝짓는다 (서버는 한 번에 1개만 둔다)
		if (bSafesHidden)
		{
			BindSafe(E);
		}
		break;

	case EntityKind::ReturnPoint:
		// 반납지점: 서버가 정한 자리에 만든다 (type_id = 방 번호, timer_ms = 남은 수명)
		if (!E.Object.IsValid() && Sub)
		{
			const UVHGameplayConfig& G = UVHSettings::Get().GetGameplayConfig();
			UClass* Class = G.ReturnPointClass.LoadSynchronous();
			const FTransform Transform(FRotator::ZeroRotator, E.Pos);
			AVHReturnPoint* Point = GetWorld()->SpawnActorDeferred<AVHReturnPoint>(Class ? Class : AVHReturnPoint::StaticClass(),
				Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Point)
			{
				// 남은 수명으로 등장 시각을 거꾸로 계산해 화면의 남은 시간이 서버와 같게 한다
				const double Age = FMath::Max(0.0, static_cast<double>(G.ReturnPointLifetimeSeconds) - E.TimerMs / 1000.0);
				Point->InitPoint(Sub->GetNow() - (E.TimerMs > 0 ? Age : 0.0));
				Point->FinishSpawning(Transform);
#if WITH_EDITOR
				Point->SetActorLabel(FString::Printf(TEXT("RP_%u"), E.Id));	// 개발 명령용 이름 (VHUse RP_12)
#endif
				E.Object = Point;
				if (UVHMatchSubsystem* Match = GetMatch())
				{
					Match->AddServerReturnPoint(Point);
				}
				ApplyObjectState(E, 0);
			}
		}
		break;

	case EntityKind::Valuable:
		// 환금 아이템 (금고에서 나온 보물, 쓰러진 플레이어가 떨어뜨린 것, 상자에서 나온 것). type_id = 품질, param = 가치
		if (!E.Object.IsValid() && Sub)
		{
			// 새 맵: 서버는 보물을 금고와 같은 자리(금고 중심)에 만든다 → 금고 몸통에 묻히지 않게 문 앞에 보여 준다.
			// 줍는 거리(3m)는 서버가 원래 자리에서 재므로, 문 앞 90cm는 그 안이다
			FVector ShowPos = E.Pos + FVector(0.f, 0.f, 10.f);
			for (const TPair<uint16, FEntity>& Pair : Entities)
			{
				const AVHSafe* Safe = Pair.Value.Kind == EntityKind::Safe ? Cast<AVHSafe>(Pair.Value.Object.Get()) : nullptr;
				if (Safe && Safe->ActorHasTag(ServerSpawnedTag) && FVector::Dist2D(Pair.Value.Pos, E.Pos) < 30.f)
				{
					ShowPos = Safe->GetActorLocation() + Safe->GetActorForwardVector() * 90.f + FVector(0.f, 0.f, 10.f);
					break;
				}
			}
			E.Object = Sub->SpawnDroppedTreasure(ShowPos, ItemForValuable(E.TypeId, E.Param));
		}
		// 방금 내가 쓰러진 자리 근처에 생긴 것 = 내가 떨어뜨린 것 (서버는 80cm 안에 흩뿌린다)
		if (!bFromStart && DownSeconds > 0.0 && FPlatformTime::Seconds() - DownSeconds < 3.0 && FVector::Dist2D(E.Pos, DownPos) < 300.f)
		{
			if (static_cast<ValuableQuality>(E.TypeId) == ValuableQuality::Unique && DownExpectTreasure > 0)
			{
				MyDroppedTreasureId = E.Id;
				MyDroppedTreasureValue = static_cast<int32>(E.Param);
				UE_LOG(LogVHServerGame, Log, TEXT("[쓰러짐] 떨어뜨린 보물 엔티티 %u: 가치 %u$ (들고 있던 보물 %d$%s), 쓰러진 자리에서 %.0fcm"), E.Id, E.Param, DownExpectTreasure,
					static_cast<int32>(E.Param) == DownExpectTreasure ? TEXT(", 같음") : TEXT(", 다름!"), FVector::Dist2D(E.Pos, DownPos));
				DownExpectTreasure = 0;
			}
			else if (static_cast<ValuableQuality>(E.TypeId) != ValuableQuality::Unique)
			{
				++DownSeenValuables;
			}
		}
		break;

	case EntityKind::Box:
		// 상자: 서버 위치에 만든다 (HP·작업·드롭은 서버가 판정, 부서지면 SC_ENTITY_DESPAWN)
		if (!E.Object.IsValid())
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (AVHBox* Box = GetWorld()->SpawnActor<AVHBox>(AVHBox::StaticClass(), E.Pos, FRotator(0.f, E.Yaw, 0.f), Params))
			{
				E.Object = Box;
			}
		}
		break;

	case EntityKind::WeaponPickup:
	case EntityKind::ConsumablePickup:
	case EntityKind::FieldItem:
		// 바닥의 무기·소비 아이템(F로 줍기), 필드 아이템(닿으면 획득 — 서버가 처리, 클라는 표시만)
		if (!E.Object.IsValid())
		{
			const FTransform Transform(FRotator(0.f, E.Yaw, 0.f), E.Pos);
			AVHItemPickup* Item = GetWorld()->SpawnActorDeferred<AVHItemPickup>(AVHItemPickup::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Item)
			{
				const EVHEntityKind LocalKind = E.Kind == EntityKind::WeaponPickup ? EVHEntityKind::WeaponPickup
					: (E.Kind == EntityKind::ConsumablePickup ? EVHEntityKind::ConsumablePickup : EVHEntityKind::FieldItem);
				Item->InitPickup(LocalKind, E.TypeId, static_cast<int32>(E.Param));
				Item->FinishSpawning(Transform);
				E.Object = Item;
			}
		}
		break;

	case EntityKind::StickyTrap:
		// 끈끈이 함정: 바닥에 반경 3m 원판. 위에 선 플레이어·경비원은 느려진다(서버가 ES_SLOWED 로 알려 준다)
		if (!E.Fx.IsValid())
		{
			const UVHGameplayConfig& G = UVHSettings::Get().GetGameplayConfig();
			if (AVHFxActor* Fx = AVHFxActor::SpawnFx(GetWorld(), E.Pos + FVector(0.f, 0.f, 3.f), EVHFxShape::Disc,
				FVector(StickyRadiusCm * 2.f), G.StickyTrapColor, 0.45f))
			{
				if (E.TimerMs > 0)
				{
					Fx->SetLife(E.TimerMs / 1000.f, 1.f);	// 마지막 1초 동안 흐려진다 (없애는 것은 SC_ENTITY_DESPAWN)
				}
				E.Fx = Fx;
			}
		}
		break;

	case EntityKind::Mine:
		// 지뢰(폭발·감전): 바닥의 작은 원기둥. 상태(작동·감지)에 따라 표시가 바뀐다 → ApplyFxState
		if (!E.Fx.IsValid())
		{
			UVHWeaponFxSubsystem* WeaponFx = UVHWeaponFxSubsystem::Get(this);
			if (AVHFxActor* Fx = WeaponFx ? WeaponFx->SpawnMine(static_cast<WeaponId>(E.TypeId) == WeaponId::ShockMine, E.Pos, E.Yaw)
				: AVHFxActor::SpawnFx(GetWorld(), E.Pos + FVector(0.f, 0.f, 5.f), EVHFxShape::Cylinder, FVector(44.f, 44.f, 10.f), FLinearColor::Red, 0.4f))
			{
				E.Fx = Fx;
				ApplyFxState(E);
			}
		}
		break;

	case EntityKind::Smoke:
		// 연막: 반경 5m 원기둥. 안에 있는 대상은 화면에서 숨긴다(AVHNetProxy, ES_IN_SMOKE)
		if (!E.Fx.IsValid())
		{
			if (UVHWeaponFxSubsystem* WeaponFx = UVHWeaponFxSubsystem::Get(this))
			{
				// 회색 연기(나이아가라) + 연막 반지름 그대로의 옅은 바닥 원 [무기 이펙트 표]
				E.Fx = WeaponFx->SpawnSmoke(E.Pos, SmokeRadiusCm, E.TimerMs / 1000.f);
				break;
			}
			const UVHGameplayConfig& G = UVHSettings::Get().GetGameplayConfig();
			if (AVHFxActor* Fx = AVHFxActor::SpawnFx(GetWorld(), E.Pos + FVector(0.f, 0.f, 130.f), EVHFxShape::Cylinder,
				FVector(SmokeRadiusCm * 2.f, SmokeRadiusCm * 2.f, 260.f), G.SmokeColor, G.SmokeOpacity))
			{
				if (E.TimerMs > 0)
				{
					Fx->SetLife(E.TimerMs / 1000.f, 1.5f);
				}
				E.Fx = Fx;
			}
		}
		break;

	default:
		break;
	}

	// 배치 점검(로그만): 서버가 고른 자리가 클라 레벨에서도 방 안의 빈 바닥인지.
	// 서버는 가구를 상자(AABB)로, 클라는 메시 모양으로 막으므로 드물게 어긋날 수 있다 → 어긋난 자리는 좌표를 모아 서버 팀에 알린다
	if (bFromStart || E.Kind == EntityKind::Safe || E.Kind == EntityKind::Box || E.Kind == EntityKind::ReturnPoint || E.Kind == EntityKind::FieldItem)
	{
		const bool bPlaced = E.Kind == EntityKind::Safe || E.Kind == EntityKind::Box || E.Kind == EntityKind::ReturnPoint
			|| E.Kind == EntityKind::FieldItem || E.Kind == EntityKind::Thief || E.Kind == EntityKind::Guard;
		if (bPlaced)
		{
			// 레벨에 놓인 벽·가구만 본다(게임 중에 생긴 상자·아이템끼리 가까운 것은 서버가 정한 것이라 세지 않는다)
			const bool bBlocked = IsBlockedByLevel(GetWorld(), E.Pos + FVector(0.f, 0.f, 70.f), 25.f);
			const AVHRoomVolume* Room = AVHRoomVolume::FindRoomAt(GetWorld(), E.Pos);
			++PlaceChecks;
			if (bBlocked || !Room)
			{
				++PlaceCheckWarnings;
				UE_LOG(LogVHServerGame, Warning, TEXT("[배치] %s %u (%.0f, %.0f): %s%s"), KindName(E.Kind), E.Id, E.Pos.X, E.Pos.Y,
					bBlocked ? TEXT("레벨의 벽·가구와 겹침 ") : TEXT(""), Room ? *FString::Printf(TEXT("(방 %s)"), *Room->GetRoomLabel()) : TEXT("방 영역 밖"));
			}
			else
			{
				UE_LOG(LogVHServerGame, Verbose, TEXT("[배치] %s %u (%.0f, %.0f) 방 %s"), KindName(E.Kind), E.Id, E.Pos.X, E.Pos.Y, *Room->GetRoomLabel());
			}
		}
	}
}

void UVHServerGameSubsystem::RemoveEntity(uint16 Id)
{
	FEntity* E = Entities.Find(Id);
	if (!E)
	{
		return;
	}
	UE_LOG(LogVHServerGame, Log, TEXT("엔티티 사라짐 %u: %s"), Id, KindName(E->Kind));
	CancelRailgunShot(Id, TEXT("쏜 사람이 사라짐"));
	if (Id == MyDroppedTreasureId)
	{
		// 바닥의 내 보물을 누가 주웠다 → 미니맵·방향 표시를 지운다 (누가 주웠는지는 SC_TREASURE_TAKEN 알림·배너가 이미 알림)
		const bool bByOther = !DroppedTreasureTaker.IsEmpty();
		UE_LOG(LogVHServerGame, Log, TEXT("[쓰러짐] 떨어뜨린 보물 %u 사라짐: %s"), Id, bByOther ? *FString::Printf(TEXT("%s이(가) 주움"), *DroppedTreasureTaker) : TEXT("내가 주움"));
		MyDroppedTreasureId = 0;
		MyDroppedTreasureValue = 0;
		DroppedHintUntil = 0.0;
		DroppedTreasureTaker.Reset();
	}
	if (AVHNetProxy* Proxy = E->Proxy.Get())
	{
		Proxy->Destroy();
	}
	if (AActor* Fx = E->Fx.Get())
	{
		Fx->Destroy();
	}
	if (AVHInteractableBase* Object = E->Object.Get())
	{
		if (AVHReturnPoint* Point = Cast<AVHReturnPoint>(Object))
		{
			if (UVHMatchSubsystem* Match = GetMatch())
			{
				Match->RemoveServerReturnPoint(Point);
			}
			Point->Destroy();
		}
		else if (AVHSafe* Safe = Cast<AVHSafe>(Object))
		{
			// 보물을 가져가면 금고가 사라진다 → 레벨 금고를 숨긴다 (다음 금고는 다른 자리에 새 엔티티로 온다)
			if (Safe->ActorHasTag(ServerSpawnedTag))
			{
				Safe->Destroy();	// 서버 위치에 만든 금고(새 맵)는 없앤다
			}
			else
			{
				SetLevelSafeActive(Safe, false);
			}
		}
		else
		{
			Object->Destroy();	// 떨어진 환금 아이템: 누군가 주움
		}
	}
	if (Pending.TargetId == Id)
	{
		Pending = FPending();
	}
	Entities.Remove(Id);
}

// ───────────────────────── 레벨 금고 ─────────────────────────

void UVHServerGameSubsystem::SetLevelSafeActive(AVHSafe* Safe, bool bActiveNow)
{
	if (!Safe)
	{
		return;
	}
	UVHInteractionSubsystem* Sub = GetInteraction();
	if (bActiveNow)
	{
		Safe->ResetSafe();	// 닫힘·진행도 0 (새 금고)
	}
	Safe->SetActorHiddenInGame(!bActiveNow);
	Safe->SetActorEnableCollision(bActiveNow);
	if (Sub)
	{
		if (bActiveNow)
		{
			Sub->RegisterInteractable(Safe);
		}
		else
		{
			Sub->UnregisterInteractable(Safe);
		}
	}
}

void UVHServerGameSubsystem::HideLevelSafes()
{
	// 서버 v3.0: 금고는 한 번에 1개. 레벨의 금고 후보(10곳)를 모두 숨기고, 서버가 보낸 금고만 보이게 한다
	int32 Count = 0;
	for (TActorIterator<AVHSafe> It(GetWorld()); It; ++It)
	{
		SetLevelSafeActive(*It, false);
		++Count;
	}
	// 레벨에 직접 놓아 둔 반납지점·떨어진 보물(테스트용)은 서버가 모르므로 숨긴다
	UVHInteractionSubsystem* Sub = GetInteraction();
	for (TActorIterator<AVHInteractableBase> It(GetWorld()); It; ++It)
	{
		if ((Cast<AVHReturnPoint>(*It) || Cast<AVHDroppedTreasure>(*It)) && FindEntityIdFor(*It) == 0)
		{
			It->SetActorHiddenInGame(true);
			It->SetActorEnableCollision(false);
			if (Sub)
			{
				Sub->UnregisterInteractable(*It);
			}
		}
	}
	bSafesHidden = true;
	LevelSafeCount = Count;
	UE_LOG(LogVHServerGame, Log, TEXT("레벨 금고 후보 %d곳 숨김 (서버가 보낸 금고만 표시%s)"), Count,
		Count == 0 ? TEXT(", 레벨에 금고 자리가 없어 서버 위치에 만든다") : TEXT(""));
	for (TPair<uint16, FEntity>& Pair : Entities)
	{
		if (Pair.Value.Kind == EntityKind::Safe)
		{
			BindSafe(Pair.Value);
		}
	}
	bAnnounceNewSafe = true;	// 첫 금고는 알리지 않고, 그 뒤에 생기는 금고만 알린다
}

void UVHServerGameSubsystem::BindSafe(FEntity& E)
{
	if (E.Object.IsValid())
	{
		return;
	}
	// 이미 다른 서버 금고와 짝지은 레벨 금고는 뺀다
	TSet<const AVHInteractableBase*> Used;
	for (const TPair<uint16, FEntity>& Pair : Entities)
	{
		if (Pair.Value.Kind == EntityKind::Safe && Pair.Value.Object.IsValid())
		{
			Used.Add(Pair.Value.Object.Get());
		}
	}
	AVHSafe* Best = nullptr;
	float BestDist = MatchDistance;
	for (TActorIterator<AVHSafe> It(GetWorld()); It; ++It)
	{
		const float D = FVector::Dist2D(It->GetActorLocation(), E.Pos);
		if (!Used.Contains(*It) && D < BestDist)
		{
			Best = *It;
			BestDist = D;
		}
	}
	if (!Best && LevelSafeCount == 0)
	{
		SpawnServerSafe(E);	// 새 맵: 금고 자리가 레벨에 없다
		return;
	}
	if (!Best)
	{
		// 서버 맵 파일이 클라 배치와 다르다 (예: 서버가 옛 배치 v1)
		const uint32 ServerMapVersion = Net.IsValid() ? Net->GetGameLoadInfo().MapVersion : 0;
		UE_LOG(LogVHServerGame, Warning, TEXT("금고 짝짓기 실패: 서버 금고 %u (%.0f, %.0f) 근처 %.0fcm 안에 레벨 금고 없음 (서버 맵 v%u)"),
			E.Id, E.Pos.X, E.Pos.Y, MatchDistance, ServerMapVersion);
		if (UVHNetSubsystem* N = Net.Get())
		{
			N->ShowToast(FText::FromString(FString::Printf(
				TEXT("서버 맵(v%u)이 클라 배치(v2)와 다릅니다: 금고 자리를 레벨에서 찾지 못했습니다. 서버 PC에 Docs/ServerHandoff/mansion_1f_server.json 을 넣고 서버를 다시 켜 주세요."),
				ServerMapVersion)), true);
		}
		return;
	}
	E.Object = Best;
	SetLevelSafeActive(Best, true);
	UE_LOG(LogVHServerGame, Log, TEXT("금고 %u ↔ %s (%.0fcm)"), E.Id, *Best->GetActorNameOrLabel(), BestDist);
	if (UVHInteractionSubsystem* Sub = GetInteraction(); Sub && bAnnounceNewSafe)
	{
		Sub->PostMessage(LOCTEXT("NewSafe", "새 금고가 나타났습니다"));
	}
	ApplyObjectState(E, 0);
}

void UVHServerGameSubsystem::SpawnServerSafe(FEntity& E)
{
	UWorld* World = GetWorld();
	if (!World || E.Object.IsValid())
	{
		return;
	}
	const UVHGameplayConfig& G = UVHSettings::Get().GetGameplayConfig();
	UClass* Class = G.SafeClass.LoadSynchronous();
	if (!Class)
	{
		Class = AVHSafe::StaticClass();
	}
	// 문(앞면)이 벽·가구에 막히지 않는 방향을 고른다. 서버의 yaw는 연출용(0·90·180·270 무작위)이고 작업 거리는
	// 금고 중심에서 재므로, 클라가 방향을 바꿔도 판정은 그대로다 [정호님 확인 2026-11-16].
	// 방향은 금고 위치·서버 yaw·레벨만으로 정한다 → 모든 클라에서 같다 (ChooseSafeYaw 설명 참고)
	FString Checked;
	const float Yaw = ChooseSafeYaw(World, E.Pos, E.Yaw, &Checked);
	// 자가 점검: 금고 자리의 바닥은 레벨 메시이므로 반드시 "막힘"으로 나와야 한다(0이면 레벨 감지가 안 되는 것)
	const bool bFloorSeen = IsBlockedByLevel(World, FVector(E.Pos.X, E.Pos.Y, E.Pos.Z), 25.f);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AVHSafe* Safe = World->SpawnActor<AVHSafe>(Class, E.Pos, FRotator(0.f, Yaw, 0.f), Params);
	if (!Safe)
	{
		UE_LOG(LogVHServerGame, Warning, TEXT("금고 %u 를 만들지 못함 (%.0f, %.0f)"), E.Id, E.Pos.X, E.Pos.Y);
		return;
	}
	Safe->Tags.AddUnique(ServerSpawnedTag);
	Safe->SetWorkFromAnySide(true);
#if WITH_EDITOR
	Safe->SetActorLabel(FString::Printf(TEXT("Safe_%u"), E.Id));	// 개발 명령용 이름
#endif
	E.Object = Safe;
	const AVHRoomVolume* Room = AVHRoomVolume::FindRoomAt(World, E.Pos);
	UE_LOG(LogVHServerGame, Log, TEXT("금고 %u 생성: 서버 위치 (%.0f, %.0f) 방 %s, 문 방향 %.0f (서버 %.0f) | 문 앞 검사:%s, 바닥 감지 %d"), E.Id, E.Pos.X, E.Pos.Y,
		Room ? *Room->GetRoomLabel() : TEXT("?"), Yaw, FRotator::NormalizeAxis(E.Yaw), *Checked, bFloorSeen ? 1 : 0);
	if (UVHInteractionSubsystem* Sub = GetInteraction(); Sub && bAnnounceNewSafe)
	{
		Sub->PostMessage(LOCTEXT("NewSafe", "새 금고가 나타났습니다"));
	}
	ApplyObjectState(E, 0);
}

void UVHServerGameSubsystem::ApplyObjectState(FEntity& E, uint32 OldState)
{
	UVHInteractionSubsystem* Sub = GetInteraction();
	AVHInteractableBase* Object = E.Object.Get();
	if (!Sub || !Object)
	{
		return;
	}
	if (AVHSafe* Safe = Cast<AVHSafe>(Object))
	{
		// 열림: SC_SAFE_OPENED 로도 오지만, 늦게 들어온 경우를 위해 상태 비트도 본다.
		// 보물은 금고 앞에 따로 생기는 환금 아이템이므로 금고 안은 비운 모습으로 둔다
		if ((E.State & ES_OPENED) && Safe->GetSafeState() == EVHSafeState::Closed)
		{
			Safe->ApplyOpened();
			Safe->ApplyTreasureTaken();
		}
	}
	else if (AVHReturnPoint* Point = Cast<AVHReturnPoint>(Object))
	{
		// 빨강(곧 사라짐) = 서버 Warning 비트 그대로 (2026-11-22 팀 서버: 등장 55초 = 사라지기 5초 전). 켜지는 순간 남은 시간도 서버에 맞춘다
		Point->SetServerWarning((E.State & ES_WARNING) != 0);
	}
}

void UVHServerGameSubsystem::ApplySelfState(uint32 OldState, uint32 NewState)
{
	UE_LOG(LogVHServerGame, Log, TEXT("내 상태 비트 0x%06x → 0x%06x"), OldState, NewState);
	UVHInteractionSubsystem* Sub = GetInteraction();
	UVHThiefStateComponent* Me = GetMyThief();
	APawn* Pawn = GetLocalPawn();

	// 쓰러짐(리스폰 대기): 안 보이고 못 움직인다. 풀리면(리스폰) 다시 자유
	const bool bWasDown = (OldState & ES_DOWNED) != 0;
	const bool bDown = (NewState & ES_DOWNED) != 0;
	if (Sub && Me && bWasDown != bDown)
	{
		Sub->ApplyThiefStatus(Me, bDown ? EVHThiefStatus::Dead : EVHThiefStatus::Free);
		if (!bDown)
		{
			RespawnAtSeconds = 0.0;
			if (UVHMatchSubsystem* Match = GetMatch())
			{
				Match->ClearRespawn(Me);
			}
		}
	}
	// 경직: 서버는 경직 중에도 이동 패킷을 받으므로 클라가 스스로 멈춘다 (서버 GameData StunMs = 2초, 면역 시간 없음)
	if ((NewState & ES_STUNNED) && !(OldState & ES_STUNNED) && Pawn)
	{
		if (UVHStatusEffectComponent* Effects = Pawn->FindComponentByClass<UVHStatusEffectComponent>())
		{
			Effects->ApplyStun(StunSeconds);
		}
		if (UVHMatchSubsystem* Match = GetMatch())
		{
			Match->PushNotice(LOCTEXT("Stunned", "경직! 2초 동안 움직일 수 없습니다"));
		}
	}

	// 몸 동작: 금고·상자 작업(무릎 꿇고 살피기), 경직(감전) — 다른 플레이어 표시와 같은 코드
	if (Pawn)
	{
		if (UVHWeaponVisualComponent* Body = Pawn->FindComponentByClass<UVHWeaponVisualComponent>())
		{
			const bool bDownNow = (NewState & ES_DOWNED) != 0;
			Body->SetBodyStates((NewState & ES_INTERACTING) != 0 && !bDownNow, (NewState & ES_STUNNED) != 0 && !bDownNow);
		}
	}
	// 끈끈이(−50%)·은신(−80%, 달리기·구르기 불가): 속도는 클라가 적용한다
	UpdateSelfSpeed();
	const uint32 Changed = OldState ^ NewState;
	// 시험 확인용: 감속·은신 동안 실제로 움직인 평균 속도를 끝날 때 로그로 남긴다
	static double SlowSince = 0.0, CloakSince = 0.0;
	static FVector SlowFrom = FVector::ZeroVector, CloakFrom = FVector::ZeroVector;
	auto AverageSpeed = [Pawn](double Since, const FVector& From) -> FString
	{
		const double Elapsed = FPlatformTime::Seconds() - Since;
		return Pawn && Elapsed > 0.05
			? FString::Printf(TEXT("%.1f초 동안 %.0fcm 이동 = 평균 %.0f cm/s"), Elapsed, FVector::Dist2D(From, Pawn->GetActorLocation()), FVector::Dist2D(From, Pawn->GetActorLocation()) / Elapsed)
			: FString();
	};
	if (Changed & ES_SLOWED)
	{
		if (NewState & ES_SLOWED)
		{
			SlowSince = FPlatformTime::Seconds();
			SlowFrom = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
		}
		UE_LOG(LogVHServerGame, Log, TEXT("[상태] 나: 끈끈이 감속 %s %s"), (NewState & ES_SLOWED) ? TEXT("시작") : TEXT("끝"),
			(NewState & ES_SLOWED) ? TEXT("") : *AverageSpeed(SlowSince, SlowFrom));
		if (UVHMatchSubsystem* Match = GetMatch(); Match && (NewState & ES_SLOWED))
		{
			Match->PushNotice(LOCTEXT("Slowed", "끈끈이! 함정 위에서는 이동 속도가 절반입니다"));
		}
	}
	// 내 캐릭터 모습: 은신 = 하늘색 반투명, 연막 안 = 회색 윤곽 (다른 사람 화면에서는 둘 다 아예 숨겨진다)
	SetSelfGhost((NewState & ES_CLOAKED) ? 1 : ((NewState & ES_IN_SMOKE) ? 2 : 0));
	if (Changed & ES_IN_SMOKE)
	{
		UE_LOG(LogVHServerGame, Log, TEXT("[상태] 나: 연막 %s"), (NewState & ES_IN_SMOKE) ? TEXT("안으로") : TEXT("밖으로"));
	}
	// 섬광: 화면이 새하얗게 된 뒤 3초에 걸쳐 돌아온다 (HUD가 GetFlashAlpha 로 그린다). 그동안 미니맵도 숨긴다
	if ((Changed & ES_FLASHED) && (NewState & ES_FLASHED))
	{
		FlashStartSeconds = FPlatformTime::Seconds();
		UE_LOG(LogVHServerGame, Log, TEXT("[상태] 나: 섬광! 화면이 하얗게 → %.0f초에 걸쳐 회복"), UVHSettings::Get().GetGameplayConfig().FlashFadeSeconds);
	}
	// 사제 테이저건을 든 상태 (서버 판정, 2026-11-22). 보통은 SC_SKILL 이 먼저 와서 이미 맞춰져 있다
	if (Changed & ES_TASER_READY)
	{
		SetSkillTaserHeld((NewState & ES_TASER_READY) != 0, TEXT("서버 상태 비트 TaserReady"));
	}
	// 레일건 충전 1초 + 발사 1초: 이동·회전 불가 (서버는 이 동안의 구르기·스킬·소비 아이템·무기 교체를 Locked 로 거절한다)
	if (Changed & ES_RAILGUN_LOCK)
	{
		const bool bLocked = (NewState & ES_RAILGUN_LOCK) != 0;
		UE_LOG(LogVHServerGame, Log, TEXT("[상태] 나: 레일건 잠금 %s"), bLocked ? TEXT("시작") : TEXT("끝"));
		if (Pawn)
		{
			if (UVHActionComponent* Action = Pawn->FindComponentByClass<UVHActionComponent>())
			{
				Action->SetMoveLocked(bLocked);
			}
			if (UVHAimComponent* Aim = Pawn->FindComponentByClass<UVHAimComponent>())
			{
				Aim->SetTurnLocked(bLocked);
			}
			if (UVHWeaponVisualComponent* WeaponVisual = Pawn->FindComponentByClass<UVHWeaponVisualComponent>())
			{
				WeaponVisual->SetRailgunLock(bLocked);	// 레일건 버팀 자세
			}
		}
	}
	// 제트팩 3초: 커서 방향으로 10m/s. 방향은 바꿀 수 있지만 멈출 수 없다 (이동은 클라, 부딪침은 서버)
	if (Changed & ES_JETPACK)
	{
		static double JetSince = 0.0;
		static FVector JetFrom = FVector::ZeroVector;
		const bool bJet = (NewState & ES_JETPACK) != 0;
		if (bJet)
		{
			JetSince = FPlatformTime::Seconds();
			JetFrom = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
		}
		UE_LOG(LogVHServerGame, Log, TEXT("[상태] 나: 제트팩 %s %s"), bJet ? TEXT("시작") : TEXT("끝"), bJet ? TEXT("") : *AverageSpeed(JetSince, JetFrom));
		if (UVHActionComponent* Action = Pawn ? Pawn->FindComponentByClass<UVHActionComponent>() : nullptr)
		{
			Action->SetJetpack(bJet, UVHSettings::Get().GetGameplayConfig().JetpackSpeed);
		}
		if (UVHMatchSubsystem* Match = GetMatch(); Match && bJet)
		{
			Match->PushNotice(LOCTEXT("JetpackOn", "제트팩! 3초 동안 커서 방향으로 날아갑니다 (멈출 수 없음)"));
		}
	}
	if (Changed & ES_CLOAKED)
	{
		const bool bCloaked = (NewState & ES_CLOAKED) != 0;
		if (bCloaked)
		{
			CloakSince = FPlatformTime::Seconds();
			CloakFrom = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
		}
		UE_LOG(LogVHServerGame, Log, TEXT("[상태] 나: 은신 %s %s%s"), bCloaked ? TEXT("시작") : TEXT("끝"), bCloaked ? TEXT("") : *AverageSpeed(CloakSince, CloakFrom),
			bCloaked ? TEXT("") : (TickCause.WasInteractConfirmed() ? TEXT(" (이유: 같은 틱에 내 상호작용이 시작됨)") : TEXT(" (이유: 상호작용 아님)")));
		if (UVHMatchSubsystem* Match = GetMatch())
		{
			Match->PushNotice(bCloaked
				? LOCTEXT("CloakOn", "은신! 다른 플레이어와 경비원에게 보이지 않습니다 (Q로 해제, 공격·상호작용하면 풀림)")
				// 서버는 상호작용이 실제로 시작(성사)될 때만 은신을 푼다. 거절된 상호작용은 풀지 않는다 [서버 S-03, Q-42]
				// → 이 스냅샷과 같은 틱에 내 상호작용이 확인됐을 때만 이유를 알려 준다 (F를 누른 것만으로는 안 띄운다)
				: (TickCause.WasInteractConfirmed()
					? LOCTEXT("CloakOffInteract", "상호작용해서 은신이 풀렸습니다")
					: LOCTEXT("CloakOff", "은신이 풀렸습니다")));
		}
	}
	// 탈진: 서버가 탈진이라고 하면 로컬도 탈진으로 (달리기·구르기 불가)
	if ((NewState & ES_EXHAUSTED) && !(OldState & ES_EXHAUSTED) && Pawn)
	{
		if (UVHActionComponent* Action = Pawn->FindComponentByClass<UVHActionComponent>())
		{
			if (!Action->IsExhausted())
			{
				Action->DebugSetStamina(0.f);
			}
		}
	}
}

void UVHServerGameSubsystem::ApplySnapshot(const SC_Snapshot& P)
{
	++SnapshotsReceived;
	const double Now = FPlatformTime::Seconds();
	for (int32 i = 0; i < P.entity_count; ++i)
	{
		const EntitySnapshot& S = P.entities[i];
		FEntity* E = Entities.Find(S.entity_id);
		if (!E)
		{
			continue;
		}
		const uint32 OldState = E->State;
		E->Pos = ToVector(S.pos);
		E->Yaw = S.yaw;
		E->State = S.state;
		E->Hp = S.hp;
		E->Action = S.action;
		E->Weapon = S.weapon;
		if (AVHNetProxy* Proxy = E->Proxy.Get())
		{
			Proxy->PushSnapshot(Now, E->Pos, E->Yaw, E->State, E->Hp, static_cast<uint8>(E->Weapon));
		}
		if (E->Object.IsValid() && OldState != S.state)
		{
			ApplyObjectState(*E, OldState);
		}
		if (E->Fx.IsValid() && OldState != S.state)
		{
			UE_LOG(LogVHServerGame, Log, TEXT("[상태] %s %u: 0x%06x → 0x%06x"), KindName(E->Kind), E->Id, OldState, S.state);
			ApplyFxState(*E);
		}
		if (E->Kind == EntityKind::Thief && E->Id != SelfEntityId && ((OldState ^ S.state) & LoggedOtherBits))
		{
			UE_LOG(LogVHServerGame, Log, TEXT("[상태] %s: 0x%06x → 0x%06x (경직 %d, 감속 %d, 은신 %d, 연막 %d, 레일건 잠금 %d, 테이저 듦 %d, 화면 손 무기 %d)%s"), *NameForPlayer(E->Owner), OldState, S.state,
				(S.state & ES_STUNNED) ? 1 : 0, (S.state & ES_SLOWED) ? 1 : 0, (S.state & ES_CLOAKED) ? 1 : 0, (S.state & ES_IN_SMOKE) ? 1 : 0, (S.state & ES_RAILGUN_LOCK) ? 1 : 0, (S.state & ES_TASER_READY) ? 1 : 0, (E->Proxy.IsValid() && E->Proxy->GetWeaponVisual()) ? (int32)E->Proxy->GetWeaponVisual()->GetWeapon() : -1,
				(E->Proxy.IsValid() && E->Proxy->IsHiddenByState()) ? TEXT(" → 화면에서 숨김") : TEXT(""));
		}
		if (E->Kind == EntityKind::Box)
		{
			if (AVHBox* Box = Cast<AVHBox>(E->Object.Get()))
			{
				Box->SetServerHp(S.hp);
			}
		}
		if (E->Id == SelfEntityId)
		{
			if (DebugSelfBits != 0 && Now > DebugSelfBitsUntil)
			{
				DebugSelfBits = 0;	// 개발용 흉내(DebugFake) 시간이 끝남 → 서버 값으로 돌아간다
			}
			const uint32 OldSelf = SelfState;
			SelfState = S.state | DebugSelfBits;
			if (OldSelf != SelfState)
			{
				ApplySelfState(OldSelf, SelfState);
			}
		}
	}

	// 이 틱의 이벤트(Cancel → 대미지 → 쓰러짐)와 방금 적용한 내 상태로 "공격받아 작업이 멈췄나"를 가리고, 다음 틱을 위해 비운다.
	// 서버는 한 틱의 이벤트를 모두 보낸 뒤 스냅샷을 보내므로, 스냅샷이 틱의 끝이다
	if (TickCause.EndTick(HasSelfState(ES_STUNNED), HasSelfState(ES_DOWNED)))
	{
		UE_LOG(LogVHServerGame, Log, TEXT("[작업] 공격받아 작업이 멈춤"));
		if (UVHInteractionSubsystem* Sub = GetInteraction())
		{
			Sub->PostMessage(LOCTEXT("InterruptedByDamage", "공격받아 작업이 멈췄습니다"), true);
		}
	}

	// 남은 시간을 서버 값으로 (1초에 한 번 맞춤)
	if (SnapshotsReceived % TICK_HZ == 1)
	{
		if (UVHMatchSubsystem* Match = GetMatch())
		{
			Match->ApplyServerTime(DurationSeconds, P.remaining_ms / 1000.f);
		}
	}
}

// ───────────────────────── 게임 이벤트 ─────────────────────────

void UVHServerGameSubsystem::HandleEvent(uint16 Type, const uint8* Data, int32 Size)
{
	UVHMatchSubsystem* Match = GetMatch();

	switch (static_cast<PacketId>(Type))
	{
	case PacketId::SC_ACTION_REJECT:	{ SC_ActionReject P;	if (VHPacket::ReadFixed(Data, Size, P)) OnReject(P); break; }
	case PacketId::SC_INTERACT:			{ SC_Interact P;		if (VHPacket::ReadFixed(Data, Size, P)) OnInteract(P); break; }
	case PacketId::SC_MONEY:			{ SC_Money P;			if (VHPacket::ReadFixed(Data, Size, P)) OnMoney(P); break; }
	case PacketId::SC_PLAYER_STATUS:	{ SC_PlayerStatus P;	if (VHPacket::ReadFixed(Data, Size, P)) OnPlayerStatus(P); break; }
	case PacketId::SC_DAMAGE:			{ SC_Damage P;			if (VHPacket::ReadFixed(Data, Size, P)) OnDamage(P); break; }
	case PacketId::SC_DOWNED:			{ SC_Downed P;			if (VHPacket::ReadFixed(Data, Size, P)) OnDowned(P); break; }
	case PacketId::SC_RESPAWNED:		{ SC_Respawned P;		if (VHPacket::ReadFixed(Data, Size, P)) OnRespawned(P); break; }
	case PacketId::SC_PICKUP:			{ SC_Pickup P;			if (VHPacket::ReadFixed(Data, Size, P)) OnPickup(P); break; }
	case PacketId::SC_SAFE_OPENED:
	{
		SC_SafeOpened P;
		if (!VHPacket::ReadFixed(Data, Size, P)) break;
		UE_LOG(LogVHServerGame, Log, TEXT("[서버] 금고 열림 %u (연 사람 %s)"), P.safe, *NameOfEntity(P.opener));
		if (AVHSafe* Safe = Cast<AVHSafe>(ObjectFor(P.safe)); Safe && Safe->GetSafeState() == EVHSafeState::Closed)
		{
			Safe->ApplyOpened();
			Safe->ApplyTreasureTaken();	// 보물은 금고 앞에 따로 생긴다
		}
		const FText Notice = FText::Format(LOCTEXT("SafeOpened", "{0}이(가) 금고를 열었습니다! 보물 등장"), FText::FromString(NameOfEntity(P.opener)));
		if (Match)
		{
			Match->PushNotice(Notice);
			Match->ShowBanner(Notice, UVHSettings::Get().GetGameplayConfig().NoticeBannerSeconds);
		}
		break;
	}
	case PacketId::SC_TREASURE_TAKEN:
	{
		SC_TreasureTaken P;
		if (!VHPacket::ReadFixed(Data, Size, P)) break;
		UE_LOG(LogVHServerGame, Log, TEXT("[서버] 보물 획득: %s, 가치 %u$, 위치 (%.0f, %.0f)"), *NameOfEntity(P.thief), P.value, P.pos.x, P.pos.y);
		if (MyDroppedTreasureId != 0)
		{
			// 보물은 한 번에 하나뿐 → 바닥의 내 보물을 주운 것. 곧 오는 SC_ENTITY_DESPAWN 에서 표시를 지운다
			DroppedTreasureTaker = P.thief == SelfEntityId ? FString() : NameOfEntity(P.thief);
		}
		if (P.thief != SelfEntityId)
		{
			// 보물탐색 퍽이 있으면 미니맵에 이쪽 방향을 잠깐 표시한다 (GetTreasureSense)
			TreasureSensePos = ToVector(P.pos);
			TreasureSenseSeconds = FPlatformTime::Seconds();
		}
		// 기획: "누군가 보물을 획득했다"는 전원 메시지. 내가 떨어뜨린 보물을 남이 주웠으면 나에게는 누가 가져갔는지 알려 준다
		const FText Money = FText::FromString(VHRanking::Money(static_cast<int32>(P.value)));
		const FText Notice = P.thief == SelfEntityId
			? FText::Format(LOCTEXT("TreasureMine", "보물을 획득했습니다! ({0})"), Money)
			: !DroppedTreasureTaker.IsEmpty()
			? FText::Format(LOCTEXT("DroppedTreasureTaken", "{0}이(가) 내가 떨어뜨린 보물을 주웠습니다 ({1})"), FText::FromString(DroppedTreasureTaker), Money)
			: FText::Format(LOCTEXT("TreasureTaken", "누군가 보물을 획득했습니다! ({0})"), Money);
		if (Match)
		{
			Match->PushNotice(Notice);
			Match->ShowBanner(Notice, UVHSettings::Get().GetGameplayConfig().NoticeBannerSeconds);
		}
		break;
	}
	case PacketId::SC_FIRE:
	{
		SC_Fire P;
		if (VHPacket::ReadFixed(Data, Size, P))
		{
			OnFire(P);
		}
		break;
	}
	case PacketId::SC_GUARD_ALERT:
	{
		SC_GuardAlert P;
		if (VHPacket::ReadFixed(Data, Size, P))
		{
			UE_LOG(LogVHServerGame, Log, TEXT("[서버] 경비원 %u 상태 %d 대상 %s"), P.guard, (int32)P.level, *NameOfEntity(P.target));
		}
		break;
	}
	case PacketId::SC_PROJECTILE:
	{
		SC_Projectile P;
		if (VHPacket::ReadFixed(Data, Size, P))
		{
			OnProjectile(P);
		}
		break;
	}
	case PacketId::SC_EXPLOSION:
	{
		SC_Explosion P;
		if (VHPacket::ReadFixed(Data, Size, P))
		{
			OnExplosion(P.kind, ToVector(P.pos), P.radius);
		}
		break;
	}
	case PacketId::SC_KNOCKBACK:
	{
		SC_Knockback P;
		if (VHPacket::ReadFixed(Data, Size, P))
		{
			OnKnockback(P.target, P.yaw, P.distance);
		}
		break;
	}
	case PacketId::SC_SKILL:
	{
		SC_Skill P;
		if (VHPacket::ReadFixed(Data, Size, P))
		{
			OnSkill(P);
		}
		break;
	}
	default:
		UE_LOG(LogVHServerGame, Verbose, TEXT("[서버] 패킷 %u (%d bytes)"), Type, Size);
		break;
	}
}

void UVHServerGameSubsystem::OnKnockback(uint16 Target, float Yaw, float Distance)
{
	UE_LOG(LogVHServerGame, Log, TEXT("[서버] 밀려남 %s yaw %.0f 거리 %.0f"), *NameOfEntity(Target), Yaw, Distance);
	if (Target != SelfEntityId)
	{
		return;	// 다른 사람은 그 사람의 클라가 움직이고, 여기서는 스냅샷 위치를 따라 그린다
	}
	// 제트팩에 부딪힘: 서버는 방향과 거리만 알려 주고, 밀려나는 이동은 받은 클라가 한다 (서버 A-19)
	APawn* Pawn = GetLocalPawn();
	UVHActionComponent* Action = Pawn ? Pawn->FindComponentByClass<UVHActionComponent>() : nullptr;
	if (!Action || HasSelfState(ES_DOWNED))
	{
		return;
	}
	if (UVHInteractionComponent* I = GetMyInteraction(); I && I->IsInteracting())
	{
		SendInteractCancel(I);	// 밀려나면 하던 작업은 끊긴다 (자리를 벗어나므로)
	}
	Action->StartKnockback(FRotator(0.f, Yaw, 0.f).Vector(), Distance, UVHSettings::Get().GetGameplayConfig().KnockbackSeconds);
	if (UVHMatchSubsystem* Match = GetMatch())
	{
		Match->PushNotice(LOCTEXT("Knockback", "제트팩에 부딪혀 밀려났습니다!"));
	}
}

void UVHServerGameSubsystem::DebugFake(const FString& What, float Seconds)
{
	// 개발용: 서버가 보낸 것처럼 내 상태·이벤트를 흉내 낸다 (실서버에서 구하기 어려운 아이템의 클라 적용 효과를 보려고).
	// 상태 비트는 Seconds 동안 스냅샷 값에 덧씌우고, 판정에는 아무 영향이 없다(서버는 모른다).
	uint32 Bit = 0;
	if (What.Equals(TEXT("knockback"), ESearchCase::IgnoreCase))
	{
		OnKnockback(SelfEntityId, GetLookYaw() + 180.f, 300.f);
		return;
	}
	// ── 연출 흉내: 내 위치·커서가 가리키는 곳에 같은 처리 함수를 부른다 ──
	const APawn* FakePawn = GetLocalPawn();
	const FVector MyFoot = FakePawn ? FakePawn->GetActorLocation() - FVector(0.f, 0.f, GetPawnHalfHeight(FakePawn)) : FVector::ZeroVector;
	FVector Aim = GetAimPoint();
	if (FVector::Dist2D(Aim, MyFoot) < 100.f || FVector::Dist2D(Aim, MyFoot) > 600.f)
	{
		Aim = MyFoot + FRotator(0.f, GetLookYaw(), 0.f).Vector() * 450.f;	// 커서가 너무 가깝거나 멀면 앞 4.5m
	}
	Aim.Z = MyFoot.Z;
	auto FakeThrow = [&](ExplosionKind Kind, float Radius)
	{
		SC_Projectile P = VHPacket::Make<SC_Projectile>(PacketId::SC_PROJECTILE);
		P.thrower = SelfEntityId;
		P.kind = Kind;
		P.from = { static_cast<float>(MyFoot.X), static_cast<float>(MyFoot.Y), static_cast<float>(MyFoot.Z) };
		P.to = { static_cast<float>(Aim.X), static_cast<float>(Aim.Y), static_cast<float>(Aim.Z) };
		P.flight_ms = 1000;
		OnProjectile(P);
		FTimerHandle Handle;
		GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Kind, Aim, Radius]() { OnExplosion(Kind, Aim, Radius); }), 1.f, false);
	};
	auto FakeEntity = [&](EntityKind Kind, uint16 TypeId, const FVector& Pos, uint32 State) -> uint16
	{
		EntitySpawn S = {};
		S.entity_id = NextFakeEntityId++;
		S.kind = Kind;
		S.type_id = TypeId;
		S.pos = { static_cast<float>(Pos.X), static_cast<float>(Pos.Y), static_cast<float>(Pos.Z) };
		S.state = State;
		S.timer_ms = Kind == EntityKind::Mine ? 0 : static_cast<uint32>(Seconds * 1000.f);
		AddEntity(S, false);
		FakeEntityUntil.Add(S.entity_id, FPlatformTime::Seconds() + Seconds);
		return S.entity_id;
	};
	auto FakeFire = [&](WeaponId Weapon)
	{
		SC_Fire P = VHPacket::Make<SC_Fire>(PacketId::SC_FIRE);
		P.shooter = SelfEntityId;
		P.weapon = Weapon;
		P.origin = { static_cast<float>(MyFoot.X), static_cast<float>(MyFoot.Y), static_cast<float>(MyFoot.Z) };
		P.yaw = GetLookYaw();
		OnFire(P);
	};
	if (What.Equals(TEXT("grenade"), ESearchCase::IgnoreCase)) { FakeThrow(ExplosionKind::Grenade, 600.f); return; }
	if (What.Equals(TEXT("flashbang"), ESearchCase::IgnoreCase)) { FakeThrow(ExplosionKind::Flashbang, 300.f); return; }
	if (What.Equals(TEXT("blastmine"), ESearchCase::IgnoreCase) || What.Equals(TEXT("shockmine"), ESearchCase::IgnoreCase))
	{
		// 설치(흐림) → 1초 뒤 작동 → 2초 뒤 감지(깜빡임) → 2.5초 뒤 폭발
		const bool bShock = What.Equals(TEXT("shockmine"), ESearchCase::IgnoreCase);
		const uint16 Id = FakeEntity(EntityKind::Mine, static_cast<uint16>(bShock ? WeaponId::ShockMine : WeaponId::BlastMine), Aim, 0);
		FakeEntityUntil.Add(Id, FPlatformTime::Seconds() + 2.5);
		auto Step = [this, Id](uint32 State)
		{
			if (FEntity* E = Entities.Find(Id))
			{
				E->State = State;
				ApplyFxState(*E);
			}
		};
		FTimerHandle H1, H2, H3;
		FTimerManager& Timers = GetWorld()->GetTimerManager();
		Timers.SetTimer(H1, FTimerDelegate::CreateWeakLambda(this, [Step]() { Step(ES_ARMED); }), 1.f, false);
		Timers.SetTimer(H2, FTimerDelegate::CreateWeakLambda(this, [Step]() { Step(ES_ARMED | ES_TRIGGERED); }), 2.f, false);
		Timers.SetTimer(H3, FTimerDelegate::CreateWeakLambda(this, [this, Aim, bShock]() { OnExplosion(bShock ? ExplosionKind::ShockMine : ExplosionKind::BlastMine, Aim, 300.f); }), 2.5f, false);
		return;
	}
	if (What.Equals(TEXT("treasure"), ESearchCase::IgnoreCase))
	{
		// 보물탐색 퍽의 미니맵 방향 표시 (퍽이 있어야 보인다)
		TreasureSensePos = Aim;
		TreasureSenseSeconds = FPlatformTime::Seconds();
		return;
	}
	if (What.Equals(TEXT("smokefx"), ESearchCase::IgnoreCase)) { FakeEntity(EntityKind::Smoke, 0, MyFoot, 0); return; }
	if (What.Equals(TEXT("trap"), ESearchCase::IgnoreCase)) { FakeEntity(EntityKind::StickyTrap, 0, Aim, 0); return; }
	if (What.Equals(TEXT("railbeam"), ESearchCase::IgnoreCase)) { FakeFire(WeaponId::Railgun); return; }
	if (What.Equals(TEXT("pistol"), ESearchCase::IgnoreCase)) { FakeFire(WeaponId::Pistol); return; }
	if (What.Equals(TEXT("baton"), ESearchCase::IgnoreCase)) { FakeFire(WeaponId::Baton); return; }
	if (What.Equals(TEXT("flame"), ESearchCase::IgnoreCase)) { FakeFire(WeaponId::Flamethrower); return; }

	if (What.Equals(TEXT("jetpack"), ESearchCase::IgnoreCase)) { Bit = ES_JETPACK; }
	else if (What.Equals(TEXT("railgun"), ESearchCase::IgnoreCase)) { Bit = ES_RAILGUN_LOCK; }
	else if (What.Equals(TEXT("sticky"), ESearchCase::IgnoreCase)) { Bit = ES_SLOWED; }
	else if (What.Equals(TEXT("cloak"), ESearchCase::IgnoreCase)) { Bit = ES_CLOAKED; }
	else if (What.Equals(TEXT("stun"), ESearchCase::IgnoreCase)) { Bit = ES_STUNNED; }
	else if (What.Equals(TEXT("flash"), ESearchCase::IgnoreCase)) { Bit = ES_FLASHED; }
	else if (What.Equals(TEXT("smoke"), ESearchCase::IgnoreCase)) { Bit = ES_IN_SMOKE; }
	if (Bit == 0)
	{
		UE_LOG(LogVHServerGame, Warning, TEXT("[흉내] 모르는 이름 '%s' (knockback, jetpack, railgun, sticky, cloak, stun, flash, smoke)"), *What);
		return;
	}
	UE_LOG(LogVHServerGame, Log, TEXT("[흉내] %s %.1f초 (내 화면에만, 서버는 모른다)"), *What, Seconds);
	DebugSelfBits |= Bit;
	DebugSelfBitsUntil = FPlatformTime::Seconds() + Seconds;
	const uint32 Old = SelfState;
	SelfState |= Bit;
	if (Old != SelfState)
	{
		ApplySelfState(Old, SelfState);
	}
}

void UVHServerGameSubsystem::OnSkill(const SC_Skill& P)
{
	static const TCHAR* PhaseNames[] = { TEXT("시전"), TEXT("발동"), TEXT("끝") };
	const bool bMe = P.caster == SelfEntityId;
	const FVector Pos = ToVector(P.pos);
	UE_LOG(LogVHServerGame, Log, TEXT("[서버] 스킬 %s: %s %s 위치 (%.0f, %.0f)"), *NameOfEntity(P.caster), SkillName(P.skill),
		PhaseNames[FMath::Clamp<int32>(static_cast<int32>(P.phase), 0, 2)], Pos.X, Pos.Y);

	const UVHGameplayConfig& G = UVHSettings::Get().GetGameplayConfig();
	UVHMatchSubsystem* Match = GetMatch();
	// 몸 동작: 끈끈이 던지기 (시전 순간)
	if (P.skill == SkillId::Sticky && P.phase == SkillPhase::Cast)
	{
		if (UVHWeaponVisualComponent* Caster = VisualOfEntity(P.caster))
		{
			Caster->PlayBodyAction(EVHBodyAction::Throw);
		}
	}
	switch (P.skill)
	{
	case SkillId::Taser:
		// 사제 테이저건 (2026-11-22 팀 서버): 들기 = Active, 발사 = Cast(쿨타임 시작), 쏘지 않고 집어넣음 = End. 내 손 모양은 바로 바꾼다(스냅샷의 ES_TASER_READY 도 같은 뜻)
		if (bMe)
		{
			SetSkillTaserHeld(P.phase == SkillPhase::Active,
				P.phase == SkillPhase::Active ? TEXT("서버 SC_SKILL 들기") : (P.phase == SkillPhase::Cast ? TEXT("서버 SC_SKILL 발사") : TEXT("서버 SC_SKILL 집어넣기")));
		}
		else if (UVHWeaponVisualComponent* Caster = VisualOfEntity(P.caster); Caster && P.phase != SkillPhase::Active)
		{
			// 다른 플레이어: 발사·집어넣기 순간 손 무기를 바로 원래대로 (다음 스냅샷을 기다리지 않음). 드는 모습은 스냅샷 비트로
			if (const FEntity* E = Entities.Find(P.caster))
			{
				Caster->SetWeapon(static_cast<uint8>(E->Weapon));
			}
		}
		if (P.phase == SkillPhase::Cast)
		{
			// 사제 테이저건: 시선 방향 3m 직선 (맞았는지는 SC_DAMAGE 와 대상의 경직 상태로 온다)
			const FEntity* Caster = Entities.Find(P.caster);
			SpawnFireFx(Pos + FVector(0.f, 0.f, 98.f), bMe ? GetLookYaw() : (Caster ? Caster->Yaw : 0.f), SkillTaserRangeCm, 0.f,
				FLinearColor(0.4f, 0.8f, 1.f), 0.3f, 8.f, false);
		}
		break;

	case SkillId::Sticky:
		if (P.phase == SkillPhase::Cast)
		{
			// 끈끈이: 1초 동안 던지는 동작 → 그 뒤 이동 방향 반대쪽 1m 뒤에 함정(StickyTrap 엔티티)이 생긴다
			if (AVHFxActor* Fx = AVHFxActor::SpawnFx(GetWorld(), Pos + FVector(0.f, 0.f, 4.f), EVHFxShape::Disc, FVector(140.f), G.StickyTrapColor, 0.7f, 1.f))
			{
				Fx->SetPulse(4.f);
			}
			if (bMe && Match)
			{
				Match->PushNotice(LOCTEXT("StickyCast", "끈끈이를 던지는 중… 1초 뒤 뒤쪽에 함정이 생깁니다"));
			}
		}
		break;

	case SkillId::Cloak:
		// 은신 표시는 상태 비트(ES_CLOAKED)로 한다: 나 = 반투명, 다른 사람 = 숨김. 여기서는 사라지는 순간만 표시
		if (P.phase == SkillPhase::Active && !bMe)
		{
			if (AVHFxActor* Fx = AVHFxActor::SpawnFx(GetWorld(), Pos + FVector(0.f, 0.f, 90.f), EVHFxShape::Sphere, FVector(120.f), G.CloakSelfColor, 0.5f, 0.4f))
			{
				Fx->SetGrow(1.8f);
			}
		}
		break;

	default:
		break;
	}
}

void UVHServerGameSubsystem::OnInteract(const SC_Interact& P)
{
	UE_LOG(LogVHServerGame, Log, TEXT("[서버] 작업 %s → %s 상태 %d %u/%ums"), *NameOfEntity(P.actor), *NameOfEntity(P.target), (int32)P.state, P.progress_ms, P.duration_ms);
	// 효과음: 금고 열림·상자 열림 (누가 했든)
	if (P.state == InteractState::Complete)
	{
		if (const FEntity* T = Entities.Find(P.target))
		{
			if (T->Kind == EntityKind::Safe || T->Kind == EntityKind::Box)
			{
				UVHSoundConfig::Play(this, T->Kind == EntityKind::Safe ? EVHSound::SafeOpen : EVHSound::BoxOpen, T->Pos);
			}
		}
	}
	if (P.actor != SelfEntityId)
	{
		return;	// 다른 플레이어: 상태는 스냅샷(Interacting 비트)으로 보인다
	}
	UVHInteractionSubsystem* Sub = GetInteraction();
	UVHInteractionComponent* By = GetMyInteraction();
	AVHInteractableBase* Target = ObjectFor(P.target);
	if (!Sub || !By || !Target)
	{
		return;
	}
	const FVHInteractOffer Offer = Pending.Target.Get() == Target ? Pending.Offer : Sub->GetAuthority()->Evaluate(By, Target);
	// duration_ms 는 작업 속도 1.0 기준. 유용한 도구 퍽이면 실제로는 1.1배 빨리 끝난다
	const float Speed = GetMyWorkSpeed();
	const float Total = P.duration_ms / 1000.f / Speed;
	const float Remaining = FMath::Max(0.f, (static_cast<float>(P.duration_ms) - static_cast<float>(P.progress_ms)) / 1000.f / Speed);
	const EVHInteractMode Mode = P.duration_ms > 0 ? EVHInteractMode::Hold : Offer.Mode;
	switch (P.state)
	{
	case InteractState::Begin:
		TickCause.OnInteractConfirmed();	// 작업이 실제로 시작됨 (은신 중이었다면 서버가 이때 은신을 푼다)
		Sub->ApplyInteract(By, Target, EVHInteractState::Begin, Sub->GetNow(), Remaining, Total, Mode, Offer.bKeepProgressOnCancel, Offer.ActionText);
		// 응답이 오기 전에 손을 뗐으면 바로 취소
		if (Mode == EVHInteractMode::Hold && !By->IsInteractHeld())
		{
			SendInteractCancel(By);
		}
		break;
	case InteractState::Cancel:
		Sub->ApplyInteract(By, Target, EVHInteractState::Cancel, Sub->GetNow(), 0.f, Total, Mode, Offer.bKeepProgressOnCancel, Offer.ActionText);
		Pending = FPending();
		// 왜 멈췄는지는 이 틱의 스냅샷이 올 때 가린다: 이 Cancel 뒤에 같은 틱의 SC_DAMAGE가 오면 공격받아 멈춘 것 [서버 IN-05, Q-41]
		TickCause.OnInteractCancel(SentOwnActionRecently());
		break;
	case InteractState::Complete:
		Sub->ApplyInteract(By, Target, EVHInteractState::Complete, Sub->GetNow(), 0.f, Total, Mode, Offer.bKeepProgressOnCancel, Offer.ActionText);
		Pending = FPending();
		break;
	}
}

bool UVHServerGameSubsystem::SentOwnActionRecently() const
{
	return LastOwnActionSentTime > 0.0 && FPlatformTime::Seconds() - LastOwnActionSentTime < OwnActionWindowSeconds;
}

void UVHServerGameSubsystem::OnMoney(const SC_Money& P)
{
	UVHMatchSubsystem* Match = GetMatch();
	if (!Match)
	{
		return;
	}
	const FVHServerMoneyRow* Before = Match->GetMyServerMoney();
	const int32 MyBankedBefore = Before ? Before->Banked : 0;
	const int32 MyCarriedBefore = Before ? Before->Carried : 0;

	// 패킷에 없는 플레이어 줄은 그대로 둔다
	TArray<FVHServerMoneyRow> Rows = Match->GetServerMoney();
	FString Log;
	for (int32 i = 0; i < FMath::Min<int32>(P.thief_count, MAX_ROOM_PLAYERS); ++i)
	{
		const ThiefMoney& T = P.thieves[i];
		const uint16 ThiefId = T.thief;
		int32 Index = Rows.IndexOfByPredicate([ThiefId](const FVHServerMoneyRow& R) { return R.EntityId == ThiefId; });
		if (Index == INDEX_NONE)
		{
			Index = Rows.AddDefaulted();
			Rows[Index].EntityId = ThiefId;
			Rows[Index].Name = NameOfEntity(ThiefId);
			Rows[Index].bIsMe = ThiefId == SelfEntityId;
		}
		Rows[Index].Carried = static_cast<int32>(T.carried_value);
		Rows[Index].Banked = static_cast<int32>(T.banked_value);
		Log += FString::Printf(TEXT(" %s 들고 %d$ 반납 %d$"), *Rows[Index].Name, Rows[Index].Carried, Rows[Index].Banked);
	}
	UE_LOG(LogVHServerGame, Log, TEXT("[서버] 금액:%s"), *Log);
	Match->ApplyServerMoney(Rows);

	// 내 것: 들고 있는 가치는 서버 값으로. 반납 금액이 늘었으면 반납 성공
	if (const FVHServerMoneyRow* Mine = Match->GetMyServerMoney())
	{
		if (UVHTreasureCarrierComponent* Carrier = GetMyCarrier())
		{
			if (Mine->Carried == 0 && Carrier->GetCount() > 0)
			{
				LastRemovedItems = Carrier->ApplyRemoveAll();	// 반납했거나 쓰러져서 잃음 (쓰러짐 창이 무엇을 떨어뜨렸는지 알 수 있게 잠깐 기억)
				LastRemovedSeconds = FPlatformTime::Seconds();
			}
			Carrier->ApplyServerCarried(Mine->Carried);
		}
		if (Mine->Banked > MyBankedBefore && MyCarriedBefore > 0)
		{
			TickCause.OnInteractConfirmed();	// 반납이 성사됨
			if (const APawn* RetPawn = GetLocalPawn())
			{
				UVHSoundConfig::Play(this, EVHSound::Return, RetPawn->GetActorLocation());
			}
			Match->PushNotice(FText::Format(LOCTEXT("Returned", "반납 +{0}  (내 반납 금액 {1})"),
				FText::FromString(VHRanking::Money(Mine->Banked - MyBankedBefore)), FText::FromString(VHRanking::Money(Mine->Banked))));
			if (Pending.bInstant && Cast<AVHReturnPoint>(Pending.Target.Get()))
			{
				Pending = FPending();
			}
		}
	}
	if (UVHInteractionSubsystem* Sub = GetInteraction())
	{
		Sub->OnTeamTreasureChanged.Broadcast();
	}
}

void UVHServerGameSubsystem::OnPlayerStatus(const SC_PlayerStatus& P)
{
	const bool bFirst = !MyStatus.bValid;
	const FVHServerPlayerStatus Old = MyStatus;
	MyStatus.bValid = true;
	MyStatus.Hp = P.hp;
	MyStatus.Shield = P.shield;
	MyStatus.Stamina = P.stamina;
	for (int32 i = 0; i < 2; ++i)
	{
		MyStatus.WeaponId[i] = static_cast<uint8>(P.weapons[i].id);
		MyStatus.WeaponAmmo[i] = P.weapons[i].ammo;
	}
	MyStatus.ActiveSlot = P.active_slot;
	MyStatus.Consumable = static_cast<uint8>(P.consumable);
	// 몸 동작: 회복 키트를 썼다 (들고 있던 회복 키트가 없어짐 + HP 가 올라감)
	if (!bFirst && Old.Consumable == static_cast<uint8>(ConsumableId::HealKit) && MyStatus.Consumable == 0 && P.hp > Old.Hp)
	{
		if (UVHWeaponVisualComponent* Me = VisualOfEntity(SelfEntityId))
		{
			Me->PlayBodyAction(EVHBodyAction::Drink);
			UVHSoundConfig::Play(this, EVHSound::Heal, Me->GetOwner() ? Me->GetOwner()->GetActorLocation() : FVector::ZeroVector);
		}
	}
	MyStatus.Skill = static_cast<uint8>(P.skill);
	MyStatus.SkillCooldownMs = static_cast<int32>(P.skill_cooldown_ms);
	MyStatus.SkillActiveMs = static_cast<int32>(P.skill_active_ms);
	MyStatus.bDeathSaveLeft = P.death_save_left;
	MyStatus.ReceivedSeconds = FPlatformTime::Seconds();

	UpdateMyWeaponVisual();

	// 달리는 동안은 SP가 매 틱 바뀌어 자주 온다 → 무기·HP·소비 아이템·스킬이 바뀔 때만 로그
	if (bFirst || Old.Hp != MyStatus.Hp || Old.Shield != MyStatus.Shield || Old.ActiveSlot != MyStatus.ActiveSlot
		|| Old.WeaponId[0] != MyStatus.WeaponId[0] || Old.WeaponId[1] != MyStatus.WeaponId[1]
		|| Old.Consumable != MyStatus.Consumable || Old.Skill != MyStatus.Skill)
	{
		UE_LOG(LogVHServerGame, Log, TEXT("[서버] 내 상태: HP %d 보호막 %d SP %d, 무기 [%s %d] [%s %d] 든 칸 %d, 소비 %s, 스킬 %d (쿨 %dms), 기사회생 %d"),
			MyStatus.Hp, MyStatus.Shield, MyStatus.Stamina,
			WeaponName(P.weapons[0].id), MyStatus.WeaponAmmo[0], WeaponName(P.weapons[1].id), MyStatus.WeaponAmmo[1], MyStatus.ActiveSlot,
			ConsumableName(P.consumable), (int32)MyStatus.Skill, MyStatus.SkillCooldownMs, MyStatus.bDeathSaveLeft ? 1 : 0);
	}

	// HP·보호막은 서버 값 그대로 화면에 (로컬 대미지 판정은 하지 않는다)
	if (const APawn* Pawn = GetLocalPawn())
	{
		if (UVHHealthComponent* Health = Pawn->FindComponentByClass<UVHHealthComponent>())
		{
			if (!FMath::IsNearlyEqual(Health->GetHP(), static_cast<float>(MyStatus.Hp)))
			{
				Health->SetHP(static_cast<float>(MyStatus.Hp));
			}
			if (!FMath::IsNearlyEqual(Health->GetShield(), static_cast<float>(MyStatus.Shield)))
			{
				Health->SetShield(static_cast<float>(MyStatus.Shield));
			}
		}
		// SP는 서버가 계산한다. 화면 반응을 위해 클라도 같은 속도로 줄이고 채우되, 서버 값과 5 넘게 벌어지면 서버 값으로 맞춘다
		if (UVHActionComponent* Action = Pawn->FindComponentByClass<UVHActionComponent>())
		{
			if (FMath::Abs(Action->GetStamina() - static_cast<float>(MyStatus.Stamina)) > 5.f)
			{
				Action->DebugSetStamina(static_cast<float>(MyStatus.Stamina));
			}
		}
	}
}

void UVHServerGameSubsystem::OnDamage(const SC_Damage& P)
{
	UE_LOG(LogVHServerGame, Log, TEXT("[서버] 대미지 %s → %s 무기 %s %u (HP %u, 보호막 %u)"),
		*NameOfEntity(P.attacker), *NameOfEntity(P.target), WeaponName(P.weapon), P.amount, P.hp_after, P.shield_after);
	++DamagesSeen;
	// 몸 동작: 맞은 사람이 움찔 (경직·쓰러짐·작업 중이면 그 동작이 우선)
	if (UVHWeaponVisualComponent* Hit = VisualOfEntity(P.target))
	{
		Hit->PlayBodyAction(EVHBodyAction::HitReact);
	}
	if (const FEntity* T = Entities.Find(P.target))
	{
		// 무기별 맞은 소리 (근접은 휘두르는 동작의 맞는 순간에 맞춰 조금 늦게)
		if (UVHWeaponFxSubsystem* HitFx = UVHWeaponFxSubsystem::Get(this))
		{
			HitFx->PlayHitSound(static_cast<uint8>(P.weapon), T->Pos);
		}
		else
		{
			UVHSoundConfig::Play(this, EVHSound::Hit, T->Pos);
		}
	}
	if (FEntity* E = Entities.Find(P.target))
	{
		E->Hp = P.hp_after;
		// 피격 표시: 맞은 대상의 몸에서 잠깐 퍼지는 구 (내가 맞으면 빨강, 남이 맞으면 흰색). 숨겨진 대상(은신·연막)은 표시하지 않는다
		const bool bMe = P.target == SelfEntityId;
		const APawn* Pawn = GetLocalPawn();
		const bool bHidden = !bMe && E->Proxy.IsValid() && E->Proxy->IsHiddenByState();
		if (!bHidden && (!bMe || Pawn))
		{
			const FVector Foot = bMe ? Pawn->GetActorLocation() - FVector(0.f, 0.f, GetPawnHalfHeight(Pawn)) : E->Pos;
			// 피격 이펙트: 무기 분류 색의 피격 불꽃 / 감전(테이저·감전 지뢰) / 불붙음(화염) [무기 이펙트 표]
			if (UVHWeaponFxSubsystem* WeaponFx = UVHWeaponFxSubsystem::Get(this))
			{
				AActor* TargetActor = bMe ? const_cast<APawn*>(Pawn) : static_cast<AActor*>(E->Proxy.Get());
				WeaponFx->OnDamage(static_cast<uint8>(P.weapon), TargetActor, Foot, E->Kind == EntityKind::Box);
			}
			if (bMe)
			{
				// 내가 맞으면 몸에서 옅은 빨간 구 (예전 표시 유지)
				if (AVHFxActor* Fx = AVHFxActor::SpawnFx(GetWorld(), Foot + FVector(0.f, 0.f, 100.f), EVHFxShape::Sphere, FVector(60.f), FLinearColor(1.f, 0.1f, 0.1f), 0.35f, 0.25f))
				{
					Fx->SetGrow(1.8f);
				}
			}
		}
	}
	if (P.target == SelfEntityId)
	{
		TickCause.OnSelfDamage();	// 같은 틱에 먼저 온 Cancel이 있으면 "공격받아 작업이 멈춤" 후보 (보호막이 다 막아도 작업은 멈춘다)
	}
	if (P.target == SelfEntityId && P.amount > 0)
	{
		UE_LOG(LogVHServerGame, Log, TEXT("[피격] 내가 맞음: -%u (HP %u, 보호막 %u) ← %s"), P.amount, P.hp_after, P.shield_after, *NameOfEntity(P.attacker));
	}

	// 실행 옵션 -VHShot: 처음 몇 번의 피격 순간도 화면으로 저장 (공격·피격 표시 확인용)
	static const bool bShotOnHit = FParse::Param(FCommandLine::Get(), TEXT("VHShotHit"));
	if (bShotOnHit && HitShotsTaken < 4)
	{
		FString Name;
		FParse::Value(FCommandLine::Get(), TEXT("VHBot="), Name);
		const FString File = FPaths::ProjectSavedDir() / TEXT("V3Test") / FString::Printf(TEXT("shot_%s_hit%02d.png"), *Name, ++HitShotsTaken);
		FScreenshotRequest::RequestScreenshot(File, true, false);
		UE_LOG(LogVHServerGame, Log, TEXT("[봇] 피격 화면 저장 %s (%s → %s)"), *FPaths::GetCleanFilename(File), *NameOfEntity(P.attacker), *NameOfEntity(P.target));
	}
}

UVHWeaponVisualComponent* UVHServerGameSubsystem::VisualOfEntity(uint16 EntityId) const
{
	if (EntityId == 0)
	{
		return nullptr;
	}
	if (EntityId == SelfEntityId)
	{
		const APawn* Pawn = GetLocalPawn();
		return Pawn ? Pawn->FindComponentByClass<UVHWeaponVisualComponent>() : nullptr;
	}
	if (const FEntity* E = Entities.Find(EntityId); E && E->Proxy.IsValid())
	{
		return E->Proxy->GetWeaponVisual();
	}
	return nullptr;
}

void UVHServerGameSubsystem::OnFire(const SC_Fire& P)
{
	UE_LOG(LogVHServerGame, Verbose, TEXT("[서버] 공격 %s 무기 %s yaw %.0f 맞은 대상 %s"),
		*NameOfEntity(P.shooter), WeaponName(P.weapon), P.yaw, *NameOfEntity(P.hit_entity));
	++FiresSeen;

	// 내 레일건: 서버가 누른 순간 발사(SC_FIRE, 충전 시작)했다 → 클릭 한 번 = 한 발이므로 손을 뗀 것으로 알린다
	if (P.shooter == SelfEntityId && P.weapon == WeaponId::Railgun && bRailAwaitFire)
	{
		const double Now = FPlatformTime::Seconds();
		UE_LOG(LogVHServerGame, Log, TEXT("[레일건] 서버 SC_FIRE (클릭 후 %.0fms, 서버 yaw %.0f) → 충전 1초 시작"), (Now - RailClickTime) * 1000.0, P.yaw);
		bRailAwaitFire = false;
		if (bAttackHeldSent)
		{
			bAttackHeldSent = false;
			if (UVHNetSubsystem* N = Net.Get())
			{
				N->SendAction(ActionType::AttackStop, GetLookYaw(), 0);
			}
			ApplyMyAttackHeldVisual();
		}
	}

	// 무기 사용 동작: 쏜 사람(나 또는 다른 플레이어)의 무기 표시 컴포넌트가 그 무기의 동작을 재생한다 (판정은 서버가 했다)
	if (P.weapon != WeaponId::None)
	{
		UVHWeaponVisualComponent* ShooterVisual = nullptr;
		if (P.shooter == SelfEntityId)
		{
			const APawn* Pawn = GetLocalPawn();
			ShooterVisual = Pawn ? Pawn->FindComponentByClass<UVHWeaponVisualComponent>() : nullptr;
		}
		else if (const FEntity* Shooter = Entities.Find(P.shooter); Shooter && Shooter->Proxy.IsValid())
		{
			ShooterVisual = Shooter->Proxy->GetWeaponVisual();
		}
		// 레일건 (2026-11-22 팀 서버): SC_FIRE = 누른 순간(충전 시작). 1초 충전 연출(소리·빛) 뒤 서버 첫 판정 시각에 발사 동작·소리·빔 (TickRailgunShots).
		// 나·다른 플레이어 모두 같은 방식이라 다른 사람의 충전도 보인다. 빔이 끝날 때까지 손에 레일건을 붙잡아 둔다(마지막 탄이면 서버 무기는 이미 삼단봉)
		if (P.weapon == WeaponId::Railgun)
		{
			CancelRailgunShot(P.shooter, TEXT("새 발사"));	// (같은 사람의 이전 연출이 남아 있으면 정리)
			FRailShot Shot;
			Shot.Shooter = P.shooter;
			Shot.Origin = ToVector(P.origin);
			Shot.Yaw = P.yaw;
			const double Now = FPlatformTime::Seconds();
			Shot.FireAt = Now + RailgunChargeSeconds;
			Shot.ReleaseAt = Shot.FireAt + RailgunBeamSeconds;
			Shot.Visual = ShooterVisual;
			if (ShooterVisual)
			{
				ShooterVisual->SetHeldWeaponOverride(static_cast<int32>(WeaponId::Railgun));
				ShooterVisual->SetRailgunCharging(true, static_cast<float>(RailgunChargeSeconds));
			}
			PendingRailShots.Add(Shot);
			if (P.shooter != SelfEntityId)
			{
				UE_LOG(LogVHServerGame, Log, TEXT("[레일건] %s SC_FIRE → 충전 연출 1초 (yaw %.0f)"), *NameOfEntity(P.shooter), P.yaw);
			}
			return;
		}
		if (ShooterVisual)
		{
			ShooterVisual->PlayAttack(static_cast<uint8>(P.weapon));
		}
		// 효과음: 무기별 (쏜 사람 위치)
		{
			// 화염방사기는 누르는 동안 반복 소리(UVHWeaponFxSubsystem)라 여기서는 내지 않는다
			const FEntity* Shooter = Entities.Find(P.shooter);
			const bool bGuard = Shooter && Shooter->Kind == EntityKind::Guard;
			const FVector At = ShooterVisual && ShooterVisual->GetOwner() ? ShooterVisual->GetOwner()->GetActorLocation() : (Shooter ? Shooter->Pos : FVector::ZeroVector);
			UVHSoundConfig::Play(this, bGuard ? EVHSound::GuardGun : UVHSoundConfig::FireSound(static_cast<uint8>(P.weapon)), At);
		}
		// 무기 이펙트(탄 궤적·전기 줄기·레일건 빔. 근접 궤적·총구 불꽃은 동작의 연출 순간에) [Docs/Design/무기_이펙트_표_2026-11-17.md]
		if (UVHWeaponFxSubsystem* Fx = UVHWeaponFxSubsystem::Get(this))
		{
			const FEntity* Hit = P.hit_entity != 0 ? Entities.Find(P.hit_entity) : nullptr;
			const FVector HitFeet = Hit ? Hit->Pos : FVector::ZeroVector;
			Fx->OnFire(static_cast<uint8>(P.weapon), ToVector(P.origin), P.yaw, Hit ? &HitFeet : nullptr, ShooterVisual);
			return;
		}
	}

	// 서버가 판정한 공격 한 번을 그대로 그린다(내 공격도 서버가 알려 준 것만 그린다 → 서버의 연사 속도와 화면이 같다).
	// 범위 값은 서버 GameData.h의 무기 표와 같은 표시용 값이다. 직선 = 빔, 부채꼴 = 부챗살.
	float Range = 0.f, HalfAngle = 0.f, Life = 0.18f, Thickness = 6.f;
	FLinearColor Color = FLinearColor::White;
	bool bThroughWalls = false;
	// 색 = 무기 분류의 발광 색 [구상안 v2.2, DA_VHWeaponCatalog]: 근접 루비 레드 · 원거리 아이스 시안. 화염방사기 불꽃만 주황(FlameColor), 경비원 기관총은 빨강 그대로
	const UVHWeaponCatalog& Catalog = UVHSettings::Get().GetWeaponCatalog();
	const FLinearColor Glow = Catalog.GlowColor(static_cast<uint8>(P.weapon));
	switch (P.weapon)
	{
	case WeaponId::Baton:			Range = 200.f; HalfAngle = 60.f; Color = Glow; break;
	case WeaponId::Sword:			Range = 300.f; HalfAngle = 60.f; Color = Glow; break;
	case WeaponId::BeamSword:		Range = 400.f; HalfAngle = 60.f; Color = Glow; Thickness = 10.f; break;
	case WeaponId::Flamethrower:	Range = 600.f; HalfAngle = 15.f; Color = Catalog.FlameColor; Thickness = 22.f; Life = 0.15f; break;
	case WeaponId::Taser:			Range = 300.f; Color = Glow; Thickness = 8.f; Life = 0.3f; break;
	case WeaponId::Railgun:			Range = 6000.f; Color = Glow; Thickness = 50.f; Life = 0.25f; bThroughWalls = true; break;	// 폭 50cm, 벽 무시
	case WeaponId::Pistol:			Range = 3000.f; Color = Glow; Thickness = 4.f; Life = 0.08f; break;
	case WeaponId::None:			Range = 1500.f; Color = FLinearColor(1.f, 0.25f, 0.25f); Thickness = 4.f; Life = 0.08f; break;	// 경비원 기관총
	default:						return;	// 수류탄·지뢰는 투척·폭발 패킷으로 그린다
	}
	FVector Origin = ToVector(P.origin);
	if (HalfAngle > 0.f)
	{
		// 화염방사기는 초당 30번 온다 → 부채꼴 표시는 쏘는 사람마다 0.1초에 한 번만
		const double Now = FPlatformTime::Seconds();
		double& Last = LastConeFxSeconds.FindOrAdd(P.shooter);
		if (P.weapon == WeaponId::Flamethrower && Now - Last < 0.1)
		{
			return;
		}
		Last = Now;
		Origin.Z += 60.f;	// 휘두르기·화염은 허리 높이
	}
	else
	{
		Origin.Z += 98.f;	// 총알·빔은 가슴 높이
		// 직선 공격이 누군가를 맞혔으면 그 대상까지만 긋는다 (레일건은 꿰뚫으므로 끝까지)
		if (P.hit_entity != 0 && !bThroughWalls)
		{
			if (const FEntity* Hit = Entities.Find(P.hit_entity))
			{
				Range = FMath::Max(50.f, FVector::Dist2D(Origin, Hit->Pos));
			}
		}
	}
	SpawnFireFx(Origin, P.yaw, Range, HalfAngle, Color, Life, Thickness, bThroughWalls);
}

void UVHServerGameSubsystem::OnDowned(const SC_Downed& P)
{
	if (const FEntity* V = Entities.Find(P.victim))
	{
		UVHSoundConfig::Play(this, EVHSound::Downed, V->Pos);
	}
	CancelRailgunShot(P.victim, TEXT("충전 중 쓰러짐"));	// 충전 중이면 서버도 쏘지 않는다 (빔이 이미 나갔으면 빔만 정리)
	const bool bByGuard = P.cause == DownCause::Guard;
	UE_LOG(LogVHServerGame, Log, TEXT("[서버] 쓰러짐: %s ← %s (%s), 잃은 반납 금액 %u$, 리스폰 %ums"),
		*NameOfEntity(P.victim), *NameOfEntity(P.attacker), bByGuard ? TEXT("경비원") : TEXT("플레이어"), P.lost_banked, P.respawn_ms);
	UVHMatchSubsystem* Match = GetMatch();
	if (!Match)
	{
		return;
	}
	const FString Victim = NameOfEntity(P.victim);
	const FText Notice = bByGuard
		? FText::Format(LOCTEXT("DownByGuard", "{0}이(가) 경비원에게 쓰러졌습니다"), FText::FromString(Victim))
		: FText::Format(LOCTEXT("DownByPlayer", "{0}이(가) {1}을(를) 쓰러뜨렸습니다"), FText::FromString(NameOfEntity(P.attacker)), FText::FromString(Victim));
	Match->PushNotice(Notice);

	if (P.victim == SelfEntityId)
	{
		TickCause.OnSelfDowned();	// 쓰러져서 멈춘 작업은 "공격받아 중단" 안내를 띄우지 않는다
		RespawnAtSeconds = FPlatformTime::Seconds() + P.respawn_ms / 1000.0;
		if (UVHThiefStateComponent* Me = GetMyThief())
		{
			// HUD 쓰러짐 화면: 사유·잃은 금액·리스폰까지 남은 시간
			FVHThiefMatchStats& S = Match->StatsFor(Me);
			++S.Deaths;
			S.LastDeathLoss = static_cast<int32>(P.lost_banked);
			S.ValueLost += static_cast<int32>(P.lost_banked);
			S.bLastDeathByGuard = bByGuard;
			S.LastKillerName = bByGuard ? FString() : NameOfEntity(P.attacker);

			// 쓰러짐 창: 반납한 돈 손실과 "들고 있던 것"을 나눠 보여 준다.
			// 서버 순서는 SC_DOWNED → SC_MONEY(들고 있는 가치 0) → SC_ENTITY_SPAWN(떨어진 아이템)이라 지금은 소지품이 아직 남아 있다.
			// SC_MONEY 가 먼저 왔으면(예외) 방금 비운 목록을 쓴다
			const double Now = FPlatformTime::Seconds();
			const bool bMoneyFirst = LastRemovedSeconds > 0.0 && Now - LastRemovedSeconds < 1.0;
			TArray<FVHItemData> Items;
			if (UVHTreasureCarrierComponent* Carrier = GetMyCarrier())
			{
				Items = Carrier->GetItems();
			}
			if (Items.Num() == 0 && bMoneyFirst)
			{
				Items = LastRemovedItems;
			}
			S.LastDropTreasureValue = 0;
			S.LastDropValuableCount = 0;
			S.LastDropValuableValue = 0;
			for (const FVHItemData& Item : Items)
			{
				if (Item.Kind == EVHItemKind::Treasure)
				{
					S.LastDropTreasureValue += Item.Value;
				}
				else
				{
					++S.LastDropValuableCount;
					S.LastDropValuableValue += Item.Value;
				}
			}
			const FVHServerMoneyRow* Mine = Match->GetMyServerMoney();
			S.LastBankedBefore = (Mine ? Mine->Banked : 0) + (bMoneyFirst ? static_cast<int32>(P.lost_banked) : 0);
			UE_LOG(LogVHServerGame, Log, TEXT("[쓰러짐] %s에게: 반납 %d$ 중 %u$ 잃음, 들고 있던 것 보물 %d$ + 환금 아이템 %d개(%d$) → %s"),
				bByGuard ? TEXT("경비원") : TEXT("플레이어"), S.LastBankedBefore, P.lost_banked, S.LastDropTreasureValue, S.LastDropValuableCount, S.LastDropValuableValue,
				bByGuard ? TEXT("보물만 떨어지고 환금 아이템은 사라짐") : TEXT("전부 쓰러진 자리에 떨어짐"));

			// 떨어질 보물을 기다린다: 곧 쓰러진 자리 근처(서버 흩어짐 80cm)에 생기는 보물 엔티티가 내 것이다
			DownPos = LastSentPos;
			DownSeconds = Now;
			bDownByGuard = bByGuard;
			DownExpectTreasure = S.LastDropTreasureValue;
			DownExpectValuables = bByGuard ? 0 : S.LastDropValuableCount;
			DownSeenValuables = 0;
			// 개발용(-VHBotDownTest): 보물을 들고, 지금 시험 중인 경우와 같은 상대에게 쓰러졌을 때만 시험으로 친다
			const TArray<FString>& Cases = BotDownTestCases();
			bBotDownShootRespawn = S.LastDropTreasureValue > 0 && Cases.IsValidIndex(BotDownStage)
				&& Cases[BotDownStage].Equals(bByGuard ? TEXT("Guard") : TEXT("Player"), ESearchCase::IgnoreCase);
			if (S.LastDropTreasureValue > 0)
			{
				MyDroppedTreasureId = 0;	// 예전에 떨어뜨린 보물은 이미 주웠을 것 (보물은 한 번에 하나)
				MyDroppedTreasureValue = 0;
				DroppedTreasureTaker.Reset();
			}
			// 확인 로그: 2.5초 뒤 실제로 떨어진 것과 비교
			FTimerHandle CheckHandle;
			GetWorld()->GetTimerManager().SetTimer(CheckHandle, FTimerDelegate::CreateWeakLambda(this, [this, Expect = S.LastDropTreasureValue, ExpectN = DownExpectValuables]()
			{
				const FEntity* T = Expect > 0 ? Entities.Find(MyDroppedTreasureId) : nullptr;
				UE_LOG(LogVHServerGame, Log, TEXT("[쓰러짐 확인] 보물: 기대 %d$ → %s, 환금 아이템: 기대 %d개 → 근처에 생긴 것 %d개"), Expect,
					T ? *FString::Printf(TEXT("엔티티 %u 가치 %u$ 쓰러진 자리에서 %.0fcm"), T->Id, T->Param, FVector::Dist2D(T->Pos, DownPos))
					  : (Expect > 0 ? TEXT("안 보임(문제)") : TEXT("없음")),
					ExpectN, DownSeenValuables);
			}), 2.5f, false);
			Match->ScheduleRespawn(Me, P.respawn_ms / 1000.f);
			if (UVHInteractionSubsystem* Sub = GetInteraction())
			{
				Sub->ApplyThiefStatus(Me, EVHThiefStatus::Dead);
			}
		}
		SelfState |= ES_DOWNED;		// 스냅샷이 오기 전이라도 바로 쓰러진 것으로 (이동 전송 중지)
		bAttackHeldSent = false;	// 서버가 공격 누름 상태를 지운다
		ApplyMyAttackHeldVisual();
		Pending = FPending();
		// 개발용(-VHBotDownTest): 쓰러짐 창을 화면으로 남기고 다음 경우로
		if (BotDownTestCases().Num() > 0 && bBotDownShootRespawn)	// 보물을 든 채 쓰러진 경우만 (촬영 대기 중 빈손으로 또 쓰러진 것은 건너뜀)
		{
			const FString Tag = bByGuard ? TEXT("guard") : TEXT("player");
			FTimerHandle ShotHandle;
			GetWorld()->GetTimerManager().SetTimer(ShotHandle, FTimerDelegate::CreateWeakLambda(this, [this, Tag]() { DownTestShot(*(Tag + TEXT("_1_downed"))); }), 1.5f, false);
			// 같은 장면의 "고치기 전" 문구도 찍는다 (vh.OldDeathPanel 을 잠깐 켬)
			FTimerHandle OldOn, OldShot, OldOff;
			GetWorld()->GetTimerManager().SetTimer(OldOn, FTimerDelegate::CreateWeakLambda(this, []() { if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("vh.OldDeathPanel"))) V->Set(1); }), 2.2f, false);
			GetWorld()->GetTimerManager().SetTimer(OldShot, FTimerDelegate::CreateWeakLambda(this, [this, Tag]() { DownTestShot(*(Tag + TEXT("_0_before"))); }), 2.5f, false);
			GetWorld()->GetTimerManager().SetTimer(OldOff, FTimerDelegate::CreateWeakLambda(this, []() { if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("vh.OldDeathPanel"))) V->Set(0); }), 3.0f, false);
			++BotDownStage;
			bBotDownLogged = false;
		}
	}
	if (P.attacker == SelfEntityId && P.victim != SelfEntityId)
	{
		BotHuntKillSeconds = FPlatformTime::Seconds();	// 개발용 봇(-VHBotHuntLoot): 떨어진 보물을 몇 초 뒤 주우러 간다
		BotHuntLootStep = 0;
	}
}

void UVHServerGameSubsystem::DownTestShot(const TCHAR* Tag)
{
	const FString File = FPaths::ProjectSavedDir() / TEXT("V3Test") / FString::Printf(TEXT("down_%s.png"), Tag);
	FScreenshotRequest::RequestScreenshot(File, true, false);
	UE_LOG(LogVHServerGame, Log, TEXT("[쓰러짐 시험] 화면 저장 %s"), *FPaths::GetCleanFilename(File));
}

void UVHServerGameSubsystem::OnRespawned(const SC_Respawned& P)
{
	const FVector Pos = ToVector(P.pos);
	UE_LOG(LogVHServerGame, Log, TEXT("[서버] 리스폰: %s 위치 (%.0f, %.0f, %.0f), 무적 %ums"), *NameOfEntity(P.thief), Pos.X, Pos.Y, Pos.Z, P.invuln_ms);
	FEntity* E = Entities.Find(P.thief);
	if (E)
	{
		E->Pos = Pos;
	}
	if (P.thief == SelfEntityId)
	{
		UVHThiefStateComponent* Me = GetMyThief();
		// 먼저 다시 움직일 수 있게 한 뒤(충돌·이동 켜기), 리스폰 위치로 스스로 옮긴다 (이동은 클라가 한다)
		if (UVHInteractionSubsystem* Sub = GetInteraction(); Sub && Me)
		{
			Sub->ApplyThiefStatus(Me, EVHThiefStatus::Free);
		}
		if (APawn* Pawn = GetLocalPawn())
		{
			Pawn->TeleportTo(Pos + FVector(0.f, 0.f, GetPawnHalfHeight(Pawn) + 2.f), Pawn->GetActorRotation());
			LastSentPos = Pos;
			// 리스폰 무적 표시(머리 위 표시·HUD). 판정은 서버가 한다(공격해도 풀리지 않고 시간으로만 끝남)
			if (UVHStatusEffectComponent* Effects = Pawn->FindComponentByClass<UVHStatusEffectComponent>())
			{
				Effects->ClearAll();
				Effects->ApplyTimedInvulnerable(P.invuln_ms / 1000.f);
			}
		}
		RespawnAtSeconds = 0.0;
		if (UVHMatchSubsystem* Match = GetMatch())
		{
			Match->ClearRespawn(Me);
			Match->PushNotice(FText::Format(LOCTEXT("Respawned", "리스폰! {0}초 동안 무적"), FMath::RoundToInt(P.invuln_ms / 1000.f)));
		}
		// 스냅샷의 쓰러짐 비트가 풀리기 전이라도 위치를 바로 알린다
		SelfState &= ~static_cast<uint32>(ES_DOWNED);
		SendCurrentPositionNow();
		// 떨어뜨린 보물이 아직 바닥에 있으면 몇 초 동안 화면에 그쪽 방향을 띄운다 (누가 주우면 바로 사라짐)
		if (MyDroppedTreasureId != 0 && Entities.Contains(MyDroppedTreasureId))
		{
			DroppedHintUntil = FPlatformTime::Seconds() + UVHSettings::Get().GetStyle().DroppedTreasureHintSeconds;
			UE_LOG(LogVHServerGame, Log, TEXT("[쓰러짐] 리스폰: 떨어뜨린 보물 %u (%d$) 방향 표시 %.0f초, 거리 %.0fm"), MyDroppedTreasureId, MyDroppedTreasureValue,
				UVHSettings::Get().GetStyle().DroppedTreasureHintSeconds, FVector::Dist2D(Pos, Entities[MyDroppedTreasureId].Pos) / 100.f);
		}
		if (BotDownTestCases().Num() > 0 && bBotDownShootRespawn)
		{
			bBotDownShootRespawn = false;
			// 개발용: 촬영하는 동안 가만히 (리스폰 1초·4초·9초 뒤 화면)
			BotDownIdleUntil = FPlatformTime::Seconds() + 12.0;
			const FString Tag = bDownByGuard ? TEXT("guard") : TEXT("player");
			const float Times[] = { 1.f, 4.f, 9.f };
			for (int32 i = 0; i < 3; ++i)
			{
				FTimerHandle Handle;
				GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Tag, i, T = Times[i]]()
				{
					DownTestShot(*FString::Printf(TEXT("%s_%d_respawn%.0fs"), *Tag, i + 2, T));
				}), Times[i], false);
			}
		}
	}
	else if (E && E->Proxy.IsValid())
	{
		E->Proxy->SnapTo(Pos, E->Yaw);
	}
}

void UVHServerGameSubsystem::OnPickup(const SC_Pickup& P)
{
	// 몸 동작: 주운 사람이 줍는 동작 (상체만 → 걸으면서 주워도 다리는 그대로)
	if (UVHWeaponVisualComponent* Picker = VisualOfEntity(P.thief))
	{
		Picker->PlayBodyAction(EVHBodyAction::PickUp);
		if (Picker->GetOwner())
		{
			UVHSoundConfig::Play(this, EVHSound::PickUp, Picker->GetOwner()->GetActorLocation());
		}
	}
	FString What;
	switch (P.kind)
	{
	case EntityKind::Valuable:
	{
		const FVHItemData Item = ItemForValuable(P.type_id, P.param);
		What = Item.Kind == EVHItemKind::Treasure
			? FString::Printf(TEXT("%s %s"), *Item.DisplayName.ToString(), *VHRanking::Money(Item.Value))
			: FString::Printf(TEXT("%s(%s) %s"), *Item.DisplayName.ToString(), *AVHDroppedTreasure::QualityText(Item.GetQuality()).ToString(), *VHRanking::Money(Item.Value));
		break;
	}
	case EntityKind::WeaponPickup:		What = FString::Printf(TEXT("%s (탄 %u)"), WeaponName(static_cast<WeaponId>(P.type_id)), P.param); break;
	case EntityKind::ConsumablePickup:	What = ConsumableName(static_cast<ConsumableId>(P.type_id)); break;
	case EntityKind::FieldItem:			What = FieldItemName(static_cast<FieldItemId>(P.type_id)); break;
	default:							What = KindName(P.kind); break;
	}
	UE_LOG(LogVHServerGame, Log, TEXT("[서버] 획득: %s ← %s"), *NameOfEntity(P.thief), *What);
	if (P.thief != SelfEntityId)
	{
		return;
	}
	// 소비 아이템은 1개만 든다: 들고 있던 것이 있으면 서버가 바꾸고 들고 있던 것을 그 자리에 떨어뜨린다 (Q-25·A-03)
	FText Notice = FText::Format(LOCTEXT("Picked", "획득: {0}"), FText::FromString(What));
	if (P.kind == EntityKind::ConsumablePickup)
	{
		const uint8 Prev = (Pending.bInstant && Pending.Seq != 0) ? Pending.PrevConsumable : MyStatus.Consumable;
		const uint8 New = static_cast<uint8>(P.type_id);
		const FString PrevName = ConsumableName(static_cast<ConsumableId>(Prev));
		if (Prev != 0 && Prev == New)
		{
			Notice = FText::Format(LOCTEXT("PickedSameConsumable", "이미 {0} 들고 있습니다 (바뀐 것 없음)"), FText::FromString(WithJosa(What, TEXT("을"), TEXT("를"))));
		}
		else if (Prev != 0)
		{
			Notice = FText::Format(LOCTEXT("SwappedConsumable", "{0} 교체 — 들고 있던 {1} 바닥에 내려놓았습니다"),
				FText::FromString(WithJosa(What, TEXT("으로"), TEXT("로"))), FText::FromString(WithJosa(PrevName, TEXT("은"), TEXT("는"))));
		}
		UE_LOG(LogVHServerGame, Log, TEXT("[줍기] 소비 아이템: 들고 있던 것 %s → %s (%s)"), *PrevName, *What,
			Prev == 0 ? TEXT("새로 듦") : (Prev == New ? TEXT("같은 종류 — 서버가 바꿔 줘서 결과가 같음") : TEXT("교체")));
		// 개발용(-VHBotPickTest): 연막탄 줍기 세 경우
		if (New == SmokeId)
		{
			const TCHAR* Case = nullptr;
			const TCHAR* Tag = nullptr;
			if (GPickTestStage == 0 && Prev == 0) { Case = TEXT("경우1 (빈손)"); Tag = TEXT("case1"); GPickTestStage = 1; }
			else if (GPickTestStage == 1 && Prev != 0 && Prev != SmokeId) { Case = TEXT("경우2 (다른 소비 아이템을 든 채)"); Tag = TEXT("case2"); GPickTestStage = 2; }
			else if (GPickTestStage == 2 && Prev == SmokeId) { Case = TEXT("경우3 (연막탄을 든 채, 서버로 보냄)"); Tag = TEXT("case3"); GPickTestStage = 3; }
			if (Case)
			{
				UE_LOG(LogVHServerGame, Log, TEXT("[줍기시험] %s: SC_PICKUP 받음 — 들고 있던 것 %s → 연막탄, 알림 \"%s\""), Case, *PrevName, *Notice.ToString());
				PickTestShot(GetWorld(), Tag);
			}
		}
	}
	if (UVHMatchSubsystem* Match = GetMatch())
	{
		Match->PushNotice(Notice);
	}
	if (P.kind != EntityKind::FieldItem)
	{
		TickCause.OnInteractConfirmed();	// F로 줍기가 성사됨 (필드 아이템은 닿아서 얻는 것이라 상호작용이 아니다)
	}
	// 내가 주운 환금 아이템: 소지품에 넣는다(들고 있는 가치 합계는 SC_MONEY 로 온다). 화면의 아이템은 SC_ENTITY_DESPAWN 으로 사라진다
	if (P.kind == EntityKind::Valuable)
	{
		if (UVHTreasureCarrierComponent* Carrier = GetMyCarrier())
		{
			Carrier->ApplyAdd(ItemForValuable(P.type_id, P.param));
		}
	}
	if (Pending.bInstant)
	{
		Pending = FPending();
	}
	// 개발용 봇(-VHBotLoot): 무기를 주우면 휠(무기 교체), 소비 아이템을 주우면 4초 뒤 E(사용)까지 보내 본다
	static const bool bLootBot = FParse::Param(FCommandLine::Get(), TEXT("VHBotLoot"));
	if (bLootBot && P.kind == EntityKind::WeaponPickup)
	{
		SendSwitchWeapon();
	}
	if (bLootBot && P.kind == EntityKind::ConsumablePickup)
	{
		BotUseConsumableAt = FPlatformTime::Seconds() + 4.0;
	}
}

void UVHServerGameSubsystem::OnReject(const SC_ActionReject& P)
{
	FText Detail;
	const EVHRejectReason Reason = ToLocalReject(P.reason, Detail);
	UE_LOG(LogVHServerGame, Log, TEXT("[서버] 행동 거절 seq %u action %d reason %d"), P.seq, (int32)P.action, (int32)P.reason);
	UVHInteractionSubsystem* Sub = GetInteraction();
	if (!Sub)
	{
		return;
	}
	switch (P.action)
	{
	case ActionType::InteractBegin:
	case ActionType::InteractCancel:
		Sub->ApplyReject(GetMyInteraction(), Reason, Detail);
		if (Pending.Seq == P.seq)
		{
			Pending = FPending();
		}
		break;
	case ActionType::AttackStart:
		bAttackHeldSent = false;
		bTaserShotPress = false;
		if (bRailAwaitFire)
		{
			bRailAwaitFire = false;
			UE_LOG(LogVHServerGame, Log, TEXT("[레일건] 공격 신호 거절됨 (reason %d) → 발사 없음"), (int32)P.reason);
		}
		ApplyMyAttackHeldVisual();
		Sub->PostMessage(Detail.IsEmpty() ? LOCTEXT("AttackRejected", "지금은 공격할 수 없습니다") : Detail, true);
		break;
	case ActionType::Dodge:
		Sub->PostMessage(Detail.IsEmpty() ? LOCTEXT("DodgeRejected", "지금은 구를 수 없습니다") : Detail, true);
		break;
	case ActionType::SwitchWeapon:
		Sub->PostMessage(Detail.IsEmpty() ? LOCTEXT("SwitchRejected", "지금은 무기를 바꿀 수 없습니다") : Detail, true);
		break;
	case ActionType::UseSkill:
		Sub->PostMessage(P.reason == RejectReason::Cooldown ? LOCTEXT("SkillCooldown", "스킬 쿨타임 중입니다")
			: (P.reason == RejectReason::NotAvailable ? LOCTEXT("NoSkill", "이 캐릭터는 스킬이 없습니다") : (Detail.IsEmpty() ? LOCTEXT("SkillRejected", "지금은 스킬을 쓸 수 없습니다") : Detail)), true);
		break;
	case ActionType::UseConsumable:
		Sub->PostMessage(P.reason == RejectReason::NotAvailable ? LOCTEXT("NoConsumable", "소비 아이템이 없습니다")
			: (Detail.IsEmpty() ? LOCTEXT("ConsumableRejected", "지금은 소비 아이템을 쓸 수 없습니다") : Detail), true);
		break;
	default:
		break;
	}
}

// ───────────────────────── 보내기 ─────────────────────────

void UVHServerGameSubsystem::SendInteractBegin(UVHInteractionComponent* By, AVHInteractableBase* Target, const FVHInteractOffer& Offer)
{
	UVHNetSubsystem* N = Net.Get();
	UVHInteractionSubsystem* Sub = GetInteraction();
	const uint16 Id = FindEntityIdFor(Target);
	if (!N || !Sub || Id == 0)
	{
		if (Sub)
		{
			Sub->ApplyReject(By, EVHRejectReason::InvalidState, LOCTEXT("NoEntity", "서버에 없는 대상입니다"));
		}
		UE_LOG(LogVHServerGame, Warning, TEXT("상호작용 대상 %s 의 서버 엔티티를 찾지 못함"), *GetNameSafe(Target));
		return;
	}
	const FEntity& E = Entities[Id];
	// 같은 종류 소비 아이템: 서버는 바꿔 주고(들고 있던 것을 그 자리에 떨어뜨림) 결과가 똑같아 "안 주워진 것"처럼 보인다
	// → 보내지 않고 알린다 (서버 규칙 Q-25·A-03: 소비 아이템은 1개만, 주우면 교체)
	const bool bSameConsumable = E.Kind == EntityKind::ConsumablePickup && MyStatus.bValid && MyStatus.Consumable != 0 && E.TypeId == MyStatus.Consumable;
	if (bSameConsumable && !bForceSameConsumableOnce)
	{
		const FString Name = ConsumableName(static_cast<ConsumableId>(E.TypeId));
		Sub->ApplyReject(By, EVHRejectReason::InvalidState,
			FText::Format(LOCTEXT("SameConsumable", "이미 {0} 들고 있습니다"), FText::FromString(WithJosa(Name, TEXT("을"), TEXT("를")))));
		UE_LOG(LogVHServerGame, Log, TEXT("→ 줍기 보내지 않음: 소비아이템 %u (%s) = 들고 있는 것과 같은 종류"), Id, *Name);
		if (GPickTestStage == 3)
		{
			UE_LOG(LogVHServerGame, Log, TEXT("[줍기시험] 경우3-2 (클라 막기): 연막탄을 든 채 연막탄 F → 서버로 보내지 않고 \"이미 연막탄을 들고 있습니다\" 알림"));
			GPickTestStage = 4;
			PickTestShot(GetWorld(), TEXT("case3b"));
		}
		return;
	}
	bForceSameConsumableOnce = false;
	// 서버는 마지막 CS_MOVE 위치로 거리를 잰다 → 상호작용 직전에 지금 위치를 한 번 더 보낸다 (최대 50ms 늦은 위치로 OutOfRange 방지)
	SendCurrentPositionNow();
	const uint16 Seq = N->SendAction(ActionType::InteractBegin, GetLookYaw(), Id, GetAimPoint());
	if (Seq == 0)
	{
		return;
	}
	Pending = FPending();
	Pending.Seq = Seq;
	Pending.TargetId = Id;
	Pending.By = By;
	Pending.Target = Target;
	Pending.Offer = Offer;
	Pending.SentTime = FPlatformTime::Seconds();
	Pending.bInstant = IsInstantKind(E.Kind);
	Pending.PrevConsumable = MyStatus.Consumable;
	if (E.Kind == EntityKind::ConsumablePickup)
	{
		UE_LOG(LogVHServerGame, Log, TEXT("→ 상호작용 시작 소비아이템 %u = %s (seq %u, 즉시) — 들고 있던 소비: %s"), Id,
			ConsumableName(static_cast<ConsumableId>(E.TypeId)), Seq, ConsumableName(static_cast<ConsumableId>(MyStatus.Consumable)));
	}
	else
	{
		UE_LOG(LogVHServerGame, Log, TEXT("→ 상호작용 시작 %s %u (seq %u%s)"), KindName(E.Kind), Id, Seq, Pending.bInstant ? TEXT(", 즉시") : TEXT(""));
	}
}

void UVHServerGameSubsystem::SendInteractCancel(UVHInteractionComponent* By)
{
	UVHNetSubsystem* N = Net.Get();
	if (!N || (!Pending.TargetId && !(By && By->IsInteracting())))
	{
		return;
	}
	LastOwnActionSentTime = FPlatformTime::Seconds();	// 내가 멈춘 것 (서버가 멈춘 것과 구분)
	// 클라 진행 막대가 (거의) 다 찬 뒤에 손을 떼면, 서버는 이미 완료했거나 곧 완료한다(지연·틱 차이).
	// 이때 취소를 보내면 서버가 완료 직후의 취소로 받아 금고 열기가 끊긴다
	// → 바로 보내지 않고 서버 완료를 기다린다. 오지 않으면 Tick 에서 취소를 보낸다.
	if (By && By->IsInteracting() && Pending.TargetId != 0 && !Pending.bInstant
		&& By->GetActiveRemainingSeconds() <= CancelGraceSeconds)
	{
		if (!Pending.bCancelDeferred)
		{
			Pending.bCancelDeferred = true;
			Pending.CancelDeferredTime = FPlatformTime::Seconds();
			UE_LOG(LogVHServerGame, Log, TEXT("→ 상호작용 취소 보류: 완료 직전(남은 %.2f초)이라 서버 완료를 기다림"), By->GetActiveRemainingSeconds());
		}
		return;
	}
	N->SendAction(ActionType::InteractCancel, GetLookYaw(), 0);
	UE_LOG(LogVHServerGame, Log, TEXT("→ 상호작용 취소"));
}

void UVHServerGameSubsystem::SendCurrentPositionNow()
{
	UVHNetSubsystem* N = Net.Get();
	const APawn* Pawn = GetLocalPawn();
	if (!N || !bActive || !Pawn || HasSelfState(ES_DOWNED))
	{
		return;
	}
	const FVector Foot = Pawn->GetActorLocation() - FVector(0.f, 0.f, GetPawnHalfHeight(Pawn));
	const float Yaw = GetLookYaw();
	if (N->SendMove(Foot, Yaw, LastSentFlags) != 0)
	{
		LastSentPos = Foot;
		LastSentYaw = Yaw;
		LastMoveSendTime = FPlatformTime::Seconds();
	}
}

uint8 UVHServerGameSubsystem::MyActiveWeapon() const
{
	return MyStatus.bValid ? MyStatus.WeaponId[FMath::Clamp(MyStatus.ActiveSlot, 0, 1)] : 0;
}

bool UVHServerGameSubsystem::IsRailgunCharging() const
{
	if (bRailAwaitFire)
	{
		return true;
	}
	return PendingRailShots.ContainsByPredicate([this](const FRailShot& S) { return S.Shooter == SelfEntityId && !S.bFired; });
}

void UVHServerGameSubsystem::CancelRailgunShot(uint16 Shooter, const TCHAR* Reason)
{
	for (int32 i = PendingRailShots.Num() - 1; i >= 0; --i)
	{
		FRailShot& S = PendingRailShots[i];
		if (S.Shooter != Shooter)
		{
			continue;
		}
		const bool bWasCharging = !S.bFired;
		if (UVHWeaponVisualComponent* V = S.Visual.Get())
		{
			V->SetRailgunCharging(false);
			V->SetHeldWeaponOverride(-1);
		}
		if (bWasCharging)
		{
			UE_LOG(LogVHServerGame, Log, TEXT("[레일건] %s 충전 연출 취소: %s (SC_FIRE 후 %.0fms, 서버도 발사하지 않음)"), *NameOfEntity(Shooter), Reason,
				(FPlatformTime::Seconds() - (S.FireAt - RailgunChargeSeconds)) * 1000.0);
			if (Shooter == SelfEntityId)
			{
				RailLastCancel = Reason;
			}
		}
		PendingRailShots.RemoveAt(i);
	}
}

void UVHServerGameSubsystem::TickRailgunShots()
{
	const double Now = FPlatformTime::Seconds();
	// 내 공격 신호를 보냈는데 0.6초 안에 서버 발사(SC_FIRE)가 없으면 끝낸다 (거절·가짜 서버 등)
	if (bRailAwaitFire && Now - RailClickTime > RailgunAwaitFireSeconds)
	{
		UE_LOG(LogVHServerGame, Warning, TEXT("[레일건] 공격 신호 후 %.0fms 동안 서버 발사(SC_FIRE)가 없음 → 공격 끝"), (Now - RailClickTime) * 1000.0);
		bRailAwaitFire = false;
		if (bAttackHeldSent)
		{
			bAttackHeldSent = false;
			if (UVHNetSubsystem* N = Net.Get())
			{
				N->SendAction(ActionType::AttackStop, GetLookYaw(), 0);
			}
			ApplyMyAttackHeldVisual();
		}
	}
	for (int32 i = PendingRailShots.Num() - 1; i >= 0; --i)
	{
		FRailShot& S = PendingRailShots[i];
		const FEntity* Shooter = Entities.Find(S.Shooter);
		if (!S.bFired)
		{
			// 충전 중 쏜 사람이 쓰러지면 서버는 발사하지 않는다 (서버 W-17)
			if (!Shooter)
			{
				CancelRailgunShot(S.Shooter, TEXT("쏜 사람이 사라짐"));
				continue;
			}
			if ((Shooter->State & ES_DOWNED) != 0 || (S.Shooter == SelfEntityId && HasSelfState(ES_DOWNED)))
			{
				CancelRailgunShot(S.Shooter, TEXT("충전 중 쓰러짐"));
				continue;
			}
			if (Now < S.FireAt)
			{
				continue;
			}
			// 충전 끝 = 서버 첫 판정: 발사 동작·소리·빔 (빔은 서버가 판정하는 1초 동안)
			S.bFired = true;
			UVHWeaponVisualComponent* V = S.Visual.Get();
			if (V)
			{
				V->SetRailgunCharging(false);
				V->PlayAttack(static_cast<uint8>(WeaponId::Railgun));
			}
			const FVector At = V && V->GetOwner() ? V->GetOwner()->GetActorLocation() : S.Origin;
			UVHSoundConfig::Play(this, UVHSoundConfig::FireSound(static_cast<uint8>(WeaponId::Railgun)), At);
			if (UVHWeaponFxSubsystem* Fx = UVHWeaponFxSubsystem::Get(this))
			{
				Fx->OnFire(static_cast<uint8>(WeaponId::Railgun), S.Origin, S.Yaw, nullptr, V);
			}
			if (S.Shooter == SelfEntityId)
			{
				++RailBeamsSeen;
				UE_LOG(LogVHServerGame, Log, TEXT("[레일건] 충전 끝 → 빔 (클릭 후 %.0fms, SC_FIRE 후 %.0fms, yaw %.0f)"), (Now - RailClickTime) * 1000.0,
					(Now - (S.FireAt - RailgunChargeSeconds)) * 1000.0, S.Yaw);
			}
			else
			{
				UE_LOG(LogVHServerGame, Log, TEXT("[레일건] %s 충전 끝 → 빔 (yaw %.0f)"), *NameOfEntity(S.Shooter), S.Yaw);
			}
			continue;
		}
		if (Now >= S.ReleaseAt)
		{
			if (UVHWeaponVisualComponent* V = S.Visual.Get())
			{
				V->SetHeldWeaponOverride(-1);	// 빔 끝: 서버 무기로 (마지막 탄이었으면 이제 삼단봉)
			}
			PendingRailShots.RemoveAt(i);
		}
	}
}

void UVHServerGameSubsystem::SendAttackHeld(bool bHeld)
{
	UVHNetSubsystem* N = Net.Get();
	if (!N || !bActive)
	{
		return;
	}
	// 사제 테이저건을 든 동안: 공격 키 = 테이저 발사. 서버가 AttackStart 를 받으면 무기 대신 테이저를 쏘고(SC_SKILL Cast) 누름을 지운다.
	// 이 누름은 무기 공격이 아니므로 꾹 누르기 자세는 보이지 않는다(bTaserShotPress). 손을 떼면 AttackStop 은 그대로 보낸다(서버에서는 아무 일 없음)
	if (bHeld && bSkillTaserHeld)
	{
		SendCurrentPositionNow();	// 서버는 마지막 CS_MOVE 위치와 이 yaw 로 3m 판정한다
		if (N->SendAction(ActionType::AttackStart, GetLookYaw(), 0, GetAimPoint()) != 0)
		{
			bAttackHeldSent = true;
			bTaserShotPress = true;
			LastOwnActionSentTime = FPlatformTime::Seconds();
			UE_LOG(LogVHServerGame, Log, TEXT("[테이저] 든 채 공격 키 → AttackStart (서버가 테이저 발사) yaw %.0f"), GetLookYaw());
		}
		ApplyMyAttackHeldVisual();
		return;
	}
	// 레일건: 클릭 한 번 = 한 발. 누르는 즉시 AttackStart → 서버가 바로 SC_FIRE(충전 시작). 내 SC_FIRE 를 받으면 AttackStop 을 보낸다(OnFire).
	// SC_FIRE 를 기다리는 동안 손을 떼도 AttackStop 을 미룬다 (같은 틱에 누름·뗌이 모두 처리되면 서버가 쏘지 않으므로)
	if (bRailAwaitFire)
	{
		if (bHeld)
		{
			UE_LOG(LogVHServerGame, Log, TEXT("[레일건] 서버 발사를 기다리는 중 클릭 → 무시"));
		}
		return;
	}
	if (bHeld && MyActiveWeapon() == static_cast<uint8>(WeaponId::Railgun))
	{
		if (HasSelfState(ES_RAILGUN_LOCK))
		{
			UE_LOG(LogVHServerGame, Log, TEXT("[레일건] 충전·발사 잠금(2초) 중 클릭 → 무시"));
			return;
		}
		if (HasSelfState(ES_DOWNED) || HasSelfState(ES_STUNNED))
		{
			return;
		}
		SendCurrentPositionNow();
		const float Yaw = GetLookYaw();
		const uint16 Seq = N->SendAction(ActionType::AttackStart, Yaw, 0, GetAimPoint());
		if (Seq != 0)
		{
			bAttackHeldSent = true;
			bRailAwaitFire = true;
			RailClickTime = FPlatformTime::Seconds();
			LastOwnActionSentTime = RailClickTime;
			UE_LOG(LogVHServerGame, Log, TEXT("[레일건] 클릭 → 공격 신호 바로 전송 (AttackStart seq %u, yaw %.0f) — 충전은 서버 SC_FIRE 부터 1초"), Seq, Yaw);
		}
		return;
	}
	if (bHeld)
	{
		// 서버는 마지막 CS_MOVE 위치·시선으로 판정한다 → 공격 직전 위치를 한 번 더 보낸다
		SendCurrentPositionNow();
		if (N->SendAction(ActionType::AttackStart, GetLookYaw(), 0, GetAimPoint()) != 0)
		{
			LastOwnActionSentTime = FPlatformTime::Seconds();	// 공격하면 서버가 내 작업을 멈춘다
			bAttackHeldSent = true;
			UE_LOG(LogVHServerGame, Log, TEXT("→ 공격 시작 (꾹 누르기) yaw %.0f"), GetLookYaw());
		}
	}
	else if (bAttackHeldSent)
	{
		bAttackHeldSent = false;
		N->SendAction(ActionType::AttackStop, GetLookYaw(), 0);
		UE_LOG(LogVHServerGame, Log, TEXT("→ 공격 끝"));
	}
	if (!bHeld)
	{
		bTaserShotPress = false;
	}
	// 꾹 누르기 표시(화염방사기 불꽃)를 보낸 그 순간 바로 맞춘다.
	// (예전에는 SC_PLAYER_STATUS 가 올 때만 맞춰서, 빠르게 연타하면 마지막 '공격 끝' 뒤에 상태 패킷이 오지 않아 불꽃이 켜진 채 남았다)
	ApplyMyAttackHeldVisual();
}

namespace
{
	// vh.Press skill|attack|wheel : Q 키·좌클릭(누르고 0.1초 뒤 뗌)·마우스 휠과 같은 처리 (봇 스크립트로 사제 테이저건 시험)
	FAutoConsoleCommandWithWorldAndArgs GVHPressCommand(TEXT("vh.Press"),
		TEXT("개발용: vh.Press skill|attack|wheel — Q 키 / 좌클릭(0.1초) / 마우스 휠을 누른 것과 같은 처리 (실서버 게임 중)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UVHServerGameSubsystem* Server = World ? World->GetSubsystem<UVHServerGameSubsystem>() : nullptr;
			if (!Server || !Server->IsActive() || Args.Num() < 1)
			{
				return;
			}
			UE_LOG(LogVHServerGame, Log, TEXT("[개발] vh.Press %s (스킬 쿨타임 %.1f초, 테이저 %s)"), *Args[0], Server->GetSkillCooldownRemaining(),
				Server->IsSkillTaserHeld() ? TEXT("들고 있음") : TEXT("안 듦"));
			if (Args[0].Equals(TEXT("skill"), ESearchCase::IgnoreCase))
			{
				Server->PressSkillKey();
			}
			else if (Args[0].Equals(TEXT("wheel"), ESearchCase::IgnoreCase))
			{
				Server->SendSwitchWeapon();
			}
			else if (Args[0].Equals(TEXT("attack"), ESearchCase::IgnoreCase))
			{
				Server->SendAttackHeld(true);
				TWeakObjectPtr<UVHServerGameSubsystem> Weak(Server);
				FTimerHandle Handle;
				World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([Weak]()
				{
					if (Weak.IsValid())
					{
						Weak->SendAttackHeld(false);
					}
				}), 0.1f, false);
			}
		}));

	// vh.AttackSpam 횟수 [누름초] [뗌초] : 공격 시작/끝을 빠르게 되풀이 (화염방사기 연타 시험). 끝나고 0.5초 뒤 꾹 누르기 표시·불꽃 수를 로그로
	FAutoConsoleCommandWithWorldAndArgs GVHAttackSpamCommand(TEXT("vh.AttackSpam"),
		TEXT("개발용: vh.AttackSpam 횟수 [누름초 0.06] [뗌초 0.06] — 공격 시작/끝을 빠르게 되풀이하고 끝난 뒤 불꽃이 꺼졌는지 로그 [연타 시험]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UVHServerGameSubsystem* Sub = World ? World->GetSubsystem<UVHServerGameSubsystem>() : nullptr;
			if (!Sub)
			{
				return;
			}
			const int32 Count = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 200) : 10;
			const float Down = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.06f;
			const float Up = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 0.06f;
			TWeakObjectPtr<UVHServerGameSubsystem> Weak(Sub);
			float T = 0.f;
			for (int32 i = 0; i < Count; ++i)
			{
				FTimerHandle A, B;
				World->GetTimerManager().SetTimer(A, FTimerDelegate::CreateLambda([Weak]() { if (Weak.IsValid()) Weak->SendAttackHeld(true); }), FMath::Max(T, 0.001f), false);
				T += Down;
				World->GetTimerManager().SetTimer(B, FTimerDelegate::CreateLambda([Weak]() { if (Weak.IsValid()) Weak->SendAttackHeld(false); }), T, false);
				T += Up;
			}
			FTimerHandle C;
			TWeakObjectPtr<UWorld> WeakWorld(World);
			World->GetTimerManager().SetTimer(C, FTimerDelegate::CreateLambda([WeakWorld, Count]()
			{
				UWorld* W = WeakWorld.Get();
				const APawn* Pawn = W && W->GetFirstPlayerController() ? W->GetFirstPlayerController()->GetPawn() : nullptr;
				const UVHWeaponVisualComponent* V = Pawn ? Pawn->FindComponentByClass<UVHWeaponVisualComponent>() : nullptr;
				const UVHWeaponFxSubsystem* Fx = W ? UVHWeaponFxSubsystem::Get(W) : nullptr;
				UE_LOG(LogVHServerGame, Log, TEXT("[연타 시험] %d번 끝 + 0.5초: 든 무기 %d, 꾹 누르기 표시 %.2f, 꾹 누르기 이펙트 %d개 → %s"), Count,
					V ? V->GetWeapon() : -1, V ? V->GetHoldAlpha() : -1.f, Fx ? Fx->GetHeldFxCount() : -1,
					(V && V->GetHoldAlpha() < 0.05f && Fx && Fx->GetHeldFxCount() == 0) ? TEXT("꺼짐 (정상)") : TEXT("남아 있음 (버그)"));
			}), T + 0.5f, false);
		}));
}

void UVHServerGameSubsystem::ApplyMyAttackHeldVisual()
{
	const APawn* Pawn = GetLocalPawn();
	if (UVHWeaponVisualComponent* Visual = Pawn ? Pawn->FindComponentByClass<UVHWeaponVisualComponent>() : nullptr)
	{
		// 테이저건을 든 채 누른 공격 키는 테이저 한 발이라 무기의 꾹 누르기 자세(화염방사기 불꽃 등)를 보이지 않는다
		Visual->SetAttackHeld(bAttackHeldSent && !bTaserShotPress && !HasSelfState(ES_DOWNED));
	}
}

void UVHServerGameSubsystem::SendDodge()
{
	if (UVHNetSubsystem* N = Net.Get(); N && bActive)
	{
		N->SendAction(ActionType::Dodge, GetLookYaw(), 0);
		LastOwnActionSentTime = FPlatformTime::Seconds();	// 구르면 서버가 내 작업을 멈춘다
		UE_LOG(LogVHServerGame, Log, TEXT("→ 구르기"));
	}
}

void UVHServerGameSubsystem::SendSwitchWeapon()
{
	// 테이저건을 든 채 휠: 서버가 무기를 바꾸고 테이저건을 집어넣는다(SC_SKILL End, 쿨타임 없음). 레일건 충전·발사 중이면 서버가 Locked 로 거절한다
	if (UVHNetSubsystem* N = Net.Get(); N && bActive)
	{
		N->SendAction(ActionType::SwitchWeapon, GetLookYaw(), 0);
		UE_LOG(LogVHServerGame, Log, TEXT("→ 무기 교체%s"), bSkillTaserHeld ? TEXT(" (테이저건 든 상태 → 서버가 집어넣음)") : TEXT(""));
	}
}

void UVHServerGameSubsystem::SendUseSkill()
{
	if (UVHNetSubsystem* N = Net.Get(); N && bActive)
	{
		SendCurrentPositionNow();
		N->SendAction(ActionType::UseSkill, GetLookYaw(), 0, GetAimPoint());
		LastOwnActionSentTime = FPlatformTime::Seconds();	// 은신을 켜면 서버가 내 작업을 멈춘다
		UE_LOG(LogVHServerGame, Log, TEXT("→ 스킬"));
	}
}

float UVHServerGameSubsystem::GetSkillCooldownRemaining() const
{
	if (!MyStatus.bValid)
	{
		return 0.f;
	}
	return FMath::Max(0.f, MyStatus.SkillCooldownMs / 1000.f - static_cast<float>(FPlatformTime::Seconds() - MyStatus.ReceivedSeconds));
}

void UVHServerGameSubsystem::PressSkillKey()
{
	if (!bActive)
	{
		return;
	}
	// 끈끈이·은신(또는 스킬 없음): 예전처럼 누르는 순간 사용
	if (MyStatus.Skill != static_cast<uint8>(SkillId::Taser))
	{
		SendUseSkill();
		return;
	}
	// 사제 테이저건: Q = UseSkill. 든 상태면 서버가 집어넣고(End), 아니면 든다(Active). 손에 드는 모습은 서버 응답(SC_SKILL·ES_TASER_READY)으로 바뀐다
	if (bSkillTaserHeld)
	{
		UE_LOG(LogVHServerGame, Log, TEXT("[테이저] 든 채 Q → UseSkill (서버가 집어넣음, 쿨타임 없음)"));
		SendUseSkill();
		return;
	}
	const float Cooldown = GetSkillCooldownRemaining();
	if (Cooldown > 0.f)
	{
		// 서버도 Cooldown 으로 거절한다. 보내지 않고 남은 시간만 알린다 (서버가 알려 준 쿨타임 기준)
		UE_LOG(LogVHServerGame, Log, TEXT("[테이저] 들기 안 됨: 쿨타임 %.1f초 남음"), Cooldown);
		if (UVHMatchSubsystem* Match = GetMatch())
		{
			Match->PushNotice(FText::Format(LOCTEXT("TaserCooldown", "사제 테이저건 쿨타임 {0}초"), FText::AsNumber(FMath::CeilToInt(Cooldown))));
		}
		return;
	}
	if (bAttackHeldSent)
	{
		// 서버는 든 순간 공격 키가 눌려 있으면 바로 테이저를 쏜다 → 들고 있던 무기 공격을 먼저 멈춘다
		SendAttackHeld(false);
	}
	UE_LOG(LogVHServerGame, Log, TEXT("[테이저] Q → UseSkill (서버가 들기)"));
	SendUseSkill();
}

void UVHServerGameSubsystem::SetSkillTaserHeld(bool bHeld, const TCHAR* Why)
{
	if (bSkillTaserHeld == bHeld)
	{
		return;
	}
	bSkillTaserHeld = bHeld;
	UE_LOG(LogVHServerGame, Log, TEXT("[테이저] %s → %s | 쿨타임 남음 %.1f초"), Why, bHeld ? TEXT("손에 듦") : TEXT("집어넣음"), GetSkillCooldownRemaining());
	UpdateMyWeaponVisual();
}

void UVHServerGameSubsystem::SendUseConsumable()
{
	if (UVHNetSubsystem* N = Net.Get(); N && bActive)
	{
		SendCurrentPositionNow();
		N->SendAction(ActionType::UseConsumable, GetLookYaw(), 0, GetAimPoint());
		LastOwnActionSentTime = FPlatformTime::Seconds();	// 제트팩을 쓰면 서버가 내 작업을 멈춘다
		UE_LOG(LogVHServerGame, Log, TEXT("→ 소비 아이템 사용"));
	}
}

void UVHServerGameSubsystem::Tick(float DeltaTime)
{
	if (!bActive || !bStarted)
	{
		return;
	}
	++FramesSeen;
	if (!bSafesHidden && GetWorld()->HasBegunPlay())
	{
		HideLevelSafes();
	}
	// 즉시 상호작용 응답이 2초 안에 없으면 잊는다
	if (Pending.bInstant && FPlatformTime::Seconds() - Pending.SentTime > 2.0)
	{
		Pending = FPending();
	}
	// 미뤄 둔 취소: 기다려도 서버 완료가 없으면 이제 보낸다
	if (Pending.bCancelDeferred && FPlatformTime::Seconds() - Pending.CancelDeferredTime > CancelWaitSeconds)
	{
		if (UVHNetSubsystem* N = Net.Get())
		{
			N->SendAction(ActionType::InteractCancel, GetLookYaw(), 0);
			LastOwnActionSentTime = FPlatformTime::Seconds();
			UE_LOG(LogVHServerGame, Log, TEXT("→ 상호작용 취소 (서버 완료가 %.1f초 안에 오지 않음)"), CancelWaitSeconds);
		}
		Pending.bCancelDeferred = false;
	}
	if (!bSelfSpeedApplied && GetLocalPawn())
	{
		// 게임 시작 뒤 한 번: 운동화 퍽(달리기 +20%)과 지금 상태 비트를 내 이동 속도에 반영
		bSelfSpeedApplied = true;
		UpdateSelfSpeed();
		if (const FVHRoomMember* Me = Net.IsValid() ? Net->GetMyMember() : nullptr)
		{
			FString Perks;
			for (int32 Perk : Me->Perks)
			{
				Perks += FString::Printf(TEXT(" %d"), Perk);
			}
			UE_LOG(LogVHServerGame, Log, TEXT("내 캐릭터 %d, 퍽%s → 달리기 배율 %.2f, 작업 속도 %.2f"), Me->CharacterId, Perks.IsEmpty() ? TEXT(" 없음") : *Perks,
				HasMyPerk(VHPerk::Sneakers) ? UVHSettings::Get().GetGameplayConfig().SneakersRunScale : 1.f, GetMyWorkSpeed());
		}
	}
	TickRailgunShots();
	TickBotWalk(DeltaTime);
	SendMoveIfNeeded();
	// 개발용 흉내로 만든 엔티티(지뢰·연막·끈끈이)는 시간이 지나면 지운다
	if (FakeEntityUntil.Num() > 0)
	{
		const double FakeNow = FPlatformTime::Seconds();
		TArray<uint16> Expired;
		for (const TPair<uint16, double>& Pair : FakeEntityUntil)
		{
			if (FakeNow > Pair.Value)
			{
				Expired.Add(Pair.Key);
			}
		}
		for (uint16 Id : Expired)
		{
			FakeEntityUntil.Remove(Id);
			RemoveEntity(Id);
		}
	}

	const double Now = FPlatformTime::Seconds();
	if (Now - LastStatLogTime > 5.0)
	{
		LastStatLogTime = Now;
		int32 Thieves = 0, Guards = 0;
		FString Others, GuardPos;
		for (const TPair<uint16, FEntity>& Pair : Entities)
		{
			Thieves += Pair.Value.Kind == EntityKind::Thief ? 1 : 0;
			Guards += Pair.Value.Kind == EntityKind::Guard ? 1 : 0;
			if (Pair.Value.Kind == EntityKind::Guard)
			{
				GuardPos += FString::Printf(TEXT(" #%u(%.0f, %.0f)"), Pair.Key, Pair.Value.Pos.X, Pair.Value.Pos.Y);
			}
			if (Pair.Value.Kind == EntityKind::Thief && Pair.Key != SelfEntityId)
			{
				Others += FString::Printf(TEXT(" %s(%.0f, %.0f, HP %u)"), *NameForPlayer(Pair.Value.Owner), Pair.Value.Pos.X, Pair.Value.Pos.Y, Pair.Value.Hp);
			}
		}
		const APawn* StatPawn = GetLocalPawn();
		const UVHActionComponent* StatAction = StatPawn ? StatPawn->FindComponentByClass<UVHActionComponent>() : nullptr;
		UE_LOG(LogVHServerGame, Log, TEXT("[동기화] 5초: CS_MOVE %d번 보냄, SC_SNAPSHOT %d번 받음, 엔티티 %d개(플레이어 %d·경비원 %d), 내 위치 (%.0f, %.0f) 상태 0x%06x, 다른 플레이어%s, 경비원%s, 공격 표시 %d·피격 표시 %d, 내 SP %d, 내 속도 %.0f / 최대 %.0f cm/s"),
			MovesSent, SnapshotsReceived, Entities.Num(), Thieves, Guards, LastSentPos.X, LastSentPos.Y, SelfState, Others.IsEmpty() ? TEXT(" 없음") : *Others, GuardPos.IsEmpty() ? TEXT(" 없음") : *GuardPos,
			FiresSeen, DamagesSeen, MyStatus.Stamina, StatPawn ? StatPawn->GetVelocity().Size2D() : 0.f, StatAction ? StatAction->GetCurrentMaxSpeed() : 0.f);
		// 새 맵 점검용: 화면 속도(조명이 많은 레벨)와 배치 점검 결과
		UE_LOG(LogVHServerGame, Log, TEXT("[성능] 5초 평균 %.0f fps, 배치 점검 %d곳 중 어긋남 %d곳"), FramesSeen / 5.f, PlaceChecks, PlaceCheckWarnings);
		FramesSeen = 0;
		FiresSeen = 0;
		DamagesSeen = 0;
		MovesSent = 0;
		SnapshotsReceived = 0;
	}
}

void UVHServerGameSubsystem::SendMoveIfNeeded()
{
	UVHNetSubsystem* N = Net.Get();
	APawn* Pawn = GetLocalPawn();
	if (!N || !Pawn || !N->IsServerAuthoritativeGame())
	{
		return;
	}
	// 쓰러진 동안에는 서버가 이동을 받지 않는다 (리스폰하면 SC_RESPAWNED 위치로 옮긴 뒤 다시 보낸다)
	if (HasSelfState(ES_DOWNED))
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - LastMoveSendTime < MoveSendInterval)
	{
		return;
	}
	const FVector Foot = Pawn->GetActorLocation() - FVector(0.f, 0.f, GetPawnHalfHeight(Pawn));
	const float Yaw = GetLookYaw();	// 시선 = 커서 방향 (공격·스킬 방향)
	uint8 Flags = 0;
	if (Pawn->GetVelocity().Size2D() > 10.f)
	{
		Flags |= MF_MOVING;
	}
	if (const UVHActionComponent* Action = Pawn->FindComponentByClass<UVHActionComponent>())
	{
		if (Action->IsRunning())
		{
			Flags |= MF_RUNNING;
		}
	}
	const bool bChanged = FVector::DistSquared(Foot, LastSentPos) > 1.f || FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, LastSentYaw)) > 1.f
		|| Flags != LastSentFlags;
	if (!bChanged && Now - LastMoveSendTime < IdleSendInterval)
	{
		return;
	}
	if (N->SendMove(Foot, Yaw, Flags) != 0)
	{
		LastMoveSendTime = Now;
		LastSentPos = Foot;
		LastSentYaw = Yaw;
		LastSentFlags = Flags;
		++MovesSent;
	}
}

void UVHServerGameSubsystem::TickBotHeist(float DeltaTime)
{
	// 서버는 클라가 보낸 위치를 검사하지 않으므로, 봇은 대상 근처로 순간 이동한 다음
	// 사람과 똑같은 길(상호작용 컴포넌트의 F 누르기 → 대상 고르기 → 다가가기 → 서버 요청)로 상호작용한다.
	//  -VHBotHeist : 금고 → 보물 → 반납 되풀이
	//  -VHBotLoot  : 바닥 아이템 줍기(환금·무기·소비) → 상자 열기 → 필드 아이템 밟기 → 들고 있으면 반납
	// 반납은 설정(bAutoReturn)이 켜져 있으면 반납지점 위에 서기만 해도 된다(F를 눌러도 아무 일 없음).
	//  -VHBotPickTest : 상자를 열어 소비 아이템을 모으며 연막탄 줍기 세 경우(빈손·다른 것·연막탄)를 차례로 만든다 (로그 [줍기시험])
	static const bool bPickTest = FParse::Param(FCommandLine::Get(), TEXT("VHBotPickTest"));
	static const bool bLoot = bPickTest || FParse::Param(FCommandLine::Get(), TEXT("VHBotLoot"));
	static const bool bShotLoot = FParse::Param(FCommandLine::Get(), TEXT("VHShotLoot"));
	static bool bStageRead = false;
	if (bPickTest && !bStageRead)
	{
		bStageRead = true;
		FParse::Value(FCommandLine::Get(), TEXT("VHPickTestStage="), GPickTestStage);	// 중간 단계부터 시험 (예: 2 = ③부터)
	}
	APawn* Pawn = GetLocalPawn();
	UVHInteractionComponent* I = GetMyInteraction();
	UVHMatchSubsystem* Match = GetMatch();
	if (!Pawn || !I || !Match)
	{
		return;
	}
	if (BotUseConsumableAt > 0.0 && FPlatformTime::Seconds() > BotUseConsumableAt)
	{
		BotUseConsumableAt = 0.0;
		SendUseConsumable();
	}
	// 줍기 시험 ①은 빈손이어야 한다 → 들고 있으면 먼저 써서 비운다
	if (bPickTest && GPickTestStage == 0 && MyStatus.bValid && MyStatus.Consumable != 0 && BotUseConsumableAt == 0.0)
	{
		UE_LOG(LogVHServerGame, Log, TEXT("[줍기시험] 경우1 준비: 들고 있는 %s 를 써서 빈손으로"), ConsumableName(static_cast<ConsumableId>(MyStatus.Consumable)));
		BotUseConsumableAt = FPlatformTime::Seconds() + 0.5;
	}
	// 쓰러짐 시험(-VHBotDownTest): 리스폰 뒤 촬영하는 동안 가만히, 경우를 다 끝냈으면 그대로 멈춘다
	const TArray<FString>& DownCases = BotDownTestCases();
	if (DownCases.Num() > 0 && (FPlatformTime::Seconds() < BotDownIdleUntil || BotDownStage >= DownCases.Num()))
	{
		if (BotDownStage >= DownCases.Num() && !bBotDownLogged)
		{
			bBotDownLogged = true;
			UE_LOG(LogVHServerGame, Log, TEXT("[쓰러짐 시험] 모든 경우 끝 (%d개)"), DownCases.Num());
		}
		return;
	}
	if (I->IsInteracting() || I->IsApproaching())
	{
		return;
	}
	BotHeistTimer += DeltaTime;
	if (BotHeistTimer < (BotHeistStep == 2 ? 0.4f : 1.f))	// 한 단계마다 1초 (순간 이동 뒤 대상 고르기가 따라오도록)
	{
		return;
	}
	BotHeistTimer = 0.f;
	const float Half = GetPawnHalfHeight(Pawn);

	if (BotHeistStep == 1)
	{
		// 커서를 올린 상태(정보창·테두리)를 화면으로 남긴다
		BotHeistStep = 2;
		const AVHInteractableBase* Hover = I->GetHoverTarget();
		UE_LOG(LogVHServerGame, Log, TEXT("[봇 털이] 커서 대상 %s, 정보창 \"%s\", F 대상 %s"), *GetNameSafe(Hover),
			Hover ? *Hover->GetInfoCard().Text.ToString() : TEXT(""), *GetNameSafe(I->GetFocusTarget()));
		// 상자·필드 아이템은 흔하므로 2장씩만 남기고, 나머지 장수는 무기·소비·환금 아이템·금고에 쓴다
		static int32 BoxShots = 0, FieldShots = 0;
		const EVHEntityKind HoverKind = Hover ? Hover->GetKind() : EVHEntityKind::Thief;
		const bool bCommonKind = (HoverKind == EVHEntityKind::Box && ++BoxShots > 2) || (HoverKind == EVHEntityKind::FieldItem && ++FieldShots > 2);
		if (bShotLoot && Hover && !bCommonKind && BotLootShots < 14)
		{
			FString Name;
			FParse::Value(FCommandLine::Get(), TEXT("VHBot="), Name);
			const FString File = FPaths::ProjectSavedDir() / TEXT("V3Test") / FString::Printf(TEXT("loot_%s_%02d.png"), *Name, ++BotLootShots);
			FScreenshotRequest::RequestScreenshot(File, true, false);
			UE_LOG(LogVHServerGame, Log, TEXT("[봇 털이] 화면 저장 %s"), *FPaths::GetCleanFilename(File));
		}
		return;
	}
	if (BotHeistStep == 2)
	{
		BotHeistStep = 0;
		const FEntity* Goal = Entities.Find(BotGoalId);
		I->DebugSetHoverAt(nullptr);
		if (Goal && Goal->Kind == EntityKind::FieldItem)
		{
			// 필드 아이템: F가 아니라 밟아야 먹는다 (서버가 마지막 CS_MOVE 위치로 60cm 안인지 본다)
			UE_LOG(LogVHServerGame, Log, TEXT("[봇 털이] 필드 아이템 %u 위로 이동 (내 HP %d)"), Goal->Id, MyStatus.Hp);
			Pawn->TeleportTo(Goal->Pos + FVector(0.f, 0.f, Half + 2.f), Pawn->GetActorRotation());
			SendCurrentPositionNow();
			return;
		}
		UE_LOG(LogVHServerGame, Log, TEXT("[봇 털이] F 누름 (대상 %s)"), *GetNameSafe(I->GetFocusTarget()));
		if (!I->GetFocusTarget() && BotGoalId != 0 && ++BotGoalFails.FindOrAdd(BotGoalId) >= 2)
		{
			UE_LOG(LogVHServerGame, Log, TEXT("[봇 털이] 대상 %u 에 F가 닿지 않아 건너뜀"), BotGoalId);
		}
		// 줍기 시험 ③: 연막탄을 든 채 연막탄 → 클라 막기를 건너뛰고 서버로 보내 서버 동작을 본다 (③-2에서는 막기 확인)
		bForceSameConsumableOnce = bPickTest && GPickTestStage == 2 && Goal && Goal->Kind == EntityKind::ConsumablePickup;
		I->InteractPressed();
		bForceSameConsumableOnce = false;
		return;
	}

	const FVector Foot = Pawn->GetActorLocation() - FVector(0.f, 0.f, Half);
	const FVHServerMoneyRow* Mine = Match->GetMyServerMoney();
	const bool bCarrying = Mine && Mine->Carried > 0;
	// 무기·소비 아이템: 빈 칸이 있으면 줍고, 꽉 찼으면 몇 번만 바꿔 주워 본다
	const bool bWantWeapon = MyStatus.WeaponId[1] == 0 || BotSwapBudget > 0;
	const bool bWantConsumable = MyStatus.Consumable == 0 || BotSwapBudget > 0;
	// 쓰러짐 시험: 한 번 반납해서 반납 금액을 만든 뒤, 보물 + 환금 아이템 1개 이상을 든 채로 쓰러지러 간다
	int32 HeldTreasure = 0, HeldValuables = 0;
	if (const UVHTreasureCarrierComponent* Carrier = GetMyCarrier())
	{
		for (const FVHItemData& Item : Carrier->GetItems())
		{
			Item.Kind == EVHItemKind::Treasure ? ++HeldTreasure : ++HeldValuables;
		}
	}
	const int32 MyBanked = Mine ? Mine->Banked : 0;
	if (DownCases.Num() > 0 && HeldTreasure > 0 && MyBanked > 0 && HeldValuables > 0)
	{
		TickBotDownTest(Pawn);
		return;
	}
	auto Wants = [&](const FEntity& E, int32 Pass) -> bool
	{
		if (!E.Object.IsValid() || BotGoalFails.FindRef(E.Id) >= 2)
		{
			return false;
		}
		if (DownCases.Num() > 0)
		{
			const bool bTreasureItem = E.Kind == EntityKind::Valuable && static_cast<ValuableQuality>(E.TypeId) == ValuableQuality::Unique;
			if (HeldTreasure > 0)
			{
				// 보물을 들었으면: 처음 한 번은 반납, 그다음은 환금 아이템을 하나 더 줍는다 (바닥에 없으면 상자를 연다)
				if (MyBanked == 0)
				{
					return Pass == 0 && E.Kind == EntityKind::ReturnPoint;
				}
				return Pass == 0 ? (E.Kind == EntityKind::Valuable && !bTreasureItem) : E.Kind == EntityKind::Box;
			}
			// 바닥의 보물(떨어진 것 포함) → 환금 아이템 2개까지 → 닫힌 금고
			return Pass == 0 ? (bTreasureItem || (E.Kind == EntityKind::Valuable && HeldValuables < 2))
				: (E.Kind == EntityKind::Safe && (E.State & ES_OPENED) == 0);
		}
		if (!bLoot)
		{
			// 들고 있으면 반납지점, 아니면 떨어져 있는 환금 아이템(보물), 그것도 없으면 닫힌 금고
			return Pass == 0 ? E.Kind == (bCarrying ? EntityKind::ReturnPoint : EntityKind::Valuable)
				: (E.Kind == EntityKind::Safe && (E.State & ES_OPENED) == 0);
		}
		if (bPickTest)
		{
			// 줍기 시험: 반납·환금·무기는 건너뛰고 상자 → 소비 아이템만 (쓰러지면 소비 아이템을 잃으니 HP가 낮으면 회복약)
			switch (Pass)
			{
			case 0: return E.Kind == EntityKind::FieldItem && MyStatus.bValid && MyStatus.Hp < 60;
			case 1: return E.Kind == EntityKind::ConsumablePickup && BotPickTestWants(E);
			default: return E.Kind == EntityKind::Box;
			}
		}
		switch (Pass)
		{
		case 0: return bCarrying ? E.Kind == EntityKind::ReturnPoint : (E.Kind == EntityKind::FieldItem && MyStatus.bValid && MyStatus.Hp < 100);
		case 1: return E.Kind == EntityKind::Valuable || (E.Kind == EntityKind::WeaponPickup && bWantWeapon) || (E.Kind == EntityKind::ConsumablePickup && bWantConsumable);
		default: return E.Kind == EntityKind::Box;
		}
	};
	const FEntity* Goal = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (int32 Pass = 0; Pass < (bLoot ? 3 : 2) && !Goal; ++Pass)
	{
		for (const TPair<uint16, FEntity>& Pair : Entities)
		{
			const float Dist = FVector::Dist2D(Foot, Pair.Value.Pos);
			if (Wants(Pair.Value, Pass) && Dist < BestDist)
			{
				BestDist = Dist;
				Goal = &Pair.Value;
			}
		}
	}
	if (!Goal)
	{
		return;
	}
	AVHInteractableBase* Object = Goal->Object.Get();
	FVector Dest = Goal->Pos;
	if (Goal->Kind == EntityKind::Safe)
	{
		// 금고 앞 작업 지점에서 1.5m 더 떨어진 곳에 선다 → F를 누르면 걸어서 다가가는 것까지 시험된다
		Dest = Object->GetWorkPoint() + Object->GetActorForwardVector() * 150.f;
		Dest.Z = Goal->Pos.Z;
	}
	else if (Goal->Kind != EntityKind::ReturnPoint)
	{
		// 상자는 2.2m, 아이템은 1.3m 떨어진 곳(벽에 막히지 않는 쪽)에 선다 → 3m 테두리·다가가기까지 시험된다
		const float Offset = Goal->Kind == EntityKind::Box ? 220.f : 130.f;
		const FVector Dirs[] = { FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0) };
		FCollisionQueryParams Query(FName(TEXT("VHBotLoot")), false, Object);
		Query.AddIgnoredActor(Pawn);
		for (const FVector& Dir : Dirs)
		{
			const FVector From = Goal->Pos + FVector(0.f, 0.f, 60.f);
			FHitResult Hit;
			if (!GetWorld()->LineTraceSingleByChannel(Hit, From, From + Dir * (Offset + 50.f), ECC_Visibility, Query))
			{
				Dest = Goal->Pos + Dir * Offset;
				break;
			}
		}
	}
	if (Goal->Kind == EntityKind::WeaponPickup && MyStatus.WeaponId[1] != 0)
	{
		--BotSwapBudget;
	}
	if (Goal->Kind == EntityKind::ConsumablePickup && MyStatus.Consumable != 0)
	{
		--BotSwapBudget;
	}
	Pawn->TeleportTo(Dest + FVector(0.f, 0.f, Half + 2.f), Pawn->GetActorRotation());
	SendCurrentPositionNow();
	BotGoalId = Goal->Id;
	BotHeistStep = 1;
	I->DebugSetHoverAt(Object);	// 실제 마우스 대신 이 물품의 화면 위치를 커서로 (정보창·우선순위 시험)
	UE_LOG(LogVHServerGame, Log, TEXT("[봇 털이] %s %u 쪽으로 이동 (%.0f, %.0f), 들고 있는 가치 %d$ 반납 %d$"), KindName(Goal->Kind), Goal->Id, Dest.X, Dest.Y,
		Mine ? Mine->Carried : 0, Mine ? Mine->Banked : 0);
}

bool UVHServerGameSubsystem::TickBotDownTest(APawn* Pawn)
{
	// 보물 + 환금 아이템을 든 채 이번 경우의 상대(경비원 / 다른 플레이어) 옆 90cm에 서서 맞는다 (반격하지 않음)
	const TArray<FString>& Cases = BotDownTestCases();
	if (!Pawn || BotDownStage >= Cases.Num())
	{
		return false;
	}
	const bool bGuard = Cases[BotDownStage].Equals(TEXT("Guard"), ESearchCase::IgnoreCase);
	const float Half = GetPawnHalfHeight(Pawn);
	const FVector Foot = Pawn->GetActorLocation() - FVector(0.f, 0.f, Half);
	const FEntity* Near = nullptr;
	float Best = TNumericLimits<float>::Max();
	for (const TPair<uint16, FEntity>& Pair : Entities)
	{
		const FEntity& E = Pair.Value;
		const bool bWant = bGuard ? (E.Kind == EntityKind::Guard && !(E.State & (ES_REPAIRING | ES_STUNNED)))
			: (E.Kind == EntityKind::Thief && E.Id != SelfEntityId && !(E.State & (ES_DOWNED | ES_LEFT_GAME)));
		const float Dist = FVector::Dist2D(Foot, E.Pos);
		if (bWant && Dist < Best)
		{
			Best = Dist;
			Near = &E;
		}
	}
	if (!Near)
	{
		return false;
	}
	if (!bBotDownLogged)
	{
		bBotDownLogged = true;
		const FVHServerMoneyRow* Mine = GetMatch() ? GetMatch()->GetMyServerMoney() : nullptr;
		UE_LOG(LogVHServerGame, Log, TEXT("[쓰러짐 시험] 경우 %d/%d: %s에게 쓰러지러 감 (들고 있는 가치 %d$, 반납 %d$)"), BotDownStage + 1, Cases.Num(),
			bGuard ? TEXT("경비원") : TEXT("플레이어"), Mine ? Mine->Carried : 0, Mine ? Mine->Banked : 0);
	}
	if (Best > 140.f)
	{
		const FVector Dirs[] = { FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0) };
		for (const FVector& Dir : Dirs)
		{
			if (Pawn->TeleportTo(Near->Pos + Dir * 90.f + FVector(0.f, 0.f, Half + 2.f), Pawn->GetActorRotation()))
			{
				break;
			}
		}
		SendCurrentPositionNow();
	}
	return true;
}

bool UVHServerGameSubsystem::BotPickTestWants(const FEntity& E) const
{
	if (!MyStatus.bValid)
	{
		return false;
	}
	const uint8 Held = MyStatus.Consumable;
	const bool bSmoke = E.TypeId == SmokeId;
	switch (GPickTestStage)
	{
	case 0: return Held == 0 && bSmoke;											// ① 빈손 → 연막탄
	case 1: return (Held == 0 || Held == SmokeId) ? !bSmoke : bSmoke;			// ② 다른 것을 먼저 들고 → 연막탄
	case 2: return bSmoke;														// ③ 연막탄을 들고(없으면 먼저 줍고) → 연막탄 (서버로 보냄)
	case 3: return Held == SmokeId && bSmoke;									// ③-2 같은 경우 클라 막기
	default: return false;
	}
}

void UVHServerGameSubsystem::TickBotHunt(float DeltaTime)
{
	// 서버는 클라가 보낸 위치를 검사하지 않으므로, 봇은 대상 옆 1.2m로 순간 이동해 그쪽을 보고 공격 키를 누른다.
	// 숨겨진 대상(은신·연막)과 맞지 않는 대상(쓰러짐·리스폰 무적·접속 끊김)은 고르지 않는다.
	APawn* Pawn = GetLocalPawn();
	if (!Pawn)
	{
		return;
	}
	BotHuntTimer += DeltaTime;
	if (BotHuntTimer < 0.5f)
	{
		return;
	}
	BotHuntTimer = 0.f;

	const float Half = GetPawnHalfHeight(Pawn);
	const FVector Foot = Pawn->GetActorLocation() - FVector(0.f, 0.f, Half);
	const uint32 Skip = ES_DOWNED | ES_LEFT_GAME | ES_INVULNERABLE | ES_CLOAKED | ES_IN_SMOKE;
	// -VHBotHuntNear=cm: 그 거리 안에 들어온 플레이어만 공격한다(제자리에서 기다림). 쓰러짐 시험에서 상대가 다가올 때까지 기다리는 용도
	static float HuntNear = -1.f;
	if (HuntNear < 0.f)
	{
		HuntNear = 0.f;
		FParse::Value(FCommandLine::Get(), TEXT("VHBotHuntNear="), HuntNear);
	}
	const FEntity* Target = nullptr;
	float BestDist = HuntNear > 0.f ? HuntNear : TNumericLimits<float>::Max();
	for (const TPair<uint16, FEntity>& Pair : Entities)
	{
		const FEntity& E = Pair.Value;
		if (E.Kind != EntityKind::Thief || E.Id == SelfEntityId || (E.State & Skip))
		{
			continue;
		}
		const float Dist = FVector::Dist2D(Foot, E.Pos);
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Target = &E;
		}
	}
	if (!Target)
	{
		if (bAttackHeldSent)
		{
			SendAttackHeld(false);
		}
		BotHuntTarget = 0;
		// -VHBotHuntLoot=초: 내가 쓰러뜨린 뒤 그 시간이 지나면 근처(20m)에 떨어진 보물을 주우러 간다 (떨어뜨린 사람 화면의 표시가 사라지는지 시험)
		static float LootDelay = -1.f;
		if (LootDelay < 0.f)
		{
			LootDelay = 0.f;
			FParse::Value(FCommandLine::Get(), TEXT("VHBotHuntLoot="), LootDelay);
		}
		UVHInteractionComponent* I = GetMyInteraction();
		if (LootDelay > 0.f && I && BotHuntKillSeconds > 0.0 && FPlatformTime::Seconds() - BotHuntKillSeconds > LootDelay)
		{
			const FEntity* Loot = nullptr;
			for (const TPair<uint16, FEntity>& Pair : Entities)
			{
				if (Pair.Value.Kind == EntityKind::Valuable && static_cast<ValuableQuality>(Pair.Value.TypeId) == ValuableQuality::Unique
					&& Pair.Value.Object.IsValid() && FVector::Dist2D(Foot, Pair.Value.Pos) < 2000.f)
				{
					Loot = &Pair.Value;
				}
			}
			if (!Loot || BotHuntLootStep >= 6)
			{
				return;
			}
			if (BotHuntLootStep % 2 == 0)
			{
				Pawn->TeleportTo(Loot->Pos + FVector(100.f, 0.f, Half + 2.f), Pawn->GetActorRotation());
				SendCurrentPositionNow();
				I->DebugSetHoverAt(Loot->Object.Get());
				UE_LOG(LogVHServerGame, Log, TEXT("[봇 사냥] 떨어진 보물 %u (%u$) 주우러 감"), Loot->Id, Loot->Param);
			}
			else
			{
				I->InteractPressed();
				I->DebugSetHoverAt(nullptr);
			}
			++BotHuntLootStep;
		}
		return;
	}
	if (Target->Id != BotHuntTarget)
	{
		BotHuntTarget = Target->Id;
		const UVHMatchSubsystem* Match = GetMatch();
		int32 TargetBanked = 0;
		if (Match)
		{
			for (const FVHServerMoneyRow& Row : Match->GetServerMoney())
			{
				if (Row.EntityId == Target->Id)
				{
					TargetBanked = Row.Banked;
				}
			}
		}
		UE_LOG(LogVHServerGame, Log, TEXT("[봇 사냥] 대상 %s (%.0f, %.0f) HP %u, 반납 금액 %d$, 거리 %.0fcm"), *NameForPlayer(Target->Owner),
			Target->Pos.X, Target->Pos.Y, Target->Hp, TargetBanked, BestDist);
	}

	// 대상에게서 1.2m 떨어진 자리(되도록 지금 내가 있는 쪽). 막힌 자리면 90°씩 돌려 본다
	FVector Dir = (Foot - Target->Pos).GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector::ForwardVector;
	}
	FVector Dest = Foot;
	if (FVector::Dist2D(Foot, Target->Pos) > 160.f || FVector::Dist2D(Foot, Target->Pos) < 60.f)
	{
		for (int32 Try = 0; Try < 4; ++Try)
		{
			const FVector Candidate = Target->Pos + Dir.RotateAngleAxis(90.f * Try, FVector::UpVector) * 120.f;
			if (Pawn->TeleportTo(Candidate + FVector(0.f, 0.f, Half + 2.f), Pawn->GetActorRotation()))
			{
				Dest = Candidate;
				break;
			}
		}
	}
	const float Yaw = (Target->Pos - Dest).Rotation().Yaw;
	if (UVHAimComponent* Aim = Pawn->FindComponentByClass<UVHAimComponent>())
	{
		Aim->SnapLookYaw(Yaw);
	}
	SendCurrentPositionNow();	// 서버는 마지막 CS_MOVE 의 위치·시선으로 공격을 판정한다
	// 주운 무기(무기2)가 있으면 그것을 든다 → 총·레일건·수류탄·지뢰 시험. 다 쓰면 서버가 삼단봉으로 돌려놓는다
	if (MyStatus.bValid && MyStatus.WeaponId[1] != 0 && MyStatus.ActiveSlot == 0)
	{
		UE_LOG(LogVHServerGame, Log, TEXT("[봇 사냥] 무기2(%s)로 바꿈"), WeaponName(static_cast<WeaponId>(MyStatus.WeaponId[1])));
		SendSwitchWeapon();
	}
	if (!bAttackHeldSent)
	{
		SendAttackHeld(true);
	}
}

void UVHServerGameSubsystem::TickBotTaserTest(float DeltaTime)
{
	// 실서버에서 사제 테이저건(2026-11-22 팀 서버 판정)을 사람 손 없이 시험한다. 로그 [테이저 시험]
	//  서 있는 다른 플레이어(-VHBotStill) 2m 앞 → 1초 Q 들기 → 2초 Q 넣기(쿨타임 0이어야) → 3초 Q 들기 → 4초 공격 키(0.1초) → 5초 결과(쿨타임 ~29초, 대상 경직)
	APawn* Pawn = GetLocalPawn();
	if (!Pawn || !MyStatus.bValid)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const float T = static_cast<float>(Now - TaserTestT0);
	const FEntity* Target = TaserTestTarget ? Entities.Find(TaserTestTarget) : nullptr;
	auto Face = [&]()
	{
		if (!Target)
		{
			return;
		}
		const float Half = GetPawnHalfHeight(Pawn);
		const FVector Foot = Pawn->GetActorLocation() - FVector(0.f, 0.f, Half);
		const float Yaw = (Target->Pos - Foot).Rotation().Yaw;
		if (UVHAimComponent* Aim = Pawn->FindComponentByClass<UVHAimComponent>())
		{
			Aim->SnapLookYaw(Yaw);
		}
		Pawn->SetActorRotation(FRotator(0.f, Yaw, 0.f));
		SendCurrentPositionNow();
	};
	auto Report = [&](const TCHAR* What)
	{
		UE_LOG(LogVHServerGame, Log, TEXT("[테이저 시험] %s: 든 상태 %d (내 비트 TaserReady %d), 쿨타임 %.1f초, 대상 %s HP %d 경직 %d"), What,
			bSkillTaserHeld ? 1 : 0, HasSelfState(ES_TASER_READY) ? 1 : 0, GetSkillCooldownRemaining(),
			*NameForPlayer(Target ? Target->Owner : 0), Target ? (int32)Target->Hp : -1, (Target && (Target->State & ES_STUNNED)) ? 1 : 0);
	};
	switch (TaserTestStage)
	{
	case 0:
	{
		if (MyStatus.Skill != static_cast<uint8>(SkillId::Taser))
		{
			return;	// 도적1이 아님 (-VHBotChar=1)
		}
		const FEntity* Best = nullptr;
		for (const TPair<uint16, FEntity>& Pair : Entities)
		{
			if (Pair.Value.Kind == EntityKind::Thief && Pair.Value.Id != SelfEntityId && !(Pair.Value.State & (ES_DOWNED | ES_LEFT_GAME)))
			{
				Best = &Pair.Value;
			}
		}
		if (!Best || HasSelfState(ES_DOWNED))
		{
			return;
		}
		TaserTestTarget = Best->Id;
		Target = Best;
		const float Half = GetPawnHalfHeight(Pawn);
		for (int32 Try = 0; Try < 8; ++Try)
		{
			const FVector Cand = Best->Pos + FRotator(0.f, 45.f * Try, 0.f).Vector() * 200.f;
			if (Pawn->TeleportTo(Cand + FVector(0.f, 0.f, Half + 2.f), Pawn->GetActorRotation()))
			{
				break;
			}
		}
		Face();
		TaserTestT0 = Now;
		TaserTestStage = 1;
		Report(TEXT("0) 대상 2m 앞"));
		return;
	}
	case 1: if (T >= 1.f) { Report(TEXT("1) Q 누르기 전")); PressSkillKey(); TaserTestStage = 2; } return;
	case 2: if (T >= 2.f) { Report(TEXT("2) Q 들기 1초 뒤 (든 상태 1이어야) → Q 다시")); PressSkillKey(); TaserTestStage = 3; } return;
	case 3: if (T >= 3.f) { Report(TEXT("3) Q 넣기 1초 뒤 (든 상태 0, 쿨타임 0이어야) → Q 들기")); PressSkillKey(); TaserTestStage = 4; } return;
	case 4:
		if (T >= 4.f)
		{
			Face();
			Report(TEXT("4) 들고 1초 (든 상태 1) → 공격 키"));
			SendAttackHeld(true);
			TaserTestStage = 5;
		}
		return;
	case 5: if (T >= 4.1f) { SendAttackHeld(false); TaserTestStage = 6; } return;
	case 6: if (T >= 4.6f) { Report(TEXT("5) 발사 0.5초 뒤 (든 상태 0, 쿨타임 약 29.5초, 대상 경직 1이어야)")); TaserTestStage = 7; } return;
	case 7: if (T >= 5.f) { Report(TEXT("6) 쿨타임 중 Q (서버에 보내지 않고 알림만)")); PressSkillKey(); TaserTestStage = 8; } return;
	case 8: if (T >= 7.f) { Report(TEXT("7) 끝 (대상 경직 2초가 풀렸어야)")); TaserTestStage = 9; } return;
	default:
		return;
	}
}

void UVHServerGameSubsystem::TickBotRailTest(float DeltaTime)
{
	// 실서버에서 레일건 충전 발사를 사람 손 없이 시험한다 (서버는 클라 위치를 검사하지 않으므로 순간 이동을 쓴다).
	//  0) 레일건 얻기: 바닥의 레일건 → 없으면 가까운 상자를 연다 (상자 하나에 약 3.9%: 무기 35% × 9종 중 1)
	//  1) 서 있는 다른 플레이어(-VHBotStill) 5m 앞에서 대상 쪽으로 클릭 → 0.1초 손 뗌·0.4초 다시 클릭(무시돼야 함) → 0.6초에 35° 돌림(회전 잠금·서버는 누른 순간 방향) → 1초에 빔 → 피해 확인 (2026-11-22 팀 서버)
	//  2) 클릭 → 0.5초에 무기 교체 → 서버가 잠금(Locked)으로 거절, 발사는 그대로 (탄이 줄어야 함)
	//  3) 경비원(기관총 10 × 0.1초) 앞에서 HP 60 이하가 되면 클릭 → 충전 중 쓰러짐 → 취소
	APawn* Pawn = GetLocalPawn();
	UVHInteractionComponent* I = GetMyInteraction();
	if (!Pawn || !I || !MyStatus.bValid)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const float Half = GetPawnHalfHeight(Pawn);
	const FVector Foot = Pawn->GetActorLocation() - FVector(0.f, 0.f, Half);
	const uint8 Rail = static_cast<uint8>(WeaponId::Railgun);
	const int32 RailSlot = MyStatus.WeaponId[0] == Rail ? 0 : (MyStatus.WeaponId[1] == Rail ? 1 : -1);
	static const bool bShots = FParse::Param(FCommandLine::Get(), TEXT("VHRailShots"));	// 화면 저장은 프레임을 잠깐 멈추므로 시간 측정 때는 끈다
	auto Shot = [](const TCHAR* Tag)
	{
		if (!bShots)
		{
			return;
		}
		const FString File = FPaths::ProjectSavedDir() / TEXT("V3Test") / FString::Printf(TEXT("rail_%s.png"), Tag);
		FScreenshotRequest::RequestScreenshot(File, false, false);
		UE_LOG(LogVHServerGame, Log, TEXT("[레일건 시험] 화면 %s"), *FPaths::GetCleanFilename(File));
	};
	auto FaceYaw = [&](float Yaw)
	{
		if (UVHAimComponent* Aim = Pawn->FindComponentByClass<UVHAimComponent>())
		{
			Aim->SnapLookYaw(Yaw);
		}
		Pawn->SetActorRotation(FRotator(0.f, Yaw, 0.f));
		SendCurrentPositionNow();
	};
	auto TeleportNear = [&](const FVector& Pos, float Dist) -> FVector
	{
		for (int32 Try = 0; Try < 8; ++Try)
		{
			const FVector Cand = Pos + FRotator(0.f, 45.f * Try, 0.f).Vector() * Dist;
			if (Pawn->TeleportTo(Cand + FVector(0.f, 0.f, Half + 2.f), Pawn->GetActorRotation()))
			{
				SendCurrentPositionNow();
				return Cand;
			}
		}
		return Foot;
	};
	auto Nearest = [&](TFunctionRef<bool(const FEntity&)> Want) -> const FEntity*
	{
		const FEntity* Best = nullptr;
		float BestDist = TNumericLimits<float>::Max();
		for (const TPair<uint16, FEntity>& Pair : Entities)
		{
			const float D = FVector::Dist2D(Foot, Pair.Value.Pos);
			if (Want(Pair.Value) && D < BestDist)
			{
				BestDist = D;
				Best = &Pair.Value;
			}
		}
		return Best;
	};
	auto OtherThief = [&](const FEntity& E) { return E.Kind == EntityKind::Thief && E.Id != SelfEntityId && !(E.State & (ES_DOWNED | ES_LEFT_GAME)); };
	const float T = static_cast<float>(Now - RailTestT0);

	switch (RailTestStage)
	{
	case 0:
	{
		if (I->IsInteracting() || I->IsApproaching() || Now < RailTestNext)
		{
			return;
		}
		if (RailSlot >= 0)
		{
			if (MyStatus.ActiveSlot != RailSlot)
			{
				SendSwitchWeapon();
				RailTestNext = Now + 1.0;
				return;
			}
			UE_LOG(LogVHServerGame, Log, TEXT("[레일건 시험] 레일건 획득 (탄 %d, 상자 %d개 열기 시도) → 1) 발사 시험"), MyStatus.WeaponAmmo[RailSlot], RailTestBoxes);
			RailTestStage = 1;
			RailTestSub = 0;
			RailTestNext = Now + 0.5;
			return;
		}
		if (const FEntity* Pick = Nearest([&](const FEntity& E) { return E.Kind == EntityKind::WeaponPickup && E.TypeId == Rail && E.Object.IsValid() && RailTestTries.FindRef(E.Id) < 3; }))
		{
			TeleportNear(Pick->Pos, 110.f);
			RailTestTries.FindOrAdd(Pick->Id)++;
			I->DebugSetHoverAt(Pick->Object.Get());
			I->InteractPressed();
			UE_LOG(LogVHServerGame, Log, TEXT("[레일건 시험] 바닥의 레일건 %u 줍기"), Pick->Id);
			RailTestNext = Now + 1.5;
			return;
		}
		if (const FEntity* Box = Nearest([&](const FEntity& E) { return E.Kind == EntityKind::Box && E.Object.IsValid() && RailTestTries.FindRef(E.Id) < 2; }))
		{
			TeleportNear(Box->Pos, 150.f);
			RailTestTries.FindOrAdd(Box->Id)++;
			++RailTestBoxes;
			I->DebugSetHoverAt(Box->Object.Get());
			I->InteractPressed();
			if (RailTestBoxes % 5 == 1)
			{
				UE_LOG(LogVHServerGame, Log, TEXT("[레일건 시험] 상자 %u 열기 (%d번째)"), Box->Id, RailTestBoxes);
			}
			RailTestNext = Now + 1.0;
		}
		return;
	}
	case 1:
	{
		const FEntity* Target = RailTestTarget ? Entities.Find(RailTestTarget) : nullptr;
		if (RailTestSub == 0)
		{
			if (Now < RailTestNext || IsRailgunCharging())
			{
				return;
			}
			Target = Nearest(OtherThief);
			if (!Target)
			{
				return;
			}
			RailTestTarget = Target->Id;
			RailTestTargetHp = Target->Hp;
			const FVector Me = TeleportNear(Target->Pos, 500.f);
			FaceYaw((Target->Pos - Me).Rotation().Yaw);	// 대상 쪽으로 겨눈다 (충전 중에 돌려도 서버는 누른 순간 방향)
			RailTestNext = Now + 1.0;
			RailTestSub = 1;
			return;
		}
		if (RailTestSub == 1 && Now >= RailTestNext)
		{
			UE_LOG(LogVHServerGame, Log, TEXT("[레일건 시험] 1) 클릭 (대상 %s 거리 %.0fcm HP %d, 대상 쪽 조준)"), *NameForPlayer(Target ? Target->Owner : 0),
				Target ? FVector::Dist2D(Foot, Target->Pos) : 0.f, RailTestTargetHp);
			SendAttackHeld(true);
			RailTestT0 = Now;
			RailTestSub = 2;
			Shot(TEXT("1_click"));
			return;
		}
		if (RailTestSub == 2 && T >= 0.1f) { SendAttackHeld(false); RailTestSub = 3; return; }		// 손 떼기 → 서버 SC_FIRE 를 기다리거나 이미 충전 중
		if (RailTestSub == 3 && T >= 0.4f) { SendAttackHeld(true); Shot(TEXT("2_charge04")); RailTestSub = 4; return; }	// 다시 클릭 → 무시
		if (RailTestSub == 4 && T >= 0.6f)
		{
			if (Target)
			{
				FaceYaw((Target->Pos - Foot).Rotation().Yaw + 35.f);
				UE_LOG(LogVHServerGame, Log, TEXT("[레일건 시험] 0.6초: 조준을 35° 돌림 → 회전 잠금이라 그대로여야 함 (지금 yaw %.0f)"), GetLookYaw());
			}
			RailTestSub = 5;
			return;
		}
		if (RailTestSub == 5 && T >= 0.9f) { Shot(TEXT("3_charge09")); RailTestSub = 6; return; }
		if (RailTestSub == 6 && T >= 1.12f) { Shot(TEXT("4_fire")); RailTestSub = 7; return; }
		if (RailTestSub == 7 && T >= 3.f)
		{
			UE_LOG(LogVHServerGame, Log, TEXT("[레일건 시험] 1) 결과: 대상 HP %d → %d"), RailTestTargetHp, Target ? (int32)Target->Hp : -1);
			RailTestStage = 3;	// (2026-11-22: 탄 2발이라 쓰러짐 시험을 먼저, 무기 교체 시험은 남은 탄이 있을 때만)
			RailTestSub = 0;
		}
		return;
	}
	case 2:
	{
		if (RailTestSub == 0)
		{
			if (HasSelfState(ES_RAILGUN_LOCK) || IsRailgunCharging())
			{
				return;
			}
			if (RailSlot < 0)
			{
				UE_LOG(LogVHServerGame, Log, TEXT("[레일건 시험] 2) 레일건이 없음(탄 다 씀) → 건너뜀"));
				RailTestStage = 4;
				return;
			}
			UE_LOG(LogVHServerGame, Log, TEXT("[레일건 시험] 2) 클릭 → 0.5초에 무기 교체 시도(서버 잠금 거절 예상) (탄 %d)"), MyStatus.WeaponAmmo[RailSlot]);
			SendAttackHeld(true);
			RailTestT0 = Now;
			RailTestSub = 1;
			return;
		}
		if (RailTestSub == 1 && T >= 0.1f) { SendAttackHeld(false); RailTestSub = 2; return; }
		if (RailTestSub == 2 && T >= 0.5f) { SendSwitchWeapon(); Shot(TEXT("5_switch_cancel")); RailTestSub = 3; return; }
		if (RailTestSub == 3 && T >= 2.f)
		{
			if (RailSlot >= 0 && MyStatus.ActiveSlot != RailSlot)
			{
				SendSwitchWeapon();
				UE_LOG(LogVHServerGame, Log, TEXT("[레일건 시험] 2) 레일건으로 다시 바꿈 (탄 %d — 1 줄었으면 교체 시도와 상관없이 발사된 것)"), MyStatus.WeaponAmmo[RailSlot]);
			}
			RailTestSub = 4;
			return;
		}
		if (RailTestSub == 4 && T >= 3.5f)
		{
			RailTestStage = 4;
			RailTestSub = 0;
		}
		return;
	}
	case 3:
	{
		if (RailTestSub == 0)
		{
			if (Now < RailTestNext)
			{
				return;
			}
			if (RailSlot < 0 || MyStatus.ActiveSlot != RailSlot)
			{
				if (RailSlot >= 0) { SendSwitchWeapon(); }
				RailTestStage = RailSlot < 0 ? 2 : 3;
				RailTestNext = Now + 1.0;
				return;
			}
			const FEntity* Guard = Nearest([](const FEntity& E) { return E.Kind == EntityKind::Guard && !(E.State & (ES_REPAIRING | ES_STUNNED)); });
			if (!Guard)
			{
				return;
			}
			const FVector Me = TeleportNear(Guard->Pos, 250.f);
			FaceYaw((Me - Guard->Pos).Rotation().Yaw);	// 경비원을 등지고 (빈 곳으로 쏜다)
			UE_LOG(LogVHServerGame, Log, TEXT("[레일건 시험] 3) 경비원 %u 앞 (HP %d) — HP 60 이하가 되면 클릭"), Guard->Id, MyStatus.Hp);
			RailTestT0 = Now;
			RailTestSub = 1;
			return;
		}
		if (RailTestSub == 1)
		{
			if (HasSelfState(ES_DOWNED))
			{
				UE_LOG(LogVHServerGame, Log, TEXT("[레일건 시험] 3) 클릭 전에 쓰러짐 → 다시 (리스폰 뒤 레일건이 없으면 끝)"));
				RailTestStage = 2;
				RailTestSub = 0;
				return;
			}
			if (MyStatus.Hp <= 60 || T > 10.f)
			{
				UE_LOG(LogVHServerGame, Log, TEXT("[레일건 시험] 3) HP %d 에서 클릭"), MyStatus.Hp);
				SendAttackHeld(true);
				RailTestT0 = Now;
				RailTestSub = 2;
			}
			else if (T > 3.f && MyStatus.Hp >= 100)
			{
				RailTestSub = 0;	// 경비원이 쏘지 않으면 다시 붙는다
			}
			return;
		}
		if (RailTestSub == 2 && T >= 0.1f)
		{
			SendAttackHeld(false);
			RailTestSub = 3;
			return;
		}
		if (RailTestSub == 3 && (!IsRailgunCharging() || T > 3.f))
		{
			UE_LOG(LogVHServerGame, Log, TEXT("[레일건 시험] 3) 충전 끝: 취소 이유 \"%s\" (내 빔 수 %d. 봇은 쓰러져 있는 동안 멈추므로 이 줄은 리스폰 뒤에 찍힐 수 있다)"), *RailLastCancel, RailBeamsSeen);
			RailTestStage = 2;
			RailTestSub = 0;
		}
		return;
	}
	case 4:
		UE_LOG(LogVHServerGame, Log, TEXT("[레일건 시험] 끝"));
		RailTestStage = 5;
		return;
	default:
		return;
	}
}

void UVHServerGameSubsystem::TickBotWalk(float DeltaTime)
{
	// 개발용 봇(vh.Bot.Start, -VHBot)이면 2초마다 방향을 바꿔 걷는다 → 다른 클라에서 움직임 확인용
	const UVHNetSubsystem* N = Net.Get();
	APawn* Pawn = GetLocalPawn();
	if (!N || !N->IsBot() || !Pawn || HasSelfState(ES_DOWNED))
	{
		return;
	}
	// 실행 옵션 -VHShot=초: 그 간격으로 화면을 Saved/V3Test/shot_<봇 이름>_<번호>.png 로 저장 (화면 확인용)
	static float ShotInterval = -1.f;
	static FString ShotName;
	if (ShotInterval < 0.f)
	{
		ShotInterval = 0.f;
		FParse::Value(FCommandLine::Get(), TEXT("VHShot="), ShotInterval);
		FParse::Value(FCommandLine::Get(), TEXT("VHBot="), ShotName);
	}
	if (ShotInterval > 0.f)
	{
		static float ShotTime = 0.f;
		static int32 ShotIndex = 0;
		ShotTime += DeltaTime;
		if (ShotTime >= ShotInterval)
		{
			ShotTime = 0.f;
			const FString File = FPaths::ProjectSavedDir() / TEXT("V3Test") / FString::Printf(TEXT("shot_%s_%02d.png"), *ShotName, ++ShotIndex);
			FScreenshotRequest::RequestScreenshot(File, true, false);
			UE_LOG(LogVHServerGame, Log, TEXT("[봇] 화면 저장 %s (내 위치 %.0f, %.0f)"), *FPaths::GetCleanFilename(File), Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y);
		}
	}

	// 실행 옵션 -VHBotSkill=초: 그 간격으로 Q(스킬)를 누른다 (은신은 한 번 더 누르면 해제)
	static float SkillEvery = -1.f;
	if (SkillEvery < 0.f)
	{
		SkillEvery = 0.f;
		FParse::Value(FCommandLine::Get(), TEXT("VHBotSkill="), SkillEvery);
	}
	if (SkillEvery > 0.f)
	{
		BotSkillTimer += DeltaTime;
		if (BotSkillTimer >= SkillEvery)
		{
			BotSkillTimer = 0.f;
			UE_LOG(LogVHServerGame, Log, TEXT("[봇] 스킬(Q) 누름, 내 상태 0x%06x"), SelfState);
			SendUseSkill();
		}
	}

	// 실행 옵션 -VHBotFake=초: 그 간격으로 밀려남 → 제트팩(3초) → 레일건 잠금(1초)을 차례로 흉내 낸다 (클라가 적용하는 이동 시험용)
	static float FakeEvery = -1.f;
	if (FakeEvery < 0.f)
	{
		FakeEvery = 0.f;
		FParse::Value(FCommandLine::Get(), TEXT("VHBotFake="), FakeEvery);
	}
	if (FakeEvery > 0.f)
	{
		static float FakeTimer = 0.f;
		static int32 FakeIndex = 0;
		static FVector FakeFrom = FVector::ZeroVector;
		// -VHBotFakeFx 를 같이 주면 연출(수류탄·섬광탄·지뢰·연막·빔·섬광 화면)을 차례로 흉내 낸다
		static const bool bFakeFx = FParse::Param(FCommandLine::Get(), TEXT("VHBotFakeFx"));
		static const TCHAR* MoveNames[] = { TEXT("knockback"), TEXT("jetpack"), TEXT("railgun") };
		static const float MoveSeconds[] = { 0.f, 3.f, 1.f };
		static const TCHAR* FxNames[] = { TEXT("grenade"), TEXT("blastmine"), TEXT("shockmine"), TEXT("flashbang"), TEXT("smokefx"), TEXT("smoke"), TEXT("railbeam"), TEXT("flame"), TEXT("baton"), TEXT("flash") };
		static const float FxSeconds[] = { 0.f, 0.f, 0.f, 0.f, 6.f, 4.f, 0.f, 0.f, 0.f, 3.f };
		const TCHAR* const* FakeNames = bFakeFx ? FxNames : MoveNames;
		const float* FakeSeconds = bFakeFx ? FxSeconds : MoveSeconds;
		const int32 FakeCount = bFakeFx ? UE_ARRAY_COUNT(FxNames) : UE_ARRAY_COUNT(MoveNames);
		FakeTimer += DeltaTime;
		if (FakeTimer >= FakeEvery)
		{
			FakeTimer = 0.f;
			UE_LOG(LogVHServerGame, Log, TEXT("[봇 흉내] 앞 단계부터 %.0fcm 이동. 다음: %s (내 위치 %.0f, %.0f, 몸 방향 %.0f°)"), FVector::Dist2D(FakeFrom, Pawn->GetActorLocation()),
				FakeNames[FakeIndex], Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y, Pawn->GetActorRotation().Yaw);
			FakeFrom = Pawn->GetActorLocation();
			DebugFake(FakeNames[FakeIndex], FakeSeconds[FakeIndex]);
			FakeIndex = (FakeIndex + 1) % FakeCount;
		}
	}

	// 실행 옵션 -VHBotTaserTest: 사제 테이저건 시험 (2026-11-22, 로그 [테이저 시험])
	static const bool bTaserTest = FParse::Param(FCommandLine::Get(), TEXT("VHBotTaserTest"));
	if (bTaserTest)
	{
		TickBotTaserTest(DeltaTime);
		return;
	}

	// 실행 옵션 -VHBotRailTest: 레일건 충전 발사 시험 (2026-11-20, 로그 [레일건 시험])
	static const bool bRailTest = FParse::Param(FCommandLine::Get(), TEXT("VHBotRailTest"));
	if (bRailTest)
	{
		TickBotRailTest(DeltaTime);
		return;
	}

	// 실행 옵션 -VHBotHunt: 다른 플레이어를 찾아가 공격한다. -VHBotHuntDelay=초 를 주면 그 시간 뒤부터(그 전에는 다른 봇 행동)
	static const bool bHunt = FParse::Param(FCommandLine::Get(), TEXT("VHBotHunt"));
	static float HuntDelay = -1.f;
	if (HuntDelay < 0.f)
	{
		HuntDelay = 0.f;
		FParse::Value(FCommandLine::Get(), TEXT("VHBotHuntDelay="), HuntDelay);
	}
	if (bHunt)
	{
		BotHuntAge += DeltaTime;
		if (BotHuntAge >= HuntDelay)
		{
			TickBotHunt(DeltaTime);
			return;
		}
	}

	// 실행 옵션 -VHBotHeist: 금고 열기 → 보물 줍기 → 반납을 되풀이한다 (상호작용 시험용)
	static const bool bHeist = FParse::Param(FCommandLine::Get(), TEXT("VHBotHeist")) || FParse::Param(FCommandLine::Get(), TEXT("VHBotLoot"))
		|| FParse::Param(FCommandLine::Get(), TEXT("VHBotPickTest"));

	// 실행 옵션 -VHScript=파일: 게임에 들어온 뒤 "초 명령" 줄들을 시간에 맞춰 실행한다 (봇은 제자리. 촬영·화면 확인용, # 줄은 설명)
	static FString ScriptPath;
	static const bool bScript = FParse::Value(FCommandLine::Get(), TEXT("VHScript="), ScriptPath);
	if (bScript)
	{
		static TArray<TPair<double, FString>> Script;
		static double ScriptStart = 0.0;
		static int32 ScriptNext = 0;
		const double NowSec = FPlatformTime::Seconds();
		if (ScriptStart == 0.0)
		{
			ScriptStart = NowSec;
			TArray<FString> Lines;
			FFileHelper::LoadFileToStringArray(Lines, *ScriptPath);
			for (FString Line : Lines)
			{
				Line.TrimStartAndEndInline();
				FString Time, Command;
				if (!Line.IsEmpty() && !Line.StartsWith(TEXT("#")) && Line.Split(TEXT(" "), &Time, &Command))
				{
					Script.Emplace(FCString::Atod(*Time), Command);
				}
			}
			UE_LOG(LogVHServerGame, Log, TEXT("[개발 스크립트] %s: 명령 %d개"), *ScriptPath, Script.Num());
		}
		while (Script.IsValidIndex(ScriptNext) && NowSec - ScriptStart >= Script[ScriptNext].Key)
		{
			UE_LOG(LogVHServerGame, Log, TEXT("[개발 스크립트] %.1f초: %s"), NowSec - ScriptStart, *Script[ScriptNext].Value);
			GEngine->Exec(GetWorld(), *Script[ScriptNext].Value);
			++ScriptNext;
		}
		// 스크립트만 있으면 봇은 제자리. 탐색(-VHBotHeist)·관찰(-VHBotWatch) 봇과 함께 쓰면 그 동작을 계속한다 (봇 화면 촬영용)
		if (!bHeist && !FParse::Param(FCommandLine::Get(), TEXT("VHBotWatch")))
		{
			return;
		}
	}

	if (bHeist)
	{
		TickBotHeist(DeltaTime);
		return;
	}

	// 실행 옵션 -VHRollTest: 구르기 동작 촬영 (조준 = 화면 위 고정, 앞이 트인 쪽으로 걷다가 구르기 → 달리다가 구르기, 슬로모션 연속 촬영)
	static const bool bRollTest = FParse::Param(FCommandLine::Get(), TEXT("VHRollTest"));
	if (bRollTest)
	{
		static double RollTestStart = 0.0;
		static int32 RollTestStep = 0;
		const double NowSec = FPlatformTime::Seconds();
		if (RollTestStart == 0.0)
		{
			RollTestStart = NowSec;
		}
		const double Age = NowSec - RollTestStart;
		if (RollTestStep == 0 && Age > 4.0)
		{
			++RollTestStep;
			GEngine->Exec(GetWorld(), TEXT("vh.Aim 0"));
			GEngine->Exec(GetWorld(), TEXT("vh.RollTest 0 0 0 1 walk"));	// 0 0 = 앞이 트인 방향
		}
		else if (RollTestStep == 1 && Age > 24.0)
		{
			++RollTestStep;
			GEngine->Exec(GetWorld(), TEXT("vh.RollTest 0 0 1 1 run"));
		}
		return;
	}

	// 실행 옵션 -VHBotRoll=초: 그 간격으로 다른 플레이어 옆(화면 왼쪽)으로 순간 이동해 오른쪽으로 걷다가(홀수 번째는 달리다가) 구른다
	//  → 다른 클라(-VHBotStill -VHRollShotsProxy)에서 다른 플레이어의 구르기 동작 확인용
	static float RollEvery = -1.f;
	if (RollEvery < 0.f)
	{
		RollEvery = 0.f;
		FParse::Value(FCommandLine::Get(), TEXT("VHBotRoll="), RollEvery);
	}
	if (RollEvery > 0.f)
	{
		static int32 RollCycle = 0;
		UVHActionComponent* Action = Pawn->FindComponentByClass<UVHActionComponent>();
		const float Before = BotWalkTime;
		BotWalkTime += DeltaTime;
		if (Before == 0.f || BotWalkTime >= RollEvery)
		{
			BotWalkTime = DeltaTime;
			++RollCycle;
			// 지금 자리에서 앞뒤로 가장 길게 트인 방향을 골라 뒤쪽 벽 가까이로 물러난다 (관찰 봇 -VHBotWatch 가 옆으로 따라온다)
			float Free = 0.f;
			BotDir = UVHWeaponVisualComponent::PrepareOpenRun(Pawn, Free);
			SendCurrentPositionNow();
			UE_LOG(LogVHServerGame, Log, TEXT("[봇 구르기] 출발 (%.0f, %.0f) 방향 (%.2f, %.2f) 트인 길이 %.0fcm"), Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y,
				BotDir.X, BotDir.Y, Free);
		}
		if (Action)
		{
			Action->SetWantsSprint(RollCycle % 2 == 0);
		}
		if (BotWalkTime < 1.8f)
		{
			Pawn->AddMovementInput(BotDir, 1.f);
		}
		static int32 RolledCycle = 0;
		if (BotWalkTime > 0.8f && RolledCycle != RollCycle && Action)
		{
			RolledCycle = RollCycle;
			if (Action->RequestRoll())
			{
				SendDodge();
				UE_LOG(LogVHServerGame, Log, TEXT("[봇 구르기] %d번째 (%s 중)"), RollCycle, RollCycle % 2 == 0 ? TEXT("달리기") : TEXT("걷기"));
			}
		}
		return;
	}

	// 실행 옵션 -VHBotWatch: 다른 플레이어와 9m 넘게 떨어지면 그 사람 옆 2.5m 로 옮겨 서 있는다 (다른 플레이어 동작을 화면에 담는 관찰 봇)
	static const bool bWatch = FParse::Param(FCommandLine::Get(), TEXT("VHBotWatch"));
	if (bWatch)
	{
		for (const TPair<uint16, FEntity>& Pair : Entities)
		{
			const FEntity& E = Pair.Value;
			if (E.Kind != EntityKind::Thief || E.Id == SelfEntityId)
			{
				continue;
			}
			const float Half = GetPawnHalfHeight(Pawn);
			const FVector Foot = Pawn->GetActorLocation() - FVector(0.f, 0.f, Half);
			if (FVector::Dist2D(Foot, E.Pos) > 900.f && (E.State & ES_DODGING) == 0)
			{
				const FVector Offsets[] = { FVector(250, 0, 0), FVector(-250, 0, 0), FVector(0, 250, 0), FVector(0, -250, 0), FVector(180, 180, 0), FVector(-180, -180, 0) };
				FCollisionQueryParams Query(FName(TEXT("VHBotWatch")), false, Pawn);
				for (const FVector& Offset : Offsets)
				{
					// 같은 방 안(그 사람에게서 벽 없이 보임) + 아래에 바닥이 있는 자리만 (벽 너머 맵 바깥으로 가지 않게)
					const FVector Eye = E.Pos + FVector(0.f, 0.f, 80.f);
					const FVector Spot = E.Pos + Offset;
					FHitResult Hit;
					if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, Spot + FVector(0.f, 0.f, 80.f), ECC_Visibility, Query)
						|| !GetWorld()->LineTraceSingleByChannel(Hit, Spot + FVector(0.f, 0.f, 50.f), Spot - FVector(0.f, 0.f, 60.f), ECC_Visibility, Query))
					{
						continue;
					}
					if (Pawn->TeleportTo(Spot + FVector(0.f, 0.f, Half + 2.f), Pawn->GetActorRotation()))
					{
						SendCurrentPositionNow();
						break;
					}
				}
			}
			break;
		}
		return;
	}

	// 실행 옵션 -VHBotKeys: 키 입력과 같은 길(AMCPTestCharacter::DoMove)로 W → D → S → A를 2초씩 누른다 → 이동 기준(화면 축/커서) 확인용
	static const bool bKeys = FParse::Param(FCommandLine::Get(), TEXT("VHBotKeys"));
	static const bool bStill = FParse::Param(FCommandLine::Get(), TEXT("VHBotStill"));	// 제자리에 서 있는 봇 (맞는 쪽 시험용)
	if (bKeys)
	{
		static const TCHAR* KeyNames[] = { TEXT("W"), TEXT("D"), TEXT("S"), TEXT("A") };
		static const FVector2D KeyInput[] = { FVector2D(0, 1), FVector2D(1, 0), FVector2D(0, -1), FVector2D(-1, 0) };	// (Right, Forward)
		static int32 KeyIndex = 0;
		static FVector KeyStart = FVector::ZeroVector;
		if (BotWalkTime == 0.f)
		{
			KeyStart = Pawn->GetActorLocation();
		}
		BotWalkTime += DeltaTime;
		if (AMCPTestCharacter* Char = Cast<AMCPTestCharacter>(Pawn))
		{
			Char->DoMove(KeyInput[KeyIndex].X, KeyInput[KeyIndex].Y);
		}
		if (BotWalkTime > 2.f)
		{
			const FVector Moved = Pawn->GetActorLocation() - KeyStart;
			UE_LOG(LogVHServerGame, Log, TEXT("[봇 키] %s 2초 → 이동 (%.0f, %.0f) cm, 화면 축 이동 %s, 몸 방향 %.0f°"), KeyNames[KeyIndex], Moved.X, Moved.Y,
				UVHAimComponent::IsScreenAxisMove() ? TEXT("켬") : TEXT("끔"), Pawn->GetActorRotation().Yaw);
			KeyIndex = (KeyIndex + 1) % 4;
			BotWalkTime = 0.f;
		}
	}
	else if (!bStill)
	{
		BotWalkTime += DeltaTime;
		if (BotWalkTime > 2.f)
		{
			BotWalkTime = 0.f;
			BotDir = BotDir.RotateAngleAxis(90.f, FVector::UpVector);
		}
		Pawn->AddMovementInput(BotDir, 1.f);
		Pawn->SetActorRotation(BotDir.Rotation());
		// 실행 옵션 -VHBotRun: Shift를 누른 채로 걷는다 (달리기·운동화 퍽·은신 중 달리기 불가 시험용)
		static const bool bRun = FParse::Param(FCommandLine::Get(), TEXT("VHBotRun"));
		if (bRun)
		{
			if (UVHActionComponent* Action = Pawn->FindComponentByClass<UVHActionComponent>())
			{
				Action->SetWantsSprint(true);
			}
		}
	}

	// 실행 옵션 -VHBotAttack: 4초마다 1.5초 동안 공격 키를 누른다 → 다른 클라에서 맞는 쪽 시험용
	static const bool bAttack = FParse::Param(FCommandLine::Get(), TEXT("VHBotAttack"));
	if (bAttack)
	{
		BotAttackTime += DeltaTime;
		if (!bAttackHeldSent && BotAttackTime > 4.f)
		{
			SendAttackHeld(true);
			BotAttackTime = 0.f;
		}
		else if (bAttackHeldSent && BotAttackTime > 1.5f)
		{
			SendAttackHeld(false);
			BotAttackTime = 0.f;
		}
	}
}

#undef LOCTEXT_NAMESPACE
