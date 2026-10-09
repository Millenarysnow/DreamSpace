#include "DreamWhitePreviewGameMode.h"

#include "Camera/CameraActor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

ADreamWhitePreviewGameMode::ADreamWhitePreviewGameMode()
{
	// 预览关卡没有可操控角色，也不需要第三人称模板的旁观者或项目调试 HUD。
	// 使用引擎原生控制器，防止解谜控制器的 BeginPlay 初始化手办、输入或交互界面。
	DefaultPawnClass = nullptr;
	SpectatorClass = nullptr;
	HUDClass = nullptr;
	PlayerControllerClass = APlayerController::StaticClass();
}

void ADreamWhitePreviewGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	if (!NewPlayer)
	{
		return;
	}

	// 生成脚本为主相机写入此标签；按标签选择而不是依赖 Actor 名称，
	// 所以后续在编辑器里重命名相机不会破坏“运行”时的预览。
	static const FName PreviewCameraTag(TEXT("DreamWhitePreviewCamera"));
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(PreviewCameraTag))
		{
			NewPlayer->SetViewTarget(*It);
			return;
		}
	}
}
