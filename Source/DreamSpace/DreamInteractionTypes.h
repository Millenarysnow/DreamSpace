// DreamSpace 交互运行时的基础数据类型。
// 这些结构只描述逻辑状态和命令，不直接依赖场景组件，便于存档、回滚和重放。

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DreamInteractionTypes.generated.h"

class UDreamInteractionCapability;
class UStaticMesh;

DECLARE_LOG_CATEGORY_EXTERN(LogDreamInteraction, Log, All);

/** 玩家当前所处的交互模式。模式只影响输入和可用能力，不复制世界状态。 */
UENUM(BlueprintType)
enum class EDreamInteractionMode : uint8
{
	ThirdPerson UMETA(DisplayName = "第三人称"),
	Overview UMETA(DisplayName = "全局视角")
};

/** 命令计算时使用的参考坐标系。 */
UENUM(BlueprintType)
enum class EDreamReferenceFrame : uint8
{
	World UMETA(DisplayName = "世界"),
	Assembly UMETA(DisplayName = "装配体局部"),
	Node UMETA(DisplayName = "节点局部"),
	Custom UMETA(DisplayName = "自定义")
};

/** 节点的持久化结构状态。表现层可以据此切换 Mesh、碰撞和特效。 */
UENUM(BlueprintType)
enum class EDreamNodeRuntimeState : uint8
{
	Intact UMETA(DisplayName = "完整"),
	Damaged UMETA(DisplayName = "损坏"),
	Broken UMETA(DisplayName = "破坏"),
	Released UMETA(DisplayName = "已释放"),
	Destroyed UMETA(DisplayName = "已销毁")
};

/** 建筑旋转或缩放时，内部占用者采用的策略。 */
UENUM(BlueprintType)
enum class EDreamOccupantPolicy : uint8
{
	FollowAssembly UMETA(DisplayName = "跟随装配体"),
	KeepWorldTransform UMETA(DisplayName = "保持世界变换"),
	FreezeAndRestore UMETA(DisplayName = "冻结并恢复"),
	Eject UMETA(DisplayName = "推出装配体"),
	FailOperation UMETA(DisplayName = "操作失败")
};

/** 输入层输出的通用意图。输入设备不应在此层实现建筑逻辑。 */
UENUM(BlueprintType)
enum class EDreamInteractionIntentType : uint8
{
	None,
	Select,
	Begin,
	UpdateTransform,
	Confirm,
	Cancel,
	ToggleOverview
};

/** 命令类型。新增行为时可以扩展命令类型，而不改变输入数据。 */
UENUM(BlueprintType)
enum class EDreamCommandType : uint8
{
	Invalid,
	Transform,
	SetNodeState,
	Composite
};

/** 一个可交互节点的定义。节点 ID 由资产保存，不能使用数组下标代替。 */
USTRUCT(BlueprintType)
struct DREAMSPACE_API FDreamNodeDefinition
{
	GENERATED_BODY()

	/** 资产内稳定的节点 ID。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "节点")
	FGuid NodeId;

	/** 父节点 ID。无父节点时使用无效 GUID，节点挂到装配体根。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "节点")
	FGuid ParentNodeId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "节点")
	FName NodeName;

	/** 节点相对父节点的默认局部变换。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "节点")
	FTransform DefaultLocalTransform;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "节点")
	FGameplayTagContainer GameplayTags;

	/** 可选的表现 Mesh；没有 Mesh 的节点仍可以作为碰撞代理或逻辑父节点。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "表现")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** 交互能力配置对象。对象定义资产拥有这些实例。 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "能力")
	TArray<TObjectPtr<UDreamInteractionCapability>> Capabilities;

	/** 是否在运行时创建可见的场景组件。纯逻辑节点可以关闭它。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "表现")
	bool bCreateSceneComponent = true;

	/** 是否可以被射线选择。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "交互")
	bool bSelectable = true;
};

/** 节点的运行时逻辑状态。所有可存档的变化都写入这里。 */
USTRUCT(BlueprintType)
struct DREAMSPACE_API FDreamNodeState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "节点")
	FGuid NodeId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "节点")
	FGuid ParentNodeId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "节点")
	FTransform LocalTransform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "节点")
	EDreamNodeRuntimeState RuntimeState = EDreamNodeRuntimeState::Intact;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "节点")
	bool bExists = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "节点")
	bool bLocked = false;

	bool IsOperational() const
	{
		return bExists && !bLocked && RuntimeState != EDreamNodeRuntimeState::Destroyed;
	}
};

