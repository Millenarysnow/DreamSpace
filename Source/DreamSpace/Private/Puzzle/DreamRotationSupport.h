#pragma once

#include "CoreMinimal.h"

class AActor;
class ACharacter;

/**
 * 步进旋转与自由旋转共用的站立角色搬运逻辑。
 * 这里只处理平台实际姿态的增量，不决定转动范围、输入方式或受阻后的运动策略。
 * 因此两种旋转组件都能遵循相同的重力、胶囊、速度、视角和底座缓存规则。
 */
namespace DreamRotationSupport
{
/** 只收集真正站在平台移动碰撞体上的角色；跳离后即使保留空中底座也不会参与。 */
void GatherStandingCharacters(const AActor* Platform, TArray<ACharacter*>& OutCharacters);

/** 预测一次平台刚体运动后角色的世界姿态，供碰撞查询使用，不修改任何对象。 */
FTransform GetStandingCharacterTransform(
	const AActor* Platform, const ACharacter* Character, const FTransform& TargetPlatformTransform);

/** 应用平台实际运动，并同步站立角色的世界重力、位置、胶囊朝向、速度及控制器视角。 */
void ApplyActorTransform(
	AActor* Platform, const FTransform& TargetPlatformTransform, TConstArrayView<ACharacter*> StandingCharacters);
} // namespace DreamRotationSupport
