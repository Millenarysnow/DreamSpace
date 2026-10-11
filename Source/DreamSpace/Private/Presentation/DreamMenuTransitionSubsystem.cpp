#include "DreamMenuTransitionSubsystem.h"

#include "DreamSpace.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"
#include "Rendering/DrawElements.h"
#include "UnrealClient.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/SLeafWidget.h"

namespace
{
/** 快照只做视觉接力，不接收按键或鼠标；纹理由所属 GameInstance 的反射引用保活。 */
class SDreamMenuTravelFrame final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamMenuTravelFrame) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UDreamMenuTransitionSubsystem>, Transition)
		SLATE_ARGUMENT(UTexture2D*, Texture)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args)
	{
		Transition = Args._Transition;
		Brush.SetResourceObject(Args._Texture);
		Brush.DrawAs = ESlateBrushDrawType::Image;
		SetVisibility(EVisibility::HitTestInvisible);
		ForceVolatile(true);
	}
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	// 透明度来自跨地图子系统而非 Slate 属性绑定。主动标记为每帧重绘，
	// 避免启用视口失效缓存时把第一帧的不透明快照一直缓存到入场结束。
	virtual bool ComputeVolatility() const override { return true; }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const override
	{
		const float Opacity = Transition.IsValid() ? Transition->GetSnapshotOpacity() : 0.0f;
		if (Opacity > 0.0f)
			FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(), &Brush,
				ESlateDrawEffect::None, FLinearColor(1.0f, 1.0f, 1.0f, Opacity));
		return Layer;
	}
private:
	TWeakObjectPtr<UDreamMenuTransitionSubsystem> Transition;
	FSlateBrush Brush;
};
}

float UDreamMenuTransitionSubsystem::Ease(float Alpha)
{
	const float T = FMath::Clamp(Alpha, 0.0f, 1.0f);
	return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f);
}

float UDreamMenuTransitionSubsystem::HorizontalFOV(const FMinimalViewInfo& View, float AspectRatio,
	EAspectRatioAxisConstraint DefaultConstraint)
{
	const EAspectRatioAxisConstraint Constraint = View.AspectRatioAxisConstraint.Get(DefaultConstraint);
	const float Aspect = FMath::Max(AspectRatio, 0.1f);
	const bool bMaintainX = Constraint == AspectRatio_MaintainXFOV
		|| (Constraint == AspectRatio_MajorAxisFOV && Aspect >= 1.0f);
	if (View.bConstrainAspectRatio || bMaintainX)
		return View.FOV;
	// UE5 以相机参考比例的水平角换算垂直角，保持垂直角后再按实际窗口比例计算水平角。
	const double Tangent = FMath::Tan(FMath::DegreesToRadians(View.FOV * 0.5));
	return FMath::RadiansToDegrees(2.0 * FMath::Atan(Tangent * Aspect / FMath::Max(View.AspectRatio, 0.1f)));
}

void UDreamMenuTransitionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 监听加载失败和意外进入其他地图，不能让快照永远挡住用户的游戏视口。
	PostLoadHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::HandlePostLoad);
	// 此委托发生在 Canvas / 场景命令已提交之后。只处理本 GameInstance 的视口，
	// 不会从编辑器预览窗口或另一个 PIE 实例误读画面。
	ViewportRenderedHandle = UGameViewportClient::OnViewportRendered().AddUObject(this, &ThisClass::HandleViewportRendered);
	if (GEngine)
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::HandleTravelFailure);
}

bool UDreamMenuTransitionSubsystem::PrepareTravel(APlayerController* Controller, const FString& TargetPackage,
	const FMinimalViewInfo& GameplayEntryView, float LandingDuration, float FrameBlendDuration)
{
	if (bPendingTravel || ArrivalWorld.IsValid() || !Controller || !Controller->IsLocalController()
		|| Controller->GetGameInstance() != GetGameInstance() || TargetPackage.IsEmpty())
		return false;
	EntryView = GameplayEntryView;
	DestinationPackage = TargetPackage;
	LandingTime = FMath::Max(0.0f, LandingDuration);
	FrameBlendTime = FMath::Max(0.0f, FrameBlendDuration);
	ArrivalElapsed = 0.0f;
	bPendingTravel = true;
	UGameViewportClient* Client = Controller->GetWorld()->GetGameViewport();
	OverlayViewport = Client;
	bWaitForRenderedArrival = FApp::CanEverRender() && Client && Client->Viewport;
	bArrivalFrameRendered = false;
	bTravelFrameCaptured = false;
	bTravelFrameReady = !bWaitForRenderedArrival;
	return true;
}

