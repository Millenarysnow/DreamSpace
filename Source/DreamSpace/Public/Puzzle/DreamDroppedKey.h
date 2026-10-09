#pragma once

#include "DreamDroppedItem.h"
#include "DreamDroppedKey.generated.h"

class UDreamKeyPickupComponent;

/**
 * 从手办取出的钥匙掉落物。
 * 将取出组件的 DropActorClass 设为本类即可启用 E 拾取；模型、缩放和物理仍由父类处理。
 * 钥匙身份由掉落物类明确指定，不根据网格名称或 Actor 名称猜测其它物体是不是钥匙。
 */
UCLASS(Blueprintable, BlueprintType)
class DREAMSPACE_API ADreamDroppedKey : public ADreamDroppedItem
{
	GENERATED_BODY()

public:
	ADreamDroppedKey();

	/** 必须先成功激活父类的物理掉落，再开放拾取；取消预览不会授予角色钥匙。 */
	virtual bool ActivateDrop() override;

private:
	/** 原生默认组件，掉落物生成后自动拥有 E 交互行为，无需为每个钥匙编写蓝图逻辑。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "物品|钥匙", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDreamKeyPickupComponent> KeyPickup;
};
