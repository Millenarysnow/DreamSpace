#include "DreamRubiksCubeComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DreamSpace.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
/** 贴纸颜色按 +X/-X、+Y/-Y、+Z/-Z 排列；相对面采用红/橙、绿/蓝、白/黄。 */
const FLinearColor FaceColors[6] = {FLinearColor(0.8f, 0.015f, 0.02f), FLinearColor(1.0f, 0.22f, 0.01f),
	FLinearColor(0.015f, 0.55f, 0.06f), FLinearColor(0.015f, 0.07f, 0.8f), FLinearColor(0.95f, 0.95f, 0.95f),
	FLinearColor(1.0f, 0.8f, 0.015f)};

/** 每个魔方独立标记自己的生成件，不会在重建时删掉同一 Actor 上其它组件的模型。 */
FName VisualTag(const UDreamRubiksCubeComponent* Cube)
{
	return FName(*FString::Printf(TEXT("DreamRubiksVisual_%s"), *Cube->GetName()));
}

/** 有符号整数基向量对应一张外侧面的索引。 */
int32 FaceIndex(const FIntVector& Normal)
{
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Normal[Axis] != 0)
			return Axis * 2 + (Normal[Axis] < 0 ? 1 : 0);
	}
	return INDEX_NONE;
}
} // namespace

UDreamRubiksCubeComponent::UDreamRubiksCubeComponent()
{
	// 构造时持有硬引用，确保运行时生成的默认模型和颜色材质也被打包烘焙。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BodyMesh = CubeMesh.Object;
	StickerMesh = CubeMesh.Object;
	ColorMaterial = ShapeMaterial.Object;
	CornerComponents.SetNum(8);
}

void UDreamRubiksCubeComponent::BeginPlay()
{
	Super::BeginPlay();
	// 编辑器预览在 PIE 中不作为运行时状态继承；始终从当前配置建立合法的还原题面。
	if (RebuildCube() && bShuffleOnBeginPlay)
		ShuffleCube(InitialShuffleMoves, InitialShuffleSeed);
}

void UDreamRubiksCubeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AbortTurn();
	Super::EndPlay(EndPlayReason);
}

void UDreamRubiksCubeComponent::Deactivate()
{
	// 停用会停止 Tick，不能留下尚未提交的半圈模型；取消视觉动画并恢复上一步完整状态。
	AbortTurn();
	Super::Deactivate();
}

void UDreamRubiksCubeComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// 删除组件时连同自动生成的角块和碰撞一起清理；自定义角块由用户的 Actor 管理。
	EndDrag();
	AbortTurn();
	if (!bDestroyingHierarchy)
		DestroyGeneratedVisuals();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool UDreamRubiksCubeComponent::IsConfigurationValid() const
{
	const AActor* Owner = GetOwner();
	const USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	if (!IsValid(Owner) || !IsValid(Root) || Root->Mobility != EComponentMobility::Movable)
		return false;
	const FVector Scale = Root->GetComponentScale();
	// 镜像或零缩放会反转旋转方向/破坏射线反变换，明确拒绝而不偷偷产生错误操作。
	if (Root->GetComponentTransform().ContainsNaN() || Scale.X <= UE_SMALL_NUMBER || Scale.Y <= UE_SMALL_NUMBER ||
		Scale.Z <= UE_SMALL_NUMBER || CubeCenter.ContainsNaN() || !FMath::IsFinite(CellSize) || CellSize < 1.0f ||
		!FMath::IsFinite(Gap) || Gap < 0.0f || !FMath::IsFinite(StickerMargin) || !FMath::IsFinite(TurnDuration) ||
		TurnDuration < 0.0f || !FMath::IsFinite(DragThreshold) || DragThreshold <= 0.0f)
		return false;
	return true;
}

bool UDreamRubiksCubeComponent::AreCornerRootsValid() const
{
	const AActor* Owner = GetOwner();
	const USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	if (CornerRoots.Num() != 8 || InitialCornerTransforms.Num() != 8 || !IsValid(Root))
		return false;
	for (const USceneComponent* Corner : CornerRoots)
	{
		if (!IsValid(Corner) || Corner->GetOwner() != Owner || Corner->GetAttachParent() != Root ||
			Corner->Mobility != EComponentMobility::Movable || Corner->IsUsingAbsoluteLocation() ||
			Corner->IsUsingAbsoluteRotation() || Corner->IsUsingAbsoluteScale())
			return false;
	}
	return true;
}

