#include "DreamPlayerController.h"
#include "DreamCharacter.h"
#include "DreamOccupantComponent.h"
#include "DreamInteractionPlayerSubsystem.h"
#include "DreamInteractionTargetResolver.h"
#include "DreamInteractionWorldSubsystem.h"
#include "DreamInteractionCapability.h"
#include "InteractiveAssemblyActor.h"
#include "InteractiveAssemblyDefinition.h"
#include "DreamStateMath.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DrawDebugHelpers.h"

UDreamInteractionPlayerSubsystem* ADreamPlayerController::GetInteractionPlayer() const
{
	return GetLocalPlayer() ? GetLocalPlayer()->GetSubsystem<UDreamInteractionPlayerSubsystem>() : nullptr;
}
void ADreamPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
		return;
	Resolver = NewObject<UDreamInteractionTargetResolver>(this);
	OverviewCamera = GetWorld()->SpawnActor<ACameraActor>();
	OverviewCamera->GetCameraComponent()->bConstrainAspectRatio = false;
	if (auto* LocalInteraction = GetInteractionPlayer())
	{
		ModeHandle = LocalInteraction->OnModeChanged.AddUObject(this, &ADreamPlayerController::ModeChanged);
		ModeChanged(LocalInteraction->GetInteractionMode());
	}
	StatusText = FText::FromString(TEXT("Tab 切换视角；全局视角左键选择物体。"));
}
void ADreamPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	Cancel();
	if (auto* LocalInteraction = GetInteractionPlayer())
		LocalInteraction->OnModeChanged.Remove(ModeHandle);
	if (GetLocalPlayer())
		if (auto* Input = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			Input->RemoveMappingContext(Mapping);
	if (OverviewCamera)
		OverviewCamera->Destroy();
	Super::EndPlay(Reason);
}
void ADreamPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	auto* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input || !GetLocalPlayer())
		return;
	Mapping = NewObject<UInputMappingContext>(this);
	auto Button = [&](FKey Key, auto Method)
	{
		auto* Action = NewObject<UInputAction>(this);
		Actions.Add(Action);
		Mapping->MapKey(Action, Key);
		Input->BindAction(Action, ETriggerEvent::Started, this, Method);
	};
	Button(EKeys::Tab, &ADreamPlayerController::ToggleMode);
	Button(EKeys::LeftMouseButton, &ADreamPlayerController::SelectTarget);
	Button(EKeys::R, &ADreamPlayerController::RotateTarget);
	Button(EKeys::Equals, &ADreamPlayerController::ScaleUp);
	Button(EKeys::Hyphen, &ADreamPlayerController::ScaleDown);
	Button(EKeys::Enter, &ADreamPlayerController::Confirm);
	Button(EKeys::Escape, &ADreamPlayerController::Cancel);
	Button(EKeys::P, &ADreamPlayerController::ParentTarget);
	Button(EKeys::O, &ADreamPlayerController::ChildTarget);
	Button(EKeys::E, &ADreamPlayerController::UseTarget);
	Button(EKeys::B, &ADreamPlayerController::BreakTarget);
	Button(EKeys::G, &ADreamPlayerController::DropTarget);
	Button(EKeys::U, &ADreamPlayerController::Undo);
	Button(EKeys::J, &ADreamPlayerController::Redo);
	Button(EKeys::F5, &ADreamPlayerController::Save);
	Button(EKeys::F9, &ADreamPlayerController::Load);
	Button(EKeys::X, &ADreamPlayerController::AxisX);
	Button(EKeys::Y, &ADreamPlayerController::AxisY);
	Button(EKeys::Z, &ADreamPlayerController::AxisZ);
	Button(EKeys::Up, &ADreamPlayerController::TranslateForward);
	Button(EKeys::Down, &ADreamPlayerController::TranslateBack);
	Button(EKeys::Left, &ADreamPlayerController::TranslateLeft);
	Button(EKeys::Right, &ADreamPlayerController::TranslateRight);
	auto* Jump = NewObject<UInputAction>(this);
	Actions.Add(Jump);
	Mapping->MapKey(Jump, EKeys::SpaceBar);
	Input->BindAction(Jump, ETriggerEvent::Started, this, &ADreamPlayerController::StartJump);
	Input->BindAction(Jump, ETriggerEvent::Completed, this, &ADreamPlayerController::EndJump);
	auto* MoveAction = NewObject<UInputAction>(this);
	Actions.Add(MoveAction);
	MoveAction->ValueType = EInputActionValueType::Axis2D;
	auto MapAxis = [&](UInputAction* Action, FKey Key, bool bY, bool bNegative)
	{
		auto& Map = Mapping->MapKey(Action, Key);
		if (bNegative)
			Map.Modifiers.Add(NewObject<UInputModifierNegate>(Mapping));
		if (bY)
		{
			auto* Swizzle = NewObject<UInputModifierSwizzleAxis>(Mapping);
			Swizzle->Order = EInputAxisSwizzle::YXZ;
			Map.Modifiers.Add(Swizzle);
		}
	};
	MapAxis(MoveAction, EKeys::W, true, false);
	MapAxis(MoveAction, EKeys::S, true, true);
	MapAxis(MoveAction, EKeys::D, false, false);
	MapAxis(MoveAction, EKeys::A, false, true);
	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ADreamPlayerController::Move);
	auto* LookAction = NewObject<UInputAction>(this);
	Actions.Add(LookAction);
	LookAction->ValueType = EInputActionValueType::Axis2D;
	MapAxis(LookAction, EKeys::MouseX, false, false);
	MapAxis(LookAction, EKeys::MouseY, true, true);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ADreamPlayerController::Look);
	auto* ZoomAction = NewObject<UInputAction>(this);
	Actions.Add(ZoomAction);
	ZoomAction->ValueType = EInputActionValueType::Axis1D;
	Mapping->MapKey(ZoomAction, EKeys::MouseWheelAxis);
	Input->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &ADreamPlayerController::Zoom);
	GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()->AddMappingContext(Mapping, 0);
}
void ADreamPlayerController::ToggleMode()
{
	if (auto* LocalInteraction = GetInteractionPlayer())
		LocalInteraction->ToggleInteractionMode();
}
void ADreamPlayerController::ModeChanged(EDreamInteractionMode Mode)
{
	Cancel();
	const bool bOverview = Mode == EDreamInteractionMode::Overview;
	bShowMouseCursor = bOverview;
	if (bOverview)
	{
		OverviewFocus = GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector;
		SetViewTargetWithBlend(OverviewCamera, 0.35f);
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
	}
	else
	{
		SetViewTargetWithBlend(GetPawn(), 0.35f);
		SetInputMode(FInputModeGameOnly());
	}
	if (auto* ControlledCharacter = Cast<ADreamCharacter>(GetPawn()))
		ControlledCharacter->GetCharacterMovement()->StopMovementImmediately();
}
void ADreamPlayerController::PlayerTick(float Delta)
{
	Super::PlayerTick(Delta);
	auto* LocalInteraction = GetInteractionPlayer();
	if (!LocalInteraction)
		return;
	if (LocalInteraction->GetInteractionMode() == EDreamInteractionMode::Overview && OverviewCamera)
	{
		OverviewCamera->SetActorLocation(OverviewFocus - OverviewRotation.Vector() * OverviewDistance);
		OverviewCamera->SetActorRotation(OverviewRotation);
	}
	auto* System = GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>();
	if (auto* A = System->FindAssembly(LocalInteraction->GetSelectedAssemblyId()))
	{
		FTransform Frame;
		DreamState::WorldTransform(A->GetEffectiveState(), LocalInteraction->GetSelectedNodeId(), Frame);
		// Gizmo 使用目标参考系；X 红、Y 绿、Z 蓝，与输入轴选择一致。
		const FVector P = Frame.GetLocation();
		const FQuat R = Frame.GetRotation();
		DrawDebugDirectionalArrow(GetWorld(), P, P + R.GetAxisX() * 150, 20, FColor::Red, false, 0, 0, 3);
		DrawDebugDirectionalArrow(GetWorld(), P, P + R.GetAxisY() * 150, 20, FColor::Green, false, 0, 0, 3);
		DrawDebugDirectionalArrow(GetWorld(), P, P + R.GetAxisZ() * 150, 20, FColor::Blue, false, 0, 0, 3);
	}
}
void ADreamPlayerController::UpdateRotation(float Delta)
{
	auto* ControlledCharacter = Cast<ADreamCharacter>(GetPawn());
	if (!ControlledCharacter || !GetInteractionPlayer() ||
		GetInteractionPlayer()->GetInteractionMode() == EDreamInteractionMode::Overview)
		return;
	const FVector Down = ControlledCharacter->GetCharacterMovement()->GetGravityDirection();
	const FQuat ToWorld = FQuat::FindBetweenNormals(FVector::DownVector, Down);
	FRotator Local = (ToWorld.Inverse() * GetControlRotation().Quaternion()).Rotator();
	Local.Yaw += RotationInput.Yaw;
	Local.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Local.Pitch + RotationInput.Pitch), -80.0, 80.0);
	Local.Roll = 0;
	SetControlRotation((ToWorld * Local.Quaternion()).Rotator());
}
void ADreamPlayerController::Move(const FInputActionValue& Value)
{
	const auto Axis = Value.Get<FVector2D>();
	auto* LocalInteraction = GetInteractionPlayer();
	if (!LocalInteraction)
		return;
	if (LocalInteraction->GetInteractionMode() == EDreamInteractionMode::Overview)
	{
		const FRotator Flat(0, OverviewRotation.Yaw, 0);
		const FVector Forward = Flat.Vector();
		OverviewFocus +=
			(Forward * Axis.Y + FVector::UpVector.Cross(Forward) * Axis.X) * GetWorld()->GetDeltaSeconds() * 1200;
	}
	else if (auto* ControlledCharacter = Cast<ADreamCharacter>(GetPawn()))
		ControlledCharacter->MoveOnGravityPlane(Axis);
}
void ADreamPlayerController::Look(const FInputActionValue& Value)
{
	const auto Axis = Value.Get<FVector2D>();
	auto* LocalInteraction = GetInteractionPlayer();
	if (!LocalInteraction)
		return;
	if (LocalInteraction->GetInteractionMode() == EDreamInteractionMode::Overview)
	{
		if (IsInputKeyDown(EKeys::RightMouseButton))
		{
			OverviewRotation.Yaw += Axis.X;
			OverviewRotation.Pitch = FMath::Clamp(OverviewRotation.Pitch + Axis.Y, -85.0, -10.0);
		}
	}
	else
	{
		AddYawInput(Axis.X);
		AddPitchInput(Axis.Y);
	}
}
void ADreamPlayerController::Zoom(const FInputActionValue& Value)
{
	OverviewDistance = FMath::Clamp(OverviewDistance - Value.Get<float>() * 150, 300.0f, 10000.0f);
}
void ADreamPlayerController::StartJump()
{
	if (GetInteractionPlayer() && GetInteractionPlayer()->GetInteractionMode() == EDreamInteractionMode::ThirdPerson)
		if (auto* C = Cast<ADreamCharacter>(GetPawn()))
			C->Jump();
}
void ADreamPlayerController::EndJump()
{
	if (auto* C = Cast<ADreamCharacter>(GetPawn()))
		C->StopJumping();
}
void ADreamPlayerController::SetFailure(const FText& Failure)
{
	StatusText = Failure;
	UE_LOG(LogDreamInteraction, Log, TEXT("%s"), *Failure.ToString());
}
void ADreamPlayerController::SelectTarget()
{
	if (!Resolver || !GetInteractionPlayer())
		return;
	Cancel();
	auto* LocalInteraction = GetInteractionPlayer();
	FDreamInteractionTarget Target;
	bool bFound = false;
	if (LocalInteraction->GetInteractionMode() == EDreamInteractionMode::Overview)
		bFound = Resolver->ResolveScreenTarget(*this, LocalInteraction->GetInteractionMode(), Target);
	else
	{
		FVector Origin;
		FRotator Rotation;
		GetPlayerViewPoint(Origin, Rotation);
		bFound = Resolver->ResolveWorldRay(*GetWorld(), Origin, Origin + Rotation.Vector() * 1500,
			LocalInteraction->GetInteractionMode(), Target, GetPawn());
	}
	if (bFound)
	{
		LocalInteraction->SetSelection(Target.Assembly->AssemblyId, Target.NodeId);
		StatusText = FText::FromString(TEXT("已选择。R 旋转，+/- 缩放；P 选父级，O 选子级。"));
	}
	else
	{
		LocalInteraction->ClearSelection();
		StatusText = FText::FromString(TEXT("没有选中可交互对象。"));
	}
}
bool ADreamPlayerController::BeginCapability(FName Id)
{
	auto* LocalInteraction = GetInteractionPlayer();
	if (!LocalInteraction || !LocalInteraction->HasSelection())
		return false;
	auto* System = GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>();
	auto* C = Cast<ADreamCharacter>(GetPawn());
	FText Failure;
	if (!System->BeginInteraction(LocalInteraction->GetSelectedAssemblyId(), LocalInteraction->GetSelectedNodeId(), Id,
			LocalInteraction->GetInteractionMode(), C ? C->Occupant->OccupantId : FGuid(), ActiveSession, Failure))
	{
		SetFailure(Failure);
		return false;
	}
	Intent = FDreamInteractionIntent();
	Intent.TargetNodeId = LocalInteraction->GetSelectedNodeId();
	return true;
}
void ADreamPlayerController::UpdateTransform(float Rotation, float Scale, const FVector& Translation)
{
	if (!ActiveSession.IsValid() && !BeginCapability(TEXT("Transform")))
		return;
	Intent.Type = EDreamInteractionIntentType::UpdateTransform;
	Intent.RotationAxis = InputAxis;
	Intent.RotationDeltaDegrees += Rotation;
	Intent.ScaleDelta = (1 + Intent.ScaleDelta) * (1 + Scale) - 1;
	Intent.Translation += Translation;
	auto* System = GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>();
	FText Failure;
	bPreviewValid = System->PrepareCommand(ActiveSession, Intent, PendingCommand, Failure) &&
					System->PreviewCommand(PendingCommand, Failure);
	if (!bPreviewValid)
		SetFailure(Failure);
	else
		StatusText = FText::FromString(TEXT("预览有效：Enter 提交，Esc 取消。蓝色轮廓是候选姿态。"));
}
void ADreamPlayerController::RotateTarget()
{
	UpdateTransform(IsInputKeyDown(EKeys::LeftShift) ? -90 : 90, 0, FVector::ZeroVector);
}
void ADreamPlayerController::ScaleUp()
{
	UpdateTransform(0, 0.25f, FVector::ZeroVector);
}
void ADreamPlayerController::ScaleDown()
{
	UpdateTransform(0, -0.2f, FVector::ZeroVector);
}
void ADreamPlayerController::TranslateForward()
{
	UpdateTransform(0, 0, FVector(100, 0, 0));
}
void ADreamPlayerController::TranslateBack()
{
	UpdateTransform(0, 0, FVector(-100, 0, 0));
}
void ADreamPlayerController::TranslateLeft()
{
	UpdateTransform(0, 0, FVector(0, -100, 0));
}
void ADreamPlayerController::TranslateRight()
{
	UpdateTransform(0, 0, FVector(0, 100, 0));
}
void ADreamPlayerController::AxisX()
{
	InputAxis = FVector::ForwardVector;
	StatusText = FText::FromString(TEXT("选择 X 轴。"));
}
void ADreamPlayerController::AxisY()
{
	InputAxis = FVector::RightVector;
	StatusText = FText::FromString(TEXT("选择 Y 轴。"));
}
void ADreamPlayerController::AxisZ()
{
	InputAxis = FVector::UpVector;
	StatusText = FText::FromString(TEXT("选择 Z 轴。"));
}
void ADreamPlayerController::ParentTarget()
{
	Cancel();
	auto* P = GetInteractionPlayer();
	if (!P)
		return;
	auto* A = GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>()->FindAssembly(P->GetSelectedAssemblyId());
	if (A)
		if (const auto* N = A->GetAssemblyState().FindNode(P->GetSelectedNodeId()))
			P->SetSelection(A->AssemblyId, N->ParentNodeId);
}
void ADreamPlayerController::ChildTarget()
{
	Cancel();
	auto* P = GetInteractionPlayer();
	if (!P)
		return;
	auto* A = GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>()->FindAssembly(P->GetSelectedAssemblyId());
	if (A)
		for (const auto& N : A->GetAssemblyState().Nodes)
			if (N.ParentNodeId == P->GetSelectedNodeId())
			{
				P->SetSelection(A->AssemblyId, N.NodeId);
				break;
			}
}
void ADreamPlayerController::ExecuteDiscrete(FName Id)
{
	Cancel();
	if (!BeginCapability(Id))
		return;
	auto* S = GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>();
	FText Failure;
	if (!S->PrepareCommand(ActiveSession, Intent, PendingCommand, Failure) ||
		!S->ExecuteCommand(PendingCommand, nullptr, Failure))
	{
		S->CancelInteraction(ActiveSession);
		SetFailure(Failure);
	}
	else
		StatusText = FText::FromString(TEXT("操作已提交。"));
	ActiveSession.Invalidate();
	bPreviewValid = false;
}
void ADreamPlayerController::UseTarget()
{
	SelectTarget();
	auto* P = GetInteractionPlayer();
	if (!P)
		return;
	auto* A = GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>()->FindAssembly(P->GetSelectedAssemblyId());
	if (A)
		ExecuteDiscrete(
			A->FindCapability(TEXT("OpenDoor"), P->GetSelectedNodeId()) ? TEXT("OpenDoor") : TEXT("Pickup"));
}
void ADreamPlayerController::BreakTarget()
{
	ExecuteDiscrete(TEXT("Destruction"));
}
void ADreamPlayerController::DropTarget()
{
	Cancel();
	auto* C = Cast<ADreamCharacter>(GetPawn());
	if (!C || !GetInteractionPlayer())
		return;
	if (auto* A =
			GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>()->FindCarriedAssembly(C->Occupant->OccupantId))
	{
		GetInteractionPlayer()->SetSelection(A->AssemblyId, FGuid());
		ExecuteDiscrete(TEXT("Drop"));
	}
}
void ADreamPlayerController::Confirm()
{
	if (!ActiveSession.IsValid() || !bPreviewValid)
		return;
	FText Failure;
	auto* S = GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>();
	if (!S->ExecuteCommand(PendingCommand, nullptr, Failure))
	{
		bPreviewValid = false;
		SetFailure(Failure);
		return;
	}
	ActiveSession.Invalidate();
	bPreviewValid = false;
	StatusText = FText::FromString(TEXT("已提交；U 撤销，J 重做。"));
}
void ADreamPlayerController::Cancel()
{
	if (ActiveSession.IsValid() && GetWorld())
		if (auto* S = GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>())
			S->CancelInteraction(ActiveSession);
	ActiveSession.Invalidate();
	bPreviewValid = false;
}
void ADreamPlayerController::Undo()
{
	Cancel();
	if (!GetInteractionPlayer())
		return;
	FText Failure;
	if (!GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>()->UndoLastTransaction(
			GetInteractionPlayer()->GetSelectedAssemblyId(), Failure))
		SetFailure(Failure);
	else
		StatusText = FText::FromString(TEXT("已撤销。"));
}
void ADreamPlayerController::Redo()
{
	Cancel();
	if (!GetInteractionPlayer())
		return;
	FText Failure;
	if (!GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>()->RedoLastTransaction(
			GetInteractionPlayer()->GetSelectedAssemblyId(), Failure))
		SetFailure(Failure);
	else
		StatusText = FText::FromString(TEXT("已重做。"));
}
void ADreamPlayerController::Save()
{
	FText Failure;
	if (!GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>()->SaveToSlot(TEXT("DreamInteraction"), Failure))
		SetFailure(Failure);
	else
		StatusText = FText::FromString(TEXT("世界状态已保存。"));
}
void ADreamPlayerController::Load()
{
	Cancel();
	FText Failure;
	if (!GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>()->LoadFromSlot(TEXT("DreamInteraction"), Failure))
		SetFailure(Failure);
	else
		StatusText = FText::FromString(TEXT("世界状态已恢复。"));
}
FString ADreamPlayerController::DescribeSelection() const
{
	auto* P = GetInteractionPlayer();
	if (!P)
		return TEXT("");
	auto* A = GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>()->FindAssembly(P->GetSelectedAssemblyId());
	if (!A)
		return TEXT("未选择对象");
	const auto* D = A->Definition->FindNodeDefinition(P->GetSelectedNodeId());
	FString Result =
		A->Definition->DisplayName.ToString() + TEXT(" / ") + (D ? D->NodeName.ToString() : TEXT("装配体根"));
	TArray<UDreamInteractionCapability*> Caps;
	A->GetCapabilitiesForNode(P->GetSelectedNodeId(), Caps);
	Result += TEXT("  能力：");
	for (const auto* Cap : Caps)
		if (Cap->SupportsMode(P->GetInteractionMode()))
			Result += Cap->DisplayName.ToString() + TEXT("  ");
	return Result;
}
