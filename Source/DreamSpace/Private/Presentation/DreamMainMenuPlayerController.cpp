#include "DreamMainMenuPlayerController.h"

#include "DreamMainMenuScene.h"
#include "DreamSpace.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

namespace
{
/**
 * 唯一一个全屏 Slate 元素：绘制 Logo 并接收任意位置的点击。
 * 没有按钮的悬停/按下状态，避免安静的建筑画面出现普通菜单控件的视觉反馈。
 */
class SDreamMainMenuOverlay final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamMainMenuOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ADreamMainMenuPlayerController>, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Controller = InArgs._Controller;
		if (const ADreamMainMenuScene* Scene = Controller.IsValid() ? Controller->GetMenuScene() : nullptr)
		{
			LogoBrush.SetResourceObject(Scene->LogoTexture);
			if (Scene->LogoTexture)
				LogoBrush.ImageSize = FVector2D(Scene->LogoTexture->GetSizeX(), Scene->LogoTexture->GetSizeY());
			LogoBrush.DrawAs = ESlateBrushDrawType::Image;
		}
	}

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
	{
		return ADreamMainMenuPlayerController::IsStartKey(Event.GetEffectingButton()) ? Start() : FReply::Unhandled();
	}
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		return OnMouseButtonDown(Geometry, Event);
	}
	virtual FReply OnTouchStarted(const FGeometry&, const FPointerEvent&) override { return Start(); }
	virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& Event) override
	{
		return !Event.IsRepeat() && ADreamMainMenuPlayerController::IsStartKey(Event.GetKey())
			? Start() : FReply::Unhandled();
	}

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const override
	{
		if (!Controller.IsValid())
			return Layer;
		const FVector2D Size = Geometry.GetLocalSize();
		if (Size.X <= 0.0 || Size.Y <= 0.0)
			return Layer;
		const ADreamMainMenuScene* Scene = Controller->GetMenuScene();
		const FDreamMainMenuLayout Layout = FDreamMainMenuLayout::ForAspectRatio(Size.X / Size.Y,
			Scene && Scene->bLogoOnRight);
		if (LogoBrush.GetResourceObject() && LogoBrush.ImageSize.X > 0 && LogoBrush.ImageSize.Y > 0)
		{
			// 按较紧的一条尺寸约束等比缩放，不把原艺术字拉伸到固定矩形。
			const FVector2D Limit = Layout.LogoMaxSize * Size;
			const float Scale = FMath::Min(Limit.X / LogoBrush.ImageSize.X, Limit.Y / LogoBrush.ImageSize.Y);
			const FVector2D LogoSize = FVector2D(LogoBrush.ImageSize) * Scale;
			const FVector2D Position = Layout.LogoCenter * Size - LogoSize * 0.5;
			FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(LogoSize,
				FSlateLayoutTransform(Position)), &LogoBrush, ESlateDrawEffect::None, FLinearColor::White);
		}
		// 遮罩画在 Logo 上方，因此淡出包含建筑、纸纹与 Logo；不会出现标题悬在白屏上的一帧。
		const float Opacity = Controller->GetPaperFadeOpacity();
		if (Opacity > 0.0f)
			FSlateDrawElement::MakeBox(Elements, Layer + 1, Geometry.ToPaintGeometry(),
				FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None,
				FLinearColor(0.965f, 0.958f, 0.94f, Opacity));
		return Layer + 1;
	}

private:
	TWeakObjectPtr<ADreamMainMenuPlayerController> Controller;
	FSlateBrush LogoBrush;
	FReply Start()
	{
		if (Controller.IsValid())
			Controller->RequestStartGame();
		return FReply::Handled();
	}
};
}

ADreamMainMenuPlayerController::ADreamMainMenuPlayerController()
{
	bAutoManageActiveCameraTarget = false;
	GameplayMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/0_/Maps/TEST.TEST")));
}

void ADreamMainMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();
	CreateMenuOverlay();
}

void ADreamMainMenuPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();
	// PIE 的 LocalPlayer 可能稍晚绑定；两个入口共用幂等安装函数，避免漏装或重复安装。
	CreateMenuOverlay();
}

