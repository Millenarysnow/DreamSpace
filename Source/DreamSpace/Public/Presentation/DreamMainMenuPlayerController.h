#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DreamMainMenuPlayerController.generated.h"

class ADreamMainMenuScene;
class SWidget;

/**
 * 菜单专用输入：全屏透明点击区域，不显示按钮或“点击开始”文字。
 * 鼠标、触摸与确认键都进入同一入口；先淡出整张纸，再一次性打开游玩关卡。
 */
UCLASS()
class DREAMSPACE_API ADreamMainMenuPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	ADreamMainMenuPlayerController();
	virtual void PlayerTick(float DeltaTime) override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	virtual void ReceivedPlayer() override;

	/** 开始请求是幂等的，淡出期间连续点击不会重复切换地图。 */
	UFUNCTION(BlueprintCallable, Category = "开始界面")
	void RequestStartGame();

	UFUNCTION(BlueprintPure, Category = "开始界面")
	bool IsStartingGame() const { return bStartingGame; }

	/** 供 Slate 绘制 Logo 和纸白遮罩；返回值范围为 0～1。 */
	float GetPaperFadeOpacity() const;
	ADreamMainMenuScene* GetMenuScene() const { return MenuScene.Get(); }
	static bool IsStartKey(const FKey& Key);

	/** 软关卡引用保留详情面板选择器；DefaultGame.ini 明确把此动态跳转目标加入烘焙。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "开始界面|流程")
	TSoftObjectPtr<UWorld> GameplayMap;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "开始界面|流程", meta = (ClampMin = "0.0", Units = "s"))
	float FadeInDuration = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "开始界面|流程", meta = (ClampMin = "0.0", Units = "s"))
	float FadeOutDuration = 0.65f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<ADreamMainMenuScene> MenuScene;

	/** Slate 不是 UObject；销毁控制器前必须从 LocalPlayer 的视口层移除它。 */
	TSharedPtr<SWidget> MenuOverlay;
	float VisibleTime = 0.0f;
	float TransitionTime = 0.0f;
	float TransitionStartOpacity = 0.0f;
	bool bStartingGame = false;
	bool bTravelRequested = false;

	void CreateMenuOverlay();
};
