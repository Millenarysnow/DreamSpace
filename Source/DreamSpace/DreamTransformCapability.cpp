#include "DreamTransformCapability.h"

#include "InteractiveAssemblyActor.h"

UDreamTransformCapability::UDreamTransformCapability()
{
	CapabilityTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Interaction.Transform")), false);
	AllowedModes.Add(EDreamInteractionMode::Overview);
}

bool UDreamTransformCapability::CanStart(const AInteractiveAssemblyActor& Target,
	const FDreamInteractionIntent& Intent,
	EDreamInteractionMode Mode,
	FText& OutFailure) const
{
	if (!UDreamInteractionCapability::CanStart(Target, Intent, Mode, OutFailure))
	{
		return false;
	}
	if (Intent.Type != EDreamInteractionIntentType::UpdateTransform &&
		Intent.Type != EDreamInteractionIntentType::Begin)
	{
		OutFailure = FText::FromString(TEXT("变换能力只接受开始或更新变换意图。"));
		return false;
	}
	if (!bAllowRotation && !bAllowScaling)
	{
		OutFailure = FText::FromString(TEXT("旋转和缩放均未启用。"));
		return false;
	}
	return true;
}

bool UDreamTransformCapability::BuildCommand(AInteractiveAssemblyActor& Target,
	const FDreamInteractionIntent& Intent,
	FDreamInteractionCommand& OutCommand,
	FText& OutFailure) const
{
	TArray<FGuid> TargetNodes;
	if (Intent.TargetNodeId.IsValid())
	{
		TargetNodes.Add(Intent.TargetNodeId);
	}
	else
	{
		// 装配体级能力没有指定节点时，选择所有根节点，内部层级会整体跟随。
		for (const FDreamNodeState& Node : Target.AssemblyState.Nodes)
		{
			if (!Node.ParentNodeId.IsValid() && Node.IsOperational())
			{
				TargetNodes.Add(Node.NodeId);
			}
		}
	}
	if (TargetNodes.IsEmpty())
	{
		OutFailure = FText::FromString(TEXT("变换能力没有找到可操作的节点。"));
		return false;
	}

	float RotationDelta = bAllowRotation ? Intent.RotationDeltaDegrees : 0.0f;
	if (bSnapRotation && RotationStepDegrees > KINDA_SMALL_NUMBER)
	{
		RotationDelta = FMath::GridSnap(RotationDelta, RotationStepDegrees);
	}
	const float ScaleDelta = bAllowScaling ? Intent.ScaleDelta : 0.0f;
	if (bAllowScaling)
	{
		const float CurrentScale = 1.0f;
		const float DesiredScale = CurrentScale + ScaleDelta;
		if (DesiredScale < MinimumScale || DesiredScale > MaximumScale)
		{
			OutFailure = FText::FromString(TEXT("缩放结果超出能力配置的范围。"));
			return false;
		}
	}

	const FVector Axis = Intent.RotationAxis.IsNearlyZero() ? DefaultRotationAxis : Intent.RotationAxis;
	return Target.BuildTransformCommand(TargetNodes, Axis, RotationDelta, ScaleDelta,
		ReferenceFrame, OutCommand, OutFailure);
}

bool UDreamTransformCapability::Validate(const AInteractiveAssemblyActor& Target,
	const FDreamInteractionCommand& Command,
	FText& OutFailure) const
{
	if (!UDreamInteractionCapability::Validate(Target, Command, OutFailure))
	{
		return false;
	}
	if (Command.Type != EDreamCommandType::Transform)
	{
		OutFailure = FText::FromString(TEXT("变换能力只能验证变换命令。"));
		return false;
	}
	for (const FDreamNodeTransformChange& Change : Command.TransformChanges)
	{
		const FVector Scale = Change.NewLocalTransform.GetScale3D();
		if (Scale.GetMin() < MinimumScale - KINDA_SMALL_NUMBER || Scale.GetMax() > MaximumScale + KINDA_SMALL_NUMBER)
		{
			OutFailure = FText::FromString(TEXT("命令中的节点缩放超出能力限制。"));
			return false;
		}
	}
	return true;
}

