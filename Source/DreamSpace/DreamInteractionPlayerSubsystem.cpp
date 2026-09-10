#include "DreamInteractionPlayerSubsystem.h"

void UDreamInteractionPlayerSubsystem::SetInteractionMode(EDreamInteractionMode NewMode)
{
	if (InteractionMode == NewMode)
	{
		return;
	}
	InteractionMode = NewMode;
	OnModeChanged.Broadcast(InteractionMode);
}

void UDreamInteractionPlayerSubsystem::ToggleInteractionMode()
{
	SetInteractionMode(InteractionMode == EDreamInteractionMode::ThirdPerson
		? EDreamInteractionMode::Overview
		: EDreamInteractionMode::ThirdPerson);
}

void UDreamInteractionPlayerSubsystem::SetSelection(const FGuid& AssemblyId, const FGuid& NodeId)
{
	if (SelectedAssemblyId == AssemblyId && SelectedNodeId == NodeId)
	{
		return;
	}
	SelectedAssemblyId = AssemblyId;
	SelectedNodeId = NodeId;
	OnSelectionChanged.Broadcast(SelectedAssemblyId, SelectedNodeId);
}

void UDreamInteractionPlayerSubsystem::ClearSelection()
{
	SetSelection(FGuid(), FGuid());
}

