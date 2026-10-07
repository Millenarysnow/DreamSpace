#pragma once

#include "CoreMinimal.h"

class AActor;

/**
 * 两种机关共用的运动碰撞查询。这里仅负责判断一小段运动是否安全，
 * 真正的 Actor 位移、回弹时间和交互状态仍由各自的组件管理。
 */
namespace DreamInteractionCollision
{
	/**
	 * 检查 Actor 从当前姿态到目标姿态的运动。
	 * 返回 1 表示整段可以通过；小于 1 表示受阻，数值是平移扫掠所能到达的安全比例。
	 * 如果阻挡来自目标姿态的旋转重叠，则保守地返回 0，由调用方留在上一安全姿态。
	 */
	float FindSafeMoveFraction(const AActor* Actor, const FTransform& TargetActorTransform);

	/** 估计有查询碰撞的组件到转轴的最大半径，用来限制旋转子步中最远点的移动距离。 */
	float GetMaxCollisionRadius(const AActor* Actor, const FVector& PivotWorldLocation);
}
