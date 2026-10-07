#pragma once

#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "DreamInteractableInterface.h"
#include "DreamRotatableComponent.h"
#include "DreamTranslatableComponent.generated.h"

class UDreamPivotPointComponent;

/**
 * 可平移组件：允许所属 Actor 沿枢轴点局部坐标系的一根轴，在有限范围内往复平移。
 *
 * 设计约定：
 * - 枢轴点组件负责提供方向。本组件读取枢轴点的局部 X/Y/Z 轴，并转换成世界方向；
 * - 默认以 Actor 开始游戏时的位置作为范围零点，负向范围和正向范围分别位于零点两侧；
 * - 也可以将范围零点改为枢轴点开始游戏时的位置，适合需要把多个移动件对齐到同一基准点的机关；
 * - 每次收到交互后沿当前方向移动一个步长；到达端点后，默认在下一次交互时自动反向；
 * - 平移只改变 Actor 的位置，不改变它的旋转和缩放。
 *
 * 范围、步长、时长等配置均为 EditAnywhere，因此既能设置蓝图默认值，也能直接在关卡实例上覆盖。
 * 控制台输入 dream.DebugPivots 1 后，会逐帧绘制负向范围（红）、正向范围（绿）、
 * 两侧端点以及 Actor 的当前位置；输入 dream.DebugPivots 0 关闭。
 */
UCLASS(
	Blueprintable,
	BlueprintType,
	ClassGroup = (DreamPuzzle),
	meta = (BlueprintSpawnableComponent, DisplayName = "Dream Translatable"))
