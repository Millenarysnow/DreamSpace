#include "DreamInteractionWorldSubsystem.h"

#include "InteractiveAssemblyActor.h"
#include "DreamInteractionCapability.h"
#include "DreamInteractionSaveGame.h"
#include "Components/PrimitiveComponent.h"

void UDreamInteractionWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Assemblies.Reset();
	TransactionHistory.Reset();
	UE_LOG(LogDreamInteraction, Log, TEXT("交互世界子系统已初始化。"));
}

void UDreamInteractionWorldSubsystem::Deinitialize()
{
	Assemblies.Reset();
	TransactionHistory.Reset();
	Super::Deinitialize();
}

void UDreamInteractionWorldSubsystem::RegisterAssembly(AInteractiveAssemblyActor* Assembly)
{
	if (!Assembly || !Assembly->GetAssemblyId().IsValid())
	{
		return;
	}
	if (const TWeakObjectPtr<AInteractiveAssemblyActor>* Existing = Assemblies.Find(Assembly->GetAssemblyId()))
	{
		if (Existing->IsValid() && Existing->Get() != Assembly)
		{
			UE_LOG(LogDreamInteraction, Warning, TEXT("检测到重复的装配体实例 ID %s，后注册实例将覆盖旧实例。"),
				*Assembly->GetAssemblyId().ToString());
		}
	}
	Assemblies.Add(Assembly->GetAssemblyId(), Assembly);
}

void UDreamInteractionWorldSubsystem::UnregisterAssembly(AInteractiveAssemblyActor* Assembly)
{
	if (Assembly)
	{
		Assemblies.Remove(Assembly->GetAssemblyId());
	}
}

AInteractiveAssemblyActor* UDreamInteractionWorldSubsystem::FindAssembly(const FGuid& AssemblyId) const
{
	if (const TWeakObjectPtr<AInteractiveAssemblyActor>* Found = Assemblies.Find(AssemblyId))
	{
		return Found->Get();
	}
	return nullptr;
}

void UDreamInteractionWorldSubsystem::GetRegisteredAssemblies(TArray<AInteractiveAssemblyActor*>& OutAssemblies) const
{
	OutAssemblies.Reset();
	for (const TPair<FGuid, TWeakObjectPtr<AInteractiveAssemblyActor>>& Pair : Assemblies)
	{
		if (AInteractiveAssemblyActor* Assembly = Pair.Value.Get())
		{
			OutAssemblies.Add(Assembly);
		}
	}
}

bool UDreamInteractionWorldSubsystem::ValidateCommand(const AInteractiveAssemblyActor& Assembly,
	const FDreamInteractionCommand& Command,
	FText& OutFailure) const
{
	if (!Command.IsValid() || Command.AssemblyId != Assembly.GetAssemblyId())
	{
		OutFailure = FText::FromString(TEXT("命令数据无效或目标装配体不匹配。"));
		return false;
	}
	if (Command.ExpectedStateVersion != 0 &&
		Command.ExpectedStateVersion != Assembly.AssemblyState.StateVersion)
	{
		OutFailure = FText::FromString(TEXT("装配体状态已变化，命令版本过期。"));
		return false;
	}
	if (Command.Type == EDreamCommandType::Transform && Command.TransformChanges.IsEmpty())
	{
		OutFailure = FText::FromString(TEXT("变换命令没有任何节点变更。"));
		return false;
	}
	if (Command.Type == EDreamCommandType::SetNodeState && Command.StateChanges.IsEmpty())
	{
		OutFailure = FText::FromString(TEXT("状态命令没有任何节点变更。"));
		return false;
	}

	TSet<FGuid> ChangedIds;
	for (const FDreamNodeTransformChange& Change : Command.TransformChanges)
	{
		if (!Change.NodeId.IsValid() || ChangedIds.Contains(Change.NodeId))
		{
			OutFailure = FText::FromString(TEXT("命令包含无效或重复的节点 ID。"));
			return false;
		}
		const FDreamNodeState* Node = Assembly.AssemblyState.FindNode(Change.NodeId);
		if (!Node || !Node->IsOperational())
		{
			OutFailure = FText::FromString(TEXT("命令包含不可操作节点。"));
			return false;
		}
		if (!Change.NewLocalTransform.IsValid() || Change.NewLocalTransform.GetScale3D().GetMin() <= KINDA_SMALL_NUMBER)
		{
			OutFailure = FText::FromString(TEXT("命令包含无效的目标变换。"));
			return false;
		}
		ChangedIds.Add(Change.NodeId);
	}
	for (const FDreamNodeStateChange& Change : Command.StateChanges)
	{
		if (!Change.NodeId.IsValid() || ChangedIds.Contains(Change.NodeId))
		{
			OutFailure = FText::FromString(TEXT("状态命令包含无效或重复的节点 ID。"));
			return false;
		}
		const FDreamNodeState* Node = Assembly.AssemblyState.FindNode(Change.NodeId);
		if (!Node)
		{
			OutFailure = FText::FromString(TEXT("状态命令包含不存在的节点。"));
			return false;
		}
		ChangedIds.Add(Change.NodeId);
	}
	return true;
}

