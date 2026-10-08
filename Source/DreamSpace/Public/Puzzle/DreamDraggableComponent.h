#pragma once

#include "DreamDragInteractionComponent.h"
#include "DreamDraggableComponent.generated.h"

/**
 * 轴向自由拖动组件：所属 Actor 沿枢轴局部 X/Y/Z 的一根轴，在有限距离内连续移动。
 * 枢轴开局世界位置为范围零点，Actor 初始的垂直于轴的偏移会被保留，抓取时不会吸到轴线上。
 * 开启碰撞后停在障碍之前；下一帧可以立即反向拖动，不播放步进组件的失败回弹。
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (DreamPuzzle),
	meta = (BlueprintSpawnableComponent, DisplayName = "Dream Draggable"))
class DREAMSPACE_API UDreamDraggableComponent : public UDreamDragInteractionComponent
{
	GENERATED_BODY()

public:
	/** 当前沿固定范围轴的实际坐标（厘米），0 对应枢轴的开局世界位置。 */
	UFUNCTION(BlueprintPure, Category = "自由拖动")
	float GetCurrentTranslation() const { return CurrentTranslation; }

	/**
	 * 蓝图直接请求一个绝对轴向坐标。距离会被范围截断，开启碰撞时检查整段平移路径。
	 * 返回实际到达的坐标；与鼠标拖动共用同一入口，便于关卡脚本驱动和查询受阻结果。
	 */
	UFUNCTION(BlueprintCallable, Category = "自由拖动")
	float SetTranslation(float Translation);

protected:
	virtual void OnReferenceInitialized() override;
	virtual void InitializeDragSample(const FVector& RayOrigin, const FVector& RayDirection) override;
	virtual void RebaseDragSample(const FVector& RayOrigin, const FVector& RayDirection) override;
	virtual void ApplyDragSample(
		const FVector& RayOrigin, const FVector& RayDirection, const FVector2D& PointerDelta) override;
	virtual void DrawDebugRange() const override;

private:
	/** 范围分别位于枢轴零点两侧；0 表示该方向不提供额外行程。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "自由拖动|范围",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", UIMin = "0.0", DisplayName = "负向范围（厘米）"))
	float NegativeLimit = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "自由拖动|范围",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", UIMin = "0.0", DisplayName = "正向范围（厘米）"))
	float PositiveLimit = 100.0f;

	/**
	 * 抓取时若视线几乎平行运动轴，轴在屏幕上退化为一个点，改用“向右或向上”为正向的输入。
	 * 此时每像素移动多少厘米；正常拖动中模式固定，重新抓取或暂停后恢复时重新判断视角。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "自由拖动|输入",
		meta = (AllowPrivateAccess = "true", AdvancedDisplay, ClampMin = "0.0", UIMin = "0.0",
			DisplayName = "退化视角灵敏度（厘米/像素）"))
	float FallbackCentimetersPerPixel = 1.0f;

	/** 参考直线与 Actor 原点之间的固定垂直偏移，不改变 Actor 的旋转与缩放。 */
	FVector PerpendicularOffset = FVector::ZeroVector;
	float CurrentTranslation = 0.0f;
	double LastRayCoordinate = 0.0;
	bool bHasRaySample = false;
	bool bUseScreenFallback = false;

	/** 求射线与固定轴线的最近点轴坐标；平行、射线后方或非有限结果会返回失败。 */
	bool TryGetRayCoordinate(const FVector& RayOrigin, const FVector& RayDirection, double& OutCoordinate) const;
};
