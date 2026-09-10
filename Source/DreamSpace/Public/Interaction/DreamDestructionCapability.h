#pragma once
#include "DreamStateTransitionCapability.h"
#include "DreamDestructionCapability.generated.h"
/** 破坏专用默认配置。Broken 可切换替换 Mesh；Released 将部件变为独立运动学对象。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class DREAMSPACE_API UDreamDestructionCapability : public UDreamStateTransitionCapability
{
	GENERATED_BODY()
public:
	UDreamDestructionCapability();
};
