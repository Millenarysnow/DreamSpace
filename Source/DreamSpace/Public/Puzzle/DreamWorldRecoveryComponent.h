#pragma once

#include "Components/ActorComponent.h"
#include "DreamInteractableInterface.h"
#include "DreamWorldRecoveryComponent.generated.h"

/** 请求被相机接受后通知蓝图；可用于机关音效、灯光或触发后隐藏模型。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamWorldRecoveryTriggered, AActor*, Interactor);

/**
 * 世界恢复交互组件：挂在任意 Actor 上，交互后逐渐扩大玩家附近的正常渲染范围。
 *
 * 复用 IDreamInteractableInterface，普通探索的 E 键和手办内的左键都能触发，
 * 不需要新输入资产、枢轴、Timeline 或特殊 Actor 父类。范围仍以实际被控制的 Pawn
 * 为球心，而不是所属机关或镜头；父 Actor 自己实现交互接口时，应在它的接口里显式调用本组件。
 *
 * 组件只提交一次动画请求，插值和结果由 DreamProximitySketchCameraManager 保存。
 * 因而所属 Actor 触发后被隐藏、销毁或随流送关卡卸载，都不会中断已经开始的恢复。
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (DreamPuzzle),
	meta = (BlueprintSpawnableComponent, DisplayName = "Dream World Recovery"))
class DREAMSPACE_API UDreamWorldRecoveryComponent : public UActorComponent, public IDreamInteractableInterface
{
	GENERATED_BODY()

public:
	UDreamWorldRecoveryComponent();
	virtual void OnInteracted_Implementation(AActor* Interactor) override;

	/**
	 * 恢复终点的正常渲染半径（厘米）；默认 10000 cm = 100 m。
	 * 从触发时的实际半径开始平滑扩大，不会把已恢复的范围缩小。
	 * 关闭“恢复整个世界”后，可为多个机关依次设置 1500 / 3000 / 6000 cm 等阶段目标。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "世界恢复|范围",
		meta = (ClampMin = "0.0", Units = "cm", DisplayName = "目标正常渲染半径"))
	float TargetNormalRadius = 10000.0f;

	/** 动画时长（游戏时间，秒）；0 立即完成。正在执行的动画采用触发时的参数快照。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "世界恢复|范围",
		meta = (ClampMin = "0.0", Units = "s", DisplayName = "恢复时长"))
	float RecoveryDuration = 5.0f;

	/**
	 * 开启时，最后四分之一时段同时淡出剩余线稿，完成后包含天空、远景在内的整个世界正常渲染。
	 * 关闭时只扩大球形正常区域，完成后保留目标半径之外的线稿，适合分阶段解锁世界。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "世界恢复|范围", meta = (DisplayName = "恢复整个世界"))
	bool bRestoreEntireWorld = true;

	/**
	 * 默认每个组件实例只成功触发一次。无效操作者、组件停用、没有新恢复目标等失败请求不消耗次数。
	 * 关闭后仍不会重复重启动画；只有更大的半径或从阶段扩张升级为全世界恢复才会被相机接受。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "世界恢复|交互", meta = (DisplayName = "仅触发一次"))
	bool bTriggerOnce = true;

	/**
	 * 蓝图直接触发入口。Interactor 必须是同一世界内、由本地 PlayerController 当前占有的 Pawn。
	 * 返回 true 表示请求已提交；相机持续更新动画，本组件无需 Tick，也不锁定玩家移动或视角。
	 */
	UFUNCTION(BlueprintCallable, Category = "世界恢复", meta = (DisplayName = "触发世界恢复"))
	bool TriggerWorldRecovery(AActor* Interactor);

	/** 是否至少成功触发过一次；只查询本组件，不把其它机关的恢复当成本组件已经使用。 */
	UFUNCTION(BlueprintPure, Category = "世界恢复", meta = (DisplayName = "已触发世界恢复"))
	bool HasTriggeredWorldRecovery() const { return bHasTriggered; }

	/** 成功接受请求时触发；此时可以安全销毁机关，恢复动画仍在玩家相机上继续。 */
	UPROPERTY(BlueprintAssignable, Category = "世界恢复", meta = (DisplayName = "世界恢复已触发"))
	FDreamWorldRecoveryTriggered OnWorldRecoveryTriggered;

private:
	/** PIE / 关卡实例运行时的单次交互标记，不保存到蓝图默认值或地图。 */
	bool bHasTriggered = false;
};
