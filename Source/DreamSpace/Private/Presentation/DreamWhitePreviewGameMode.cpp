#include "DreamWhitePreviewGameMode.h"

#include "Camera/CameraActor.h"
#include "EngineUtils.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

ADreamWhitePreviewGameMode::ADreamWhitePreviewGameMode()
{
	// 预览关卡没有可操控角色，也不需要第三人称模板的旁观者或项目调试 HUD。
	// 使用引擎原生控制器，防止解谜控制器的 BeginPlay 初始化手办、输入或交互界面。
	DefaultPawnClass = nullptr;
	SpectatorClass = nullptr;
	HUDClass = nullptr;
	PlayerControllerClass = APlayerController::StaticClass();
}

void ADreamWhitePreviewGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 色调映射之后生成的线条不会再经过 TSR。TSR 的 GBuffer 采样抖动会
	// 使同一条窗框逐帧被识别成不同的黑线，因此线稿展示临时使用 FXAA。
	// 按后处理体积标签判断而非关卡名；另一套白盒预览没有此标签。
	static const FName HatchingPreviewTag(TEXT("DreamWhiteHatchingPreview"));
	for (TActorIterator<APostProcessVolume> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(HatchingPreviewTag))
		{
			if (IConsoleVariable* AntiAliasing = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod")))
			{
				PreviousAntiAliasingMethod = AntiAliasing->GetInt();
				bRestoreAntiAliasingMethod = PreviousAntiAliasingMethod != 1;
				// 使用当前优先级替换值，保留原有配置来源；不写项目配置文件。
				AntiAliasing->SetWithCurrentPriority(1);
			}
			break;
		}
	}
}

void ADreamWhitePreviewGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bRestoreAntiAliasingMethod)
	{
		if (IConsoleVariable* AntiAliasing = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod")))
		{
			// 如果用户在展示期间主动改成其他模式，保留用户的新选择。
			// 只有仍是本规则设置的 FXAA 时，才恢复进入展示前的值。
			if (AntiAliasing->GetInt() == 1)
			{
				AntiAliasing->SetWithCurrentPriority(PreviousAntiAliasingMethod);
			}
		}
		bRestoreAntiAliasingMethod = false;
	}
	Super::EndPlay(EndPlayReason);
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
