#include "DreamInteractionWorldSubsystem.h"
#include "InteractiveAssemblyActor.h"
#include "InteractiveAssemblyDefinition.h"
#include "DreamInteractionCapability.h"
#include "DreamStateMath.h"
#include "DreamOccupantComponent.h"
#include "DreamInteractionSaveGame.h"
#include "DreamInteractionTags.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/OverlapResult.h"

namespace
{
struct FProxy
{
	FGuid NodeId;
	FVector Center, Extent;
	FQuat Rotation;
};
TArray<FProxy> Proxies(const AInteractiveAssemblyActor& A, const FDreamAssemblyState& S)
{
	TArray<FProxy> Result;
	if (S.bIsCarried)
		return Result;
	for (const auto& N : S.Nodes)
	{
		const auto* D = A.Definition->FindNodeDefinition(N.NodeId);
		if (!D || !D->bCollisionEnabled || !DreamState::IsVisible(S, N.NodeId) ||
			(N.RuntimeState == EDreamNodeRuntimeState::Broken && !D->bCollisionWhenBroken))
			continue;
		FTransform W;
		DreamState::WorldTransform(S, N.NodeId, W);
		Result.Add(
			{N.NodeId, W.TransformPosition(D->CollisionCenter), D->CollisionExtent * W.GetScale3D(), W.GetRotation()});
	}
	return Result;
}
bool SameProxy(const FProxy& A, const FProxy& B)
{
	return A.Center.Equals(B.Center, 0.001) && A.Extent.Equals(B.Extent, 0.001) &&
		   A.Rotation.Equals(B.Rotation, 0.0001);
}
/** OBB 的 15 个分离轴检查；接触不算穿透，留出 0.5 cm 数值容差。 */
bool Intersects(const FProxy& A, const FProxy& B)
{
	FVector AxesA[3] = {A.Rotation.GetAxisX(), A.Rotation.GetAxisY(), A.Rotation.GetAxisZ()};
	FVector AxesB[3] = {B.Rotation.GetAxisX(), B.Rotation.GetAxisY(), B.Rotation.GetAxisZ()};
	auto Separated = [&](FVector Axis)
	{
		if (!Axis.Normalize())
			return false;
		double RA = 0, RB = 0;
		for (int32 I = 0; I < 3; ++I)
		{
			RA += A.Extent[I] * FMath::Abs(Axis.Dot(AxesA[I]));
			RB += B.Extent[I] * FMath::Abs(Axis.Dot(AxesB[I]));
		}
		return FMath::Abs((B.Center - A.Center).Dot(Axis)) >= RA + RB - 0.5;
	};
	for (int32 I = 0; I < 3; ++I)
	{
		if (Separated(AxesA[I]) || Separated(AxesB[I]))
			return false;
		for (int32 J = 0; J < 3; ++J)
			if (Separated(AxesA[I].Cross(AxesB[J])))
				return false;
	}
	return true;
}
bool CapsuleIntersects(const FProxy& B, const FDreamOccupantState& S, double Radius, double HalfHeight)
{
	const FVector Center = B.Rotation.UnrotateVector(S.Transform.GetLocation() - B.Center);
	const FVector Axis = B.Rotation.UnrotateVector(S.Transform.GetRotation().GetAxisZ());
	const double Segment = FMath::Max(0.0, HalfHeight - Radius);
	auto DistSquared = [&](double T)
	{
		const FVector P = Center + Axis * T;
		const FVector Closest(FMath::Clamp(P.X, -B.Extent.X, B.Extent.X), FMath::Clamp(P.Y, -B.Extent.Y, B.Extent.Y),
			FMath::Clamp(P.Z, -B.Extent.Z, B.Extent.Z));
		return FVector::DistSquared(P, Closest);
	};
	// 线段到盒子的距离是凸函数，用三分收敛求最短距离，再与胶囊半径比较。
	double Low = -Segment, High = Segment;
	for (int32 I = 0; I < 40; ++I)
	{
		double L = (2 * Low + High) / 3, R = (Low + 2 * High) / 3;
		if (DistSquared(L) < DistSquared(R))
			High = R;
		else
			Low = L;
	}
	return DistSquared((Low + High) / 2) < FMath::Square(FMath::Max(0.0, Radius - 0.5));
}
} // namespace

