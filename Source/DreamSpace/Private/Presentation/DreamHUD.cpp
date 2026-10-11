#include "DreamHUD.h"
#include "CanvasItem.h"
#include "DreamCharacter.h"
#include "DreamPlayerController.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

void ADreamHUD::ShowKeyAcquiredMessage()
{
	if (GetWorld())
		KeyAcquiredMessageUntil = GetWorld()->GetRealTimeSeconds() + KeyAcquiredMessageDuration;
}

bool ADreamHUD::IsKeyAcquiredMessageVisible() const
{
	return GetWorld() && GetWorld()->GetRealTimeSeconds() < KeyAcquiredMessageUntil;
}

void ADreamHUD::ShowMiniatureAcquiredMessage()
{
	if (GetWorld())
		MiniatureAcquiredMessageUntil = GetWorld()->GetRealTimeSeconds() + MiniatureAcquiredMessageDuration;
}

bool ADreamHUD::IsMiniatureAcquiredMessageVisible() const
{
	return GetWorld() && GetWorld()->GetRealTimeSeconds() < MiniatureAcquiredMessageUntil;
}

void ADreamHUD::DrawPasswordEntry(const ADreamPlayerController* Controller)
{
	// 按较小的屏幕维度缩放面板，保证窗口缩小后数字格和操作说明仍在可见区域。
	const float Scale = FMath::Min(1.0f, FMath::Min(Canvas->ClipX / 620.0f, Canvas->ClipY / 360.0f));
	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	const float PanelWidth = 560.0f * Scale;
	const float PanelHeight = 280.0f * Scale;
	DrawRect(FLinearColor(0, 0, 0, 0.5f), 0, 0, Canvas->ClipX, Canvas->ClipY);
	DrawRect(FLinearColor(0.02f, 0.035f, 0.06f, 0.96f), CenterX - PanelWidth * 0.5f,
		CenterY - PanelHeight * 0.5f, PanelWidth, PanelHeight);
	// 文字按实际字形宽高居中，避免中文字符串长度与像素宽度不一致造成偏移。
	auto CenteredText = [&](const FString& Message, float OffsetY, float TextScale, const FLinearColor& Color)
	{
		UFont* Font = GEngine->GetLargeFont();
		float Width = 0.0f, Height = 0.0f;
		GetTextSize(Message, Width, Height, Font, TextScale * Scale);
		FCanvasTextItem Item(FVector2D(CenterX - Width * 0.5f, CenterY + OffsetY * Scale - Height * 0.5f),
			FText::FromString(Message), Font, Color);
		Item.Scale = FVector2D(TextScale * Scale);
		Item.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(Item);
	};
	CenteredText(TEXT("密码箱"), -105.0f, 2.2f, FLinearColor::White);
	CenteredText(TEXT("输入四位数字密码"), -72.0f, 1.2f, FLinearColor(0.7f, 0.8f, 0.9f));
	const FString& Password = Controller->GetEnteredPassword();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const float CellCenter = CenterX + (Index - 1.5f) * 80.0f * Scale;
		const bool bCurrentCell = Index == Password.Len();
		DrawRect(bCurrentCell ? FLinearColor(0.16f, 0.32f, 0.48f) : FLinearColor(0.08f, 0.12f, 0.18f),
			CellCenter - 32.0f * Scale, CenterY - 40.0f * Scale, 64.0f * Scale, 76.0f * Scale);
		const FString Digit = Index < Password.Len() ? Password.Mid(Index, 1) : TEXT("_");
		float Width = 0.0f, Height = 0.0f;
		GetTextSize(Digit, Width, Height, GEngine->GetLargeFont(), 3.0f * Scale);
		DrawText(Digit, FLinearColor::White, CellCenter - Width * 0.5f, CenterY - 2.0f * Scale - Height * 0.5f,
			GEngine->GetLargeFont(), 3.0f * Scale);
	}
	CenteredText(Controller->GetPasswordEntryMessage(), 62.0f, 1.2f, FLinearColor(1.0f, 0.5f, 0.35f));
	CenteredText(TEXT("数字键输入  |  Backspace 删除  |  Enter 确认  |  E / Esc 取消"), 108.0f, 1.0f, FLinearColor::White);
}

void ADreamHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
		return;
	const ADreamPlayerController* DreamPC = Cast<ADreamPlayerController>(PlayerOwner);
	if (DreamPC && DreamPC->IsEnteringPassword())
	{
		DrawPasswordEntry(DreamPC);
		return;
	}
	const bool bMiniatureMode = DreamPC && DreamPC->IsMiniatureInteractionMode();
	const ADreamCharacter* Character = DreamPC ? Cast<ADreamCharacter>(DreamPC->GetPawn()) : nullptr;
	// 光标模式和普通探索使用不同提示，避免玩家以为左键会从准星位置发射射线。
	DrawRect(FLinearColor(0.01f, 0.02f, 0.04f, 0.85f), 12, 12,
		FMath::Min(1040.0f, Canvas->ClipX - 24), 30);
	DrawText(bMiniatureMode
		? TEXT("手办交互 | 左键点击手办画面中的物体 | Tab 返回探索 | dream.DebugMiniatureInteraction 1 显示射线")
		: Character && Character->bHasMiniature
			? TEXT("DreamSpace | WASD 移动 | 鼠标 视角 | 空格 跳跃 | E 世界交互 | Tab 手办交互")
			: TEXT("DreamSpace | WASD 移动 | 鼠标 视角 | 空格 跳跃 | E 世界交互 | 寻找密码箱获得手办"),
		FLinearColor::White, 24, 20, GEngine->GetSmallFont(), 1);
	// 沿玩家视线的三维射线在当前相机中往往只呈现一个点，Capture 射线还可能
	// 远在玩家视野之外。因此无论成功或失败，额外在鼠标点击处画屏幕十字与结果。
	FVector2D ClickPosition;
	FString ClickMessage;
	FLinearColor ClickColor;
	if (DreamPC && DreamPC->GetMiniatureClickDebug(ClickPosition, ClickMessage, ClickColor))
	{
		DrawRect(FLinearColor(0.01f, 0.02f, 0.04f, 0.9f), 12, 48,
			FMath::Min(1300.0f, Canvas->ClipX - 24), 32);
		DrawText(ClickMessage, ClickColor, 24, 56, GEngine->GetSmallFont(), 1);
		DrawLine(ClickPosition.X - 9, ClickPosition.Y, ClickPosition.X + 9, ClickPosition.Y, ClickColor, 2);
		DrawLine(ClickPosition.X, ClickPosition.Y - 9, ClickPosition.X, ClickPosition.Y + 9, ClickColor, 2);
	}
	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	if (IsMiniatureAcquiredMessageVisible() || IsKeyAcquiredMessageVisible())
	{
		// 用实际字体测量整行文字，再按宽高各减去一半，确保文字本身位于视口正中央。
		// 黑色阴影保证浅色地面上的可读性；提示期间不画重叠的准星，切换手办模式仍显示提示。
		const FString Message = IsMiniatureAcquiredMessageVisible() ? TEXT("已获得手办，按 Tab 观察") : TEXT("已获得钥匙");
		UFont* Font = GEngine->GetLargeFont();
		// 引擎默认的 LargeFont 也可能只有 10 pt，单独放大获得提示，确保主视口里可以直接读到。
		const float TextScale = 2.0f;
		float TextWidth = 0.0f, TextHeight = 0.0f;
		GetTextSize(Message, TextWidth, TextHeight, Font, TextScale);
		FCanvasTextItem TextItem(FVector2D(CenterX - TextWidth * 0.5f, CenterY - TextHeight * 0.5f),
			FText::FromString(Message), Font, FLinearColor::White);
		TextItem.Scale = FVector2D(TextScale);
		TextItem.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(TextItem);
		return;
	}
	// 普通模式的准星与 E 键世界射线一致；手办模式则使用可见鼠标光标选点。
	if (bMiniatureMode)
		return;
	DrawLine(CenterX - 5, CenterY, CenterX + 5, CenterY, FLinearColor::White);
	DrawLine(CenterX, CenterY - 5, CenterX, CenterY + 5, FLinearColor::White);
}
