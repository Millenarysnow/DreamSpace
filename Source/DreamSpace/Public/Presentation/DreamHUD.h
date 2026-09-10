#pragma once
#include "GameFramework/HUD.h"
#include "DreamHUD.generated.h"
/** 开发验收用 HUD；只读取模式、选择和操作反馈，不保存玩法状态。 */
UCLASS()
class DREAMSPACE_API ADreamHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;
};