bool UDreamInteractionWorldSubsystem::PreviewCommand(const FDreamInteractionCommand& Command, FText& OutFailure)
{
	AInteractiveAssemblyActor* Assembly = FindAssembly(Command.AssemblyId);
	if (!Assembly || !ValidateCommand(*Assembly, Command, OutFailure))
	{
		if (!Assembly)
		{
			OutFailure = FText::FromString(TEXT("找不到命令目标装配体。"));
		}
		return false;
	}
	return Assembly->PreviewCommand(Command, OutFailure);
}

void UDreamInteractionWorldSubsystem::CancelPreview(const FGuid& AssemblyId)
{
	if (AInteractiveAssemblyActor* Assembly = FindAssembly(AssemblyId))
	{
		Assembly->CancelPreview();
	}
}

bool UDreamInteractionWorldSubsystem::ExecuteCommand(const FDreamInteractionCommand& Command,
	FDreamInteractionTransaction* OutTransaction,
	FText& OutFailure)
{
	AInteractiveAssemblyActor* Assembly = FindAssembly(Command.AssemblyId);
	if (!Assembly)
	{
		OutFailure = FText::FromString(TEXT("找不到命令目标装配体。"));
		return false;
	}
	if (!ValidateCommand(*Assembly, Command, OutFailure))
	{
		return false;
	}

	const FDreamAssemblyState BeforeState = Assembly->AssemblyState;
	FDreamAssemblyState AfterState = BeforeState;
	for (const FDreamNodeTransformChange& Change : Command.TransformChanges)
	{
		if (FDreamNodeState* Node = AfterState.FindNode(Change.NodeId))
		{
			Node->LocalTransform = Change.NewLocalTransform;
		}
	}
	for (const FDreamNodeStateChange& Change : Command.StateChanges)
	{
		if (FDreamNodeState* Node = AfterState.FindNode(Change.NodeId))
		{
			Node->RuntimeState = Change.NewRuntimeState;
			Node->bExists = Change.bExists;
			Node->bLocked = Change.bLocked;
		}
	}
	if (Command.bChangesGravity)
	{
		if (Command.NewLocalGravityDirection.IsNearlyZero())
		{
			OutFailure = FText::FromString(TEXT("局部重力方向不能为零向量。"));
			return false;
		}
		AfterState.LocalGravityDirection = Command.NewLocalGravityDirection.GetSafeNormal();
	}
	if (Command.bSetsCarriedState)
	{
		AfterState.bIsCarried = Command.bIsCarried;
	}
	AfterState.StateTags.AppendTags(Command.AddedStateTags);
	AfterState.StateTags.RemoveTags(Command.RemovedStateTags);
	AfterState.StateVersion = BeforeState.StateVersion + 1;

	FDreamInteractionTransaction Transaction;
	Transaction.TransactionId = FGuid::NewGuid();
	Transaction.Command = Command;
	Transaction.BeforeState = BeforeState;
	Transaction.AfterState = AfterState;

	if (!Assembly->ApplyState(AfterState, OutFailure))
	{
		// ApplyState 失败时不应该留下部分变化；再次尝试恢复是事务原子性的最后一道保险。
		FText IgnoredFailure;
		Assembly->ApplyState(BeforeState, IgnoredFailure);
		return false;
	}

	Assembly->CancelPreview();
	TransactionHistory.Add(Transaction);
	if (OutTransaction)
	{
		*OutTransaction = Transaction;
	}
	OnTransactionCommitted.Broadcast(Transaction);
	UE_LOG(LogDreamInteraction, Verbose, TEXT("装配体 %s 提交交互事务 %s，版本 %lld -> %lld。"),
		*Assembly->GetAssemblyId().ToString(), *Transaction.TransactionId.ToString(),
		BeforeState.StateVersion, AfterState.StateVersion);
	return true;
}

bool UDreamInteractionWorldSubsystem::ExecuteCommand(const UDreamInteractionCapability& Capability,
	const FDreamInteractionCommand& Command,
	FDreamInteractionTransaction* OutTransaction,
	FText& OutFailure)
{
	AInteractiveAssemblyActor* Assembly = FindAssembly(Command.AssemblyId);
	if (!Assembly)
	{
		OutFailure = FText::FromString(TEXT("找不到命令目标装配体。"));
		return false;
	}
	if (!Capability.Validate(*Assembly, Command, OutFailure))
	{
		return false;
	}
	return ExecuteCommand(Command, OutTransaction, OutFailure);
}

