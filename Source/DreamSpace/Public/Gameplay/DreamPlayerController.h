#pragma once
#include "GameFramework/PlayerController.h"
#include "DreamInteractionTypes.h"
#include "DreamPlayerController.generated.h"
class UInputMappingContext;
class UInputAction;
class UDreamInteractionPlayerSubsystem;
class UDreamInteractionTargetResolver;
class ACameraActor;
struct FInputActionValue;

/** 输入适配、相机和交互会话协调；不包含具体建筑行为。 */
UCLASS()
class DREAMSPACE_API ADreamPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void UpdateRotation(float DeltaTime) override;
	FText GetStatusText() const { return StatusText; }
	FString DescribeSelection() const;
	UDreamInteractionPlayerSubsystem* GetInteractionPlayer() const;

private:
	UPROPERTY()
	TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY()
	TArray<TObjectPtr<UInputAction>> Actions;
	UPROPERTY()
	TObjectPtr<UDreamInteractionTargetResolver> Resolver;
	UPROPERTY()
	TObjectPtr<ACameraActor> OverviewCamera;
	FGuid ActiveSession;
	FDreamInteractionIntent Intent;
	FDreamInteractionCommand PendingCommand;
	bool bPreviewValid = false;
	FVector OverviewFocus = FVector::ZeroVector;
	FRotator OverviewRotation = FRotator(-50, -45, 0);
	float OverviewDistance = 2400;
	FVector InputAxis = FVector::UpVector;
	FText StatusText;
	FDelegateHandle ModeHandle;
	void ModeChanged(EDreamInteractionMode Mode);
	void SelectTarget();
	void ToggleMode();
	void RotateTarget();
	void ScaleUp();
	void ScaleDown();
	void TranslateForward();
	void TranslateBack();
	void TranslateLeft();
	void TranslateRight();
	void ParentTarget();
	void ChildTarget();
	void UseTarget();
	void BreakTarget();
	void DropTarget();
	void Confirm();
	void Cancel();
	void Undo();
	void Redo();
	void Save();
	void Load();
	void AxisX();
	void AxisY();
	void AxisZ();
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void Zoom(const FInputActionValue& Value);
	void StartJump();
	void EndJump();
	bool BeginCapability(FName Id);
	void UpdateTransform(float Rotation, float Scale, const FVector& Translation);
	void ExecuteDiscrete(FName Id);
	void SetFailure(const FText& Failure);
};
