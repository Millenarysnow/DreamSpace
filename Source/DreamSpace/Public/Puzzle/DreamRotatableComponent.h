#pragma once

#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "DreamInteractableInterface.h"
#include "DreamRotatableComponent.generated.h"

class UDreamPivotPointComponent;
class ACharacter;

/** 可转动组件允许绕枢轴点局部坐标系的哪一根轴转动。 */
UENUM(BlueprintType)
enum class EDreamPivotRotationAxis : uint8
{
	/** 枢轴点局部 X 轴（红轴）。 */
	X,
	/** 枢轴点局部 Y 轴（绿轴）。 */
	Y,
	/** 枢轴点局部 Z 轴（蓝轴）。 */
	Z
};

/**
 * 可转动组件：允许所属 Actor 在被交互后，绕枢轴点转动一个固定的步进角。
 *
 * 工作方式：
 * - 需要一个枢轴点组件（UDreamPivotPointComponent）作为转轴的原点和方向来源；
 * - 配置绕枢轴点局部 X/Y/Z 中的哪根轴、每次交互转动多少度；
 * - 玩家（或蓝图）触发交互后，所属 Actor 会在 RotationDuration 秒内平滑地
 *   绕“过枢轴点的目标轴”公转 + 自转；RotationDuration 为 0 时瞬间完成；
 * - 转动进行中再次触发会被忽略，避免姿态叠加出错。
 * - 可选开启站立角色重力跟随：角色随平台转动，离开后仍保留最后的世界重力方向。
 *
 * 由于枢轴点是一个独立组件，策划可以自由摆放它的位置和朝向，
 * 从而实现门绕铰链转、机关绕任意斜轴转等效果，而不需要修改代码。
 *
 * 调试：控制台 dream.DebugPivots 1 会同时绘制枢轴点和本组件当前使用的转轴。
 */
