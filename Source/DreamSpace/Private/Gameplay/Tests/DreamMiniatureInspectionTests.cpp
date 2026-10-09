#include "DreamShoulderCameraComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DreamCharacter.h"
#include "DreamDraggableComponent.h"
#include "DreamPivotPointComponent.h"
#include "DreamPlayerController.h"
#include "DreamRotatableComponent.h"
#include "DreamSceneCaptureAnchor.h"
#include "DreamSceneCapturePresentationComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputActionValue.h"
#include "EnhancedPlayerInput.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "SceneView.h"
#include "UnrealClient.h"

namespace
{
	/** 使用真实原生角色、显示平面和 SceneCapture，测试只创建瞬时世界，不保存任何关卡资产。 */
	struct FMiniatureInspectionFixture
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		ADreamCharacter* Character = nullptr;
		ADreamPlayerController* Controller = nullptr;
		ADreamSceneCaptureAnchor* Anchor = nullptr;
		UDreamShoulderCameraComponent* Camera = nullptr;
		UDreamSceneCapturePresentationComponent* Miniature = nullptr;
		UStaticMeshComponent* Display = nullptr;
		ASceneCapture2D* Capture = nullptr;

		FMiniatureInspectionFixture()
		{
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Character = World->SpawnActor<ADreamCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
			Controller = World->SpawnActor<ADreamPlayerController>();
			Controller->Player = NewObject<ULocalPlayer>(GEngine);
			// PlayerTick 会执行引擎输入阶段；瞬时世界需显式创建正常初始化时提供的 PlayerInput。
			Controller->PlayerInput = NewObject<UEnhancedPlayerInput>(Controller);
			World->AddController(Controller);
			Controller->SpawnPlayerCameraManager();
			Controller->Possess(Character);
			Controller->SetViewTarget(Character);
			Controller->SetControlRotation(FRotator::ZeroRotator);
			Camera = CastChecked<UDreamShoulderCameraComponent>(Character->CameraBoom);
			Camera->Activate(true);
			Camera->ResetCameraState();
			Camera->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
			Anchor = World->SpawnActor<ADreamSceneCaptureAnchor>(FVector(5000, 2000, 1000), FRotator::ZeroRotator);
			Miniature = Character->SceneMiniature;
			// 测试只验证投影与输入生命周期，降低瞬时 RT 大小以减少无窗口回归占用。
			Miniature->RenderTargetWidth = 256;
			Miniature->RenderTargetHeight = 256;
			Miniature->BeginPlay();
			TInlineComponentArray<UStaticMeshComponent*> Meshes(Character);
			for (UStaticMeshComponent* Mesh : Meshes)
				if (Mesh->GetFName() == TEXT("SceneCaptureDisplayMesh"))
					Display = Mesh;
			for (AActor* Actor : World->PersistentLevel->Actors)
				if (ASceneCapture2D* Candidate = Cast<ASceneCapture2D>(Actor))
					Capture = Candidate;
			check(Display && Capture);
		}

