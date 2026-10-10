#pragma once

#include "DreamWhitePreviewGameMode.h"
#include "DreamMainMenuGameMode.generated.h"

/** 菜单继承线稿预览的 FXAA 管理，但选择独立菜单控制器和展示相机，不生成 Pawn/HUD。 */
UCLASS()
class DREAMSPACE_API ADreamMainMenuGameMode : public ADreamWhitePreviewGameMode
{
	GENERATED_BODY()
public:
	ADreamMainMenuGameMode();
protected:
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
};
