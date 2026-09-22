#include "DreamHUD.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
void ADreamHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
		return;
	// 顶部提示条：只显示基础操作说明；旧的交互框架状态展示已随框架一并移除。
	DrawRect(FLinearColor(0.01f, 0.02f, 0.04f, 0.85f), 12, 12, FMath::Min(860.0f, Canvas->ClipX - 24), 30);
	DrawText(TEXT("DreamSpace | WASD 移动 | 鼠标 视角 | 空格 跳跃 | E 交互 | 调试命令 dream.DebugPivots 1"),
		FLinearColor::White, 24, 20, GEngine->GetSmallFont(), 1);
	// 屏幕中心准星：辅助玩家瞄准可交互物体，交互射线与准星方向一致。
	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	DrawLine(CenterX - 5, CenterY, CenterX + 5, CenterY, FLinearColor::White);
	DrawLine(CenterX, CenterY - 5, CenterX, CenterY + 5, FLinearColor::White);
}
