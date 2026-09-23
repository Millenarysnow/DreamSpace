#include "DreamPuzzleDebug.h"
#include "HAL/IConsoleManager.h"

// 控制台变量：一行命令即可开关枢轴点、转轴与平移范围的调试绘制。
// 用法：在游戏中按 ~ 打开控制台，输入 dream.DebugPivots 1 开启，输入 dream.DebugPivots 0 关闭。
static bool bDreamPivotDebugDraw = false;
static FAutoConsoleVariableRef CVarDreamPivotDebugDraw(
	TEXT("dream.DebugPivots"),
	bDreamPivotDebugDraw,
	TEXT("是否在游戏中绘制解谜组件的枢轴点、转轴与平移范围调试信息。0=关闭，1=开启。"),
	ECVF_Default);

bool DreamPuzzleDebug::IsPivotDebugDrawEnabled()
{
	return bDreamPivotDebugDraw;
}
