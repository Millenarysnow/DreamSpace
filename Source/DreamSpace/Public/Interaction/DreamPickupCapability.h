#pragma once
#include "DreamInteractionCapability.h"
#include "DreamPickupCapability.generated.h"
/** 缩小到阈值且在玩家范围内后，使用同一装配体进入携带参考系。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class DREAMSPACE_API UDreamPickupCapability : public UDreamInteractionCapability
{
	GENERATED_BODY()
public:
	UDreamPickupCapability();
	UPROPERTY(EditAnywhere, Category = "拾取")
	float MaximumScaleToPickup = 0.35f;
	UPROPERTY(EditAnywhere, Category = "拾取")
	float MaximumDistance = 300;
	UPROPERTY(EditAnywhere, Category = "拾取")
	FTransform CarryTransform = FTransform(FVector(100, 0, 20));
	UPROPERTY(EditAnywhere, Category = "拾取")
	FGameplayTag CarriedStateTag;
	UPROPERTY(EditAnywhere, Category = "拾取")
	bool bDropInsteadOfPickup = false;
	virtual bool CanStart(const FDreamCapabilityContext& C, FText& Failure) const override;
	virtual bool BuildResult(
		const FDreamCapabilityContext& C, FDreamInteractionCommand& Command, FText& Failure) const override;
	virtual void ValidateConfiguration(const UInteractiveAssemblyDefinition& D, TArray<FString>& Errors) const override;
};
