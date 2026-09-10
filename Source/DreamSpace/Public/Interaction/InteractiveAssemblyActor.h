#pragma once
#include "GameFramework/Actor.h"
#include "DreamInteractionTypes.h"
#include "InteractiveAssemblyActor.generated.h"
class UInteractiveAssemblyDefinition;
class UBoxComponent;
class UStaticMeshComponent;
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamAssemblyStateChanged, const FDreamAssemblyState&);

/** 装配体实例仅负责持有状态和刷新表现；状态写入口仅对事务管理器开放。 */
UCLASS(BlueprintType)
class DREAMSPACE_API AInteractiveAssemblyActor : public AActor
{
	GENERATED_BODY()
public:
	AInteractiveAssemblyActor();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostActorCreated() override;
	virtual void PostRegisterAllComponents() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "装配体")
	TObjectPtr<UInteractiveAssemblyDefinition> Definition;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "装配体")
	FGuid AssemblyId;
	bool InitializeFromDefinition();
	UFUNCTION(BlueprintPure, Category = "装配体")
	FGuid GetAssemblyId() const { return AssemblyId; }
	const FDreamAssemblyState& GetAssemblyState() const { return AssemblyState; }
	bool IsOperational() const { return bInitialized; }
	bool IsNodeOperational(const FGuid& Id) const;
	UBoxComponent* FindNodeComponent(const FGuid& Id) const;
	bool FindNodeId(const UPrimitiveComponent* Component, FGuid& Id) const;
	bool GetNodeAssemblyTransform(const FGuid& Id, FTransform& Result) const;
	FDreamAssemblyState GetEffectiveState() const;
	void GetCapabilitiesForNode(const FGuid& Id, TArray<UDreamInteractionCapability*>& Result) const;
	UDreamInteractionCapability* FindCapability(FName Id, const FGuid& NodeId) const;
	bool ValidateState(const FDreamAssemblyState& State, FText& Failure) const;
	void ShowPreview(const FDreamAssemblyState& State);
	void CancelPreview();
	void DrawPreview() const;
	/** 携带时仅刷新根参考系的派生表现，不产生隐式的逐帧事务。 */
	void RefreshPresentation();
	FDreamAssemblyStateChanged OnStateChanged;

private:
	friend class UDreamInteractionWorldSubsystem;
	UPROPERTY(VisibleInstanceOnly, Category = "状态")
	FDreamAssemblyState AssemblyState;
	UPROPERTY(Transient)
	TMap<FGuid, TObjectPtr<UBoxComponent>> NodeComponents;
	UPROPERTY(Transient)
	TMap<FGuid, TObjectPtr<UStaticMeshComponent>> MeshComponents;
	FDreamAssemblyState PreviewState;
	bool bInitialized = false;
	bool bHasPreview = false;
	void SetCommittedState(const FDreamAssemblyState& State);
	void DestroyNodeComponents();
};
