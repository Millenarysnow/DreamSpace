#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DreamInteractionTypes.h"
#include "InteractiveAssemblyActor.generated.h"

class UInteractiveAssemblyDefinition;
class UDreamInteractionCapability;
class USceneComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FDreamAssemblyStateChanged, const FDreamAssemblyState& /*NewState*/);

/**
 * 装配体运行时实例。
 * Actor 只负责承载表现组件和状态入口，所有实际命令仍由世界子系统统一提交。
 */
UCLASS(BlueprintType)
class DREAMSPACE_API AInteractiveAssemblyActor : public AActor
{
	GENERATED_BODY()

public:
	AInteractiveAssemblyActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 可复用的对象定义资产。多个 Actor 可以引用同一个定义。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "装配体")
	TObjectPtr<UInteractiveAssemblyDefinition> Definition;

	/** 关卡实例 ID。为空时 BeginPlay 自动生成并保持到本次实例生命周期结束。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "装配体")
	FGuid AssemblyId;

	/** 运行时状态快照，作为唯一事实来源。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "状态")
	FDreamAssemblyState AssemblyState;

	/** 根据对象定义创建节点组件并初始化逻辑状态。 */
	UFUNCTION(BlueprintCallable, Category = "装配体")
	bool InitializeFromDefinition();

	UFUNCTION(BlueprintPure, Category = "装配体")
	FGuid GetAssemblyId() const { return AssemblyId; }

	UFUNCTION(BlueprintPure, Category = "装配体")
	FDreamAssemblyState GetAssemblyState() const { return AssemblyState; }

	UFUNCTION(BlueprintPure, Category = "装配体")
	bool IsOperational() const;

	UFUNCTION(BlueprintPure, Category = "装配体")
	bool IsNodeOperational(const FGuid& NodeId) const;

	/** 返回节点场景组件，目标解析器可以从命中组件找到稳定 ID。 */
	UFUNCTION(BlueprintPure, Category = "装配体")
	USceneComponent* FindNodeComponent(const FGuid& NodeId) const;

	/** 构造旋转/缩放命令，不会修改持久化状态。 */
	bool BuildTransformCommand(const TArray<FGuid>& TargetNodeIds,
		const FVector& Axis,
		float RotationDegrees,
		float ScaleDelta,
		EDreamReferenceFrame ReferenceFrame,
		FDreamInteractionCommand& OutCommand,
		FText& OutFailure) const;

	/** 仅更新表现组件的临时预览。 */
	bool PreviewCommand(const FDreamInteractionCommand& Command, FText& OutFailure);

	/** 丢弃预览，恢复到当前逻辑状态。 */
	void CancelPreview();

	/** 应用一个已经通过事务验证的状态快照。 */
	bool ApplyState(const FDreamAssemblyState& NewState, FText& OutFailure);

	/** 计算指定节点相对装配体根的变换。 */
	bool GetNodeAssemblyTransform(const FGuid& NodeId, FTransform& OutTransform) const;

	/** 根据装配体局部重力和根变换计算世界重力方向。 */
	UFUNCTION(BlueprintPure, Category = "重力")
	FVector GetWorldGravityDirection() const;

	/** 获取装配体和节点上的能力配置，供目标解析器使用。 */
	void GetCapabilitiesForNode(const FGuid& NodeId, TArray<UDreamInteractionCapability*>& OutCapabilities) const;

	FDreamAssemblyStateChanged OnStateChanged;

private:
	TMap<FGuid, TObjectPtr<USceneComponent>> NodeComponents;
	bool bInitialized = false;
	bool bHasPreview = false;

	void DestroyNodeComponents();
	void ApplyStateToComponents(const FDreamAssemblyState& StateToApply);
	bool GetNodeAssemblyTransformRecursive(const FGuid& NodeId,
		TMap<FGuid, FTransform>& Cache,
		TSet<FGuid>& Visiting,
		FTransform& OutTransform) const;
};
