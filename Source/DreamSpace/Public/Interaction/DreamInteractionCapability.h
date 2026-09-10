#pragma once
#include "CoreMinimal.h"
#include "DreamInteractionTypes.h"
#include "DreamInteractionCapability.generated.h"
class AInteractiveAssemblyActor;
class UInteractiveAssemblyDefinition;

/** 能力纯计算所需的上下文。组合能力可传入上一条命令的临时状态，无需改动 Actor。 */
struct FDreamCapabilityContext
{
	const AInteractiveAssemblyActor& Target;
	const FDreamAssemblyState& State;
	const FDreamInteractionIntent& Intent;
	EDreamInteractionMode Mode;
	FGuid RequesterId;
};

UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced)
class DREAMSPACE_API UDreamInteractionCapability : public UObject
{
	GENERATED_BODY()
public:
	/** 同一对象层级中的稳定行为标识；子级同名配置覆盖父级。 */
	UPROPERTY(EditAnywhere, Category = "能力")
	FName CapabilityId;
	UPROPERTY(EditAnywhere, Category = "能力")
	FText DisplayName;
	UPROPERTY(EditAnywhere, Category = "能力")
	TArray<EDreamInteractionMode> AllowedModes;
	UPROPERTY(EditAnywhere, Category = "能力")
	bool bEnabledByDefault = true;
	UPROPERTY(EditAnywhere, Category = "能力")
	bool bInheritToChildren = true;
	UPROPERTY(EditAnywhere, Category = "规则")
	FGameplayTagContainer RequiredStateTags;
	UPROPERTY(EditAnywhere, Category = "规则")
	FGameplayTagContainer BlockedStateTags;
	UPROPERTY(EditAnywhere, Category = "规则")
	FGameplayTagContainer RequiredNodeTags;
	UPROPERTY(EditAnywhere, Category = "物理")
	bool bRejectCollisions = true;
	UPROPERTY(EditAnywhere, Category = "物理")
	bool bOverrideOccupantPolicy = false;
	UPROPERTY(EditAnywhere, Category = "物理")
	EDreamOccupantPolicy OccupantPolicy = EDreamOccupantPolicy::FollowAssembly;
	bool SupportsMode(EDreamInteractionMode Mode) const;
	virtual bool CanStart(const FDreamCapabilityContext& Context, FText& Failure) const;
	/** BuildResult 只能写临时命令结果，不操作组件，也不广播表现事件。 */
	virtual bool BuildResult(
		const FDreamCapabilityContext& Context, FDreamInteractionCommand& Command, FText& Failure) const;
	virtual void ValidateConfiguration(const UInteractiveAssemblyDefinition& Definition, TArray<FString>& Errors) const;
	/** 配置目标集合优先；否则采用输入选择；最后为空表示装配体根。 */
	UPROPERTY(EditAnywhere, Category = "目标")
	TArray<FGuid> ConfiguredNodeIds;
	UPROPERTY(EditAnywhere, Category = "目标")
	bool bTargetAssemblyRoot = false;
	bool ResolveTargets(const FDreamCapabilityContext& Context, TArray<FGuid>& Targets, FText& Failure) const;
};
