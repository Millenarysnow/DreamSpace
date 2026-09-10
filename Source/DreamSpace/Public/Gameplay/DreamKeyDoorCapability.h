#pragma once
#include "DreamStateTransitionCapability.h"
#include "DreamKeyDoorCapability.generated.h"
/** 门只依赖钥匙逻辑标签与携带状态，不读取钥匙 Mesh 的尺寸或名字。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class DREAMSPACE_API UDreamKeyDoorCapability : public UDreamStateTransitionCapability
{
	GENERATED_BODY()
public:
	UDreamKeyDoorCapability();
	UPROPERTY(EditAnywhere, Category = "门")
	FGameplayTag RequiredKeyTag;
	UPROPERTY(EditAnywhere, Category = "门")
	float UseDistance = 300;
	virtual bool CanStart(const FDreamCapabilityContext& C, FText& Failure) const override;
};
