#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "DreamProximitySketchCameraManager.generated.h"

class ADirectionalLight;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * 游玩相机的距离线稿：以被控制的 Pawn 为中心，内圈正常，外圈手绘。
 * 后处理只加入本地玩家的相机缓存，不放置全局 PostProcessVolume，因而
 * 手办 SceneCapture、其他玩家视图和编辑器视口不会共享该玩家的位置参数。
 * 菜单使用独立控制器，继续沿用原先已经验收的展示材质。
 */
UCLASS(Config = Game)
class DREAMSPACE_API ADreamProximitySketchCameraManager : public APlayerCameraManager
{

	GENERATED_BODY()

public:
	ADreamProximitySketchCameraManager();

	/** 关闭后不提交此后处理通道，不替换建筑材质或更改项目抗锯齿设置。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "距离线稿", meta = (DisplayName = "启用距离线稿"))
	bool bEnableProximitySketch = true;

	/** 完全保留原场景的三维球形半径；600 cm = 6 m。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "距离线稿", meta = (ClampMin = "0.0", Units = "cm", DisplayName = "正常渲染半径"))
	float NormalRadius = 600.0f;

	/** 从 NormalRadius 向外渐变；150 cm 后完全变成手绘，0 则近似硬切。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "距离线稿", meta = (ClampMin = "0.0", Units = "cm", DisplayName = "过渡宽度"))
	float TransitionWidth = 150.0f;

	/** 外圈风格化权重。1 为完整线稿，0 为原始场景，允许蓝图做整体淡入淡出。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "距离线稿", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "效果强度"))
	float EffectStrength = 1.0f;

	/** 调试视图：0 正式，1 明暗依据，2 球形距离遮罩，3 原始场景。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "距离线稿|调试", meta = (ClampMin = "0", ClampMax = "3"))
	int32 SketchDebugView = 0;

	/** 只读访问当前相机的动态实例，便于蓝图调节排线和自动化检查真实运行参数。 */
	UFUNCTION(BlueprintPure, Category = "距离线稿")
	UMaterialInstanceDynamic* GetSketchMaterialInstance() const { return SketchInstance; }

protected:
	virtual void ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	/** 硬引用已生成的材质实例，使烘焙能够发现资源依赖；不依赖编辑器 Python。 */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> SketchMaterial;

	/** 末端平滑也使用独立硬引用，只作用于本玩家的线稿范围。 */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> SmoothingMaterial;

	/** 每个相机独有的瞬时实例，绝不把玩家位置写入磁盘上的共享 MI。 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SketchInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SmoothingInstance;

	/** 只包含本效果的 blendable；没有曝光、调色、景深等额外覆盖项。 */
	UPROPERTY(Transient)
	FPostProcessSettings SketchSettings;

	/** 缓存最亮的可见方向光。其旋转每帧读取，解谜中旋转建筑不会固定光向。 */
	TWeakObjectPtr<ADirectionalLight> KeyLight;
	void UpdateLightDirection();
	friend class FDreamProximitySketchLifecycleTest;
};
