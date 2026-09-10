#include "InteractiveAssemblyActor.h"
#include "InteractiveAssemblyDefinition.h"
#include "DreamInteractionCapability.h"
#include "DreamInteractionWorldSubsystem.h"
#include "DreamOccupantComponent.h"
#include "DreamStateMath.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DrawDebugHelpers.h"

AInteractiveAssemblyActor::AInteractiveAssemblyActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("AssemblyRoot"));
	RootComponent->SetMobility(EComponentMobility::Movable);
}
void AInteractiveAssemblyActor::PostActorCreated()
{
	Super::PostActorCreated();
	if (!AssemblyId.IsValid())
		AssemblyId = FGuid::NewGuid();
}
void AInteractiveAssemblyActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	// 动态表现组件不写进地图；编辑器重新加载地图时从定义资产恢复可见部件。
	if (Definition && !bInitialized && GetWorld() && !GetWorld()->IsGameWorld())
		InitializeFromDefinition();
}
void AInteractiveAssemblyActor::PostDuplicate(EDuplicateMode::Type Mode)
{
	Super::PostDuplicate(Mode);
	// PIE 是同一关卡实例的副本，必须保留 ID；编辑器复制则必须创建新实例 ID。
	if (Mode == EDuplicateMode::Normal)
		AssemblyId = FGuid::NewGuid();
}
void AInteractiveAssemblyActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (Definition && (!GetWorld()->IsGameWorld() || !bInitialized))
		InitializeFromDefinition();
}
void AInteractiveAssemblyActor::BeginPlay()
{
	Super::BeginPlay();
	if (!bInitialized)
		InitializeFromDefinition();
	if (bInitialized)
		GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>()->RegisterAssembly(this);
}
void AInteractiveAssemblyActor::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* System = GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>())
		System->UnregisterAssembly(this);
	Super::EndPlay(Reason);
}
bool AInteractiveAssemblyActor::InitializeFromDefinition()
{
	if (!Definition)
		return false;
	if (bInitialized && GetWorld()->IsGameWorld())
		return true;
	TArray<FString> Errors;
	if (!Definition->ValidateDefinition(Errors))
	{
		for (const auto& Error : Errors)
			UE_LOG(LogDreamInteraction, Error, TEXT("%s：%s"), *GetName(), *Error);
		return false;
	}
	if (!AssemblyId.IsValid())
		AssemblyId = FGuid::NewGuid();
	DestroyNodeComponents();
	AssemblyState = FDreamAssemblyState();
	AssemblyState.AssemblyId = AssemblyId;
	AssemblyState.DefinitionPath = FSoftObjectPath(Definition);
	AssemblyState.RootTransform = GetActorTransform();
	AssemblyState.StateTags = Definition->DefaultStateTags;
	for (const auto& D : Definition->Nodes)
	{
		auto& N = AssemblyState.Nodes.AddDefaulted_GetRef();
		N.NodeId = D.NodeId;
		N.ParentNodeId = D.ParentNodeId;
		N.LocalTransform = D.DefaultLocalTransform;
		// 纯逻辑父节点也创建参考系，避免没有 Mesh 的父节点丢失局部变换。
		auto* Box = NewObject<UBoxComponent>(
			this, MakeUniqueObjectName(this, UBoxComponent::StaticClass(), TEXT("Node")), RF_Transient);
		Box->SetMobility(EComponentMobility::Movable);
		Box->SetBoxExtent(D.CollisionExtent);
		Box->SetCollisionProfileName(TEXT("BlockAllDynamic"));
		Box->SetGenerateOverlapEvents(false);
		Box->SetHiddenInGame(true);
		Box->SetupAttachment(RootComponent);
		Box->RegisterComponent();
		NodeComponents.Add(D.NodeId, Box);
		// 碰撞中心通过 Shape 的独立子组件表达；节点自身原点不能被 Mesh 或代理中心改变。
		if (!D.Mesh.IsNull() || !D.BrokenMesh.IsNull())
		{
			auto* Mesh = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetStaticMesh(D.Mesh.LoadSynchronous());
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetupAttachment(Box);
			if (!D.Material.IsNull())
			{
				if (auto* Material = D.Material.LoadSynchronous())
				{
					auto* Instance = UMaterialInstanceDynamic::Create(Material, this);
					Instance->SetVectorParameterValue(TEXT("Tint"), D.Tint);
					Mesh->SetMaterial(0, Instance);
				}
			}
			Mesh->RegisterComponent();
			MeshComponents.Add(D.NodeId, Mesh);
		}
	}
	bInitialized = true;
	RefreshPresentation();
	return true;
}
void AInteractiveAssemblyActor::DestroyNodeComponents()
{
	for (const auto& Pair : MeshComponents)
		if (Pair.Value)
			Pair.Value->DestroyComponent();
	for (const auto& Pair : NodeComponents)
		if (Pair.Value)
			Pair.Value->DestroyComponent();
	MeshComponents.Reset();
	NodeComponents.Reset();
}
bool AInteractiveAssemblyActor::IsNodeOperational(const FGuid& Id) const
{
	const auto* N = AssemblyState.FindNode(Id);
	return N && N->IsOperational() && DreamState::IsVisible(AssemblyState, Id);
}
UBoxComponent* AInteractiveAssemblyActor::FindNodeComponent(const FGuid& Id) const
{
	const auto* Value = NodeComponents.Find(Id);
	return Value ? Value->Get() : nullptr;
}
bool AInteractiveAssemblyActor::FindNodeId(const UPrimitiveComponent* Component, FGuid& Id) const
{
	for (const auto& Pair : NodeComponents)
		if (Pair.Value == Component)
		{
			Id = Pair.Key;
			return true;
		}
	return false;
}
FDreamAssemblyState AInteractiveAssemblyActor::GetEffectiveState() const
{
	FDreamAssemblyState Result = AssemblyState;
	if (Result.bIsCarried && GetWorld())
		if (auto* System = GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>())
			if (auto* Carrier = System->FindOccupant(Result.CarrierId))
				Result.RootTransform = Result.CarryTransform * Carrier->GetCarryFrame();
	return Result;
}
bool AInteractiveAssemblyActor::GetNodeAssemblyTransform(const FGuid& Id, FTransform& Result) const
{
	const auto Effective = GetEffectiveState();
	if (!DreamState::WorldTransform(Effective, Id, Result))
		return false;
	Result = Result.GetRelativeTransform(Effective.RootTransform);
	return true;
}
bool AInteractiveAssemblyActor::ValidateState(const FDreamAssemblyState& State, FText& Failure) const
{
	if (!Definition || State.AssemblyId != AssemblyId || State.DefinitionPath != FSoftObjectPath(Definition) ||
		State.Nodes.Num() != Definition->Nodes.Num())
	{
		Failure = FText::FromString(TEXT("快照与当前对象 ID、定义资产或部件数量不匹配。"));
		return false;
	}
	if (!DreamState::Validate(State, Failure))
		return false;
	if (!State.RootTransform.GetScale3D().AllComponentsEqual(0.001))
	{
		Failure = FText::FromString(TEXT("装配体坐标系只能使用均匀缩放。"));
		return false;
	}
	for (const auto& N : State.Nodes)
	{
		if (!Definition->FindNodeDefinition(N.NodeId) || !N.LocalTransform.GetScale3D().AllComponentsEqual(0.001))
		{
			Failure = FText::FromString(TEXT("快照包含未知节点或非均匀的节点坐标系。"));
			return false;
		}
	}
	return true;
}
void AInteractiveAssemblyActor::SetCommittedState(const FDreamAssemblyState& State)
{
	AssemblyState = State;
	bHasPreview = false;
	RefreshPresentation();
}
void AInteractiveAssemblyActor::RefreshPresentation()
{
	if (!bInitialized)
		return;
	const auto Effective = GetEffectiveState();
	SetActorTransform(Effective.RootTransform, false, nullptr, ETeleportType::TeleportPhysics);
	for (const auto& N : Effective.Nodes)
	{
		const auto* D = Definition->FindNodeDefinition(N.NodeId);
		auto* Box = FindNodeComponent(N.NodeId);
		if (!D || !Box)
			continue;
		FTransform W;
		DreamState::WorldTransform(Effective, N.NodeId, W);
		// 表现代理以世界变换派生；逻辑父链由状态保存，不依赖组件 Attach 的遗留结果。
		const FVector Center = W.TransformPosition(D->CollisionCenter);
		Box->SetWorldTransform(
			FTransform(W.GetRotation(), Center, W.GetScale3D()), false, nullptr, ETeleportType::TeleportPhysics);
		const bool bVisible = DreamState::IsVisible(Effective, N.NodeId);
		const bool bBlocking = bVisible && D->bCollisionEnabled && !Effective.bIsCarried &&
							   (N.RuntimeState != EDreamNodeRuntimeState::Broken || D->bCollisionWhenBroken);
		Box->SetCollisionEnabled(
			bBlocking || (bVisible && D->bSelectable) ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		Box->SetCollisionResponseToAllChannels(bBlocking ? ECR_Block : ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_Visibility, D->bSelectable && bVisible ? ECR_Block : ECR_Ignore);
		if (const auto* MeshPtr = MeshComponents.Find(N.NodeId))
		{
			auto* Mesh = MeshPtr->Get();
			const bool bBroken =
				N.RuntimeState == EDreamNodeRuntimeState::Broken || N.RuntimeState == EDreamNodeRuntimeState::Released;
			Mesh->SetStaticMesh(
				(bBroken && !D->BrokenMesh.IsNull()) ? D->BrokenMesh.LoadSynchronous() : D->Mesh.LoadSynchronous());
			Mesh->SetWorldTransform(D->MeshTransform * W);
			Mesh->SetVisibility(bVisible);
		}
	}
}
void AInteractiveAssemblyActor::GetCapabilitiesForNode(
	const FGuid& Id, TArray<UDreamInteractionCapability*>& Result) const
{
	Result.Reset();
	if (!Definition)
		return;
	TSet<FName> Seen;
	FGuid Current = Id;
	bool bInherited = false;
	TSet<FGuid> Visited;
	while (Current.IsValid() && !Visited.Contains(Current))
	{
		Visited.Add(Current);
		const auto* D = Definition->FindNodeDefinition(Current);
		const auto* N = AssemblyState.FindNode(Current);
		if (!D || !N)
			break;
		for (const auto& Cap : D->Capabilities)
			if (Cap && !Seen.Contains(Cap->CapabilityId) && (!bInherited || Cap->bInheritToChildren))
			{
				Result.Add(Cap);
				Seen.Add(Cap->CapabilityId);
			}
		for (const auto& Disabled : D->DisabledInheritedCapabilities)
			Seen.Add(Disabled);
		Current = N->ParentNodeId;
		bInherited = true;
	}
	for (const auto& Cap : Definition->Capabilities)
		if (Cap && !Seen.Contains(Cap->CapabilityId) && (!Id.IsValid() || Cap->bInheritToChildren))
			Result.Add(Cap);
}
UDreamInteractionCapability* AInteractiveAssemblyActor::FindCapability(FName Id, const FGuid& NodeId) const
{
	TArray<UDreamInteractionCapability*> Caps;
	GetCapabilitiesForNode(NodeId, Caps);
	for (auto* Cap : Caps)
		if (Cap->CapabilityId == Id)
			return Cap;
	return nullptr;
}
void AInteractiveAssemblyActor::ShowPreview(const FDreamAssemblyState& State)
{
	PreviewState = State;
	bHasPreview = true;
}
void AInteractiveAssemblyActor::CancelPreview()
{
	bHasPreview = false;
}
void AInteractiveAssemblyActor::DrawPreview() const
{
	if (!bHasPreview || !Definition)
		return;
	for (const auto& N : PreviewState.Nodes)
	{
		const auto* D = Definition->FindNodeDefinition(N.NodeId);
		if (!D || !DreamState::IsVisible(PreviewState, N.NodeId))
			continue;
		FTransform W;
		DreamState::WorldTransform(PreviewState, N.NodeId, W);
		DrawDebugBox(GetWorld(), W.TransformPosition(D->CollisionCenter), D->CollisionExtent * W.GetScale3D(),
			W.GetRotation(), FColor::Cyan, false, 0, 0, 2);
	}
}
