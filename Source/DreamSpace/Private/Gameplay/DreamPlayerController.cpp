#include "DreamPlayerController.h"
#include "DreamCharacter.h"
#include "DreamInteractableInterface.h"
#include "DreamSpace.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "Components/ActorComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/HitResult.h"
#include "DrawDebugHelpers.h"

void ADreamPlayerController::BeginPlay()
{
	Super::BeginPlay();
	// BeginPlay 通常已经有 LocalPlayer；如果初始化顺序不同，ReceivedPlayer
	// 也会再次尝试，确保 Enhanced Input 映射不会因为过早检查而漏装。
	ApplyInputMapping();
}

void ADreamPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();
	ApplyInputMapping();
}

void ADreamPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	// 退出时移除动态创建的输入映射，避免泄漏到下一次会话。
	if (bMappingApplied && Mapping && GetLocalPlayer())
		if (auto* Input = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			Input->RemoveMappingContext(Mapping);
	bMappingApplied = false;
	Super::EndPlay(Reason);
}

void ADreamPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	auto* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		UE_LOG(LogDreamSpace, Error,
			TEXT("玩家控制器输入组件不是 UEnhancedInputComponent，无法安装运行时输入映射。实际类型：%s"),
			*GetNameSafe(InputComponent));
		return;
	}
	if (Mapping)
	{
		ApplyInputMapping();
		return;
	}

	Mapping = NewObject<UInputMappingContext>(this, TEXT("DreamRuntimeInputMapping"));
	// 辅助 lambda：创建一个瞬时按钮动作并绑定到成员函数。
	auto Button = [&](FKey Key, auto Method)
	{
		auto* Action = NewObject<UInputAction>(this, MakeUniqueObjectName(this, UInputAction::StaticClass(), FName(*FString::Printf(TEXT("DreamAction_%s"), *Key.ToString()))));
		Action->ValueType = EInputActionValueType::Boolean;
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
	// 鼠标滚轮：Axis1D，向上滚动为正、向下为负。
	auto* ZoomAction = NewObject<UInputAction>(this);
	Actions.Add(ZoomAction);
	ZoomAction->ValueType = EInputActionValueType::Axis1D;
	Mapping->MapKey(ZoomAction, EKeys::MouseWheelAxis);
	Input->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &ADreamPlayerController::ZoomCamera);
	ApplyInputMapping();
}

void ADreamPlayerController::ApplyInputMapping()
{
	if (bMappingApplied || !Mapping || !GetLocalPlayer())
		return;

	if (UEnhancedInputLocalPlayerSubsystem* Input =
		GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		Input->AddMappingContext(Mapping, 0);
		bMappingApplied = true;
		UE_LOG(LogDreamSpace, Verbose, TEXT("已为玩家控制器安装运行时输入映射（包含 E 交互键）。"));
	}
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
void ADreamPlayerController::ZoomCamera(const FInputActionValue& Value)
{
	const float WheelDelta = Value.Get<float>();
	if (FMath::IsNearlyZero(WheelDelta))
		return;

	auto* ControlledCharacter = Cast<ADreamCharacter>(GetPawn());
	if (!ControlledCharacter)
		return;
	USpringArmComponent* SpringArm = ControlledCharacter->FindComponentByClass<USpringArmComponent>();
	if (!SpringArm)
		return;

	// 向上滚动（WheelDelta > 0）拉近相机，向下滚动拉远。
	// CameraZoomStep 为负时可以反转方向。
	const float NewLength = FMath::Clamp(
		SpringArm->TargetArmLength - WheelDelta * CameraZoomStep,
		MinCameraArmLength, MaxCameraArmLength);
	SpringArm->TargetArmLength = NewLength;
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
	const FVector TraceEnd = Origin + Rotation.Vector() * InteractTraceDistance;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, Origin, TraceEnd, InteractTraceChannel, Params);

	// 每次按 E 都把本次实际射线短暂留在世界中：绿色表示命中，红色表示未命中。
	// 使用有限显示时长，既能在按键后观察射线，也不会让旧调试线永久堆积在场景里。
	const FColor TraceColor = bHit ? FColor::Green : FColor::Red;
	DrawDebugLine(GetWorld(), Origin, bHit ? Hit.ImpactPoint : TraceEnd, TraceColor,
		false, 2.5f, 0, 2.0f);
	if (bHit)
	{
		// 球标记真实碰撞点，法线箭头显示命中表面的朝向，方便判断射线是否打在预期碰撞体上。
		DrawDebugSphere(GetWorld(), Hit.ImpactPoint, 8.0f, 12, FColor::Yellow,
			false, 2.5f, 0, 2.0f);
		DrawDebugDirectionalArrow(GetWorld(), Hit.ImpactPoint,
			Hit.ImpactPoint + Hit.ImpactNormal * 35.0f, 8.0f, FColor::Cyan,
			false, 2.5f, 0, 1.5f);
	}

	if (!bHit)
	{
		UE_LOG(LogDreamSpace, Verbose,
			TEXT("E 交互未命中任何对象。通道=%s，距离=%.1f，起点=%s，终点=%s。请确认目标 PrimitiveComponent 阻挡该通道。"),
			*UEnum::GetValueAsString(InteractTraceChannel.GetValue()), InteractTraceDistance,
			*Origin.ToString(), *TraceEnd.ToString());
		return;
	}

	DispatchInteraction(Hit.GetActor(), Hit.GetComponent());
}

void ADreamPlayerController::DispatchInteraction(AActor* HitActor, UActorComponent* HitComponent)
{
	if (!HitActor && HitComponent)
		HitActor = HitComponent->GetOwner();
	if (!HitActor && !HitComponent)
		return;

	TArray<UActorComponent*> Interactables;
	if (HitActor)
		Interactables = HitActor->GetComponentsByInterface(UDreamInteractableInterface::StaticClass());

	// 如果射线直接命中一个实现接口的 PrimitiveComponent，也支持它自身作为
	// 交互目标；随后再触发所属 Actor 的其他交互组件，并避免重复调用。
	TSet<UActorComponent*> Dispatched;
	if (HitComponent && HitComponent->GetClass()->ImplementsInterface(UDreamInteractableInterface::StaticClass()))
	{
		IDreamInteractableInterface::Execute_OnInteracted(HitComponent, GetPawn());
		Dispatched.Add(HitComponent);
	}
	for (UActorComponent* Component : Interactables)
	{
		if (Component && !Dispatched.Contains(Component))
		{
			IDreamInteractableInterface::Execute_OnInteracted(Component, GetPawn());
			Dispatched.Add(Component);
		}
	}

	UE_LOG(LogDreamSpace, Verbose,
		TEXT("E 交互命中 Actor=%s Component=%s，可交互组件=%d。"),
		*GetNameSafe(HitActor), *GetNameSafe(HitComponent), Dispatched.Num());
	if (Dispatched.IsEmpty())
		UE_LOG(LogDreamSpace, Warning,
			TEXT("E 交互命中了 Actor [%s]，但其上没有实现 IDreamInteractableInterface 的组件。"),
			*GetNameSafe(HitActor));
}
