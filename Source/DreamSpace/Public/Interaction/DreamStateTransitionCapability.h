#pragma once
#include "DreamInteractionCapability.h"
#include "DreamStateTransitionCapability.generated.h"
/** 有限状态转移。门、机关和破坏都通过逻辑状态与标签联动。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class DREAMSPACE_API UDreamStateTransitionCapability : public UDreamInteractionCapability
{
	GENERATED_BODY()
public:
	UDreamStateTransitionCapability();
	UPROPERTY(EditAnywhere, Category = "状态")
	TArray<EDreamNodeRuntimeState> AllowedSourceStates;
	UPROPERTY(EditAnywhere, Category = "状态")
	EDreamNodeRuntimeState TargetState = EDreamNodeRuntimeState::Broken;
	UPROPERTY(EditAnywhere, Category = "状态")
	bool bExistsAfterTransition = true;
	UPROPERTY(EditAnywhere, Category = "状态")
	bool bLockAfterTransition = false;
	UPROPERTY(EditAnywhere, Category = "状态")
	FGameplayTagContainer AddedStateTags;
	UPROPERTY(EditAnywhere, Category = "状态")
	FGameplayTagContainer RemovedStateTags;
	virtual bool BuildResult(
		const FDreamCapabilityContext& C, FDreamInteractionCommand& Command, FText& Failure) const override;
};