/** 装配体的完整逻辑状态快照，可直接作为事务前后状态和存档数据。 */
USTRUCT(BlueprintType)
struct DREAMSPACE_API FDreamAssemblyState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "装配体")
	FGuid AssemblyId;

	/** 装配体根相对世界的变换。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "装配体")
	FTransform RootTransform;

	/** 装配体局部重力方向，世界方向由 RootTransform 旋转得到。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "重力")
	FVector LocalGravityDirection = FVector::DownVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "重力")
	bool bProvidesGravity = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "重力")
	int32 GravityPriority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "状态")
	bool bIsCarried = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "状态")
	FGameplayTagContainer StateTags;

	/** 每成功提交一次命令递增，用于并发保护和未来网络验证。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "状态")
	int64 StateVersion = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "节点")
	TArray<FDreamNodeState> Nodes;

	FDreamNodeState* FindNode(const FGuid& NodeId)
	{
		return Nodes.FindByPredicate([&NodeId](const FDreamNodeState& Node) { return Node.NodeId == NodeId; });
	}

	const FDreamNodeState* FindNode(const FGuid& NodeId) const
	{
		return Nodes.FindByPredicate([&NodeId](const FDreamNodeState& Node) { return Node.NodeId == NodeId; });
	}
};

/** 输入系统传给目标解析器和能力的通用意图。 */
USTRUCT(BlueprintType)
struct DREAMSPACE_API FDreamInteractionIntent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "意图")
	EDreamInteractionIntentType Type = EDreamInteractionIntentType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "意图")
	FGuid TargetNodeId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "意图")
	FVector PointerDelta = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "意图")
	FVector RotationAxis = FVector::UpVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "意图")
	float RotationDeltaDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "意图")
	float ScaleDelta = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "意图")
	bool bConfirmed = false;
};

/** 单个节点的变换结果，作为命令的可序列化载荷。 */
USTRUCT(BlueprintType)
struct DREAMSPACE_API FDreamNodeTransformChange
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	FGuid NodeId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	FTransform NewLocalTransform;
};

/** 节点状态转移结果，破坏、释放和锁定都通过该结构进入事务。 */
USTRUCT(BlueprintType)
struct DREAMSPACE_API FDreamNodeStateChange
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	FGuid NodeId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	EDreamNodeRuntimeState NewRuntimeState = EDreamNodeRuntimeState::Intact;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	bool bExists = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	bool bLocked = false;
};

/** 可验证、可记录的操作命令。 */
USTRUCT(BlueprintType)
struct DREAMSPACE_API FDreamInteractionCommand
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	EDreamCommandType Type = EDreamCommandType::Invalid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	FGuid CommandId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	FGuid AssemblyId;

	/** 0 表示不检查版本；正式操作应填写能力开始时捕获的版本。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	int64 ExpectedStateVersion = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	EDreamReferenceFrame ReferenceFrame = EDreamReferenceFrame::Assembly;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	TArray<FDreamNodeTransformChange> TransformChanges;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	TArray<FDreamNodeStateChange> StateChanges;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	FVector NewLocalGravityDirection = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	bool bChangesGravity = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	bool bSetsCarriedState = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	bool bIsCarried = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	FGameplayTagContainer AddedStateTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "命令")
	FGameplayTagContainer RemovedStateTags;

	bool IsValid() const
	{
		return Type != EDreamCommandType::Invalid && AssemblyId.IsValid() && CommandId.IsValid() &&
			(!TransformChanges.IsEmpty() || !StateChanges.IsEmpty() || bChangesGravity);
	}
};

/** 一次提交的事务记录，保留前后状态以支持回滚和调试。 */
USTRUCT(BlueprintType)
struct DREAMSPACE_API FDreamInteractionTransaction
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "事务")
	FGuid TransactionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "事务")
	FDreamInteractionCommand Command;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "事务")
	FDreamAssemblyState BeforeState;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "事务")
	FDreamAssemblyState AfterState;
};
