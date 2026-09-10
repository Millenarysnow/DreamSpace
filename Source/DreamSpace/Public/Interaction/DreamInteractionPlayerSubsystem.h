#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "DreamInteractionTypes.h"
#include "DreamInteractionPlayerSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FDreamInteractionModeChanged, EDreamInteractionMode /*NewMode*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FDreamInteractionSelectionChanged, const FGuid& /*AssemblyId*/, const FGuid& /*NodeId*/);

/**
 * 当前本地玩家的交互模式和选择状态。
 * 该状态不持有建筑副本，切换模式时只改变相机/输入层的读取方式。
 */
UCLASS()
class DREAMSPACE_API UDreamInteractionPlayerSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "交互")
	EDreamInteractionMode GetInteractionMode() const { return InteractionMode; }

	UFUNCTION(BlueprintCallable, Category = "交互")
	void SetInteractionMode(EDreamInteractionMode NewMode);

	UFUNCTION(BlueprintCallable, Category = "交互")
	void ToggleInteractionMode();

	UFUNCTION(BlueprintPure, Category = "交互")
	bool HasSelection() const { return SelectedAssemblyId.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "交互")
	FGuid GetSelectedAssemblyId() const { return SelectedAssemblyId; }

	UFUNCTION(BlueprintPure, Category = "交互")
	FGuid GetSelectedNodeId() const { return SelectedNodeId; }

	UFUNCTION(BlueprintCallable, Category = "交互")
	void SetSelection(const FGuid& AssemblyId, const FGuid& NodeId);

	UFUNCTION(BlueprintCallable, Category = "交互")
	void ClearSelection();

	FDreamInteractionModeChanged OnModeChanged;
	FDreamInteractionSelectionChanged OnSelectionChanged;

private:
	UPROPERTY(VisibleInstanceOnly)
	EDreamInteractionMode InteractionMode = EDreamInteractionMode::ThirdPerson;

	UPROPERTY(VisibleInstanceOnly)
	FGuid SelectedAssemblyId;

	UPROPERTY(VisibleInstanceOnly)
	FGuid SelectedNodeId;
};