bool UDreamInteractionWorldSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
	return Type == EWorldType::Game || Type == EWorldType::PIE;
}
TStatId UDreamInteractionWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamInteractionWorldSubsystem, STATGROUP_Tickables);
}
void UDreamInteractionWorldSubsystem::Tick(float Delta)
{
	for (const auto& Pair : Assemblies)
		if (auto* A = Pair.Value.Get())
		{
			if (A->GetAssemblyState().bIsCarried)
				A->RefreshPresentation();
			A->DrawPreview();
		}
}
void UDreamInteractionWorldSubsystem::Deinitialize()
{
	Sessions.Reset();
	Assemblies.Reset();
	Occupants.Reset();
	History.Reset();
	RedoHistory.Reset();
	Super::Deinitialize();
}
bool UDreamInteractionWorldSubsystem::RegisterAssembly(AInteractiveAssemblyActor* A)
{
	if (!A || !A->IsOperational() || !A->AssemblyId.IsValid())
		return false;
	if (auto* Existing = FindAssembly(A->AssemblyId); Existing && Existing != A)
	{
		UE_LOG(LogDreamInteraction, Error, TEXT("装配体实例 ID 重复：%s，拒绝覆盖。"), *A->AssemblyId.ToString());
		return false;
	}
	Assemblies.Add(A->AssemblyId, A);
	return true;
}
void UDreamInteractionWorldSubsystem::UnregisterAssembly(AInteractiveAssemblyActor* A)
{
	if (!A || FindAssembly(A->AssemblyId) != A)
		return;
	TArray<FGuid> Removed;
	for (const auto& Pair : Sessions)
		if (Pair.Value.AssemblyId == A->AssemblyId)
			Removed.Add(Pair.Key);
	for (const auto& Id : Removed)
		CancelInteraction(Id);
	Assemblies.Remove(A->AssemblyId);
}
AInteractiveAssemblyActor* UDreamInteractionWorldSubsystem::FindAssembly(const FGuid& Id) const
{
	const auto* A = Assemblies.Find(Id);
	return A ? A->Get() : nullptr;
}
void UDreamInteractionWorldSubsystem::GetRegisteredAssemblies(TArray<AInteractiveAssemblyActor*>& Result) const
{
	Result.Reset();
	for (const auto& Pair : Assemblies)
		if (auto* A = Pair.Value.Get())
			Result.Add(A);
}
void UDreamInteractionWorldSubsystem::RegisterOccupant(UDreamOccupantComponent* O)
{
	if (!O)
		return;
	if (auto* Existing = FindOccupant(O->OccupantId); Existing && Existing != O)
	{
		UE_LOG(LogDreamInteraction, Error, TEXT("占用者 ID 重复。"));
		return;
	}
	Occupants.Add(O->OccupantId, O);
}
void UDreamInteractionWorldSubsystem::UnregisterOccupant(UDreamOccupantComponent* O)
{
	if (!O || FindOccupant(O->OccupantId) != O)
		return;
	// 持有者离开时先保存插槽世界姿态，避免被携带物体留在失效参考系中。
	if (auto* A = FindCarriedAssembly(O->OccupantId))
	{
		auto S = A->GetEffectiveState();
		S.bIsCarried = false;
		S.CarrierId.Invalidate();
		++S.StateVersion;
		S.StateTags.RemoveTag(DreamTags::StateCarried);
		A->SetCommittedState(S);
		A->OnStateChanged.Broadcast(S);
	}
	Occupants.Remove(O->OccupantId);
	TArray<FGuid> Removed;
	for (const auto& Pair : Sessions)
		if (Pair.Value.RequesterId == O->OccupantId)
			Removed.Add(Pair.Key);
	for (const auto& Id : Removed)
		CancelInteraction(Id);
}
UDreamOccupantComponent* UDreamInteractionWorldSubsystem::FindOccupant(const FGuid& Id) const
{
	const auto* O = Occupants.Find(Id);
	return O ? O->Get() : nullptr;
}
AInteractiveAssemblyActor* UDreamInteractionWorldSubsystem::FindCarriedAssembly(const FGuid& Id) const
{
	for (const auto& Pair : Assemblies)
		if (auto* A = Pair.Value.Get())
			if (A->GetAssemblyState().bIsCarried && A->GetAssemblyState().CarrierId == Id)
				return A;
	return nullptr;
}
bool UDreamInteractionWorldSubsystem::IsAssemblyLocked(const FGuid& Id) const
{
	for (const auto& Pair : Sessions)
		if (Pair.Value.AssemblyId == Id)
			return true;
	return false;
}
bool UDreamInteractionWorldSubsystem::BeginInteraction(const FGuid& AssemblyId, const FGuid& NodeId, FName CapabilityId,
	EDreamInteractionMode Mode, const FGuid& RequesterId, FGuid& SessionId, FText& Failure)
{
	SessionId.Invalidate();
	auto* A = FindAssembly(AssemblyId);
	auto* Cap = A ? A->FindCapability(CapabilityId, NodeId) : nullptr;
	if (!A || !Cap || IsAssemblyLocked(AssemblyId) || bApplying)
	{
		Failure = FText::FromString(TEXT("目标不存在、能力不可用或装配体正在操作。"));
		return false;
	}
	FDreamInteractionIntent Intent;
	Intent.TargetNodeId = NodeId;
	FDreamCapabilityContext C{*A, A->GetAssemblyState(), Intent, Mode, RequesterId};
	if (!Cap->CanStart(C, Failure))
		return false;
	SessionId = FGuid::NewGuid();
	Sessions.Add(SessionId,
		{SessionId, AssemblyId, NodeId, RequesterId, CapabilityId, Mode, A->GetAssemblyState().StateVersion});
	return true;
}
bool UDreamInteractionWorldSubsystem::PrepareCommand(const FGuid& SessionId, const FDreamInteractionIntent& Input,
	FDreamInteractionCommand& Command, FText& Failure) const
{
	const auto* S = Sessions.Find(SessionId);
	auto* A = S ? FindAssembly(S->AssemblyId) : nullptr;
	auto* Cap = A ? A->FindCapability(S->CapabilityId, S->NodeId) : nullptr;
	if (!S || !A || !Cap || A->GetAssemblyState().StateVersion != S->Version)
	{
		Failure = FText::FromString(TEXT("交互会话失效或目标状态已经变化。"));
		return false;
	}
	FDreamInteractionIntent Intent = Input;
	Intent.TargetNodeId = S->NodeId;
	Command = FDreamInteractionCommand();
	Command.CommandId = FGuid::NewGuid();
	Command.SessionId = SessionId;
	Command.AssemblyId = S->AssemblyId;
	Command.RequesterId = S->RequesterId;
	Command.CapabilityId = S->CapabilityId;
	Command.Mode = S->Mode;
	Command.Intent = Intent;
	Command.ExpectedStateVersion = S->Version;
	Command.ResultState = A->GetAssemblyState();
	FDreamCapabilityContext C{*A, A->GetAssemblyState(), Intent, S->Mode, S->RequesterId};
	return Cap->CanStart(C, Failure) && Cap->BuildResult(C, Command, Failure) &&
		   A->ValidateState(Command.ResultState, Failure);
}
bool UDreamInteractionWorldSubsystem::VerifyCommand(const FDreamInteractionCommand& Command, FText& Failure) const
{
	const auto* S = Sessions.Find(Command.SessionId);
	if (!Command.IsValid() || !S || Command.AssemblyId != S->AssemblyId || Command.CapabilityId != S->CapabilityId ||
		Command.ExpectedStateVersion != S->Version || Command.RequesterId != S->RequesterId || Command.Mode != S->Mode)
	{
		Failure = FText::FromString(TEXT("命令与会话所有权或版本不匹配。"));
		return false;
	}
	FDreamInteractionCommand Expected;
	if (!PrepareCommand(Command.SessionId, Command.Intent, Expected, Failure))
		return false;
	if (!DreamState::Equivalent(Expected.ResultState, Command.ResultState) || Expected.Type != Command.Type ||
		Expected.ReferenceFrame != Command.ReferenceFrame || !Expected.WorldPivot.Equals(Command.WorldPivot) ||
		!Expected.WorldAxis.Equals(Command.WorldAxis))
	{
		Failure = FText::FromString(TEXT("命令结果与能力规则重新计算的结果不一致。"));
		return false;
	}
	return true;
}
void UDreamInteractionWorldSubsystem::CancelInteraction(const FGuid& SessionId)
{
	if (const auto* S = Sessions.Find(SessionId))
		if (auto* A = FindAssembly(S->AssemblyId))
			A->CancelPreview();
	Sessions.Remove(SessionId);
}
bool UDreamInteractionWorldSubsystem::BuildParticipants(
	const FDreamInteractionCommand& Command, FDreamInteractionTransaction& T, FText& Failure) const
{
	auto* A = FindAssembly(Command.AssemblyId);
	if (!A)
		return false;
	auto* Cap = A->FindCapability(Command.CapabilityId, Command.Intent.TargetNodeId);
	if (!Cap)
		return false;
	T.TransactionId = FGuid::NewGuid();
	T.Command = Command;
	T.BeforeState = A->GetAssemblyState();
	T.AfterState = Command.ResultState;
	T.AfterState.StateVersion = T.BeforeState.StateVersion + 1;
	const auto Policy = Cap->bOverrideOccupantPolicy ? Cap->OccupantPolicy : A->Definition->DefaultOccupantPolicy;
	for (const auto& Pair : Occupants)
	{
		auto* O = Pair.Value.Get();
		if (!O)
			continue;
		auto Old = O->CaptureState();
		auto New = Old;
		FVector Gravity;
		FGuid Source, NodeId;
		ResolveGravityAtLocation(Old.Transform.GetLocation(), Gravity, &Source, &NodeId);
		if (Source != Command.AssemblyId)
			continue;
		FTransform OldFrame, NewFrame;
		DreamState::WorldTransform(T.BeforeState, NodeId, OldFrame);
		DreamState::WorldTransform(T.AfterState, NodeId, NewFrame);
		if (OldFrame.Equals(NewFrame, 0.001))
			continue;
		if (Policy == EDreamOccupantPolicy::FailOperation)
		{
			Failure = FText::FromString(TEXT("受影响空间内存在占用者，此能力不允许操作。"));
			return false;
		}
		if (Policy == EDreamOccupantPolicy::FollowAssembly)
		{
			const FQuat Delta = NewFrame.GetRotation() * OldFrame.GetRotation().Inverse();
			New.Transform.SetLocation(
				NewFrame.TransformPosition(OldFrame.InverseTransformPosition(Old.Transform.GetLocation())));
			New.Transform.SetRotation(Delta * Old.Transform.GetRotation());
			New.Velocity = Delta.RotateVector(Old.Velocity);
			New.GravityDirection = Delta.RotateVector(Old.GravityDirection);
			New.ControlRotation = Delta * Old.ControlRotation;
		}
		T.BeforeOccupants.Add(Old);
		T.AfterOccupants.Add(New);
	}
	return !Cap->bRejectCollisions || ValidateSpatial(*A, T.BeforeState, T.AfterState, T.AfterOccupants, Failure);
}

