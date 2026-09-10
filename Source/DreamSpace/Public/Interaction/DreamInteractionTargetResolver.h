#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DreamInteractionTypes.h"
#include "DreamInteractionTargetResolver.generated.h"

class AInteractiveAssemblyActor;
class UDreamInteractionCapability;

/** 解析器返回的目标和当前模式下可用能力。 */
USTRUCT(BlueprintType)
struct DREAMSPACE_API FDreamInteractionTarget
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "目标")
	bool bValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "目标")
	TObjectPtr<AInteractiveAssemblyActor> Assembly;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "目标")
	FGuid NodeId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "目标")
	FVector WorldPoint = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "目标")
	FVector WorldNormal = FVector::UpVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "目标")
	TArray<UDreamInteractionCapability*> Capabilities;
};

/**
 * 将屏幕射线解析为稳定的装配体/节点目标。
 * 解析器只查询对象和能力，不执行变换、破坏或拾取。
 */
UCLASS(BlueprintType)
class DREAMSPACE_API UDreamInteractionTargetResolver : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "查询")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "查询")
	float TraceDistance = 100000.0f;

	bool ResolveScreenTarget(
		APlayerController& PlayerController, EDreamInteractionMode Mode, FDreamInteractionTarget& OutTarget) const;

	bool ResolveWorldRay(UWorld& World, const FVector& Start, const FVector& End, EDreamInteractionMode Mode,
		FDreamInteractionTarget& OutTarget, const AActor* IgnoredActor = nullptr) const;
};
