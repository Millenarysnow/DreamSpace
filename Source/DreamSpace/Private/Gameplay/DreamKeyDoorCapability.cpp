#include "DreamKeyDoorCapability.h"
#include "DreamInteractionTags.h"
#include "DreamInteractionWorldSubsystem.h"
#include "DreamOccupantComponent.h"
#include "InteractiveAssemblyActor.h"
#include "InteractiveAssemblyDefinition.h"
UDreamKeyDoorCapability::UDreamKeyDoorCapability()
{
	CapabilityId = TEXT("OpenDoor");
	DisplayName = FText::FromString(TEXT("开门"));
	AllowedModes = {EDreamInteractionMode::ThirdPerson};
	RequiredKeyTag = DreamTags::ItemKey;
	TargetState = EDreamNodeRuntimeState::Destroyed;
	bExistsAfterTransition = false;
	AddedStateTags.AddTag(DreamTags::StateUnlocked);
	bTargetAssemblyRoot = true;
}
bool UDreamKeyDoorCapability::CanStart(const FDreamCapabilityContext& C, FText& Failure) const
{
	if (!Super::CanStart(C, Failure))
		return false;
	auto* System = C.Target.GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>();
	auto* User = System->FindOccupant(C.RequesterId);
	auto* Key = System->FindCarriedAssembly(C.RequesterId);
	if (!User || !Key || !Key->Definition->GameplayTags.HasTag(RequiredKeyTag) ||
		!Key->GetAssemblyState().StateTags.HasTag(DreamTags::StateCarried))
	{
		Failure = FText::FromString(TEXT("需要携带对应的钥匙才能开门。"));
		return false;
	}
	if (FVector::Dist(User->GetOwner()->GetActorLocation(), C.State.RootTransform.GetLocation()) > UseDistance)
	{
		Failure = FText::FromString(TEXT("请靠近门。"));
		return false;
	}
	return true;
}
