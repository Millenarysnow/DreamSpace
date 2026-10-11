#include "DreamPasswordChest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DreamCharacter.h"
#include "DreamPlayerController.h"
#include "DreamSceneCapturePresentationComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

/** 实际游戏视口的异步验收步骤，仅在显式运行本自动化入口时执行。 */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FDreamPasswordChestRenderStep, TFunction<bool()>, Step);
bool FDreamPasswordChestRenderStep::Update()
{
	return Step();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamPasswordChestGameplayRenderTest,
	"DreamSpace.Presentation.PasswordChest.GameplayRender",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FDreamPasswordChestGameplayRenderTest::RunTest(const FString& Parameters)
{
	ADreamPlayerController* Controller = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (Context.WorldType == EWorldType::Game && Context.World())
			Controller = Cast<ADreamPlayerController>(Context.World()->GetFirstPlayerController());
	ADreamCharacter* Character = Controller ? Cast<ADreamCharacter>(Controller->GetPawn()) : nullptr;
	if (!TestNotNull(TEXT("渲染验收存在原生玩家角色"), Character))
		return false;
	ADreamPasswordChest* Chest = nullptr;
	for (TActorIterator<ADreamPasswordChest> It(Character->GetWorld()); It; ++It)
		Chest = *It;
	if (!TestNotNull(TEXT("请在 L_PasswordChestExample 独立游戏进程运行此渲染入口"), Chest))
		return false;
	// 示例出生点保持角色胶囊直立；此验收入口单独设置稍向下的观察角，不改变正式游戏的相机默认值。
	Controller->SetControlRotation(FRotator(-25.0f, -90.0f, 0.0f));
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/PasswordChest"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	struct FPasswordChestRenderState
	{
		int32 Stage = 0;
		double Started = FPlatformTime::Seconds();
		double Since = Started;
		FDateTime StartedUTC = FDateTime::UtcNow();
		FString LastScreenshot;
	};
	const TSharedRef<FPasswordChestRenderState> State = MakeShared<FPasswordChestRenderState>();
	const TWeakObjectPtr<ADreamPlayerController> WeakController = Controller;
	const TWeakObjectPtr<ADreamCharacter> WeakCharacter = Character;
	const TWeakObjectPtr<ADreamPasswordChest> WeakChest = Chest;
	const TFunction<bool()> Step = [this, State, Directory, WeakController, WeakCharacter, WeakChest]()
	{
		ADreamPlayerController* CurrentController = WeakController.Get();
		ADreamCharacter* CurrentCharacter = WeakCharacter.Get();
		ADreamPasswordChest* CurrentChest = WeakChest.Get();
		const double Now = FPlatformTime::Seconds();
		if (!CurrentController || !CurrentCharacter || !CurrentChest || Now - State->Started > 90.0)
		{
			if (CurrentController)
			{
				CurrentController->ClosePasswordEntry();
				CurrentController->SetMiniatureInteractionMode(false, false);
			}
			AddError(TEXT("密码箱主视口验收丢失对象或等待截图超时"));
			return true;
		}
		if (Now - State->Since < 1.5)
			return false;
		if (!State->LastScreenshot.IsEmpty())
		{
			// 等待本次截图真正写入，旧运行留下的同名 PNG 不算验收成功。
			if (IFileManager::Get().GetTimeStamp(*State->LastScreenshot) < State->StartedUTC)
				return false;
			State->LastScreenshot.Reset();
		}
		auto Screenshot = [&](const TCHAR* Name)
		{
			State->LastScreenshot = Directory / Name;
			FScreenshotRequest::RequestScreenshot(State->LastScreenshot, false, false);
		};
		auto Key = [&](const FKey& Input)
		{
			FInputKeyEventArgs Args(nullptr, INPUTDEVICEID_NONE, Input, IE_Pressed, FPlatformTime::Cycles64());
			CurrentController->InputKey(Args);
		};
		switch (State->Stage)
		{
		case 0:
			TestFalse(TEXT("实际游戏出生时为空手"), CurrentCharacter->bHasMiniature || CurrentCharacter->SceneMiniature->IsPresentationActive());
			AddInfo(FString::Printf(TEXT("示例取景：角色=%s 箱子=%s 控制旋转=%s"),
				*CurrentCharacter->GetActorLocation().ToString(), *CurrentChest->GetActorLocation().ToString(),
				*CurrentController->GetControlRotation().ToString()));
			Screenshot(TEXT("Locked.png"));
			break;
		case 1:
			// 离屏渲染仍有 Slate 游戏视口，显式聚焦以通过与真实玩家相同的失焦保护。
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
			CurrentController->Interact();
			if (!TestTrue(TEXT("示例出生视角的真实 E 射线打开密码面板"), CurrentController->IsEnteringPassword()))
				return true;
			Key(EKeys::One);
			Key(EKeys::Two);
			Key(EKeys::Three);
			Key(EKeys::Four);
			TestEqual(TEXT("实际游戏键盘输入填满四格"), CurrentController->GetEnteredPassword(), FString(TEXT("1234")));
			Screenshot(TEXT("PasswordEntry.png"));
			break;
		case 2:
			Key(EKeys::Enter);
			if (!TestEqual(TEXT("提交密码后开始开盖"), CurrentChest->GetChestState(), EDreamPasswordChestState::Opening))
				return true;
			// E 交互会短暂绘制调试射线，截图只清除本次验收进程的调试线，避免遮住开盖和光点。
			if (ULineBatchComponent* Lines = CurrentCharacter->GetWorld()->GetLineBatcher(UWorld::ELineBatcherType::World))
				Lines->Flush();
			if (ULineBatchComponent* Lines = CurrentCharacter->GetWorld()->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent))
				Lines->Flush();
			// 固定动画采样点后等待 GPU，保证截图能稳定查看开盖中途与箱内光点。
			// 正式游戏始终由 Actor Tick 推进，这里只在本次验收进程手动驱动相同的 Tick 函数。
			CurrentChest->SetActorTickEnabled(false);
			CurrentChest->Tick(CurrentChest->OpeningDuration * 0.5f);
			Screenshot(TEXT("OpeningGlow.png"));
			break;
		case 3:
			CurrentChest->Tick(CurrentChest->OpeningDuration * 0.5f);
			AddInfo(FString::Printf(TEXT("箱内光点：位置=%s 缩放=%s 隐藏=%d 可见=%d"),
				*CurrentChest->GlowMesh->GetComponentLocation().ToString(), *CurrentChest->GlowMesh->GetComponentScale().ToString(),
				CurrentChest->GlowMesh->bHiddenInGame, CurrentChest->GlowMesh->IsVisible()));
			Screenshot(TEXT("OpenGlow.png"));
			break;
		case 4:
			CurrentChest->Tick(CurrentChest->PickupDelay + CurrentChest->AttractionDuration * 0.45f);
			// StartAttraction 会按正常游戏逻辑重新启用 Tick，截图等待期间再次冻结采样点。
			CurrentChest->SetActorTickEnabled(false);
			TestFalse(TEXT("吸取中途仍没有手办"), CurrentCharacter->bHasMiniature);
			AddInfo(FString::Printf(TEXT("吸取光点：位置=%s 缩放=%s 隐藏=%d 可见=%d"),
				*CurrentChest->GlowMesh->GetComponentLocation().ToString(), *CurrentChest->GlowMesh->GetComponentScale().ToString(),
				CurrentChest->GlowMesh->bHiddenInGame, CurrentChest->GlowMesh->IsVisible()));
			Screenshot(TEXT("Attracting.png"));
			break;
		case 5:
			CurrentChest->Tick(CurrentChest->AttractionDuration);
			TestTrue(TEXT("实际领取启用玩家的手办显示"), CurrentCharacter->bHasMiniature && CurrentCharacter->SceneMiniature->IsPresentationActive());
			Screenshot(TEXT("Collected.png"));
			break;
		case 6:
			CurrentController->SetMiniatureInteractionMode(true);
			TestTrue(TEXT("实际解锁后可进入 Tab 观察"), CurrentController->IsMiniatureInteractionMode());
			break;
		case 7:
			Screenshot(TEXT("Inspection.png"));
			break;
		case 8:
			CurrentController->SetMiniatureInteractionMode(false);
			AddInfo(FString::Printf(TEXT("密码箱主视口截图已保存：%s"), *Directory));
			return true;
		}
		++State->Stage;
		State->Since = Now;
		return false;
	};
	ADD_LATENT_AUTOMATION_COMMAND(FDreamPasswordChestRenderStep(Step));
	return true;
}
#endif
