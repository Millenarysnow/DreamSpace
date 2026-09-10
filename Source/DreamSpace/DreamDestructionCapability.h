#pragma once

#include "CoreMinimal.h"
#include "DreamStateTransitionCapability.h"
#include "DreamDestructionCapability.generated.h"

/** 破坏能力是状态转移能力的语义化配置，避免为每个建筑复制专用 Actor。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class DREAMSPACE_API UDreamDestructionCapability : public UDreamStateTransitionCapability
{
	GENERATED_BODY()

public:
	UDreamDestructionCapability();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "破坏")
	bool bReleaseNodeOnBreak = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "破坏")
	bool bRemoveNodeOnBreak = false;

	virtual bool BuildCommand(AInteractiveAssemblyActor& Target,
		const FDreamInteractionIntent& Intent,
		FDreamInteractionCommand& OutCommand,
		FText& OutFailure) const override;

	virtual bool Validate(const AInteractiveAssemblyActor& Target,
		const FDreamInteractionCommand& Command,
		FText& OutFailure) const override;
};