bool UDreamRubiksCubeComponent::EnsureInitialized()
{
	if (bInitialized)
		return IsConfigurationValid() && AreCornerRootsValid();
	return RebuildCube();
}

bool UDreamRubiksCubeComponent::RebuildCube()
{
	if (IsDragging() || bTurning || bNotifying || !IsConfigurationValid())
		return false;
	// 自定义角块在运行时已经被转动时，先回到上次记录的初始姿态，避免把打乱状态当成新零点。
	if (bInitialized && AreCornerRootsValid())
	{
		for (int32 Index = 0; Index < 8; ++Index)
			CornerRoots[Index]->SetRelativeTransform(InitialCornerTransforms[Index]);
	}
	bInitialized = false;
	DestroyGeneratedVisuals();
	CornerRoots.Reset();
	InitialCornerTransforms.Reset();
	MoveHistory.Reset();
	if (!BuildVisuals())
	{
		DestroyGeneratedVisuals();
		CornerRoots.Reset();
		UE_LOG(LogDreamSpace, Warning,
			TEXT("二阶魔方 [%s] 初始化失败：根需要 Movable、正缩放；默认网格/材质须有效，或绑定八个独立角块。"),
			*GetName());
		return false;
	}
	for (USceneComponent* Corner : CornerRoots)
		InitialCornerTransforms.Add(Corner->GetRelativeTransform());
	InitializeSolvedState();
	bInitialized = true;
	return true;
}

void UDreamRubiksCubeComponent::InitializeSolvedState()
{
	Corners.SetNum(8);
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Corners[Index] = FCornerState();
		Corners[Index].Coordinate = HomeCoordinate(Index);
	}
	bSolved = true;
}

FIntVector UDreamRubiksCubeComponent::HomeCoordinate(int32 Index)
{
	return FIntVector((Index & 4) != 0 ? 1 : -1, (Index & 2) != 0 ? 1 : -1, (Index & 1) != 0 ? 1 : -1);
}

FVector UDreamRubiksCubeComponent::AxisVector(EDreamRubiksCubeAxis Axis)
{
	FVector AxisDirection = FVector::ZeroVector;
	AxisDirection[static_cast<int32>(Axis)] = 1.0;
	return AxisDirection;
}

FIntVector UDreamRubiksCubeComponent::RotateQuarter(
	const FIntVector& Vector, EDreamRubiksCubeAxis Axis, int32 Direction)
{
	// 90° 旋转只交换整数分量和符号；同时用于角块位置与三个朝向基向量，永远不需要 Round 浮点数。
	switch (Axis)
	{
	case EDreamRubiksCubeAxis::X:
		return FIntVector(Vector.X, -Direction * Vector.Z, Direction * Vector.Y);
	case EDreamRubiksCubeAxis::Y:
		return FIntVector(Direction * Vector.Z, Vector.Y, -Direction * Vector.X);
	case EDreamRubiksCubeAxis::Z:
		return FIntVector(-Direction * Vector.Y, Direction * Vector.X, Vector.Z);
	default:
		return Vector;
	}
}

