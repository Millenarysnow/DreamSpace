#pragma once

#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Styling/SlateBrush.h"
#include "DreamSceneCapturePresentationComponent.generated.h"

class ASceneCapture2D;
class UTextureRenderTarget2D;
class UWidgetComponent;
class SWidget;

/**
 * 将当前世界渲染成可放在玩家手部或其他表现节点上的场景缩略图。
 *
 * 这是纯表现层组件：
 * - 不生成交互命令，不修改装配体状态，也不参与事务验证；
 * - 不读取任何玩法装配体或拾取状态；
 * - 启用时创建 SceneCapture2D、RenderTarget 和世界空间显示面；
 * - 可以作为角色组件挂到身体、手部骨骼 Socket 或其他表现 Actor 上。
 *
 * 当前版本使用 UWidgetComponent 显示 RenderTarget，避免要求项目立刻创建
 * 一个带 Texture 参数的材质资产。后续需要门户材质、深度遮罩或三维外壳时，
 * 可以在不改变捕获逻辑的情况下替换 DisplayWidget 的承载方式。
 */
UCLASS(ClassGroup = (DreamPresentation), meta = (BlueprintSpawnableComponent))
class DREAMSPACE_API UDreamSceneCapturePresentationComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDreamSceneCapturePresentationComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 表现组件是否在 BeginPlay 后启用；相机导演也可以运行时开关它。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|启用")
	bool bEnabledAtBeginPlay = true;

	/** RenderTarget 的宽度；当前原型使用方形纹理，方便后续替换成门户材质。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获", meta = (ClampMin = "64", UIMin = "64"))
	int32 RenderTargetWidth = 1024;

	/** RenderTarget 的高度。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获", meta = (ClampMin = "64", UIMin = "64"))
	int32 RenderTargetHeight = 1024;

	/** SceneCapture 使用的投影方式；当前默认使用透视投影。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获")
	TEnumAsByte<ECameraProjectionMode::Type> ProjectionType = ECameraProjectionMode::Perspective;

	/** 透视捕获的水平视场角。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获", meta = (ClampMin = "5", ClampMax = "170"))
	float CaptureFOV = 60.0f;

	/** 自动取景时在包围球外额外留出的比例。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获", meta = (ClampMin = "1", UIMin = "1"))
	float AutoFramePadding = 1.25f;

	/** 捕获相机到目标中心的距离；没有配置取景 Actor 时使用此值。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获", meta = (ClampMin = "1", UIMin = "1"))
	float CaptureDistance = 2400.0f;

	/** 自动取景时额外加到场景中心上的世界空间偏移。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获")
	FVector CaptureTargetOffset = FVector::ZeroVector;

	/** 可选的场景中心 Actor；配置后使用其包围盒中心和大小自动取景。 */
	UPROPERTY(EditInstanceOnly, Category = "场景缩略图|捕获")
	TObjectPtr<AActor> CaptureTargetActor;

	/** 可选的取景 Actor 集合；适合把当前关卡的主要建筑一起框入缩略图。 */
	UPROPERTY(EditInstanceOnly, Category = "场景缩略图|捕获")
	TArray<TObjectPtr<AActor>> ActorsToFrame;

	/** 未启用跟随玩家相机时，捕获相机使用的世界旋转。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获")
	FRotator CaptureRotation = FRotator(-35.0f, -45.0f, 0.0f);

	/** 为调试或特定关卡使用当前玩家相机旋转；位置仍由自动取景计算。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获")
	bool bUsePlayerCameraRotation = false;

	/** 捕获输出类型。FinalColorLDR 便于直接显示；后续合成可改用 SceneColorHDR。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获")
	TEnumAsByte<ESceneCaptureSource> CaptureSource = SCS_FinalColorLDR;

	/** RenderTarget 清屏颜色；如果黑名单没有排除天空，天空仍可能覆盖此颜色。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获")
	FLinearColor CaptureClearColor = FLinearColor::Black;

	/**
	 * 捕获黑名单。
	 *
	 * SceneCapture 使用 PRM_RenderScenePrimitives 模式，因此会渲染普通场景，
	 * 但不会渲染这里列出的 Actor。可以把山体、天空盒、远景装饰或其他不希望
	 * 出现在手中模型里的 Actor 填到这里。当前测试场景可以保持为空。
	 */
	UPROPERTY(EditInstanceOnly, Category = "场景缩略图|捕获过滤")
	TArray<TObjectPtr<AActor>> ActorsToHideFromCapture;

	/** 是否把组件所属 Actor 整体加入黑名单；默认关闭，以便缩略图可以看到玩家自己。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获过滤")
	bool bHideOwnerActor = false;

	/** 世界空间显示面的相对变换；需要按实际手部/模型坐标调整朝向和位置。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|显示")
	FTransform DisplayRelativeTransform = FTransform(FRotator::ZeroRotator, FVector(0, 0, 60));

	/** 世界空间显示面的绘制尺寸，单位为 UMG 画布像素。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|显示", meta = (ClampMin = "1", UIMin = "1"))
	FVector2D DisplaySize = FVector2D(512.0f, 512.0f);

	/** 是否双面显示；原型阶段开启，避免因模型朝向造成画面消失。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|显示")
	bool bDisplayTwoSided = true;

	/** 立即重建黑名单、更新相机姿态并请求一次捕获；调试或运行时改变配置时可调用。 */
	UFUNCTION(BlueprintCallable, Category = "场景缩略图")
	void RefreshCaptureNow();

	/** 由相机导演或其他表现逻辑控制显示，不改变任何玩法状态。 */
	UFUNCTION(BlueprintCallable, Category = "场景缩略图")
	void SetPresentationEnabled(bool bEnabled);

	/** 返回当前输出纹理，供后续门户材质或其他表现组件复用。 */
	UFUNCTION(BlueprintPure, Category = "场景缩略图")
	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }

	/** 返回当前是否正在输出场景缩略图。 */
	UFUNCTION(BlueprintPure, Category = "场景缩略图")
	bool IsPresentationActive() const { return bPresentationActive; }

private:
	/** 运行时动态创建的场景捕获 Actor；它只属于表现层，不注册为交互装配体。 */
	UPROPERTY(Transient)
	TObjectPtr<ASceneCapture2D> CaptureActor;

	/** SceneCapture 的输出纹理。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	/** 将输出纹理放到世界中的显示面。 */
	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> DisplayWidget;

	/** 世界空间显示面内部的 Slate 图像；它直接引用 RenderTarget。 */
	TSharedPtr<SWidget> DisplaySlateWidget;

	/** Slate Image 使用的画刷必须由组件持有，不能使用 CreatePresentationResources 的局部变量。 */
	FSlateBrush DisplayBrush;

	bool bPresentationActive = false;

	void CreatePresentationResources();
	void DestroyPresentationResources();
	void SetPresentationActive(bool bActive);
	void UpdateCaptureView();
	void UpdateCaptureBlacklist();
	void CaptureOnce();
};