UCLASS(ClassGroup = (DreamPuzzle), meta = (BlueprintSpawnableComponent))
class DREAMSPACE_API UDreamRotatableComponent : public UActorComponent, public IDreamInteractableInterface
{
	GENERATED_BODY()

public:
	UDreamRotatableComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(
		float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	//~ IDreamInteractableInterface 接口：玩家交互时触发一次转动。
	virtual void OnInteracted_Implementation(AActor* Interactor) override;

	/** 立即触发一次转动；转动进行中再次调用会被忽略并输出警告日志。 */
	UFUNCTION(BlueprintCallable, Category = "可转动")
	void TriggerRotation();

	/** 当前是否正在转动中。 */
	UFUNCTION(BlueprintPure, Category = "可转动")
	bool IsRotating() const { return bRotating; }

private:
	// ---------- 配置项 ----------

	/**
	 * 要使用的枢轴点组件。编辑器会提供组件选择器，默认只列出同一 Actor
	 * 上的 UDreamPivotPointComponent；留空时自动选择第一个枢轴点。
	 */
	UPROPERTY(EditAnywhere, Category = "可转动|枢轴",
		meta = (UseComponentPicker = true,
			AllowedClasses = "/Script/DreamSpace.DreamPivotPointComponent",
			DisplayName = "枢轴点组件"))
	FComponentReference PivotComponent;

	/**
	 * 旧版本按名称选择枢轴点的兼容字段。新配置应使用上面的组件选择器；
	 * 当组件引用为空时仍会读取这个名称，避免已有蓝图失效。
	 */
	UPROPERTY(EditAnywhere, Category = "可转动|枢轴", meta = (AdvancedDisplay))
	FName PivotComponentName;

	/** 绕枢轴点局部坐标系的哪根轴转动。 */
	UPROPERTY(EditAnywhere, Category = "可转动|转动")
	EDreamPivotRotationAxis RotationAxis = EDreamPivotRotationAxis::Z;

	/** 每次交互转动的角度（度）；负值表示绕轴反向转动。 */
	UPROPERTY(EditAnywhere, Category = "可转动|转动")
	float StepAngleDegrees = 90.0f;

	/** 单次转动的耗时（秒）；0 表示瞬间完成。 */
	UPROPERTY(EditAnywhere, Category = "可转动|转动", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RotationDuration = 0.5f;

	/**
	 * 开启后，运动会检测所属 Actor 上随根一起移动的查询碰撞组件。
	 * 转动中途碰到阻挡物会沿原路径弹回本次交互的起始姿态；默认关闭以兼容已有机关。
	 * 网格或碰撞体需要启用 Query 碰撞，并与障碍物互相设置为 Block。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "可转动|碰撞",
		meta = (AllowPrivateAccess = "true", DisplayName = "运动考虑碰撞"))
	bool bConsiderCollision = false;

	/**
	 * 开启后，只影响当前真正站在所属 Actor 的移动碰撞组件上的 Character。
	 * 每次实际转动都把角色的位置、胶囊朝向、世界重力方向和控制器视角一起旋转，
	 * 因此平台绕 X/Y 轴倾斜至墙面或天花板时，角色仍可在该表面站立和行走。
	 * 走下平台或跳离后停止跟随，但不恢复原重力；下一座平台从角色当前重力继续旋转。
	 * 与“运动考虑碰撞”独立配置；受阻回弹时仍在平台上的角色会同步转回。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "可转动|重力",
		meta = (AllowPrivateAccess = "true", DisplayName = "站立角色重力跟随旋转"))
	bool bRotateStandingCharacterGravity = false;

	/** 调试绘制时转轴箭头的长度（厘米）。 */
	UPROPERTY(EditAnywhere, Category = "调试", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DebugAxisDrawLength = 150.0f;

	// ---------- 内部状态 ----------

	/** 在 BeginPlay 中解析并缓存的枢轴点组件；未找到时为 nullptr 并已在日志中报警。 */
	UPROPERTY(Transient)
	TObjectPtr<UDreamPivotPointComponent> CachedPivot;

	/** 是否正在转动中。 */
	bool bRotating = false;
	/** 本次转动已经过的时间（秒）。 */
	float RotationElapsed = 0.0f;
	/** 正在从首次受阻处回到本次起始姿态；此期间仍视为转动中，新的交互会被忽略。 */
	bool bReturning = false;
	/** 前进时最后一个已验证安全的进度，回弹的起点就固定在这里。 */
	float SafeRotationAlpha = 0.0f;
	/** 回弹已经过的时间和回弹总时长；总时长随已走过的路程缩放。 */
	float ReturnElapsed = 0.0f;
	float ReturnDuration = 0.0f;

	// 以下三个量在触发瞬间快照固定：
	// 枢轴点在被转 Actor 上时会随 Actor 一起运动，但“绕某轴转动”不会改变该轴上的点和轴方向，
	// 因此用触发瞬间的快照做整段插值在数学上是自洽的，也避免逐帧重取引入误差。
	/** 触发瞬间所属 Actor 的世界变换（插值起点）。 */
	FTransform StartActorTransform = FTransform::Identity;
	/** 完整步进角对应的四元数（绕触发瞬间的转轴方向）。 */
	FQuat FullStepQuat = FQuat::Identity;
	/** 触发瞬间枢轴点的世界位置（公转中心）。 */
	FVector PivotWorldLocation = FVector::ZeroVector;

	// ---------- 内部函数 ----------

	/** 按配置解析枢轴点组件：组件引用 > 旧名称 > Actor 上第一个枢轴点。 */
	UDreamPivotPointComponent* ResolvePivot() const;

	/** 取枢轴点局部坐标系中配置轴的世界方向（单位向量）。 */
	FVector GetRotationAxisWorldDir(const UDreamPivotPointComponent* Pivot) const;

	/** 按本次交互的起点和进度计算世界姿态；前进和回弹共用同一条精确轨迹。 */
	FTransform GetActorTransformAtAlpha(float Alpha) const;

	/** 沿旋转弧线检查并推进到目标进度；遇阻时停在最后一个安全姿态。 */
	bool AdvanceWithCollision(float TargetAlpha, const TArray<ACharacter*>& StandingCharacters);

	/**
	 * 每帧运动前重新收集站立角色，不用空间重叠推断站立，也不把角色附着到平台。
	 * 这样从旁边经过、正在下落或已跳离的角色不会被吸住，中途登上平台也能参与后续旋转。
	 */
	void GatherStandingCharacters(TArray<ACharacter*>& OutCharacters) const;

	/** 预测平台从当前姿态到目标姿态时，站立角色应到达的完整世界变换。 */
	FTransform GetStandingCharacterTransform(const ACharacter* Character, const FTransform& TargetActorTransform) const;

	/**
	 * 统一应用平台姿态与站立角色的增量旋转，覆盖普通插值、碰撞子步、瞬时旋转和回弹。
	 * 同时刷新 CharacterMovement 的底座缓存，防止引擎随后再次搬运同一段平台位移。
	 */
	void ApplyActorTransform(const FTransform& TargetActorTransform, const TArray<ACharacter*>& StandingCharacters);

	/** 从当前已通过的角度开始回弹，完成后精确恢复起始姿态。 */
	void BeginReturn();

	/** 调试开关打开时逐帧绘制当前使用的转轴（品红色双向箭头）。 */
	void DrawDebugRotationAxis() const;
};