bool UDreamRubiksCubeComponent::BuildVisuals()
{
	AActor* Owner = GetOwner();
	USceneComponent* Root = Owner->GetRootComponent();
	if (!bGenerateVisuals)
	{
		if (CornerComponents.Num() != 8)
			return false;
		TSet<USceneComponent*> UniqueCorners;
		for (const FComponentReference& Reference : CornerComponents)
		{
			USceneComponent* Corner = Cast<USceneComponent>(Reference.GetComponent(Owner));
			if (!IsValid(Corner) || Corner == Root || Corner->GetOwner() != Owner ||
				Corner->GetAttachParent() != Root || UniqueCorners.Contains(Corner) ||
				Corner->Mobility != EComponentMobility::Movable || Corner->IsUsingAbsoluteLocation() ||
				Corner->IsUsingAbsoluteRotation() || Corner->IsUsingAbsoluteScale() ||
				Corner->GetRelativeTransform().ContainsNaN())
				return false;
			UniqueCorners.Add(Corner);
			// 自定义角块可以由多个子网格组成，但它们不能自行模拟物理或保持绝对世界姿态。
			TArray<USceneComponent*> Children;
			Corner->GetChildrenComponents(true, Children);
			Children.Add(Corner);
			for (const USceneComponent* Child : Children)
			{
				if (Child->Mobility != EComponentMobility::Movable || Child->IsUsingAbsoluteLocation() ||
					Child->IsUsingAbsoluteRotation() || Child->IsUsingAbsoluteScale())
					return false;
				if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Child))
					if (Primitive->IsSimulatingPhysics())
						return false;
			}
			// 模型位置应与绑定身份的三个正/负半空间一致，防止错误顺序导致拖错层。
			const FVector Offset = Corner->GetRelativeLocation() - CubeCenter;
			const FIntVector Home = HomeCoordinate(CornerRoots.Num());
			for (int32 Axis = 0; Axis < 3; ++Axis)
				if (Offset[Axis] * Home[Axis] <= UE_SMALL_NUMBER)
					return false;
			CornerRoots.Add(Corner);
		}
		return true;
	}
	if (!IsValid(BodyMesh) || !IsValid(StickerMesh) || !IsValid(ColorMaterial))
		return false;
	const FVector BodySize = BodyMesh->GetBounds().BoxExtent * 2.0;
	if (BodySize.GetMin() <= UE_SMALL_NUMBER || BodySize.ContainsNaN())
		return false;

	// 只创建七个 MID：黑色主体共用一个，六种贴纸颜色各共用一个，避免每张贴纸独立分配材质。
	TArray<UMaterialInstanceDynamic*> Materials;
	for (int32 ColorIndex = 0; ColorIndex < 7; ++ColorIndex)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(ColorMaterial, this);
		Material->SetVectorParameterValue(
			ColorParameterName, ColorIndex == 6 ? FLinearColor(0.008f, 0.008f, 0.008f) : FaceColors[ColorIndex]);
		Materials.Add(Material);
	}
	const FName Tag = VisualTag(this);
	const float HalfSpacing = (CellSize + Gap) * 0.5f;
	const float Margin = FMath::Clamp(StickerMargin, 0.0f, CellSize * 0.45f);
	const float StickerWidth = CellSize - 2.0f * Margin;
	const float StickerThickness = FMath::Max(0.2f, CellSize * 0.008f);

	for (int32 Index = 0; Index < 8; ++Index)
	{
		USceneComponent* Corner = NewObject<USceneComponent>(Owner,
			MakeUniqueObjectName(
				Owner, USceneComponent::StaticClass(), FName(*FString::Printf(TEXT("RubiksCorner_%d"), Index))),
			RF_Transient | RF_DuplicateTransient);
		Corner->SetMobility(EComponentMobility::Movable);
		Corner->SetupAttachment(Root);
		Corner->SetRelativeLocation(CubeCenter + FVector(HomeCoordinate(Index)) * HalfSpacing);
		Corner->ComponentTags.Add(Tag);
		Owner->AddInstanceComponent(Corner);
		Corner->RegisterComponent();
		GeneratedComponents.Add(Corner);
		CornerRoots.Add(Corner);

		UStaticMeshComponent* Body =
			NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient | RF_DuplicateTransient);
		Body->SetMobility(EComponentMobility::Movable);
		Body->SetupAttachment(Corner);
		Body->SetStaticMesh(BodyMesh);
		Body->SetRelativeScale3D(FVector(CellSize) / BodySize);
		// 兼容原点偏离 Bounds 中心的主体网格，缩放后仍以角块根为几何中心。
		Body->SetRelativeLocation(-BodyMesh->GetBounds().Origin * Body->GetRelativeScale3D());
		Body->SetCollisionProfileName(TEXT("BlockAllDynamic"));
		Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Body->SetGenerateOverlapEvents(false);
		Body->ComponentTags.Add(Tag);
		for (int32 Slot = 0; Slot < FMath::Max(1, Body->GetNumMaterials()); ++Slot)
			Body->SetMaterial(Slot, Materials[6]);
		Owner->AddInstanceComponent(Body);
		Body->RegisterComponent();
		GeneratedComponents.Add(Body);

		// 每个角块只生成朝外的三张贴纸，内部面保留黑色；贴纸随角块根一起公转和自转。
		const FIntVector Home = HomeCoordinate(Index);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			UStaticMeshComponent* Sticker =
				NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient | RF_DuplicateTransient);
			Sticker->SetMobility(EComponentMobility::Movable);
			Sticker->SetupAttachment(Corner);
			Sticker->SetStaticMesh(StickerMesh);
			FVector Size(StickerWidth);
			Size[Axis] = StickerThickness;
			FVector Position = FVector::ZeroVector;
			Position[Axis] = Home[Axis] * (CellSize * 0.5f + StickerThickness * 0.5f);
			Sticker->SetRelativeScale3D(Size / 100.0);
			Sticker->SetRelativeLocation(Position);
			Sticker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Sticker->SetGenerateOverlapEvents(false);
			Sticker->SetCastShadow(false);
			Sticker->SetMaterial(0, Materials[Axis * 2 + (Home[Axis] < 0 ? 1 : 0)]);
			Sticker->ComponentTags.Add(Tag);
			Owner->AddInstanceComponent(Sticker);
			Sticker->RegisterComponent();
			GeneratedComponents.Add(Sticker);
		}
	}
	return true;
}