bool UDreamInteractionWorldSubsystem::ValidateSpatial(const AInteractiveAssemblyActor& A,
	const FDreamAssemblyState& Before, const FDreamAssemblyState& After,
	const TArray<FDreamOccupantState>& ChangedOccupants, FText& Failure) const
{
	const auto Boxes = Proxies(A, After);
	const auto OldBoxes = Proxies(A, Before);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DreamTransactionCollision), false);
	Params.AddIgnoredActor(&A);
	for (const auto& Pair : Assemblies)
		if (auto* Other = Pair.Value.Get())
			Params.AddIgnoredActor(Other);
	for (const auto& Pair : Occupants)
		if (auto* O = Pair.Value.Get())
			Params.AddIgnoredActor(O->GetOwner());
	for (int32 I = 0; I < Boxes.Num(); ++I)
	{
		const auto& Box = Boxes[I];
		const auto* Old = OldBoxes.FindByPredicate([&](const auto& B) { return B.NodeId == Box.NodeId; });
		if (Old && SameProxy(*Old, Box))
			continue;
		// 与不属于本运行时的地形/静态关卡碰撞；自身及其他装配体使用下面的逻辑 OBB 检查。
		if (GetWorld()->OverlapBlockingTestByChannel(Box.Center, Box.Rotation, ECC_WorldDynamic,
				FCollisionShape::MakeBox((Box.Extent - FVector(0.5)).ComponentMax(FVector(0.01))), Params))
		{
			Failure = FText::FromString(TEXT("目标位置与关卡碰撞阻挡重叠。"));
			return false;
		}
		for (int32 J = 0; J < Boxes.Num(); ++J)
			if (I != J && Intersects(Box, Boxes[J]))
			{
				Failure = FText::FromString(TEXT("部件目标位置发生非法穿透。"));
				return false;
			}
		for (const auto& Pair : Assemblies)
		{
			auto* Other = Pair.Value.Get();
			if (!Other || Other == &A)
				continue;
			for (const auto& OtherBox : Proxies(*Other, Other->GetEffectiveState()))
				if (Intersects(Box, OtherBox))
				{
					Failure = FText::FromString(TEXT("目标位置被其他装配体占据。"));
					return false;
				}
		}
	}
	// 检查所有已注册玩家，既包括跟随者也包括保持世界位置的外部玩家。
	for (const auto& Pair : Occupants)
	{
		const auto* O = Pair.Value.Get();
		if (!O)
			continue;
		const auto* Character = Cast<ACharacter>(O->GetOwner());
		if (!Character)
			continue;
		const auto* Changed = ChangedOccupants.FindByPredicate([&](const auto& S) { return S.OccupantId == Pair.Key; });
		const auto State = Changed ? *Changed : O->CaptureState();
		const double Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const double Height = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		for (const auto& Box : Boxes)
			if (CapsuleIntersects(Box, State, Radius, Height))
			{
				Failure = FText::FromString(TEXT("操作后玩家会被部件挤入碰撞体。"));
				return false;
			}
		if (Changed)
		{
			FCollisionQueryParams PawnParams(SCENE_QUERY_STAT(DreamOccupantCollision), false);
			PawnParams.AddIgnoredActor(&A);
			PawnParams.AddIgnoredActor(Character);
			if (auto* Carried = FindCarriedAssembly(Pair.Key))
				PawnParams.AddIgnoredActor(Carried);
			if (GetWorld()->OverlapBlockingTestByChannel(State.Transform.GetLocation(), State.Transform.GetRotation(),
					ECC_Pawn, FCollisionShape::MakeCapsule(Radius - 0.5, Height - 0.5), PawnParams))
			{
				Failure = FText::FromString(TEXT("玩家跟随后的目标位置被世界阻挡。"));
				return false;
			}
		}
	}
	return true;
}

