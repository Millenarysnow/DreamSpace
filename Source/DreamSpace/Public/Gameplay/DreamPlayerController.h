#pragma once
#include "GameFramework/PlayerController.h"
#include "DreamPlayerController.generated.h"
class UInputMappingContext;
class UInputAction;
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
	virtual void SetupInputComponent() override;
	virtual void UpdateRotation(float DeltaTime) override;

private:
	/** 运行时动态创建的输入映射上下文，不依赖任何内容侧资产配置。 */
	UPROPERTY()
	TObjectPtr<UInputMappingContext> Mapping;
	/** 动态创建的输入动作集合，仅用于持有引用、防止被 GC 回收。 */
	UPROPERTY()
	TArray<TObjectPtr<UInputAction>> Actions;

	/** 按键交互的检测距离（厘米），从相机中心向前做射线检测。 */
	UPROPERTY(EditAnywhere, Category = "交互")
	float InteractTraceDistance = 600.0f;

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void StartJump();
	void EndJump();
	/** E 键触发：对视线命中的 Actor 调用其身上所有可交互组件。 */
	void Interact();
};
