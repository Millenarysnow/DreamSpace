#include "InteractiveAssemblyActor.h"

#include "DreamInteractionCapability.h"
#include "DreamInteractionWorldSubsystem.h"
#include "InteractiveAssemblyDefinition.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"

AInteractiveAssemblyActor::AInteractiveAssemblyActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("AssemblyRoot"));
	RootComponent->SetMobility(EComponentMobility::Movable);
}

void AInteractiveAssemblyActor::BeginPlay()
{
	Super::BeginPlay();

	if (!bInitialized)
	{
		InitializeFromDefinition();
	}

	if (UWorld* World = GetWorld())
	{
		if (UDreamInteractionWorldSubsystem* InteractionSubsystem = World->GetSubsystem<UDreamInteractionWorldSubsystem>())
		{
			InteractionSubsystem->RegisterAssembly(this);
		}
	}
}

void AInteractiveAssemblyActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UDreamInteractionWorldSubsystem* InteractionSubsystem = World->GetSubsystem<UDreamInteractionWorldSubsystem>())
		{
			InteractionSubsystem->UnregisterAssembly(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

bool AInteractiveAssemblyActor::InitializeFromDefinition()
{
	if (!Definition)
	{
		UE_LOG(LogDreamInteraction, Error, TEXT("装配体 %s 没有配置对象定义资产。"), *GetName());
		return false;
	}

	TArray<FString> DefinitionErrors;
	if (!Definition->ValidateDefinition(DefinitionErrors))
	{
		for (const FString& Error : DefinitionErrors)
		{
			UE_LOG(LogDreamInteraction, Error, TEXT("装配体定义 %s：%s"), *Definition->GetName(), *Error);
		}
		return false;
	}

	if (!AssemblyId.IsValid())
	{
		AssemblyId = FGuid::NewGuid();
	}

	DestroyNodeComponents();
	AssemblyState = FDreamAssemblyState();
	AssemblyState.AssemblyId = AssemblyId;
	AssemblyState.RootTransform = GetActorTransform();
	AssemblyState.LocalGravityDirection = Definition->DefaultLocalGravityDirection.GetSafeNormal();
	AssemblyState.bProvidesGravity = Definition->bProvidesGravity;
	AssemblyState.GravityPriority = Definition->GravityPriority;
	AssemblyState.StateVersion = 0;

	// 先创建所有组件，再建立父子关系，因此资产中的节点顺序可以自由调整。
	for (const FDreamNodeDefinition& NodeDefinition : Definition->Nodes)
	{
		FDreamNodeState NodeState;
		NodeState.NodeId = NodeDefinition.NodeId;
		NodeState.ParentNodeId = NodeDefinition.ParentNodeId;
		NodeState.LocalTransform = NodeDefinition.DefaultLocalTransform;
		AssemblyState.Nodes.Add(NodeState);

		if (!NodeDefinition.bCreateSceneComponent)
		{
			continue;
		}

		const FString ComponentName = FString::Printf(TEXT("Node_%s"), *NodeDefinition.NodeId.ToString(EGuidFormats::Digits));
		USceneComponent* NodeComponent = nullptr;
		if (NodeDefinition.Mesh.IsValid() || !NodeDefinition.Mesh.IsNull())
		{
			UStaticMeshComponent* MeshComponent = NewObject<UStaticMeshComponent>(this,
				MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), FName(*ComponentName)));
			MeshComponent->SetStaticMesh(NodeDefinition.Mesh.LoadSynchronous());
			MeshComponent->SetMobility(EComponentMobility::Movable);
			NodeComponent = MeshComponent;
		}
		else
		{
			NodeComponent = NewObject<USceneComponent>(this,
				MakeUniqueObjectName(this, USceneComponent::StaticClass(), FName(*ComponentName)));
			NodeComponent->SetMobility(EComponentMobility::Movable);
		}

		NodeComponent->RegisterComponent();
		NodeComponents.Add(NodeDefinition.NodeId, NodeComponent);
	}

	for (const FDreamNodeDefinition& NodeDefinition : Definition->Nodes)
	{
		TObjectPtr<USceneComponent>* ComponentPtr = NodeComponents.Find(NodeDefinition.NodeId);
		if (!ComponentPtr || !ComponentPtr->Get())
		{
			continue;
		}

		USceneComponent* NodeParentComponent = RootComponent;
		if (NodeDefinition.ParentNodeId.IsValid())
		{
			if (TObjectPtr<USceneComponent>* ParentPtr = NodeComponents.Find(NodeDefinition.ParentNodeId))
			{
				NodeParentComponent = ParentPtr->Get();
			}
		}
		ComponentPtr->Get()->AttachToComponent(NodeParentComponent, FAttachmentTransformRules::KeepRelativeTransform);
	}

	bInitialized = true;
	ApplyStateToComponents(AssemblyState);
	return true;
}