bool UDreamInteractionWorldSubsystem::PreviewCommand(const FDreamInteractionCommand& Command, FText& Failure)
{
	FDreamInteractionTransaction T;
	if (!VerifyCommand(Command, Failure) || !BuildParticipants(Command, T, Failure))
	{
		if (auto* A = FindAssembly(Command.AssemblyId))
			A->CancelPreview();
		return false;
	}
	FindAssembly(Command.AssemblyId)->ShowPreview(T.AfterState);
	return true;
}
bool UDreamInteractionWorldSubsystem::ExecuteCommand(
	const FDreamInteractionCommand& Command, FDreamInteractionTransaction* Out, FText& Failure)
{
	if (bApplying)
	{
		Failure = FText::FromString(TEXT("事务广播期间不能重入提交。"));
		return false;
	}
	FDreamInteractionTransaction T;
	if (!VerifyCommand(Command, Failure) || !BuildParticipants(Command, T, Failure))
	{
		if (auto* A = FindAssembly(Command.AssemblyId))
			A->CancelPreview();
		return false;
	}
	auto* A = FindAssembly(Command.AssemblyId);
	if (DreamState::Equivalent(T.BeforeState, T.AfterState))
	{
		Failure = FText::FromString(TEXT("操作没有产生状态变化。"));
		return false;
	}
	TGuardValue<bool> Guard(bApplying, true);
	// 所有可能失败的检查已在写入前完成；以下同步写入没有部分失败路径。
	A->SetCommittedState(T.AfterState);
	for (const auto& S : T.AfterOccupants)
		FindOccupant(S.OccupantId)->RestoreState(S);
	CancelInteraction(Command.SessionId);
	History.Add(T);
	RedoHistory.Reset();
	if (History.Num() > 128)
		History.RemoveAt(0);
	if (Out)
		*Out = T;
	A->OnStateChanged.Broadcast(T.AfterState);
	OnTransactionCommitted.Broadcast(T);
	return true;
}
bool UDreamInteractionWorldSubsystem::UndoLastTransaction(const FGuid& Id, FText& Failure)
{
	return RestoreHistory(false, Id, Failure);
}
bool UDreamInteractionWorldSubsystem::RedoLastTransaction(const FGuid& Id, FText& Failure)
{
	return RestoreHistory(true, Id, Failure);
}
bool UDreamInteractionWorldSubsystem::RestoreHistory(bool bRedo, const FGuid& Id, FText& Failure)
{
	auto* A = FindAssembly(Id);
	if (!A || bApplying || IsAssemblyLocked(Id))
	{
		Failure = FText::FromString(TEXT("请先结束当前交互再撤销或重做。"));
		return false;
	}
	auto& From = bRedo ? RedoHistory : History;
	auto& To = bRedo ? History : RedoHistory;
	const int32 Index = From.FindLastByPredicate([&](const auto& T) { return T.Command.AssemblyId == Id; });
	if (Index == INDEX_NONE)
	{
		Failure = FText::FromString(TEXT("没有可用的历史操作。"));
		return false;
	}
	const auto T = From[Index];
	const auto& Expected = bRedo ? T.BeforeState : T.AfterState;
	auto Restore = bRedo ? T.AfterState : T.BeforeState;
	const auto& People = bRedo ? T.AfterOccupants : T.BeforeOccupants;
	if (!DreamState::Equivalent(A->GetAssemblyState(), Expected))
	{
		Failure = FText::FromString(TEXT("当前状态与历史不一致。"));
		return false;
	}
	for (const auto& S : People)
		if (!FindOccupant(S.OccupantId))
		{
			Failure = FText::FromString(TEXT("历史中的占用者已离开世界。"));
			return false;
		}
	if (Restore.bIsCarried && !FindOccupant(Restore.CarrierId))
	{
		Failure = FText::FromString(TEXT("历史中的持有者不存在。"));
		return false;
	}
	Restore.StateVersion = A->GetAssemblyState().StateVersion + 1;
	if (!A->ValidateState(Restore, Failure) || !ValidateSpatial(*A, A->GetAssemblyState(), Restore, People, Failure))
		return false;
	TGuardValue<bool> Guard(bApplying, true);
	A->SetCommittedState(Restore);
	for (const auto& S : People)
		FindOccupant(S.OccupantId)->RestoreState(S);
	From.RemoveAt(Index);
	To.Add(T);
	A->OnStateChanged.Broadcast(Restore);
	FDreamInteractionTransaction Event = T;
	Event.BeforeState = Expected;
	Event.AfterState = Restore;
	OnTransactionCommitted.Broadcast(Event);
	return true;
}

