#include "Gameplay/VHInteractionAuthority.h"

#include "Gameplay/VHInteractionSubsystem.h"
#include "Gameplay/VHInteractionComponent.h"
#include "Gameplay/VHInteractionConfig.h"
#include "Gameplay/VHInteractableBase.h"
#include "Gameplay/VHThiefStateComponent.h"
#include "Gameplay/VHTreasureCarrierComponent.h"
#include "Gameplay/VHSafe.h"
#include "Gameplay/VHBox.h"
#include "Gameplay/VHDroppedTreasure.h"
#include "Gameplay/VHItemPickup.h"
#include "Gameplay/VHFlashlightComponent.h"
#include "Gameplay/VHStatusEffectComponent.h"
#include "Gameplay/VHReturnPoint.h"
#include "Gameplay/VHGameplayConfig.h"
#include "Gameplay/VHServerGameSubsystem.h"
#include "Network/VHSettings.h"

#define LOCTEXT_NAMESPACE "VHInteract"

namespace
{
	const UVHInteractionConfig& Cfg()
	{
		return UVHSettings::Get().GetInteractionConfig();
	}

	template <typename T>
	T* FindOn(const UVHInteractionComponent* By)
	{
		return By && By->GetOwner() ? By->GetOwner()->FindComponentByClass<T>() : nullptr;
	}

	/** 환금 아이템을 들 수 있는 캐릭터인지 → 아니면 Offer를 막는다 */
	void CheckCarry(FVHInteractOffer& Offer, const UVHInteractionComponent* By)
	{
		if (!FindOn<UVHTreasureCarrierComponent>(By))
		{
			Offer.Blocked(EVHInteractBlock::NotAvailable, LOCTEXT("NoCarrier", "보물을 들 수 없는 캐릭터입니다"));
		}
	}
}

// ───────────────────────── 오브젝트별 안내 (방식·시간·문구) ─────────────────────────
// 시간 값은 화면 표시용이다. 실제 걸리는 시간은 서버가 SC_INTERACT 로 알려 준다.

FVHInteractOffer UVHLocalInteractionAuthority::BuildOffer(const UVHInteractionComponent* By, const AVHInteractableBase* Target) const
{
	const UVHInteractionConfig& C = Cfg();
	const float Scale = GetSubsystem() ? GetSubsystem()->DebugDurationScale : 1.f;
	// 시작 전 안내 시간도 내 작업 속도로 (팀 서버 0168a0a, 2026-11-27): 서버와 같은 식 = 작업량(기본 초 × 100) ÷ 작업 속도, ms 올림.
	// 유용한 도구(110)면 금고 10초 → 9.1초, 상자 3초 → 2.7초, 반납 4초 → 3.6초. 실제 시간은 작업이 시작되면 서버 SC_INTERACT 로 온다
	const UVHServerGameSubsystem* Game = By && By->GetWorld() ? By->GetWorld()->GetSubsystem<UVHServerGameSubsystem>() : nullptr;
	const float WorkSpeed = Game && Game->IsActive() ? FMath::Max(1.f, Game->GetMyWorkSpeed()) : 100.f;
	auto WorkSeconds = [WorkSpeed](float BaseSeconds)
	{
		return FMath::CeilToFloat(BaseSeconds * 100.f * 1000.f / WorkSpeed) / 1000.f;
	};

	switch (Target->GetKind())
	{
	case EVHEntityKind::Safe:
	{
		// 닫힌 금고만 열 수 있다(진행도 유지). 열린 금고에는 상호작용이 없다 — 보물은 금고 앞에 따로 생기는 환금 아이템을 줍는다 [서버 v3.0]
		const AVHSafe* Safe = CastChecked<AVHSafe>(Target);
		if (Safe->GetSafeState() == EVHSafeState::Closed)
		{
			return FVHInteractOffer::Make(EVHInteractMode::Hold, WorkSeconds(C.SafeOpenSeconds) * Scale, C.bSafeKeepProgress, C.SafeOpenText);
		}
		return FVHInteractOffer();
	}

	case EVHEntityKind::DroppedTreasure:
	{
		// 안내는 "줍기"만. 이름·품질·금액은 커서를 올렸을 때 정보창으로 보여 준다 [기획 2026-11 조작키 2.3]
		const AVHDroppedTreasure* Treasure = CastChecked<AVHDroppedTreasure>(Target);
		FVHInteractOffer Offer = FVHInteractOffer::Make(EVHInteractMode::Tap, C.TreasurePickupSeconds * Scale, false,
			Treasure->GetItem().Kind == EVHItemKind::Treasure ? C.TreasurePickupText : LOCTEXT("PickupValuable", "환금 아이템 줍기"));
		CheckCarry(Offer, By);
		return Offer;
	}

	case EVHEntityKind::Box:
		// [기획·서버] 3초 작업, 멈추면 처음부터 (공격으로도 부술 수 있다). 2026-11-24: 무기 상자 / 환금 상자 이름으로
		return FVHInteractOffer::Make(EVHInteractMode::Hold, WorkSeconds(C.BoxOpenSeconds) * Scale, false,
			FText::Format(LOCTEXT("BoxOpenNamed", "{0} 열기"), CastChecked<AVHBox>(Target)->GetBoxName()));

	case EVHEntityKind::WeaponPickup:
	case EVHEntityKind::ConsumablePickup:
		// F로 즉시 줍기 (칸이 차 있으면 서버가 바꿔 준다)
		return FVHInteractOffer::Make(EVHInteractMode::Tap, 0.f, false,
			FText::Format(LOCTEXT("PickupItem", "{0} 줍기"), CastChecked<AVHItemPickup>(Target)->GetItemName()));

	case EVHEntityKind::ReturnPoint:
	{
		// 들고 있는 환금 아이템 전부 반납. "들고 있는가"는 서버용 담당이 SC_MONEY 값으로 다시 판단한다.
		// 2026-11-23 팀 서버: 구역 안에서 4초 머무는 시간 작업(금고·상자처럼 F 한 번 → 진행 게이지, 끊기면 처음부터)
		const UVHGameplayConfig& G = UVHSettings::Get().GetGameplayConfig();
		const UVHTreasureCarrierComponent* Carrier = FindOn<UVHTreasureCarrierComponent>(By);
		const int32 Count = Carrier ? Carrier->GetCount() : 0;
		FVHInteractOffer Offer = FVHInteractOffer::Make(EVHInteractMode::Hold, WorkSeconds(G.ReturnSeconds), false,
			Count > 0 ? FText::Format(LOCTEXT("ReturnDetail", "{0} ({1}개 · {2}$)"), G.ReturnText, Count, FText::AsNumber(Carrier->GetTotalValue()))
				: G.ReturnText);
		if (Count == 0)
		{
			Offer.Blocked(EVHInteractBlock::NotAvailable, LOCTEXT("NothingToReturn", "반납할 환금 아이템이 없습니다"));
		}
		return Offer;
	}

	default:
		return FVHInteractOffer();
	}
}

