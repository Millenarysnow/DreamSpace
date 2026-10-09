#pragma once

#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "DreamInteractableInterface.h"
#include "DreamMiniatureExtractableComponent.generated.h"

class ADreamDroppedItem;
class UPrimitiveComponent;
class UStaticMeshComponent;

/**
 * 给房间中的静态网格 Actor 添加“从手办取出”的持续交互。
 *
 * 只有控制器在手办模式命中本 Actor 后才会开始取出。按住左键时源 Actor 暂时隐藏，
 * 一个缩小的掉落物预览跟随鼠标在显示面所在平面移动；越过显示面边缘并松开左键才提交。
 * 在面内松开、切换模式、停用组件或失去角色均取消操作，并恢复源 Actor 的可见性与碰撞。
 * 成功提交时移除源 Actor，预览物成为开启物理模拟的掉落物；每个源 Actor 只允许取出一次。
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (DreamPuzzle),
	meta = (BlueprintSpawnableComponent, DisplayName = "Dream Miniature Extractable"))
class DREAMSPACE_API UDreamMiniatureExtractableComponent : public UActorComponent, public IDreamInteractableInterface
{
	GENERATED_BODY()

public:
	UDreamMiniatureExtractableComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void Deactivate() override;
	virtual void TickComponent(
		float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnInteracted_Implementation(AActor* Interactor) override;

	/** 从已经命中的房间模型开始；InitialDisplayPoint 是鼠标在手办显示面上的世界交点。 */
	UFUNCTION(BlueprintCallable, Category = "手办取出")
	bool BeginExtract(AActor* Interactor, UPrimitiveComponent* HitComponent, const FVector& InitialDisplayPoint,
		const FVector& DisplayFrontNormal);

	/** 鼠标在显示面的无限延伸平面上的位置；UV 允许超出 0..1，以判定是否已拖出。 */
	UFUNCTION(BlueprintCallable, Category = "手办取出")
	void UpdateExtract(const FVector& DisplayPoint, const FVector2D& UnclampedUV, const FVector& DisplayFrontNormal);

	/** bTryDrop 仅表示正常松开；真正成功还要求最后的 UV 越过边缘阈值。 */
	UFUNCTION(BlueprintCallable, Category = "手办取出")
	bool EndExtract(bool bTryDrop);

	/** 尚未提交的持续抓取状态；控制器据此维持成对的输入锁。 */
	UFUNCTION(BlueprintPure, Category = "手办取出")
	bool IsExtracting() const { return bExtracting; }

	/** 成功后保持已取出状态；保留隐藏源 Actor 的配置也不能再次生成相同掉落物。 */
	UFUNCTION(BlueprintPure, Category = "手办取出")
	bool IsExtracted() const { return bExtracted; }

	/** 默认掉落物会触发物理；蓝图子类可在 OnDroppedFromMiniature 中扩展拾取玩法。 */
	UFUNCTION(BlueprintPure, Category = "手办取出")
	ADreamDroppedItem* GetPreviewItem() const { return PreviewItem.Get(); }

private:
	/** 优先使用指定网格；留空时使用点击命中的静态网格，否则取所属 Actor 的第一个静态网格。 */
	UPROPERTY(EditAnywhere, Category = "手办取出|模型",
		meta = (UseComponentPicker = true, AllowedClasses = "/Script/Engine.StaticMeshComponent",
			DisplayName = "源静态网格组件"))
	FComponentReference SourceMeshComponent;

	/** 可指定 ADreamDroppedItem 的蓝图子类，实现项目自己的拾取、音效和特效。 */
	UPROPERTY(EditAnywhere, Category = "手办取出|掉落物")
	TSubclassOf<ADreamDroppedItem> DropActorClass;

	/** 掉落物和拖动预览相对于房间原模型世界尺寸的比例；例如 0.1 表示十分之一。 */
	UPROPERTY(EditAnywhere, Category = "手办取出|掉落物",
		meta = (ClampMin = "0.001", UIMin = "0.001", DisplayName = "取出模型比例"))
	float DropScaleMultiplier = 0.1f;

	/** 鼠标 UV 超过显示边缘的距离；0.02 约为显示面宽度的 2%，减少边缘误触。 */
	UPROPERTY(EditAnywhere, Category = "手办取出|输入",
		meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "拖出边缘阈值"))
	float ExitMarginUV = 0.02f;

	/** 掉落物与显示面之间额外保留的间距，避免球体刚启用时嵌入手办。 */
	UPROPERTY(EditAnywhere, Category = "手办取出|掉落物",
		meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "显示面前方间距（厘米）"))
	float SurfaceClearance = 8.0f;

	/**
	 * 默认成功后销毁整个源 Actor。关闭时源 Actor 保留为隐藏、无碰撞且已取出的对象，
	 * 适用于关卡脚本仍持有它的引用。无论哪种配置，都应把模型放在独立 Actor 上。
	 */
	UPROPERTY(EditAnywhere, Category = "手办取出|模型", meta = (DisplayName = "成功后销毁源 Actor"))
	bool bDestroySourceActor = true;

	/** 预览持有强引用，操作者持有弱引用，角色销毁后会自动取消。 */
	UPROPERTY(Transient)
	TObjectPtr<ADreamDroppedItem> PreviewItem;
	TWeakObjectPtr<AActor> ExtractInteractor;
	bool bExtracting = false;
	bool bExtracted = false;
	/** 最后一次有效 UV 是否越过边缘阈值；拖回面内会立即清除此标志。 */
	bool bOutsideDisplay = false;
	/** 抓取前源 Actor 的状态，仅在取消时恢复；成功后不再恢复。 */
	bool bSourceWasHidden = false;
	bool bSourceHadCollision = true;

	UStaticMeshComponent* ResolveSourceMesh(UPrimitiveComponent* HitComponent) const;
};
