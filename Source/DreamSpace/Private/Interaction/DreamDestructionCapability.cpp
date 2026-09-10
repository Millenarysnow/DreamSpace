#include "DreamDestructionCapability.h"
UDreamDestructionCapability::UDreamDestructionCapability()
{
	CapabilityId = TEXT("Destruction");
	DisplayName = FText::FromString(TEXT("破坏"));
	AllowedSourceStates = {EDreamNodeRuntimeState::Intact, EDreamNodeRuntimeState::Damaged};
	TargetState = EDreamNodeRuntimeState::Broken;
}
