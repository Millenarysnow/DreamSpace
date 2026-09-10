#include "DreamExampleDefinitions.h"
#include "InteractiveAssemblyDefinition.h"
#include "DreamTransformCapability.h"
#include "DreamDestructionCapability.h"
#include "DreamPickupCapability.h"
#include "DreamKeyDoorCapability.h"
#include "DreamInteractionTags.h"
namespace
{
FDreamNodeDefinition Node(int32 Id, FName Name, FVector Center, FVector Extent, FGuid Parent = FGuid())
{
	FDreamNodeDefinition N;
	N.NodeId = FGuid(0xD5, 0, 0, Id);
	N.NodeName = Name;
	N.ParentNodeId = Parent;
	N.DefaultLocalTransform = FTransform(Center);
	N.CollisionExtent = Extent;
	N.Mesh = FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"));
	N.MeshTransform = FTransform(FQuat::Identity, FVector::ZeroVector, Extent / 50);
	N.Material = FSoftObjectPath(TEXT("/Game/DreamInteraction/Materials/M_Prototype.M_Prototype"));
	return N;
}
UDreamTransformCapability* Transform(UObject* Owner)
{
	auto* C = NewObject<UDreamTransformCapability>(Owner);
	C->bAllowTranslation = true;
	C->bAllowScaling = true;
	return C;
}
} // namespace
void DreamExamples::MakeCube(UInteractiveAssemblyDefinition& D)
{
	D.DisplayName = FText::FromString(TEXT("分层魔方"));
	D.Nodes.Reset();
	D.Capabilities.Reset();
	auto* Root = Transform(&D);
	Root->bAllowInputAxis = true;
	D.Capabilities.Add(Root);
	auto* Split = NewObject<UDreamDestructionCapability>(&D);
	Split->DisplayName = FText::FromString(TEXT("释放部件"));
	Split->TargetState = EDreamNodeRuntimeState::Released;
	D.Capabilities.Add(Split);
	for (int32 Layer = 0; Layer < 3; ++Layer)
	{
		auto L = Node(
			Layer + 1, FName(*FString::Printf(TEXT("Layer_%d"), Layer + 1)), FVector(0, 0, Layer * 120), FVector(50));
		L.Mesh.Reset();
		L.bCollisionEnabled = false;
		L.bSelectable = false;
		auto* C = Transform(&D);
		C->ConfiguredNodeIds.Add(L.NodeId);
		L.Capabilities.Add(C);
		D.Nodes.Add(L);
		for (int32 X = -1; X <= 1; ++X)
			for (int32 Y = -1; Y <= 1; ++Y)
			{
				auto Block = Node(100 + Layer * 9 + (X + 1) * 3 + Y + 1,
					FName(*FString::Printf(TEXT("Block_%d_%d_%d"), Layer, X + 1, Y + 1)), FVector(X * 120, Y * 120, 0),
					FVector(50), L.NodeId);
				const FLinearColor Colors[3] = {
					FLinearColor(0.05, 0.35, 0.8), FLinearColor(0.8, 0.2, 0.04), FLinearColor(0.04, 0.6, 0.25)};
				Block.Tint = Colors[Layer] * (0.4f + 0.075f * ((X + 1) * 3 + Y + 1));
				D.Nodes.Add(Block);
			}
	}
}
void DreamExamples::MakeKey(UInteractiveAssemblyDefinition& D)
{
	D.DisplayName = FText::FromString(TEXT("巨大钥匙"));
	D.Nodes.Reset();
	D.Capabilities.Reset();
	D.GameplayTags.AddTag(DreamTags::ItemKey);
	auto* C = Transform(&D);
	C->bTargetAssemblyRoot = true;
	C->bAllowRotation = false;
	C->MinimumScale = 0.1;
	C->MaximumScale = 2;
	C->PivotMode = EDreamPivotMode::ConfiguredPoint;
	C->PivotPoint = FVector(0, 0, -210);
	D.Capabilities.Add(C);
	auto* Pickup = NewObject<UDreamPickupCapability>(&D);
	Pickup->bTargetAssemblyRoot = true;
	D.Capabilities.Add(Pickup);
	auto* Drop = NewObject<UDreamPickupCapability>(&D);
	Drop->CapabilityId = TEXT("Drop");
	Drop->DisplayName = FText::FromString(TEXT("放下"));
	Drop->bDropInsteadOfPickup = true;
	D.Capabilities.Add(Drop);
	auto Shaft = Node(1, TEXT("Shaft"), FVector::ZeroVector, FVector(100, 25, 210));
	Shaft.CollisionCenter = FVector(25, 0, 0);
	Shaft.MeshTransform = FTransform(FQuat::Identity, FVector(0, 0, -30), FVector(0.4, 0.4, 3.6));
	D.Nodes.Add(Shaft);
	auto Head = Node(2, TEXT("Head"), FVector(0, 0, 150), FVector(75, 20, 50));
	Head.bCollisionEnabled = false;
	D.Nodes.Add(Head);
	auto Tooth = Node(3, TEXT("Tooth"), FVector(45, 0, -140), FVector(45, 20, 20));
	Tooth.bCollisionEnabled = false;
	D.Nodes.Add(Tooth);
	for (auto& N : D.Nodes)
		N.Tint = FLinearColor(1, 0.65, 0.08);
}
void DreamExamples::MakeGravityRoom(UInteractiveAssemblyDefinition& D)
{
	D.DisplayName = FText::FromString(TEXT("局部重力房间"));
	D.Nodes.Reset();
	D.Capabilities.Reset();
	auto* C = Transform(&D);
	C->bAllowInputAxis = true;
	C->bAllowScaling = false;
	C->PivotMode = EDreamPivotMode::NodeOrigin;
	D.Capabilities.Add(C);
	// 房间操作固定作用于根；玩家在体积内时随参考系改变位置、速度、朝向和重力。
	C->bTargetAssemblyRoot = true;
	D.Gravity.bEnabled = true;
	D.Gravity.Extent = FVector(360, 360, 220);
	D.Gravity.Priority = 10;
	D.Nodes.Add(Node(1, TEXT("Floor"), FVector(0, 0, -200), FVector(350, 350, 10)));
	D.Nodes.Add(Node(2, TEXT("BackWall"), FVector(340, 0, 0), FVector(10, 350, 190)));
	D.Nodes.Add(Node(3, TEXT("LeftWall"), FVector(0, -340, 0), FVector(330, 10, 190)));
	D.Nodes.Add(Node(4, TEXT("RightWall"), FVector(0, 340, 0), FVector(330, 10, 190)));
	D.Nodes.Add(Node(5, TEXT("Ceiling"), FVector(0, 0, 200), FVector(350, 350, 10)));
	// 一个普通内部方块，属于节点层级且没有独立刚体模拟，始终随房间一致变化。
	D.Nodes.Add(Node(6, TEXT("InteriorObject"), FVector(180, 0, -130), FVector(45, 45, 60)));
	for (auto& N : D.Nodes)
		N.Tint = N.NodeName == TEXT("InteriorObject") ? FLinearColor(0.9, 0.2, 0.1) : FLinearColor(0.18, 0.5, 0.62);
}
void DreamExamples::MakeDoor(UInteractiveAssemblyDefinition& D)
{
	D.DisplayName = FText::FromString(TEXT("钥匙门"));
	D.Nodes.Reset();
	D.Capabilities.Reset();
	D.Nodes.Add(Node(1, TEXT("Door"), FVector::ZeroVector, FVector(20, 130, 160)));
	D.Nodes[0].Tint = FLinearColor(0.35, 0.12, 0.5);
	D.Capabilities.Add(NewObject<UDreamKeyDoorCapability>(&D));
}
