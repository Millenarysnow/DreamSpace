#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DreamInteractionTypes.h"
#include "InteractiveAssemblyDefinition.generated.h"

/**
 * 可复用的装配体对象定义。
 * 该资产只描述默认结构和能力配置；关卡中的 Actor 负责保存独立的运行时状态。
 */
UCLASS(BlueprintType)
class DREAMSPACE_API UInteractiveAssemblyDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UInteractiveAssemblyDefinition();

	/** 对象定义的稳定标签，用于目标过滤和任务条件。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "装配体")
	FGameplayTagContainer GameplayTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "装配体")
	FText DisplayName;

	/** 装配体级能力。节点能力在对应节点定义中配置。 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "能力")
	TArray<TObjectPtr<UDreamInteractionCapability>> Capabilities;

	/** 根节点之外的全部节点定义。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "节点")
	TArray<FDreamNodeDefinition> Nodes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "重力")
	FVector DefaultLocalGravityDirection = FVector::DownVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "重力")
	bool bProvidesGravity = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "重力")
	int32 GravityPriority = 0;

	/** 默认的占用者策略，新能力可以在自己的配置中覆盖它。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "物理")
	EDreamOccupantPolicy DefaultOccupantPolicy = EDreamOccupantPolicy::FollowAssembly;

	/** 校验稳定 ID、父节点关系和重复引用。编辑器工具也可以调用此函数。 */
	bool ValidateDefinition(TArray<FString>& OutErrors) const;

	const FDreamNodeDefinition* FindNodeDefinition(const FGuid& NodeId) const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