void UDreamRubiksCubeComponent::DestroyGeneratedVisuals()
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
		return;
	// 标签补齐编辑器复制/重建时可能没有继承的缓存，再逆序删除子网格和角块根。
	TArray<USceneComponent*> Components;
	Owner->GetComponents(Components);
	for (USceneComponent* Component : Components)
		if (Component->ComponentTags.Contains(VisualTag(this)))
			GeneratedComponents.AddUnique(Component);
	for (int32 Index = GeneratedComponents.Num() - 1; Index >= 0; --Index)
	{
		USceneComponent* Component = GeneratedComponents[Index];
		if (IsValid(Component))
		{
			Owner->RemoveInstanceComponent(Component);
			Component->DestroyComponent();
		}
	}
	GeneratedComponents.Reset();
}

FTransform UDreamRubiksCubeComponent::GetCornerTransform(int32 CornerIndex) const
{
	const FCornerState& State = Corners[CornerIndex];
	const FTransform& Initial = InitialCornerTransforms[CornerIndex];
	const FVector X(State.Basis[0]), Y(State.Basis[1]), Z(State.Basis[2]);
	FMatrix RotationMatrix = FMatrix::Identity;
	RotationMatrix.SetAxes(&X, &Y, &Z);
	const FQuat Rotation(RotationMatrix);
	// 从初始变换直接计算最终姿态，不把动画中间帧当成下一步的参考零点。
	return FTransform((Rotation * Initial.GetRotation()).GetNormalized(),
		CubeCenter + Rotation.RotateVector(Initial.GetLocation() - CubeCenter), Initial.GetScale3D());
}

void UDreamRubiksCubeComponent::ApplyAllVisualStates()
{
	if (!AreCornerRootsValid() || Corners.Num() != 8)
		return;
	for (int32 Index = 0; Index < 8; ++Index)
		CornerRoots[Index]->SetRelativeTransform(GetCornerTransform(Index));
}

void UDreamRubiksCubeComponent::ApplyLogicalMove(const FMove& Move)
{
	for (FCornerState& Corner : Corners)
	{
		if (Corner.Coordinate[static_cast<int32>(Move.Axis)] != Move.LayerSign)
			continue;
		Corner.Coordinate = RotateQuarter(Corner.Coordinate, Move.Axis, Move.Direction);
		for (FIntVector& Basis : Corner.Basis)
			Basis = RotateQuarter(Basis, Move.Axis, Move.Direction);
	}
}

bool UDreamRubiksCubeComponent::ComputeSolved() const
{
	if (Corners.Num() != 8)
		return false;
	int32 Colors[6] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
	int32 Counts[6] = {0, 0, 0, 0, 0, 0};
	TSet<FIntVector> Occupied;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const FCornerState& Corner = Corners[Index];
		if (Occupied.Contains(Corner.Coordinate))
			return false;
		Occupied.Add(Corner.Coordinate);
		const FIntVector Home = HomeCoordinate(Index);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (FMath::Abs(Corner.Coordinate[Axis]) != 1)
				return false;
			const FIntVector Normal = Corner.Basis[Axis] * Home[Axis];
			const int32 Face = FaceIndex(Normal);
			if (Face == INDEX_NONE)
				return false;
			// 当前角块的该张贴纸必须朝向它实际所在的外侧面，再比较面上四张贴纸的身份颜色。
			if (Corner.Coordinate[Face / 2] != Normal[Face / 2])
				return false;
			const int32 Color = Axis * 2 + (Home[Axis] < 0 ? 1 : 0);
			if (Colors[Face] != INDEX_NONE && Colors[Face] != Color)
				return false;
			Colors[Face] = Color;
			++Counts[Face];
		}
	}
	for (int32 Count : Counts)
		if (Count != 4)
			return false;
	return true;
}

