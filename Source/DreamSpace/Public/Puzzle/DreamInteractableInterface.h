#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DreamInteractableInterface.generated.h"

/**
 * 可交互组件接口（新组件式交互框架的统一入口）。
 *
 * 任何挂在 Actor 上、希望响应玩家“交互”按键的组件都实现这个接口。
 * 玩家控制器只做视线检测与触发调用，完全不关心组件的具体行为，
 * 因此后续所有解谜逻辑（转动、推拉、开关等）都以组件形式插拔到任意 Actor 上。
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
	 * 玩家对组件所属 Actor 发起交互时调用。
	 * @param Interactor 发起交互的 Pawn（通常是玩家角色），组件可用它做距离/朝向等判断。
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "交互")
	void OnInteracted(AActor* Interactor);
};
