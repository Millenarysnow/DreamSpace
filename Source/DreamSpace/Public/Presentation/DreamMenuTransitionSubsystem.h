#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DreamMenuTransitionSubsystem.generated.h"

class APlayerController;
class SWidget;
class UGameViewportClient;
class UTexture2D;
class FViewport;

/**
 * 菜单飞入的跨地图接力。GameInstance 不随 OpenLevel 销毁，因此可以同时保存
 * 天台的最后一帧和转换后的真实世界视点，直到新地图的相机完成接管。
 * 每场 PIE 各有一份；直接运行 TEST 没有请求时，完全走原来的游玩相机与输入。
 */
UCLASS()
class DREAMSPACE_API UDreamMenuTransitionSubsystem : public UGameInstanceSubsystem
{

	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * 在菜单镜头已到达天台后请求；真正读屏在该视口下一次完成场景绘制时执行。
	 * 屏幕截图是运行时瞬时纹理，不写文件、不修改资产；持久 Slate 层在加载期间显示它。
	 * LandingDuration 只计算新世界中的落镜头时间，加载耗时不会吃掉这段动画。
	 */
	bool PrepareTravel(APlayerController* Controller, const FString& TargetPackage,
		const FMinimalViewInfo& GameplayEntryView, float LandingDuration, float FrameBlendDuration);
	/** 在普通 PlayerTick 把已读出的末帧装到视口；返回 true 表示安全切图。 */
	bool ActivateTravelFrame();

	/** 新地图已有真实 Pawn / FollowCamera 时调用；重复相机更新不会重置动画。 */
	void AdvanceArrival(UWorld* World, float DeltaSeconds);

	/** 包括等待新 Pawn 的阶段，用于冻结探索输入、隐藏 HUD，防止首帧闪现或误操作。 */
	bool IsArrivalForWorld(const UWorld* World) const;
	bool IsArrivalRunning(const UWorld* World) const;

	/** 先在相同视点交叉淡化两张场景，再飞到玩家；交叉淡化时不移动镜头，避免双重轮廓。 */
	float GetCameraBlendAlpha() const;
	float GetStyleBlendAlpha() const;
	float GetSnapshotOpacity() const;
	const FMinimalViewInfo& GetEntryView() const { return EntryView; }

	/** 相机在当前帧已经精确落到真实玩家 POV 后调用；释放快照、Slate 层与本次请求。 */
	void CompleteArrival();

	/** 连续缓入缓出，起止速度和加速度均为零；多种帧率只影响采样密度。 */
	static float Ease(float Alpha);

	/** 将 UE 的视角约定转换成当前窗口的水平 FOV，接续两台轴约束不同的相机时保持投影连续。 */
	static float HorizontalFOV(const FMinimalViewInfo& View, float AspectRatio,
		EAspectRatioAxisConstraint DefaultConstraint);

private:
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FrozenFrame;
	UPROPERTY(Transient)
	FMinimalViewInfo EntryView;
	TWeakObjectPtr<UGameViewportClient> OverlayViewport;
	TWeakObjectPtr<UWorld> ArrivalWorld;
	TSharedPtr<SWidget> FrameOverlay;
	FString DestinationPackage;
	float LandingTime = 1.75f;
	float FrameBlendTime = 0.55f;
	float ArrivalElapsed = 0.0f;
	bool bPendingTravel = false;
	bool bTravelFrameReady = false;
	bool bTravelFrameCaptured = false;
	bool bWaitForRenderedArrival = false;
	bool bArrivalFrameRendered = false;
	FDelegateHandle PostLoadHandle;
	FDelegateHandle TravelFailureHandle;
	FDelegateHandle ViewportRenderedHandle;

	void HandleViewportRendered(FViewport* Viewport);
	void CaptureTravelFrame(FViewport* Viewport);
	void HandlePostLoad(UWorld* World);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error);
	void RemoveFrameOverlay();
	friend class FDreamMenuTransitionLifecycleTest;
};
