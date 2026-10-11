#pragma once
#include "GameFramework/PlayerController.h"
#include "DreamPlayerController.generated.h"
class UInputMappingContext;
class UInputAction;
class UActorComponent;
class UDreamDragInteractionComponent;
class UDreamMiniatureExtractableComponent;
class UDreamSceneCapturePresentationComponent;
class UDreamShoulderCameraComponent;
class UPrimitiveComponent;
class ADreamPasswordChest;
struct FInputActionValue;
struct FInputKeyEventArgs;

/**
 * 第三人称探索控制器：安装输入映射，管理视角、拾取、单次交互和持续拖动输入。
 * 旧的策划可配置交互框架（选择/会话/事务/撤销等）已整体移除；
 * 具体的解谜行为由目标 Actor 或其交互组件实现，控制器只管理拾取分发和输入会话。
 */
UCLASS()
class DREAMSPACE_API ADreamPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	ADreamPlayerController();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void BeginPlay() override;
	virtual void ReceivedPlayer() override;
	virtual void SetupInputComponent() override;
	virtual void UpdateRotation(float DeltaTime) override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void OnUnPossess() override;
	/** UE5.8 的原始按键入口：密码会话先消费数字/确认/取消，防止按键同时触发探索动作。 */
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	/** 由密码箱的统一交互接口开启输入；只有本地、近距离、尚未获得手办的角色可以进入。 */
	bool BeginPasswordEntry(ADreamPasswordChest* Chest);
	/** 成功、取消、箱子移除及失去 Pawn 共用同一清理入口，每次仅释放本会话添加的输入锁。 */
	void ClosePasswordEntry();
	/** HUD 与角色共用这个状态，分别控制密码面板和跳跃/视角输入。 */
	UFUNCTION(BlueprintPure, Category = "交互|密码箱")
	bool IsEnteringPassword() const { return bPasswordInputLocked; }
	ADreamPasswordChest* GetActivePasswordChest() const { return ActivePasswordChest.Get(); }
	const FString& GetEnteredPassword() const { return EnteredPassword; }
	const FString& GetPasswordEntryMessage() const { return PasswordEntryMessage; }
	/** 是否正在持续拖动机关或从手办取出模型；角色与相机用此状态保持交互期间的输入稳定。 */
	UFUNCTION(BlueprintPure, Category = "交互")
	bool IsDraggingInteraction() const;
	/** 当前是否显示鼠标、允许直接点击手办中的物体。供开发期 HUD 显示操作提示。 */
	bool IsMiniatureInteractionMode() const { return bMiniatureInteractionMode; }
	/** HUD 在屏幕上显示最近一次点击的十字和结果，避免沿视线的世界调试线缩成一个点。 */
	bool GetMiniatureClickDebug(FVector2D& OutPosition, FString& OutMessage, FLinearColor& OutColor) const;

