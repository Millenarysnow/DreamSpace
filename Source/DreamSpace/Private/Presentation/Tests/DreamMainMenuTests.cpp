#include "DreamMainMenuScene.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DreamMainMenuPlayerController.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"

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

namespace
{
/** 开发验收命令只存在于非 Shipping 测试构建，截取真实 Slate + 3D 游戏视口。 */
FAutoConsoleCommandWithWorldAndArgs CaptureMenuCommand(
	TEXT("dream.TestMenuScreenshot"), TEXT("开发验收：保存包含 Logo 的真实菜单视口。"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (World && Args.Num() == 1)
			FScreenshotRequest::RequestScreenshot(Args[0], true, false, false, FIntRect(), true);
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
}
#endif