bool AInteractiveAssemblyActor::IsOperational() const
{
	return bInitialized && AssemblyState.AssemblyId.IsValid();
}

bool AInteractiveAssemblyActor::IsNodeOperational(const FGuid& NodeId) const
{
	const FDreamNodeState* Node = AssemblyState.FindNode(NodeId);
	return Node && Node->IsOperational();
}

USceneComponent* AInteractiveAssemblyActor::FindNodeComponent(const FGuid& NodeId) const
{
	if (const TObjectPtr<USceneComponent>* Component = NodeComponents.Find(NodeId))
	{
		return Component->Get();
	}
	return nullptr;
}

bool AInteractiveAssemblyActor::GetNodeAssemblyTransformRecursive(const FGuid& NodeId,
	TMap<FGuid, FTransform>& Cache,
	TSet<FGuid>& Visiting,
	FTransform& OutTransform) const
{
	if (const FTransform* Cached = Cache.Find(NodeId))
	{
		OutTransform = *Cached;
		return true;
	}
	if (Visiting.Contains(NodeId))
	{
		return false;
	}

	const FDreamNodeState* Node = AssemblyState.FindNode(NodeId);
	if (!Node)
	{
		return false;
	}

	Visiting.Add(NodeId);
	FTransform Result = Node->LocalTransform;
	if (Node->ParentNodeId.IsValid())
	{
		FTransform ParentTransform;
		if (!GetNodeAssemblyTransformRecursive(Node->ParentNodeId, Cache, Visiting, ParentTransform))
		{
			Visiting.Remove(NodeId);
			return false;
		}
		Result = Result * ParentTransform;
	}
	Visiting.Remove(NodeId);
	Cache.Add(NodeId, Result);
	OutTransform = Result;
	return true;
}

bool AInteractiveAssemblyActor::GetNodeAssemblyTransform(const FGuid& NodeId, FTransform& OutTransform) const
{
	TMap<FGuid, FTransform> Cache;
	TSet<FGuid> Visiting;
	return GetNodeAssemblyTransformRecursive(NodeId, Cache, Visiting, OutTransform);
}

