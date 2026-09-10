#include "DreamInteractionCapability.h"
#include "InteractiveAssemblyActor.h"
#include "InteractiveAssemblyDefinition.h"
#include "DreamStateMath.h"

bool UDreamInteractionCapability::SupportsMode(EDreamInteractionMode Mode) const
{
	return AllowedModes.IsEmpty() || AllowedModes.Contains(Mode);
}
bool UDreamInteractionCapability::CanStart(const FDreamCapabilityContext& C, FText& Failure) const
{
	if (!bEnabledByDefault || !SupportsMode(C.Mode) || !C.Target.IsOperational())
	{
		Failure = FText::FromString(TEXT("当前模式或对象状态不允许此操作。"));
		return false;
	}
	if (!C.State.StateTags.HasAll(RequiredStateTags) || C.State.StateTags.HasAny(BlockedStateTags))
	{
		Failure = FText::FromString(TEXT("操作的状态标签条件尚未满足。"));
		return false;
	}
	TArray<FGuid> Targets;
	return ResolveTargets(C, Targets, Failure);
}
bool UDreamInteractionCapability::ResolveTargets(
	const FDreamCapabilityContext& C, TArray<FGuid>& Targets, FText& Failure) const
{
	TArray<FGuid> Requested = ConfiguredNodeIds.IsEmpty() ? C.Intent.TargetNodeIds : ConfiguredNodeIds;
	if (bTargetAssemblyRoot)
		Requested = {FGuid()};
	if (Requested.IsEmpty())
		Requested.Add(C.Intent.TargetNodeId);
	Targets.Reset();
	for (const auto& Id : Requested)
	{
		if (Id.IsValid())
		{
			const auto* Node = C.State.FindNode(Id);
			const auto* Def = C.Target.Definition->FindNodeDefinition(Id);
			if (!Node || !Def || !DreamState::IsVisible(C.State, Id) || !Def->GameplayTags.HasAll(RequiredNodeTags))
			{
				Failure = FText::FromString(TEXT("目标节点不存在、已移除或标签不匹配。"));
				return false;
			}
			// 锁定祖先会锁定整棵子树；不能通过点选子 Mesh 绕过锁定。
			while (Node)
			{
				if (!Node->IsOperational())
				{
					Failure = FText::FromString(TEXT("目标或其父级已经锁定。"));
					return false;
				}
				Node = Node->ParentNodeId.IsValid() ? C.State.FindNode(Node->ParentNodeId) : nullptr;
			}
		}
		else if (!C.Target.Definition->GameplayTags.HasAll(RequiredNodeTags))
		{
			Failure = FText::FromString(TEXT("装配体标签不满足操作条件。"));
			return false;
		}
		bool bCovered = false;
		for (const auto& Other : Requested)
			if (Other != Id && DreamState::IsDescendant(C.State, Id, Other))
			{
				bCovered = true;
				break;
			}
		if (!bCovered)
			Targets.AddUnique(Id);
	}
	for (const auto& Id : Targets)
		for (const auto& Node : C.State.Nodes)
			if (Node.bLocked && DreamState::IsDescendant(C.State, Node.NodeId, Id))
			{
				Failure = FText::FromString(TEXT("受影响的子树中存在锁定节点。"));
				return false;
			}
	return !Targets.IsEmpty();
}
bool UDreamInteractionCapability::BuildResult(
	const FDreamCapabilityContext& C, FDreamInteractionCommand& Command, FText& Failure) const
{
	Failure = FText::FromString(TEXT("抽象能力没有执行模型。"));
	return false;
}
void UDreamInteractionCapability::ValidateConfiguration(
	const UInteractiveAssemblyDefinition& Definition, TArray<FString>& Errors) const
{
	for (const auto& Id : ConfiguredNodeIds)
		if (!Definition.FindNodeDefinition(Id))
			Errors.Add(TEXT("能力目标集合引用了不存在的节点。"));
}
