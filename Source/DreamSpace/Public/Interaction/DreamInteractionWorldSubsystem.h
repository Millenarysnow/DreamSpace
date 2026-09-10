#pragma once
#include "Subsystems/WorldSubsystem.h"
#include "DreamInteractionTypes.h"
#include "DreamInteractionWorldSubsystem.generated.h"
class AInteractiveAssemblyActor;
class UDreamInteractionCapability;
class UDreamInteractionSaveGame;
class UDreamOccupantComponent;
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamTransactionCommitted, const FDreamInteractionTransaction&);

/** 世界唯一写入口。先验证全部状态与参与者，再统一写入，最后广播事件。 */
UCLASS()
class DREAMSPACE_API UDreamInteractionWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
	/** 注册以关卡持久 ID 为键。重复 ID 会拒绝注册，绝不覆盖另一个实例。 */
	bool RegisterAssembly(AInteractiveAssemblyActor* Assembly);
	void UnregisterAssembly(AInteractiveAssemblyActor* Assembly);
	AInteractiveAssemblyActor* FindAssembly(const FGuid& Id) const;
	void GetRegisteredAssemblies(TArray<AInteractiveAssemblyActor*>& Result) const;
	/** 显式登记事务参与者；普通关卡物体只有碰撞关系，不会被隐式拖走。 */
	void RegisterOccupant(UDreamOccupantComponent* Occupant);
	void UnregisterOccupant(UDreamOccupantComponent* Occupant);
	UDreamOccupantComponent* FindOccupant(const FGuid& Id) const;
	AInteractiveAssemblyActor* FindCarriedAssembly(const FGuid& CarrierId) const;
	/** 锁定粒度为装配体。SessionId 是预览和提交的所有权凭据。 */
	bool BeginInteraction(const FGuid& AssemblyId, const FGuid& NodeId, FName CapabilityId, EDreamInteractionMode Mode,
		const FGuid& RequesterId, FGuid& SessionId, FText& Failure);
	/** 从会话起始版本计算候选结果。此函数以及能力 BuildResult 都不能修改世界。 */
	bool PrepareCommand(const FGuid& SessionId, const FDreamInteractionIntent& Intent,
		FDreamInteractionCommand& Command, FText& Failure) const;
	/** 校验候选碰撞与占用者后绘制临时轮廓，真实碰撞、重力与玩家仍停留在已提交状态。 */
	bool PreviewCommand(const FDreamInteractionCommand& Command, FText& Failure);
	/** 重新计算能力结果、检查版本和所有参与者，随后原子写入并发出完成事件。 */
	bool ExecuteCommand(
		const FDreamInteractionCommand& Command, FDreamInteractionTransaction* Transaction, FText& Failure);
	/** 取消释放锁并移除轮廓；因为预览没有写入状态，不需要执行反向变换。 */
	void CancelInteraction(const FGuid& SessionId);
	/** Undo/Redo 恢复语义快照并递增版本，因此旧命令无法在撤销后复活。 */
	bool UndoLastTransaction(const FGuid& AssemblyId, FText& Failure);
	bool RedoLastTransaction(const FGuid& AssemblyId, FText& Failure);
	/** 命中组件只用来查找稳定 ID；权限、能力和节点状态继续由逻辑数据判定。 */
	bool ResolveComponentTarget(
		const UPrimitiveComponent* Hit, AInteractiveAssemblyActor*& Assembly, FGuid& NodeId) const;
	/** 先比优先级，再选更小的体积，最后按 ID 决胜；离开全部体积则使用世界向下重力。 */
	bool ResolveGravityAtLocation(
		const FVector& Location, FVector& Direction, FGuid* AssemblyId = nullptr, FGuid* NodeId = nullptr) const;
	/** 只捕获已提交状态；正在显示的预览不会混入存档。 */
	void CaptureToSaveGame(UDreamInteractionSaveGame& SaveGame) const;
	/** 先校验整份存档，再恢复所有对象和玩家；未知定义、缺失 ID 或损坏条目均拒绝整次恢复。 */
	bool RestoreFromSaveGame(const UDreamInteractionSaveGame& SaveGame, TArray<FGuid>& RestoredIds, FText& Failure);
	bool SaveToSlot(const FString& Slot, FText& Failure) const;
	bool LoadFromSlot(const FString& Slot, FText& Failure);
	FDreamTransactionCommitted OnTransactionCommitted;

private:
	struct FSession
	{
		FGuid Id, AssemblyId, NodeId, RequesterId;
		FName CapabilityId;
		EDreamInteractionMode Mode;
		int64 Version = 0;
	};
	TMap<FGuid, FSession> Sessions;
	TMap<FGuid, TWeakObjectPtr<AInteractiveAssemblyActor>> Assemblies;
	TMap<FGuid, TWeakObjectPtr<UDreamOccupantComponent>> Occupants;
	TArray<FDreamInteractionTransaction> History;
	TArray<FDreamInteractionTransaction> RedoHistory;
	/** 在广播结束前保持标记，防止事件回调重入一半完成的事务。 */
	bool bApplying = false;
	bool IsAssemblyLocked(const FGuid& Id) const;
	/** 防止调用者修改结果绕过尺寸/目标/模式限制，提交必须匹配同一能力重新生成的状态。 */
	bool VerifyCommand(const FDreamInteractionCommand& Command, FText& Failure) const;
	/** 把占用者相对旧参考系的姿态映射到新参考系，速度只旋转，胶囊尺寸保持不变。 */
	bool BuildParticipants(
		const FDreamInteractionCommand& Command, FDreamInteractionTransaction& Transaction, FText& Failure) const;
	/** 第一版为瞬时逻辑提交：检查目标姿态 OBB、关卡碰撞及胶囊净空，不模拟旋转途中的 Chaos 动力学。 */
	bool ValidateSpatial(const AInteractiveAssemblyActor& Assembly, const FDreamAssemblyState& Before,
		const FDreamAssemblyState& After, const TArray<FDreamOccupantState>& AfterOccupants, FText& Failure) const;
	bool RestoreHistory(bool bRedo, const FGuid& Id, FText& Failure);
};
