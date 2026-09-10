#include "DreamStateTransitionCapability.h"

#include "InteractiveAssemblyActor.h"

UDreamStateTransitionCapability::UDreamStateTransitionCapability()
{
	CapabilityTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Interaction.StateTransition")), false);
	AllowedModes.Add(EDreamInteractionMode::ThirdPerson);
	AllowedModes.Add(EDreamInteractionMode::Overview);
	AllowedSourceStates.Add(EDreamNodeRuntimeState::Intact);
	AllowedSourceStates.Add(EDreamNodeRuntimeState::Damaged);
	AllowedSourceStates.Add(EDreamNodeRuntimeState::Broken);
}

bool UDreamStateTransitionCapability::CanStart(const AInteractiveAssemblyActor& Target,
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
		OutFailure = FText::FromString(TEXT("状态转移能力需要开始或确认意图。"));
		return false;
	}
	return true;
}

bool UDreamStateTransitionCapability::BuildCommand(AInteractiveAssemblyActor& Target,
	const FDreamInteractionIntent& Intent,
	FDreamInteractionCommand& OutCommand,
	FText& OutFailure) const
{
	TArray<FGuid> NodeIds = ConfiguredNodeIds;
	if (NodeIds.IsEmpty() && Intent.TargetNodeId.IsValid())
	{
		NodeIds.Add(Intent.TargetNodeId);
	}
	if (NodeIds.IsEmpty())
	{
		for (const FDreamNodeState& Node : Target.AssemblyState.Nodes)
		{
			if (!Node.ParentNodeId.IsValid() && Node.IsOperational())
			{
				NodeIds.Add(Node.NodeId);
			}
		}
	}
	if (NodeIds.IsEmpty())
	{
		OutFailure = FText::FromString(TEXT("状态转移没有目标节点。"));
		return false;
	}

	OutCommand = FDreamInteractionCommand();
	OutCommand.Type = EDreamCommandType::SetNodeState;
	OutCommand.CommandId = FGuid::NewGuid();
	OutCommand.AssemblyId = Target.GetAssemblyId();
	OutCommand.ExpectedStateVersion = Target.AssemblyState.StateVersion;

	TSet<FGuid> AddedIds;
	for (const FGuid& NodeId : NodeIds)
	{
		if (AddedIds.Contains(NodeId))
		{
			continue;
		}
		const FDreamNodeState* Node = Target.AssemblyState.FindNode(NodeId);
		if (!Node || !Node->IsOperational())
		{
			OutFailure = FText::FromString(TEXT("状态转移目标不可操作。"));
			return false;
		}
		if (!AllowedSourceStates.IsEmpty() && !AllowedSourceStates.Contains(Node->RuntimeState))
		{
			OutFailure = FText::FromString(TEXT("目标节点当前状态不满足转移条件。"));
			return false;
		}
		FDreamNodeStateChange& Change = OutCommand.StateChanges.AddDefaulted_GetRef();
		Change.NodeId = NodeId;
		Change.NewRuntimeState = TargetState;
		Change.bExists = bExistsAfterTransition;
		Change.bLocked = bLockAfterTransition;
		AddedIds.Add(NodeId);
	}
	return true;
}

bool UDreamStateTransitionCapability::Validate(const AInteractiveAssemblyActor& Target,
	const FDreamInteractionCommand& Command,
	FText& OutFailure) const
{
	if (!UDreamInteractionCapability::Validate(Target, Command, OutFailure))
	{
		return false;
	}
	if (Command.Type != EDreamCommandType::SetNodeState || Command.StateChanges.IsEmpty())
	{
		OutFailure = FText::FromString(TEXT("状态转移能力只能验证节点状态命令。"));
		return false;
	}
	for (const FDreamNodeStateChange& Change : Command.StateChanges)
	{
		if (Change.NewRuntimeState != TargetState || Change.bExists != bExistsAfterTransition ||
			Change.bLocked != bLockAfterTransition)
		{
			OutFailure = FText::FromString(TEXT("节点状态命令与能力配置不一致。"));
			return false;
		}
	}
	return true;
}

