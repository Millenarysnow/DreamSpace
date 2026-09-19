#include "DreamSceneCaptureAnchor.h"

#include "Components/SceneComponent.h"

const FName ADreamSceneCaptureAnchor::AnchorTag(TEXT("DreamSceneCaptureAnchor"));

ADreamSceneCaptureAnchor::ADreamSceneCaptureAnchor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);

	// 空 Actor 仍需要根组件承载 Transform，否则在关卡中编辑的位置和旋转
	// 无法作为稳定的参考坐标。这里只创建 SceneComponent，不添加网格或碰撞体。
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("AnchorRoot")));

	// 在构造阶段就提供 Tag，不等锚点自己的 BeginPlay；这样即使运行时玩家
	// 比锚点更早执行 BeginPlay，也能找到已经加载到当前世界中的关卡实例。
	Tags.AddUnique(AnchorTag);
}
