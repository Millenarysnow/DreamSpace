#include "DreamInteractionSaveGame.h"

void UDreamInteractionSaveGame::CaptureAssemblyState(const FDreamAssemblyState& State)
{
	if (FDreamAssemblyState* Existing = AssemblyStates.FindByPredicate(
		[&State](const FDreamAssemblyState& Item) { return Item.AssemblyId == State.AssemblyId; }))
	{
		*Existing = State;
	}
	else
	{
		AssemblyStates.Add(State);
	}
	SavedAtUtc = FDateTime::UtcNow();
}

const FDreamAssemblyState* UDreamInteractionSaveGame::FindAssemblyState(const FGuid& AssemblyId) const
{
	return AssemblyStates.FindByPredicate(
		[&AssemblyId](const FDreamAssemblyState& State) { return State.AssemblyId == AssemblyId; });
}

