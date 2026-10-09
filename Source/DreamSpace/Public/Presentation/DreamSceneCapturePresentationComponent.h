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

	/**
	 * 窗口模式下手动指定的水平视场角。
	 *
	 * 只在 bMatchCaptureFOVToDisplay 关闭、或未跟随玩家相机时使用。窗口模式下默认
	 * 由面片的张角自动推出视场角，手填一个值会让画面相对面片缩放，破坏窗口对应关系。
	 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获", meta = (ClampMin = "5", ClampMax = "170"))
	float CaptureFOV = 60.0f;

	/**
	 * 是否让捕获视场角等于面片在观察者眼中的张角；默认开启，这是严格窗口模式。
	 *
	 * 面片宽度为 W、眼睛到面片距离为 d 时，视场角取 2·atan((W/2) / d)。
	 * 只有这样，顺着面片看过去的那一束角度才与 RT 的整幅画面一一对应：
	 * 面片边缘对应画面边缘，画面上下的方向也与面片一致。
	 * 手填固定角度会让画面相对面片放大或缩小，视差就不再是窗口的视差。
	 * 关闭后使用 CaptureFOV。
	 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获")
	bool bMatchCaptureFOVToDisplay = true;

	/** 自动取景时在包围球外额外留出的比例。 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获", meta = (ClampMin = "1", UIMin = "1"))
	float AutoFramePadding = 1.25f;

	/**
	 * 未跟随玩家相机时，固定取景相机的取景距离。
	 *
	 * 跟随玩家相机时不再使用：那时捕获相机到锚点的距离由观察者到面片的实际距离
	 * 按 MiniatureSceneScale 还原得到，也就是窗口模式的取景距离。
	 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|捕获", meta = (ClampMin = "1", UIMin = "1"))
	float CaptureDistance = 2400.0f;

	/**
	 * 手办的场景缩放，也决定“窗口里能装下多大的真实场景”；默认 0.03。
	 *
	 * 这是窗口模式唯一的景别旋钮：真实场景中 1/s 单位的内容会被装进 1 单位的
	 * 面片里，因此窗口覆盖的真实范围 ≈ DisplayWorldSize / MiniatureSceneScale。
	 * 例如面片 80、比例 0.03 时，窗口大约覆盖 80/0.03 ≈ 2667 cm（约 27 m）的建筑。
	 * 比例越小，装下的建筑越大、手办显得越“深”；比例越大，越像是贴着建筑看。
	 *
	 * 注意视场角不随本值变化：它只由面片张角决定，因此调本值相当于换镜头焦距，
	 * 而不是改取景距离。
	 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|坐标映射", meta = (ClampMin = "0.0001", UIMin = "0.0001"))
	float MiniatureSceneScale = 0.03f;

	/**
	 * 是否忽略 SpringArm 的碰撞修正，用”未被障碍推近的理想镜头位置”观察手办。
	 *
	 * 窗口模式的取景方向与取景距离都来自观察位置，而观察位置取 SpringArm 按
	 * TargetArmLength 算出、未经碰撞缩短的值，因此镜头贴墙被挤近时手办画面不变。
	 * 面片朝向也取自这条视线，不会跟着抖动。
	 * 观察目标上找不到 SpringArm 时自动使用真实相机位置。
	 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|坐标映射")
	bool bIgnoreSpringArmCollision = true;

	/**
	 * 是否让手办视角跟随玩家相机的臂长调整（滚轮缩放）；默认关闭。
	 *
	 * 关闭时，手办用固定的 ObserverArmLength 计算观察位置，玩家用滚轮拉近拉远
	 * 第三人称相机时手办画面保持不变。开启时手办视角跟随实际臂长变化。
	 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|坐标映射")
	bool bFollowCameraZoom = false;

	/**
	 * 手办使用的固定观察臂长（厘米），只在 bFollowCameraZoom 关闭时使用。
	 *
	 * 它决定”观察者站在离枢轴多远的地方透过手办看建筑”。默认 420，与
	 * DreamCharacter 的初始 SpringArm 长度一致。玩家用滚轮改实际相机臂长时，
	 * 手办仍使用这个固定值，因此画面不会跟着推近。
	 * 如果想调整手办的取景角度（比如建筑偏离画面中心），改这个值而不是实际臂长。
	 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|坐标映射",
		meta = (ClampMin = "50", UIMin = "50", EditCondition = "!bFollowCameraZoom", EditConditionHides))
	float ObserverArmLength = 420.0f;

	/**
	 * 捕获场景的参考坐标系到真实世界的变换。
	 *
	 * SceneCapture 最终需要一个真实世界 Transform。这里只使用它的**位置**作为
	 * 被捕获场景的原点；旋转不参与计算。手办组件与锚点的旋转都属于显示层或取景
	 * 参考系，一旦乘进捕获方向，方位角会整体翻转，捕获相机就跑到玩家相机的另一侧。
	 * 默认值为世界原点。
	 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|坐标映射")
	FTransform CapturedSceneReferenceTransform = FTransform::Identity;

	/** 可选的参考 Actor；设置后使用它的世界位置作为捕获场景原点，同样不取旋转。 */
	UPROPERTY(EditInstanceOnly, Category = "场景缩略图|坐标映射")
	TObjectPtr<AActor> CapturedSceneReferenceActor;

	/**
	 * 游戏开始时自动绑定的相机轨道锚点，只读显示绑定结果。
	 *
	 * 在“被捕获的真实场景”中放置一个 DreamSceneCaptureAnchor；组件会在
	 * BeginPlay 中查找当前世界里第一个带默认 Tag 的该类实例，无需给运行时
	 * 生成的角色手动配置关卡引用。查找发生在首次捕获之前，不会每帧遍历世界。
	 * 锚点位置就是手办内部的取景中心，对应面片中心：捕获相机绕它取景。
	 * 锚点旋转不参与映射，捕获相机始终在玩家相机所在的同一侧。
	 * 没有找到时沿用 CapturedSceneReferenceActor/Transform 的位置。
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "场景缩略图|相机锚点")
	TObjectPtr<ADreamSceneCaptureAnchor> CameraOrbitAnchorActor;

	/**
	 * 未跟随玩家相机时，是否让固定取景的捕获相机朝向锚点位置。
	 *
	 * 跟随玩家相机时不读取此选项：窗口映射的捕获相机本身就看向锚点。
	 */
	UPROPERTY(EditAnywhere, Category = "场景缩略图|相机锚点")
	bool bAimCaptureCameraAtOrbitAnchor = true;

	/** 是否让 SceneCapture 跟随第三人称相机观察手办的视角；映射方式见 MapObserverWindowToCaptureWorld。 */
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

	/**
	 * 面片是否跟随第三人称观察相机转向；它只改变显示面，不改变 SceneCapture 坐标映射。
	 *
	 * 跟随玩家相机的固定轨道模式下，FaceCamera 的面片法线与捕获视线取自同一方向
	 * （默认是玩家相机前方向的反方向），保证画面与面片不会互相错开。
	 */
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
	 * 严格窗口映射：把观察者透过面片看到的画面，还原成被捕获真实场景里的一台相机。
	 *
	 * 面片中心对应锚点，面片本身是被缩放后的成像平面。要让 RT 的四角正好落在面片的
	 * 四角上（这是“透过窗口看”成立的前提），三条必须同时满足：
	 * - 观察方向 dir = normalize(面片中心 − 观察者位置)。相机光轴穿过面片中心，
	 *   因此锚点落在画面正中。注意它不是观察者前方向——第三人称相机的前方向穿过角色，
	 *   并不穿过面片；
	 * - 取景距离 = 观察者到面片的距离 / MiniatureSceneScale。捕获相机位于
	 *   锚点 − dir × 取景距离，与观察者相对面片中心的偏移方向相同；
	 * - 视场角 = 面片在观察者眼中的张角，见 ComputeWindowFieldOfView。
	 *
	 * 上方向取观察者的上方向并正交化到垂直于 dir，与 FaceCamera 面片的竖直方向一致。
	 * MiniatureFrameWorldTransform 的位置应为面片中心（由 ResolveDisplayPlaneWorldTransform
	 * 提供）；旋转不参与计算。
	 */
	UFUNCTION(BlueprintPure, Category = "场景缩略图|坐标映射")
	static FTransform MapObserverWindowToCaptureWorld(
		const FTransform& ObserverWorldTransform,
		const FTransform& MiniatureFrameWorldTransform,
		const FTransform& CapturedSceneReferenceWorldTransform,
		float InMiniatureSceneScale);

	/**
	 * 严格窗口对应的水平视场角：面片在观察者眼中的张角。
	 *
	 * 面片宽度 W、眼睛到面片距离 d 时为 2·atan((W/2) / d)。取这个值，RT 的整幅画面
	 * 才恰好覆盖“顺着面片看过去”的那一束角度，面片边缘对应画面边缘。
	 * 距离过小时被限制在 ToolMaxFOV，避免取景器崩成极广角。
	 */
	UFUNCTION(BlueprintPure, Category = "场景缩略图|坐标映射")
	static float ComputeWindowFieldOfView(float DisplayWorldWidth, float ObserverDistance);

	/** 返回当前输出纹理，供后续门户材质或其他表现组件复用。 */
	UFUNCTION(BlueprintPure, Category = "场景缩略图")
	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }

	/** 返回当前是否正在输出场景缩略图。 */
	UFUNCTION(BlueprintPure, Category = "场景缩略图")
	bool IsPresentationActive() const { return bPresentationActive; }

	/**
	 * 将实际玩家相机射线映射到 SceneCapture 的真实世界射线，并给出可读失败原因。
	 * 先求显示面 UV，再按当前捕获投影反算方向；面片与 RT 宽高比无需一致。
	 * 支持透视投影、正缩放的薄矩形平面，以及默认 Plane 的 UV0/直接采样材质。
	 * 本函数仅计算映射；近处遮挡、世界碰撞与交互分发由控制器负责。
	 */
	bool TryMapViewRayToCaptureRay(
		const FVector& ViewRayOrigin, const FVector& ViewRayDirection,
		FVector& OutDisplayHitPoint, FVector& OutCaptureRayOrigin,
		FVector& OutCaptureRayDirection, FString& OutFailureReason) const;

	/**
	 * 与显示面的无限延伸平面求交，返回未截断的 UV、世界交点和朝向玩家的法线。
	 * 取出交互在鼠标越过显示面边缘后仍需要连续跟随，所以不能使用捕获射线的矩形边界限制。
	 * 本入口只提供显示几何；是否提交取出、如何生成掉落物仍由玩法组件决定。
	 */
	bool TryMapViewRayToDisplayPlane(
		const FVector& ViewRayOrigin, const FVector& ViewRayDirection,
		FVector& OutDisplayHitPoint, FVector2D& OutUnclampedUV,
		FVector& OutDisplayFrontNormal, FString& OutFailureReason) const;

	/** 返回 SceneCapture 不渲染的 Actor；拾取射线也跳过它们及其 ChildActor。 */
	void GetCaptureHiddenActors(TArray<AActor*>& OutActors) const;

	/**
	 * 返回手办取景实际使用的观察姿态，供取景、面片和调试共用。
	 * 忽略碰撞时，越肩相机提供无碰撞的枢轴/旋转/完整肩位；普通 SpringArm 使用原生偏移约定。
	 * 未忽略碰撞或找不到摇臂时使用传入的真实视口姿态。此接口不创建资源、不改变玩法状态。
	 */
	FTransform GetObserverTransform(const FMinimalViewInfo& PlayerPOV) const;

	/**
	 * 无碰撞平面的解析求交：输出默认 Plane UV0 布局下的纹理坐标。
	 * 纯几何函数便于覆盖越界、背面、图像旋转、缩放和实际相机偏移的回归测试。
	 * 180° 修正由显示面的真实变换体现，调用者不可再次翻转 UV。
	 * bRequireInsideDisplay 默认为 true；取出交互传 false 后保留超出 0..1 的 UV，
	 * 但仍检查正面、有限射线及正缩放，不把背面或非法射线当成成功拖出。
	 */
	static bool MapViewRayToDisplayUV(
		const FVector& ViewRayOrigin, const FVector& ViewRayDirection,
		const FTransform& DisplayWorldTransform, const FBox& DisplayLocalBounds,
		FVector& OutDisplayHitPoint, FVector2D& OutUV, FString& OutFailureReason,
		bool bRequireInsideDisplay = true);

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
	/** dream.DebugSceneCapture 1 时每帧绘制捕获相机、锚点与面片方向，只用于调试。 */
	void DrawCaptureDebug() const;
};