bool AInteractiveAssemblyActor::BuildTransformCommand(const TArray<FGuid>& TargetNodeIds,
	const FVector& Axis,
	float RotationDegrees,
	float ScaleDelta,
	EDreamReferenceFrame ReferenceFrame,
	FDreamInteractionCommand& OutCommand,
	FText& OutFailure) const
{
	OutCommand = FDreamInteractionCommand();
	if (!bInitialized || TargetNodeIds.IsEmpty())
	{
		OutFailure = FText::FromString(TEXT("没有可变换的目标节点。"));
		return false;
	}
	if (!FMath::IsFinite(RotationDegrees) || !FMath::IsFinite(ScaleDelta))
	{
		OutFailure = FText::FromString(TEXT("变换参数不是有效数字。"));
		return false;
	}
	if (ScaleDelta <= -1.0f)
	{
		OutFailure = FText::FromString(TEXT("缩放结果必须大于零。"));
		return false;
	}

	// 只保留最上层目标。选择父节点时，子节点通过层级自然跟随，避免重复应用变换。
	TSet<FGuid> Selected;
	for (const FGuid& NodeId : TargetNodeIds)
	{
		if (!IsNodeOperational(NodeId))
		{
			OutFailure = FText::FromString(TEXT("目标节点不存在、已锁定或已销毁。"));
			return false;
		}
		Selected.Add(NodeId);
	}

	TArray<FGuid> TopLevelTargets;
	for (const FGuid& NodeId : Selected)
	{
		const FDreamNodeState* Node = AssemblyState.FindNode(NodeId);
		bool bHasSelectedAncestor = false;
		FGuid ParentId = Node ? Node->ParentNodeId : FGuid();
		while (ParentId.IsValid())
		{
			if (Selected.Contains(ParentId))
			{
				bHasSelectedAncestor = true;
				break;
			}
			const FDreamNodeState* Parent = AssemblyState.FindNode(ParentId);
			if (!Parent)
			{
				break;
			}
			ParentId = Parent->ParentNodeId;
		}
		if (!bHasSelectedAncestor)
		{
			TopLevelTargets.Add(NodeId);
		}
	}

	TMap<FGuid, FTransform> Cache;
	TSet<FGuid> Visiting;
	TArray<FTransform> CurrentTransforms;
	for (const FGuid& NodeId : TopLevelTargets)
	{
		FTransform Transform;
		if (!GetNodeAssemblyTransformRecursive(NodeId, Cache, Visiting, Transform))
		{
			OutFailure = FText::FromString(TEXT("无法计算目标节点的局部层级变换。"));
			return false;
		}
		CurrentTransforms.Add(Transform);
	}

	FVector Pivot = FVector::ZeroVector;
	for (const FTransform& Transform : CurrentTransforms)
	{
		Pivot += Transform.GetLocation();
	}
	Pivot /= static_cast<float>(CurrentTransforms.Num());

	FTransform AssemblyRootTransform = AssemblyState.RootTransform;
	FVector AssemblyAxis = Axis.GetSafeNormal();
	if (AssemblyAxis.IsNearlyZero())
	{
		OutFailure = FText::FromString(TEXT("旋转轴不能为零向量。"));
		return false;
	}
	if (ReferenceFrame == EDreamReferenceFrame::World)
	{
		AssemblyAxis = AssemblyRootTransform.InverseTransformVectorNoScale(AssemblyAxis).GetSafeNormal();
	}
	else if (ReferenceFrame == EDreamReferenceFrame::Node && TopLevelTargets.Num() == 1)
	{
		AssemblyAxis = CurrentTransforms[0].InverseTransformVectorNoScale(AssemblyAxis).GetSafeNormal();
	}

	const FQuat DeltaRotation(AssemblyAxis, FMath::DegreesToRadians(RotationDegrees));
	const float ScaleMultiplier = 1.0f + ScaleDelta;

	OutCommand.Type = EDreamCommandType::Transform;
	OutCommand.CommandId = FGuid::NewGuid();
	OutCommand.AssemblyId = AssemblyId;
	OutCommand.ExpectedStateVersion = AssemblyState.StateVersion;
	OutCommand.ReferenceFrame = ReferenceFrame;
	OutCommand.TransformChanges.Reserve(TopLevelTargets.Num());

	for (int32 Index = 0; Index < TopLevelTargets.Num(); ++Index)
	{
		const FGuid& NodeId = TopLevelTargets[Index];
		const FTransform& Current = CurrentTransforms[Index];
		FTransform NewAssembly = Current;
		const FVector Offset = Current.GetLocation() - Pivot;
		NewAssembly.SetLocation(Pivot + DeltaRotation.RotateVector(Offset) * ScaleMultiplier);
		NewAssembly.SetRotation(DeltaRotation * Current.GetRotation());
		NewAssembly.SetScale3D(Current.GetScale3D() * ScaleMultiplier);

		const FDreamNodeState* Node = AssemblyState.FindNode(NodeId);
		FTransform ParentTransform = FTransform::Identity;
		if (Node && Node->ParentNodeId.IsValid() &&
			!GetNodeAssemblyTransformRecursive(Node->ParentNodeId, Cache, Visiting, ParentTransform))
		{
			OutFailure = FText::FromString(TEXT("无法计算目标节点父级变换。"));
			return false;
		}

		FDreamNodeTransformChange& Change = OutCommand.TransformChanges.AddDefaulted_GetRef();
		Change.NodeId = NodeId;
		Change.NewLocalTransform = NewAssembly.GetRelativeTransform(ParentTransform);
	}

	return true;
}

