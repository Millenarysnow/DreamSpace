#include "DreamStateTransitionCapability.h"
#include "DreamStateMath.h"
UDreamStateTransitionCapability::UDreamStateTransitionCapability()
{
	CapabilityId = TEXT("StateTransition");
	DisplayName = FText::FromString(TEXT("切换状态"));
	AllowedSourceStates = {
		EDreamNodeRuntimeState::Intact, EDreamNodeRuntimeState::Damaged, EDreamNodeRuntimeState::Broken};
}
bool UDreamStateTransitionCapability::BuildResult(
	const FDreamCapabilityContext& C, FDreamInteractionCommand& Command, FText& Failure) const
{
	TArray<FGuid> Targets;
	if (!ResolveTargets(C, Targets, Failure))
		return false;
	if (Targets.Contains(FGuid()))
	{
		Targets.Reset();
		for (const auto& Node : C.State.Nodes)
			if (!Node.ParentNodeId.IsValid())
				Targets.Add(Node.NodeId);
	}
	for (const auto& Id : Targets)
	{
		const auto* Old = C.State.FindNode(Id);
		if (!Old || !Old->IsOperational() || !AllowedSourceStates.Contains(Old->RuntimeState))
		{
			Failure = FText::FromString(TEXT("节点状态不允许此转移。"));
			return false;
		}
		auto* Node = Command.ResultState.FindNode(Id);
		Node->RuntimeState = TargetState;
		Node->bExists = bExistsAfterTransition && TargetState != EDreamNodeRuntimeState::Destroyed;
		Node->bLocked = bLockAfterTransition;
		if (TargetState == EDreamNodeRuntimeState::Released)
		{
			// 脱离父链时转换为世界变换，子树的内部局部关系不变，也保持原有稳定 ID。
			DreamState::WorldTransform(C.State, Id, Node->LocalTransform);
			Node->ParentNodeId.Invalidate();
		}
		else if (Old->RuntimeState == EDreamNodeRuntimeState::Released)
		{
			// 从 Released 切回其他状态时，根节点重新以装配体为参考系。
			Node->LocalTransform = Old->LocalTransform.GetRelativeTransform(C.State.RootTransform);
		}
	}
	Command.ResultState.StateTags.AppendTags(AddedStateTags);
	Command.ResultState.StateTags.RemoveTags(RemovedStateTags);
	Command.Type = EDreamCommandType::SetNodeState;
	return true;
}
