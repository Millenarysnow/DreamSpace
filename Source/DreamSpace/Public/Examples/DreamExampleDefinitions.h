#pragma once
#include "CoreMinimal.h"
class UInteractiveAssemblyDefinition;
/** 仅用于创建验收资产；关卡中的对象仍全部使用通用装配体 Actor。 */
namespace DreamExamples
{
DREAMSPACE_API void MakeCube(UInteractiveAssemblyDefinition& D);
DREAMSPACE_API void MakeKey(UInteractiveAssemblyDefinition& D);
DREAMSPACE_API void MakeGravityRoom(UInteractiveAssemblyDefinition& D);
DREAMSPACE_API void MakeDoor(UInteractiveAssemblyDefinition& D);
} // namespace DreamExamples