bool UDreamInteractionWorldSubsystem::UndoLastTransaction(const FGuid& AssemblyId, FText& OutFailure)
{
	for (int32 Index = TransactionHistory.Num() - 1; Index >= 0; --Index)
	{
		if (TransactionHistory[Index].Command.AssemblyId != AssemblyId)
		{
			continue;
		}
		AInteractiveAssemblyActor* Assembly = FindAssembly(AssemblyId);
		if (!Assembly)
		{
			OutFailure = FText::FromString(TEXT("找不到需要回滚的装配体。"));
			return false;
		}
		if (Assembly->AssemblyState.StateVersion != TransactionHistory[Index].AfterState.StateVersion)
		{
			OutFailure = FText::FromString(TEXT("装配体已经发生新的变化，不能直接回滚历史事务。"));
			return false;
		}

		FDreamAssemblyState RestoreState = TransactionHistory[Index].BeforeState;
		// 版本号单调递增，即使是 Undo 也能让旧命令失效。
		RestoreState.StateVersion = Assembly->AssemblyState.StateVersion + 1;
		const bool bRestored = Assembly->ApplyState(RestoreState, OutFailure);
		if (bRestored)
		{
			TransactionHistory.RemoveAt(Index);
		}
		return bRestored;
	}

	OutFailure = FText::FromString(TEXT("没有可回滚的事务。"));
	return false;
}

void UDreamInteractionWorldSubsystem::CaptureToSaveGame(UDreamInteractionSaveGame& SaveGame) const
{
	SaveGame.AssemblyStates.Reset();
	for (const TPair<FGuid, TWeakObjectPtr<AInteractiveAssemblyActor>>& Pair : Assemblies)
	{
		if (const AInteractiveAssemblyActor* Assembly = Pair.Value.Get())
		{
			SaveGame.CaptureAssemblyState(Assembly->AssemblyState);
		}
	}
}

bool UDreamInteractionWorldSubsystem::RestoreFromSaveGame(const UDreamInteractionSaveGame& SaveGame,
	TArray<FGuid>& OutRestoredIds)
{
	OutRestoredIds.Reset();
	bool bAllRestored = true;
	for (const FDreamAssemblyState& SavedState : SaveGame.AssemblyStates)
	{
		AInteractiveAssemblyActor* Assembly = FindAssembly(SavedState.AssemblyId);
		if (!Assembly)
		{
			bAllRestored = false;
			continue;
		}
		FText Failure;
		if (Assembly->ApplyState(SavedState, Failure))
		{
			OutRestoredIds.Add(SavedState.AssemblyId);
		}
		else
		{
			bAllRestored = false;
			UE_LOG(LogDreamInteraction, Warning, TEXT("恢复装配体 %s 失败：%s"),
				*SavedState.AssemblyId.ToString(), *Failure.ToString());
		}
	}
	return bAllRestored;
}

bool UDreamInteractionWorldSubsystem::ResolveComponentTarget(const UPrimitiveComponent* HitComponent,
	AInteractiveAssemblyActor*& OutAssembly,
	FGuid& OutNodeId) const
{
	OutAssembly = nullptr;
	OutNodeId.Invalidate();
	if (!HitComponent)
	{
		return false;
	}
	for (const TPair<FGuid, TWeakObjectPtr<AInteractiveAssemblyActor>>& Pair : Assemblies)
	{
		AInteractiveAssemblyActor* Assembly = Pair.Value.Get();
		if (!Assembly)
		{
			continue;
		}
		for (const FDreamNodeState& Node : Assembly->AssemblyState.Nodes)
		{
			if (Assembly->FindNodeComponent(Node.NodeId) == HitComponent)
			{
				OutAssembly = Assembly;
				OutNodeId = Node.NodeId;
				return true;
			}
		}
	}
	return false;
}

bool UDreamInteractionWorldSubsystem::ResolveGravityAtLocation(const FVector& WorldLocation,
	FVector& OutGravityDirection,
	FGuid* OutSourceAssemblyId) const
{
	OutGravityDirection = FVector::DownVector;
	if (OutSourceAssemblyId)
	{
		OutSourceAssemblyId->Invalidate();
	}

	const AInteractiveAssemblyActor* BestAssembly = nullptr;
	int32 BestPriority = TNumericLimits<int32>::Lowest();
	for (const TPair<FGuid, TWeakObjectPtr<AInteractiveAssemblyActor>>& Pair : Assemblies)
	{
		const AInteractiveAssemblyActor* Assembly = Pair.Value.Get();
		if (!Assembly || !Assembly->AssemblyState.bProvidesGravity ||
			Assembly->AssemblyState.GravityPriority < BestPriority)
		{
			continue;
		}

		// 通过表现组件的包围盒做轻量的重力体积判断。
		const FBox Bounds = Assembly->GetComponentsBoundingBox(true);
		if (Bounds.IsValid && Bounds.IsInsideOrOn(WorldLocation))
		{
			BestAssembly = Assembly;
			BestPriority = Assembly->AssemblyState.GravityPriority;
		}
	}
	if (BestAssembly)
	{
		OutGravityDirection = BestAssembly->GetWorldGravityDirection();
		if (OutSourceAssemblyId)
		{
			*OutSourceAssemblyId = BestAssembly->GetAssemblyId();
		}
		return true;
	}
	return false;
}
