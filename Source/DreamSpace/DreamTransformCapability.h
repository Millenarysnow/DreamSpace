#pragma once

#include "CoreMinimal.h"
#include "DreamInteractionCapability.h"
#include "DreamTransformCapability.generated.h"

/** 通用旋转/缩放能力，既可以挂在装配体上，也可以挂在具体节点上。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class DREAMSPACE_API UDreamTransformCapability : public UDreamInteractionCapability
{
	GENERATED_BODY()

public:
	UDreamTransformCapability();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "变换")
	bool bAllowRotation = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "变换")
	bool bAllowScaling = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "变换")
	FVector DefaultRotationAxis = FVector::UpVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "变换", meta = (ClampMin = "0.0"))
	float RotationStepDegrees = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "变换")
	bool bSnapRotation = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "变换")
	float MinimumScale = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "变换")
	float MaximumScale = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "变换")
	EDreamReferenceFrame ReferenceFrame = EDreamReferenceFrame::Assembly;

	/** 可选的自定义 Pivot 节点；为空时使用目标集合的几何中心。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "变换")
	FGuid PivotNodeId;

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

