#pragma once
#include "GameFramework/GameModeBase.h"
#include "MainGameMode.generated.h"
/** 游戏规则入口只选择原生默认类；建筑逻辑属于交互运行时。 */
UCLASS()
class DREAMSPACE_API AMainGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AMainGameMode();
};