bool UDreamRubiksCubeComponent::RotateLayer(EDreamRubiksCubeAxis Axis, int32 LayerSign, int32 Direction, bool bAnimate)
{
	if (static_cast<uint8>(Axis) > static_cast<uint8>(EDreamRubiksCubeAxis::Z) || (LayerSign != -1 && LayerSign != 1) ||
		(Direction != -1 && Direction != 1) || IsDragging() || bTurning || bNotifying || !IsActive() ||
		!EnsureInitialized())
		return false;
	return StartTurn({Axis, LayerSign, Direction}, bAnimate, false);
}

bool UDreamRubiksCubeComponent::StartTurn(const FMove& Move, bool bAnimate, bool bUndo)
{
	if (bTurning || bNotifying || !AreCornerRootsValid())
		return false;
	ActiveMove = Move;
	bUndoing = bUndo;
	TurnElapsed = 0.0f;
	ActiveTurnDuration = bAnimate ? TurnDuration : 0.0f;
	TurningCorners.Reset();
	TurningStartTransforms.Reset();
	for (int32 Index = 0; Index < 8; ++Index)
	{
		if (Corners[Index].Coordinate[static_cast<int32>(Move.Axis)] == Move.LayerSign)
		{
			TurningCorners.Add(Index);
			TurningStartTransforms.Add(CornerRoots[Index]->GetRelativeTransform());
		}
	}
	if (TurningCorners.Num() != 4)
		return false;
	bTurning = true;
	if (ActiveTurnDuration <= UE_SMALL_NUMBER)
		FinishTurn();
	return true;
}

void UDreamRubiksCubeComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bTurning && !IsDragging())
		return;
	if (!IsActive() || !IsConfigurationValid() || !AreCornerRootsValid())
	{
		EndDrag();
		AbortTurn();
		return;
	}
	if (!bTurning || !FMath::IsFinite(DeltaTime) || DeltaTime < 0.0f)
		return;
	TurnElapsed += DeltaTime;
	const float Alpha = FMath::Clamp(TurnElapsed / ActiveTurnDuration, 0.0f, 1.0f);
	const float Eased = Alpha * Alpha * (3.0f - 2.0f * Alpha);
	const FQuat Rotation(AxisVector(ActiveMove.Axis), ActiveMove.Direction * HALF_PI * Eased);
	for (int32 Index = 0; Index < TurningCorners.Num(); ++Index)
	{
		const FTransform& Start = TurningStartTransforms[Index];
		CornerRoots[TurningCorners[Index]]->SetRelativeTransform(
			FTransform((Rotation * Start.GetRotation()).GetNormalized(),
				CubeCenter + Rotation.RotateVector(Start.GetLocation() - CubeCenter), Start.GetScale3D()));
	}
	if (Alpha >= 1.0f)
		FinishTurn();
}

void UDreamRubiksCubeComponent::FinishTurn()
{
	const bool bPreviouslySolved = bSolved;
	const FMove CompletedMove = ActiveMove;
	ApplyLogicalMove(CompletedMove);
	if (bUndoing)
		MoveHistory.Pop(EAllowShrinking::No);
	else
		MoveHistory.Add(CompletedMove);
	bSolved = ComputeSolved();
	bTurning = false;
	bUndoing = false;
	TurningCorners.Reset();
	TurningStartTransforms.Reset();
	ApplyAllVisualStates();
	// 在广播前完成所有状态提交；回调期间拒绝再次修改状态，避免同一次还原被重入打乱。
	TGuardValue<bool> NotificationGuard(bNotifying, true);
	OnTurnCompleted.Broadcast(CompletedMove.Axis, CompletedMove.LayerSign, CompletedMove.Direction);
	NotifySolvedChange(bPreviouslySolved, true);
}