void ADreamMainMenuPlayerController::CreateMenuOverlay()
{
	if (MenuOverlay.IsValid() || !IsLocalController() || !GetLocalPlayer() || !GetWorld())
		return;
	UGameViewportClient* Viewport = GetLocalPlayer()->ViewportClient;
	if (!Viewport)
		return;
	for (TActorIterator<ADreamMainMenuScene> It(GetWorld()); It; ++It)
	{
		MenuScene = *It;
		SetViewTarget(*It);
		break;
	}
	if (!MenuScene.IsValid())
		return;
	MenuOverlay = SNew(SDreamMainMenuOverlay).Controller(this);
	Viewport->AddViewportWidgetForPlayer(GetLocalPlayer(), MenuOverlay.ToSharedRef(), 20);
	bShowMouseCursor = true;
	FInputModeGameAndUI Mode;
	Mode.SetWidgetToFocus(MenuOverlay);
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	// 取消鼠标捕获，第一下点击就到达透明界面，不会先被视口用于捕获鼠标。
	Viewport->SetMouseCaptureMode(EMouseCaptureMode::NoCapture);
}

bool ADreamMainMenuPlayerController::IsStartKey(const FKey& Key)
{
	// “任意处”指位置；只接受明确的确认动作，鼠标移动、滚轮、Esc 与 Alt+Tab 不会开始游戏。
	return Key == EKeys::LeftMouseButton || Key == EKeys::Enter || Key == EKeys::SpaceBar
		|| Key == EKeys::Gamepad_FaceButton_Bottom;
}

bool ADreamMainMenuPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	// Slate 未持有焦点时仍保留控制器入口，例如 PIE 刚恢复到游戏视口或手柄输入直达视口。
	if (Params.Event == IE_Pressed && IsStartKey(Params.Key))
	{
		RequestStartGame();
		return true;
	}
	return Super::InputKey(Params);
}

float ADreamMainMenuPlayerController::GetPaperFadeOpacity() const
{
	const auto Smooth = [](float Value) { return Value * Value * (3.0f - 2.0f * Value); };
	if (bStartingGame)
	{
		const float Progress = FadeOutDuration > 0.0f ? FMath::Clamp(TransitionTime / FadeOutDuration, 0.0f, 1.0f) : 1.0f;
		return FMath::Lerp(TransitionStartOpacity, 1.0f, Smooth(Progress));
	}
	const float Progress = FadeInDuration > 0.0f ? FMath::Clamp(VisibleTime / FadeInDuration, 0.0f, 1.0f) : 1.0f;
	return 1.0f - Smooth(Progress);
}

void ADreamMainMenuPlayerController::RequestStartGame()
{
	if (bStartingGame || !IsLocalController())
		return;
	const FString Package = GameplayMap.ToSoftObjectPath().GetLongPackageName();
	if (Package.IsEmpty() || !FPackageName::DoesPackageExist(Package))
	{
		// 缺地图时保留可见菜单，不把玩家留在无法恢复的淡出白屏中。
		UE_LOG(LogDreamSpace, Error, TEXT("[开始界面] 游玩关卡不存在或未打包：%s"), *Package);
		return;
	}
	TransitionStartOpacity = GetPaperFadeOpacity();
	bStartingGame = true;
	TransitionTime = 0.0f;
	UE_LOG(LogDreamSpace, Log, TEXT("[开始界面] 确认开始，淡出后打开 %s"), *Package);
}

void ADreamMainMenuPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	CreateMenuOverlay();
	VisibleTime += DeltaTime;
	if (!bStartingGame || bTravelRequested)
		return;
	TransitionTime += DeltaTime;
	// 留一帧完整遮罩再发出跳转，避免同步加载前最后一帧还残留建筑轮廓。
	if (TransitionTime > FadeOutDuration + 0.05f)
	{
		bTravelRequested = true;
		bShowMouseCursor = false;
		SetInputMode(FInputModeGameOnly());
		UGameplayStatics::OpenLevel(this, FName(*GameplayMap.ToSoftObjectPath().GetLongPackageName()));
	}
}

void ADreamMainMenuPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (MenuOverlay.IsValid() && GetLocalPlayer() && GetLocalPlayer()->ViewportClient)
		GetLocalPlayer()->ViewportClient->RemoveViewportWidgetForPlayer(GetLocalPlayer(), MenuOverlay.ToSharedRef());
	MenuOverlay.Reset();
	Super::EndPlay(EndPlayReason);
}
