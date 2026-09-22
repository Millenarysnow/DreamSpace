#pragma once

#include "CoreMinimal.h"

/**
 * 解谜组件的调试开关集合。
 *
 * 目前只包含枢轴点/转轴的调试绘制开关，后续新的解谜调试开关也统一放在这里，
 * 避免每个组件各自注册控制台变量、风格不一致。
 */
namespace DreamPuzzleDebug
{
	/**
	 * 是否开启枢轴点与转轴的调试绘制。
	 * 对应控制台命令：dream.DebugPivots 0/1。
	 */
	DREAMSPACE_API bool IsPivotDebugDrawEnabled();
}