void UDreamRubiksCubeComponent::AbortTurn()
{
	if (!bTurning)
		return;
	// 尚未提交整数逻辑和历史，直接恢复起始变换即可；销毁了的角块不再被访问。
	for (int32 Index = 0; Index < TurningCorners.Num(); ++Index)
	{
		const int32 CornerIndex = TurningCorners[Index];
		if (CornerRoots.IsValidIndex(CornerIndex) && IsValid(CornerRoots[CornerIndex]))
			CornerRoots[CornerIndex]->SetRelativeTransform(TurningStartTransforms[Index]);
	}
	bTurning = false;
	bUndoing = false;
	TurningCorners.Reset();
	TurningStartTransforms.Reset();
}

void UDreamRubiksCubeComponent::NotifySolvedChange(bool bPreviouslySolved, bool bAllowVictoryEvent)
{
	if (bPreviouslySolved == bSolved)
		return;
	OnSolvedStateChanged.Broadcast(bSolved);
	if (bSolved && bAllowVictoryEvent)
		OnCubeSolved.Broadcast();
}

bool UDreamRubiksCubeComponent::ResetCube()
{
	if (IsDragging() || bTurning || bNotifying || !IsActive() || !EnsureInitialized())
		return false;
	const bool bPreviouslySolved = bSolved;
	InitializeSolvedState();
	MoveHistory.Reset();
	ApplyAllVisualStates();
	TGuardValue<bool> NotificationGuard(bNotifying, true);
	NotifySolvedChange(bPreviouslySolved, false);
	return true;
}

bool UDreamRubiksCubeComponent::ShuffleCube(int32 NumMoves, int32 Seed)
{
	if (IsDragging() || bTurning || bNotifying || !IsActive() || !EnsureInitialized())
		return false;
	const bool bPreviouslySolved = bSolved;
	InitializeSolvedState();
	MoveHistory.Reset();
	FRandomStream Random(Seed);
	int32 PreviousAxis = INDEX_NONE;
	for (int32 Index = 0; Index < FMath::Clamp(NumMoves, 1, 200); ++Index)
	{
		int32 Axis = Random.RandRange(0, 2);
		if (Axis == PreviousAxis)
			Axis = (Axis + Random.RandRange(1, 2)) % 3;
		ApplyLogicalMove({static_cast<EDreamRubiksCubeAxis>(Axis), Random.RandRange(0, 1) != 0 ? 1 : -1,
			Random.RandRange(0, 1) != 0 ? 1 : -1});
		PreviousAxis = Axis;
	}
	// 极少数合法随机序列可能回到完整状态；补一次单层动作，确保给玩家的确实是待解题面。
	if (ComputeSolved())
		ApplyLogicalMove({EDreamRubiksCubeAxis::X, 1, 1});
	bSolved = ComputeSolved();
	ApplyAllVisualStates();
	TGuardValue<bool> NotificationGuard(bNotifying, true);
	NotifySolvedChange(bPreviouslySolved, false);
	return true;
}

bool UDreamRubiksCubeComponent::UndoLastMove(bool bAnimate)
{
	if (IsDragging() || bTurning || bNotifying || !IsActive() || !EnsureInitialized() || MoveHistory.IsEmpty())
		return false;
	const FMove Last = MoveHistory.Last();
	return StartTurn({Last.Axis, Last.LayerSign, -Last.Direction}, bAnimate, true);
}

FIntVector UDreamRubiksCubeComponent::GetCornerCoordinate(int32 CornerIndex) const
{
	return bInitialized && Corners.IsValidIndex(CornerIndex) ? Corners[CornerIndex].Coordinate : FIntVector::ZeroValue;
}

USceneComponent* UDreamRubiksCubeComponent::GetCornerComponent(int32 CornerIndex) const
{
	return CornerRoots.IsValidIndex(CornerIndex) && IsValid(CornerRoots[CornerIndex]) ? CornerRoots[CornerIndex]
																					  : nullptr;
}

