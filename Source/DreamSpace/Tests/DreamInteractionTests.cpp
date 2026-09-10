// 验收测试覆盖玩家可观察行为与事务不变量；不依赖之前的关卡或蓝图。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>
#include "Engine/World.h"
#include "InteractiveAssemblyActor.h"
#include "InteractiveAssemblyDefinition.h"
#include "DreamInteractionWorldSubsystem.h"
#include "DreamTransformCapability.h"
#include "DreamSequenceCapability.h"
#include "DreamStateTransitionCapability.h"
#include "DreamExampleDefinitions.h"
#include "DreamStateMath.h"
#include "DreamCharacter.h"
#include "DreamOccupantComponent.h"
#include "DreamInteractionSaveGame.h"
#include "DreamInteractionTags.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

namespace DreamTest
{
struct FWorld
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UDreamInteractionWorldSubsystem* System = World->GetSubsystem<UDreamInteractionWorldSubsystem>();
	~FWorld() { World->DestroyWorld(false); }
	AInteractiveAssemblyActor* Spawn(UInteractiveAssemblyDefinition* D, FVector Location = FVector(0, 0, 1000))
	{
		auto* A = World->SpawnActorDeferred<AInteractiveAssemblyActor>(
			AInteractiveAssemblyActor::StaticClass(), FTransform(Location));
		A->Definition = D;
		A->FinishSpawning(FTransform(Location));
		System->RegisterAssembly(A);
		return A;
	}
	ADreamCharacter* Player(FVector Position)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* C = World->SpawnActor<ADreamCharacter>(Position, FRotator::ZeroRotator, P);
		System->RegisterOccupant(C->Occupant);
		return C;
	}
};
UInteractiveAssemblyDefinition* Simple()
{
	auto* D = NewObject<UInteractiveAssemblyDefinition>();
	FDreamNodeDefinition Node;
	Node.NodeId = FGuid(1, 0, 0, 1);
	Node.NodeName = TEXT("Node");
	D->Nodes.Add(Node);
	auto* Cap = NewObject<UDreamTransformCapability>(D);
	Cap->bAllowScaling = true;
	Cap->bAllowTranslation = true;
	Cap->bAllowInputAxis = true;
	D->Capabilities.Add(Cap);
	return D;
}
bool Command(FWorld& W, AInteractiveAssemblyActor* A, const FDreamInteractionIntent& Intent,
	FDreamInteractionCommand& Out, FText& Failure, FName Capability = TEXT("Transform"), FGuid User = FGuid(),
	EDreamInteractionMode Mode = EDreamInteractionMode::Overview)
{
	FGuid Session;
	if (!W.System->BeginInteraction(A->AssemblyId, Intent.TargetNodeId, Capability, Mode, User, Session, Failure))
		return false;
	if (!W.System->PrepareCommand(Session, Intent, Out, Failure))
	{
		W.System->CancelInteraction(Session);
		return false;
	}
	return true;
}
bool Execute(FWorld& W, AInteractiveAssemblyActor* A, const FDreamInteractionIntent& Intent, FText& Failure,
	FName Cap = TEXT("Transform"), FGuid User = FGuid(), EDreamInteractionMode Mode = EDreamInteractionMode::Overview)
{
	FDreamInteractionCommand C;
	if (!Command(W, A, Intent, C, Failure, Cap, User, Mode))
		return false;
	const bool Result = W.System->ExecuteCommand(C, nullptr, Failure);
	W.System->CancelInteraction(C.SessionId);
	return Result;
}
} // namespace DreamTest
using namespace DreamTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamDefinitionsTest, "DreamSpace.Interaction.DefinitionsAndHierarchy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamDefinitionsTest::RunTest(const FString& Parameters)
{
	TArray<FString> Errors;
	for (auto Factory :
		{DreamExamples::MakeCube, DreamExamples::MakeKey, DreamExamples::MakeGravityRoom, DreamExamples::MakeDoor})
	{
		auto* D = NewObject<UInteractiveAssemblyDefinition>();
		Factory(*D);
		TestTrue(TEXT("Example definition validates"), D->ValidateDefinition(Errors));
	}
	auto* D = Simple();
	const auto Duplicate = D->Nodes[0];
	D->Nodes.Add(Duplicate);
	TestFalse(TEXT("Reject duplicate node IDs"), D->ValidateDefinition(Errors));
	D->Nodes[1].NodeId = FGuid(2, 0, 0, 1);
	D->Nodes[0].ParentNodeId = D->Nodes[1].NodeId;
	D->Nodes[1].ParentNodeId = D->Nodes[0].NodeId;
	TestFalse(TEXT("Reject parent cycle"), D->ValidateDefinition(Errors));
	D->Nodes[0].ParentNodeId.Invalidate();
	D->Nodes[1].DefaultLocalTransform = FTransform(FVector(100, 0, 0));
	TestTrue(TEXT("Valid hierarchy"), D->ValidateDefinition(Errors));
	FWorld W;
	auto* A = W.Spawn(D);
	auto S = A->GetAssemblyState();
	Swap(S.Nodes[0], S.Nodes[1]);
	FTransform Child;
	TestTrue(TEXT("Shuffled nodes still resolve"), DreamState::WorldTransform(S, D->Nodes[1].NodeId, Child));
	TestTrue(
		TEXT("Child local chain gives expected world location"), Child.GetLocation().Equals(FVector(100, 0, 1000)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamTransformTest, "DreamSpace.Interaction.PivotFramesAndSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamTransformTest::RunTest(const FString& Parameters)
{
	FWorld W;
	auto* D = Simple();
	auto* Cap = Cast<UDreamTransformCapability>(D->Capabilities[0]);
	Cap->PivotMode = EDreamPivotMode::ConfiguredPoint;
	Cap->PivotPoint = FVector(50, 0, 0);
	Cap->ReferenceFrame = EDreamReferenceFrame::Assembly;
	auto Child = D->Nodes[0];
	Child.NodeId = FGuid(2, 0, 0, 1);
	Child.ParentNodeId = D->Nodes[0].NodeId;
	Child.DefaultLocalTransform = FTransform(FVector(0, 200, 0));
	D->Nodes.Add(Child);
	auto* A = W.Spawn(D);
	FText Failure;
	FDreamInteractionIntent I;
	I.TargetNodeIds = {D->Nodes[0].NodeId, Child.NodeId};
	I.RotationDeltaDegrees = 90;
	FDreamInteractionCommand C;
	TestTrue(TEXT("Prepare parent and child selection"), Command(W, A, I, C, Failure));
	FTransform Actual;
	DreamState::WorldTransform(C.ResultState, D->Nodes[0].NodeId, Actual);
	TestTrue(TEXT("Rotate around configured pivot"), Actual.GetLocation().Equals(FVector(50, -50, 1000), 0.001));
	TestTrue(TEXT("Selected child is not transformed twice"),
		C.ResultState.FindNode(Child.NodeId)->LocalTransform.Equals(Child.DefaultLocalTransform));
	W.System->CancelInteraction(C.SessionId);
	Cap->PivotMode = EDreamPivotMode::Socket;
	Cap->PivotNodeId = D->Nodes[0].NodeId;
	Cap->PivotSocket = TEXT("Hinge");
	D->Nodes[0].Sockets.Add(TEXT("Hinge"), FTransform(FVector(50, 0, 0)));
	TestTrue(TEXT("Prepare socket pivot"), Command(W, A, I, C, Failure));
	TestTrue(TEXT("Socket pivot uses logical anchor"), C.WorldPivot.Equals(FVector(50, 0, 1000)));
	W.System->CancelInteraction(C.SessionId);
	Cap->ReferenceFrame = EDreamReferenceFrame::Node;
	I.TargetNodeId = D->Nodes[0].NodeId;
	I.TargetNodeIds.Reset();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamTransactionsTest, "DreamSpace.Interaction.PreviewLocksVersionsAndUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamTransactionsTest::RunTest(const FString& Parameters)
{
	FWorld W;
	auto* D = Simple();
	auto* A = W.Spawn(D);
	FText Failure;
	FDreamInteractionIntent I;
	I.Translation = FVector(200, 0, 0);
	const auto Before = A->GetAssemblyState();
	const auto OldComponent = A->FindNodeComponent(D->Nodes[0].NodeId)->GetComponentTransform();
	FDreamInteractionCommand C;
	TestTrue(TEXT("Prepare initial version zero"), Command(W, A, I, C, Failure));
	TestTrue(TEXT("Valid preview"), W.System->PreviewCommand(C, Failure));
	TestTrue(TEXT("Preview does not change logic"), DreamState::Equivalent(Before, A->GetAssemblyState()));
	TestTrue(TEXT("Preview does not move collision"),
		A->FindNodeComponent(D->Nodes[0].NodeId)->GetComponentTransform().Equals(OldComponent));
	FGuid Other;
	TestFalse(TEXT("Second session cannot claim same assembly"),
		W.System->BeginInteraction(
			A->AssemblyId, FGuid(), TEXT("Transform"), EDreamInteractionMode::Overview, FGuid(), Other, Failure));
	W.System->CancelInteraction(C.SessionId);
	TestTrue(TEXT("Cancel preserves logic"), DreamState::Equivalent(Before, A->GetAssemblyState()));
	TestFalse(TEXT("Canceled command is invalid"), W.System->ExecuteCommand(C, nullptr, Failure));
	TestTrue(TEXT("Prepare new command"), Command(W, A, I, C, Failure));
	auto Tampered = C;
	Tampered.ResultState.RootTransform.SetScale3D(FVector(100));
	TestFalse(TEXT("Reject tampered result"), W.System->ExecuteCommand(Tampered, nullptr, Failure));
	TestTrue(TEXT("Commit version zero command"), W.System->ExecuteCommand(C, nullptr, Failure));
	TestFalse(TEXT("Replay of version zero rejected"), W.System->ExecuteCommand(C, nullptr, Failure));
	TestTrue(TEXT("Second operation"), Execute(W, A, I, Failure));
	TestTrue(TEXT("Undo second operation"), W.System->UndoLastTransaction(A->AssemblyId, Failure));
	TestTrue(TEXT("Undo first operation after monotonic version advance"),
		W.System->UndoLastTransaction(A->AssemblyId, Failure));
	TestTrue(TEXT("Undo restores initial state"), DreamState::Equivalent(Before, A->GetAssemblyState()));
	TestTrue(TEXT("Redo first operation"), W.System->RedoLastTransaction(A->AssemblyId, Failure));
	TestTrue(TEXT("Redo second operation"), W.System->RedoLastTransaction(A->AssemblyId, Failure));
	TestEqual(TEXT("Versions advance through undo/redo"), A->GetAssemblyState().StateVersion, int64(6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamCollisionTest, "DreamSpace.Interaction.CollisionAndAtomicFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamCollisionTest::RunTest(const FString& Parameters)
{
	FWorld W;
	auto* A = W.Spawn(Simple());
	W.Spawn(Simple(), FVector(200, 0, 1000));
	FText Failure;
	FDreamInteractionIntent I;
	I.Translation = FVector(200, 0, 0);
	FDreamInteractionCommand C;
	const auto Before = A->GetAssemblyState();
	TestTrue(TEXT("Prepare collision candidate"), Command(W, A, I, C, Failure));
	TestFalse(TEXT("Reject blocked preview"), W.System->PreviewCommand(C, Failure));
	TestFalse(TEXT("Reject blocked commit"), W.System->ExecuteCommand(C, nullptr, Failure));
	TestTrue(TEXT("Failed commit retains complete state"), DreamState::Equivalent(Before, A->GetAssemblyState()));
	W.System->CancelInteraction(C.SessionId);
	auto* Sequence = NewObject<UDreamSequenceCapability>(A->Definition);
	auto* First = NewObject<UDreamStateTransitionCapability>(Sequence);
	auto* Last = NewObject<UDreamStateTransitionCapability>(Sequence);
	First->TargetState = EDreamNodeRuntimeState::Damaged;
	Last->AllowedSourceStates = {EDreamNodeRuntimeState::Intact};
	Sequence->Steps = {First, Last};
	A->Definition->Capabilities.Add(Sequence);
	I = FDreamInteractionIntent();
	TestFalse(TEXT("Sequence fails when second condition rejects intermediate state"),
		Command(W, A, I, C, Failure, TEXT("Sequence")));
	TestTrue(TEXT("Failed sequence does not commit first step"), DreamState::Equivalent(Before, A->GetAssemblyState()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamGravityTest, "DreamSpace.Interaction.GravityOccupantAtomicFollow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamGravityTest::RunTest(const FString& Parameters)
{
	FWorld W;
	auto* D = NewObject<UInteractiveAssemblyDefinition>();
	DreamExamples::MakeGravityRoom(*D);
	auto* A = W.Spawn(D, FVector(0, 0, 550));
	auto* C = W.Player(FVector(-100, 0, 450));
	C->GetCharacterMovement()->Velocity = FVector(10, 20, 30);
	const auto Original = C->Occupant->CaptureState();
	FText Failure;
	FDreamInteractionIntent I;
	I.RotationAxis = FVector::ForwardVector;
	I.RotationDeltaDegrees = 90;
	TestTrue(TEXT("Room rotates with player"), Execute(W, A, I, Failure, TEXT("Transform"), C->Occupant->OccupantId));
	const FQuat Turn(FVector::ForwardVector, PI / 2);
	const auto After = C->Occupant->CaptureState();
	TestTrue(TEXT("Player position follows frame"),
		After.Transform.GetLocation().Equals(
			FVector(0, 0, 550) + Turn.RotateVector(Original.Transform.GetLocation() - FVector(0, 0, 550)), 0.01));
	TestTrue(
		TEXT("Velocity rotates without scaling"), After.Velocity.Equals(Turn.RotateVector(Original.Velocity), 0.01));
	TestTrue(
		TEXT("Gravity follows room"), After.GravityDirection.Equals(Turn.RotateVector(FVector::DownVector), 0.001));
	FVector Gravity;
	FGuid Source;
	W.System->ResolveGravityAtLocation(After.Transform.GetLocation(), Gravity, &Source);
	TestTrue(TEXT("Rotated logical gravity volume still contains player"),
		Source == A->AssemblyId && Gravity.Equals(After.GravityDirection));
	TestTrue(TEXT("Undo restores room and occupant"), W.System->UndoLastTransaction(A->AssemblyId, Failure));
	TestTrue(TEXT("Player restored atomically"), C->GetActorTransform().Equals(Original.Transform, 0.001));
	TestTrue(TEXT("Player velocity restored"), C->GetCharacterMovement()->Velocity.Equals(Original.Velocity));
	Cast<UDreamTransformCapability>(D->Capabilities[0])->bOverrideOccupantPolicy = true;
	Cast<UDreamTransformCapability>(D->Capabilities[0])->OccupantPolicy = EDreamOccupantPolicy::FailOperation;
	TestFalse(TEXT("Configured occupant block policy rejects operation"), Execute(W, A, I, Failure));
	if (!Failure.IsEmpty())
		AddInfo(Failure.ToString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamPickupTest, "DreamSpace.Interaction.ShrinkPickupDoorAndRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamPickupTest::RunTest(const FString& Parameters)
{
	FWorld W;
	auto* D = NewObject<UInteractiveAssemblyDefinition>();
	DreamExamples::MakeKey(*D);
	auto* Key = W.Spawn(D, FVector(0, 0, 220));
	auto* C = W.Player(FVector(-100, 0, 88));
	FText Failure;
	FDreamInteractionIntent I;
	TestFalse(TEXT("Huge key cannot be picked up"),
		Execute(W, Key, I, Failure, TEXT("Pickup"), C->Occupant->OccupantId, EDreamInteractionMode::ThirdPerson));
	I.ScaleDelta = -0.7f;
	TestTrue(TEXT("Key shrinks to usable size"), Execute(W, Key, I, Failure));
	I = FDreamInteractionIntent();
	TestTrue(TEXT("Shrunk key becomes carried"),
		Execute(W, Key, I, Failure, TEXT("Pickup"), C->Occupant->OccupantId, EDreamInteractionMode::ThirdPerson));
	TestTrue(TEXT("Carried state is logical"),
		Key->GetAssemblyState().bIsCarried && Key->GetAssemblyState().StateTags.HasTag(DreamTags::StateCarried));
	const auto InitialCarry = Key->GetEffectiveState().RootTransform;
	C->SetActorLocation(FVector(-100, 100, 88));
	Key->RefreshPresentation();
	TestTrue(TEXT("Same key instance follows carrier frame"),
		Key->GetActorLocation().Equals(InitialCarry.GetLocation() + FVector(0, 100, 0), 0.001));
	auto* DoorDef = NewObject<UInteractiveAssemblyDefinition>();
	DreamExamples::MakeDoor(*DoorDef);
	auto* Door = W.Spawn(DoorDef, FVector(100, 100, 160));
	TestTrue(TEXT("Door checks carried logical key tag"),
		Execute(W, Door, I, Failure, TEXT("OpenDoor"), C->Occupant->OccupantId, EDreamInteractionMode::ThirdPerson));
	TestTrue(TEXT("Door collision removed from logical state"),
		Door->GetAssemblyState().StateTags.HasTag(DreamTags::StateUnlocked));
	TestTrue(TEXT("Drop restores independent world root"),
		Execute(W, Key, I, Failure, TEXT("Drop"), C->Occupant->OccupantId, EDreamInteractionMode::ThirdPerson));
	TestFalse(TEXT("Object no longer carried"), Key->GetAssemblyState().bIsCarried);
	if (!Failure.IsEmpty())
		AddInfo(Failure.ToString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSaveTest, "DreamSpace.Interaction.SaveRoundTripAndAtomicRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamSaveTest::RunTest(const FString& Parameters)
{
	FWorld W;
	auto* A = W.Spawn(Simple());
	auto* B = W.Spawn(Simple(), FVector(600, 0, 1000));
	FText Failure;
	FDreamInteractionIntent I;
	I.Translation = FVector(100, 0, 0);
	TestTrue(TEXT("Move before save"), Execute(W, A, I, Failure));
	auto* Save = NewObject<UDreamInteractionSaveGame>();
	W.System->CaptureToSaveGame(*Save);
	TArray<uint8> Bytes;
	TestTrue(TEXT("Serialize reflected state"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
	auto* Loaded = Cast<UDreamInteractionSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("Deserialize save"), Loaded))
		return false;
	TestTrue(TEXT("Mutate after save"), Execute(W, A, I, Failure));
	const auto BeforeRestore = A->GetAssemblyState();
	const auto SavedB = Loaded->AssemblyStates[1];
	Loaded->AssemblyStates[1].AssemblyId = FGuid::NewGuid();
	TArray<FGuid> Restored;
	TestFalse(
		TEXT("Missing second object rejects full restore"), W.System->RestoreFromSaveGame(*Loaded, Restored, Failure));
	TestTrue(TEXT("First object unchanged when later save entry invalid"),
		DreamState::Equivalent(A->GetAssemblyState(), BeforeRestore));
	Loaded->AssemblyStates[1] = SavedB;
	Swap(Loaded->AssemblyStates[0], Loaded->AssemblyStates[1]);
	TestTrue(TEXT("Restore independent of array order"), W.System->RestoreFromSaveGame(*Loaded, Restored, Failure));
	for (const auto& State : Save->AssemblyStates)
		TestTrue(TEXT("Saved state round trip"),
			DreamState::Equivalent(State, W.System->FindAssembly(State.AssemblyId)->GetAssemblyState()));
	TestFalse(TEXT("Load clears incompatible undo history"), W.System->UndoLastTransaction(A->AssemblyId, Failure));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamReleaseTest, "DreamSpace.Interaction.ReleasedSubtreeKeepsWorldFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamReleaseTest::RunTest(const FString& Parameters)
{
	FWorld W;
	auto* D = Simple();
	auto Child = D->Nodes[0];
	Child.NodeId = FGuid(2, 0, 0, 1);
	Child.ParentNodeId = D->Nodes[0].NodeId;
	Child.DefaultLocalTransform = FTransform(FVector(0, 200, 0));
	D->Nodes.Add(Child);
	auto* Release = NewObject<UDreamStateTransitionCapability>(D);
	Release->CapabilityId = TEXT("Release");
	Release->TargetState = EDreamNodeRuntimeState::Released;
	D->Capabilities.Add(Release);
	auto* A = W.Spawn(D);
	FText Failure;
	FDreamInteractionIntent I;
	I.TargetNodeId = Child.NodeId;
	TestTrue(TEXT("Release child through state transaction"), Execute(W, A, I, Failure, TEXT("Release")));
	FTransform Detached;
	DreamState::WorldTransform(A->GetAssemblyState(), Child.NodeId, Detached);
	TestTrue(TEXT("Released node preserves world pose"), Detached.GetLocation().Equals(FVector(0, 200, 1000)));
	I = FDreamInteractionIntent();
	I.Translation = FVector(500, 0, 0);
	TestTrue(TEXT("Parent assembly remains movable"), Execute(W, A, I, Failure));
	FTransform AfterRootMove;
	DreamState::WorldTransform(A->GetAssemblyState(), Child.NodeId, AfterRootMove);
	TestTrue(TEXT("Released child no longer follows old root"), Detached.Equals(AfterRootMove));
	I.TargetNodeId = Child.NodeId;
	I.Translation = FVector(100, 0, 0);
	TestTrue(TEXT("Released child still uses same generic transform capability"), Execute(W, A, I, Failure));
	DreamState::WorldTransform(A->GetAssemblyState(), Child.NodeId, AfterRootMove);
	TestTrue(
		TEXT("Independent transform preserves stable ID"), AfterRootMove.GetLocation().Equals(FVector(100, 200, 1000)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamLimitsTest, "DreamSpace.Interaction.PersistentScaleLimitsAndFiniteInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamLimitsTest::RunTest(const FString& Parameters)
{
	FWorld W;
	auto* D = Simple();
	auto* Cap = Cast<UDreamTransformCapability>(D->Capabilities[0]);
	Cap->MinimumScale = 0.5;
	Cap->MaximumScale = 2;
	auto* A = W.Spawn(D);
	FText Failure;
	FDreamInteractionIntent I;
	I.ScaleDelta = 1;
	TestTrue(TEXT("Scale to configured maximum"), Execute(W, A, I, Failure));
	TestFalse(TEXT("Repeated scaling cannot exceed persistent maximum"), Execute(W, A, I, Failure));
	TestEqual(TEXT("Rejected scale leaves version unchanged"), A->GetAssemblyState().StateVersion, int64(1));
	I.ScaleDelta = -1;
	TestFalse(TEXT("Zero scale is rejected"), Execute(W, A, I, Failure));
	I.ScaleDelta = 0;
	I.RotationDeltaDegrees = std::numeric_limits<float>::infinity();
	TestFalse(TEXT("Infinite rotation is rejected"), Execute(W, A, I, Failure));
	return true;
}
#endif
