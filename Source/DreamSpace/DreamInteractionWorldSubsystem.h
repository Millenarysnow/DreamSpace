#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamInteractionTypes.h"
#include "DreamInteractionWorldSubsystem.generated.h"

class AInteractiveAssemblyActor;
class UDreamInteractionCapability;
class UDreamInteractionSaveGame;

DECLARE_MULTICAST_DELEGATE_OneParam(FDreamTransactionCommitted, const FDreamInteractionTransaction& /*Transaction*/);

/**
 * 世界级交互入口：维护装配体注册表，并把所有命令包装为原子事务。
 * 目标查询和 UI 可以读取注册表，但不能绕过这里直接改变装配体状态。
 */
UCLASS()
class DREAMSPACE_API UDreamInteractionWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void RegisterAssembly(AInteractiveAssemblyActor* Assembly);
	void UnregisterAssembly(AInteractiveAssemblyActor* Assembly);

	AInteractiveAssemblyActor* FindAssembly(const FGuid& AssemblyId) const;

	/** 将预览命令应用到表现层，不推进逻辑版本。 */
	bool PreviewCommand(const FDreamInteractionCommand& Command, FText& OutFailure);

	/** 取消指定装配体的预览。 */
	void CancelPreview(const FGuid& AssemblyId);

	/** 验证并原子提交命令，失败时自动恢复事务前快照。 */
	bool ExecuteCommand(const FDreamInteractionCommand& Command,
		FDreamInteractionTransaction* OutTransaction,
		FText& OutFailure);

	/** 带能力规则校验的提交入口；所有高层交互都应优先使用它。 */
	bool ExecuteCommand(const UDreamInteractionCapability& Capability,
		const FDreamInteractionCommand& Command,
		FDreamInteractionTransaction* OutTransaction,
		FText& OutFailure);

	/** 回滚最近一次已经提交的事务，供开发调试和编辑器 Undo 使用。 */
	bool UndoLastTransaction(const FGuid& AssemblyId, FText& OutFailure);

	/** 将当前所有装配体逻辑状态写入存档对象。 */
	void CaptureToSaveGame(UDreamInteractionSaveGame& SaveGame) const;

	/** 从存档恢复所有已注册装配体；单个对象失败不会污染其他对象。 */
	bool RestoreFromSaveGame(const UDreamInteractionSaveGame& SaveGame, TArray<FGuid>& OutRestoredIds);

	/** 返回当前注册的装配体，目标解析器可在此基础上做射线筛选。 */
	void GetRegisteredAssemblies(TArray<AInteractiveAssemblyActor*>& OutAssemblies) const;

	/** 通过命中的场景组件反查装配体和稳定节点 ID。 */
	bool ResolveComponentTarget(const UPrimitiveComponent* HitComponent,
		AInteractiveAssemblyActor*& OutAssembly,
		FGuid& OutNodeId) const;

	/** 根据位置解析当前有效的局部重力源，优先级高者胜出。 */
	bool ResolveGravityAtLocation(const FVector& WorldLocation,
		FVector& OutGravityDirection,
		FGuid* OutSourceAssemblyId = nullptr) const;

	FDreamTransactionCommitted OnTransactionCommitted;

private:
	UPROPERTY()
	TMap<FGuid, TWeakObjectPtr<AInteractiveAssemblyActor>> Assemblies;

	UPROPERTY()
	TArray<FDreamInteractionTransaction> TransactionHistory;

	bool ValidateCommand(const AInteractiveAssemblyActor& Assembly,
		const FDreamInteractionCommand& Command,
		FText& OutFailure) const;
};
