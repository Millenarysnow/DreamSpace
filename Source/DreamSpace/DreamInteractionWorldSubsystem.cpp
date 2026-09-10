#include "DreamInteractionWorldSubsystem.h"

#include "InteractiveAssemblyActor.h"
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
	if (Command.bChangesGravity)
	{
		if (Command.NewLocalGravityDirection.IsNearlyZero())
		{
			OutFailure = FText::FromString(TEXT("局部重力方向不能为零向量。"));
			return false;
		}
		AfterState.LocalGravityDirection = Command.NewLocalGravityDirection.GetSafeNormal();
	}
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