bool UDreamRubiksCubeComponent::PickCorner(
	const FVector& Origin, const FVector& Direction, int32& OutIndex, FHitResult& OutHit) const
{
	if (Origin.ContainsNaN() || Direction.ContainsNaN() || Direction.IsNearlyZero() || !AreCornerRootsValid())
		return false;
	// 只在八个角块自己的查询碰撞上重投射，不搜索墙后 Actor；外部遮挡已由控制器的首个命中保证。
	// 距离兼容手办远处捕获相机；最终仍选最近的角块，子网格和复杂碰撞都可作为命中入口。
	const FVector End = Origin + Direction.GetSafeNormal() * 1000000.0;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DreamRubiksCornerPick), true);
	float BestTime = TNumericLimits<float>::Max();
	OutIndex = INDEX_NONE;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		TArray<USceneComponent*> Children;
		CornerRoots[Index]->GetChildrenComponents(true, Children);
		Children.Add(CornerRoots[Index]);
		for (USceneComponent* Child : Children)
		{
			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Child);
			if (!Primitive || !Primitive->IsQueryCollisionEnabled())
				continue;
			FHitResult Hit;
			if (Primitive->LineTraceComponent(Hit, Origin, End, Params) && Hit.Time < BestTime)
			{
				BestTime = Hit.Time;
				OutHit = Hit;
				OutIndex = Index;
			}
		}
	}
	return OutIndex != INDEX_NONE;
}

bool UDreamRubiksCubeComponent::BeginDrag(AActor* Interactor, const FVector& RayOrigin, const FVector& RayDirection)
{
	if (bTurning || bNotifying || IsDragging() || !IsActive() || !EnsureInitialized())
		return false;
	int32 CornerIndex;
	FHitResult Hit;
	if (!PickCorner(RayOrigin, RayDirection, CornerIndex, Hit))
		return false;
	const FTransform RootTransform = GetOwner()->GetRootComponent()->GetComponentTransform();
	const FVector LocalHit = RootTransform.InverseTransformPosition(Hit.ImpactPoint) - CubeCenter;
	// 法线应按缩放转置回到局部空间。正的非等比缩放也不会把点击的面识别成另一轴。
	const FVector LocalNormal =
		(RootTransform.InverseTransformVectorNoScale(Hit.ImpactNormal) * RootTransform.GetScale3D()).GetSafeNormal();
	GrabFaceAxis = 0;
	for (int32 Axis = 1; Axis < 3; ++Axis)
		if (FMath::Abs(LocalNormal[Axis]) > FMath::Abs(LocalNormal[GrabFaceAxis]))
			GrabFaceAxis = Axis;
	GrabFaceNormal = FVector::ZeroVector;
	GrabFaceNormal[GrabFaceAxis] = LocalNormal[GrabFaceAxis] >= 0.0 ? 1.0 : -1.0;
	GrabPlaneCoordinate = LocalHit[GrabFaceAxis];
	GrabCoordinate = Corners[CornerIndex].Coordinate;
	// 正好命中内部黑色面、模型倒置法线或面向内的碰撞时不开始抓取，避免转动不可见的错误层。
	if (GrabCoordinate[GrabFaceAxis] * GrabFaceNormal[GrabFaceAxis] <= 0.0)
		return false;
	bTurnIssuedForGesture = false;
	AccumulatedDrag = FVector::ZeroVector;
	return Super::BeginDrag(Interactor, RayOrigin, RayDirection);
}

void UDreamRubiksCubeComponent::EndDrag()
{
	Super::EndDrag();
	bHasGrabSample = false;
	AccumulatedDrag = FVector::ZeroVector;
	// 不取消已经确定的一层转动；动画在松手或模式退出后仍会提交到精确的 90° 姿态。
}

bool UDreamRubiksCubeComponent::IntersectGrabPlane(
	const FVector& Origin, const FVector& Direction, FVector& OutPoint) const
{
	const FTransform RootTransform = GetOwner()->GetRootComponent()->GetComponentTransform();
	const FVector LocalOrigin = RootTransform.InverseTransformPosition(Origin) - CubeCenter;
	const FVector LocalDirection = RootTransform.InverseTransformVector(Direction).GetSafeNormal();
	const double Denominator = LocalDirection[GrabFaceAxis];
	if (FMath::Abs(Denominator) < 0.0001 || LocalOrigin.ContainsNaN() || LocalDirection.ContainsNaN())
		return false;
	const double Distance = (GrabPlaneCoordinate - LocalOrigin[GrabFaceAxis]) / Denominator;
	if (Distance < 0.0 || !FMath::IsFinite(Distance))
		return false;
	OutPoint = LocalOrigin + LocalDirection * Distance;
	return !OutPoint.ContainsNaN();
}

