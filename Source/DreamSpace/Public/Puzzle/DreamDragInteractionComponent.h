#pragma once

#include "Components/ActorComponent.h"
#include "DreamInteractableInterface.h"
#include "DreamRotatableComponent.h"
#include "Engine/EngineTypes.h"
#include "DreamDragInteractionComponent.generated.h"

class UDreamPivotPointComponent;

/**
 * 自由拖动交互的公共基类，统一枢轴引用、固定参考系和一次拖动的生命周期。
 * 玩家控制器只传入世界射线与屏幕鼠标增量，具体的距离或角度计算由派生组件完成。
 * 普通视口传入玩家相机射线，手办模式传入 SceneCapture 射线，两者使用同一套运动逻辑。
 *
 * 参考系在 BeginPlay 固定：枢轴虽然挂在被移动 Actor 上，也不会让范围零点随物体漂移。
 * 运行时若确实需要重新设置枢轴、轴向或零姿态，应先结束拖动，再调用 ReinitializeReference。
 */
UCLASS(Abstract, BlueprintType, ClassGroup = (DreamPuzzle))
class DREAMSPACE_API UDreamDragInteractionComponent : public UActorComponent, public IDreamInteractableInterface
{
	GENERATED_BODY()

public:
	UDreamDragInteractionComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Deactivate() override;
	virtual void TickComponent(
		float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnInteracted_Implementation(AActor* Interactor) override;

	/**
	 * 开始一次拖动，只记录射线起点，不立即改变 Actor 姿态。
	 * 同一组件同时只接受一个操作者；重新抓取不会重建范围，也不会让物体跳到鼠标位置。
	 */
	UFUNCTION(BlueprintCallable, Category = "自由交互")
	bool BeginDrag(AActor* Interactor, const FVector& RayOrigin, const FVector& RayDirection);

	/**
	 * 提交当前鼠标对应的世界射线。PointerDelta 是本帧屏幕像素增量，X 向右、Y 向下。
	 * 仅在抓取视角无法稳定解算轴线或旋转平面时使用像素增量作为备用输入。
	 * 鼠标射线无需再次命中机关；因此抓取后移出机关轮廓仍能继续拖动。
	 */
	UFUNCTION(BlueprintCallable, Category = "自由交互")
	void UpdateDrag(const FVector& RayOrigin, const FVector& RayDirection, const FVector2D& PointerDelta);

	/** 结束拖动并保留最后实际到达的姿态；不会自动回弹或吸附到固定步长。 */
	UFUNCTION(BlueprintCallable, Category = "自由交互")
	void EndDrag();

	/** 映射暂时失效时丢弃输入基线；恢复后的第一帧只重新采样，避免跨出手办面片再回来时跳变。 */
	void SuspendDragInput() { bNeedsNewDragSample = true; }

	/**
	 * 用当前帧相机投影重新采样“上一鼠标位置”，不产生运动，也不切换本次拖动的备用输入模式。
	 * 相机可能被平台重力跟随带着旋转；比较同一相机下的两条射线可消除这种相机变化的假输入。
	 */
	void RebaseDragRay(const FVector& RayOrigin, const FVector& RayDirection);

	/** 当前是否有操作者持有本组件。控制器用此值判断是否仍应发送持续输入。 */
	UFUNCTION(BlueprintPure, Category = "自由交互")
	bool IsDragging() const { return bDragging; }

	/**
	 * 从当前 Actor 与枢轴姿态重新建立参考系；拖动期间拒绝执行，避免途中的运动轨迹突变。
	 * 平移重新计算当前轴向坐标，旋转把当前姿态定义为新的零度姿态，不会移动 Actor。
	 */
	UFUNCTION(BlueprintCallable, Category = "自由交互")
	bool ReinitializeReference();

protected:
	/** 派生类构造函数可选择默认轴；此属性在两个具体组件的详情面板中共用。 */
	UPROPERTY(EditAnywhere, Category = "自由交互|枢轴", meta = (DisplayName = "运动轴"))
	EDreamPivotRotationAxis InteractionAxis = EDreamPivotRotationAxis::X;

	/** 默认关闭，开启后由具体组件在每次实际运动前检查全部附属查询碰撞体。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "自由交互|碰撞", meta = (DisplayName = "运动考虑碰撞"))
	bool bConsiderCollision = false;

	/**
	 * 输入增益，1 表示几何映射原比例；0 暂停拖动输入。蓝图直接设置位置或角度时不使用此值。
	 * 运行时仍会再次限制到非负值，避免蓝图绕过编辑器 ClampMin 后改变拖动方向。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "自由交互|输入",
		meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "拖动灵敏度"))
	float DragSensitivity = 1.0f;

	/** 已固定的枢轴世界位置、所选轴世界单位向量及参考 Actor 姿态。 */
	FVector ReferencePivotWorld = FVector::ZeroVector;
	FVector ReferenceAxisWorld = FVector::ForwardVector;
	FTransform ReferenceActorTransform = FTransform::Identity;

	/** 保证参考系已初始化且根组件仍允许移动，供拖动和蓝图直接驱动共用。 */
	bool EnsureReference();
	virtual void OnReferenceInitialized() {}
	virtual void InitializeDragSample(const FVector& RayOrigin, const FVector& RayDirection) {}
	virtual void RebaseDragSample(const FVector& RayOrigin, const FVector& RayDirection) {}
	virtual void ApplyDragSample(const FVector& RayOrigin, const FVector& RayDirection, const FVector2D& PointerDelta)
	{
	}
	virtual void DrawDebugRange() const {}

private:
	/**
	 * 与现有交互组件相同的枢轴选择器，只选择同一 Actor 的 Dream Pivot Point。
	 * 优先级为真实组件引用、备用名称、Actor 上第一个枢轴；名称填错时不会误选其他枢轴。
	 */
	UPROPERTY(EditAnywhere, Category = "自由交互|枢轴",
		meta = (UseComponentPicker = true, AllowedClasses = "/Script/DreamSpace.DreamPivotPointComponent",
			DisplayName = "枢轴点组件"))
	FComponentReference PivotComponent;

	UPROPERTY(
		EditAnywhere, Category = "自由交互|枢轴", meta = (AdvancedDisplay, DisplayName = "枢轴点组件名称（备用）"))
	FName PivotComponentName;

	/** 弱操作者引用不会阻止角色销毁；枢轴缓存由 UE 反射管理，销毁后会重新解析。 */
	TWeakObjectPtr<AActor> DragInteractor;
	UPROPERTY(Transient)
	TObjectPtr<UDreamPivotPointComponent> CachedPivot;
	bool bDragging = false;
	bool bNeedsNewDragSample = false;
	bool bReferenceInitialized = false;

	UDreamPivotPointComponent* ResolvePivot() const;
	static bool IsValidRay(const FVector& RayOrigin, const FVector& RayDirection);
};
