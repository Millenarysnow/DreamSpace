#pragma once

#include "DreamDragInteractionComponent.h"
#include "DreamDragRotatableComponent.generated.h"

/**
 * 轴向自由旋转组件：所属 Actor 绕固定枢轴轴线连续公转和自转，并受正负角度范围限制。
 * 开局 Actor 姿态定义为零度，角度以带符号的累计值保存，支持超过 180 度甚至多圈的范围。
 * 碰撞开启时停在最后安全角度；重力开关开启时，站立角色随每段实际旋转同步改变重力。
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (DreamPuzzle),
	meta = (BlueprintSpawnableComponent, DisplayName = "Dream Drag Rotatable"))
class DREAMSPACE_API UDreamDragRotatableComponent : public UDreamDragInteractionComponent
{
	GENERATED_BODY()

public:
	UDreamDragRotatableComponent();

	/** 当前相对开局零姿态的累计角度（度）；保留完整圈数，不归一化到正负 180 度。 */
	UFUNCTION(BlueprintPure, Category = "自由旋转")
	float GetCurrentAngleDegrees() const { return CurrentAngleDegrees; }

	/**
	 * 蓝图请求绝对累计角度，返回实际到达的角度。先截断范围，再检查完整旋转路径。
	 * 例如从 0 请求 270 会正向转过 270 度，碰撞查询不会走等价的反向 90 度短路径。
	 */
	UFUNCTION(BlueprintCallable, Category = "自由旋转")
	float SetRotationAngle(float AngleDegrees);

protected:
	virtual void OnReferenceInitialized() override;
	virtual void InitializeDragSample(const FVector& RayOrigin, const FVector& RayDirection) override;
	virtual void RebaseDragSample(const FVector& RayOrigin, const FVector& RayDirection) override;
	virtual void ApplyDragSample(
		const FVector& RayOrigin, const FVector& RayDirection, const FVector2D& PointerDelta) override;
	virtual void DrawDebugRange() const override;

private:
	/** 零姿态两侧的最大角度，正向遵循枢轴所选轴的右手定则；均填写非负数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "自由旋转|范围",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", UIMin = "0.0", DisplayName = "负向角度范围（度）"))
	float NegativeAngleLimit = 90.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "自由旋转|范围",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", UIMin = "0.0", DisplayName = "正向角度范围（度）"))
	float PositiveAngleLimit = 90.0f;

	/**
	 * 与 Dream Rotatable 相同：只影响真正站在所属 Actor 移动碰撞体上的 Character。
	 * 实际转过多少角度就旋转多少重力、胶囊、速度和视角；离开后保留最后世界重力方向。
	 * 与碰撞开关独立，开启碰撞时还会检查站立角色的路径，防止头部随平台穿进天花板。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "自由旋转|重力",
		meta = (AllowPrivateAccess = "true", DisplayName = "站立角色重力跟随旋转"))
	bool bRotateStandingCharacterGravity = false;

	/** 抓取视线平行于旋转平面或正对枢轴中心时，改用向右/向上的屏幕输入作为正向。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "自由旋转|输入",
		meta = (AllowPrivateAccess = "true", AdvancedDisplay, ClampMin = "0.0", UIMin = "0.0",
			DisplayName = "退化视角灵敏度（度/像素）"))
	float FallbackDegreesPerPixel = 0.5f;

	/** Actor 原点位于轴线上时没有可画的公转半径，使用此长度绘制角度扇形。 */
	UPROPERTY(
		EditAnywhere, Category = "调试", meta = (ClampMin = "1.0", UIMin = "1.0", DisplayName = "旋转范围调试半径"))
	float DebugArcRadius = 100.0f;

	float CurrentAngleDegrees = 0.0f;
	FVector LastRadialDirection = FVector::ForwardVector;
	bool bHasRaySample = false;
	bool bUseScreenFallback = false;

	/** 射线与过枢轴、法向为旋转轴的平面求交，返回面内单位径向。 */
	bool TryGetRadialDirection(const FVector& RayOrigin, const FVector& RayDirection, FVector& OutDirection) const;
	/** 从固定零姿态直接计算目标姿态，避免多次抓取与多次增量四元数产生累计漂移。 */
	FTransform GetTransformAtAngle(float AngleDegrees) const;
};
