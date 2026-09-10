// 交互运行时的数据契约：稳定 ID、局部状态、意图以及可序列化命令。
#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DreamInteractionTypes.generated.h"

class UDreamInteractionCapability;
class UStaticMesh;
class UMaterialInterface;
DECLARE_LOG_CATEGORY_EXTERN(LogDreamInteraction, Log, All);

UENUM(BlueprintType)
enum class EDreamInteractionMode : uint8
{
	ThirdPerson,
	Overview
};
UENUM(BlueprintType)
enum class EDreamReferenceFrame : uint8
{
	World,
	Assembly,
	Node,
	Custom
};
UENUM(BlueprintType)
enum class EDreamPivotMode : uint8
{
	SelectionCenter,
	NodeOrigin,
	Socket,
	ConfiguredPoint
};
UENUM(BlueprintType)
enum class EDreamNodeRuntimeState : uint8
{
	Intact,
	Damaged,
	Broken,
	Released,
	Destroyed
};
/** 第一版内部对象使用运动学节点；占用者可随参考系移动、留在世界原位或阻止操作。 */
UENUM(BlueprintType)
enum class EDreamOccupantPolicy : uint8
{
	FollowAssembly,
	KeepWorldTransform,
	FailOperation
};
UENUM(BlueprintType)
enum class EDreamInteractionIntentType : uint8
{
	Begin,
	UpdateTransform,
	Confirm,
	Cancel
};
UENUM(BlueprintType)
enum class EDreamCommandType : uint8
{
	Invalid,
	Transform,
	SetNodeState,
	Pickup,
	Composite
};

/** 显式的逻辑重力体积。范围独立于 Mesh，体积随所属参考系旋转。 */
USTRUCT(BlueprintType)
struct DREAMSPACE_API FDreamGravitySettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "重力")
	bool bEnabled = false;
	UPROPERTY(EditAnywhere, Category = "重力")
	FVector Center = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category = "重力")
	FVector Extent = FVector(500);
	UPROPERTY(EditAnywhere, Category = "重力")
	FVector LocalDirection = FVector::DownVector;
	UPROPERTY(EditAnywhere, Category = "重力")
	int32 Priority = 0;
};

/** 每个节点总有坐标系，Mesh 仅是可选表现。几何尺寸由 MeshTransform 配置。 */
USTRUCT(BlueprintType)
struct DREAMSPACE_API FDreamNodeDefinition
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "节点")
	FGuid NodeId;
	UPROPERTY(EditAnywhere, Category = "节点")
	FGuid ParentNodeId;
	UPROPERTY(EditAnywhere, Category = "节点")
	FName NodeName;
	UPROPERTY(EditAnywhere, Category = "节点")
	FTransform DefaultLocalTransform = FTransform::Identity;
	UPROPERTY(EditAnywhere, Category = "节点")
	FGameplayTagContainer GameplayTags;
	UPROPERTY(EditAnywhere, Category = "表现")
	TSoftObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(EditAnywhere, Category = "表现")
	TSoftObjectPtr<UStaticMesh> BrokenMesh;
	UPROPERTY(EditAnywhere, Category = "表现")
	TSoftObjectPtr<UMaterialInterface> Material;
	/** 表现材质可使用 Tint 参数，帮助美术区分层和部件，不影响逻辑状态。 */
	UPROPERTY(EditAnywhere, Category = "表现")
	FLinearColor Tint = FLinearColor::White;
	UPROPERTY(EditAnywhere, Category = "表现")
	FTransform MeshTransform = FTransform::Identity;
	/** 命名锚点属于逻辑节点，改变 Mesh 后仍然稳定。 */
	UPROPERTY(EditAnywhere, Category = "锚点")
	TMap<FName, FTransform> Sockets;
	UPROPERTY(EditAnywhere, Instanced, Category = "能力")
	TArray<TObjectPtr<UDreamInteractionCapability>> Capabilities;
	/** 子节点可屏蔽祖先能力；同 ID 的本地配置优先于祖先配置。 */
	UPROPERTY(EditAnywhere, Category = "能力")
	TArray<FName> DisabledInheritedCapabilities;
	UPROPERTY(EditAnywhere, Category = "碰撞")
	bool bCollisionEnabled = true;
	UPROPERTY(EditAnywhere, Category = "碰撞")
	bool bCollisionWhenBroken = true;
	UPROPERTY(EditAnywhere, Category = "碰撞")
	FVector CollisionCenter = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category = "碰撞")
	FVector CollisionExtent = FVector(50);
	UPROPERTY(EditAnywhere, Category = "交互")
	bool bSelectable = true;
	UPROPERTY(EditAnywhere, Category = "重力")
	FDreamGravitySettings Gravity;
};