bool UDreamMenuTransitionSubsystem::ActivateTravelFrame()
{
	if (bTravelFrameReady)
		return true;
	if (!bTravelFrameCaptured)
		return false; // 当前视口尚未画出终点；等绘制委托读完，不按帧率猜测时序。
	if (!FrozenFrame || !OverlayViewport.IsValid())
	{
		// 读屏失败时仍允许开始游戏，避免在无效 RHI / 最小化窗口卡死在菜单。
		bTravelFrameReady = true;
		return true;
	}
	if (!FrameOverlay.IsValid())
	{
		FrameOverlay = SNew(SDreamMenuTravelFrame).Transition(this).Texture(FrozenFrame);
		// 仅在后续正常 PlayerTick 中改 Slate 树，避免从视口绘制回调内部增删绘制中的控件。
		OverlayViewport->AddViewportWidgetContent(FrameOverlay.ToSharedRef(), 100);
	}
	bTravelFrameReady = true;
	return false; // 留一帧让完全不透明的快照真实显示，再开始同步加载。
}

void UDreamMenuTransitionSubsystem::HandleViewportRendered(FViewport* Viewport)
{
	UGameViewportClient* Client = OverlayViewport.Get();
	if (!Client || Client->Viewport != Viewport)
		return;
	UWorld* RenderedWorld = Client->GetWorld();
	if (!RenderedWorld || RenderedWorld->GetGameInstance() != GetGameInstance())
		return;
	if (bPendingTravel && !bTravelFrameCaptured && !bTravelFrameReady && !IsArrivalForWorld(RenderedWorld))
		CaptureTravelFrame(Viewport);
	if (IsArrivalRunning(RenderedWorld))
		bArrivalFrameRendered = true;
}

void UDreamMenuTransitionSubsystem::CaptureTravelFrame(FViewport* Viewport)
{
	FIntPoint Size = Viewport ? Viewport->GetSizeXY() : FIntPoint::ZeroValue;
	TArray<FColor> Pixels;
	// 此时当前天台视点已经用于真正的场景 Draw。ReadPixels 会等待已提交的 GPU 命令，
	// 从当前 3D 渲染目标取得完整画面；无需在 PlayerTick 中嵌套强制刷新 Slate。
	// Logo 在飞入初段已经退场，快照只需包含场景，按整个视口等比展示即可。
	const bool bCaptured = Viewport && Size.X > 0 && Size.Y > 0 && Viewport->ReadPixels(Pixels);
	if (bCaptured && Size.X > 0 && Size.Y > 0 && Pixels.Num() == Size.X * Size.Y)
	{
		// UI 纹理保留屏幕的 sRGB 值。读取的场景缓冲 Alpha 可能为 0，必须改成不透明，
		// 否则切关后的第一帧会从“透明快照”中漏出尚未初始化的相机。
		for (FColor& Pixel : Pixels)
			Pixel.A = 255;
		FrozenFrame = UTexture2D::CreateTransient(Size.X, Size.Y, PF_B8G8R8A8);
		FrozenFrame->SRGB = true;
		FrozenFrame->NeverStream = true;
		FrozenFrame->Filter = TF_Bilinear;
		FTexture2DMipMap& Mip = FrozenFrame->GetPlatformData()->Mips[0];
		void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		FrozenFrame->UpdateResource();
		bTravelFrameCaptured = true;
	}
	else
	{
		// 无渲染的自动化 / 服务器可跳过快照；相机数据依然可以正确接力。
		UE_LOG(LogDreamSpace, Warning, TEXT("[开始过渡] 未能读取菜单最后一帧，继续执行镜头接力。"));
		bTravelFrameReady = true;
	}
}

bool UDreamMenuTransitionSubsystem::IsArrivalForWorld(const UWorld* World) const
{
	return World && World->GetGameInstance() == GetGameInstance()
		&& (bPendingTravel || ArrivalWorld.IsValid())
		&& UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) == DestinationPackage;
}

bool UDreamMenuTransitionSubsystem::IsArrivalRunning(const UWorld* World) const
{
	return IsArrivalForWorld(World) && ArrivalWorld.Get() == World;
}

