#pragma once

#include "GameFramework/Actor.h"
#include "DreamDroppedItem.generated.h"

class USphereComponent;
class UStaticMeshComponent;

/**
 * 从手办取出的默认掉落物。
 *
 * 拖动时它是无碰撞的模型预览；成功拖出后，球形碰撞体开启物理模拟，
 * 模型随碰撞体一起掉落。使用独立球形碰撞体是为了让没有简单碰撞的美术网格也能掉落。
 * 关卡可以为取出组件指定本类的蓝图子类，在子类中补充拾取、音效等后续玩法。
 */
UCLASS(Blueprintable, BlueprintType)
class DREAMSPACE_API ADreamDroppedItem : public AActor
{
	GENERATED_BODY()

public:
	ADreamDroppedItem();

	/**
	 * 复制房间模型的网格、材质和世界缩放，缩小为掉落物。
	 * 只复制被指定的静态网格组件；复杂蓝图的其它组件不会被隐式克隆。
	 */
	bool InitializeFromSource(const UStaticMeshComponent* Source, float ScaleMultiplier);

	/** 拖出成功后打开球形碰撞与物理模拟；返回刚体是否成功激活。 */
	virtual bool ActivateDrop();

	/** 释放位置必须没有外部阻挡；不能把预览中允许穿过的墙体变成初始穿透的刚体。 */
	bool CanActivateDrop(const AActor* SourceActor) const;

	/** 当前球形碰撞半径，供取出组件把掉落物放在显示面前方。 */
	float GetDropRadius() const;

	/** 允许掉落物蓝图在物理激活后补充拾取提示、音效或特效。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "手办取出")
	void OnDroppedFromMiniature();

private:
	/** 球形根组件负责碰撞与刚体，预览时关闭，提交时开启。 */
	UPROPERTY(VisibleAnywhere, Category = "掉落物")
	TObjectPtr<USphereComponent> DropCollision;

	/** 只承载复制的网格和材质，不参与物理，也不再次出现在手办捕获中。 */
	UPROPERTY(VisibleAnywhere, Category = "掉落物")
	TObjectPtr<UStaticMeshComponent> DropMesh;
};
