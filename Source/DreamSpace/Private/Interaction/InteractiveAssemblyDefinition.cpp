#include "InteractiveAssemblyDefinition.h"
#include "DreamInteractionCapability.h"
#include "DreamStateMath.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

void UInteractiveAssemblyDefinition::AssignMissingNodeIds()
{
	Modify();
	for (auto& Node : Nodes)
		if (!Node.NodeId.IsValid())
			Node.NodeId = FGuid::NewGuid();
	MarkPackageDirty();
}
void UInteractiveAssemblyDefinition::CheckDefinition()
{
	TArray<FString> Errors;
	ValidateDefinition(Errors);
	for (const auto& Error : Errors)
		UE_LOG(LogDreamInteraction, Warning, TEXT("%s: %s"), *GetName(), *Error);
	if (Errors.IsEmpty())
		UE_LOG(LogDreamInteraction, Log, TEXT("%s：对象定义校验通过。"), *GetName());
}
const FDreamNodeDefinition* UInteractiveAssemblyDefinition::FindNodeDefinition(const FGuid& Id) const
{
	return Nodes.FindByPredicate([&](const auto& Node) { return Node.NodeId == Id; });
}
FPrimaryAssetId UInteractiveAssemblyDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(FPrimaryAssetType(TEXT("InteractiveAssembly")), GetFName());
}
bool UInteractiveAssemblyDefinition::ValidateDefinition(TArray<FString>& Errors) const
{
	Errors.Reset();
	FDreamAssemblyState State;
	State.AssemblyId = FGuid(1, 0, 0, 0);
	auto CheckGravity = [&](const FDreamGravitySettings& G)
	{
		if (G.bEnabled && (G.Extent.ContainsNaN() || G.Extent.GetMin() <= 0 || G.Center.ContainsNaN() ||
							  G.LocalDirection.ContainsNaN() || G.LocalDirection.IsNearlyZero()))
			Errors.Add(TEXT("重力体积的方向或范围无效。"));
	};
	auto CheckCapabilities = [&](const auto& Items)
	{
		TSet<FName> Ids;
		for (const auto& Capability : Items)
		{
			if (!Capability)
			{
				Errors.Add(TEXT("能力列表中存在空配置。"));
				continue;
			}
			if (Capability->CapabilityId.IsNone() || Ids.Contains(Capability->CapabilityId))
				Errors.Add(TEXT("同一层级的能力 ID 为空或重复。"));
			Ids.Add(Capability->CapabilityId);
			Capability->ValidateConfiguration(*this, Errors);
		}
	};
	CheckGravity(Gravity);
	CheckCapabilities(Capabilities);
	for (const auto& Node : Nodes)
	{
		FDreamNodeState N;
		N.NodeId = Node.NodeId;
		N.ParentNodeId = Node.ParentNodeId;
		N.LocalTransform = Node.DefaultLocalTransform;
		State.Nodes.Add(N);
		if (Node.bCollisionEnabled && (Node.CollisionExtent.ContainsNaN() || Node.CollisionExtent.GetMin() <= 0 ||
										  Node.CollisionCenter.ContainsNaN()))
			Errors.Add(TEXT("碰撞代理必须具有正的半尺寸和有限的中心坐标。"));
		if (!DreamState::IsFinitePositive(Node.MeshTransform))
			Errors.Add(TEXT("Mesh 表现变换无效。"));
		// FTransform 无法表达剪切；节点坐标系统一使用均匀缩放，外观长宽比放在 MeshTransform。
		if (!Node.DefaultLocalTransform.GetScale3D().AllComponentsEqual(0.001))
			Errors.Add(TEXT("节点坐标系必须均匀缩放；请用 MeshTransform 设置外观长宽比。"));
		for (const auto& Socket : Node.Sockets)
			if (Socket.Key.IsNone() || !DreamState::IsFinitePositive(Socket.Value))
				Errors.Add(TEXT("Socket 名称或变换无效。"));
		CheckGravity(Node.Gravity);
		CheckCapabilities(Node.Capabilities);
	}
	FText Failure;
	if (!DreamState::Validate(State, Failure))
		Errors.Add(Failure.ToString());
	return Errors.IsEmpty();
}
#if WITH_EDITOR
EDataValidationResult UInteractiveAssemblyDefinition::IsDataValid(FDataValidationContext& Context) const
{
	TArray<FString> Errors;
	ValidateDefinition(Errors);
	for (const auto& Error : Errors)
		Context.AddError(FText::FromString(Error));
	return Errors.IsEmpty() ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