void UDreamRubiksCubeComponent::InitializeDragSample(const FVector& RayOrigin, const FVector& RayDirection)
{
	// 暂停映射后也只重建面内采样，保留一次手势已经提交的标记，不让重新进入显示面再转一步。
	AccumulatedDrag = FVector::ZeroVector;
	bHasGrabSample = IntersectGrabPlane(RayOrigin, RayDirection, LastGrabPoint);
}

void UDreamRubiksCubeComponent::RebaseDragSample(const FVector& RayOrigin, const FVector& RayDirection)
{
	// 控制器用“当前相机、上一鼠标位置”重采样。只更新上一点，不能清空跨多帧积累的拖动阈值。
	bHasGrabSample = IntersectGrabPlane(RayOrigin, RayDirection, LastGrabPoint);
}

void UDreamRubiksCubeComponent::ApplyDragSample(
	const FVector& RayOrigin, const FVector& RayDirection, const FVector2D& PointerDelta)
{
	if (bTurnIssuedForGesture || bTurning)
		return;
	if (!AreCornerRootsValid())
	{
		EndDrag();
		return;
	}
	FVector Point;
	if (!IntersectGrabPlane(RayOrigin, RayDirection, Point))
	{
		bHasGrabSample = false;
		AccumulatedDrag = FVector::ZeroVector;
		return;
	}
	if (!bHasGrabSample)
	{
		LastGrabPoint = Point;
		bHasGrabSample = true;
		return;
	}
	const float Sensitivity = FMath::IsFinite(DragSensitivity) ? FMath::Max(0.0f, DragSensitivity) : 0.0f;
	AccumulatedDrag += (Point - LastGrabPoint) * Sensitivity;
	LastGrabPoint = Point;
	AccumulatedDrag[GrabFaceAxis] = 0.0;
	int32 TangentAxis = (GrabFaceAxis + 1) % 3;
	const int32 OtherTangent = (GrabFaceAxis + 2) % 3;
	if (FMath::Abs(AccumulatedDrag[OtherTangent]) > FMath::Abs(AccumulatedDrag[TangentAxis]))
		TangentAxis = OtherTangent;
	if (FMath::Abs(AccumulatedDrag[TangentAxis]) < DragThreshold)
		return;
	FVector Tangent = FVector::ZeroVector;
	Tangent[TangentAxis] = AccumulatedDrag[TangentAxis] > 0.0 ? 1.0 : -1.0;
	// 点在外表面时，角速度方向满足 Tangent = Axis × Normal；因此 Axis = Normal × Tangent。
	const FVector SignedAxis = FVector::CrossProduct(GrabFaceNormal, Tangent);
	int32 Axis = 0;
	for (int32 Candidate = 1; Candidate < 3; ++Candidate)
		if (FMath::Abs(SignedAxis[Candidate]) > FMath::Abs(SignedAxis[Axis]))
			Axis = Candidate;
	const FMove Move = {static_cast<EDreamRubiksCubeAxis>(Axis), GrabCoordinate[Axis], SignedAxis[Axis] > 0.0 ? 1 : -1};
	// 先锁定手势，再可能同步完成/广播，防止同一次抓取在即时动画或回调里重复触发。
	bTurnIssuedForGesture = true;
	if (!StartTurn(Move, true, false))
		bTurnIssuedForGesture = false;
}

void UDreamRubiksCubeComponent::DrawDebugRange() const
{
	if (!bInitialized || !AreCornerRootsValid() || !GetWorld())
		return;
	const FTransform RootTransform = GetOwner()->GetRootComponent()->GetComponentTransform();
	const FVector Center = RootTransform.TransformPosition(CubeCenter);
	for (int32 Index = 0; Index < 8; ++Index)
		DrawDebugSphere(GetWorld(), CornerRoots[Index]->GetComponentLocation(), 3.0f, 8,
			TurningCorners.Contains(Index) ? FColor::Yellow : FColor::Cyan, false, 0.0f);
	if (bTurning)
	{
		const FVector Axis = RootTransform.TransformVectorNoScale(AxisVector(ActiveMove.Axis));
		const double Length = CellSize * RootTransform.GetScale3D().GetMax() * 1.5;
		DrawDebugDirectionalArrow(
			GetWorld(), Center - Axis * Length, Center + Axis * Length, 12.0f, FColor::Magenta, false, 0.0f, 0, 2.0f);
	}
}
