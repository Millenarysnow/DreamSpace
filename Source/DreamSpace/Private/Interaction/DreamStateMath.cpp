#include "DreamStateMath.h"

bool DreamState::IsFinitePositive(const FTransform& Transform)
{
	return Transform.IsValid() && !Transform.ContainsNaN() && Transform.GetScale3D().GetMin() > 0.0001;
}

bool DreamState::Validate(const FDreamAssemblyState& State, FText& Failure)
{
	auto Reject = [&](const TCHAR* Message)
	{
		Failure = FText::FromString(Message);
		return false;
	};
	if (!State.AssemblyId.IsValid() || !IsFinitePositive(State.RootTransform) || State.StateVersion < 0 ||
		State.StateVersion >= MAX_int64 - 1)
		return Reject(TEXT("装配体 ID、根变换或版本无效。"));
	if (State.bIsCarried && (!State.CarrierId.IsValid() || !IsFinitePositive(State.CarryTransform)))
		return Reject(TEXT("携带状态缺少持有者或有效的插槽变换。"));
	TSet<FGuid> Ids;
	for (const auto& Node : State.Nodes)
	{
		if (!Node.NodeId.IsValid() || Ids.Contains(Node.NodeId) || !IsFinitePositive(Node.LocalTransform))
			return Reject(TEXT("节点 ID 重复、缺失或局部变换无效。"));
		if (static_cast<uint8>(Node.RuntimeState) > static_cast<uint8>(EDreamNodeRuntimeState::Destroyed))
			return Reject(TEXT("未知的节点状态。"));
		if (Node.RuntimeState == EDreamNodeRuntimeState::Released && Node.ParentNodeId.IsValid())
			return Reject(TEXT("释放后的根节点不能保留父节点引用。"));
		Ids.Add(Node.NodeId);
	}
	for (const auto& Node : State.Nodes)
	{
		TSet<FGuid> Chain;
		const FDreamNodeState* Current = &Node;
		while (Current)
		{
			if (Chain.Contains(Current->NodeId))
				return Reject(TEXT("节点层级存在环。"));
			Chain.Add(Current->NodeId);
			if (!Current->ParentNodeId.IsValid())
				break;
			Current = State.FindNode(Current->ParentNodeId);
			if (!Current)
				return Reject(TEXT("节点引用了不存在的父节点。"));
		}
	}
	return true;
}

bool DreamState::WorldTransform(const FDreamAssemblyState& State, const FGuid& NodeId, FTransform& Result)
{
	Result = FTransform::Identity;
	if (!NodeId.IsValid())
	{
		Result = State.RootTransform;
		return true;
	}
	TSet<FGuid> Visited;
	const FDreamNodeState* Node = State.FindNode(NodeId);
	while (Node)
	{
		if (Visited.Contains(Node->NodeId))
			return false;
		Visited.Add(Node->NodeId);
		Result = Result * Node->LocalTransform;
		if (!Node->ParentNodeId.IsValid())
		{
			if (Node->RuntimeState != EDreamNodeRuntimeState::Released)
				Result = Result * State.RootTransform;
			return true;
		}
		Node = State.FindNode(Node->ParentNodeId);
	}
	return false;
}

bool DreamState::IsDescendant(const FDreamAssemblyState& State, const FGuid& NodeId, const FGuid& Ancestor)
{
	if (!Ancestor.IsValid())
		return true;
	FGuid Current = NodeId;
	TSet<FGuid> Visited;
	while (Current.IsValid() && !Visited.Contains(Current))
	{
		if (Current == Ancestor)
			return true;
		Visited.Add(Current);
		const auto* Node = State.FindNode(Current);
		if (!Node)
			return false;
		Current = Node->ParentNodeId;
	}
	return false;
}

bool DreamState::IsVisible(const FDreamAssemblyState& State, const FGuid& NodeId)
{
	const auto* Node = State.FindNode(NodeId);
	TSet<FGuid> Visited;
	while (Node && !Visited.Contains(Node->NodeId))
	{
		if (!Node->bExists || Node->RuntimeState == EDreamNodeRuntimeState::Destroyed)
			return false;
		Visited.Add(Node->NodeId);
		if (!Node->ParentNodeId.IsValid())
			return true;
		Node = State.FindNode(Node->ParentNodeId);
	}
	return false;
}

bool DreamState::Equivalent(const FDreamAssemblyState& A, const FDreamAssemblyState& B)
{
	// 数组顺序和版本不参与语义比较：Undo 后版本递增，但仍应允许连续撤销。
	if (A.AssemblyId != B.AssemblyId || A.DefinitionPath != B.DefinitionPath ||
		!A.RootTransform.Equals(B.RootTransform, 0.001) || A.Nodes.Num() != B.Nodes.Num() ||
		A.StateTags != B.StateTags || A.bIsCarried != B.bIsCarried || A.CarrierId != B.CarrierId ||
		!A.CarryTransform.Equals(B.CarryTransform, 0.001))
		return false;
	for (const auto& Node : A.Nodes)
	{
		const auto* Other = B.FindNode(Node.NodeId);
		if (!Other || Node.ParentNodeId != Other->ParentNodeId || Node.RuntimeState != Other->RuntimeState ||
			Node.bExists != Other->bExists || Node.bLocked != Other->bLocked ||
			!Node.LocalTransform.Equals(Other->LocalTransform, 0.001))
			return false;
	}
	return true;
}
