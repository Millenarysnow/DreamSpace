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

	// 观察相机的朝向偏离面片（面片不在屏幕中心）时，默认的视线模式仍由
	// “眼睛 -> 面片”的方向决定；显式关闭该模式才跟随相机前方向。
	const FTransform OffAxisObserver(ObserverRotation + FRotator(0.0f, 20.0f, 0.0f), ObserverLocation);
	const FTransform OffAxisCapture = UDreamSceneCapturePresentationComponent::MapObserverOrbitToCaptureWorld(
		OffAxisObserver, MiniatureFrame, SceneReference, Radius);
	TestTrue(TEXT("Default orbit keeps looking along the eye-to-plane direction"),
		OffAxisCapture.GetLocation().Equals(Capture.GetLocation(), 0.01f));
	const FTransform ForwardCapture = UDreamSceneCapturePresentationComponent::MapObserverOrbitToCaptureWorld(
		OffAxisObserver, MiniatureFrame, SceneReference, Radius, false);
	TestTrue(TEXT("Observer-forward orbit follows the camera forward direction"),
		ForwardCapture.GetRotation().GetForwardVector().Equals(
			OffAxisObserver.GetRotation().GetForwardVector(), 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSceneCapturePivotOffsetTest,
	"DreamSpace.Presentation.SceneCapturePivotOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSceneCapturePivotOffsetTest::RunTest(const FString& Parameters)
{
	// 第三人称相机绕角色枢轴转动，面片偏在角色右前方。默认的视线模式下，
	// 捕获方向是“相机 -> 面片”的视线，因此与相机前方向相差一个由面片偏移和
	// 臂长决定的夹角；面片正好落在枢轴上时两者才重合。
	const FVector Pivot(0.0f, 0.0f, 60.0f);
	const FTransform SceneReference = FTransform::Identity;
	constexpr float ArmLength = 420.0f;
	constexpr float Radius = 1000.0f;
	const FTransform OffsetFrame(FRotator::ZeroRotator, FVector(75.0f, 55.0f, 95.0f));
	const FRotator CameraRotation(-30.0f, -90.0f, 0.0f);
	const FVector ObserverLocation = Pivot - CameraRotation.Vector() * ArmLength;

	// 捕获方向 = normalize(面片位置 − 观察位置)，且捕获相机落在锚点的反侧。
	const FTransform Capture = UDreamSceneCapturePresentationComponent::MapObserverOrbitToCaptureWorld(
		FTransform(CameraRotation, ObserverLocation), OffsetFrame, SceneReference, Radius);
	const FVector ExpectedForward =
		(OffsetFrame.GetLocation() - ObserverLocation).GetSafeNormal();
	TestTrue(TEXT("Capture forward is the eye-to-plane direction"),
		Capture.GetRotation().GetForwardVector().Equals(ExpectedForward, 0.001f));
	TestTrue(TEXT("Capture sits on the far side of the anchor"),
		Capture.GetLocation().Equals(SceneReference.GetLocation() - ExpectedForward * Radius, 0.01f));

	// 面片偏在枢轴上时，视线与相机前方向重合，俯仰完全一致。
	const FTransform PivotFrame(FRotator::ZeroRotator, ObserverLocation + CameraRotation.Vector() * ArmLength * 0.4f);
	const FTransform PivotCapture = UDreamSceneCapturePresentationComponent::MapObserverOrbitToCaptureWorld(
		FTransform(CameraRotation, ObserverLocation), PivotFrame, SceneReference, Radius);
	TestTrue(TEXT("A plane on the arm axis keeps the camera pitch"),
		FMath::IsNearlyEqual(PivotCapture.Rotator().Pitch, CameraRotation.Pitch, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSceneCaptureFrameRotationTest,
	"DreamSpace.Presentation.SceneCaptureFrameRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSceneCaptureFrameRotationTest::RunTest(const FString& Parameters)
{
	// 回归测试：手办组件与锚点的旋转都属于显示层/参考系，不能影响捕获相机所在的一侧。
	// 原型角色给面片组件设了 180° 朝向（DreamCharacter 的 SetRelativeRotation），
	// 曾经让方位角整体翻转，捕获相机跑到玩家相机的另一侧。
	const FTransform SceneReference = FTransform::Identity;
	const FTransform Observer(FRotator(0.0f, 45.0f, 0.0f), FVector(-500.0f, 0.0f, 0.0f));
	const FVector FrameLocation(75.0f, 55.0f, 95.0f);
	constexpr float Radius = 1000.0f;

	const FTransform Baseline = UDreamSceneCapturePresentationComponent::MapObserverOrbitToCaptureWorld(
		Observer, FTransform(FRotator::ZeroRotator, FrameLocation), SceneReference, Radius);
	const FVector ViewDirection = (FrameLocation - Observer.GetLocation()).GetSafeNormal();

	// 捕获相机始终在视线的反侧：玩家能看见手办的那一面，就是捕获相机所在的那一面。
	TestTrue(TEXT("Capture sits on the far side of the anchor along the view direction"),
		Baseline.GetLocation().Equals(SceneReference.GetLocation() - ViewDirection * Radius, 0.01f));

	for (const FRotator& FrameRotation : {
		FRotator(0.0f, 180.0f, 0.0f), FRotator(0.0f, -90.0f, 0.0f), FRotator(20.0f, 140.0f, 0.0f) })
	{
		const FTransform Capture = UDreamSceneCapturePresentationComponent::MapObserverOrbitToCaptureWorld(
			Observer, FTransform(FrameRotation, FrameLocation), SceneReference, Radius);
		TestTrue(TEXT("Miniature rotation does not move the capture camera"),
			Capture.GetLocation().Equals(Baseline.GetLocation(), 0.01f));
	}
	for (const FRotator& ReferenceRotation : { FRotator(0.0f, 90.0f, 0.0f), FRotator(0.0f, -35.0f, 0.0f) })
	{
		const FTransform RotatedReference(ReferenceRotation, SceneReference.GetLocation());
		const FTransform Capture = UDreamSceneCapturePresentationComponent::MapObserverOrbitToCaptureWorld(
			Observer, FTransform(FRotator(0.0f, 180.0f, 0.0f), FrameLocation), RotatedReference, Radius);
		TestTrue(TEXT("Anchor rotation only moves the capture camera with the anchor"),
			Capture.GetLocation().Equals(
				RotatedReference.GetLocation() - ViewDirection * Radius, 0.01f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSceneCaptureObserverTransformTest,
	"DreamSpace.Presentation.SceneCaptureObserverTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSceneCaptureObserverTransformTest::RunTest(const FString& Parameters)
{
	// 等比映射路径：观察相机相对面片的偏移按比例还原到捕获场景，旋转沿用观察相机。
	// 手办组件与锚点的旋转同样不参与，捕获场景始终与世界轴对齐。
	const FVector FrameLocation(50.0f, 0.0f, 0.0f);
	const FVector ObserverLocation(0.0f, 50.0f, 0.0f);
	const FRotator ObserverRotation(-30.0f, -90.0f, 0.0f);
	const FTransform SceneReference(FRotator(0.0f, 70.0f, 0.0f), FVector(120.0f, -60.0f, 30.0f));
	const FTransform Observer(ObserverRotation, ObserverLocation);
	const FTransform MiniatureFrame(FRotator(0.0f, 180.0f, 0.0f), FrameLocation);

	const FTransform Capture = UDreamSceneCapturePresentationComponent::MapObserverCameraToCaptureWorld(
		Observer, MiniatureFrame, SceneReference, 1.0f);
	const FVector ExpectedOffset = ObserverLocation - FrameLocation;
	TestTrue(TEXT("Capture keeps the observer rotation regardless of frame and anchor rotation"),
		Capture.GetRotation().Equals(ObserverRotation.Quaternion(), 0.001f));
	TestTrue(TEXT("Capture keeps the observer-relative offset from the anchor"),
		Capture.GetLocation().Equals(SceneReference.GetLocation() + ExpectedOffset, 0.01f));

	const FTransform HalfScaleCapture = UDreamSceneCapturePresentationComponent::MapObserverCameraToCaptureWorld(
		Observer, MiniatureFrame, SceneReference, 0.5f);
	TestTrue(TEXT("Scale divides the observer offset"),
		HalfScaleCapture.GetLocation().Equals(SceneReference.GetLocation() + ExpectedOffset * 2.0f, 0.01f));
	TestTrue(TEXT("Scale does not change the capture rotation"),
		HalfScaleCapture.GetRotation().Equals(ObserverRotation.Quaternion(), 0.001f));
	return true;
}
#endif
