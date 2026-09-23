#include "DreamSceneCapturePresentationComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSceneCaptureOrbitTest,
	"DreamSpace.Presentation.SceneCaptureFixedOrbit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSceneCaptureOrbitTest::RunTest(const FString& Parameters)
{
	const FTransform SceneReference(FRotator(0.0f, 35.0f, 0.0f), FVector(120.0f, -60.0f, 30.0f));
	const FTransform MiniatureFrame(FRotator(0.0f, 180.0f, 0.0f), FVector(75.0f, 55.0f, 35.0f));
	const FRotator ObserverRotation(15.0f, 10.0f, 0.0f);
	constexpr float Radius = 2400.0f;

	const FTransform Initial = UDreamSceneCapturePresentationComponent::MapObserverOrbitToCaptureWorld(
		ObserverRotation, MiniatureFrame, SceneReference, Radius);
	FTransform MovedFrame = MiniatureFrame;
	MovedFrame.SetLocation(FVector(-500.0f, 700.0f, 100.0f));
	const FTransform AfterTranslation = UDreamSceneCapturePresentationComponent::MapObserverOrbitToCaptureWorld(
		ObserverRotation, MovedFrame, SceneReference, Radius);
	TestTrue(TEXT("Translation does not move the capture camera"),
		Initial.GetLocation().Equals(AfterTranslation.GetLocation(), 0.01f));
	TestTrue(TEXT("Capture stays at the configured radius"),
		FMath::IsNearlyEqual(FVector::Distance(Initial.GetLocation(), SceneReference.GetLocation()), Radius, 0.01f));

	const FTransform AfterRotation = UDreamSceneCapturePresentationComponent::MapObserverOrbitToCaptureWorld(
		FRotator(15.0f, 100.0f, 0.0f), MiniatureFrame, SceneReference, Radius);
	TestFalse(TEXT("Camera orbit changes the capture angle"),
		Initial.GetLocation().Equals(AfterRotation.GetLocation(), 0.01f));
	TestTrue(TEXT("Camera orbit retains the configured radius"),
		FMath::IsNearlyEqual(FVector::Distance(AfterRotation.GetLocation(), SceneReference.GetLocation()), Radius, 0.01f));
	return true;
}
#endif
