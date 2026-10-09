#pragma once
#include "GameFramework/HUD.h"
#include "DreamHUD.generated.h"
/** 显示基础操作提示、准星及短暂的钥匙获得提示；钥匙持有状态由角色保存。 */
UCLASS()
class DREAMSPACE_API ADreamHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;

	/** 成功拾取时调用；再次获得其它钥匙会从本次拾取重新计算提示时长。 */
	void ShowKeyAcquiredMessage();

	/** 按真实经过时间判断提示是否可见，不依赖 Tick，也不会因切换手办模式而永久残留。 */
	bool IsKeyAcquiredMessageVisible() const;

private:
	/** 中央文字的显示秒数，只影响表现，不影响角色是否持有钥匙。 */
	UPROPERTY(EditDefaultsOnly, Category = "提示|钥匙", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float KeyAcquiredMessageDuration = 3.0f;

	/** 当前提示的到期时间；HUD 随玩家销毁后自然清理，无需额外注册计时器。 */
	double KeyAcquiredMessageUntil = 0.0;
};
