#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DreamInteractionTypes.h"
#include "InteractiveAssemblyDefinition.generated.h"

/** 数据驱动的可复用对象定义；具体行为由内联 C++ 能力配置组合。 */
UCLASS(BlueprintType)
class DREAMSPACE_API UInteractiveAssemblyDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "装配体")
	FText DisplayName;
	UPROPERTY(EditAnywhere, Category = "装配体")
	FGameplayTagContainer GameplayTags;
	UPROPERTY(EditAnywhere, Category = "装配体")
	FGameplayTagContainer DefaultStateTags;
	UPROPERTY(EditAnywhere, Instanced, Category = "能力")
	TArray<TObjectPtr<UDreamInteractionCapability>> Capabilities;
	UPROPERTY(EditAnywhere, Category = "节点")
	TArray<FDreamNodeDefinition> Nodes;
	UPROPERTY(EditAnywhere, Category = "重力")
	FDreamGravitySettings Gravity;
	UPROPERTY(EditAnywhere, Category = "物理")
	EDreamOccupantPolicy DefaultOccupantPolicy = EDreamOccupantPolicy::FollowAssembly;
	/** 仅为尚未分配 ID 的新节点生成 ID；重复 ID 会报告错误，不会偷偷破坏旧引用。 */
	UFUNCTION(CallInEditor, Category = "内容工具")
	void AssignMissingNodeIds();
	UFUNCTION(CallInEditor, Category = "内容工具")
	void CheckDefinition();
	bool ValidateDefinition(TArray<FString>& Errors) const;
	const FDreamNodeDefinition* FindNodeDefinition(const FGuid& Id) const;
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
