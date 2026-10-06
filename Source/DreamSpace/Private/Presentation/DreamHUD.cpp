#include "DreamHUD.h"
#include "DreamPlayerController.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
void ADreamHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
		return;
	const ADreamPlayerController* DreamPC = Cast<ADreamPlayerController>(PlayerOwner);
	const bool bMiniatureMode = DreamPC && DreamPC->IsMiniatureInteractionMode();
	// 光标模式和普通探索使用不同提示，避免玩家以为左键会从准星位置发射射线。
	DrawRect(FLinearColor(0.01f, 0.02f, 0.04f, 0.85f), 12, 12,
		FMath::Min(1040.0f, Canvas->ClipX - 24), 30);
	DrawText(bMiniatureMode
		? TEXT("手办交互 | 左键点击手办画面中的物体 | Tab 返回探索 | dream.DebugMiniatureInteraction 1 显示射线")
		: TEXT("DreamSpace | WASD 移动 | 鼠标 视角 | 空格 跳跃 | E 世界交互 | Tab 手办交互"),
		FLinearColor::White, 24, 20, GEngine->GetSmallFont(), 1);
	// 普通模式的准星与 E 键世界射线一致；手办模式则使用可见鼠标光标选点。
	if (bMiniatureMode)
		return;
	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	DrawLine(CenterX - 5, CenterY, CenterX + 5, CenterY, FLinearColor::White);
	DrawLine(CenterX, CenterY - 5, CenterX, CenterY + 5, FLinearColor::White);
}
