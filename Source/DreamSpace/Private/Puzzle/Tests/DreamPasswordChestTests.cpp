#include "DreamPasswordChest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "DreamCharacter.h"
#include "DreamHUD.h"
#include "DreamPlayerController.h"
#include "DreamSceneCaptureAnchor.h"
#include "DreamSceneCapturePresentationComponent.h"
#include "DreamShoulderCameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/World.h"
#include "EnhancedPlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"

namespace
{
	/** 真实角色、原生箱子和 HUD 组成瞬时测试世界；不打开、保存或更改任何用户关卡。 */
	struct FPasswordChestFixture
	{
		// Actor 的接口事件经过 ProcessEvent，未初始化的瞬时世界默认禁止执行。
		// 用作用域守卫仅允许本夹具执行事件，析构后恢复全局设置，正式游戏路径保持原样。
		FEditorScriptExecutionGuard ScriptExecutionGuard;
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		ADreamCharacter* Character = nullptr;
		ADreamPlayerController* Controller = nullptr;
		ADreamPasswordChest* Chest = nullptr;
		ADreamHUD* HUD = nullptr;

		FPasswordChestFixture()
		{
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			Character = SpawnCharacter();
			Controller = World->SpawnActor<ADreamPlayerController>();
			Controller->Player = NewObject<ULocalPlayer>(GEngine);
			Controller->PlayerInput = NewObject<UEnhancedPlayerInput>(Controller);
			World->AddController(Controller);
			Controller->SpawnPlayerCameraManager();
			Controller->Possess(Character);
			Controller->SetViewTarget(Character);
			Controller->ClientSetHUD_Implementation(ADreamHUD::StaticClass());
			HUD = CastChecked<ADreamHUD>(Controller->GetHUD());
			World->SpawnActor<ADreamSceneCaptureAnchor>();
			PreparePresentation(Character);
			Chest = World->SpawnActor<ADreamPasswordChest>(FVector(230, 0, 0), FRotator::ZeroRotator);
			// 正常世界会自动分发 BeginPlay；瞬时世界显式执行，记录铰链/光点的真实初始变换。
			Chest->DispatchBeginPlay();
		}

		ADreamCharacter* SpawnCharacter()
		{
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<ADreamCharacter>(FVector(0, 0, 100), FRotator::ZeroRotator, Spawn);
		}

		void PreparePresentation(ADreamCharacter* Pawn)
		{
			UDreamShoulderCameraComponent* Camera = CastChecked<UDreamShoulderCameraComponent>(Pawn->CameraBoom);
			Camera->Activate(true);
			Camera->TickComponent(0.0f, LEVELTICK_All, nullptr);
			Pawn->SceneMiniature->RenderTargetWidth = 128;
			Pawn->SceneMiniature->RenderTargetHeight = 128;
			Pawn->SceneMiniature->BeginPlay();
		}

		/** 注入确定的相机缓存；调用 Interact 时仍走真实碰撞射线，而非绕过拾取分发。 */
		void AimAtChest()
		{
			FMinimalViewInfo View;
			View.Location = Chest->GetActorLocation() + FVector(-150, 0, 29);
			View.Rotation = FRotator::ZeroRotator;
			World->TimeSeconds = 1.0;
			Controller->PlayerCameraManager->UpdateCamera(0.0f);
			Controller->PlayerCameraManager->SetCameraCachePOV(View);
		}

		/** 从 UE5.8 的实际原始按键入口注入，覆盖数字识别、重复事件和模态输入路由。 */
		void Key(const FKey& Key, EInputEvent Event = IE_Pressed)
		{
			FInputKeyEventArgs Args(nullptr, INPUTDEVICEID_NONE, Key, Event, FPlatformTime::Cycles64());
			Controller->InputKey(Args);
		}

