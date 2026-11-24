#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Gameplay/VHWeaponCatalog.h"
#include "VHCrosshairWidget.generated.h"

/**
 * 게임 중 마우스 조준점 (2026-11-25). 탑뷰라 마우스 위치 = 조준 위치. 가장 큰 목적은 "가시성":
 * 모든 모양이 지름 48px 이상(DA_VHUIStyle CrosshairMinSize)·선 3px 이상 + 어두운 테두리로, 밝은 바닥·어두운 바닥·이펙트 위에서도 보인다.
 *
 *  - 무기별 모양 (DA_VHWeaponCatalog → Weapons → Crosshair, 자동 = 무기 종류)
 *      근접·맨손 = 괄호 4개 / 권총·테이저건 = 틈 있는 십자 / 레일건 = 십자 + 큰 원(충전 1초 동안 채워지는 링, 잠금 중 색 변화)
 *      화염방사기 = 부채꼴 테두리(내 캐릭터 → 커서 방향) / 수류탄·지뢰 = 큰 착탄 원 + 십자 / 사제 테이저건(Q로 든 상태) = 원 + 번개
 *  - 반동: 내 SC_FIRE(레일건은 빔 순간)마다 벌어졌다가 부드럽게 돌아온다 (판정과 무관한 연출)
 *  - 히트 마커: 서버가 내 공격의 SC_DAMAGE / SC_DOWNED 를 보낼 때만 굵은 X + 작은 소리 (쓰러뜨림은 다른 색)
 *  - 사거리 밖: 커서가 지금 무기의 사거리 밖이면 색을 바꾼다 (흐리게 하지 않음). 수류탄은 실제로 떨어질 자리에 작은 원
 *  - 상태: 탄 적음·탄 없음 글자, 1초 무기의 쿨타임 원 (재장전은 게임에 없음)
 * 숫자는 1080p 기준 px 이고 위젯 좌표(UI 배율)로 그리므로 해상도가 달라도 화면 대비 같은 크기다.
 * 뷰포트 맨 위(Z 100)에 붙어 천장·문 가림과 상관없이 보인다. 판정은 서버가 한다(표시만).
 *
 * AVHGamePlayerController 가 만들고, 보일지(=운영체제 커서를 숨길지)를 매 프레임 정한다.
 * 개발용 콘솔: vh.Crosshair 0|1 · at dx dy · follow · free · weapon N|skill|auto · gallery 0|1|2 · info
 */
UCLASS()
class MCPTEST_API UVHCrosshairWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 설정(DA_VHUIStyle bCrosshairEnabled, 콘솔 vh.Crosshair 0/1)으로 켜져 있나 */
	static bool IsEnabledByConfig();
	/** 지금 조준점을 그리는 위젯 (개발 명령용) */
	static UVHCrosshairWidget* GetActive();

	/** 개발용: 상태를 한 줄로 (위치·무기·모양·사거리·운영체제 커서) */
	FString Describe() const;

	/**
	 * 지금 조준점이 차지하는 영역 (뷰포트 위젯 좌표 = HUD 캔버스 좌표, 가운데 + 반지름 — 상태 글자·히트 마커 포함 여유).
	 * HUD 가 상호작용(F) 안내·정보창을 조준점과 겹치지 않게 옮길 때 쓴다. 조준점이 안 보이면 false
	 */
	bool GetScreenBounds(FVector2D& OutCenter, float& OutRadius) const;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void HandleMyShot(uint8 WeaponId);
	void HandleMyHit(bool bKill, uint8 TargetKind);
	void BindServer();

	/** 한 조준점의 그릴 상태 (게임 중 1개, 갤러리는 여러 개) */
	struct FDrawState
	{
		FVector2D Center = FVector2D::ZeroVector;
		FVHCrosshairSpec Spec;
		FVector2D Dir = FVector2D(0.0, -1.0);	// 화면에서 내 캐릭터 → 커서 방향 (부채꼴)
		float Kick = 0.f;						// 벌어짐 (px)
		bool bOutOfRange = false;
		bool bDisabled = false;					// 쓰러짐·경직
		bool bLocked = false;					// 레일건 잠금
		bool bCharging = false;
		float Charge = 0.f;						// 레일건 충전 0~1
		bool bLanding = false;					// 수류탄: 사거리 밖 → 실제로 떨어질 자리
		FVector2D Landing = FVector2D::ZeroVector;
		float Cooldown = 0.f;					// 다음 공격까지 남은 비율 (1 → 0), 0 = 없음
		FString Status;							// 조준점 옆 글자 (탄 2 / 탄 없음 / 잠금)
		float MarkerT = -1.f;					// 히트 마커 진행 0~1, < 0 = 없음
		bool bMarkerKill = false;
		FString Label;							// 갤러리 이름
	};
	void PaintOne(const FDrawState& D, const FGeometry& Geo, FSlateWindowElementList& Out, int32 Layer) const;
	void BuildGallery(const FVector2D& View, TArray<FDrawState>& Out) const;

	TWeakObjectPtr<class UVHServerGameSubsystem> BoundServer;
	FDelegateHandle ShotHandle;
	FDelegateHandle HitHandle;

	FDrawState State;
	bool bHasCursor = false;
	float KickPx = 0.f;
	double MarkerStartReal = -1.0;		// 히트 마커 시작 (FPlatformTime)
	bool bMarkerKill = false;
	uint8 ShownWeapon = 255;
	bool bShownSkillTaser = false;
	float LastRangeDistance = 0.f;		// 개발용: 내 캐릭터 → 커서 지점 거리 (cm)
	double LastShotLogTime = 0.0;
	int32 ShotsSeen = 0;
	int32 HitsSeen = 0;
	int32 KillsSeen = 0;
};
