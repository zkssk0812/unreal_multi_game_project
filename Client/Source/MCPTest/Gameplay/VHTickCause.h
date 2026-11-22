#pragma once

#include "CoreMinimal.h"

/**
 * "왜 내 작업이 멈췄나 / 왜 내 은신이 풀렸나"를 서버 한 틱 단위로 가리는 작은 기록장.
 *
 * 서버는 한 틱에 생긴 일을 [이벤트들] → [SC_SNAPSHOT] 순서로 보낸다 (서버 GameRoomPlay.cpp).
 * 이벤트에는 이유가 없으므로, 같은 틱(= 지난 스냅샷 뒤 ~ 이번 스냅샷 전)에 함께 온 것을 보고 이유를 추정한다.
 *
 *  - 공격받아 작업이 멈춤 [서버 IN-05, Q-41, 정호님 답 2026-11-16]:
 *      맞아서 멈출 때 서버는 같은 틱 안에서 SC_INTERACT(Cancel)를 먼저, SC_DAMAGE를 나중에 보낸다.
 *      → "서버가 멈춘 Cancel" 뒤에 "내가 받은 SC_DAMAGE"가 같은 틱에 오면 공격받아 멈춘 것.
 *      아닌 경우: 내가 공격·구르기·스킬·소비 아이템·직접 취소를 보내서 멈춤, 경직(스냅샷의 Stunned), 쓰러짐(SC_DOWNED/Downed).
 *      Cancel보다 먼저 온 대미지는 이전 일이므로 세지 않는다(그 대미지는 그때 이미 Cancel을 일으켰거나 작업 중이 아니었다).
 *
 *  - 상호작용해서 은신이 풀림 [서버 S-03, Q-42]:
 *      서버는 상호작용이 실제로 시작(또는 성사)될 때 은신을 푼다. 거절된 상호작용(다른 사람이 여는 금고,
 *      반납할 것이 없는 반납지점)은 은신을 풀지 않는다.
 *      → 같은 틱에 "내 상호작용 확인"(SC_INTERACT Begin, F로 줍기 SC_PICKUP, 반납 SC_MONEY)이 있고
 *        그 틱의 스냅샷에서 Cloaked 비트가 꺼졌으면 상호작용 때문에 풀린 것.
 *
 * 판정만 하는 순수 구조체라 자동화 테스트(VaultHeist.V3.TickCause)로 순서별 결과를 확인한다.
 */
struct FVHTickCause
{
	/** 서버가 내 작업을 멈췄다 (SC_INTERACT Cancel). bOwnCause: 방금 내가 취소·공격·구르기·스킬·소비 아이템을 보냈다 */
	void OnInteractCancel(bool bOwnCause)
	{
		bCancel = true;
		bCancelByMe = bOwnCause;
		bDamageAfterCancel = false;	// Cancel보다 먼저 온 대미지는 세지 않는다
	}

	/** 내가 대미지를 받았다 (SC_DAMAGE target = 나) */
	void OnSelfDamage()
	{
		if (bCancel)
		{
			bDamageAfterCancel = true;
		}
	}

	/** 내가 쓰러졌다 (SC_DOWNED victim = 나) */
	void OnSelfDowned() { bDowned = true; }

	/** 내 상호작용이 실제로 시작되거나 성사됐다 (거절되지 않음) */
	void OnInteractConfirmed() { bInteractConfirmed = true; }

	/** 이번 틱에 내 상호작용이 확인됐나 — 스냅샷에서 Cloaked 비트가 꺼질 때 본다 */
	bool WasInteractConfirmed() const { return bInteractConfirmed; }

	/**
	 * 스냅샷을 적용한 뒤 부른다: 이번 틱의 판정을 돌려주고 다음 틱을 위해 비운다.
	 * bStunnedNow·bDownedNow = 이번 스냅샷의 내 상태 비트.
	 * true → "공격받아 작업이 멈췄습니다"
	 */
	bool EndTick(bool bStunnedNow, bool bDownedNow)
	{
		const bool bInterrupted = bCancel && !bCancelByMe && bDamageAfterCancel && !bDowned && !bDownedNow && !bStunnedNow;
		*this = FVHTickCause();
		return bInterrupted;
	}

private:
	bool bCancel = false;
	bool bCancelByMe = false;
	bool bDamageAfterCancel = false;
	bool bDowned = false;
	bool bInteractConfirmed = false;
};