		~FPasswordChestFixture()
		{
			Controller->UnPossess();
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamPasswordChestFlowTest,
	"DreamSpace.Puzzle.PasswordChest.WorldEPasswordOpeningAndAutoPickup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPasswordChestFlowTest::RunTest(const FString& Parameters)
{
	FPasswordChestFixture Scene;
	TestFalse(TEXT("开局没有手办，显示和捕获均未启用"), Scene.Character->bHasMiniature || Scene.Character->SceneMiniature->IsPresentationActive());
	TInlineComponentArray<UStaticMeshComponent*> Meshes(Scene.Character);
	for (UStaticMeshComponent* Mesh : Meshes)
		if (Mesh->GetFName() == TEXT("SceneCaptureDisplayMesh"))
			TestTrue(TEXT("开局实际显示网格被隐藏"), Mesh->bHiddenInGame && !Mesh->IsVisible());
	for (AActor* Actor : Scene.World->PersistentLevel->Actors)
		if (ASceneCapture2D* Capture = Cast<ASceneCapture2D>(Actor))
			TestFalse(TEXT("开局没有每帧场景捕获"), Capture->GetCaptureComponent2D()->bCaptureEveryFrame);
	Scene.Controller->ToggleMiniatureInteractionMode();
	TestFalse(TEXT("未获得手办时 Tab 不建立观察会话或输入锁"), Scene.Controller->IsMiniatureInteractionMode() ||
		Scene.Controller->IsMoveInputIgnored() || Scene.Controller->IsLookInputIgnored() || Scene.Controller->bShowMouseCursor);
	// 仅启用表现不应绕过角色持有条件，防止蓝图误开显示后提前解锁手办交互。
	Scene.Character->SceneMiniature->SetPresentationEnabled(true);
	Scene.Controller->ToggleMiniatureInteractionMode();
	TestFalse(TEXT("外部打开显示也不能提前进入 Tab"), Scene.Controller->IsMiniatureInteractionMode());
	Scene.Character->SceneMiniature->SetPresentationEnabled(false);

	Scene.AimAtChest();
	AActor* Wall = Scene.World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box);
	Wall->AddInstanceComponent(Box);
	Box->SetBoxExtent(FVector(10));
	Box->SetCollisionProfileName(TEXT("BlockAll"));
	Box->RegisterComponent();
	Wall->SetActorLocation(Scene.Chest->GetActorLocation() + FVector(-90, 0, 29));
	AddExpectedMessagePlain(TEXT("没有实现 IDreamInteractableInterface"), ELogVerbosity::Warning);
	Scene.Controller->Interact();
	TestFalse(TEXT("E 射线被墙遮挡时不打开密码面板"), Scene.Controller->IsEnteringPassword());
	Wall->Destroy();
	Scene.Controller->Interact();
	TestTrue(TEXT("真实世界 E 命中原生 Actor 后打开输入"), Scene.Controller->IsEnteringPassword());
	TestTrue(TEXT("输入期间锁住移动和视角"), Scene.Controller->IsMoveInputIgnored() && Scene.Controller->IsLookInputIgnored());
	Scene.Character->DoJumpStart();
	TestFalse(TEXT("密码输入期间跳跃入口被阻止"), Scene.Character->bPressedJump);
	Scene.Controller->SetMiniatureInteractionMode(true);
	TestFalse(TEXT("输入期间不允许切换手办模式"), Scene.Controller->IsMiniatureInteractionMode());
	Scene.Key(EKeys::One);
	Scene.Key(EKeys::Two);
	Scene.Key(EKeys::Three);
	Scene.Key(EKeys::Enter);
	TestEqual(TEXT("不足四位时保留当前输入"), Scene.Controller->GetEnteredPassword(), FString(TEXT("123")));
	TestFalse(TEXT("不足四位会显示提示"), Scene.Controller->GetPasswordEntryMessage().IsEmpty());
	Scene.Key(EKeys::A);
	Scene.Key(EKeys::Tab);
	Scene.Key(EKeys::Four);
	Scene.Key(EKeys::Five);
	TestEqual(TEXT("字母和第五位不会写入输入缓冲"), Scene.Controller->GetEnteredPassword(), FString(TEXT("1234")));
	Scene.Chest->UnlockPassword = TEXT("0007");
	Scene.Key(EKeys::Enter);
	TestEqual(TEXT("错误密码仍然上锁"), Scene.Chest->GetChestState(), EDreamPasswordChestState::Locked);
	TestTrue(TEXT("错误后保留会话、清空输入并显示错误"), Scene.Controller->IsEnteringPassword() &&
		Scene.Controller->GetEnteredPassword().IsEmpty() && !Scene.Controller->GetPasswordEntryMessage().IsEmpty());
	Scene.Key(EKeys::NumPadZero);
	Scene.Key(EKeys::NumPadZero, IE_Repeat);
	TestEqual(TEXT("长按数字不重复输入"), Scene.Controller->GetEnteredPassword(), FString(TEXT("0")));
	Scene.Key(EKeys::One);
	Scene.Key(EKeys::BackSpace);
	Scene.Key(EKeys::Zero);
	Scene.Key(EKeys::NumPadZero);
	Scene.Key(EKeys::NumPadSeven);
	TestEqual(TEXT("主键盘、小键盘和删除保留前导零"), Scene.Controller->GetEnteredPassword(), FString(TEXT("0007")));
	Scene.Key(EKeys::Enter);
	TestEqual(TEXT("正确密码开始开盖"), Scene.Chest->GetChestState(), EDreamPasswordChestState::Opening);
	TestFalse(TEXT("正确密码关闭面板、释放锁但尚未领取"), Scene.Controller->IsEnteringPassword() ||
		Scene.Controller->IsMoveInputIgnored() || Scene.Controller->IsLookInputIgnored() || Scene.Character->bHasMiniature);
	TestFalse(TEXT("输入正确时不提前显示获得提示"), Scene.HUD->IsMiniatureAcquiredMessageVisible());
	Scene.Chest->Tick(Scene.Chest->OpeningDuration * 0.5f);
	TestTrue(TEXT("开盖中可看到箱内光点"), !Scene.Chest->GlowMesh->bHiddenInGame && !Scene.Character->bHasMiniature);
	TestFalse(TEXT("盖子绕独立铰链转动"), Scene.Chest->LidPivot->GetRelativeRotation().IsNearlyZero());
	Scene.Chest->Tick(Scene.Chest->OpeningDuration * 0.5f);
	TestEqual(TEXT("完全开盖后先在箱内停留"), Scene.Chest->GetChestState(), EDreamPasswordChestState::AwaitingPickup);
	Scene.Chest->Tick(Scene.Chest->PickupDelay);
	TestEqual(TEXT("无需再次按 E，光点自动开始吸取"), Scene.Chest->GetChestState(), EDreamPasswordChestState::Attracting);
	TestFalse(TEXT("飞行开始时仍没有手办"), Scene.Character->bHasMiniature);
	Scene.Character->SetActorLocation(FVector(70, 40, 100));
	Scene.Chest->Tick(Scene.Chest->AttractionDuration);
	TestEqual(TEXT("光点抵达后箱子进入不可重复领取的状态"), Scene.Chest->GetChestState(), EDreamPasswordChestState::Collected);
	TestTrue(TEXT("抵达后角色持有手办，实际显示开启"), Scene.Character->bHasMiniature && Scene.Character->SceneMiniature->IsPresentationActive());
	TestTrue(TEXT("领取后光点隐藏且动画停止"), Scene.Chest->GlowMesh->bHiddenInGame && !Scene.Chest->IsActorTickEnabled());
	TestTrue(TEXT("获得提示仅在实际领取后出现"), Scene.HUD->IsMiniatureAcquiredMessageVisible());
	Scene.Controller->ToggleMiniatureInteractionMode();
	TestTrue(TEXT("领取后原有 Tab 观察正常开启"), Scene.Controller->IsMiniatureInteractionMode());
	Scene.Controller->ToggleMiniatureInteractionMode();
	Scene.World->RealTimeSeconds = 2.9;
	Scene.Character->AcquireMiniature();
	Scene.World->RealTimeSeconds = 3.1;
	TestFalse(TEXT("重复授予不刷新三秒获得提示"), Scene.HUD->IsMiniatureAcquiredMessageVisible());
	TestFalse(TEXT("已领取箱子不能再次解锁"), Scene.Chest->TryUnlock(TEXT("0007"), Scene.Character));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamPasswordChestSessionTest,
	"DreamSpace.Puzzle.PasswordChest.CancelDistanceViewTargetDestructionAndInputLocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPasswordChestSessionTest::RunTest(const FString& Parameters)
{
	FPasswordChestFixture Scene;
	Scene.Controller->SetIgnoreMoveInput(true);
	Scene.Controller->SetIgnoreLookInput(true);
	TestTrue(TEXT("开始密码输入"), Scene.Controller->BeginPasswordEntry(Scene.Chest));
	TestFalse(TEXT("同一会话不会重复加锁"), Scene.Controller->BeginPasswordEntry(Scene.Chest));
	Scene.Key(EKeys::BackSpace);
	Scene.Key(EKeys::One);
	Scene.Key(EKeys::Escape);
	Scene.Controller->ClosePasswordEntry();
	TestTrue(TEXT("取消和重复清理保留其它系统的锁"), Scene.Controller->IsMoveInputIgnored() && Scene.Controller->IsLookInputIgnored());
	TestEqual(TEXT("取消保持箱子上锁"), Scene.Chest->GetChestState(), EDreamPasswordChestState::Locked);
	TestTrue(TEXT("取消不会留下输入和光点"), Scene.Controller->GetEnteredPassword().IsEmpty() && Scene.Chest->GlowMesh->bHiddenInGame);
	Scene.Controller->SetIgnoreMoveInput(false);
	Scene.Controller->SetIgnoreLookInput(false);
	Scene.Controller->BeginPasswordEntry(Scene.Chest);
	Scene.Key(EKeys::E);
	TestFalse(TEXT("再次按 E 关闭面板，支持 PIE 保留 Esc 停止运行快捷键"), Scene.Controller->IsEnteringPassword());
	Scene.Controller->BeginPasswordEntry(Scene.Chest);
	Scene.Character->SetActorLocation(FVector(5000, 0, 100));
	Scene.Controller->UpdatePasswordEntry();
	TestFalse(TEXT("平台搬运离开范围后关闭面板"), Scene.Controller->IsEnteringPassword() || Scene.Controller->IsMoveInputIgnored());
	Scene.Character->SetActorLocation(FVector(0, 0, 100));
	Scene.Controller->BeginPasswordEntry(Scene.Chest);
	Scene.Controller->SetViewTarget(Scene.Chest);
	Scene.Controller->UpdatePasswordEntry();
	TestFalse(TEXT("切换观看目标后释放密码锁"), Scene.Controller->IsEnteringPassword() || Scene.Controller->IsLookInputIgnored());
	Scene.Controller->SetViewTarget(Scene.Character);
	Scene.Controller->BeginPasswordEntry(Scene.Chest);
	Scene.Controller->UnPossess();
	TestFalse(TEXT("失去角色立即关闭输入并释放两类锁"), Scene.Controller->IsEnteringPassword() ||
		Scene.Controller->IsMoveInputIgnored() || Scene.Controller->IsLookInputIgnored());
	Scene.Controller->Possess(Scene.Character);
	Scene.Controller->SetViewTarget(Scene.Character);
	Scene.Controller->BeginPasswordEntry(Scene.Chest);
	Scene.Chest->Destroy();
	Scene.Controller->UpdatePasswordEntry();
	TestFalse(TEXT("箱子销毁不留下悬挂会话或输入锁"), Scene.Controller->IsEnteringPassword() ||
		Scene.Controller->IsMoveInputIgnored() || Scene.Controller->IsLookInputIgnored());
	TestFalse(TEXT("上述中断都未发放手办"), Scene.Character->bHasMiniature);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamPasswordChestRecoveryTest,
	"DreamSpace.Puzzle.PasswordChest.CollectorReplacementReturnsUnclaimedReward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPasswordChestRecoveryTest::RunTest(const FString& Parameters)
{
	FPasswordChestFixture Scene;
	const FTransform Home = Scene.Chest->GlowRoot->GetRelativeTransform();
	TestTrue(TEXT("开始开盖"), Scene.Chest->TryUnlock(TEXT("1234"), Scene.Character));
	Scene.Chest->Tick(Scene.Chest->OpeningDuration + Scene.Chest->PickupDelay + 0.2f);
	TestEqual(TEXT("跨阶段的一帧仍正确进入吸取"), Scene.Chest->GetChestState(), EDreamPasswordChestState::Attracting);
	Scene.Controller->UnPossess();
	Scene.Chest->Tick(1.0f);
	TestFalse(TEXT("失去控制权不能把奖励授予旧角色"), Scene.Character->bHasMiniature);
	TestEqual(TEXT("未领取奖励保留在已打开的箱内"), Scene.Chest->GetChestState(), EDreamPasswordChestState::AwaitingPickup);
	TestTrue(TEXT("中断恢复光点原始位置与缩放"), Scene.Chest->GlowRoot->GetRelativeTransform().Equals(Home, 0.001f));
	TestFalse(TEXT("静止等待不消耗逐帧更新"), Scene.Chest->IsActorTickEnabled());
	ADreamCharacter* Replacement = Scene.SpawnCharacter();
	Scene.Controller->Possess(Replacement);
	Scene.Controller->SetViewTarget(Replacement);
	Scene.PreparePresentation(Replacement);
	IDreamInteractableInterface::Execute_OnInteracted(Scene.Chest, Replacement);
	TestEqual(TEXT("新角色 E 继续领取已打开箱子的光点"), Scene.Chest->GetChestState(), EDreamPasswordChestState::Attracting);
	TestFalse(TEXT("已解锁箱子不要求重新输入密码"), Scene.Controller->IsEnteringPassword());
	Scene.Chest->Tick(Scene.Chest->AttractionDuration);
	TestTrue(TEXT("奖励只授予当前新角色"), Replacement->bHasMiniature && !Scene.Character->bHasMiniature);
	IDreamInteractableInterface::Execute_OnInteracted(Scene.Chest, Scene.Character);
	TestEqual(TEXT("已领取箱子保持终态"), Scene.Chest->GetChestState(), EDreamPasswordChestState::Collected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamPasswordChestValidationTest,
	"DreamSpace.Puzzle.PasswordChest.StrictPasswordAndFrameRateIndependentTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPasswordChestValidationTest::RunTest(const FString& Parameters)
{
	for (const FString& Invalid : { FString(), FString(TEXT("123")), FString(TEXT("12345")), FString(TEXT("12a4")),
		FString(TEXT(" 123")), FString(TEXT("１２３４")), FString(TEXT("+123")) })
		TestFalse(FString::Printf(TEXT("严格拒绝非法密码：%s"), *Invalid), ADreamPasswordChest::IsFourDigitPassword(Invalid));
	TestTrue(TEXT("允许带前导零的四位密码"), ADreamPasswordChest::IsFourDigitPassword(TEXT("0007")));
	for (int32 FPS : { 30, 120 })
	{
		FPasswordChestFixture Scene;
		Scene.Chest->UnlockPassword = TEXT("123");
		TestFalse(TEXT("错误配置不能自动截断或放行"), Scene.Chest->TryUnlock(TEXT("123"), Scene.Character));
		Scene.Chest->UnlockPassword = TEXT("1234");
		TestFalse(TEXT("无效操作者不能解锁"), Scene.Chest->TryUnlock(TEXT("1234"), nullptr));
		TestTrue(TEXT("合法操作者可解锁"), Scene.Chest->TryUnlock(TEXT("1234"), Scene.Character));
		const float TotalDuration = Scene.Chest->OpeningDuration + Scene.Chest->PickupDelay + Scene.Chest->AttractionDuration;
		for (int32 Frame = 0; Frame < FMath::CeilToInt(TotalDuration * FPS) + 1; ++Frame)
			Scene.Chest->Tick(1.0f / FPS);
		TestTrue(FString::Printf(TEXT("%d FPS 按相同总时长完成领取"), FPS), Scene.Character->bHasMiniature);
	}
	FPasswordChestFixture Instant;
	Instant.Chest->OpeningDuration = 0.0f;
	Instant.Chest->PickupDelay = 0.0f;
	Instant.Chest->AttractionDuration = 0.0f;
	Instant.Chest->TryUnlock(TEXT("1234"), Instant.Character);
	Instant.Chest->Tick(0.0f);
	TestTrue(TEXT("零时长配置在一次更新内完成且没有除零"), Instant.Character->bHasMiniature);
	return true;
}
#endif
