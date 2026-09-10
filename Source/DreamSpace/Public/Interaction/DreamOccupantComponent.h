#pragma once
#include "Components/ActorComponent.h"
#include "DreamInteractionTypes.h"
#include "DreamOccupantComponent.generated.h"
/** 注册为事务参与者的角色/内部物体。第一版不允许把自由模拟的 Chaos 刚体隐式挂进建筑。 */
UCLASS(ClassGroup = (DreamInteraction), meta = (BlueprintSpawnableComponent))
class DREAMSPACE_API UDreamOccupantComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UDreamOccupantComponent();
	UPROPERTY(EditAnywhere, Category = "占用者")
	FGuid OccupantId;
	UPROPERTY(EditAnywhere, Category = "占用者")
	bool bResolveLocalGravity = true;
	UPROPERTY(EditAnywhere, Category = "占用者")
	FVector CarryOffset = FVector::ZeroVector;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function) override;
	/** 同时捕获引擎运动组件和控制器状态，避免撤销只还原位置却遗留错误速度/相机。 */
	FDreamOccupantState CaptureState() const;
	/** 事务通过全部校验后调用；清理移动平台缓存，再恢复姿态、重力、移动模式和速度。 */
	void RestoreState(const FDreamOccupantState& State);
	/** 返回不带角色缩放的持有者参考系；携带物体的尺寸来自自身状态。 */
	FTransform GetCarryFrame() const;
};