private:
	/**
	 * 官方第三人称模板的输入映射上下文。
	 * 资源来自 Content/Input，由 C++ 构造函数加载，不需要通过控制器蓝图填写。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "输入|映射")
	TArray<TObjectPtr<UInputMappingContext>> DefaultMappingContexts;

	/** 已经加到本地玩家子系统的模板上下文，用于 EndPlay 时精确移除。 */
	UPROPERTY()
	TArray<TObjectPtr<UInputMappingContext>> AppliedDefaultMappingContexts;

	/**
	 * 项目额外交互使用的运行时映射：E、Tab、手办左键/右键和滚轮缩放。
	 * 官方模板负责移动/视角/跳跃，这个上下文只承载 DreamSpace 专属输入。
	 */
	UPROPERTY()
	TObjectPtr<UInputMappingContext> Mapping;
	/** 动态创建的交互动作集合，仅用于持有引用、防止被 GC 回收。 */
	UPROPERTY()
	TArray<TObjectPtr<UInputAction>> Actions;

	/** 按键交互的检测距离（厘米），从相机中心向前做射线检测。 */
	UPROPERTY(EditAnywhere, Category = "交互", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float InteractTraceDistance = 600.0f;
	/** 交互射线使用的碰撞通道；默认 Visibility，关卡网格需要阻挡该通道。 */
	UPROPERTY(EditAnywhere, Category = "交互")
	TEnumAsByte<ECollisionChannel> InteractTraceChannel = ECC_Visibility;
	/** 手办中的射线从远处的 SceneCapture 发出，不能复用普通 E 交互的 600 cm 距离。 */
	UPROPERTY(EditAnywhere, Category = "交互|手办", meta = (ClampMin = "1.0", UIMin = "1000.0"))
	float MiniatureInteractTraceDistance = 50000.0f;

	/** 滚轮每档改变理想距离 25 cm；越肩组件负责平滑追踪，负值可反转滚轮方向。 */
	UPROPERTY(EditAnywhere, Category = "相机")
	float CameraZoomStep = 25.0f;

	/** SpringArm 允许的最小长度（厘米），防止滚轮拉得太近。 */
	UPROPERTY(EditAnywhere, Category = "相机", meta = (ClampMin = "50", UIMin = "50"))
	float MinCameraArmLength = 150.0f;

	/** SpringArm 允许的最大长度（厘米），防止滚轮拉得太远。 */
	UPROPERTY(EditAnywhere, Category = "相机", meta = (ClampMin = "100", UIMin = "100"))
	float MaxCameraArmLength = 300.0f;

private:
	void ZoomCamera(const FInputActionValue& Value);
	/** E 按下：普通组件触发一次，自由组件进入持续拖动；松开 E 结束自由拖动。 */
	void Interact();
	void EndWorldDrag();
	void EndMiniatureDrag();
	/** 输入取消与正常松开分开处理，失焦/上下文移除不能提交取出。 */
	void CancelMiniatureDrag();
	/** Tab 切换居中观察：进入时锁住探索输入，左键操作机关，右键旋转手办展示角度。 */
	void ToggleMiniatureInteractionMode();
	/** 普通 Tab 平滑切换镜头；生命周期退出传 false，立即终止旧角色上的过渡。 */
	void SetMiniatureInteractionMode(bool bEnabled, bool bBlendCamera = true);
	/** 右键必须从可见显示面内开始；抓取后允许光标越过面片边缘，松开即停止旋转。 */
	void BeginMiniatureRotation();
	bool BeginMiniatureRotationRay(const FVector& ViewOrigin, const FVector& ViewDirection);
	/** 每帧使用光标像素位移，失焦、丢失右键、目标停用或更换 Pawn 时立即结束会话。 */
	void UpdateMiniatureRotation();
	void EndMiniatureRotation();
	/** 记录本次观察的组件，退出时清理旧角色，而不是误操作刚被 Possess 的新角色。 */
	TWeakObjectPtr<UDreamSceneCapturePresentationComponent> InspectedMiniature;
	TWeakObjectPtr<UDreamShoulderCameraComponent> MiniatureFocusCamera;
	FVector2D LastMiniatureRotationMousePosition = FVector2D::ZeroVector;
	bool bRotatingMiniature = false;
	/** Tab 会话的输入锁与左键机关拖动的输入锁独立计数，退出时各自只释放自己添加的一次。 */
	bool bMiniatureInputLocked = false;
	/** 左键触发：实际相机射线命中显示面，再映射成 SceneCapture 的世界射线。 */
	void InteractWithMiniature();
	/** 与鼠标输入解耦的完整拾取路径，自动化测试可直接提供一条已知的实际视线。 */
	void InteractWithMiniatureRay(const FVector& ViewRayOrigin, const FVector& ViewRayDirection);
	friend class FDreamMiniatureConfiguredProjectionTest;
	friend class FDreamDragControllerLifecycleTest;
	friend class FDreamMiniatureDragMappingTest;
	friend class FDreamMiniatureExtractionControllerTest;
	friend class FDreamKeyPickupControllerTest;
	friend class FDreamMiniatureFocusLifecycleTest;
	friend class FDreamMiniatureFocusTransitionTest;
	friend class FDreamMiniatureInspectionRotationTest;
	friend class FDreamMiniatureGameplayRenderTest;
	friend class FDreamRubiksControllerTest;
	friend class FDreamPasswordChestFlowTest;
	friend class FDreamPasswordChestSessionTest;
	friend class FDreamPasswordChestGameplayRenderTest;
	/** 在本地玩家已绑定后安装官方模板和项目交互的 Enhanced Input 映射。 */
	void ApplyInputMapping();
	/** 将命中的组件和所属 Actor 上的可交互组件统一分发。 */
	void DispatchInteraction(AActor* HitActor, UActorComponent* HitComponent,
		const FVector& RayOrigin, const FVector& RayDirection);

	/** 持续拖动只锁定一个组件，避免同一 Actor 上多个行为同时修改变换。 */
	void BeginActiveDrag(UDreamDragInteractionComponent* Component, const FVector& RayOrigin, const FVector& RayDirection);
	void UpdateActiveDrag();
	/** 把任意屏幕位置映射到当前输入空间，手办模式仍使用表现组件自己的投影与边界检查。 */
	bool TryGetDragRay(const FVector2D& ScreenPosition, FVector& OutOrigin, FVector& OutDirection) const;
	/** 只有正常松开左键才允许提交取出，其它生命周期清理默认取消。 */
	void EndActiveDrag(bool bTryCommitMiniatureExtract = false);
	/** 复用持续交互的移动/视角锁，但取出组件不需要枢轴，也不移动房间模型。 */
	void BeginMiniatureExtract(UDreamMiniatureExtractableComponent* Component,
		UPrimitiveComponent* HitComponent, const FVector& DisplayHitPoint,
		const FVector& DisplayFrontNormal);
	/** 屏幕输入与几何处理分离，自动化可通过已知视线覆盖真实的面外拖动路径。 */
	bool UpdateMiniatureExtractRay(const FVector& ViewOrigin, const FVector& ViewDirection);
	bool UpdateActiveMiniatureExtract();
	TWeakObjectPtr<UDreamDragInteractionComponent> ActiveDragComponent;
	TWeakObjectPtr<UDreamMiniatureExtractableComponent> ActiveMiniatureExtract;
	TWeakObjectPtr<APawn> DragPawn;
	FVector2D LastDragMousePosition = FVector2D::ZeroVector;
	bool bDragFromMiniature = false;
	/** 每次拖动仅成对增加/减少一次输入忽略计数，不覆盖其他系统已有的输入锁。 */
	bool bDragInputLocked = false;

	/** 输入会话只持弱引用，箱子卸载、重生或切换角色时不会延长旧对象的生命。 */
	TWeakObjectPtr<ADreamPasswordChest> ActivePasswordChest;
	TWeakObjectPtr<APawn> PasswordPawn;
	/** 输入缓冲是字符串，前导零不会丢失；永远只允许四位 ASCII 数字。 */
	FString EnteredPassword;
	FString PasswordEntryMessage;
	bool bPasswordInputLocked = false;
	void HandlePasswordKey(const FKey& Key);
	/** 每帧验证输入目标、距离、Pawn、ViewTarget 和视口焦点，避免不可见面板继续锁住输入。 */
	void UpdatePasswordEntry();

	/** 每条退出路径都记录诊断；仅在调试 CVar 开启时输出，不影响玩法结果。 */
	void ReportMiniatureClick(const FString& Message, const FColor& Color);
	FVector2D MiniatureDebugPosition = FVector2D::ZeroVector;
	FString MiniatureDebugMessage;
	FColor MiniatureDebugColor = FColor::White;
	double MiniatureDebugUntil = 0.0;

	bool bMappingApplied = false;
	bool bDefaultMappingsApplied = false;
	bool bMiniatureInteractionMode = false;
};
