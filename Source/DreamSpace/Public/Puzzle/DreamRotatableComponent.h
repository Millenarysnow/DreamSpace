#pragma once

#include "Components/ActorComponent.h"
#include "DreamInteractableInterface.h"
#include "DreamRotatableComponent.generated.h"

class UDreamPivotPointComponent;

/** 可转动组件允许绕枢轴点局部坐标系的哪一根轴转动。 */
UENUM(BlueprintType)
enum class EDreamPivotRotationAxis : uint8
{
	/** 枢轴点局部 X 轴（红轴）。 */
	X,
	/** 枢轴点局部 Y 轴（绿轴）。 */
	Y,
	/** 枢轴点局部 Z 轴（蓝轴）。 */
	Z
};

/**
 * 可转动组件：允许所属 Actor 在被交互后，绕枢轴点转动一个固定的步进角。
 *
 * 工作方式：
 * - 需要一个枢轴点组件（UDreamPivotPointComponent）作为转轴的原点和方向来源；
 * - 配置绕枢轴点局部 X/Y/Z 中的哪根轴、每次交互转动多少度；
 * - 玩家（或蓝图）触发交互后，所属 Actor 会在 RotationDuration 秒内平滑地
 *   绕“过枢轴点的目标轴”公转 + 自转；RotationDuration 为 0 时瞬间完成；
 * - 转动进行中再次触发会被忽略，避免姿态叠加出错。
 *
 * 由于枢轴点是一个独立组件，策划可以自由摆放它的位置和朝向，
 * 从而实现门绕铰链转、机关绕任意斜轴转等效果，而不需要修改代码。
 *
 * 调试：控制台 dream.DebugPivots 1 会同时绘制枢轴点和本组件当前使用的转轴。
 */
UCLASS(ClassGroup = (DreamPuzzle), meta = (BlueprintSpawnableComponent))
class DREAMSPACE_API UDreamRotatableComponent : public UActorComponent, public IDreamInteractableInterface
{
	GENERATED_BODY()

public:
	UDreamRotatableComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(
		float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	//~ IDreamInteractableInterface 接口：玩家交互时触发一次转动。
	virtual void OnInteracted_Implementation(AActor* Interactor) override;

	/** 立即触发一次转动；转动进行中再次调用会被忽略并输出警告日志。 */
	UFUNCTION(BlueprintCallable, Category = "可转动")
	void TriggerRotation();

	/** 当前是否正在转动中。 */
	UFUNCTION(BlueprintPure, Category = "可转动")
	bool IsRotating() const { return bRotating; }

private:
	// ---------- 配置项 ----------

	/**
	 * 枢轴点组件的名称；留空时自动使用所属 Actor 上的第一个枢轴点组件。
	 * 同一个 Actor 挂多个枢轴点时，用这个名称区分当前组件使用哪一个。
	 */
	UPROPERTY(EditAnywhere, Category = "可转动|枢轴")
	FName PivotComponentName;

	/** 绕枢轴点局部坐标系的哪根轴转动。 */
	UPROPERTY(EditAnywhere, Category = "可转动|转动")
	EDreamPivotRotationAxis RotationAxis = EDreamPivotRotationAxis::Z;

	/** 每次交互转动的角度（度）；负值表示绕轴反向转动。 */
	UPROPERTY(EditAnywhere, Category = "可转动|转动")
	float StepAngleDegrees = 90.0f;

	/** 单次转动的耗时（秒）；0 表示瞬间完成。 */
	UPROPERTY(EditAnywhere, Category = "可转动|转动", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RotationDuration = 0.5f;

	/** 调试绘制时转轴箭头的长度（厘米）。 */
	UPROPERTY(EditAnywhere, Category = "调试")
	float DebugAxisDrawLength = 150.0f;

	// ---------- 内部状态 ----------

	/** 在 BeginPlay 中解析并缓存的枢轴点组件；未找到时为 nullptr 并已在日志中报警。 */
	UPROPERTY(Transient)
	TObjectPtr<UDreamPivotPointComponent> CachedPivot;

	/** 是否正在转动中。 */
	bool bRotating = false;
	/** 本次转动已经过的时间（秒）。 */
	float RotationElapsed = 0.0f;

	// 以下三个量在触发瞬间快照固定：
	// 枢轴点在被转 Actor 上时会随 Actor 一起运动，但“绕某轴转动”不会改变该轴上的点和轴方向，
	// 因此用触发瞬间的快照做整段插值在数学上是自洽的，也避免逐帧重取引入误差。
	/** 触发瞬间所属 Actor 的世界变换（插值起点）。 */
	FTransform StartActorTransform = FTransform::Identity;
	/** 完整步进角对应的四元数（绕触发瞬间的转轴方向）。 */
	FQuat FullStepQuat = FQuat::Identity;
	/** 触发瞬间枢轴点的世界位置（公转中心）。 */
	FVector PivotWorldLocation = FVector::ZeroVector;

	// ---------- 内部函数 ----------

	/** 按配置解析枢轴点组件：优先按名称匹配，否则取 Actor 上第一个枢轴点组件。 */
	UDreamPivotPointComponent* ResolvePivot() const;

	/** 取枢轴点局部坐标系中配置轴的世界方向（单位向量）。 */
	FVector GetRotationAxisWorldDir(const UDreamPivotPointComponent* Pivot) const;

	/** 调试开关打开时逐帧绘制当前使用的转轴（品红色双向箭头）。 */
	void DrawDebugRotationAxis() const;
};
