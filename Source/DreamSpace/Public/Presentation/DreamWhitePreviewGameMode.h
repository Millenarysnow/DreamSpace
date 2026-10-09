#pragma once

#include "GameFramework/GameModeBase.h"
#include "DreamWhitePreviewGameMode.generated.h"

/**
 * 白色建筑效果预览专用的游戏规则。
 *
 * 此类只由独立的 L_WhiteOutlinePreview 关卡引用：不生成角色和调试 HUD，
 * 而是让玩家控制器直接观看关卡中的固定相机。这样在编辑器中点击“运行”后，
 * 看到的仍然是建筑效果本身，方便后续把这个场景接入真正的开始界面。
 * 当前阶段不处理开始按钮、输入、建筑旋转或任何解谜规则。
 */
UCLASS()
class DREAMSPACE_API ADreamWhitePreviewGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADreamWhitePreviewGameMode();

protected:
	/** 玩家进入时直接选择预览相机，避免父类尝试生成默认角色并改变取景。 */
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
};
