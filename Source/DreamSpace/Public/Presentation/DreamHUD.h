#pragma once
#include "GameFramework/HUD.h"
#include "DreamHUD.generated.h"
/** 显示操作提示、准星、密码箱输入面板及物品获得提示；持有状态由角色保存。 */
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

	/** 光点实际拾取后显示手办解锁提示；输入正确但仍在开盖时不会调用。 */
	void ShowMiniatureAcquiredMessage();
	bool IsMiniatureAcquiredMessageVisible() const;

private:
	/** 中央文字的显示秒数，只影响表现，不影响角色是否持有钥匙。 */
	UPROPERTY(EditDefaultsOnly, Category = "提示|钥匙", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float KeyAcquiredMessageDuration = 3.0f;

	/** 当前提示的到期时间；HUD 随玩家销毁后自然清理，无需额外注册计时器。 */
	double KeyAcquiredMessageUntil = 0.0;

	/** 手办提示与钥匙提示分别计时，模式切换不会清除尚未到期的获得信息。 */
	UPROPERTY(EditDefaultsOnly, Category = "提示|手办", meta = (ClampMin = "0.1"))
	float MiniatureAcquiredMessageDuration = 3.0f;
	double MiniatureAcquiredMessageUntil = 0.0;
	/** 使用现有 Canvas 绘制居中的四格密码面板，保持主键盘/小键盘与实际输入缓冲一致。 */
	void DrawPasswordEntry(const class ADreamPlayerController* Controller);
};