bool UDreamInteractionWorldSubsystem::ResolveComponentTarget(
	const UPrimitiveComponent* Hit, AInteractiveAssemblyActor*& Assembly, FGuid& NodeId) const
{
	Assembly = nullptr;
	NodeId.Invalidate();
	if (!Hit)
		return false;
	auto* A = Cast<AInteractiveAssemblyActor>(Hit->GetOwner());
	if (!A || FindAssembly(A->AssemblyId) != A || !A->FindNodeId(Hit, NodeId))
		return false;
	const auto* D = A->Definition->FindNodeDefinition(NodeId);
	if (!D || !D->bSelectable || !A->IsNodeOperational(NodeId))
		return false;
	Assembly = A;
	return true;
}
bool UDreamInteractionWorldSubsystem::ResolveGravityAtLocation(
	const FVector& Location, FVector& Direction, FGuid* OutAssembly, FGuid* OutNode) const
{
	Direction = FVector::DownVector;
	if (OutAssembly)
		OutAssembly->Invalidate();
	if (OutNode)
		OutNode->Invalidate();
	int32 BestPriority = MIN_int32;
	double BestVolume = TNumericLimits<double>::Max();
	FString BestKey;
	bool bFound = false;
	auto Consider =
		[&](const FDreamGravitySettings& G, const FTransform& Frame, const FGuid& AssemblyId, const FGuid& NodeId)
	{
		if (!G.bEnabled)
			return;
		const FVector P = Frame.InverseTransformPosition(Location) - G.Center;
		if (FMath::Abs(P.X) > G.Extent.X || FMath::Abs(P.Y) > G.Extent.Y || FMath::Abs(P.Z) > G.Extent.Z)
			return;
		const double Volume =
			(G.Extent * Frame.GetScale3D()).X * (G.Extent * Frame.GetScale3D()).Y * (G.Extent * Frame.GetScale3D()).Z;
		const FString Key = AssemblyId.ToString() + NodeId.ToString();
		// 同优先级选更小的局部体积，仍相等按稳定 ID 排序，避免 TMap 遍历顺序导致闪烁。
		if (!bFound || G.Priority > BestPriority ||
			(G.Priority == BestPriority && (Volume < BestVolume || (Volume == BestVolume && Key < BestKey))))
		{
			BestPriority = G.Priority;
			BestVolume = Volume;
			BestKey = Key;
			bFound = true;
			Direction = Frame.TransformVectorNoScale(G.LocalDirection).GetSafeNormal();
			if (OutAssembly)
				*OutAssembly = AssemblyId;
			if (OutNode)
				*OutNode = NodeId;
		}
	};
	for (const auto& Pair : Assemblies)
	{
		auto* A = Pair.Value.Get();
		if (!A || A->GetAssemblyState().bIsCarried)
			continue;
		const auto& S = A->GetAssemblyState();
		Consider(A->Definition->Gravity, S.RootTransform, Pair.Key, FGuid());
		for (const auto& N : S.Nodes)
			if (DreamState::IsVisible(S, N.NodeId))
			{
				FTransform Frame;
				DreamState::WorldTransform(S, N.NodeId, Frame);
				Consider(A->Definition->FindNodeDefinition(N.NodeId)->Gravity, Frame, Pair.Key, N.NodeId);
			}
	}
	return bFound;
}