void UDreamMenuTransitionSubsystem::AdvanceArrival(UWorld* World, float DeltaSeconds)
{
	if (!IsArrivalForWorld(World))
		return;
	if (bPendingTravel)
	{
		bPendingTravel = false;
		ArrivalWorld = World;
		// 第一帧不累计时间，保证新世界起点严格等于菜单传来的视点。
		UE_LOG(LogDreamSpace, Log, TEXT("[开始过渡] 天台视点接续，开始落到实际玩家相机。"));
		return;
	}
	// OpenLevel 后引擎可能暂时关闭场景渲染。相机缓存已经有效也不能说明屏幕有效，
	// 在真正画过 TEST 之前始终保持天台视点与完全不透明的快照。
	if (bWaitForRenderedArrival && (!bArrivalFrameRendered || !FViewport::IsGameRenderingEnabled()))
		return;
	// 每个真正画出的视口帧只允许推进一次。首次材质 / 纹理上传或窗口暂时不绘制时，
	// 不能让数次相机更新在屏幕仍停留旧画面的间隙完成整段交叉淡化。
	bArrivalFrameRendered = false;
	if (FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0.0f && !World->IsPaused())
	{
		// 真实渲染时将单次长卡顿最多计为 0.1 秒；正常 10～120 FPS 按游戏时间走。
		// 卡顿期间多保持一会儿天台，比下一帧突然跳过尚未看见的淡化 / 落镜头更自然。
		const float VisibleDelta = bWaitForRenderedArrival ? FMath::Min(DeltaSeconds, 0.1f) : DeltaSeconds;
		ArrivalElapsed = FMath::Min(FrameBlendTime + LandingTime, ArrivalElapsed + VisibleDelta);
	}
	if (FrameOverlay.IsValid())
	{
		// 显式失效绘制缓存；跨地图后父视口可能重建全局失效根，仅靠 Volatility 不足以
		// 保证旧控件这一帧重绘。快照淡完立刻移除，余下落镜头完全显示真实世界。
		FrameOverlay->Invalidate(EInvalidateWidgetReason::Paint);
		if (GetSnapshotOpacity() <= 0.0f)
			RemoveFrameOverlay();
	}
}

float UDreamMenuTransitionSubsystem::GetCameraBlendAlpha() const
{
	return LandingTime > 0.0f ? Ease((ArrivalElapsed - FrameBlendTime) / LandingTime)
		: (ArrivalElapsed >= FrameBlendTime ? 1.0f : 0.0f);
}

float UDreamMenuTransitionSubsystem::GetStyleBlendAlpha() const
{
	// 颜色与紙纹在快照消失后恢复，既保留天台线稿，又避免交接时跳出一块彩色贴图。
	return GetCameraBlendAlpha();
}

float UDreamMenuTransitionSubsystem::GetSnapshotOpacity() const
{
	if (bPendingTravel)
		return 1.0f;
	if (!ArrivalWorld.IsValid())
		return 0.0f;
	return FrameBlendTime > 0.0f ? 1.0f - Ease(ArrivalElapsed / FrameBlendTime) : 0.0f;
}

void UDreamMenuTransitionSubsystem::RemoveFrameOverlay()
{
	if (FrameOverlay.IsValid() && OverlayViewport.IsValid())
		OverlayViewport->RemoveViewportWidgetContent(FrameOverlay.ToSharedRef());
	FrameOverlay.Reset();
	FrozenFrame = nullptr;
}

void UDreamMenuTransitionSubsystem::CompleteArrival()
{
	RemoveFrameOverlay();
	bPendingTravel = false;
	bTravelFrameReady = false;
	bTravelFrameCaptured = false;
	bWaitForRenderedArrival = false;
	bArrivalFrameRendered = false;
	OverlayViewport.Reset();
	ArrivalWorld.Reset();
	DestinationPackage.Reset();
}

void UDreamMenuTransitionSubsystem::HandlePostLoad(UWorld* World)
{
	// 其他 PIE 的广播不属于本实例。加载失败的 nullptr 则只清理本实例尚未开始的请求。
	if (!World)
	{
		if (bPendingTravel)
			CompleteArrival();
	}
	else if (World->GetGameInstance() == GetGameInstance() && !IsArrivalForWorld(World))
		CompleteArrival();
}

void UDreamMenuTransitionSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type, const FString& Error)
{
	if (World && World->GetGameInstance() == GetGameInstance())
	{
		UE_LOG(LogDreamSpace, Error, TEXT("[开始过渡] 切关失败，释放天台快照：%s"), *Error);
		CompleteArrival();
	}
}

void UDreamMenuTransitionSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
	UGameViewportClient::OnViewportRendered().Remove(ViewportRenderedHandle);
	if (GEngine)
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	CompleteArrival();
	Super::Deinitialize();
}