class DREAMSPACE_API UDreamTranslatableComponent : public UActorComponent, public IDreamInteractableInterface
{
	GENERATED_BODY()

public:
	UDreamTranslatableComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(
		float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	//~ IDreamInteractableInterface：玩家对所属 Actor 按下交互键时触发一次平移。
	virtual void OnInteracted_Implementation(AActor* Interactor) override;

	/**
	 * 按当前配置触发一次平移。
	 * 正在平移时再次调用会被忽略，防止多段插值同时修改 Actor 位置。
	 */
	UFUNCTION(BlueprintCallable, Category = "可平移")
	void TriggerTranslation();

	/** 当前是否正在执行平移动画。 */
	UFUNCTION(BlueprintPure, Category = "可平移")
	bool IsTranslating() const { return bTranslating; }

	/**
	 * Actor 当前位于范围轴上的逻辑坐标（厘米）。
	 * 0 为范围零点，负值位于所选轴的反方向，正值位于所选轴的正方向。
	 */
	UFUNCTION(BlueprintPure, Category = "可平移")
	float GetCurrentTranslation() const { return CurrentTranslation; }

private:
	// ---------- 策划配置 ----------

	/**
	 * 提供平移方向的枢轴点组件。
	 * 编辑器组件选择器只列出同一 Actor 上的 UDreamPivotPointComponent；
	 * 留空时会自动使用所属 Actor 上找到的第一个枢轴点组件。
	 */
	UPROPERTY(EditAnywhere, Category = "可平移|枢轴",
		meta = (UseComponentPicker = true,
			AllowedClasses = "/Script/DreamSpace.DreamPivotPointComponent",
			DisplayName = "枢轴点组件"))
	FComponentReference PivotComponent;

	/**
	 * 按组件名称选择枢轴点的备用方式。
	 * 一般应使用上面的组件选择器；仅当引用留空且同一 Actor 上有多个枢轴点时才需要填写。
	 */
	UPROPERTY(EditAnywhere, Category = "可平移|枢轴", meta = (AdvancedDisplay, DisplayName = "枢轴点组件名称（备用）"))
	FName PivotComponentName;

	/**
	 * 平移所使用的枢轴点局部轴。
	 * X/Y/Z 分别对应枢轴调试图中的红轴、绿轴和蓝轴。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "可平移|平移",
		meta = (AllowPrivateAccess = "true", DisplayName = "平移轴"))
	EDreamPivotRotationAxis TranslationAxis = EDreamPivotRotationAxis::X;

	/**
	 * 所选轴负方向允许移动的最大距离（厘米）。
	 * 这是实例可编辑属性；设置为 0 表示不允许 Actor 进入负方向。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "可平移|范围",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", UIMin = "0.0", DisplayName = "负向范围（厘米）"))
	float NegativeLimit = 100.0f;

	/**
	 * 所选轴正方向允许移动的最大距离（厘米）。
	 * 这是实例可编辑属性；设置为 0 表示不允许 Actor 进入正方向。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "可平移|范围",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", UIMin = "0.0", DisplayName = "正向范围（厘米）"))
	float PositiveLimit = 100.0f;

	/**
	 * 是否使用枢轴点作为范围零点。
	 * 关闭时以 Actor 开始游戏时的位置为零点；打开时以枢轴点开始游戏时的位置为零点。
	 * 无论选择哪一种，零点都会在 BeginPlay 固定，Actor 移动后范围不会跟着自己漂移。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "可平移|范围",
		meta = (AllowPrivateAccess = "true", DisplayName = "范围以枢轴点为零点"))
	bool bUsePivotAsRangeOrigin = false;

	/**
	 * 每次交互尝试移动的距离（厘米）。
	 * 如果剩余距离小于此值，会自动截断到端点，保证 Actor 永远不会越出配置范围。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "可平移|平移",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", UIMin = "0.0", DisplayName = "每次交互步长（厘米）"))
	float StepDistance = 100.0f;

	/** 单次平移的耗时（秒）；设置为 0 时会在触发瞬间到位。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "可平移|平移",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", UIMin = "0.0", DisplayName = "平移时长（秒）"))
	float TranslationDuration = 0.5f;

	/**
	 * 开启后，平移会检查所属 Actor 上随根一起移动的查询碰撞组件。
	 * 任意一段路程受阻，本次交互就失败并弹回起点；默认关闭以兼容已有机关。
	 * 网格或碰撞体需要启用 Query 碰撞，并与障碍物互相设置为 Block。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "可平移|碰撞",
		meta = (AllowPrivateAccess = "true", DisplayName = "运动考虑碰撞"))
	bool bConsiderCollision = false;

	/**
	 * 到达正向或负向端点后，下一次交互是否改为向另一端移动。
	 * 关闭后会一直保持当前方向；到达该方向端点后，后续触发不会产生位移。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "可平移|平移",
		meta = (AllowPrivateAccess = "true", DisplayName = "到达端点后反向"))
	bool bReverseAtLimits = true;

	/** 调试图中两侧端点球的半径（厘米）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "调试",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", UIMin = "0.0", DisplayName = "平移端点标记半径"))
	float DebugEndpointRadius = 7.0f;

	/** 调试图中正负范围线的粗细。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "调试",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", UIMin = "0.0", DisplayName = "平移范围线宽"))
	float DebugRangeThickness = 3.0f;

	// ---------- 运行时状态 ----------

	/** 在 BeginPlay 解析出的枢轴点；引用失效时会在下一次触发或调试绘制时重新解析。 */
	UPROPERTY(Transient)
	TObjectPtr<UDreamPivotPointComponent> CachedPivot;

	/** 范围零点的世界位置。它在 BeginPlay 固定，避免范围随被移动的 Actor 一同漂移。 */
	FVector RangeOriginWorld = FVector::ZeroVector;

	/** 是否已经建立范围零点和初始逻辑坐标。 */
	bool bRangeInitialized = false;

	/**
	 * Actor 当前沿平移轴的逻辑坐标。
	 * 正常运动时位于配置范围内；若关卡初始摆放越界，会先保留真实坐标并在首次交互时回到最近端点。
	 */
	float CurrentTranslation = 0.0f;

	/** 本次动画开始时的逻辑坐标，用来让蓝图查询值与画面插值保持同步。 */
	float StartTranslation = 0.0f;

	/** 本次动画完成后应到达的逻辑坐标。 */
	float TargetTranslation = 0.0f;

	/** 下一次平移的方向：1 表示轴正方向，-1 表示轴负方向。 */
	float TranslationDirection = 1.0f;

	/** 是否正在执行平移动画。 */
	bool bTranslating = false;

	/** 本次平移动画已经过的时间（秒）。 */
	float TranslationElapsed = 0.0f;

	/** 受阻后正沿原路径回弹；此时仍处于平移状态，不能接收下一次交互。 */
	bool bReturning = false;
	/** 回弹的起始世界位置、逻辑坐标与已过时间。 */
	FVector ReturnStartLocation = FVector::ZeroVector;
	float ReturnStartTranslation = 0.0f;
	float ReturnElapsed = 0.0f;
	float ReturnDuration = 0.0f;
	/** 下次运动方向在计算目标时可能被端点逻辑改写，失败时必须回滚这个决定。 */
	float StartTranslationDirection = 1.0f;

	/** 本次动画触发瞬间 Actor 的世界位置。 */
	FVector StartActorLocation = FVector::ZeroVector;

	/** 本次动画最终要到达的世界位置。 */
	FVector TargetActorLocation = FVector::ZeroVector;

	/** 本次动画使用的世界轴方向；触发时快照，避免插值途中枢轴旋转导致路径弯折。 */
	FVector ActiveAxisWorld = FVector::ForwardVector;

	// ---------- 内部辅助函数 ----------

	/** 按“组件引用 > 备用名称 > 第一个枢轴点”的优先级解析枢轴点组件。 */
	UDreamPivotPointComponent* ResolvePivot() const;

	/** 读取枢轴点局部 X/Y/Z 中配置轴的世界单位方向。 */
	FVector GetTranslationAxisWorldDir(const UDreamPivotPointComponent* Pivot) const;

	/**
	 * 建立范围零点和初始逻辑坐标。
	 * 正常情况下在 BeginPlay 调用；若运行时动态创建组件，也会在第一次触发时补做初始化。
	 */
	void InitializeRange();

	/** 根据当前方向和步长计算下一次交互的目标逻辑坐标。 */
	float CalculateNextTranslation();

	/** 将运行状态精确收束到本次目标，避免浮点插值留下细小误差。 */
	void CompleteTranslation();

	/** 从受阻处开始返回本次起点，并恢复逻辑坐标与运动方向。 */
	void BeginReturn();

	/** 调试开关打开时，逐帧绘制正负范围、端点、零点和 Actor 当前所在位置。 */
	void DrawDebugTranslationRange() const;
};
