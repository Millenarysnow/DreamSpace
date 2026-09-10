#pragma once
#include "DreamInteractionTypes.h"

/** 无场景依赖的层级运算，预览、事务验证和存档恢复共用同一套计算。 */
namespace DreamState
{
DREAMSPACE_API bool Validate(const FDreamAssemblyState& State, FText& Failure);
DREAMSPACE_API bool WorldTransform(const FDreamAssemblyState& State, const FGuid& NodeId, FTransform& Result);
DREAMSPACE_API bool IsDescendant(const FDreamAssemblyState& State, const FGuid& NodeId, const FGuid& Ancestor);
DREAMSPACE_API bool IsVisible(const FDreamAssemblyState& State, const FGuid& NodeId);
DREAMSPACE_API bool Equivalent(const FDreamAssemblyState& A, const FDreamAssemblyState& B);
DREAMSPACE_API bool IsFinitePositive(const FTransform& Transform);
} // namespace DreamState
