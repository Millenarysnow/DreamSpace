#pragma once
#include "GameFramework/PlayerController.h"
#include "DreamPlayerController.generated.h"
class UInputMappingContext;
class UInputAction;
class UActorComponent;
struct FInputActionValue;

/**
 * 第三人称探索控制器：只负责移动、视角、跳跃和交互触发。
 * 旧的策划可配置交互框架（选择/会话/事务/撤销等）已整体移除；
 * 具体的解谜行为全部由挂在 Actor 上的组件实现，控制器不感知细节。
 */
UCLASS()
class DREAMSPACE_API ADreamPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void BeginPlay() override;
	virtual void ReceivedPlayer() override;
	virtual void SetupInputComponent() override;
	virtual void UpdateRotation(float DeltaTime) override;
	/** 当前是否显示鼠标、允许直接点击手办中的物体。供开发期 HUD 显示操作提示。 */
	bool IsMiniatureInteractionMode() const { return bMiniatureInteractionMode; }

private:
	/** 运行时动态创建的输入映射上下文，不依赖任何内容侧资产配置。 */
	UPROPERTY()
	TObjectPtr<UInputMappingContext> Mapping;
	/** 动态创建的输入动作集合，仅用于持有引用、防止被 GC 回收。 */
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

	/** 滚轮每一档改变 SpringArm 长度的量（厘米）；负值反转滚轮方向。 */
	UPROPERTY(EditAnywhere, Category = "相机")
	float CameraZoomStep = 50.0f;

	/** SpringArm 允许的最小长度（厘米），防止滚轮拉得太近。 */
	UPROPERTY(EditAnywhere, Category = "相机", meta = (ClampMin = "50", UIMin = "50"))
	float MinCameraArmLength = 150.0f;

	/** SpringArm 允许的最大长度（厘米），防止滚轮拉得太远。 */
	UPROPERTY(EditAnywhere, Category = "相机", meta = (ClampMin = "100", UIMin = "100"))
	float MaxCameraArmLength = 800.0f;

private:
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void ZoomCamera(const FInputActionValue& Value);
	void StartJump();
	void EndJump();
	/** E 键触发：对视线命中的 Actor 调用其身上所有可交互组件。 */
	void Interact();
	/** Tab 切换光标模式：进入时暂停鼠标转视角，左键改为点击手办画面。 */
	void ToggleMiniatureInteractionMode();
	void SetMiniatureInteractionMode(bool bEnabled);
	/** 左键触发：实际相机射线命中显示面，再映射成 SceneCapture 的世界射线。 */
	void InteractWithMiniature();
	/** 在本地玩家已绑定后安装运行时创建的 Enhanced Input 映射。 */
	void ApplyInputMapping();
	/** 将命中的组件和所属 Actor 上的可交互组件统一分发。 */
	void DispatchInteraction(AActor* HitActor, UActorComponent* HitComponent);

	bool bMappingApplied = false;
	bool bMiniatureInteractionMode = false;
};
