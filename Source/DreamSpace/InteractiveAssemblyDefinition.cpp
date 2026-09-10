#include "InteractiveAssemblyDefinition.h"

#include "UObject/PrimaryAssetId.h"

UInteractiveAssemblyDefinition::UInteractiveAssemblyDefinition()
{
	DisplayName = FText::FromString(TEXT("可交互装配体"));
}

bool UInteractiveAssemblyDefinition::ValidateDefinition(TArray<FString>& OutErrors) const
{
	OutErrors.Reset();
	TSet<FGuid> KnownIds;

	for (const FDreamNodeDefinition& Node : Nodes)
	{
		if (!Node.NodeId.IsValid())
		{
			OutErrors.Add(FString::Printf(TEXT("节点 %s 没有有效的稳定 ID。"), *Node.NodeName.ToString()));
			continue;
		}

		if (KnownIds.Contains(Node.NodeId))
		{
			OutErrors.Add(FString::Printf(TEXT("节点 %s 使用了重复的稳定 ID %s。"),
				*Node.NodeName.ToString(), *Node.NodeId.ToString()));
		}
		KnownIds.Add(Node.NodeId);
	}

	for (const FDreamNodeDefinition& Node : Nodes)
	{
		if (Node.ParentNodeId.IsValid() && !KnownIds.Contains(Node.ParentNodeId))
		{
			OutErrors.Add(FString::Printf(TEXT("节点 %s 的父节点 %s 不存在。"),
				*Node.NodeName.ToString(), *Node.ParentNodeId.ToString()));
		}
	}

	// 用 DFS 检查父节点环。环会让局部变换无法计算，必须在资产阶段报错。
	TMap<FGuid, uint8> VisitState;
	for (const FDreamNodeDefinition& Node : Nodes)
	{
		FGuid Current = Node.NodeId;
		TSet<FGuid> Chain;
		while (Current.IsValid())
		{
			if (Chain.Contains(Current))
			{
				OutErrors.Add(FString::Printf(TEXT("节点层级包含环，起点为 %s。"), *Current.ToString()));
				break;
			}
			Chain.Add(Current);
			const FDreamNodeDefinition* CurrentNode = FindNodeDefinition(Current);
			if (!CurrentNode)
			{
				break;
			}
			Current = CurrentNode->ParentNodeId;
		}
	}

	if (DefaultLocalGravityDirection.IsNearlyZero())
	{
		OutErrors.Add(TEXT("默认局部重力方向不能为零向量。"));
	}

	return OutErrors.IsEmpty();
}

const FDreamNodeDefinition* UInteractiveAssemblyDefinition::FindNodeDefinition(const FGuid& NodeId) const
{
	return Nodes.FindByPredicate([&NodeId](const FDreamNodeDefinition& Node)
	{
		return Node.NodeId == NodeId;
	});
}

FPrimaryAssetId UInteractiveAssemblyDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(FPrimaryAssetType(TEXT("InteractiveAssembly")), GetFName());
}
