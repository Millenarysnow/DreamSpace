#include "DreamInteractionTargetResolver.h"

#include "DreamInteractionCapability.h"
#include "DreamInteractionWorldSubsystem.h"
#include "InteractiveAssemblyActor.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"

bool UDreamInteractionTargetResolver::ResolveScreenTarget(APlayerController& PlayerController,
	EDreamInteractionMode Mode,
	FDreamInteractionTarget& OutTarget) const
{
	OutTarget = FDreamInteractionTarget();
	FVector WorldOrigin;
	FVector WorldDirection;
	if (!PlayerController.DeprojectMousePositionToWorld(WorldOrigin, WorldDirection))
	{
		return false;
	}
	if (!PlayerController.GetWorld())
	{
		return false;
	}
	return ResolveWorldRay(*PlayerController.GetWorld(), WorldOrigin,
		WorldOrigin + WorldDirection * TraceDistance, Mode, OutTarget);
}

bool UDreamInteractionTargetResolver::ResolveWorldRay(UWorld& World,
	const FVector& Start,
	const FVector& End,
	EDreamInteractionMode Mode,
	FDreamInteractionTarget& OutTarget) const
{
	OutTarget = FDreamInteractionTarget();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DreamInteractionTarget), true);
	FHitResult Hit;
	if (!World.LineTraceSingleByChannel(Hit, Start, End, TraceChannel, QueryParams))
	{
		return false;
	}

	UDreamInteractionWorldSubsystem* Subsystem = World.GetSubsystem<UDreamInteractionWorldSubsystem>();
	if (!Subsystem)
	{
		return false;
	}
	AInteractiveAssemblyActor* Assembly = nullptr;
	FGuid NodeId;
	if (!Subsystem->ResolveComponentTarget(Hit.GetComponent(), Assembly, NodeId) || !Assembly)
	{
		return false;
	}

	OutTarget.Assembly = Assembly;
	OutTarget.NodeId = NodeId;
	OutTarget.WorldPoint = Hit.ImpactPoint;
	OutTarget.WorldNormal = Hit.ImpactNormal;
	Assembly->GetCapabilitiesForNode(NodeId, OutTarget.Capabilities);
	for (int32 Index = OutTarget.Capabilities.Num() - 1; Index >= 0; --Index)
	{
		UDreamInteractionCapability* Capability = OutTarget.Capabilities[Index];
		// 选择阶段只做静态筛选；CanStart 要等到用户产生 Begin/Confirm 意图后再检查。
		if (!Capability || !Capability->bEnabledByDefault || !Capability->SupportsMode(Mode))
		{
			OutTarget.Capabilities.RemoveAt(Index);
		}
	}
	OutTarget.bValid = true;
	return true;
}
