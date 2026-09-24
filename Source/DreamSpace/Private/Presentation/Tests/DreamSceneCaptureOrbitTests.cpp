#include "DreamSceneCapturePresentationComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSceneCaptureOrbitTest,
	"DreamSpace.Presentation.SceneCaptureFixedOrbit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSceneCaptureOrbitTest::RunTest(const FString& Parameters)
{
	const FTransform SceneReference(FRotator(0.0f, 35.0f, 0.0f), FVector(120.0f, -60.0f, 30.0f));
	const FTransform MiniatureFrame(FRotator(0.0f, 180.0f, 0.0f), FVector(75.0f, 55.0f, 95.0f));
	const FTransform Observer(FRotator(-20.0f, 10.0f, 0.0f), FVector(-300.0f, 0.0f, 200.0f));
	constexpr float Radius = 2400.0f;

	const FTransform Initial = UDreamSceneCapturePresentationComponent::MapObserverOrbitToCaptureWorld(
		Observer, MiniatureFrame, SceneReference, Radius);
	TestTrue(TEXT("Capture stays at the configured radius"),
		FMath::IsNearlyEqual(FVector::Distance(Initial.GetLocation(), SceneReference.GetLocation()), Radius, 0.01f));
	const FVector ToAnchor = (SceneReference.GetLocation() - Initial.GetLocation()).GetSafeNormal();
	TestTrue(TEXT("Capture looks at the anchor"),
		Initial.GetRotation().GetForwardVector().Equals(ToAnchor, 0.001f));

	// SpringArm 被障碍推近：观察相机沿视线朝面片前移，捕获相机不应变化。
	const FVector ToPlane = MiniatureFrame.GetLocation() - Observer.GetLocation();
	FTransform PushedObserver = Observer;
	PushedObserver.SetLocation(Observer.GetLocation() + ToPlane * 0.6f);
	const FTransform AfterPushIn = UDreamSceneCapturePresentationComponent::MapObserverOrbitToCaptureWorld(
		PushedObserver, MiniatureFrame, SceneReference, Radius);
	TestTrue(TEXT("Pushing the observer toward the plane does not move the capture camera"),
		Initial.GetLocation().Equals(AfterPushIn.GetLocation(), 0.01f));
	TestTrue(TEXT("Pushing the observer toward the plane does not rotate the capture camera"),
		Initial.GetRotation().Equals(AfterPushIn.GetRotation(), 0.001f));

	// 角色（连同手办和相机）整体平移：相对关系不变，捕获相机也不变。
	FTransform MovedFrame = MiniatureFrame;
	MovedFrame.AddToTranslation(FVector(-500.0f, 700.0f, 100.0f));
	FTransform MovedObserver = Observer;
	MovedObserver.AddToTranslation(FVector(-500.0f, 700.0f, 100.0f));
	const FTransform AfterTranslation = UDreamSceneCapturePresentationComponent::MapObserverOrbitToCaptureWorld(
		MovedObserver, MovedFrame, SceneReference, Radius);
	TestTrue(TEXT("Moving the character does not move the capture camera"),
		Initial.GetLocation().Equals(AfterTranslation.GetLocation(), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSceneCaptureSidePitchTest,
	"DreamSpace.Presentation.SceneCaptureSidePitch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSceneCaptureSidePitchTest::RunTest(const FString& Parameters)
{
	// 复现问题场景：面片在 X+50，观察相机转到 Y+ 一侧、朝 Y- 低头看面片。
	// 捕获相机应该同样从 Y+ 一侧、从上方俯视锚点，视线不应出现 X 分量。
	const FTransform MiniatureFrame(FRotator::ZeroRotator, FVector(50.0f, 0.0f, 0.0f));
	const FTransform SceneReference = FTransform::Identity;
	constexpr float Radius = 1000.0f;

	const FVector ObserverLocation(50.0f, 300.0f, 200.0f);
	const FRotator ObserverRotation = (MiniatureFrame.GetLocation() - ObserverLocation).Rotation();
	const FTransform Observer(ObserverRotation, ObserverLocation);

	const FTransform Capture = UDreamSceneCapturePresentationComponent::MapObserverOrbitToCaptureWorld(
		Observer, MiniatureFrame, SceneReference, Radius);
	const FVector Forward = Capture.GetRotation().GetForwardVector();
	TestTrue(TEXT("Side view pitch does not leak into the X axis"),
		FMath::IsNearlyZero(Forward.X, 0.001f));
	TestTrue(TEXT("Capture looks toward Y- like the observer"), Forward.Y < 0.0f);
	TestTrue(TEXT("Capture looks down like the observer"), Forward.Z < 0.0f);
	TestTrue(TEXT("Capture sits on the Y+ side and above the anchor"),
		Capture.GetLocation().Y > 0.0f && Capture.GetLocation().Z > 0.0f);
	TestTrue(TEXT("Capture up direction matches the observer up direction"),
		(Capture.GetRotation().GetUpVector() | Observer.GetRotation().GetUpVector()) > 0.99f);

	// 观察相机的朝向偏离面片（面片不在屏幕中心）时，视线仍由眼睛到面片的方向决定。
	const FTransform OffAxisObserver(ObserverRotation + FRotator(0.0f, 20.0f, 0.0f), ObserverLocation);
	const FTransform OffAxisCapture = UDreamSceneCapturePresentationComponent::MapObserverOrbitToCaptureWorld(
		OffAxisObserver, MiniatureFrame, SceneReference, Radius);
	TestTrue(TEXT("Capture keeps looking at the anchor when the plane is off screen center"),
		OffAxisCapture.GetLocation().Equals(Capture.GetLocation(), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSceneCaptureFrameRotationTest,
	"DreamSpace.Presentation.SceneCaptureFrameRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSceneCaptureFrameRotationTest::RunTest(const FString& Parameters)
{
	// 手办随手部转 180 度时，同一条世界视线在手办坐标中来自相反一侧。
	const FTransform SceneReference = FTransform::Identity;
	const FTransform Observer(FRotator::ZeroRotator, FVector(-500.0f, 0.0f, 0.0f));
	constexpr float Radius = 1000.0f;

	const FTransform Capture = UDreamSceneCapturePresentationComponent::MapObserverOrbitToCaptureWorld(
		Observer, FTransform(FRotator::ZeroRotator, FVector::ZeroVector), SceneReference, Radius);
	const FTransform RotatedCapture = UDreamSceneCapturePresentationComponent::MapObserverOrbitToCaptureWorld(
		Observer, FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector::ZeroVector), SceneReference, Radius);
	TestTrue(TEXT("Unrotated miniature is viewed from X-"),
		Capture.GetLocation().Equals(FVector(-Radius, 0.0f, 0.0f), 0.01f));
	TestTrue(TEXT("Miniature turned 180 degrees is viewed from X+"),
		RotatedCapture.GetLocation().Equals(FVector(Radius, 0.0f, 0.0f), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSceneCaptureObserverTransformTest,
	"DreamSpace.Presentation.SceneCaptureObserverTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSceneCaptureObserverTransformTest::RunTest(const FString& Parameters)
{
	// 等比映射路径：捕获相机保留观察相机相对面片的完整位姿。
	const FTransform MiniatureFrame(FRotator::ZeroRotator, FVector(50.0f, 0.0f, 0.0f));
	const FTransform Observer(FRotator(-30.0f, -90.0f, 0.0f), FVector(0.0f, 50.0f, 0.0f));
	const FTransform SceneReference = FTransform::Identity;

	const FTransform Capture = UDreamSceneCapturePresentationComponent::MapObserverCameraToCaptureWorld(
		Observer, MiniatureFrame, SceneReference, 1.0f);
	const FTransform ExpectedRelative = Observer.GetRelativeTransform(MiniatureFrame);

	TestTrue(TEXT("Capture camera keeps the observer-relative rotation"),
		Capture.GetRotation().Equals(ExpectedRelative.GetRotation(), 0.001f));
	TestTrue(TEXT("Capture camera keeps the observer-relative offset"),
		Capture.GetLocation().Equals(ExpectedRelative.GetLocation(), 0.01f));

	const FTransform HalfScaleCapture = UDreamSceneCapturePresentationComponent::MapObserverCameraToCaptureWorld(
		Observer, MiniatureFrame, SceneReference, 0.5f);
	TestTrue(TEXT("Scale divides the observer offset"),
		HalfScaleCapture.GetLocation().Equals(ExpectedRelative.GetLocation() * 2.0f, 0.01f));
	return true;
}
#endif
