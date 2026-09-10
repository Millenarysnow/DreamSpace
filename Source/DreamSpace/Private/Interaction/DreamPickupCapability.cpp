#include "DreamPickupCapability.h"
#include "DreamInteractionWorldSubsystem.h"
#include "DreamOccupantComponent.h"
#include "InteractiveAssemblyActor.h"
#include "DreamStateMath.h"
#include "DreamInteractionTags.h"
UDreamPickupCapability::UDreamPickupCapability()
{
	CapabilityId = TEXT("Pickup");
	DisplayName = FText::FromString(TEXT("拾取"));
	AllowedModes.Add(EDreamInteractionMode::ThirdPerson);
	CarriedStateTag = DreamTags::StateCarried;
}
void UDreamPickupCapability::ValidateConfiguration(
	const UInteractiveAssemblyDefinition& D, TArray<FString>& Errors) const
{
	Super::ValidateConfiguration(D, Errors);
	if (!FMath::IsFinite(MaximumScaleToPickup) || MaximumScaleToPickup <= 0 || !FMath::IsFinite(MaximumDistance) ||
		MaximumDistance <= 0 || !DreamState::IsFinitePositive(CarryTransform))
		Errors.Add(TEXT("拾取范围、尺寸或携带插槽配置无效。"));
}
bool UDreamPickupCapability::CanStart(const FDreamCapabilityContext& C, FText& Failure) const
{
	if (!Super::CanStart(C, Failure))
		return false;
	auto* World = C.Target.GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>();
	auto* User = World->FindOccupant(C.RequesterId);
	if (!User)
	{
		Failure = FText::FromString(TEXT("拾取需要有效的玩家。"));
		return false;
	}
	if (bDropInsteadOfPickup)
	{
		if (!C.State.bIsCarried || C.State.CarrierId != C.RequesterId)
		{
			Failure = FText::FromString(TEXT("只能放下自己携带的物体。"));
			return false;
		}
	}
	else
	{
		if (C.State.bIsCarried || C.State.RootTransform.GetScale3D().GetMax() > MaximumScaleToPickup)
		{
			Failure = FText::FromString(TEXT("请将物体缩小后再拾取。"));
			return false;
		}
		if (FVector::Dist(C.State.RootTransform.GetLocation(), User->GetOwner()->GetActorLocation()) > MaximumDistance)
		{
			Failure = FText::FromString(TEXT("物体超出拾取距离。"));
			return false;
		}
		for (const auto& Node : C.State.Nodes)
			if (Node.RuntimeState == EDreamNodeRuntimeState::Released || !Node.IsOperational())
			{
				Failure = FText::FromString(TEXT("已拆分或损坏的对象不能作为整体拾取。"));
				return false;
			}
		if (World->FindCarriedAssembly(C.RequesterId))
		{
			Failure = FText::FromString(TEXT("请先放下当前物体。"));
			return false;
		}
	}
	return true;
}
bool UDreamPickupCapability::BuildResult(
	const FDreamCapabilityContext& C, FDreamInteractionCommand& Command, FText& Failure) const
{
	auto* World = C.Target.GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>();
	auto* User = World->FindOccupant(C.RequesterId);
	if (!User)
	{
		Failure = FText::FromString(TEXT("持有者已离开世界。"));
		return false;
	}
	auto& Result = Command.ResultState;
	Result.bIsCarried = !bDropInsteadOfPickup;
	if (bDropInsteadOfPickup)
	{
		// 放下时保存最后的真实插槽世界姿态，并通过事务检查放下位置是否被阻挡。
		Result.RootTransform = C.State.CarryTransform * User->GetCarryFrame();
		Result.CarrierId.Invalidate();
		Result.CarryTransform = FTransform::Identity;
		Result.StateTags.RemoveTag(CarriedStateTag);
	}
	else
	{
		Result.CarrierId = C.RequesterId;
		Result.CarryTransform = CarryTransform;
		Result.CarryTransform.SetScale3D(C.State.RootTransform.GetScale3D());
		Result.StateTags.AddTag(CarriedStateTag);
	}
	Command.Type = EDreamCommandType::Pickup;
	return true;
}
