#include "DreamPlayerController.h"
#include "DreamCharacter.h"
#include "DreamInteractableInterface.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/HitResult.h"

void ADreamPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	// 退出时移除动态创建的输入映射，避免泄漏到下一次会话。
	if (GetLocalPlayer())
		if (auto* Input = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			Input->RemoveMappingContext(Mapping);
	Super::EndPlay(Reason);
}
void ADreamPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	auto* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input || !GetLocalPlayer())
		return;
	Mapping = NewObject<UInputMappingContext>(this);
	// 辅助 lambda：创建一个瞬时按钮动作并绑定到成员函数。
	auto Button = [&](FKey Key, auto Method)
	{
		auto* Action = NewObject<UInputAction>(this);
		Actions.Add(Action);
		Mapping->MapKey(Action, Key);
		Input->BindAction(Action, ETriggerEvent::Started, this, Method);
	};
	Button(EKeys::E, &ADreamPlayerController::Interact);
	// 空格跳跃需要同时监听按下与松开。
	auto* Jump = NewObject<UInputAction>(this);
	Actions.Add(Jump);
	Mapping->MapKey(Jump, EKeys::SpaceBar);
	Input->BindAction(Jump, ETriggerEvent::Started, this, &ADreamPlayerController::StartJump);
	Input->BindAction(Jump, ETriggerEvent::Completed, this, &ADreamPlayerController::EndJump);
	// WASD 移动：单动作 Axis2D，通过修饰器把四个方向键合成二维轴。
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
	// 鼠标视角：MouseY 取反以符合常见操作习惯。
	auto* LookAction = NewObject<UInputAction>(this);
	Actions.Add(LookAction);
	LookAction->ValueType = EInputActionValueType::Axis2D;
	MapAxis(LookAction, EKeys::MouseX, false, false);
	MapAxis(LookAction, EKeys::MouseY, true, true);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ADreamPlayerController::Look);
	GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()->AddMappingContext(Mapping, 0);
}
void ADreamPlayerController::UpdateRotation(float Delta)
{
	// 在自定义重力场景中，把控制旋转转换到重力相对空间再叠加输入，保证相机姿态始终贴合当前重力方向。
	auto* ControlledCharacter = Cast<ADreamCharacter>(GetPawn());
	if (!ControlledCharacter)
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
	if (auto* ControlledCharacter = Cast<ADreamCharacter>(GetPawn()))
		ControlledCharacter->MoveOnGravityPlane(Value.Get<FVector2D>());
}
void ADreamPlayerController::Look(const FInputActionValue& Value)
{
	const auto Axis = Value.Get<FVector2D>();
	AddYawInput(Axis.X);
	AddPitchInput(Axis.Y);
}
void ADreamPlayerController::StartJump()
{
	if (auto* ControlledCharacter = Cast<ADreamCharacter>(GetPawn()))
		ControlledCharacter->Jump();
}
void ADreamPlayerController::EndJump()
{
	if (auto* ControlledCharacter = Cast<ADreamCharacter>(GetPawn()))
		ControlledCharacter->StopJumping();
}
void ADreamPlayerController::Interact()
{
	// 从相机中心向前做射线检测，命中后调用该 Actor 上所有实现了可交互接口的组件。
	// 控制器只做“触发”，具体行为（转动、开关门等）完全由组件自身决定。
	FVector Origin;
	FRotator Rotation;
	GetPlayerViewPoint(Origin, Rotation);
	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(GetPawn());
	if (!GetWorld()->LineTraceSingleByChannel(
			Hit, Origin, Origin + Rotation.Vector() * InteractTraceDistance, ECC_Visibility, Params))
		return;
	AActor* HitActor = Hit.GetActor();
	if (!HitActor)
		return;
	// 收集命中 Actor 上全部可交互组件并逐一触发；组件自行决定是否响应。
	TArray<UActorComponent*> Interactables =
		HitActor->GetComponentsByInterface(UDreamInteractableInterface::StaticClass());
	for (UActorComponent* Component : Interactables)
		IDreamInteractableInterface::Execute_OnInteracted(Component, GetPawn());
}
