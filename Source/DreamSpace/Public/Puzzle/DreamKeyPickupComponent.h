#pragma once

#include "Components/ActorComponent.h"
#include "DreamInteractableInterface.h"
#include "DreamKeyPickupComponent.generated.h"

/**
 * 钥匙的单次拾取行为，复用控制器已有的 E 键射线和可交互组件接口。
 *
 * DreamDroppedKey 默认携带本组件。模型仍在拖动预览阶段时不允许拾取；
 * 只有物理掉落成功后才启用拾取和 Visibility 检测。交互成功后移除掉落 Actor，
 * 并将钥匙持有状态交给 DreamCharacter 保存，不在组件中创建另一份背包状态。
 */
UCLASS(
	BlueprintType, ClassGroup = (DreamPuzzle), meta = (BlueprintSpawnableComponent, DisplayName = "Dream Key Pickup"))
class DREAMSPACE_API UDreamKeyPickupComponent : public UActorComponent, public IDreamInteractableInterface
{
	GENERATED_BODY()

public:
	UDreamKeyPickupComponent();
	virtual void OnInteracted_Implementation(AActor* Interactor) override;

	/** 由掉落物在 ActivateDrop 成功后调用；预览阶段不会打开碰撞或授予钥匙。 */
	void EnablePickup();

	/** 供蓝图查询该对象是否已成为可拾取的钥匙；已消费的钥匙不能再次触发。 */
	UFUNCTION(BlueprintPure, Category = "物品|钥匙")
	bool IsPickupEnabled() const { return bPickupEnabled && !bConsumed; }

private:
	/** 初始关闭，避免直接调用交互接口时把尚未提交的拖动预览当成物品。 */
	bool bPickupEnabled = false;

	/** 销毁前先标记消费，阻止同一帧重复交互或销毁回调重入。 */
	bool bConsumed = false;
};
