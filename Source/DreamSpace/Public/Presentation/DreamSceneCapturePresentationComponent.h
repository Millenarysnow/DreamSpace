#pragma once

#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "DreamSceneCapturePresentationComponent.generated.h"

class ASceneCapture2D;
class ADreamSceneCaptureAnchor;
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

	/**
	 * 捕获相机到场景中心的距离。
	 *
	 * 跟随玩家相机的固定轨道模式下，它是捕获相机到锚点的固定半径，决定手办内的景别；
	 * 观察相机的推拉、SpringArm 被障碍推近都不会改变它。未跟随玩家相机时作为固定取景距离。
	 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获", meta = (ClampMin = "1", UIMin = "1"))
	float CaptureDistance = 2400.0f;

	/**
	 * 真实场景映射到手办空间时使用的统一缩放。
	 *
	 * 例如 0.1 表示真实场景中的 100 cm，在手办中占 10 cm。
	 * 只在等比映射路径（bUseFixedCaptureOrbit 关闭）中使用：观察相机相对面片的
	 * 偏移除以此比例后还原到被捕获场景。该路径的取景距离随观察相机到面片的
	 * 实际距离变化，镜头推近时手办画面也会一起推近。
	 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|坐标映射", meta = (ClampMin = "0.001", UIMin = "0.001"))
	float MiniatureSceneScale = 0.25f;

	/**
	 * 是否使用固定半径轨道映射观察相机；默认开启。
	 *
	 * 开启时，捕获相机始终在锚点周围、距离为 CaptureDistance 的球面上并看向锚点：
	 * - 观察方向取“观察相机 -> 面片”的完整三维方向，映射进手办坐标系。
	 *   绕手办转动时从对应侧面看建筑；从高处向下看时看到对应的俯视角；
	 * - 画面上方向取观察相机的上方向，与 FaceCamera 面片的上方向一致；
	 * - 视场角使用 CaptureFOV，与观察相机的 FOV 无关；
	 * - 观察相机的前后位移不会改变捕获距离。
	 *
	 * 关闭时退回等比映射：捕获相机 = 锚点 ⊕ (观察相机 − 面片) / MiniatureSceneScale，
	 * 并沿用观察相机的旋转和 FOV。
	 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|坐标映射")
	bool bUseFixedCaptureOrbit = true;

	/**
	 * 是否忽略 SpringArm 的碰撞修正，用“未被障碍推近的理想镜头位置”计算手办视角。
	 *
	 * 镜头被墙体推近时，真实相机位置会突然跳变；开启后手办视角只随鼠标绕转变化，
	 * 不随碰撞推近抖动。面片自身仍然朝向真实相机，保证在主视口中正面可见。
	 * 观察目标上找不到 SpringArm 时自动使用真实相机位置。
	 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|坐标映射")
	bool bIgnoreSpringArmCollision = true;

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

	/**
	 * 游戏开始时自动绑定的相机轨道锚点，只读显示绑定结果。
	 *
	 * 在“被捕获的真实场景”中放置一个 DreamSceneCaptureAnchor；组件会在
	 * BeginPlay 中查找当前世界里第一个带默认 Tag 的该类实例，无需给运行时
	 * 生成的角色手动配置关卡引用。查找发生在首次捕获之前，不会每帧遍历世界。
	 * 锚点位置就是手办内部的取景中心，对应面片中心；锚点旋转决定被捕获场景
	 * 相对手办的朝向。跟随玩家相机的固定轨道模式下，捕获相机始终看向锚点。
	 * 没有找到时沿用 CapturedSceneReferenceActor/Transform。
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "场景缩略图|相机锚点")
	TObjectPtr<ADreamSceneCaptureAnchor> CameraOrbitAnchorActor;

	/**
	 * 未跟随玩家相机时，是否让固定取景的捕获相机朝向锚点位置。
	 *
	 * 跟随玩家相机时不读取此选项：固定轨道模式本身就看向锚点，
	 * 等比映射模式沿用观察相机的旋转。
	 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|相机锚点")
	bool bAimCaptureCameraAtOrbitAnchor = true;

	/** 是否让 SceneCapture 跟随第三人称相机观察手办的视角；映射方式见 bUseFixedCaptureOrbit。 */
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
	 * 出现在手中模型里的 Actor 填到这里。游戏开始时，带有普通 Actor Tag
	 * "HiddenFromCapture" 的对象也会自动追加到此数组。
	 */
	UPROPERTY(EditInstanceOnly, Category = "场景缩略图|捕获过滤")
	TArray<TObjectPtr<AActor>> ActorsToHideFromCapture;

	/**
	 * 仅在手办捕获中关闭大气天空；主视口仍正常显示天空。
	 * SkyAtmosphere 不是普通网格，不能只靠 HiddenActors 剔除。
	 * 使用网格制作的天空球/天空盒仍须加入 ActorsToHideFromCapture。
	 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获过滤")
	bool bHideAtmosphere = true;

	/** 仅在手办捕获中关闭体积云，避免云层填满本应透明的背景。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获过滤")
	bool bHideClouds = true;

	/** 仅在手办捕获中关闭高度雾/体积雾，保留模型周围的透明背景。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获过滤")
	bool bHideFog = true;

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
	 * 固定半径轨道映射：捕获相机位于锚点周围的球面上，沿“观察相机 -> 面片”的方向看向锚点。
	 *
	 * 这是跟随玩家相机的默认数学：
	 * - 观察方向：观察相机指向面片的世界方向，先变换到手办坐标系，再变换到捕获场景参考系。
	 *   外部从哪一侧、以多大俯角看手办，捕获相机就从同一侧、同一俯角看建筑；
	 * - 位置：锚点 − 观察方向 × OrbitRadius。距离固定，观察相机前后移动不改变景别；
	 * - 上方向：观察相机的上方向投影到垂直于观察方向的平面，与 FaceCamera 面片的
	 *   上方向计算方式一致，保证 RT 画面和面片的上下对齐。
	 *
	 * MiniatureFrameWorldTransform 的位置应为面片中心，旋转为手办坐标系朝向
	 * （由 ResolveDisplayPlaneWorldTransform 提供），而不是组件原点。
	 */
	UFUNCTION(BlueprintPure, Category = "场景缩略图|坐标映射")
	static FTransform MapObserverOrbitToCaptureWorld(
		const FTransform& ObserverWorldTransform,
		const FTransform& MiniatureFrameWorldTransform,
		const FTransform& CapturedSceneReferenceWorldTransform,
		float OrbitRadius);

	/**
	 * 等比映射：观察相机相对面片的完整偏移除以手办比例，还原到捕获场景，旋转沿用观察相机。
	 *
	 * 只在 bUseFixedCaptureOrbit 关闭时使用。取景距离随观察相机到面片的实际距离变化。
	 * MiniatureFrameWorldTransform 的约定与 MapObserverOrbitToCaptureWorld 相同。
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
	FTransform ResolveDisplayPlaneWorldTransform() const;
	FVector ResolveObserverLocation(const FMinimalViewInfo& POV) const;
};
