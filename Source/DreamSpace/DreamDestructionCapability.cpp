#include "DreamDestructionCapability.h"

UDreamDestructionCapability::UDreamDestructionCapability()
{
	CapabilityTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Interaction.Destruction")), false);
	TargetState = EDreamNodeRuntimeState::Broken;
	AllowedModes.Reset();
	AllowedModes.Add(EDreamInteractionMode::ThirdPerson);
	AllowedSourceStates.Reset();
	AllowedSourceStates.Add(EDreamNodeRuntimeState::Intact);
	AllowedSourceStates.Add(EDreamNodeRuntimeState::Damaged);
}

bool UDreamDestructionCapability::BuildCommand(AInteractiveAssemblyActor& Target,
	const FDreamInteractionIntent& Intent,
	FDreamInteractionCommand& OutCommand,
	FText& OutFailure) const
{
	if (!UDreamStateTransitionCapability::BuildCommand(Target, Intent, OutCommand, OutFailure))
	{
		return false;
	}
	for (FDreamNodeStateChange& Change : OutCommand.StateChanges)
	{
		if (bReleaseNodeOnBreak)
		{
			Change.NewRuntimeState = EDreamNodeRuntimeState::Released;
		}
		Change.bExists = !bRemoveNodeOnBreak;
	}
	return true;
}

bool UDreamDestructionCapability::Validate(const AInteractiveAssemblyActor& Target,
	const FDreamInteractionCommand& Command,
	FText& OutFailure) const
{
	if (!UDreamInteractionCapability::Validate(Target, Command, OutFailure) ||
		Command.Type != EDreamCommandType::SetNodeState || Command.StateChanges.IsEmpty())
	{
		if (OutFailure.IsEmpty())
		{
			OutFailure = FText::FromString(TEXT("破坏命令格式不正确。"));
		}
		return false;
	}
	for (const FDreamNodeStateChange& Change : Command.StateChanges)
	{
		const EDreamNodeRuntimeState ExpectedState = bReleaseNodeOnBreak
			? EDreamNodeRuntimeState::Released
			: EDreamNodeRuntimeState::Broken;
		if (Change.NewRuntimeState != ExpectedState || Change.bExists != !bRemoveNodeOnBreak)
		{
			OutFailure = FText::FromString(TEXT("破坏命令与破坏能力配置不一致。"));
			return false;
		}
	}
	return true;
}
