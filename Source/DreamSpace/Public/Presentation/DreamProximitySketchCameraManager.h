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
	virtual void UpdateCamera(float DeltaTime) override;

	/**
	 * 开始一次“世界恢复”动画：让正常渲染半径从当前值平滑扩大到目标值。
	 *
	 * 该请求只影响当前相机管理器所属的本地玩家，不会修改 Config 中的默认半径，
	 * 也不会把运行时状态写入磁盘上的共享材质实例或地图。目标半径会被限制为不小于当前值，
	 * 因而这个入口只负责恢复世界，不会因为误配置把正常渲染范围缩小。
	 * 动画中收到更大的目标会从当前半径重新开始；相同或更小的请求不重置进度。
	 *
	 * @param TargetRadius       动画结束时使用的正常渲染半径（厘米）。
	 * @param Duration           动画时长（秒）；小于等于 0 时立即完成。
	 * @param bRestoreEntireWorld 最后四分之一时段额外淡出线稿，包含天空和目标半径外的场景；完成后移除通道。
	 * @return 是否接受了新的恢复请求；非法参数、没有本地 Pawn、无新增恢复目标或全世界已恢复时返回 false。
	 */
	UFUNCTION(BlueprintCallable, Category = "距离线稿|世界恢复")
	bool StartWorldRecovery(float TargetRadius, float Duration, bool bRestoreEntireWorld = true);

	/** 读回当前实际半径：未恢复时使用配置 / 控制台，恢复开始后使用动画的瞬时半径。 */
	UFUNCTION(BlueprintPure, Category = "距离线稿|世界恢复")
	float GetEffectiveNormalRadius() const;

	/** 当前是否正在执行正常渲染范围扩张。 */
	UFUNCTION(BlueprintPure, Category = "距离线稿|世界恢复")
	bool IsWorldRecoveryActive() const { return bWorldRecoveryActive; }

	/** 最近一次被接受的恢复动画是否完成；只扩大半径的动画完成后仍保留外圈线稿。 */
	UFUNCTION(BlueprintPure, Category = "距离线稿|世界恢复")
	bool HasWorldRecoveryFinished() const { return bWorldRecoveryFinished; }

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
	/** 先读取真实越肩相机，再覆盖本次入场的镜头姿态；结束帧直接返回原始 POV。 */
	virtual void UpdateViewTargetInternal(FTViewTarget& OutVT, float DeltaTime) override;
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

	/** 菜单纸纹只在从菜单进入时出现；使用瞬时 MID 渐变，不修改磁盘上的纸张实例。 */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ArrivalPaperMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ArrivalPaperInstance;
	UPROPERTY(Transient)
	FPostProcessSettings ArrivalPaperSettings;

	/** 只包含本效果的 blendable；没有曝光、调色、景深等额外覆盖项。 */
	UPROPERTY(Transient)
	FPostProcessSettings SketchSettings;

	/**
	 * 世界恢复的瞬时状态。它们不使用 UPROPERTY 配置，避免 PIE 期间把动画进度保存到资产。
	 * 半径单独缓存是为了让组件在两帧之间触发时，能够从当前实际进度继续扩张。
	 */
	bool bWorldRecoveryActive = false;
	bool bWorldRecoveryFinished = false;
	bool bRestoreEntireWorldRequested = false;
	float WorldRecoveryElapsed = 0.0f;
	float WorldRecoveryDuration = 0.0f;
	float WorldRecoveryStartRadius = 0.0f;
	float WorldRecoveryTargetRadius = 0.0f;
	float WorldRecoveryCurrentRadius = 0.0f;
	/** 全世界恢复时在动画末段淡出。记录起始权重，避免更大目标重入时线稿重新变浓。 */
	float WorldRecoveryStartStrength = 1.0f;
	float WorldRecoveryCurrentStrength = 1.0f;

	/** 只在 UpdateCamera 推进一次；视角混合可能一帧调用两次 ApplyCameraModifiers。 */
	void UpdateWorldRecovery(float DeltaTime);
	/** 精确落到目标状态，兼容零时长以及低帧率下单帧跨过动画终点。 */
	void CompleteWorldRecovery();

	/** 缓存最亮的可见方向光。其旋转每帧读取，解谜中旋转建筑不会固定光向。 */
	TWeakObjectPtr<ADirectionalLight> KeyLight;
	void UpdateLightDirection();
	friend class FDreamProximitySketchLifecycleTest;
};
