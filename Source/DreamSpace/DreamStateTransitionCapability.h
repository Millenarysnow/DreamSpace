#pragma once

#include "CoreMinimal.h"
#include "DreamInteractionCapability.h"
#include "DreamStateTransitionCapability.generated.h"

/** 将一个或多个节点从当前状态切换到配置的目标状态。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class DREAMSPACE_API UDreamStateTransitionCapability : public UDreamInteractionCapability
{
	GENERATED_BODY()

public:
	UDreamStateTransitionCapability();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "状态")
	TArray<EDreamNodeRuntimeState> AllowedSourceStates;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "状态")
	EDreamNodeRuntimeState TargetState = EDreamNodeRuntimeState::Broken;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "状态")
	bool bExistsAfterTransition = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "状态")
	bool bLockAfterTransition = false;

	/** 为空时使用意图目标；装配体级能力再为空则作用于所有根节点。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "状态")
	TArray<FGuid> ConfiguredNodeIds;

	virtual bool CanStart(const AInteractiveAssemblyActor& Target,
		const FDreamInteractionIntent& Intent,
		EDreamInteractionMode Mode,
		FText& OutFailure) const override;

	virtual bool BuildCommand(AInteractiveAssemblyActor& Target,
		const FDreamInteractionIntent& Intent,
		FDreamInteractionCommand& OutCommand,
		FText& OutFailure) const override;

	virtual bool Validate(const AInteractiveAssemblyActor& Target,
		const FDreamInteractionCommand& Command,
		FText& OutFailure) const override;
};

