#pragma once
#include "GameFramework/HUD.h"
#include "DreamHUD.generated.h"
/** 开发期 HUD：只画基础操作提示和准星，不保存任何玩法状态。 */
UCLASS()
class DREAMSPACE_API ADreamHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;
};
