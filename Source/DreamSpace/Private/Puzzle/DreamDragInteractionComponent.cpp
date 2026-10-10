#include "DreamDragInteractionComponent.h"

#include "DreamPivotPointComponent.h"
#include "DreamPuzzleDebug.h"
#include "DreamSpace.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

UDreamDragInteractionComponent::UDreamDragInteractionComponent()
{
	// 持续输入由玩家控制器提供；组件 Tick 只承担引用失效清理和统一调试绘制。
	PrimaryComponentTick.bCanEverTick = true;
	bAutoActivate = true;
	bEditableWhenInherited = true;
}

void UDreamDragInteractionComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!EnsureReference())
	{
		UE_LOG(LogDreamSpace, Warning,
			TEXT("自由交互组件 [%s]：根组件必须为 Movable%s。"), *GetName(),
			RequiresPivot() ? TEXT("，并配置同一 Actor 的枢轴点") : TEXT(""));
	}
}

void UDreamDragInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	EndDrag();
	Super::EndPlay(EndPlayReason);
}

void UDreamDragInteractionComponent::Deactivate()
{
	// Deactivate 会停用组件 Tick，必须在停用瞬间释放操作者，不能等待下一帧清理。
	EndDrag();
	Super::Deactivate();
}

void UDreamDragInteractionComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bDragging && (!DragInteractor.IsValid() || (RequiresPivot() && !IsValid(CachedPivot.Get())) || !IsActive()))
		EndDrag();
	if (bReferenceInitialized && DreamPuzzleDebug::IsPivotDebugDrawEnabled())
		DrawDebugRange();
}

bool UDreamDragInteractionComponent::IsValidRay(const FVector& RayOrigin, const FVector& RayDirection)
{
	// ContainsNaN 同时排除无穷大，拒绝非法射线，防止蓝图输入把 Actor 变换污染为 NaN。
	return !RayOrigin.ContainsNaN() && !RayDirection.ContainsNaN() && !RayDirection.IsNearlyZero();
}

bool UDreamDragInteractionComponent::BeginDrag(
	AActor* Interactor, const FVector& RayOrigin, const FVector& RayDirection)
{
	if (bDragging || !IsValid(Interactor) || !IsActive() || !IsValidRay(RayOrigin, RayDirection) || !EnsureReference())
		return false;
	DragInteractor = Interactor;
	bDragging = true;
	bNeedsNewDragSample = false;
	InitializeDragSample(RayOrigin, RayDirection.GetSafeNormal());
	return true;
}

void UDreamDragInteractionComponent::UpdateDrag(
	const FVector& RayOrigin, const FVector& RayDirection, const FVector2D& PointerDelta)
{
	if (!bDragging)
		return;
	// 目标、操作者、枢轴或组件状态变化时结束会话；坏射线只跳过这一帧。
	// 不能在拖动途中补做参考系初始化，否则枢轴随 Actor 移动后会悄悄改变范围零点。
	const AActor* Owner = GetOwner();
	const USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	if (!IsValid(Owner) || !DragInteractor.IsValid() || (RequiresPivot() && !IsValid(CachedPivot.Get())) ||
		!IsActive() || !Root || Root->Mobility != EComponentMobility::Movable)
	{
		EndDrag();
		return;
	}
	if (!IsValidRay(RayOrigin, RayDirection) || PointerDelta.ContainsNaN())
	{
		SuspendDragInput();
		return;
	}
	if (bNeedsNewDragSample)
	{
		InitializeDragSample(RayOrigin, RayDirection.GetSafeNormal());
		bNeedsNewDragSample = false;
		return;
	}
	ApplyDragSample(RayOrigin, RayDirection.GetSafeNormal(), PointerDelta);
}

void UDreamDragInteractionComponent::EndDrag()
{
	bDragging = false;
	DragInteractor.Reset();
}

void UDreamDragInteractionComponent::RebaseDragRay(const FVector& RayOrigin, const FVector& RayDirection)
{
	if (bDragging && IsValidRay(RayOrigin, RayDirection))
		RebaseDragSample(RayOrigin, RayDirection.GetSafeNormal());
}

void UDreamDragInteractionComponent::OnInteracted_Implementation(AActor* Interactor)
{
	// 保留已有接口供蓝图调用。接口没有鼠标射线参数，默认从操作者相机中心开始；
	// 项目控制器会直接调用 BeginDrag，以传入普通鼠标或手办映射后的精确世界射线。
	const APawn* Pawn = Cast<APawn>(Interactor);
	const APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (Controller)
	{
		FVector Origin;
		FRotator Rotation;
		Controller->GetPlayerViewPoint(Origin, Rotation);
		BeginDrag(Interactor, Origin, Rotation.Vector());
	}
}

bool UDreamDragInteractionComponent::ReinitializeReference()
{
	if (bDragging)
		return false;
	bReferenceInitialized = false;
	CachedPivot = nullptr;
	return EnsureReference();
}

bool UDreamDragInteractionComponent::EnsureReference()
{
	const AActor* Owner = GetOwner();
	const USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	if (!IsValid(Owner) || !Root || Root->Mobility != EComponentMobility::Movable)
		return false;
	if (bReferenceInitialized && (!RequiresPivot() || IsValid(CachedPivot.Get())))
		return true;
	CachedPivot = RequiresPivot() ? ResolvePivot() : nullptr;
	if (RequiresPivot() && !IsValid(CachedPivot.Get()))
		return false;

	// 不依赖枢轴的派生组件使用 Actor 的局部坐标系；既有平移/旋转组件仍走原来的枢轴路径。
	ReferencePivotWorld = RequiresPivot() ? CachedPivot->GetComponentLocation() : Owner->GetActorLocation();
	const FQuat Rotation = RequiresPivot() ? CachedPivot->GetComponentQuat() : Owner->GetActorQuat();
	switch (InteractionAxis)
	{
	case EDreamPivotRotationAxis::X:
		ReferenceAxisWorld = Rotation.GetAxisX();
		break;
	case EDreamPivotRotationAxis::Y:
		ReferenceAxisWorld = Rotation.GetAxisY();
		break;
	case EDreamPivotRotationAxis::Z:
	default:
		ReferenceAxisWorld = Rotation.GetAxisZ();
		break;
	}
	ReferenceActorTransform = Owner->GetActorTransform();
	bReferenceInitialized = true;
	OnReferenceInitialized();
	return true;
}

UDreamPivotPointComponent* UDreamDragInteractionComponent::ResolvePivot() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
		return nullptr;
	if (UDreamPivotPointComponent* Pivot = Cast<UDreamPivotPointComponent>(PivotComponent.GetComponent(Owner)))
		return Pivot->GetOwner() == Owner ? Pivot : nullptr;

	TArray<UDreamPivotPointComponent*> Pivots;
	Owner->GetComponents(Pivots);
	if (!PivotComponentName.IsNone())
	{
		for (UDreamPivotPointComponent* Pivot : Pivots)
			if (IsValid(Pivot) && Pivot->GetFName() == PivotComponentName)
				return Pivot;
		return nullptr;
	}
	return Pivots.IsEmpty() ? nullptr : Pivots[0];
}
