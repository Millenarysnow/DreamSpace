#pragma once

#include "CoreMinimal.h"
#include "DreamInteractionCapability.h"
#include "DreamPickupCapability.generated.h"

/** 钥匙等物体的拾取/携带能力。携带状态写入装配体逻辑状态，表现层可自行绑定插槽。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class DREAMSPACE_API UDreamPickupCapability : public UDreamInteractionCapability
{
	GENERATED_BODY()

public:
	UDreamPickupCapability();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "拾取", meta = (ClampMin = "0.0"))
	float MinimumScaleToPickup = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "拾取")
	FGameplayTag CarriedStateTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "拾取")
	bool bDropInsteadOfPickup = false;

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

