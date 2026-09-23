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

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void StartJump();
	void EndJump();
	/** E 键触发：对视线命中的 Actor 调用其身上所有可交互组件。 */
	void Interact();
	/** 在本地玩家已绑定后安装运行时创建的 Enhanced Input 映射。 */
	void ApplyInputMapping();
	/** 将命中的组件和所属 Actor 上的可交互组件统一分发。 */
	void DispatchInteraction(AActor* HitActor, UActorComponent* HitComponent);

	bool bMappingApplied = false;
};
