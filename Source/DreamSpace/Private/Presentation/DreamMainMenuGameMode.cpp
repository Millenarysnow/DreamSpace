#include "DreamMainMenuGameMode.h"

#include "DreamMainMenuPlayerController.h"
#include "DreamMainMenuScene.h"
#include "EngineUtils.h"

ADreamMainMenuGameMode::ADreamMainMenuGameMode()
{
	PlayerControllerClass = ADreamMainMenuPlayerController::StaticClass();
}

void ADreamMainMenuGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	if (!NewPlayer)
		return;
	// 不调用生成角色的基类路径。菜单相机属于展示 Actor，能随窗口比例自动调整。
	for (TActorIterator<ADreamMainMenuScene> It(GetWorld()); It; ++It)
	{
		NewPlayer->SetViewTarget(*It);
		return;
	}
}
