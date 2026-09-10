#include "DreamPickupCapability.h"

#include "InteractiveAssemblyActor.h"

UDreamPickupCapability::UDreamPickupCapability()
{
	CapabilityTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Interaction.Pickup")), false);
	CarriedStateTag = FGameplayTag::RequestGameplayTag(FName(TEXT("State.Carried")), false);
	AllowedModes.Add(EDreamInteractionMode::ThirdPerson);
}

bool UDreamPickupCapability::CanStart(const AInteractiveAssemblyActor& Target,
	const FDreamInteractionIntent& Intent,
	EDreamInteractionMode Mode,
	FText& OutFailure) const
{
	if (!UDreamInteractionCapability::CanStart(Target, Intent, Mode, OutFailure))
	{
		return false;
	}
	if (Intent.Type != EDreamInteractionIntentType::Begin && Intent.Type != EDreamInteractionIntentType::Confirm)
	{
		OutFailure = FText::FromString(TEXT("拾取能力需要开始或确认意图。"));
		return false;
	}
	const float Scale = Target.AssemblyState.RootTransform.GetScale3D().GetMin();
	if (!bDropInsteadOfPickup && Scale < MinimumScaleToPickup)
	{
		OutFailure = FText::FromString(TEXT("物体尺寸尚未达到可拾取条件。"));
		return false;
	}
	if (!bDropInsteadOfPickup && Target.AssemblyState.bIsCarried)
	{
		OutFailure = FText::FromString(TEXT("物体已经处于携带状态。"));
		return false;
	}
	return true;
}

bool UDreamPickupCapability::BuildCommand(AInteractiveAssemblyActor& Target,
	const FDreamInteractionIntent& Intent,
	FDreamInteractionCommand& OutCommand,
	FText& OutFailure) const
{
	OutCommand = FDreamInteractionCommand();
	OutCommand.Type = EDreamCommandType::Composite;
	OutCommand.CommandId = FGuid::NewGuid();
	OutCommand.AssemblyId = Target.GetAssemblyId();
	OutCommand.ExpectedStateVersion = Target.AssemblyState.StateVersion;
	OutCommand.bSetsCarriedState = true;
	OutCommand.bIsCarried = !bDropInsteadOfPickup;
	if (CarriedStateTag.IsValid())
	{
		if (bDropInsteadOfPickup)
		{
			OutCommand.RemovedStateTags.AddTag(CarriedStateTag);
		}
		else
		{
			OutCommand.AddedStateTags.AddTag(CarriedStateTag);
		}
	}
	return true;
}

bool UDreamPickupCapability::Validate(const AInteractiveAssemblyActor& Target,
	const FDreamInteractionCommand& Command,
	FText& OutFailure) const
{
	if (!UDreamInteractionCapability::Validate(Target, Command, OutFailure) ||
		Command.Type != EDreamCommandType::Composite || !Command.bSetsCarriedState)
	{
		OutFailure = FText::FromString(TEXT("拾取命令格式不正确。"));
		return false;
	}
	if (Command.bIsCarried == bDropInsteadOfPickup)
	{
		OutFailure = FText::FromString(TEXT("拾取命令的携带状态与能力配置不一致。"));
		return false;
	}
	return true;
}

