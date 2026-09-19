#pragma once

#include "GameFramework/Actor.h"
#include "DreamSceneCaptureAnchor.generated.h"

/**
 * 放在关卡中的手办相机锚点，只提供位置和朝向，不参与任何玩法逻辑。
 *
 * 在希望成为手办中心的位置放置一个实例即可。运行时生成的角色会由自己的
 * 场景缩略图表现组件自动查找并绑定它，不需要在角色或角色蓝图里引用关卡对象。
 * 位置决定捕获取景中心，旋转决定场景参考坐标系，缩放不参与相机映射。
 * Actor 默认隐藏、无碰撞、无 Tick，也不会生成可渲染的模型。
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (DreamPresentation))
class DREAMSPACE_API ADreamSceneCaptureAnchor : public AActor
{
	GENERATED_BODY()

public:
	ADreamSceneCaptureAnchor();

	/** 普通 Actor Tag（不是 GameplayTag）；构造时自动添加，供表现组件统一查找。 */
	static const FName AnchorTag;
};
