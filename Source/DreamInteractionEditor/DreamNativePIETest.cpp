#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/AutomationCommon.h"
#include "Editor.h"
#include "DreamPlayerController.h"
#include "DreamCharacter.h"
#include "DreamInteractionPlayerSubsystem.h"
#include "DreamInteractionWorldSubsystem.h"
#include "DreamInteractionTargetResolver.h"
#include "DreamSceneCapturePresentationComponent.h"
#include "InteractiveAssemblyActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

/** 跨真实 PIE 帧注入键盘事件，验证 Enhanced Input、原生相机和事务的完整链路。 */
class FDreamPIECheck : public IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	int32 Stage = 0;
	double NextTime = 0;
	double Started = 0;
	FVector InitialPosition = FVector::ZeroVector;

public:
	explicit FDreamPIECheck(FAutomationTestBase* InTest) : Test(InTest) {}
	virtual bool Update() override
	{
		if (Started == 0)
			Started = FPlatformTime::Seconds();
		if (FPlatformTime::Seconds() - Started > 45)
		{
			Test->AddError(TEXT("Native PIE input test timed out."));
			return true;
		}
		if (FPlatformTime::Seconds() < NextTime)
			return false;
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		auto* Controller = World ? Cast<ADreamPlayerController>(World->GetFirstPlayerController()) : nullptr;
		auto* Character = Controller ? Cast<ADreamCharacter>(Controller->GetPawn()) : nullptr;
		if (!Controller || !Character)
			return false;
		auto* Player = Controller->GetInteractionPlayer();
		auto* System = World->GetSubsystem<UDreamInteractionWorldSubsystem>();
		auto* Room = System->FindAssembly(FGuid(0xD5A, 1, 0, 4));
		if (!Room)
		{
			Test->AddError(TEXT("Demo room did not retain its persistent ID in PIE."));
			return true;
		}
		auto Press = [&](FKey Key)
		{
			Controller->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), Key, IE_Pressed,
				1.0, false, FPlatformTime::Cycles64()));
		};
		auto Release = [&](FKey Key)
		{
			Controller->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), Key, IE_Released,
				0.0, false, FPlatformTime::Cycles64()));
		};
		switch (Stage++)
		{
		case 0:
		{
			TArray<AInteractiveAssemblyActor*> Registered;
			System->GetRegisteredAssemblies(Registered);
			Test->TestEqual(TEXT("PIE registers four independent definition instances"), Registered.Num(), 4);
			Test->TestTrue(TEXT("Native character starts in third person"),
				Player->GetInteractionMode() == EDreamInteractionMode::ThirdPerson);
			auto* Resolver = NewObject<UDreamInteractionTargetResolver>();
			FDreamInteractionTarget Target;
			Test->TestTrue(TEXT("World ray resolves cube component to stable node"),
				Resolver->ResolveWorldRay(*World, FVector(-700, -400, 180), FVector(-700, 400, 180),
					EDreamInteractionMode::Overview, Target, Character));
			Test->TestTrue(TEXT("Selected cube exposes inherited capabilities"), Target.Capabilities.Num() > 0);
			InitialPosition = Character->GetActorLocation();
			Test->TestNotNull(TEXT("Third person owns independent scene miniature presentation"), Character->SceneMiniature.Get());
			if (Character->SceneMiniature)
			{
				Test->TestTrue(TEXT("Scene miniature presentation is active"), Character->SceneMiniature->IsPresentationActive());
				Test->TestNotNull(TEXT("Scene miniature has a render target"), Character->SceneMiniature->GetRenderTarget());
			}
			if (FParse::Param(FCommandLine::Get(), TEXT("DreamCapture")))
				FScreenshotRequest::RequestScreenshot(
					FPaths::ProjectSavedDir() / TEXT("Screenshots/DreamSceneMiniatureThirdPerson.png"), false, false);
			Player->SetSelection(Room->AssemblyId, FGuid());
			Press(EKeys::Tab);
			break;
		}
		case 1:
			Release(EKeys::Tab);
			Test->TestTrue(
				TEXT("Tab input selects overview"), Player->GetInteractionMode() == EDreamInteractionMode::Overview);
			Press(EKeys::X);
			break;
		case 2:
			Release(EKeys::X);
			Press(EKeys::R);
			break;
		case 3:
			Release(EKeys::R);
			Test->TestEqual(
				TEXT("R preview keeps persistent version zero"), Room->GetAssemblyState().StateVersion, int64(0));
			Test->TestTrue(
				TEXT("Preview does not transport player"), Character->GetActorLocation().Equals(InitialPosition, 5));
			Press(EKeys::Enter);
			break;
		case 4:
			Release(EKeys::Enter);
			Test->TestEqual(
				TEXT("Enter commits through the native input path"), Room->GetAssemblyState().StateVersion, int64(1));
			Test->TestTrue(TEXT("Gravity changed after input commit"),
				!Character->GetCharacterMovement()->GetGravityDirection().Equals(FVector::DownVector));
			if (FParse::Param(FCommandLine::Get(), TEXT("DreamCapture")))
				FScreenshotRequest::RequestScreenshot(
					FPaths::ProjectSavedDir() / TEXT("Screenshots/DreamInteractionOverview.png"), false, false);
			break;
		case 5:
			Press(EKeys::U);
			break;
		case 6:
			Release(EKeys::U);
			Test->TestEqual(TEXT("U input performs undo"), Room->GetAssemblyState().StateVersion, int64(2));
			Press(EKeys::Tab);
			break;
		case 7:
			Release(EKeys::Tab);
			Test->TestTrue(TEXT("Tab returns to same native pawn"),
				Player->GetInteractionMode() == EDreamInteractionMode::ThirdPerson &&
					Controller->GetPawn() == Character);
			InitialPosition = Character->GetActorLocation();
			Press(EKeys::W);
			break;
		case 8:
			Release(EKeys::W);
			Test->TestTrue(TEXT("W input moves character on restored gravity plane"),
				FVector::Dist(Character->GetActorLocation(), InitialPosition) > 1);
			return true;
		}
		NextTime = FPlatformTime::Seconds() + 0.25;
		return false;
	}
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamNativePIETest, "DreamSpace.Integration.NativeInputAndDemoMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamNativePIETest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/DreamInteraction/Maps/InteractionDemo")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
	if (FParse::Param(FCommandLine::Get(), TEXT("DreamCapture")))
	{
		ADD_LATENT_AUTOMATION_COMMAND(FWaitForShadersToFinishCompiling());
	}
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FDreamPIECheck>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
