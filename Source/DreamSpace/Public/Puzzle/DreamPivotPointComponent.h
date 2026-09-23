#pragma once

#include "Components/SceneComponent.h"
#include "DreamPivotPointComponent.generated.h"

/**
 * 枢轴点组件：一个纯粹的目标点标记，本身不包含任何行为。
 *
 * 它利用 SceneComponent 自带的相对位置与世界朝向，为其他交互组件
 * （如可转动组件，后续还会有更多基于枢轴的交互）提供：
 * - 枢轴的世界位置（即组件自身位置）；
 * - 枢轴的局部坐标系（即组件自身旋转），交互组件可绕它的 X/Y/Z 轴运动。
 *
 * 由于朝向完全由组件自身旋转决定，策划可以在编辑器里自由摆放和旋转该组件，
 * 让所属 Actor 支持绕任意自定义方向的交互，而不需要改任何代码。
 *
 * 调试：控制台输入 dream.DebugPivots 1 后，所有枢轴点会在世界中被绘制出来
 * （球标记 + RGB 三色局部轴），输入 dream.DebugPivots 0 关闭。
 */
UCLASS(
	Blueprintable,
	BlueprintType,
	ClassGroup = (DreamPuzzle),
	meta = (BlueprintSpawnableComponent, DisplayName = "Dream Pivot Point"))
class DREAMSPACE_API UDreamPivotPointComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDreamPivotPointComponent();

	virtual void TickComponent(
		float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 调试绘制时枢轴点球标记的半径（厘米）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "调试", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DebugMarkerRadius = 8.0f;

	/** 调试绘制时局部坐标轴箭头的长度（厘米）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "调试", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DebugAxisLength = 100.0f;

	/**
	 * 在世界中绘制枢轴点标记与局部坐标轴（X 红、Y 绿、Z 蓝）。
	 * 仅在调试开关 dream.DebugPivots 打开时由 Tick 逐帧调用，绘制的图形只存在一帧。
	 */
	void DrawDebugVisualization() const;
};
