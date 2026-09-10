#pragma once
#include "DreamInteractionCapability.h"
#include "DreamSequenceCapability.generated.h"
/** 有序组合：子能力只改变临时状态，任一步失败都丢弃全部结果。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class DREAMSPACE_API UDreamSequenceCapability : public UDreamInteractionCapability
{
	GENERATED_BODY()
public:
	UDreamSequenceCapability();
	UPROPERTY(EditAnywhere, Instanced, Category = "序列")
	TArray<TObjectPtr<UDreamInteractionCapability>> Steps;
	virtual bool BuildResult(
		const FDreamCapabilityContext& C, FDreamInteractionCommand& Command, FText& Failure) const override;
	virtual void ValidateConfiguration(const UInteractiveAssemblyDefinition& D, TArray<FString>& Errors) const override;
};
