// 开发期控制台入口只转发给事务系统，不提供直接改 Actor 变换的后门。
#if !UE_BUILD_SHIPPING
#include "DreamInteractionWorldSubsystem.h"
#include "InteractiveAssemblyActor.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
namespace
{
FAutoConsoleCommandWithWorldAndArgs Dump(TEXT("Dream.Dump"), TEXT("列出装配体 ID、节点数和已提交状态版本。"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			auto* System = World ? World->GetSubsystem<UDreamInteractionWorldSubsystem>() : nullptr;
			if (!System)
				return;
			TArray<AInteractiveAssemblyActor*> Assemblies;
			System->GetRegisteredAssemblies(Assemblies);
			for (const auto* A : Assemblies)
				UE_LOG(LogDreamInteraction, Display, TEXT("%s | %s | nodes=%d | version=%lld | carried=%d"),
					*A->GetName(), *A->AssemblyId.ToString(), A->GetAssemblyState().Nodes.Num(),
					A->GetAssemblyState().StateVersion, A->GetAssemblyState().bIsCarried);
		}));
FAutoConsoleCommandWithWorldAndArgs Undo(TEXT("Dream.Undo"),
	TEXT("Dream.Undo <AssemblyGuid>：通过事务撤销指定装配体。"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			auto* System = World ? World->GetSubsystem<UDreamInteractionWorldSubsystem>() : nullptr;
			FGuid Id;
			FText Failure;
			if (!System || Args.Num() != 1 || !FGuid::Parse(Args[0], Id))
			{
				UE_LOG(LogDreamInteraction, Display, TEXT("用法：Dream.Undo <AssemblyGuid>"));
				return;
			}
			if (!System->UndoLastTransaction(Id, Failure))
				UE_LOG(LogDreamInteraction, Display, TEXT("%s"), *Failure.ToString());
		}));
} // namespace
#endif