// ───────────────────────── 공통 ─────────────────────────

FVHInteractOffer UVHLocalInteractionAuthority::Evaluate(const UVHInteractionComponent* By, const AVHInteractableBase* Target) const
{
	if (!By || !Target)
	{
		return FVHInteractOffer();
	}
	const UVHThiefStateComponent* State = FindOn<UVHThiefStateComponent>(By);
	FVHInteractOffer Offer = BuildOffer(By, Target);
	if (!Offer.IsAvailable())
	{
		return Offer;
	}

	// 의적 본인 상태: 자유 상태에서만 (쓰러져 있으면 안 됨)
	if (State && !State->IsFree())
	{
		return Offer.Blocked(EVHInteractBlock::NotAvailable, LOCTEXT("NotFree", "지금은 할 수 없습니다"));
	}

	// 경직 중에는 할 수 없다
	if (const UVHStatusEffectComponent* Status = FindOn<UVHStatusEffectComponent>(By); Status && Status->IsStunned())
	{
		return Offer.Blocked(EVHInteractBlock::NotAvailable, LOCTEXT("Stunned", "경직 중에는 할 수 없습니다"));
	}

	// 동시 1명 [기획]
	const UVHInteractionComponent* User = Target->GetActiveUser();
	if (User && User != By)
	{
		return Offer.Blocked(EVHInteractBlock::Busy, LOCTEXT("Busy", "다른 의적이 사용 중입니다"));
	}
	return Offer;
}

void UVHLocalInteractionAuthority::RequestBegin(UVHInteractionComponent* By, AVHInteractableBase* Target)
{
	// 서버가 없다(게임 레벨을 직접 ▶ 플레이). 판정할 곳이 없으므로 안내만 한다
	if (UVHInteractionSubsystem* Sub = GetSubsystem(); Sub && By)
	{
		Sub->ApplyReject(By, EVHRejectReason::InvalidState, LOCTEXT("NoServer", "서버에 연결되어 있지 않아 상호작용할 수 없습니다"));
	}
}

void UVHLocalInteractionAuthority::RequestCancel(UVHInteractionComponent* By)
{
	// 진행 중인 상호작용이 없다 (시작 자체가 서버를 거친다)
}

void UVHLocalInteractionAuthority::RequestFlashlight(UVHFlashlightComponent* Light, bool bOn)
{
	UVHInteractionSubsystem* Sub = GetSubsystem();
	if (!Sub || !Light)
	{
		return;
	}
	if (!Light->bHasFlashlight)
	{
		Sub->ApplyFlashlightReject(Light, EVHRejectReason::NoFlashlight);
		return;
	}
	// 연타 방지 [제안: 0.3초]
	if (Sub->GetNow() - Light->GetLastToggleTime() < Cfg().FlashlightToggleCooldown)
	{
		Sub->ApplyFlashlightReject(Light, EVHRejectReason::Cooldown);
		return;
	}
	// 자유 상태가 아니면(쓰러짐 등) 켤 수 없다 (끄는 것은 허용)
	const UVHThiefStateComponent* State = Light->GetOwner() ? Light->GetOwner()->FindComponentByClass<UVHThiefStateComponent>() : nullptr;
	if (bOn && State && !State->IsFree())
	{
		Sub->ApplyFlashlightReject(Light, EVHRejectReason::InvalidState);
		return;
	}
	Sub->ApplyFlashlight(Light, bOn);
}

#undef LOCTEXT_NAMESPACE
