#include "DreamInteractionCapability.h"

#include "InteractiveAssemblyActor.h"

bool UDreamInteractionCapability::CanStart(const AInteractiveAssemblyActor& Target,
	const FDreamInteractionIntent& Intent,
	EDreamInteractionMode Mode,
	FText& OutFailure) const
{
	if (!bEnabledByDefault)
	{
		OutFailure = FText::FromString(TEXT("该交互能力当前已禁用。"));
		return false;
	}
	if (!SupportsMode(Mode))
	{
		OutFailure = FText::FromString(TEXT("当前交互模式不支持该能力。"));
		return false;
	}
	if (!Target.IsOperational())
	{
		OutFailure = FText::FromString(TEXT("装配体当前不可操作。"));
		return false;
	}
	if (Intent.TargetNodeId.IsValid() && !Target.IsNodeOperational(Intent.TargetNodeId))
	{
		OutFailure = FText::FromString(TEXT("目标节点不存在、已锁定或已销毁。"));
		return false;
	}
	return true;
}

void UDreamInteractionCapability::Begin(AInteractiveAssemblyActor& Target, const FDreamInteractionIntent& Intent)
{
	// 基类没有临时资源；具体能力可以覆盖此函数保存初始状态。
}

bool UDreamInteractionCapability::Preview(AInteractiveAssemblyActor& Target,
	const FDreamInteractionIntent& Intent,
	FText& OutFailure) const
{
	FDreamInteractionCommand PreviewCommand;
	if (!BuildCommand(Target, Intent, PreviewCommand, OutFailure))
	{
		return false;
	}
	return Target.PreviewCommand(PreviewCommand, OutFailure);
}

bool UDreamInteractionCapability::Validate(const AInteractiveAssemblyActor& Target,
	const FDreamInteractionCommand& Command,
	FText& OutFailure) const
{
	if (!Command.IsValid())
	{
		OutFailure = FText::FromString(TEXT("命令数据不完整。"));
		return false;
	}
	if (Command.AssemblyId != Target.GetAssemblyId())
	{
		OutFailure = FText::FromString(TEXT("命令目标装配体不匹配。"));
		return false;
	}
	return true;
}

bool UDreamInteractionCapability::BuildCommand(AInteractiveAssemblyActor& Target,
	const FDreamInteractionIntent& Intent,
	FDreamInteractionCommand& OutCommand,
	FText& OutFailure) const
{
	OutFailure = FText::FromString(TEXT("该能力没有实现命令生成。"));
	return false;
}

void UDreamInteractionCapability::Cancel(AInteractiveAssemblyActor& Target)
{
	Target.CancelPreview();
}

bool UDreamInteractionCapability::SupportsMode(EDreamInteractionMode Mode) const
{
	return AllowedModes.IsEmpty() || AllowedModes.Contains(Mode);
}