USTRUCT(BlueprintType)
struct DREAMSPACE_API FDreamNodeState
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FGuid NodeId;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FGuid ParentNodeId;
	/** 释放的根节点以世界为父坐标系；其他节点以父节点或装配体为坐标系。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FTransform LocalTransform = FTransform::Identity;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	EDreamNodeRuntimeState RuntimeState = EDreamNodeRuntimeState::Intact;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	bool bExists = true;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	bool bLocked = false;
	bool IsOperational() const { return bExists && !bLocked && RuntimeState != EDreamNodeRuntimeState::Destroyed; }
};

USTRUCT(BlueprintType)
struct DREAMSPACE_API FDreamAssemblyState
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FGuid AssemblyId;
	/** 存档还检查对象定义，防止把同 ID 的旧存档套到另一份资产上。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FSoftObjectPath DefinitionPath;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FTransform RootTransform = FTransform::Identity;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	int64 StateVersion = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	TArray<FDreamNodeState> Nodes;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FGameplayTagContainer StateTags;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	bool bIsCarried = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FGuid CarrierId;
	/** 携带期间根参考系由持有者插槽导出，不逐帧篡改逻辑根变换。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FTransform CarryTransform = FTransform::Identity;
	FDreamNodeState* FindNode(const FGuid& Id)
	{
		return Nodes.FindByPredicate([&](const auto& N) { return N.NodeId == Id; });
	}
	const FDreamNodeState* FindNode(const FGuid& Id) const
	{
		return Nodes.FindByPredicate([&](const auto& N) { return N.NodeId == Id; });
	}
};

/** 拖动量始终是相对 Begin 时状态的累计量，预览次数不会累积误差。 */
USTRUCT(BlueprintType)
struct DREAMSPACE_API FDreamInteractionIntent
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EDreamInteractionIntentType Type = EDreamInteractionIntentType::Begin;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGuid TargetNodeId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FGuid> TargetNodeIds;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Translation = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector RotationAxis = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float RotationDeltaDegrees = 0;
	/** 0 表示保持尺寸，-0.5 表示尺寸乘以 0.5。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float ScaleDelta = 0;
};

/** 占用者是事务参与者；位置、速度、相机和重力必须作为整体恢复。 */
USTRUCT(BlueprintType)
struct DREAMSPACE_API FDreamOccupantState
{
	GENERATED_BODY()
	UPROPERTY(SaveGame)
	FGuid OccupantId;
	UPROPERTY(SaveGame)
	FTransform Transform = FTransform::Identity;
	UPROPERTY(SaveGame)
	FVector Velocity = FVector::ZeroVector;
	UPROPERTY(SaveGame)
	FVector GravityDirection = FVector::DownVector;
	UPROPERTY(SaveGame)
	FQuat ControlRotation = FQuat::Identity;
	UPROPERTY(SaveGame)
	uint8 MovementMode = 1;
	UPROPERTY(SaveGame)
	uint8 CustomMovementMode = 0;
};

/** 命令保存输入和最终结果。提交时重新生成并校验，不能绕过能力限制提交任意快照。 */
USTRUCT(BlueprintType)
struct DREAMSPACE_API FDreamInteractionCommand
{
	GENERATED_BODY()
	UPROPERTY(SaveGame)
	FGuid CommandId;
	UPROPERTY(SaveGame)
	FGuid SessionId;
	UPROPERTY(SaveGame)
	FGuid AssemblyId;
	UPROPERTY(SaveGame)
	FGuid RequesterId;
	UPROPERTY(SaveGame)
	FName CapabilityId;
	UPROPERTY(SaveGame)
	EDreamInteractionMode Mode = EDreamInteractionMode::Overview;
	UPROPERTY(SaveGame)
	EDreamCommandType Type = EDreamCommandType::Invalid;
	/** 版本 0 也是需要严格检查的有效版本。 */
	UPROPERTY(SaveGame)
	int64 ExpectedStateVersion = 0;
	UPROPERTY(SaveGame)
	FDreamInteractionIntent Intent;
	UPROPERTY(SaveGame)
	EDreamReferenceFrame ReferenceFrame = EDreamReferenceFrame::Assembly;
	UPROPERTY(SaveGame)
	FVector WorldPivot = FVector::ZeroVector;
	UPROPERTY(SaveGame)
	FVector WorldAxis = FVector::UpVector;
	UPROPERTY(SaveGame)
	FDreamAssemblyState ResultState;
	bool IsValid() const
	{
		return CommandId.IsValid() && AssemblyId.IsValid() && !CapabilityId.IsNone() &&
			   Type != EDreamCommandType::Invalid;
	}
};

USTRUCT(BlueprintType)
struct DREAMSPACE_API FDreamInteractionTransaction
{
	GENERATED_BODY()
	UPROPERTY(SaveGame)
	FGuid TransactionId;
	UPROPERTY(SaveGame)
	FDreamInteractionCommand Command;
	UPROPERTY(SaveGame)
	FDreamAssemblyState BeforeState;
	UPROPERTY(SaveGame)
	FDreamAssemblyState AfterState;
	UPROPERTY(SaveGame)
	TArray<FDreamOccupantState> BeforeOccupants;
	UPROPERTY(SaveGame)
	TArray<FDreamOccupantState> AfterOccupants;
};
