#include "DreamSceneCapturePresentationComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

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

	// 取景距离 = 眼睛到面片的距离 ÷ 比例，捕获相机落在锚点背后与观察者对称的一侧。
	TestTrue(TEXT("Window distance is the observer distance divided by the scale"),
		FMath::IsNearlyEqual(FVector::Distance(Capture.GetLocation(), SceneReference.GetLocation()),
			ObserverDistance / Scale, 0.05f));
	TestTrue(TEXT("Observer and capture sit on opposite sides of the anchor"),
		((Observer.GetLocation() - SceneReference.GetLocation()) |
			(Capture.GetLocation() - SceneReference.GetLocation())) < 0.0f);

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
