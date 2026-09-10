#include "DreamSequenceCapability.h"
UDreamSequenceCapability::UDreamSequenceCapability()
{
	CapabilityId = TEXT("Sequence");
	DisplayName = FText::FromString(TEXT("组合操作"));
}
bool UDreamSequenceCapability::BuildResult(
	const FDreamCapabilityContext& C, FDreamInteractionCommand& Command, FText& Failure) const
{
	// 禁止递归引用序列，第一版用平坦步骤覆盖条件标签、变换和状态转移的组合。
	if (Steps.IsEmpty())
	{
		Failure = FText::FromString(TEXT("组合能力没有步骤。"));
		return false;
	}
	for (const auto& Step : Steps)
	{
		if (!Step || Step->IsA<UDreamSequenceCapability>())
		{
			Failure = FText::FromString(TEXT("序列步骤为空或包含嵌套序列。"));
			return false;
		}
		const FDreamAssemblyState Intermediate = Command.ResultState;
		FDreamCapabilityContext Next{C.Target, Intermediate, C.Intent, C.Mode, C.RequesterId};
		if (!Step->CanStart(Next, Failure) || !Step->BuildResult(Next, Command, Failure))
			return false;
	}
	Command.Type = EDreamCommandType::Composite;
	return true;
}
void UDreamSequenceCapability::ValidateConfiguration(
	const UInteractiveAssemblyDefinition& D, TArray<FString>& Errors) const
{
	Super::ValidateConfiguration(D, Errors);
	if (Steps.IsEmpty())
		Errors.Add(TEXT("序列至少需要一个步骤。"));
	for (const auto& Step : Steps)
		if (!Step || Step->IsA<UDreamSequenceCapability>())
			Errors.Add(TEXT("序列不能包含空步骤或嵌套序列。"));
		else
			Step->ValidateConfiguration(D, Errors);
}