bool AInteractiveAssemblyActor::PreviewCommand(const FDreamInteractionCommand& Command, FText& OutFailure)
{
	if (!Command.IsValid() || Command.AssemblyId != AssemblyId)
	{
		OutFailure = FText::FromString(TEXT("预览命令无效或目标不匹配。"));
		return false;
	}
	FDreamAssemblyState PreviewState = AssemblyState;
	for (const FDreamNodeTransformChange& Change : Command.TransformChanges)
	{
		FDreamNodeState* Node = PreviewState.FindNode(Change.NodeId);
		if (!Node || !Node->IsOperational())
		{
			OutFailure = FText::FromString(TEXT("预览包含不可操作节点。"));
			return false;
		}
		Node->LocalTransform = Change.NewLocalTransform;
	}
	ApplyStateToComponents(PreviewState);
	bHasPreview = true;
	return true;
}

void AInteractiveAssemblyActor::CancelPreview()
{
	if (bHasPreview)
	{
		ApplyStateToComponents(AssemblyState);
		bHasPreview = false;
	}
}

bool AInteractiveAssemblyActor::ApplyState(const FDreamAssemblyState& NewState, FText& OutFailure)
{
	if (NewState.AssemblyId != AssemblyId)
	{
		OutFailure = FText::FromString(TEXT("状态快照的装配体 ID 不匹配。"));
		return false;
	}
	if (!NewState.RootTransform.IsValid())
	{
		OutFailure = FText::FromString(TEXT("状态快照包含无效的根变换。"));
		return false;
	}

	AssemblyState = NewState;
	bInitialized = true;
	bHasPreview = false;
	ApplyStateToComponents(AssemblyState);
	OnStateChanged.Broadcast(AssemblyState);
	return true;
}

void AInteractiveAssemblyActor::ApplyStateToComponents(const FDreamAssemblyState& StateToApply)
{
	SetActorTransform(StateToApply.RootTransform);
	for (const TPair<FGuid, TObjectPtr<USceneComponent>>& Pair : NodeComponents)
	{
		const FDreamNodeState* Node = StateToApply.FindNode(Pair.Key);
		if (!Node || !Pair.Value)
		{
			continue;
		}
		Pair.Value->SetRelativeTransform(Node->LocalTransform);
		const bool bVisible = Node->bExists && Node->RuntimeState != EDreamNodeRuntimeState::Destroyed;
		Pair.Value->SetVisibility(bVisible, true);
		if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Pair.Value.Get()))
		{
			PrimitiveComponent->SetCollisionEnabled(bVisible ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		}
	}
}

FVector AInteractiveAssemblyActor::GetWorldGravityDirection() const
{
	const FVector LocalDirection = AssemblyState.LocalGravityDirection.GetSafeNormal();
	if (LocalDirection.IsNearlyZero())
	{
		return FVector::DownVector;
	}
	return AssemblyState.RootTransform.GetRotation().RotateVector(LocalDirection).GetSafeNormal();
}

void AInteractiveAssemblyActor::GetCapabilitiesForNode(const FGuid& NodeId,
	TArray<UDreamInteractionCapability*>& OutCapabilities) const
{
	OutCapabilities.Reset();
	if (!Definition)
	{
		return;
	}
	for (const TObjectPtr<UDreamInteractionCapability>& Capability : Definition->Capabilities)
	{
		if (Capability)
		{
			OutCapabilities.Add(Capability.Get());
		}
	}
	if (const FDreamNodeDefinition* NodeDefinition = Definition->FindNodeDefinition(NodeId))
	{
		for (const TObjectPtr<UDreamInteractionCapability>& Capability : NodeDefinition->Capabilities)
		{
			if (Capability)
			{
				OutCapabilities.Add(Capability.Get());
			}
		}
	}
}

void AInteractiveAssemblyActor::DestroyNodeComponents()
{
	for (TPair<FGuid, TObjectPtr<USceneComponent>>& Pair : NodeComponents)
	{
		if (Pair.Value)
		{
			Pair.Value->UnregisterComponent();
			Pair.Value->DestroyComponent();
		}
	}
	NodeComponents.Reset();
}
