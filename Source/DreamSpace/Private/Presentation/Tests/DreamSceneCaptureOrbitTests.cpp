#include "DreamSceneCapturePresentationComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Math/RotationMatrix.h"
#include "DreamCharacter.h"
#include "DreamPlayerController.h"
#include "DreamSceneCaptureAnchor.h"
#include "DreamPivotPointComponent.h"
#include "DreamRotatableComponent.h"
#include "DreamTranslatableComponent.h"
#include "HAL/IConsoleManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "StaticMeshResources.h"

namespace
{
	using FComponent = UDreamSceneCapturePresentationComponent;

	/** 观察者到面片的单位视线，和实现使用同一个定义。 */
	FVector ViewDirectionOf(const FTransform& Observer, const FTransform& Frame)
	{
		return (Frame.GetLocation() - Observer.GetLocation()).GetSafeNormal();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSceneCaptureWindowTest,
	"DreamSpace.Presentation.SceneCaptureWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSceneCaptureWindowTest::RunTest(const FString& Parameters)
{
	const FTransform SceneReference(FRotator(0.0f, 35.0f, 0.0f), FVector(120.0f, -60.0f, 30.0f));
	const FTransform Frame(FRotator(0.0f, 180.0f, 0.0f), FVector(75.0f, 55.0f, 95.0f));
	const FTransform Observer(FRotator(-20.0f, 10.0f, 0.0f), FVector(-300.0f, 0.0f, 200.0f));
	constexpr float Scale = 0.03f;

	const FTransform Capture = FComponent::MapObserverWindowToCaptureWorld(
		Observer, Frame, SceneReference, Scale);
	const FVector ViewDirection = ViewDirectionOf(Observer, Frame);
	const float ObserverDistance = FVector::Distance(Observer.GetLocation(), Frame.GetLocation());

	// 光轴穿过面片中心：捕获相机沿视线看向锚点，锚点落在画面正中。
	TestTrue(TEXT("Capture looks along the eye-to-plane line of sight"),
		Capture.GetRotation().GetForwardVector().Equals(ViewDirection, 0.001f));
	TestTrue(TEXT("Capture is aimed at the anchor"),
		Capture.GetRotation().GetForwardVector().Equals(
			(SceneReference.GetLocation() - Capture.GetLocation()).GetSafeNormal(), 0.001f));

	// 取景距离 = 眼睛到面片的距离 ÷ 比例。观察者相对面片中心的偏移
	// 与捕获相机相对锚点的偏移方向相同；两个局部窗口坐标系按比例对应。
	TestTrue(TEXT("Window distance is the observer distance divided by the scale"),
		FMath::IsNearlyEqual(FVector::Distance(Capture.GetLocation(), SceneReference.GetLocation()),
			ObserverDistance / Scale, 0.05f));
	TestTrue(TEXT("Observer and capture have matching offsets from their window centers"),
		(Observer.GetLocation() - Frame.GetLocation()).GetSafeNormal().Equals(
			(Capture.GetLocation() - SceneReference.GetLocation()).GetSafeNormal(), 0.001f));

	// 眼睛离面片越远，等效相机退得越远，但视角方向不变。
	FTransform FartherObserver = Observer;
	FartherObserver.SetLocation(Observer.GetLocation() - ViewDirection * 500.0f);
	const FTransform FartherCapture = FComponent::MapObserverWindowToCaptureWorld(
		FartherObserver, Frame, SceneReference, Scale);
	TestTrue(TEXT("Backing the observer away keeps the capture direction"),
		FartherCapture.GetRotation().Equals(Capture.GetRotation(), 0.001f));
	TestTrue(TEXT("Backing the observer away pushes the capture camera farther"),
		FVector::Distance(FartherCapture.GetLocation(), SceneReference.GetLocation()) >
		FVector::Distance(Capture.GetLocation(), SceneReference.GetLocation()));

	// 角色整体平移：手办与相机同步移动，观察关系不变，画面不应变化。
	FTransform MovedFrame = Frame;
	FTransform MovedObserver = Observer;
	MovedFrame.AddToTranslation(FVector(-500.0f, 700.0f, 100.0f));
	MovedObserver.AddToTranslation(FVector(-500.0f, 700.0f, 100.0f));
	const FTransform AfterTranslation = FComponent::MapObserverWindowToCaptureWorld(
		MovedObserver, MovedFrame, SceneReference, Scale);
	TestTrue(TEXT("Moving the character does not move the capture camera"),
		AfterTranslation.GetLocation().Equals(Capture.GetLocation(), 0.05f));
	TestTrue(TEXT("Moving the character does not rotate the capture camera"),
		AfterTranslation.GetRotation().Equals(Capture.GetRotation(), 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSceneCaptureWindowRayTest,
	"DreamSpace.Presentation.SceneCaptureWindowRays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSceneCaptureWindowRayTest::RunTest(const FString& Parameters)
{
	// 窗口成立的定义：眼睛透过面片上任意一点看到的方向，必须等于捕获相机到
	// 对应真实点的方向。少了任一条件（方向、距离、视场角），这条就不成立。
	const FTransform SceneReference(FRotator(0.0f, -50.0f, 0.0f), FVector(-200.0f, 80.0f, 40.0f));
	const FTransform Frame(FRotator::ZeroRotator, FVector(75.0f, 55.0f, 95.0f));
	const FVector ObserverLocation(120.0f, -140.0f, 260.0f);
	const FTransform Observer((Frame.GetLocation() - ObserverLocation).Rotation(), ObserverLocation);

	for (const float Scale : { 0.03f, 0.1f, 0.5f })
	{
		const FTransform Capture = FComponent::MapObserverWindowToCaptureWorld(
			Observer, Frame, SceneReference, Scale);
		const FVector Axis = Capture.GetRotation().GetForwardVector();
		const FVector Right = Capture.GetRotation().GetRightVector();
		const FVector Up = Capture.GetRotation().GetUpVector();
		const FVector EyeToPlane = Frame.GetLocation() - ObserverLocation;

		// 面片上取样若干点，包含四角与中心。
		for (const FVector2D& Offset : {
			FVector2D(0.0f, 0.0f), FVector2D(40.0f, 0.0f), FVector2D(-40.0f, 0.0f),
			FVector2D(0.0f, 40.0f), FVector2D(40.0f, 40.0f), FVector2D(-40.0f, -40.0f) })
		{
			const FVector PlanePoint = Frame.GetLocation() + Right * Offset.X + Up * Offset.Y;
			// 面片上的点按比例放大后，落在真实场景中的对应位置。
			const FVector RealPoint =
				SceneReference.GetLocation() + (Right * Offset.X + Up * Offset.Y) / Scale;

			const FVector EyeRay = (PlanePoint - ObserverLocation).GetSafeNormal();
			const FVector CaptureRay = (RealPoint - Capture.GetLocation()).GetSafeNormal();
			TestTrue(TEXT("Eye ray through the plane matches the capture ray to the scaled point"),
				EyeRay.Equals(CaptureRay, 0.002f));
		}

		// 面片左侧边缘朝向眼睛的方向，与光轴的夹角等于视场角的一半。
		const float HalfAngleDegrees = FMath::RadiansToDegrees(
			FMath::Acos(FMath::Clamp(((Frame.GetLocation() - Right * 40.0f) - ObserverLocation)
				.GetSafeNormal() | Axis, -1.0, 1.0)));
		const float ExpectedHalfFOV = 0.5f * FComponent::ComputeWindowFieldOfView(80.0f, EyeToPlane.Size());
		TestTrue(TEXT("Plane half-extent subtends half the window FOV"),
			FMath::IsNearlyEqual(HalfAngleDegrees, ExpectedHalfFOV, 0.05f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMiniatureClickRayTest,
	"DreamSpace.Presentation.MiniatureClickRays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMiniatureClickRayTest::RunTest(const FString& Parameters)
{
	const FBox Bounds(FVector(-50, -50, 0), FVector(50, 50, 0));
	const FVector Camera(30, -20, 400);
	FVector Hit;
	FVector2D UV;
	FString Reason;
	// 在实际相机偏离平面中心、面片非等比缩放及绕法线旋转时，纹理坐标仍须稳定。
	for (float Angle : { 0.0f, 180.0f })
	{
		const FTransform Display(FRotator(0, Angle, 0), FVector(15, 20, 60), FVector(0.8, 1.2, 1));
		for (const FVector2D Expected : { FVector2D(0.5, 0.5), FVector2D(0, 0), FVector2D(1, 1), FVector2D(0.25, 0.75) })
		{
			const FVector Point = Display.TransformPosition(FVector(Expected.X * 100 - 50, Expected.Y * 100 - 50, 0));
			TestTrue(TEXT("Visible point maps under scaling and image rotation"),
				FComponent::MapViewRayToDisplayUV(Camera, Point - Camera, Display, Bounds, Hit, UV, Reason));
			TestTrue(TEXT("UV follows the actual displayed image without double flipping"), UV.Equals(Expected, 0.0001));
			TestTrue(TEXT("Physical hit is preserved for foreground occlusion"), Hit.Equals(Point, 0.001));
		}
	}
	TestFalse(TEXT("Outside display rejected"), FComponent::MapViewRayToDisplayUV(
		Camera, FVector(100, 0, 0) - Camera, FTransform::Identity, Bounds, Hit, UV, Reason));
	TestFalse(TEXT("Failure explains why mapping stopped"), Reason.IsEmpty());
	TestFalse(TEXT("Back face rejected"), FComponent::MapViewRayToDisplayUV(
		FVector(0, 0, -100), FVector::UpVector, FTransform::Identity, Bounds, Hit, UV, Reason));
	TestFalse(TEXT("Parallel ray rejected"), FComponent::MapViewRayToDisplayUV(
		Camera, FVector::ForwardVector, FTransform::Identity, Bounds, Hit, UV, Reason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMiniatureConfiguredProjectionTest,
	"DreamSpace.Presentation.MiniatureConfiguredProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMiniatureConfiguredProjectionTest::RunTest(const FString& Parameters)
{
	// 回归最终运行时入口：直接生成原生 ADreamCharacter，避免测试继续依赖旧角色蓝图。
	// 该测试仍使用瞬时世界，不读取或保存用户关卡；手办显示配置由 C++ 默认对象提供。
	UClass* CharacterClass = ADreamCharacter::StaticClass();
	if (!TestNotNull(TEXT("Native project character class exists"), CharacterClass))
		return false;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ADreamCharacter* Character = World->SpawnActor<ADreamCharacter>(CharacterClass);
	if (!TestNotNull(TEXT("Character spawns in isolated world"), Character))
	{
		World->DestroyWorld(false);
		return false;
	}
	World->SpawnActor<ADreamSceneCaptureAnchor>();
	FComponent* Miniature = Character->SceneMiniature;
	TestNotNull(TEXT("Native character has a miniature presentation component"), Miniature);
	if (!Miniature)
	{
		World->DestroyWorld(false);
		return false;
	}
	// 这些值原先由 BP_DreamCharacter 覆盖；迁移到 C++ 后必须保持一致，
	// 否则会改变真实项目的手办清晰度和 RenderTarget 长宽比。
	TestEqual(TEXT("Native character preserves miniature RT width"), Miniature->RenderTargetWidth, 2200);
	TestEqual(TEXT("Native character preserves miniature RT height"), Miniature->RenderTargetHeight, 2500);
	// 显示与拾取数学的回归使用已获得状态；开局隐藏及 Tab 门禁由密码箱套件验证。
	Character->AcquireMiniature();
	Miniature->BeginPlay();
	UStaticMeshComponent* Display = nullptr;
	TArray<UStaticMeshComponent*> Meshes;
	Character->GetComponents(Meshes);
	for (UStaticMeshComponent* Mesh : Meshes)
		if (Mesh->GetFName() == TEXT("SceneCaptureDisplayMesh"))
			Display = Mesh;
	ASceneCapture2D* Capture = nullptr;
	for (AActor* Actor : World->PersistentLevel->Actors)
		if (ASceneCapture2D* Candidate = Cast<ASceneCapture2D>(Actor))
			Capture = Candidate;
	if (!TestNotNull(TEXT("Real presentation creates display"), Display) ||
		!TestNotNull(TEXT("Real presentation creates capture"), Capture))
	{
		World->DestroyWorld(false);
		return false;
	}

	// 直接检查默认资源的顶点 UV，防止数学推导与真实 Plane 的 UV 朝向相反。
	const FStaticMeshLODResources& LOD = Display->GetStaticMesh()->GetRenderData()->LODResources[0];
	const FBox Bounds = Display->GetStaticMesh()->GetBoundingBox();
	for (uint32 Index = 0; Index < LOD.VertexBuffers.PositionVertexBuffer.GetNumVertices(); ++Index)
	{
		const FVector Vertex(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Index));
		const FVector2f MeshUV = LOD.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index, 0);
		FVector Hit;
		FVector2D UV;
		FString Reason;
		TestTrue(TEXT("Plane vertex is inside analytical click bounds"), FComponent::MapViewRayToDisplayUV(
			Vertex + FVector(0, 0, 100), -FVector::UpVector, FTransform::Identity, Bounds, Hit, UV, Reason));
		TestTrue(TEXT("Analytical UV agrees with actual Engine Plane UV0"), UV.Equals(FVector2D(MeshUV), 0.001));
	}

	Capture->SetActorLocationAndRotation(FVector(10000, -3000, 2000), FRotator(-15, 35, 0));
	USceneCaptureComponent2D* CaptureComponent = Capture->GetCaptureComponent2D();
	CaptureComponent->FOVAngle = 45;
	const FTransform CaptureTransform = CaptureComponent->GetComponentTransform();
	const FTransform DisplayTransform(FRotationMatrix::MakeFromXZ(
		Capture->GetActorRightVector(), -Capture->GetActorForwardVector()).ToQuat(), FVector(75, 55, 95), FVector(0.8));
	Display->SetWorldTransform(DisplayTransform);
	const FVector Camera = DisplayTransform.GetLocation() - Capture->GetActorForwardVector() * 350 + FVector(0, 10, 30);

	// 同时覆盖实际蓝图尺寸及方形/横向/纵向 RT；期望方向独立使用透视相机解析式计算。
	for (const FIntPoint Size : { FIntPoint(Miniature->RenderTargetWidth, Miniature->RenderTargetHeight),
		FIntPoint(1024, 1024), FIntPoint(2500, 2200), FIntPoint(2200, 2500) })
	{
		Miniature->GetRenderTarget()->ResizeTarget(Size.X, Size.Y);
		for (const FVector2D UV : { FVector2D(0.5, 0.5), FVector2D(0.25, 0.75), FVector2D(0.8, 0.2) })
		{
			const FVector Point = Display->GetComponentTransform().TransformPosition(FVector(UV.X * 100 - 50, UV.Y * 100 - 50, 0));
			FVector Hit, Origin, Direction;
			FString Reason;
			const bool bMapped = Miniature->TryMapViewRayToCaptureRay(Camera, Point - Camera, Hit, Origin, Direction, Reason);
			TestTrue(FString::Printf(TEXT("Runtime mapping accepts %dx%d: %s"), Size.X, Size.Y, *Reason), bMapped);
			const double HalfTan = FMath::Tan(FMath::DegreesToRadians(22.5));
			const FVector Expected = CaptureTransform.TransformVectorNoScale(FVector(1,
				(2 * UV.X - 1) * HalfTan, (1 - 2 * UV.Y) * HalfTan * Size.Y / Size.X)).GetSafeNormal();
			// UE 反投影把像素取整，边缘允许不到一个像素的误差。
			TestTrue(TEXT("Ray matches render projection including portrait aspect ratio"), Direction.Equals(Expected, 0.002));
			TestTrue(TEXT("Ray starts at capture optical center"), Origin.Equals(Capture->GetActorLocation(), 0.001));
		}
	}
	// 复用鼠标事件真正调用的控制器路径；通过真实碰撞命中并检查组件运动后的姿态，
	// 避免“映射函数返回 true”被误当成“世界已经响应”。整个场景只存在于测试内存中。
	ADreamPlayerController* Controller = World->SpawnActor<ADreamPlayerController>();
	Controller->Possess(Character);
	auto SpawnCube = [World](const FVector& Location)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Actor);
		Actor->SetRootComponent(Mesh);
		Actor->AddInstanceComponent(Mesh);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Mesh->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	};
	const FVector TargetLocation = Capture->GetActorLocation() + Capture->GetActorForwardVector() * 2000;
	AActor* Target = SpawnCube(TargetLocation);
	UDreamPivotPointComponent* Pivot = NewObject<UDreamPivotPointComponent>(Target);
	Target->AddInstanceComponent(Pivot);
	Pivot->SetupAttachment(Target->GetRootComponent());
	Pivot->RegisterComponent();
	UDreamRotatableComponent* Rotation = NewObject<UDreamRotatableComponent>(Target);
	Target->AddInstanceComponent(Rotation);
	Rotation->RegisterComponent();
	UDreamTranslatableComponent* Translation = NewObject<UDreamTranslatableComponent>(Target);
	Target->AddInstanceComponent(Translation);
	Translation->RegisterComponent();
	const FVector ClickDirection = Display->GetComponentLocation() - Camera;

	// 屏幕诊断不需要真正的鼠标才能验证：开启 CVar 后必须在成功和失败路径都有结果。
	IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("dream.DebugMiniatureInteraction"));
	const bool bPreviousDebug = Debug->GetBool();
	Debug->Set(true, ECVF_SetByCode);
	Controller->InteractWithMiniatureRay(Camera, ClickDirection);
	TestTrue(TEXT("Click through portrait RT starts rotation"), Rotation->IsRotating());
	TestTrue(TEXT("Same hit dispatches translation component"), Translation->IsTranslating());
	Rotation->TickComponent(1.0f, LEVELTICK_All, nullptr);
	Translation->TickComponent(1.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("World actor really rotated 90 degrees"), Target->GetActorRotation().Equals(FRotator(0, 90, 0), 0.01));
	TestTrue(TEXT("World actor really translated 100 cm"), Target->GetActorLocation().Equals(TargetLocation + FVector(100, 0, 0), 0.01));
	FVector2D DebugPosition;
	FString DebugMessage;
	FLinearColor DebugColor;
	TestTrue(TEXT("Successful click has visible HUD feedback"), Controller->GetMiniatureClickDebug(DebugPosition, DebugMessage, DebugColor));
	TestTrue(TEXT("Successful dispatch reports green"), DebugColor.Equals(FLinearColor(FColor::Green)));

	// 将机关复位，再验证近处遮挡、世界首个非交互遮挡，以及捕获黑名单的行为。
	Target->SetActorLocationAndRotation(TargetLocation, FRotator::ZeroRotator);
	AActor* Blocker = SpawnCube((Camera + Display->GetComponentLocation()) * 0.5);
	Controller->InteractWithMiniatureRay(Camera, ClickDirection);
	TestFalse(TEXT("Foreground obstruction prevents dispatch"), Rotation->IsRotating());
	TestTrue(TEXT("Blocked click also has HUD feedback"), Controller->GetMiniatureClickDebug(DebugPosition, DebugMessage, DebugColor));
	TestTrue(TEXT("Foreground obstruction reports orange"), DebugColor.Equals(FLinearColor(FColor::Orange)));
	Blocker->SetActorLocation(Capture->GetActorLocation() + Capture->GetActorForwardVector() * 1000);
	Controller->InteractWithMiniatureRay(Camera, ClickDirection);
	TestFalse(TEXT("Non-interactable first world hit blocks target behind it"), Rotation->IsRotating());
	Miniature->ActorsToHideFromCapture.Add(Blocker);
	Miniature->TickComponent(0, LEVELTICK_All, nullptr);
	// Tick 更新取景后，恢复测试固定的捕获姿态与显示姿态。
	Capture->SetActorTransform(CaptureTransform);
	Display->SetWorldTransform(DisplayTransform);
	Controller->InteractWithMiniatureRay(Camera, ClickDirection);
	TestTrue(TEXT("Hidden capture actor is also skipped by picking"), Rotation->IsRotating());
	Controller->InteractWithMiniatureRay(Camera, Display->GetComponentTransform().TransformPosition(FVector(100, 0, 0)) - Camera);
	TestTrue(TEXT("Mapping failure has HUD feedback"), Controller->GetMiniatureClickDebug(DebugPosition, DebugMessage, DebugColor));
	TestTrue(TEXT("Mapping failure reports red"), DebugColor.Equals(FLinearColor(FColor::Red)));
	Debug->Set(bPreviousDebug, ECVF_SetByCode);
	Miniature->SetPresentationEnabled(false);
	FVector Hit, Origin, Direction;
	FString Reason;
	TestFalse(TEXT("Disabled presentation cannot interact"), Miniature->TryMapViewRayToCaptureRay(
		Camera, Display->GetComponentLocation() - Camera, Hit, Origin, Direction, Reason));
	TestFalse(TEXT("Disabled presentation returns an explanation"), Reason.IsEmpty());
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSceneCaptureWindowFOVTest,
	"DreamSpace.Presentation.SceneCaptureWindowFOV",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSceneCaptureWindowFOVTest::RunTest(const FString& Parameters)
{
	// 视场角 = 面片张角 2·atan((宽/2) / 距离)，与被捕获场景的比例无关。
	const float Width = 80.0f;
	const float Distance = 400.0f;
	const float Expected = FMath::RadiansToDegrees(2.0f * FMath::Atan2(Width * 0.5f, Distance));
	TestTrue(TEXT("Window FOV is the angle the plane subtends"),
		FMath::IsNearlyEqual(FComponent::ComputeWindowFieldOfView(Width, Distance), Expected, 0.01f));

	// 距离越大，张角越小。
	TestTrue(TEXT("Farther observer sees a narrower window"),
		FComponent::ComputeWindowFieldOfView(Width, Distance * 2.0f) <
		FComponent::ComputeWindowFieldOfView(Width, Distance));

	// 极近或极远时被限制在合理区间，避免取景器崩成极广角或退化成零。
	TestTrue(TEXT("FOV is clamped at the near end"),
		FComponent::ComputeWindowFieldOfView(Width, 0.0f) <= 170.0f);
	TestTrue(TEXT("FOV is clamped at the far end"),
		FComponent::ComputeWindowFieldOfView(Width, 1.0e6f) >= 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSceneCaptureFrameRotationTest,
	"DreamSpace.Presentation.SceneCaptureFrameRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSceneCaptureFrameRotationTest::RunTest(const FString& Parameters)
{
	// 回归测试：手办组件与锚点的旋转都属于显示层/参考系，不能影响捕获相机。
	// 原型角色给面片组件设了 180° 朝向（DreamCharacter 的 SetRelativeRotation），
	// 曾经让方位角整体翻转，捕获相机跑到玩家相机的另一侧。
	const FTransform SceneReference = FTransform::Identity;
	const FTransform Observer(FRotator(0.0f, 45.0f, 0.0f), FVector(-500.0f, 0.0f, 0.0f));
	const FVector FrameLocation(75.0f, 55.0f, 95.0f);
	constexpr float Scale = 0.03f;

	const FTransform Baseline = FComponent::MapObserverWindowToCaptureWorld(
		Observer, FTransform(FRotator::ZeroRotator, FrameLocation), SceneReference, Scale);
	const FVector ViewDirection = ViewDirectionOf(Observer, FTransform(FRotator::ZeroRotator, FrameLocation));

	// 捕获相机始终在视线的反侧：玩家能看见手办的那一面，就是捕获相机看到建筑的那一面。
	TestTrue(TEXT("Capture sits behind the anchor along the view direction"),
		FVector::DotProduct(Baseline.GetLocation() - SceneReference.GetLocation(), ViewDirection) < 0.0f);

	for (const FRotator& FrameRotation : {
		FRotator(0.0f, 180.0f, 0.0f), FRotator(0.0f, -90.0f, 0.0f), FRotator(20.0f, 140.0f, 0.0f) })
	{
		const FTransform Capture = FComponent::MapObserverWindowToCaptureWorld(
			Observer, FTransform(FrameRotation, FrameLocation), SceneReference, Scale);
		TestTrue(TEXT("Miniature rotation does not move the capture camera"),
			Capture.GetLocation().Equals(Baseline.GetLocation(), 0.01f));
		TestTrue(TEXT("Miniature rotation does not rotate the capture camera"),
			Capture.GetRotation().Equals(Baseline.GetRotation(), 0.001f));
	}
	for (const FRotator& ReferenceRotation : { FRotator(0.0f, 90.0f, 0.0f), FRotator(0.0f, -35.0f, 0.0f) })
	{
		const FTransform RotatedReference(ReferenceRotation, SceneReference.GetLocation());
		const FTransform Capture = FComponent::MapObserverWindowToCaptureWorld(
			Observer, FTransform(FRotator(0.0f, 180.0f, 0.0f), FrameLocation), RotatedReference, Scale);
		TestTrue(TEXT("Anchor rotation does not change where the capture camera sits relative to the anchor"),
			(Capture.GetLocation() - RotatedReference.GetLocation()).Equals(
				Baseline.GetLocation() - SceneReference.GetLocation(), 0.01f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSceneCaptureSidePitchTest,
	"DreamSpace.Presentation.SceneCaptureSidePitch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSceneCaptureSidePitchTest::RunTest(const FString& Parameters)
{
	// 复现问题场景：面片在 X+50，观察相机转到 Y+ 一侧、朝 Y- 低头看面片。
	// 捕获相机应该同样从 Y+ 一侧、从上方俯视锚点，光轴不应出现 X 分量。
	const FTransform Frame(FRotator::ZeroRotator, FVector(50.0f, 0.0f, 0.0f));
	const FTransform SceneReference = FTransform::Identity;
	constexpr float Scale = 0.03f;

	const FVector ObserverLocation(50.0f, 300.0f, 200.0f);
	const FRotator ObserverRotation = (Frame.GetLocation() - ObserverLocation).Rotation();
	const FTransform Observer(ObserverRotation, ObserverLocation);

	const FTransform Capture = FComponent::MapObserverWindowToCaptureWorld(
		Observer, Frame, SceneReference, Scale);
	const FVector Forward = Capture.GetRotation().GetForwardVector();
	TestTrue(TEXT("Side view pitch does not leak into the X axis"),
		FMath::IsNearlyZero(Forward.X, 0.001f));
	TestTrue(TEXT("Capture looks toward Y- like the observer"), Forward.Y < 0.0f);
	TestTrue(TEXT("Capture looks down like the observer"), Forward.Z < 0.0f);
	TestTrue(TEXT("Capture sits on the Y+ side and above the anchor"),
		Capture.GetLocation().Y > 0.0f && Capture.GetLocation().Z > 0.0f);
	TestTrue(TEXT("Capture up direction matches the observer up direction"),
		(Capture.GetRotation().GetUpVector() | Observer.GetRotation().GetUpVector()) > 0.99f);

	// 面片不在屏幕中心、相机前方向偏离面片时，光轴仍由“眼睛 -> 面片”决定。
	const FTransform OffAxisObserver(ObserverRotation + FRotator(0.0f, 20.0f, 0.0f), ObserverLocation);
	const FTransform OffAxisCapture = FComponent::MapObserverWindowToCaptureWorld(
		OffAxisObserver, Frame, SceneReference, Scale);
	TestTrue(TEXT("Window keeps looking along the eye-to-plane direction when the plane is off center"),
		OffAxisCapture.GetRotation().GetForwardVector().Equals(
			ViewDirectionOf(OffAxisObserver, Frame), 0.001f));
	return true;
}
#endif
