#include "DreamMainMenuScene.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DreamMainMenuPlayerController.h"
#include "DreamMenuTransitionSubsystem.h"
#include "DreamPlayerController.h"
#include "DreamProximitySketchCameraManager.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "ImageUtils.h"
#include "SceneView.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"
#if WITH_EDITOR
#include "Editor.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMainMenuCameraFitTest,
	"DreamSpace.MainMenu.CameraFitsFullRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMainMenuCameraFitTest::RunTest(const FString&)
{
	// 验证屏幕投影本身，而不重复相机求距公式：不同窗口比例、左右布局、
	// 偏离世界原点的建筑以及 360 度内所有采样姿态都必须落在建筑预留区域。
	const FVector Center(2350.0, -840.0, 4200.0);
	const FVector HalfExtent(900.0, 1200.0, 2400.0);
	for (const float Aspect : { 16.0f / 9.0f, 4.0f / 3.0f, 21.0f / 9.0f, 2.0f / 3.0f })
	{
		for (const bool bRight : { false, true })
		{
			const FMinimalViewInfo View = ADreamMainMenuScene::ComputeCameraView(Center, HalfExtent, Aspect, bRight);
			const FDreamMainMenuLayout Layout = FDreamMainMenuLayout::ForAspectRatio(Aspect, bRight);
			const double TanX = FMath::Tan(FMath::DegreesToRadians(View.FOV * 0.5));
			for (int32 Angle = 0; Angle < 360; Angle += 5)
			{
				const FQuat Rotation(FVector::UpVector, FMath::DegreesToRadians(static_cast<double>(Angle)));
				for (int32 Corner = 0; Corner < 8; ++Corner)
				{
					const FVector Offset((Corner & 1) ? HalfExtent.X : -HalfExtent.X,
						(Corner & 2) ? HalfExtent.Y : -HalfExtent.Y, (Corner & 4) ? HalfExtent.Z : -HalfExtent.Z);
					const FVector CameraPoint = View.Rotation.UnrotateVector(Center + Rotation.RotateVector(Offset) - View.Location);
					const double U = 0.5 + CameraPoint.Y / (CameraPoint.X * TanX) * 0.5;
					const double V = 0.5 - CameraPoint.Z * Aspect / (CameraPoint.X * TanX) * 0.5;
					if (CameraPoint.X <= 0 || U < Layout.BuildingMin.X || U > Layout.BuildingMax.X
						|| V < Layout.BuildingMin.Y || V > Layout.BuildingMax.Y)
					{
						AddError(FString::Printf(TEXT("建筑越过预留区：aspect=%.3f right=%d angle=%d UV=(%.3f, %.3f)"),
							Aspect, bRight, Angle, U, V));
						return false;
					}
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMainMenuRotationTest,
	"DreamSpace.MainMenu.BuildingRotatesCameraStaysFixed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMainMenuRotationTest::RunTest(const FString&)
{
	// 在瞬时世界里挂上一个真实子 Actor，检查变换传播与帧率无关的角速度。
	// 不保存关卡、不调用原建筑的交互/物理组件。
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ADreamMainMenuScene* Scene = World->SpawnActor<ADreamMainMenuScene>();
	AActor* Child = World->SpawnActor<AActor>();
	USceneComponent* Root = NewObject<USceneComponent>(Child);
	Root->SetMobility(EComponentMobility::Movable);
	Child->SetRootComponent(Root);
	Child->AddInstanceComponent(Root);
	Root->RegisterComponent();
	Child->SetActorLocation(Scene->BuildingPivot->GetComponentLocation() + FVector(100.0, 0.0, 0.0));
	Child->AttachToComponent(Scene->BuildingPivot, FAttachmentTransformRules::KeepWorldTransform);
	const FTransform InitialCamera = Scene->MenuCamera->GetComponentTransform();
	Scene->RotationSpeed = 2.0f;
	for (int32 Frame = 0; Frame < 60; ++Frame)
		Scene->Tick(0.5f);
	const FVector Expected = Scene->BuildingPivot->GetComponentLocation()
		+ FQuat(FVector::UpVector, FMath::DegreesToRadians(60.0)).RotateVector(FVector(100.0, 0.0, 0.0));
	TestTrue(TEXT("30 秒恰好转 60 度"), FMath::IsNearlyEqual(Scene->GetRotationAngle(), 60.0f, 0.01f));
	TestTrue(TEXT("子 Actor 绕建筑中心旋转"), Child->GetActorLocation().Equals(Expected, 0.01));
	TestTrue(TEXT("固定镜头没有随建筑转动"), Scene->MenuCamera->GetComponentTransform().Equals(InitialCamera, 0.001));
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMainMenuRooftopTest,
	"DreamSpace.MainMenu.RooftopApproachAtAnyRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMainMenuRooftopTest::RunTest(const FString&)
{
	// 与真实组件对比变换与投影：久置菜单到背面、跨 360 度、左右布局和窄窗口，
	// 点击当帧不跳相机，末帧仍严格对应同一个 TEST 天台视点。
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	// Actor::Destroy 需要对应的引擎世界上下文；测试结束时一起释放，避免留下瞬时世界。
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	for (const float Angle : { 0.0f, 89.0f, 179.0f, 359.0f })
		for (const float Aspect : { 16.0f / 9.0f, 21.0f / 9.0f, 2.0f / 3.0f })
		{
			ADreamMainMenuScene* Scene = World->SpawnActor<ADreamMainMenuScene>();
			Scene->SetActorLocation(Scene->GameplayBuildingTransform.GetLocation());
			Scene->bLogoOnRight = Angle < 90.0f;
			Scene->Tick(Angle / Scene->RotationSpeed);
			Scene->UpdateCamera(Aspect);
			const FTransform Start = Scene->MenuCamera->GetComponentTransform();
			Scene->BeginRooftopApproach(3.8f);
			Scene->AdvanceRooftopApproach(0.0f);
			TestTrue(TEXT("点击瞬间维持已有镜头"), Start.Equals(Scene->MenuCamera->GetComponentTransform(), 0.001));
			// 过渡中调用构图刷新也不得把镜头重置回远景。
			Scene->AdvanceRooftopApproach(2.0f);
			const FTransform Middle = Scene->MenuCamera->GetComponentTransform();
			Scene->UpdateCamera(Aspect);
			TestTrue(TEXT("取景刷新不打断飞入"), Middle.Equals(Scene->MenuCamera->GetComponentTransform(), 0.001));
			Scene->AdvanceRooftopApproach(5.0f);
			TestTrue(TEXT("菜单终点建筑与 TEST 的原始朝向一致"), FMath::IsNearlyZero(Scene->GetRotationAngle(), 0.001f));
			const FMinimalViewInfo Entry = Scene->GetGameplayEntryView();
			const FTransform Display = Scene->BuildingPivot->GetComponentTransform();
			const FVector ActualPosition = Scene->GameplayBuildingTransform.TransformPosition(
				Display.InverseTransformPosition(Scene->MenuCamera->GetComponentLocation()));
			const FQuat ActualRotation = Scene->GameplayBuildingTransform.GetRotation()
				* Display.GetRotation().Inverse() * Scene->MenuCamera->GetComponentQuat();
			TestTrue(TEXT("跨世界的天台位置一致"), ActualPosition.Equals(Entry.Location, 0.01));
			TestTrue(TEXT("跨世界的天台朝向一致"), ActualRotation.Equals(Entry.Rotation.Quaternion(), 0.001));
			TestTrue(TEXT("跨世界的视野一致"), FMath::IsNearlyEqual(Scene->MenuCamera->FieldOfView, Entry.FOV, 0.001f));
			TestTrue(TEXT("终点与玩法天台同一世界坐标"), Scene->MenuCamera->GetComponentLocation().Equals(Entry.Location, 0.01));
			const FTransform End = Scene->MenuCamera->GetComponentTransform();
			Scene->AdvanceRooftopApproach(50.0f);
			TestTrue(TEXT("低帧率越过终点时稳定停在天台"), End.Equals(Scene->MenuCamera->GetComponentTransform(), 0.001));
			Scene->Destroy();
		}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMenuProjectionTest,
	"DreamSpace.MainMenu.ArrivalProjectionMatchesPlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMenuProjectionTest::RunTest(const FString&)
{
	// 用引擎实际生成的投影矩阵验证 FOV 约定转换，覆盖超宽屏与竖屏，
	// 避免“数值都为 80 度”却在最终接管帧发生一次明显缩放。
	for (const float Aspect : { 16.0f / 9.0f, 21.0f / 9.0f, 2.0f / 3.0f })
		for (const EAspectRatioAxisConstraint Constraint : { AspectRatio_MaintainYFOV,
			AspectRatio_MaintainXFOV, AspectRatio_MajorAxisFOV })
		{
			FMinimalViewInfo PlayerView;
			PlayerView.FOV = 80.0f;
			PlayerView.AspectRatio = 16.0f / 9.0f;
			PlayerView.AspectRatioAxisConstraint = Constraint;
			FMinimalViewInfo ArrivalView = PlayerView;
			ArrivalView.FOV = UDreamMenuTransitionSubsystem::HorizontalFOV(PlayerView, Aspect, Constraint);
			ArrivalView.AspectRatioAxisConstraint = AspectRatio_MaintainXFOV;
			FSceneViewProjectionData PlayerProjection, ArrivalProjection;
			const FIntRect Rect(0, 0, FMath::RoundToInt(Aspect * 900.0f), 900);
			PlayerProjection.SetViewRectangle(Rect);
			ArrivalProjection.SetViewRectangle(Rect);
			FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(PlayerView, Constraint, Rect, PlayerProjection);
			FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(ArrivalView, AspectRatio_MaintainXFOV, Rect, ArrivalProjection);
			TestTrue(TEXT("同一屏幕点的横纵投影在交接时一致"), PlayerProjection.ProjectionMatrix.Equals(
				ArrivalProjection.ProjectionMatrix, 0.001));
		}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMenuTransitionLifecycleTest,
	"DreamSpace.MainMenu.ArrivalLifecycleAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMenuTransitionLifecycleTest::RunTest(const FString&)
{
	// 用真正的 GameInstance 与两个 World 验证状态隔离；瞬时测试不读屏幕、不发 OpenLevel。
	// 时长推进来自游戏相机，跨地图加载再慢也不会把尚未展示的落镜头动画跳掉。
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->Init();
	UDreamMenuTransitionSubsystem* Transition = Instance->GetSubsystem<UDreamMenuTransitionSubsystem>();
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UWorld* Other = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(Instance);
	Other->SetGameInstance(NewObject<UGameInstance>(GEngine));
	TestFalse(TEXT("直接进入游玩没有入场请求"), Transition->IsArrivalForWorld(World));
	Transition->DestinationPackage = World->GetOutermost()->GetName();
	Transition->bPendingTravel = true;
	Transition->FrameBlendTime = 0.55f;
	Transition->LandingTime = 1.75f;
	TestTrue(TEXT("新世界第一帧前已有输入 / HUD 门控"), Transition->IsArrivalForWorld(World));
	TestFalse(TEXT("另一场 PIE 不继承入场请求"), Transition->IsArrivalForWorld(Other));
	TestEqual(TEXT("加载阶段快照完全不透明"), Transition->GetSnapshotOpacity(), 1.0f);
	Transition->AdvanceArrival(World, 10.0f);
	TestTrue(TEXT("只有正确世界开始接续"), Transition->IsArrivalRunning(World));
	TestEqual(TEXT("加载时间不消耗动画"), Transition->GetCameraBlendAlpha(), 0.0f);
	Transition->bWaitForRenderedArrival = true;
	Transition->AdvanceArrival(World, 10.0f);
	TestEqual(TEXT("没有真正绘制 TEST 时快照不退去"), Transition->GetSnapshotOpacity(), 1.0f);
	Transition->bArrivalFrameRendered = true;
	Transition->AdvanceArrival(World, 10.0f);
	const float FirstVisibleOpacity = Transition->GetSnapshotOpacity();
	TestTrue(TEXT("首次长卡顿保留大部分交叉淡化"), FirstVisibleOpacity > 0.9f && FirstVisibleOpacity < 1.0f);
	Transition->AdvanceArrival(World, 0.1f);
	TestEqual(TEXT("同一渲染帧重复更新不消耗动画"), Transition->GetSnapshotOpacity(), FirstVisibleOpacity);
	// 后续验证无渲染环境的完整时长与清理；不需要伪造每个图形设备帧。
	Transition->bWaitForRenderedArrival = false;
	Transition->ArrivalElapsed = 0.0f;
	Transition->AdvanceArrival(World, 0.30f);
	TestEqual(TEXT("交叉淡化阶段镜头固定"), Transition->GetCameraBlendAlpha(), 0.0f);
	TestTrue(TEXT("场景交叉淡化正在进行"), Transition->GetSnapshotOpacity() > 0.0f && Transition->GetSnapshotOpacity() < 1.0f);
	Transition->AdvanceArrival(World, 0.50f);
	TestEqual(TEXT("落镜头前快照已消失"), Transition->GetSnapshotOpacity(), 0.0f);
	TestTrue(TEXT("镜头正在靠近玩家"), Transition->GetCameraBlendAlpha() > 0.0f && Transition->GetCameraBlendAlpha() < 1.0f);
	Transition->AdvanceArrival(World, 10.0f);
	TestEqual(TEXT("低帧率也精确到终点"), Transition->GetCameraBlendAlpha(), 1.0f);
	Transition->CompleteArrival();
	TestFalse(TEXT("完成后恢复常规游玩"), Transition->IsArrivalForWorld(World));
	TestFalse(TEXT("完成后移除持久 Slate 层"), Transition->FrameOverlay.IsValid());
	TestNull(TEXT("完成后释放快照纹理"), Transition->FrozenFrame.Get());
	// 再用真正的游玩控制器与相机管理器验证中断路径，不能只断言子系统清空自己的字段。
	ADreamPlayerController* Controller = World->SpawnActor<ADreamPlayerController>();
	Controller->Player = NewObject<ULocalPlayer>(GEngine);
	World->AddController(Controller);
	Controller->SpawnPlayerCameraManager();
	APawn* Pawn = World->SpawnActor<APawn>();
	Controller->Possess(Pawn);
	Controller->SetViewTarget(Pawn);
	const auto Restart = [&]()
	{
		Transition->DestinationPackage = World->GetOutermost()->GetName();
		Transition->bPendingTravel = true;
		Transition->ArrivalElapsed = 0.0f;
		Transition->AdvanceArrival(World, 0.0f);
	};
	Restart();
	TestTrue(TEXT("真实控制器在落镜头阶段锁住探索"), Controller->IsMenuArrivalActive());
	Controller->UnPossess();
	TestFalse(TEXT("失去角色后立即释放输入门控"), Controller->IsMenuArrivalActive());
	Controller->Possess(Pawn);
	Controller->SetViewTarget(Pawn);
	Restart();
	Controller->SetViewTarget(World->SpawnActor<ACameraActor>());
	Controller->PlayerCameraManager->UpdateCamera(0.01f);
	TestFalse(TEXT("外部镜头接管时取消入场"), Controller->IsMenuArrivalActive());
	Controller->SetViewTarget(Pawn);
	Restart();
	Controller->Destroy();
	TestFalse(TEXT("控制器销毁后释放持久接力"), Transition->IsArrivalForWorld(World));
	Instance->Shutdown();
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	Other->DestroyWorld(false);
	return true;
}

namespace
{
#if WITH_EDITOR
/** 独立验收窗口不受编辑器面板布局影响；只复制设置，不写用户的编辑器配置。 */
FAutoConsoleCommand BeginMenuPIECommand(
	TEXT("dream.TestMenuBeginPIE"), TEXT("开发验收：在固定尺寸新窗口启动 PIE；默认 1280x720。"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (!GEditor || GEditor->PlayWorld)
			return;
		ULevelEditorPlaySettings* Settings = DuplicateObject<ULevelEditorPlaySettings>(
			GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage());
		Settings->NewWindowWidth = Args.Num() >= 2 ? FMath::Max(320, FCString::Atoi(*Args[0])) : 1280;
		Settings->NewWindowHeight = Args.Num() >= 2 ? FMath::Max(180, FCString::Atoi(*Args[1])) : 720;
		Settings->CenterNewWindow = true;
		Settings->PIEAlwaysOnTop = false;
		FRequestPlaySessionParams Request;
		Request.EditorPlaySettings = Settings;
		Request.bAllowOnlineSubsystem = false;
		GEditor->RequestPlaySession(Request);
	}));
#endif

/** 开发验收命令只存在于非 Shipping 测试构建，截取真实 Slate + 3D 游戏视口。 */
FAutoConsoleCommandWithWorldAndArgs CaptureMenuCommand(
	TEXT("dream.TestMenuScreenshot"), TEXT("开发验收：保存包含 Logo 的真实菜单视口。"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (World && Args.Num() >= 1)
		{
			// 相机缓存的数值正确并不意味着本帧已经画出该视角，记录最后一次真实渲染的
			// 视图矩阵及加载期间的渲染开关；raw 参数可单独取 3D 缓冲来排除 Slate 快照影响。
			const FWorldCachedViewInfo* Rendered = World->CachedViewInfoRenderedLastFrame.IsEmpty()
				? nullptr : &World->CachedViewInfoRenderedLastFrame.Last();
			UE_LOG(LogTemp, Display, TEXT("MenuRender time=%.3f last=%.3f enabled=%d views=%d origin=%s forward=%s projection=%s"),
				World->GetTimeSeconds(), World->LastRenderTime, FViewport::IsGameRenderingEnabled(),
				World->CachedViewInfoRenderedLastFrame.Num(),
				Rendered ? *Rendered->ViewToWorld.GetOrigin().ToString() : TEXT("none"),
				Rendered ? *Rendered->ViewToWorld.GetUnitAxis(EAxis::Z).ToString() : TEXT("none"),
				Rendered ? *FVector2D(Rendered->ProjectionMatrix.M[0][0], Rendered->ProjectionMatrix.M[1][1]).ToString() : TEXT("none"));
			// 连同真实快照淡化状态写日志，便于对照 GPU 画面与相机缓存，不能仅凭截图文件存在判定通过。
			if (UGameInstance* Instance = World->GetGameInstance())
				if (UDreamMenuTransitionSubsystem* Transition = Instance->GetSubsystem<UDreamMenuTransitionSubsystem>())
					UE_LOG(LogTemp, Display, TEXT("MenuCapture frame=%s snapshot=%.3f camera=%.3f"),
						*Args[0], Transition->GetSnapshotOpacity(), Transition->GetCameraBlendAlpha());
			const bool bShowUI = Args.Num() < 2 || !Args[1].Equals(TEXT("raw"), ESearchCase::IgnoreCase);
			UGameViewportClient* Client = World->GetGameViewport();
			if (!Client || !Client->Viewport)
				return;
			TArray<FColor> Pixels;
			FIntPoint Size = Client->Viewport->GetSizeXY();
			bool bCaptured = false;
			const TSharedPtr<SViewport> Widget = Client->GetGameViewportWidget();
			if (bShowUI && Widget.IsValid() && FSlateApplication::IsInitialized())
			{
				FIntVector WidgetSize = FIntVector::ZeroValue;
				bCaptured = FSlateApplication::Get().TakeScreenshot(Widget.ToSharedRef(), Pixels, WidgetSize);
				Size = FIntPoint(WidgetSize.X, WidgetSize.Y);
			}
			else
				bCaptured = Client->Viewport->ReadPixels(Pixels);
			// FScreenshotRequest 是全局请求，切图后游戏暂不绘制时，编辑器视口会先消费它，
			// 将旧编辑器画面冒充新世界的首帧。直接读取指定 PIE 视口后保存，保证证据归属正确。
			if (bCaptured && Size.X > 0 && Size.Y > 0 && Pixels.Num() == Size.X * Size.Y)
			{
				for (FColor& Pixel : Pixels)
					Pixel.A = 255;
				TArray64<uint8> PNG;
				FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
				FFileHelper::SaveArrayToFile(PNG, *Args[0]);
			}
		}
	}));

/**
 * 从指定的归一化视口位置经过 Slate 命中检测发出真实左键，验证“任意处”界面。
 * 默认点在右下角留白，避开 Logo 和建筑，防止仅按钮区域可点击的错误蒙混通过。
 */
FAutoConsoleCommandWithWorldAndArgs ClickMenuCommand(
	TEXT("dream.TestMenuClick"), TEXT("开发验收：在菜单留白处通过 Slate 模拟一次左键。"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || !Cast<ADreamMainMenuPlayerController>(World->GetFirstPlayerController()))
			return;
		UGameViewportClient* Client = World->GetGameViewport();
		const TSharedPtr<SViewport> Viewport = Client ? Client->GetGameViewportWidget() : nullptr;
		if (!Viewport || !FSlateApplication::IsInitialized())
			return;
		const FVector2D UV = Args.Num() == 2 ? FVector2D(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]))
			: FVector2D(0.96, 0.96);
		const FGeometry& Geometry = Viewport->GetCachedGeometry();
		const FVector2D Position = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * UV);
		FSlateApplication& Slate = FSlateApplication::Get();
		const TSet<FKey> Pressed{ EKeys::LeftMouseButton };
		const FPointerEvent Down(0, Position, Position, Pressed, EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
		Slate.ProcessMouseButtonDownEvent(nullptr, Down);
		const FPointerEvent Up(0, Position, Position, TSet<FKey>(), EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
		Slate.ProcessMouseButtonUpEvent(Up);
	}));

/** 真实 PIE 验收经由原始按键入口驱动 Enhanced Input；Shipping 不包含模拟输入命令。 */
FAutoConsoleCommandWithWorldAndArgs PlayerKeyCommand(
	TEXT("dream.TestPlayerKey"), TEXT("开发验收：模拟游玩按键。格式：键 Press/Release/Axis [轴值]。"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ADreamPlayerController* Controller = World ? Cast<ADreamPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!Controller || Args.Num() < 2)
			return;
		const EInputEvent Event = Args[1].Equals(TEXT("Release"), ESearchCase::IgnoreCase) ? IE_Released
			: Args[1].Equals(TEXT("Axis"), ESearchCase::IgnoreCase) ? IE_Axis : IE_Pressed;
		const float Amount = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 1.0f;
		UGameViewportClient* Client = World->GetGameViewport();
		const FInputKeyEventArgs Key = FInputKeyEventArgs::CreateSimulated(FKey(*Args[0]), Event, Amount,
			1, INPUTDEVICEID_NONE, false, Client ? Client->Viewport : nullptr);
		Controller->InputKey(Key);
	}));
}
#endif
