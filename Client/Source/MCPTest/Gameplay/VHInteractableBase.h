#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/VHInteractionTypes.h"
#include "Gameplay/VHInteractionRules.h"
#include "VHInteractableBase.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UVHInteractionComponent;
class UVHInteractionSubsystem;

/**
 * 상호작용 오브젝트(금고·떨어진 보물·반납지점)의 공통 부모.
 *
 * 이 클래스와 자식들은 "상태 보관 + 연출"만 한다.
 * "할 수 있는가 / 끝나면 무슨 일이 일어나는가" 판정은 UVHInteractionAuthority 한곳에 있다.
 * (나중에 서버가 판정하면 판정 클래스만 서버 응답을 받는 쪽으로 바꾸면 된다.)
 *
 * 외형: 기본 도형(Body)이 들어 있고, 블루프린트 자식에서 메시·머티리얼·크기를 바꾸면 된다.
 */
UCLASS(Abstract, Blueprintable)
class MCPTEST_API AVHInteractableBase : public AActor
{
	GENERATED_BODY()

public:
	AVHInteractableBase();

	EVHEntityKind GetKind() const { return Kind; }

	/** 방 안 엔티티 ID (서버 entity_id와 같은 역할). 등록할 때 정해진다 */
	int32 GetEntityId() const { return EntityId; }
	void SetEntityId(int32 InId) { EntityId = InId; }

	/** 거리 판정에 쓰는 상자(기본: 충돌이 있는 컴포넌트들의 경계) */
	virtual FBox GetInteractBounds() const;

	/** 상호작용 거리: 기본은 From에서 상자 가장자리까지 수평 거리 (반납지점은 가운데까지) */
	virtual float GetInteractDistance(const FVector& From) const;
	/** 이 거리 안이면 상호작용 가능: 기본 SearchRadius(150), 반납지점 ReturnPointInteractRadius(100) */
	virtual float GetInteractRange() const;
	/**
	 * 시간이 걸리는 작업(금고·상자)을 할 때 서야 하는 지점. 서버는 이 지점에서 작업 거리(130cm)를 잰다.
	 * 기본 = 상자 가운데, 금고 = 앞면에서 조금 떨어진 곳(맵 파일의 interact_pos)
	 */
	virtual FVector GetWorkPoint() const;
	/** F를 누르면 작업 지점에서 이 거리 안까지 다가간 뒤 작업을 요청한다 (기본: 설정의 WorkApproachDistance, 상자는 몸통이 있어 더 멀리) */
	virtual float GetWorkApproachDistance() const;

	/** 커서를 올렸을 때 정보창에 쓸 내용. 기본 = 이름만(금고·상자·반납지점) [기획 2026-11 조작키 2.1] */
	virtual FVHInfoCard GetInfoCard() const;

	/** 안내 문구·진행 바를 띄울 위치 */
	FVector GetPromptLocation() const;

	/** 지금 사용 중인 의적(동시 1명). 없으면 nullptr */
	UVHInteractionComponent* GetActiveUser() const { return ActiveUser.Get(); }
	void SetActiveUser(UVHInteractionComponent* User);

	FVHHoldProgress& GetHoldProgress() { return HoldProgress; }
	const FVHHoldProgress& GetHoldProgress() const { return HoldProgress; }

	/** 아무도 안 쓰는데 진행도가 남아 있어 HUD에 옅게 보여줄지 [제안] */
	virtual bool ShouldShowIdleProgress() const;

	/**
	 * 테두리(외곽선) 상태 [팀 결정 2026-10-25: 면 전체 발광 대신 진짜 외곽선].
	 * 메시들의 커스텀 뎁스·스텐실 값을 바꾸고, 포스트 프로세스 머티리얼(M_VHOutline)이 테두리를 그린다.
	 * 내 손전등 빛 안에 있는 물품만 켜지도록 UVHInteractionComponent 가 매 프레임 정한다.
	 */
	virtual void SetOutline(EVHOutline State);
	EVHOutline GetOutline() const { return OutlineState; }

	/**
	 * 조명과 관계없이 항상 보이는 물품인가 (반납지점) [팀 결정].
	 * 이런 물품은 손전등 빛 밖에서도 상호작용 대상이 된다 [사용자 결정 2026-10-25].
	 */
	bool IsAlwaysVisible() const { return bAlwaysVisible; }

	/** 상태가 바뀌었을 때 블루프린트 연출용 */
	UFUNCTION(BlueprintImplementableEvent, Category = "VaultHeist")
	void OnVisualStateChanged();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	UVHInteractionSubsystem* GetInteractionSubsystem() const;

	/** 메시의 기본 도형 머티리얼(Color 파라미터)에 색을 입힌다 */
	static void TintMesh(UStaticMeshComponent* Mesh, const FLinearColor& Color);

	/** 자식 생성자에서 기본 도형을 넣을 때 사용 */
	static UStaticMesh* FindBasicShape(const TCHAR* Name);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	/** 본체 외형. 블루프린트에서 메시 교체 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Body;

	/** 안내 문구 위치 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> PromptAnchor;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VaultHeist")
	EVHEntityKind Kind = EVHEntityKind::Safe;

	/** 기본 도형 색 (메시를 바꾸면 무시해도 됨) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VaultHeist|Visual")
	FLinearColor BodyColor = FLinearColor(0.3f, 0.3f, 0.3f);

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "VaultHeist")
	int32 EntityId = 0;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "VaultHeist")
	FVHHoldProgress HoldProgress;

	TWeakObjectPtr<UVHInteractionComponent> ActiveUser;

	EVHOutline OutlineState = EVHOutline::None;

	/** 조명과 관계없이 항상 보임 (자식 생성자에서 정한다) */
	bool bAlwaysVisible = false;
};
