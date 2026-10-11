#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DreamInteractableInterface.generated.h"

/**
 * 可交互组件接口（新组件式交互框架的统一入口）。
 *
 * 挂在 Actor 上、希望响应玩家“交互”按键的组件实现这个接口。
 * 密码箱等完整机关也可由 Actor 自身实现；控制器优先交给 Actor，避免重复触发它的子组件。
 * 玩家控制器只做视线检测与触发调用，完全不关心组件的具体行为，
 * 转动、推拉等解谜逻辑可继续以组件形式插拔到任意 Actor，完整密码箱则由 Actor 实现。
 */
UINTERFACE(BlueprintType, Category = "交互")
class DREAMSPACE_API UDreamInteractableInterface : public UInterface
{
	GENERATED_BODY()
};

class DREAMSPACE_API IDreamInteractableInterface
{
	GENERATED_BODY()

public:
	/**
	 * 玩家对目标 Actor 或其组件发起交互时调用。
	 * @param Interactor 发起交互的 Pawn（通常是玩家角色），目标可用它做距离/朝向等判断。
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "交互")
	void OnInteracted(AActor* Interactor);
};
