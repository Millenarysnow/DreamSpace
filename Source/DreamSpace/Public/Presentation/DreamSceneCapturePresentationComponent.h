#pragma once

#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "DreamSceneCapturePresentationComponent.generated.h"

class ASceneCapture2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/** SceneCapture 显示面如何相对观察相机转向。 */
UENUM(BlueprintType)
enum class EDreamMiniatureFacingMode : uint8
{
	/** 保持 DisplayRelativeTransform 中配置的固定方向。 */
	Fixed,
	/** 完整绕三个轴朝向观察相机，适合先验证捕获视差。 */
	FaceCamera,
	/** 只绕世界竖直轴转向观察相机，避免面片上下翻转。 */
	FaceCameraAroundWorldUp
};

/**
 * 将当前世界渲染成可放在玩家手部或其他表现节点上的场景缩略图。
 *
 * 这是纯表现层组件：
 * - 不生成交互命令，不修改装配体状态，也不参与事务验证；
 * - 不读取任何玩法装配体或拾取状态；
 * - 启用时创建 SceneCapture2D、RenderTarget 和世界空间显示面；
 * - 可以作为角色组件挂到身体、手部骨骼 Socket 或其他表现 Actor 上。
 *
 * 当前版本使用一个带透明材质的静态平面直接采样 RenderTarget，避免 Slate
 * 中间层吞掉 SceneCapture 的 Alpha。后续需要门户材质或三维外壳时，
 * 可以替换 DisplayMeshAsset 和 DisplayMaterialAsset，不需要改捕获数学。
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

	/** 捕获相机到目标中心的距离；未跟随玩家相机时作为固定取景距离。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获", meta = (ClampMin = "1", UIMin = "1"))
	float CaptureDistance = 2400.0f;

	/**
	 * 真实场景映射到手办空间时使用的统一缩放。
	 *
	 * 例如 0.1 表示真实场景中的 100 cm，在手办中占 10 cm。
	 * 这个值会参与观察相机位置换算，不能只拿来缩放显示面，否则不会产生正确视差。
	 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|坐标映射", meta = (ClampMin = "0.001", UIMin = "0.001"))
	float MiniatureSceneScale = 0.25f;

	/**
	 * 捕获场景的参考坐标系到真实世界的变换。
	 *
	 * SceneCapture 最终仍然需要一个真实世界 Transform。计算过程先把外部相机
	 * 变换到手办坐标，再通过这个参考系还原到实际场景世界坐标。默认值为世界原点。
	 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|坐标映射")
	FTransform CapturedSceneReferenceTransform = FTransform::Identity;

	/** 可选的参考 Actor；设置后优先使用它的世界变换作为捕获场景参考系。 */
	UPROPERTY(EditInstanceOnly, Category = "场景缩略图|坐标映射")
	TObjectPtr<AActor> CapturedSceneReferenceActor;

	/** 是否让 SceneCapture 跟随第三人称相机的完整位置、旋转和视场角。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|坐标映射")
	bool bFollowPlayerCamera = true;

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

	/** 捕获输出类型；透明显示默认依赖 SceneColorHDR 的反向不透明度。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获")
	TEnumAsByte<ESceneCaptureSource> CaptureSource = SCS_SceneColorHDR;

	/** RenderTarget 清屏 RGB；Alpha 始终按 SceneColorHDR 的反向不透明度约定清为 1。 */
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

	/** 显示平面的世界尺寸，单位为厘米；平面资源的原始尺寸会自动换算。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|显示", meta = (ClampMin = "1", UIMin = "1"))
	FVector2D DisplayWorldSize = FVector2D(80.0f, 80.0f);

	/** 显示平面资源；默认使用 UE 基础 Plane，后续可以替换为自定义手办外壳。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|显示")
	TSoftObjectPtr<UStaticMesh> DisplayMeshAsset;

	/** 透明显示材质；参数名由 DisplayTextureParameterName 指定。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|显示")
	TSoftObjectPtr<UMaterialInterface> DisplayMaterialAsset;

	/** 材质中接收 SceneCapture RenderTarget 的纹理参数名。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|显示")
	FName DisplayTextureParameterName = TEXT("SceneCaptureTexture");

	/** 面片是否跟随第三人称观察相机转向；它只改变显示面，不改变 SceneCapture 坐标映射。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|显示")
	EDreamMiniatureFacingMode DisplayFacingMode = EDreamMiniatureFacingMode::FaceCamera;

	/** 面片转向时使用的上方向；当前原型固定为世界上方向。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|显示")
	FVector DisplayUpDirection = FVector::UpVector;

	/**
	 * 是否将显示图像绕面片法线旋转 180 度。
	 *
	 * UE 基础 Plane 的局部 UV 方向与 SceneCapture RenderTarget 的画面坐标
	 * 约定相差一个 180 度旋转，因此默认开启以使手办中的上下、左右与外部
	 * 观察相机保持一致。这个开关只修正显示面上的图像方向，不会改变
	 * SceneCapture 的世界位置、旋转或手办视差计算。
	 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|显示")
	bool bRotateDisplayImage180Degrees = true;

	/** 立即重建黑名单、更新相机姿态并请求一次捕获；调试或运行时改变配置时可调用。 */
	UFUNCTION(BlueprintCallable, Category = "场景缩略图")
	void RefreshCaptureNow();

	/** 由相机导演或其他表现逻辑控制显示，不改变任何玩法状态。 */
	UFUNCTION(BlueprintCallable, Category = "场景缩略图")
	void SetPresentationEnabled(bool bEnabled);

	/**
	 * 将外部世界中的观察相机映射到 SceneCapture 所在的真实世界。
	 * 该纯函数保留在公开接口中，便于自动化测试和后续相机导演复用同一套数学。
	 */
	UFUNCTION(BlueprintPure, Category = "场景缩略图|坐标映射")
	static FTransform MapObserverCameraToCaptureWorld(
		const FTransform& ObserverWorldTransform,
		const FTransform& MiniatureFrameWorldTransform,
		const FTransform& CapturedSceneReferenceWorldTransform,
		float InMiniatureSceneScale);

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

	/** 将输出纹理直接显示在世界空间平面上的静态网格。 */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> DisplayMesh;

	/** 运行时材质实例，用于把 RenderTarget 绑定到透明材质参数。 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DisplayMaterialInstance;

	bool bPresentationActive = false;

	void CreatePresentationResources();
	void DestroyPresentationResources();
	void SetPresentationActive(bool bActive);
	void UpdateCaptureView();
	void UpdateDisplayFacing();
	void UpdateCaptureBlacklist();
	void CaptureOnce();
	bool GetPlayerCameraPOV(FMinimalViewInfo& OutPOV) const;
	FTransform ResolveCapturedSceneReference() const;
};
