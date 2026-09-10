#include "DreamOccupantComponent.h"
#include "DreamInteractionWorldSubsystem.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
UDreamOccupantComponent::UDreamOccupantComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}
void UDreamOccupantComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!OccupantId.IsValid())
		OccupantId = FGuid::NewGuid();
	GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>()->RegisterOccupant(this);
	if (auto* Character = Cast<ACharacter>(GetOwner()))
		Character->GetCharacterMovement()->AddTickPrerequisiteComponent(this);
}
void UDreamOccupantComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* System = GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>())
		System->UnregisterOccupant(this);
	Super::EndPlay(Reason);
}
FTransform UDreamOccupantComponent::GetCarryFrame() const
{
	FTransform Frame = GetOwner()->GetActorTransform();
	Frame.SetScale3D(FVector::OneVector);
	Frame.AddToTranslation(Frame.TransformVectorNoScale(CarryOffset));
	return Frame;
}
FDreamOccupantState UDreamOccupantComponent::CaptureState() const
{
	FDreamOccupantState S;
	S.OccupantId = OccupantId;
	S.Transform = GetOwner()->GetActorTransform();
	if (const auto* Character = Cast<ACharacter>(GetOwner()))
	{
		const auto* Move = Character->GetCharacterMovement();
		S.Velocity = Move->Velocity;
		S.GravityDirection = Move->GetGravityDirection();
		S.MovementMode = Move->MovementMode;
		S.CustomMovementMode = Move->CustomMovementMode;
		if (const auto* Controller = Character->GetController())
			S.ControlRotation = Controller->GetControlRotation().Quaternion();
	}
	return S;
}
void UDreamOccupantComponent::RestoreState(const FDreamOccupantState& S)
{
	GetOwner()->SetActorTransform(S.Transform, false, nullptr, ETeleportType::TeleportPhysics);
	if (auto* Character = Cast<ACharacter>(GetOwner()))
	{
		// 清空引擎移动平台缓存，避免下一帧再应用一次建筑已提交的参考系变化。
		Character->SetBase(nullptr);
		auto* Move = Character->GetCharacterMovement();
		Move->SetGravityDirection(S.GravityDirection);
		Move->SetMovementMode(static_cast<EMovementMode>(S.MovementMode), S.CustomMovementMode);
		Move->Velocity = S.Velocity;
		Move->bForceNextFloorCheck = true;
		if (auto* Controller = Character->GetController())
			Controller->SetControlRotation(S.ControlRotation.Rotator());
	}
}
void UDreamOccupantComponent::TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Delta, Tick, Function);
	if (!bResolveLocalGravity)
		return;
	auto* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
		return;
	FVector Direction;
	FGuid Source, Node;
	GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>()->ResolveGravityAtLocation(
		Character->GetActorLocation(), Direction, &Source, &Node);
	auto S = CaptureState();
	if (!S.GravityDirection.Equals(Direction, 0.0001))
	{
		const FQuat Turn = FQuat::FindBetweenNormals(S.GravityDirection, Direction);
		S.Transform.SetRotation(Turn * S.Transform.GetRotation());
		S.ControlRotation = Turn * S.ControlRotation;
		S.GravityDirection = Direction;
		RestoreState(S);
	}
}