		~FMiniatureInspectionFixture()
		{
			Controller->UnPossess();
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		FMinimalViewInfo POV() const
		{
			FMinimalViewInfo View;
			Character->FollowCamera->GetCameraView(0.0f, View);
			return View;
		}

		void Step()
		{
			Camera->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
			Miniature->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
		}

		/** 创建真实碰撞目标：同时用于相机避障与旋转后左键命中验证。 */
		UBoxComponent* Box(const FVector& Location, const FVector& Extent, ECollisionChannel Channel)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			UBoxComponent* Component = NewObject<UBoxComponent>(Actor);
			Actor->SetRootComponent(Component);
			Actor->AddInstanceComponent(Component);
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetBoxExtent(Extent);
			Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Component->SetCollisionResponseToAllChannels(ECR_Ignore);
			Component->SetCollisionResponseToChannel(Channel, ECR_Block);
			Component->RegisterComponent();
			Actor->SetActorLocation(Location);
			return Component;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMiniatureFocusLifecycleTest,
	"DreamSpace.Camera.Miniature.FocusInputLocksAndLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMiniatureFocusLifecycleTest::RunTest(const FString& Parameters)
{
	FMiniatureInspectionFixture Scene;
	const FTransform Exploration(Scene.POV().Rotation, Scene.POV().Location);
	const FQuat Control = Scene.Controller->GetControlRotation().Quaternion();
	const float ArmLength = Scene.Camera->TargetArmLength;
	const FVector ShoulderOffset = Scene.Camera->SocketOffset;
	// 先加其它系统的输入锁，验证 Tab 和左键会话都只释放自己的计数。
	Scene.Controller->SetIgnoreMoveInput(true);
	Scene.Controller->SetIgnoreLookInput(true);
	Scene.Controller->SetMiniatureInteractionMode(true);
	TestTrue(TEXT("Tab 同时启用观察、居中与光标模式"), Scene.Controller->IsMiniatureInteractionMode()
		&& Scene.Miniature->IsInspecting() && Scene.Camera->IsMiniatureFocused() && Scene.Controller->bShowMouseCursor);
	const FMinimalViewInfo Focus = Scene.POV();
	TestTrue(TEXT("主相机光轴穿过手办显示面的真实中心"), Focus.Rotation.Vector().Equals(
		(Scene.Miniature->GetDisplayCenter() - Focus.Location).GetSafeNormal(), 0.0001));
	TestTrue(TEXT("居中后推近显示面，便于鼠标操作"), FVector::Distance(Focus.Location, Scene.Miniature->GetDisplayCenter())
		< FVector::Distance(Exploration.GetLocation(), Scene.Miniature->GetDisplayCenter()));
	Scene.Character->DoLook(20, 15);
	Scene.Controller->UpdateRotation(1.0f / 60.0f);
	Scene.Controller->ZoomCamera(FInputActionValue(1.0f));
	Scene.Character->DoJumpStart();
	TestTrue(TEXT("观察期间不修改探索控制旋转"), Scene.Controller->GetControlRotation().Quaternion().Equals(Control, 0.0001));
	TestEqual(TEXT("观察期间滚轮不改写探索距离"), Scene.Camera->TargetArmLength, ArmLength);
	TestFalse(TEXT("观察期间不能通过角色跳跃入口扰动显示面"), Scene.Character->bPressedJump);
	TestTrue(TEXT("观察相机仍锁住角色移动与视角输入"),
		Scene.Controller->IsMoveInputIgnored() && Scene.Controller->IsLookInputIgnored());

	AActor* Target = Scene.Box(FVector(8000, 0, 1000), FVector(50), ECC_Visibility)->GetOwner();
	UDreamPivotPointComponent* Pivot = NewObject<UDreamPivotPointComponent>(Target);
	Target->AddInstanceComponent(Pivot);
	Pivot->SetupAttachment(Target->GetRootComponent());
	Pivot->RegisterComponent();
	UDreamDraggableComponent* Drag = NewObject<UDreamDraggableComponent>(Target);
	Target->AddInstanceComponent(Drag);
	Drag->RegisterComponent();
	Drag->Activate(true);
	Scene.Controller->BeginActiveDrag(Drag, Target->GetActorLocation() + FVector(0, 0, 500), FVector::DownVector);
	TestTrue(TEXT("居中后仍可开始原来的左键机关拖动"), Scene.Controller->IsDraggingInteraction());
	Scene.Controller->EndActiveDrag();
	TestTrue(TEXT("松开左键不解除 Tab 或其它系统添加的输入锁"),
		Scene.Controller->IsMoveInputIgnored() && Scene.Controller->IsLookInputIgnored());
	Scene.Controller->SetMiniatureInteractionMode(false);
	TestTrue(TEXT("退出后恢复原探索相机姿态"), FTransform(Scene.POV().Rotation, Scene.POV().Location).Equals(Exploration, 0.01));
	TestTrue(TEXT("探索肩位保持原来的配置"), Scene.Camera->SocketOffset.Equals(ShoulderOffset));
	TestTrue(TEXT("退出 Tab 保留其它系统的输入锁"),
		Scene.Controller->IsMoveInputIgnored() && Scene.Controller->IsLookInputIgnored());
	Scene.Controller->SetIgnoreMoveInput(false);
	Scene.Controller->SetIgnoreLookInput(false);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Scene.Controller->SetMiniatureInteractionMode(true);
		Scene.Controller->SetMiniatureInteractionMode(false);
	}
	TestFalse(TEXT("反复切换不会累积输入锁"), Scene.Controller->IsMoveInputIgnored() || Scene.Controller->IsLookInputIgnored());
	Scene.Controller->SetMiniatureInteractionMode(true);
	Scene.Controller->UnPossess();
	TestFalse(TEXT("失去 Pawn 立即清理观察、聚焦和输入锁"), Scene.Miniature->IsInspecting() || Scene.Camera->IsMiniatureFocused()
		|| Scene.Controller->IsMiniatureInteractionMode() || Scene.Controller->IsMoveInputIgnored() || Scene.Controller->IsLookInputIgnored());
	// 重新控制角色之前也可能收到输入；无 Pawn 的控制器不能开启一个无法聚焦的空观察会话。
	Scene.Controller->SetMiniatureInteractionMode(true);
	TestFalse(TEXT("无 Pawn 时 Tab 保持探索状态且不显示观察光标"), Scene.Controller->IsMiniatureInteractionMode()
		|| Scene.Controller->bShowMouseCursor || Scene.Controller->IsMoveInputIgnored() || Scene.Controller->IsLookInputIgnored());
	Scene.Controller->Possess(Scene.Character);
	Scene.Controller->SetMiniatureInteractionMode(true);
	Scene.Miniature->SetPresentationEnabled(false);
	Scene.Controller->PlayerTick(0.0f);
	TestFalse(TEXT("手办停用后逐帧兜底释放 Tab 输入锁"), Scene.Controller->IsMiniatureInteractionMode()
		|| Scene.Controller->IsMoveInputIgnored() || Scene.Controller->IsLookInputIgnored());
	Scene.Miniature->SetPresentationEnabled(true);
	Scene.Controller->SetMiniatureInteractionMode(true);
	Scene.Controller->SetViewTarget(Scene.World->SpawnActor<AActor>());
	Scene.Controller->PlayerTick(0.0f);
	TestFalse(TEXT("外部切镜头后立即退出观察并清理聚焦"), Scene.Controller->IsMiniatureInteractionMode()
		|| Scene.Miniature->IsInspecting() || Scene.Camera->IsMiniatureFocused());
	Scene.Controller->SetViewTarget(Scene.Character);
	Scene.Miniature->SetPresentationEnabled(false);
	Scene.Controller->SetMiniatureInteractionMode(true);
	TestFalse(TEXT("隐藏的手办不能进入观察模式"), Scene.Controller->IsMiniatureInteractionMode());
	Scene.Miniature->SetPresentationEnabled(true);
	Scene.Controller->SetMiniatureInteractionMode(true);
	Scene.Miniature->DestroyComponent();
	Scene.Controller->PlayerTick(0.0f);
	TestFalse(TEXT("显示组件销毁后退出观察并释放输入锁"), Scene.Controller->IsMiniatureInteractionMode()
		|| Scene.Controller->IsMoveInputIgnored() || Scene.Controller->IsLookInputIgnored());
	TestTrue(TEXT("失效弱引用清理后重建探索 Socket"), FTransform(Scene.POV().Rotation, Scene.POV().Location).Equals(Exploration, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMiniatureFocusFramingTest,
	"DreamSpace.Camera.Miniature.FramingCollisionAndGravity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMiniatureFocusFramingTest::RunTest(const FString& Parameters)
{
	FMiniatureInspectionFixture Scene;
	// 直接用主视口投影检查实际网格四角，覆盖横/竖屏、三种 FOV 约定及相机自身覆盖设置。
	for (float Aspect : {16.0f / 9.0f, 9.0f / 16.0f})
	{
		Scene.Character->FollowCamera->AspectRatio = Aspect;
		for (EAspectRatioAxisConstraint Axis : {AspectRatio_MaintainXFOV, AspectRatio_MaintainYFOV, AspectRatio_MajorAxisFOV})
		{
			Scene.Controller->GetLocalPlayer()->AspectRatioAxisConstraint = Axis;
			TestTrue(TEXT("真实手办组件可进入观察"), Scene.Miniature->BeginInspection());
			TestTrue(TEXT("相机可聚焦到本角色的显示面"), Scene.Camera->BeginMiniatureFocus(Scene.Miniature, Scene.POV()));
			Scene.Step();
			FMinimalViewInfo View = Scene.POV();
			const FIntRect Rect(0, 0, 1920, FMath::RoundToInt(1920.0f / Aspect));
			FSceneViewProjectionData Projection;
			Projection.SetViewRectangle(Rect);
			FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(View, Axis, Rect, Projection);
			const FTransform CameraTransform(View.Rotation, View.Location);
			const FBox Bounds = Scene.Display->GetStaticMesh()->GetBoundingBox();
			for (const FVector2D Sign : {FVector2D(-1, -1), FVector2D(-1, 1), FVector2D(1, -1), FVector2D(1, 1)})
			{
				const FVector Point = Scene.Display->GetComponentTransform().TransformPosition(
					FVector(Sign.X * Bounds.GetExtent().X, Sign.Y * Bounds.GetExtent().Y, 0));
				const FVector Local = CameraTransform.InverseTransformPosition(Point);
				// UE 投影矩阵的输入为右/上/前，输出再除齐次 W 得到归一化设备坐标。
				const FVector4 Clip = Projection.ProjectionMatrix.TransformFVector4(FVector4(Local.Y, Local.Z, Local.X, 1));
				TestTrue(TEXT("四角位于相机前方"), Clip.W > 0);
				TestTrue(TEXT("四角保留配置的屏幕边距"), FMath::Abs(Clip.X / Clip.W) <= Scene.Camera->MiniatureScreenFill + 0.001
					&& FMath::Abs(Clip.Y / Clip.W) <= Scene.Camera->MiniatureScreenFill + 0.001);
			}
			Scene.Camera->EndMiniatureFocus();
			Scene.Miniature->EndInspection();
		}
	}

	Scene.Character->FollowCamera->AspectRatio = 16.0f / 9.0f;
	Scene.Miniature->BeginInspection();
	Scene.Camera->BeginMiniatureFocus(Scene.Miniature, Scene.POV());
	const FVector Center = Scene.Miniature->GetDisplayCenter();
	const FMinimalViewInfo Before = Scene.POV();
	UBoxComponent* Wall = Scene.Box(Center + (Before.Location - Center) * 0.65f, FVector(5), ECC_Camera);
	Scene.Step();
	TestTrue(TEXT("聚焦相机仍响应真实 Camera 通道障碍"), Scene.Camera->IsCollisionFixApplied());
	TestTrue(TEXT("碰撞收近后相机仍指向手办中心"), Scene.POV().Rotation.Vector().Equals(
		(Center - Scene.POV().Location).GetSafeNormal(), 0.0001));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DreamMiniatureFocusTest), false, Scene.Character);
	TestFalse(TEXT("聚焦相机球没有留在障碍内"), Scene.World->OverlapBlockingTestByChannel(
		Scene.POV().Location, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(Scene.Camera->ProbeSize), Params));
	Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	const FQuat Turn(FVector::ForwardVector, HALF_PI);
	Scene.Character->SetActorRotation(Turn);
	Scene.Character->GetCharacterMovement()->SetGravityDirection(Turn.RotateVector(FVector::DownVector));
	Scene.Step();
	TestTrue(TEXT("重力翻转后相机和显示面保持居中关系"), Scene.POV().Rotation.Vector().Equals(
		(Scene.Miniature->GetDisplayCenter() - Scene.POV().Location).GetSafeNormal(), 0.0001));
	TestTrue(TEXT("聚焦上方向随角色搬运，无额外世界 Z 约束"), Scene.POV().Rotation.Quaternion().GetUpVector().Equals(
		Turn.RotateVector(Before.Rotation.Quaternion().GetUpVector()), 0.0001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMiniatureInspectionRotationTest,
	"DreamSpace.Presentation.Miniature.FreeRotationAndPicking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMiniatureInspectionRotationTest::RunTest(const FString& Parameters)
{
	FMiniatureInspectionFixture Scene;
	Scene.Controller->SetMiniatureInteractionMode(true);
	const FTransform MainCamera(Scene.POV().Rotation, Scene.POV().Location);
	const FTransform CharacterTransform = Scene.Character->GetActorTransform();
	const FVector Gravity = Scene.Character->GetCharacterMovement()->GetGravityDirection();
	const FTransform InitialCapture = Scene.Capture->GetActorTransform();
	// 同一段斜拖按不同帧率采样，最终角度应一致；直接比较最终捕获姿态，不复制旋转实现。
	Scene.Miniature->RotateInspection(FVector2D(120, 90));
	const FQuat SingleFrameTurn = Scene.Capture->GetActorQuat();
	Scene.Controller->SetMiniatureInteractionMode(false);
	Scene.Controller->SetMiniatureInteractionMode(true);
	for (int32 Frame = 0; Frame < 12; ++Frame)
		Scene.Miniature->RotateInspection(FVector2D(10, 7.5));
	TestTrue(TEXT("同一斜拖拆成多帧仍得到同一展示角度"), Scene.Capture->GetActorQuat().Equals(SingleFrameTurn, 0.0001));
	Scene.Controller->SetMiniatureInteractionMode(false);
	Scene.Controller->SetMiniatureInteractionMode(true);
	const float CaptureDistance = FVector::Distance(Scene.Capture->GetActorLocation(), Scene.Anchor->GetActorLocation());
	const float FOV = Scene.Capture->GetCaptureComponent2D()->FOVAngle;
	const FVector Ray = Scene.Miniature->GetDisplayCenter() - MainCamera.GetLocation();
	TestTrue(TEXT("右键可从手办显示面内抓取"), Scene.Controller->BeginMiniatureRotationRay(MainCamera.GetLocation(), Ray));
	Scene.Controller->UpdateMiniatureRotation();
	TestFalse(TEXT("没有焦点/右键的窗口会立即清理旋转会话"), Scene.Controller->bRotatingMiniature);
	Scene.Miniature->RotateInspection(FVector2D(120, 0));
	Scene.Step();
	TestFalse(TEXT("横向拖动确实改变捕获姿态"), Scene.Capture->GetActorTransform().Equals(InitialCapture, 0.001));
	const FQuat AfterHorizontal = Scene.Capture->GetActorQuat();
	Scene.Miniature->RotateInspection(FVector2D(0, 760));
	Scene.Step();
	const float VerticalAngle = FMath::RadiansToDegrees(AfterHorizontal.AngularDistance(Scene.Capture->GetActorQuat()));
	TestTrue(TEXT("垂直拖动可越过 90 度，不被探索俯仰范围夹紧"), VerticalAngle > 160.0f);
	TestTrue(TEXT("自由旋转保持捕获距离"), FMath::IsNearlyEqual(
		FVector::Distance(Scene.Capture->GetActorLocation(), Scene.Anchor->GetActorLocation()), CaptureDistance, 0.01f));
	TestEqual(TEXT("自由旋转保持进入前的捕获 FOV"), Scene.Capture->GetCaptureComponent2D()->FOVAngle, FOV);
	TestTrue(TEXT("旋转后捕获光轴仍穿过锚点"), Scene.Capture->GetActorForwardVector().Equals(
		(Scene.Anchor->GetActorLocation() - Scene.Capture->GetActorLocation()).GetSafeNormal(), 0.0001));
	TestTrue(TEXT("右键展示旋转不转动角色或改变重力"), Scene.Character->GetActorTransform().Equals(CharacterTransform, 0.001)
		&& Scene.Character->GetCharacterMovement()->GetGravityDirection().Equals(Gravity));
	TestTrue(TEXT("右键展示旋转不改变居中的主镜头"), FTransform(Scene.POV().Rotation, Scene.POV().Location).Equals(MainCamera, 0.001));
	TestTrue(TEXT("显示面正面始终朝玩家，未跟随捕获角度翻面"), Scene.Display->GetUpVector().Equals(-Scene.POV().Rotation.Vector(), 0.0001));
	const FTransform StoppedCapture = Scene.Capture->GetActorTransform();
	Scene.Miniature->RotateInspection(FVector2D::ZeroVector);
	Scene.Step();
	TestTrue(TEXT("鼠标停止时没有惯性漂移"), Scene.Capture->GetActorTransform().Equals(StoppedCapture, 0.0001));
	// 居中观察临时覆盖固定/圆柱面向，确保任意原有显示模式都不会因翻转变成背面。
	Scene.Miniature->DisplayFacingMode = EDreamMiniatureFacingMode::Fixed;
	Scene.Display->SetWorldRotation(FRotator::ZeroRotator);
	Scene.Step();
	TestTrue(TEXT("观察期间固定面向配置也临时朝向玩家"), Scene.Display->GetUpVector().Equals(-Scene.POV().Rotation.Vector(), 0.0001));
	Scene.Miniature->DisplayFacingMode = EDreamMiniatureFacingMode::FaceCamera;

	// 旋转后的实际捕获射线必须继续命中一个真实可交互 Actor，而非只验证数学函数返回成功。
	FVector DisplayHit, Origin, Direction;
	FString Reason;
	TestTrue(TEXT("自由旋转后中心点击仍能映射"), Scene.Miniature->TryMapViewRayToCaptureRay(
		MainCamera.GetLocation(), Ray, DisplayHit, Origin, Direction, Reason));
	TestTrue(TEXT("中心点击采用旋转后的捕获光轴"), Direction.Equals(Scene.Capture->GetActorForwardVector(), 0.002));
	AActor* Target = Scene.Box(Origin + Direction * 2000.0f, FVector(75), ECC_Visibility)->GetOwner();
	const FTransform TargetTransform = Target->GetActorTransform();
	UDreamPivotPointComponent* Pivot = NewObject<UDreamPivotPointComponent>(Target);
	Target->AddInstanceComponent(Pivot);
	Pivot->SetupAttachment(Target->GetRootComponent());
	Pivot->RegisterComponent();
	UDreamRotatableComponent* Rotation = NewObject<UDreamRotatableComponent>(Target);
	Target->AddInstanceComponent(Rotation);
	Rotation->RegisterComponent();
	Scene.Controller->BeginMiniatureRotationRay(MainCamera.GetLocation(), Ray);
	Scene.Controller->InteractWithMiniatureRay(MainCamera.GetLocation(), Ray);
	TestFalse(TEXT("按住右键时左键不会同时触发机关"), Rotation->IsRotating());
	Scene.Controller->EndMiniatureRotation();
	Scene.Controller->InteractWithMiniatureRay(MainCamera.GetLocation(), Ray);
	TestTrue(TEXT("松开右键后左键仍能操作看到的机关"), Rotation->IsRotating());
	Rotation->TickComponent(1.0f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("命中的真实机关确实执行了旋转"), Target->GetActorTransform().Equals(TargetTransform, 0.001));
	Scene.Miniature->DisplayFacingMode = EDreamMiniatureFacingMode::Fixed;
	Scene.Controller->SetMiniatureInteractionMode(false);
	TestTrue(TEXT("退出后恢复固定面向的配置旋转"), Scene.Display->GetRelativeRotation().Quaternion().Equals(
		Scene.Miniature->DisplayRelativeTransform.GetRotation(), 0.0001));
	const FTransform AfterExit = Scene.Capture->GetActorTransform();
	Scene.Miniature->RotateInspection(FVector2D(100, 100));
	TestTrue(TEXT("退出后右键展示接口不再修改捕获相机"), Scene.Capture->GetActorTransform().Equals(AfterExit, 0.001));
	return true;
}

/**
 * GPU 验证在独立 -game 进程运行，使用目标关卡的真实游戏主视口。
 * 只在指定自动化命令启动时改变本次运行时观察状态；不打开/保存编辑器地图，也不生成资产。
 * 本测试负责导出主镜头画面和检查运行时状态，像素与手感仍需查看截图/实际操作。
 */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FDreamMiniatureRenderStep, TFunction<bool()>, Step);
bool FDreamMiniatureRenderStep::Update()
{
	return Step();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMiniatureGameplayRenderTest,
	"DreamSpace.Presentation.Miniature.GameplayRender",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FDreamMiniatureGameplayRenderTest::RunTest(const FString& Parameters)
{
	ADreamPlayerController* Controller = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (Context.WorldType == EWorldType::Game && Context.World())
			Controller = Cast<ADreamPlayerController>(Context.World()->GetFirstPlayerController());
	ADreamCharacter* Character = Controller ? Cast<ADreamCharacter>(Controller->GetPawn()) : nullptr;
	if (!TestNotNull(TEXT("独立游戏进程存在原生 DreamCharacter"), Character))
		return false;
	Controller->SetMiniatureInteractionMode(false);
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/MiniatureInspection"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	struct FRenderState
	{
		int32 Stage = 0;
		double Started = FPlatformTime::Seconds();
		double Since = Started;
		FDateTime StartedUTC = FDateTime::UtcNow();
		FString LastScreenshot;
	};
	const TSharedRef<FRenderState> State = MakeShared<FRenderState>();
	const TWeakObjectPtr<ADreamPlayerController> WeakController = Controller;
	const TWeakObjectPtr<ADreamCharacter> WeakCharacter = Character;
	const TFunction<bool()> Step = [this, State, Directory, WeakController, WeakCharacter]()
	{
		ADreamPlayerController* CurrentController = WeakController.Get();
		ADreamCharacter* CurrentCharacter = WeakCharacter.Get();
		const double Now = FPlatformTime::Seconds();
		if (!CurrentController || !CurrentCharacter || Now - State->Started > 90.0)
		{
			if (CurrentController)
				CurrentController->SetMiniatureInteractionMode(false);
			AddError(TEXT("主视口渲染检查丢失角色或等待截图超时"));
			return true;
		}
		if (Now - State->Since < 2.0)
			return false;
		// 奇数阶段等待渲染线程真正保存新截图，不能用先前运行遗留的同名文件判定成功。
		if (State->Stage % 2 == 1 && IFileManager::Get().GetTimeStamp(*State->LastScreenshot) < State->StartedUTC)
			return false;
		auto Screenshot = [&](const TCHAR* Name)
		{
			State->LastScreenshot = Directory / Name;
			FScreenshotRequest::RequestScreenshot(State->LastScreenshot, false, false);
		};
		switch (State->Stage)
		{
		case 0:
			Screenshot(TEXT("Exploration.png"));
			break;
		case 1:
			CurrentController->SetMiniatureInteractionMode(true);
			TestTrue(TEXT("实际游戏主视口进入 Tab 居中观察"), CurrentController->IsMiniatureInteractionMode());
			break;
		case 2:
			Screenshot(TEXT("Centered.png"));
			break;
		case 3:
			// 捕获像素核对使用已由逻辑回归覆盖的展示旋转入口，无需依赖无窗口进程的鼠标焦点。
			CurrentCharacter->SceneMiniature->RotateInspection(FVector2D(200, 140));
			break;
		case 4:
			Screenshot(TEXT("Rotated.png"));
			break;
		case 5:
			CurrentController->SetMiniatureInteractionMode(false);
			TestFalse(TEXT("实际游戏退出后解除移动与视角锁"), CurrentController->IsMoveInputIgnored() || CurrentController->IsLookInputIgnored());
			break;
		case 6:
			Screenshot(TEXT("Restored.png"));
			break;
		case 7:
			AddInfo(FString::Printf(TEXT("主视口截图已保存：%s"), *Directory));
			return true;
		}
		++State->Stage;
		State->Since = Now;
		return false;
	};
	ADD_LATENT_AUTOMATION_COMMAND(FDreamMiniatureRenderStep(Step));
	return true;
}
#endif
