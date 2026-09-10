#include "DreamTransformCapability.h"
#include "DreamStateMath.h"
#include "InteractiveAssemblyActor.h"
#include "InteractiveAssemblyDefinition.h"

UDreamTransformCapability::UDreamTransformCapability()
{
	CapabilityId = TEXT("Transform");
	DisplayName = FText::FromString(TEXT("变换"));
	AllowedModes.Add(EDreamInteractionMode::Overview);
}
void UDreamTransformCapability::ValidateConfiguration(
	const UInteractiveAssemblyDefinition& D, TArray<FString>& Errors) const
{
	Super::ValidateConfiguration(D, Errors);
	if (DefaultRotationAxis.ContainsNaN() || DefaultRotationAxis.IsNearlyZero() || MinimumScale <= 0 ||
		MaximumScale < MinimumScale || !FMath::IsFinite(MaximumScale) || !FMath::IsFinite(MinimumScale) ||
		RotationStepDegrees < 0 || TranslationStep < 0 || ScaleStep < 0 || MaximumRotationPerOperation <= 0 ||
		MaximumTranslationPerOperation < 0 || PivotPoint.ContainsNaN() || !CustomFrameRotation.IsNormalized())
		Errors.Add(TEXT("变换能力的轴、步长、范围或 Pivot 配置无效。"));
	if (PivotNodeId.IsValid() && !D.FindNodeDefinition(PivotNodeId))
		Errors.Add(TEXT("Pivot 节点不存在。"));
	if (PivotMode == EDreamPivotMode::Socket)
	{
		const auto* Node = D.FindNodeDefinition(PivotNodeId);
		if (!Node || !Node->Sockets.Contains(PivotSocket))
			Errors.Add(TEXT("Pivot Socket 不存在。"));
	}
}
bool UDreamTransformCapability::BuildResult(
	const FDreamCapabilityContext& C, FDreamInteractionCommand& Command, FText& Failure) const
{
	auto Reject = [&](const TCHAR* Message)
	{
		Failure = FText::FromString(Message);
		return false;
	};
	const auto& I = C.Intent;
	if (C.State.bIsCarried)
		return Reject(TEXT("请先放下携带中的物体。"));
	if (I.Translation.ContainsNaN() || I.RotationAxis.ContainsNaN() || !FMath::IsFinite(I.RotationDeltaDegrees) ||
		!FMath::IsFinite(I.ScaleDelta))
		return Reject(TEXT("输入包含无效数字。"));
	if ((!bAllowRotation && !FMath::IsNearlyZero(I.RotationDeltaDegrees)) ||
		(!bAllowScaling && !FMath::IsNearlyZero(I.ScaleDelta)) || (!bAllowTranslation && !I.Translation.IsNearlyZero()))
		return Reject(TEXT("此能力未启用请求的变换类型。"));
	float Angle =
		RotationStepDegrees > 0 ? FMath::GridSnap(I.RotationDeltaDegrees, RotationStepDegrees) : I.RotationDeltaDegrees;
	float Factor = 1 + (ScaleStep > 0 ? FMath::GridSnap(I.ScaleDelta, ScaleStep) : I.ScaleDelta);
	FVector Translation = TranslationStep > 0 ? I.Translation.GridSnap(TranslationStep) : I.Translation;
	if (FMath::Abs(Angle) > MaximumRotationPerOperation || Translation.Size() > MaximumTranslationPerOperation ||
		Factor <= 0)
		return Reject(TEXT("本次操作超过移动、旋转或缩放范围。"));
	TArray<FGuid> Targets;
	if (!ResolveTargets(C, Targets, Failure))
		return false;
	FTransform Frame = C.State.RootTransform;
	if (ReferenceFrame == EDreamReferenceFrame::World)
		Frame = FTransform::Identity;
	if (ReferenceFrame == EDreamReferenceFrame::Custom)
		Frame = FTransform(CustomFrameRotation);
	if (ReferenceFrame == EDreamReferenceFrame::Node &&
		!DreamState::WorldTransform(C.State, PivotNodeId.IsValid() ? PivotNodeId : Targets[0], Frame))
		return Reject(TEXT("参考节点不存在。"));
	FVector Axis = (bAllowInputAxis && !I.RotationAxis.IsNearlyZero()) ? I.RotationAxis : DefaultRotationAxis;
	Axis = Frame.TransformVectorNoScale(Axis).GetSafeNormal();
	if (Axis.IsNearlyZero())
		return Reject(TEXT("旋转轴不能为零。"));
	FVector Pivot = FVector::ZeroVector;
	if (PivotMode == EDreamPivotMode::SelectionCenter)
	{
		for (const auto& Id : Targets)
		{
			FTransform W;
			DreamState::WorldTransform(C.State, Id, W);
			Pivot += W.GetLocation();
		}
		Pivot /= Targets.Num();
	}
	else if (PivotMode == EDreamPivotMode::ConfiguredPoint)
		Pivot = Frame.TransformPosition(PivotPoint);
	else
	{
		FTransform P;
		if (!DreamState::WorldTransform(C.State, PivotNodeId, P))
			return Reject(TEXT("Pivot 节点不存在。"));
		if (PivotMode == EDreamPivotMode::Socket)
		{
			const auto* D = C.Target.Definition->FindNodeDefinition(PivotNodeId);
			const auto* Socket = D ? D->Sockets.Find(PivotSocket) : nullptr;
			if (!Socket)
				return Reject(TEXT("Pivot Socket 不存在。"));
			P = *Socket * P;
		}
		Pivot = P.GetLocation();
	}
	const FQuat Rotation(Axis, FMath::DegreesToRadians(Angle));
	for (const auto& Id : Targets)
	{
		FTransform Old;
		DreamState::WorldTransform(C.State, Id, Old);
		const auto* Def = C.Target.Definition->FindNodeDefinition(Id);
		const auto* Node = C.State.FindNode(Id);
		// 用逻辑局部缩放与资产默认缩放相比，避免将 Mesh 的长宽比误当成可拾取尺寸。
		const double CurrentScale =
			Id.IsValid() ? Node->LocalTransform.GetScale3D().X / Def->DefaultLocalTransform.GetScale3D().X
						 : C.State.RootTransform.GetScale3D().X;
		if (CurrentScale * Factor < MinimumScale - 0.0001 || CurrentScale * Factor > MaximumScale + 0.0001)
			return Reject(TEXT("目标尺寸超出能力配置范围。"));
		FTransform New(Rotation * Old.GetRotation(),
			Pivot + Rotation.RotateVector(Old.GetLocation() - Pivot) * Factor +
				Frame.TransformVectorNoScale(Translation),
			Old.GetScale3D() * Factor);
		if (!Id.IsValid())
			Command.ResultState.RootTransform = New;
		else
		{
			FTransform Parent = C.State.RootTransform;
			if (Node->ParentNodeId.IsValid())
				DreamState::WorldTransform(C.State, Node->ParentNodeId, Parent);
			else if (Node->RuntimeState == EDreamNodeRuntimeState::Released)
				Parent = FTransform::Identity;
			Command.ResultState.FindNode(Id)->LocalTransform = New.GetRelativeTransform(Parent);
		}
	}
	Command.Type = EDreamCommandType::Transform;
	Command.ReferenceFrame = ReferenceFrame;
	Command.WorldAxis = Axis;
	Command.WorldPivot = Pivot;
	return true;
}