void UDreamInteractionWorldSubsystem::CaptureToSaveGame(UDreamInteractionSaveGame& Save) const
{
	Save.SaveVersion = 1;
	Save.MapPackage = UGameplayStatics::GetCurrentLevelName(this, true);
	Save.SavedAtUtc = FDateTime::UtcNow();
	Save.AssemblyStates.Reset();
	Save.OccupantStates.Reset();
	for (const auto& Pair : Assemblies)
		if (auto* A = Pair.Value.Get())
			Save.AssemblyStates.Add(A->GetAssemblyState());
	for (const auto& Pair : Occupants)
		if (auto* O = Pair.Value.Get())
			Save.OccupantStates.Add(O->CaptureState());
}
bool UDreamInteractionWorldSubsystem::RestoreFromSaveGame(
	const UDreamInteractionSaveGame& Save, TArray<FGuid>& Restored, FText& Failure)
{
	Restored.Reset();
	if (bApplying || !Sessions.IsEmpty())
	{
		Failure = FText::FromString(TEXT("请先结束交互再读取存档。"));
		return false;
	}
	if (Save.SaveVersion != 1 || Save.MapPackage != UGameplayStatics::GetCurrentLevelName(this, true))
	{
		Failure = FText::FromString(TEXT("存档版本或地图不匹配。"));
		return false;
	}
	TArray<AInteractiveAssemblyActor*> Current;
	GetRegisteredAssemblies(Current);
	if (Save.AssemblyStates.Num() != Current.Num())
	{
		Failure = FText::FromString(TEXT("存档中的装配体集合与当前关卡不匹配。"));
		return false;
	}
	int32 OccupantCount = 0;
	for (const auto& Pair : Occupants)
		if (Pair.Value.IsValid())
			++OccupantCount;
	if (Save.OccupantStates.Num() != OccupantCount)
	{
		Failure = FText::FromString(TEXT("存档中的占用者集合与当前关卡不匹配。"));
		return false;
	}
	TSet<FGuid> Seen;
	TSet<FGuid> Carriers;
	for (const auto& S : Save.AssemblyStates)
	{
		auto* A = FindAssembly(S.AssemblyId);
		if (!A || Seen.Contains(S.AssemblyId) || !A->ValidateState(S, Failure))
		{
			if (Failure.IsEmpty())
				Failure = FText::FromString(TEXT("存档包含缺失或重复的装配体。"));
			return false;
		}
		Seen.Add(S.AssemblyId);
		if (S.bIsCarried && !FindOccupant(S.CarrierId))
		{
			Failure = FText::FromString(TEXT("存档的持有者尚未生成。"));
			return false;
		}
		if (S.bIsCarried)
		{
			if (Carriers.Contains(S.CarrierId))
			{
				Failure = FText::FromString(TEXT("同一持有者的存档携带了多个物体。"));
				return false;
			}
			Carriers.Add(S.CarrierId);
		}
	}
	Seen.Reset();
	for (const auto& S : Save.OccupantStates)
	{
		if (!FindOccupant(S.OccupantId) || Seen.Contains(S.OccupantId) || !DreamState::IsFinitePositive(S.Transform) ||
			S.Velocity.ContainsNaN() || !S.GravityDirection.IsNormalized() || !S.ControlRotation.IsNormalized() ||
			S.MovementMode > 6)
		{
			Failure = FText::FromString(TEXT("存档占用者状态缺失、重复或损坏。"));
			return false;
		}
		Seen.Add(S.OccupantId);
	}
	// 先验证整个快照再写入；任一对象校验失败都不会恢复半个世界。
	TGuardValue<bool> Guard(bApplying, true);
	for (auto S : Save.AssemblyStates)
	{
		auto* A = FindAssembly(S.AssemblyId);
		S.StateVersion = FMath::Max(S.StateVersion, A->GetAssemblyState().StateVersion) + 1;
		A->SetCommittedState(S);
		Restored.Add(S.AssemblyId);
	}
	for (const auto& S : Save.OccupantStates)
		FindOccupant(S.OccupantId)->RestoreState(S);
	for (auto* A : Current)
		A->RefreshPresentation();
	History.Reset();
	RedoHistory.Reset();
	for (auto* A : Current)
		A->OnStateChanged.Broadcast(A->GetAssemblyState());
	return true;
}
bool UDreamInteractionWorldSubsystem::SaveToSlot(const FString& Slot, FText& Failure) const
{
	auto* Save = NewObject<UDreamInteractionSaveGame>();
	CaptureToSaveGame(*Save);
	if (!UGameplayStatics::SaveGameToSlot(Save, Slot, 0))
	{
		Failure = FText::FromString(TEXT("写入存档失败。"));
		return false;
	}
	return true;
}
bool UDreamInteractionWorldSubsystem::LoadFromSlot(const FString& Slot, FText& Failure)
{
	const auto* Save = Cast<UDreamInteractionSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	if (!Save)
	{
		Failure = FText::FromString(TEXT("未找到有效的交互存档。"));
		return false;
	}
	TArray<FGuid> Restored;
	return RestoreFromSaveGame(*Save, Restored, Failure);
}
