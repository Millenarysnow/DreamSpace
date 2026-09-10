#include "DreamHUD.h"
#include "DreamPlayerController.h"
#include "DreamInteractionPlayerSubsystem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
void ADreamHUD::DrawHUD()
{
	Super::DrawHUD();
	auto* Controller = Cast<ADreamPlayerController>(GetOwningPlayerController());
	if (!Controller || !Canvas)
		return;
	auto* Player = Controller->GetInteractionPlayer();
	DrawRect(FLinearColor(0.01, 0.02, 0.04, 0.85), 12, 12, 900, 165);
	const bool Overview = Player && Player->GetInteractionMode() == EDreamInteractionMode::Overview;
	DrawText(Overview ? TEXT("DreamSpace | 全局建筑操作") : TEXT("DreamSpace | 第三人称探索"),
		FLinearColor(0.3, 0.8, 1), 24, 20, GEngine->GetMediumFont(), 1);
	DrawText(Controller->DescribeSelection(), FLinearColor::White, 24, 48, GEngine->GetSmallFont(), 1);
	DrawText(TEXT("Tab 视角 | WASD 移动 | 右键拖动全局镜头 | 滚轮缩放镜头 | 左键选择 | P 父级 / O 子级"),
		FLinearColor::White, 24, 73, GEngine->GetSmallFont(), 1);
	DrawText(TEXT("R/Shift+R 旋转 | X/Y/Z 轴 | +/- 尺寸 | 方向键平移 | Enter 提交 | Esc 取消 | U 撤销 / J 重做"),
		FLinearColor::White, 24, 96, GEngine->GetSmallFont(), 1);
	DrawText(TEXT("E 拾取/开门 | G 放下 | B 破坏/拆分 | F5 存档 / F9 读档"), FLinearColor::White, 24, 119,
		GEngine->GetSmallFont(), 1);
	DrawText(Controller->GetStatusText().ToString(), FLinearColor(1, 0.8, 0.3), 24, 145, GEngine->GetSmallFont(), 1);
	if (!Overview)
	{
		DrawLine(
			Canvas->ClipX / 2 - 5, Canvas->ClipY / 2, Canvas->ClipX / 2 + 5, Canvas->ClipY / 2, FLinearColor::White);
		DrawLine(
			Canvas->ClipX / 2, Canvas->ClipY / 2 - 5, Canvas->ClipX / 2, Canvas->ClipY / 2 + 5, FLinearColor::White);
	}
}
