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
			// 本套件验证已持有手办后的观察；解锁前的空手状态由密码箱套件单独覆盖。
			Character->AcquireMiniature();
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

		void Step(float DeltaTime = 1.0f / 60.0f)
		{
			Camera->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
			Miniature->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
		}

		/** 模拟真实 Tick 顺序和累计时间；投影/拾取断言要在 Tab 的构图动画结束后执行。 */
		void Advance(float Seconds, int32 FPS = 60)
		{
			for (int32 Frame = 0; Frame < FMath::RoundToInt(Seconds * FPS); ++Frame)
				Step(1.0f / FPS);
		}

		void Settle()
		{
			Advance(Camera->MiniatureTransitionDuration + 1.0f / 60.0f);
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
	TestTrue(TEXT("进入 Tab 的当帧保持当前探索姿态"), FTransform(Scene.POV().Rotation, Scene.POV().Location).Equals(Exploration, 0.01));
	Scene.Settle();
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
	TestTrue(TEXT("退出 Tab 的当帧保持当前聚焦姿态"), FTransform(Scene.POV().Rotation, Scene.POV().Location).Equals(
		FTransform(Focus.Rotation, Focus.Location), 0.01));
	Scene.Settle();
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMiniatureFocusTransitionTest,
	"DreamSpace.Camera.Miniature.LookDownAndSmoothTransitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMiniatureFocusTransitionTest::RunTest(const FString& Parameters)
{
	FMiniatureInspectionFixture Scene;
	const FTransform Exploration(Scene.POV().Rotation, Scene.POV().Location);
	const FTransform ExplorationCapture = Scene.Capture->GetActorTransform();
	Scene.Controller->SetMiniatureInteractionMode(true);
	TestTrue(TEXT("主镜头进入时不直接跳到终点"), FTransform(Scene.POV().Rotation, Scene.POV().Location).Equals(Exploration, 0.01));
	TestTrue(TEXT("捕获角度进入时也不跳变"), Scene.Capture->GetActorTransform().Equals(ExplorationCapture, 0.01));
	TestTrue(TEXT("Tab 开始播放有限时长过渡"), Scene.Camera->IsMiniatureCameraBlending());
	const FVector BeginRay = Scene.Miniature->GetDisplayCenter() - Scene.POV().Location;
	TestFalse(TEXT("镜头自动构图期间不开始右键抓取"), Scene.Controller->BeginMiniatureRotationRay(Scene.POV().Location, BeginRay));
	Scene.Step(Scene.Camera->MiniatureTransitionDuration * 0.5f);
	const FTransform EnterMiddle(Scene.POV().Rotation, Scene.POV().Location);
	TestFalse(TEXT("经过半个过渡时长主镜头已连续移动"), EnterMiddle.Equals(Exploration, 0.01));
	TestFalse(TEXT("经过半个过渡时长捕获取景也连续改变"), Scene.Capture->GetActorTransform().Equals(ExplorationCapture, 0.01));
	Scene.Settle();
	const FTransform Focus(Scene.POV().Rotation, Scene.POV().Location);
	TestFalse(TEXT("中间帧没有提前抵达聚焦终点"), EnterMiddle.Equals(Focus, 0.01));
	TestFalse(TEXT("到达聚焦终点后自动结束过渡"), Scene.Camera->IsMiniatureCameraBlending());
	const FVector Up = -Scene.Character->GetCharacterMovement()->GetGravityDirection();
	const float ExpectedDown = -FMath::Sin(FMath::DegreesToRadians(Scene.Camera->MiniatureLookDownAngle));
	TestTrue(TEXT("主镜头按照配置角度稍向下俯视"), FMath::IsNearlyEqual(FVector::DotProduct(Focus.GetRotation().GetForwardVector(), Up), ExpectedDown, 0.0001));
	TestTrue(TEXT("实际捕获取景也呈现同样的俯视角度"), FMath::IsNearlyEqual(FVector::DotProduct(Scene.Capture->GetActorForwardVector(), Up), ExpectedDown, 0.0001));
	TestTrue(TEXT("俯视时主镜头位于手办中心上方"), FVector::DotProduct(Focus.GetLocation() - Scene.Miniature->GetDisplayCenter(), Up) > 0.0);

	Scene.Miniature->RotateInspection(FVector2D(160, 90));
	const FTransform RotatedCapture = Scene.Capture->GetActorTransform();
	Scene.Controller->SetMiniatureInteractionMode(false);
	TestTrue(TEXT("返回探索的当帧主镜头保持原姿态"), FTransform(Scene.POV().Rotation, Scene.POV().Location).Equals(Focus, 0.01));
	TestTrue(TEXT("返回探索的当帧手办画面保持展示角度"), Scene.Capture->GetActorTransform().Equals(RotatedCapture, 0.01));
	Scene.Step(Scene.Camera->MiniatureTransitionDuration * 0.5f);
	const FTransform ReturnMiddle(Scene.POV().Rotation, Scene.POV().Location);
	const FTransform ReturnCapture = Scene.Capture->GetActorTransform();
	TestFalse(TEXT("返回探索时主镜头经过中间帧"), ReturnMiddle.Equals(Focus, 0.01) || ReturnMiddle.Equals(Exploration, 0.01));
	TestTrue(TEXT("右键旋转后的退出轨道仍对准场景锚点"), Scene.Capture->GetActorForwardVector().Equals(
		(Scene.Anchor->GetActorLocation() - Scene.Capture->GetActorLocation()).GetSafeNormal(), 0.0001));
	// 返回过程中再次按 Tab，主镜头和捕获镜头都从正在显示的中间帧重新开始。
	Scene.Controller->SetMiniatureInteractionMode(true);
	TestTrue(TEXT("连续切换 Tab 不跳回上次起点"), FTransform(Scene.POV().Rotation, Scene.POV().Location).Equals(ReturnMiddle, 0.01));
	TestTrue(TEXT("连续切换 Tab 保持捕获画面连续"), Scene.Capture->GetActorTransform().Equals(ReturnCapture, 0.01));
	Scene.Settle();
	Scene.Controller->SetMiniatureInteractionMode(false);
	// 玩家在返回动画期间已可恢复探索，最终相机应追踪新的控制旋转，而不是返回一份过期的快照。
	Scene.Controller->SetControlRotation(FRotator(-10, 25, 0));
	Scene.Settle();
	TestTrue(TEXT("退出终点跟随玩家最新探索视角"), Scene.POV().Rotation.Quaternion().Equals(Scene.Controller->GetControlRotation().Quaternion(), 0.0001));
	TestTrue(TEXT("退出终点采用当前越肩构图"), Scene.POV().Location.Equals(Scene.Camera->GetIdealCameraTransform(Scene.Camera->GetSmoothedArmLength()).GetLocation(), 0.01));

	Scene.Controller->SetControlRotation(FRotator::ZeroRotator);
	Scene.Step();
	const FVector OpenExploration = Scene.POV().Location;
	Scene.Controller->SetMiniatureInteractionMode(true);
	Scene.Settle();
	const FVector OpenFocus = Scene.POV().Location;
	Scene.Controller->SetMiniatureInteractionMode(false);
	Scene.Settle();
	// 把障碍放在两个安全终点之间；独立球重叠检查确认动画不会插值进入墙体。
	UBoxComponent* Wall = Scene.Box((OpenExploration + OpenFocus) * 0.5f, FVector(3), ECC_Camera);
	Scene.Controller->SetMiniatureInteractionMode(true);
	Scene.Step(Scene.Camera->MiniatureTransitionDuration * 0.5f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DreamMiniatureTransitionCollisionTest), false, Scene.Character);
	TestTrue(TEXT("过渡中间位置执行真实 Camera 避障"), Scene.Camera->IsCollisionFixApplied());
	TestFalse(TEXT("过渡中间帧相机球不留在墙内"), Scene.World->OverlapBlockingTestByChannel(
		Scene.POV().Location, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(Scene.Camera->ProbeSize), Params));
	Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Scene.Settle();
	Scene.Controller->SetMiniatureInteractionMode(false, false);
	Scene.Controller->SetMiniatureInteractionMode(true);
	Scene.Advance(0.1f);
	const FTransform BeforeGravity(Scene.POV().Rotation, Scene.POV().Location);
	const FQuat Turn(FVector::ForwardVector, HALF_PI);
	const FVector Translation(40, 20, 0);
	Scene.Character->SetActorLocationAndRotation(Translation, Turn);
	Scene.Character->GetCharacterMovement()->SetGravityDirection(Turn.RotateVector(FVector::DownVector));
	Scene.Step(0.0f);
	TestTrue(TEXT("过渡中角色搬运时起点和目标一起转移"), Scene.POV().Location.Equals(Translation + Turn.RotateVector(BeforeGravity.GetLocation()), 0.01)
		&& Scene.POV().Rotation.Quaternion().Equals(Turn * BeforeGravity.GetRotation(), 0.0001));

	// 在相同累计时间比较真实 Socket，覆盖 30/60/120 FPS，不复制动画进度公式作为预期结果。
	TArray<FTransform> Samples;
	TArray<FTransform> CaptureSamples;
	for (int32 FPS : {30, 60, 120})
	{
		FMiniatureInspectionFixture TimedScene;
		TimedScene.Controller->SetMiniatureInteractionMode(true);
		TimedScene.Advance(0.1f, FPS);
		Samples.Add(FTransform(TimedScene.POV().Rotation, TimedScene.POV().Location));
		CaptureSamples.Add(TimedScene.Capture->GetActorTransform());
	}
	for (int32 Index = 1; Index < Samples.Num(); ++Index)
	{
		TestTrue(TEXT("不同帧率的主镜头过渡保持同样进度"), Samples[0].Equals(Samples[Index], 0.01));
		TestTrue(TEXT("不同帧率的捕获取景过渡保持同样进度"), CaptureSamples[0].Equals(CaptureSamples[Index], 0.01));
	}
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
			Scene.Settle();
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
			Scene.Camera->EndMiniatureFocus(false);
			Scene.Miniature->EndInspection(false);
		}
	}

	Scene.Character->FollowCamera->AspectRatio = 16.0f / 9.0f;
	Scene.Miniature->BeginInspection();
	Scene.Camera->BeginMiniatureFocus(Scene.Miniature, Scene.POV());
	Scene.Settle();
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
	Scene.Settle();
	const FTransform MainCamera(Scene.POV().Rotation, Scene.POV().Location);
	const FTransform CharacterTransform = Scene.Character->GetActorTransform();
	const FVector Gravity = Scene.Character->GetCharacterMovement()->GetGravityDirection();
	const FTransform InitialCapture = Scene.Capture->GetActorTransform();
	// 同一段斜拖按不同帧率采样，最终角度应一致；直接比较最终捕获姿态，不复制旋转实现。
	Scene.Miniature->RotateInspection(FVector2D(120, 90));
	const FQuat SingleFrameTurn = Scene.Capture->GetActorQuat();
	Scene.Controller->SetMiniatureInteractionMode(false);
	Scene.Settle();
	Scene.Controller->SetMiniatureInteractionMode(true);
	Scene.Settle();
	for (int32 Frame = 0; Frame < 12; ++Frame)
		Scene.Miniature->RotateInspection(FVector2D(10, 7.5));
	TestTrue(TEXT("同一斜拖拆成多帧仍得到同一展示角度"), Scene.Capture->GetActorQuat().Equals(SingleFrameTurn, 0.0001));
	Scene.Controller->SetMiniatureInteractionMode(false);
	Scene.Settle();
	Scene.Controller->SetMiniatureInteractionMode(true);
	Scene.Settle();
	const float CaptureDistance = FVector::Distance(Scene.Capture->GetActorLocation(), Scene.Anchor->GetActorLocation());
	const float FOV = Scene.Capture->GetCaptureComponent2D()->FOVAngle;
	const FVector Ray = Scene.Miniature->GetDisplayCenter() - MainCamera.GetLocation();
	TestTrue(TEXT("右键可从手办显示面内抓取"), Scene.Controller->BeginMiniatureRotationRay(MainCamera.GetLocation(), Ray));
	Scene.Controller->UpdateMiniatureRotation();
	TestFalse(TEXT("没有焦点/右键的窗口会立即清理旋转会话"), Scene.Controller->bRotatingMiniature);
	Scene.Miniature->RotateInspection(FVector2D(120, 0));
	Scene.Step();
	TestFalse(TEXT("横向拖动确实改变捕获姿态"), Scene.Capture->GetActorTransform().Equals(InitialCapture, 0.001));
	// 在锚点靠近镜头的一侧选取模型表面参考点，检查其屏幕运动方向。
	// 只比较捕获姿态是否变化无法发现左右反向；相机局部 +Y 为屏幕右、+Z 为屏幕上。
	const FVector HorizontalProbe = Scene.Anchor->GetActorLocation()
		- InitialCapture.GetRotation().GetForwardVector() * CaptureDistance * 0.25f;
	const FVector AfterRightDrag = Scene.Capture->GetActorTransform().InverseTransformPosition(HorizontalProbe);
	TestTrue(TEXT("向右拖动时模型表面在画面中向右移动"), AfterRightDrag.X > 0.0 && AfterRightDrag.Y > 0.0);
	const FQuat AfterHorizontal = Scene.Capture->GetActorQuat();
	const FVector VerticalProbe = Scene.Anchor->GetActorLocation()
		- AfterHorizontal.GetForwardVector() * CaptureDistance * 0.25f;
	Scene.Miniature->RotateInspection(FVector2D(0, 120));
	const FVector AfterDownDrag = Scene.Capture->GetActorTransform().InverseTransformPosition(VerticalProbe);
	TestTrue(TEXT("向下拖动仍让模型表面在画面中向下移动"), AfterDownDrag.X > 0.0 && AfterDownDrag.Z < 0.0);
	// 撤销本次小角度垂直检查，继续使用原来的大角度输入验证越过极点。
	Scene.Miniature->RotateInspection(FVector2D(0, -120));
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
	Scene.Settle();
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
	// 此渲染入口专门验收手办构图，显式准备已获得状态，正常游戏仍需要打开密码箱。
	Character->AcquireMiniature();
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
		// 进入/退出后的下一阶段在动画中途抓图，其它阶段等待两秒让目标姿态与 GPU 画面稳定。
		const double Delay = (State->Stage == 2 || State->Stage == 7) ? 0.12 : 2.0;
		if (Now - State->Since < Delay)
			return false;
		// 每次请求后等待渲染线程真正保存新截图，不能用先前运行遗留的同名文件判定成功。
		if (!State->LastScreenshot.IsEmpty())
		{
			if (IFileManager::Get().GetTimeStamp(*State->LastScreenshot) < State->StartedUTC)
				return false;
			State->LastScreenshot.Reset();
		}
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
			Screenshot(TEXT("EnterTransition.png"));
			break;
		case 3:
			Screenshot(TEXT("Centered.png"));
			break;
		case 4:
			// 捕获像素核对使用已由逻辑回归覆盖的展示旋转入口，无需依赖无窗口进程的鼠标焦点。
			CurrentCharacter->SceneMiniature->RotateInspection(FVector2D(200, 140));
			break;
		case 5:
			Screenshot(TEXT("Rotated.png"));
			break;
		case 6:
			CurrentController->SetMiniatureInteractionMode(false);
			TestFalse(TEXT("实际游戏退出后解除移动与视角锁"), CurrentController->IsMoveInputIgnored() || CurrentController->IsLookInputIgnored());
			break;
		case 7:
			Screenshot(TEXT("ExitTransition.png"));
			break;
		case 8:
			Screenshot(TEXT("Restored.png"));
			break;
		case 9:
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
