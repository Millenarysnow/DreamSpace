#include "DreamWorldRecoveryComponent.h"

#include "DreamProximitySketchCameraManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

UDreamWorldRecoveryComponent::UDreamWorldRecoveryComponent()
{
	// 所有逐帧插值统一交给相机，组件不轮询材质，也不会和其它机关分别写入同一个半径。
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
	bEditableWhenInherited = true;
}

void UDreamWorldRecoveryComponent::OnInteracted_Implementation(AActor* Interactor)
{
	TriggerWorldRecovery(Interactor);
}

bool UDreamWorldRecoveryComponent::TriggerWorldRecovery(AActor* Interactor)
{
	APawn* Pawn = Cast<APawn>(Interactor);
	if (!IsActive() || (bTriggerOnce && bHasTriggered) || !IsValid(GetOwner())
		|| !IsValid(Pawn) || Pawn->GetWorld() != GetWorld())
		return false;

	// 只定位这次交互 Pawn 的控制器，不使用全局 GetPlayerController(0)。这样多个本地玩家
	// 不会误修改彼此的相机；AI、旧 Pawn、跨世界引用或已经取消占有的角色也不会消耗机关。
	APlayerController* Controller = Cast<APlayerController>(Pawn->GetController());
	if (!IsValid(Controller) || !Controller->IsLocalController() || Controller->GetPawn() != Pawn)
		return false;
	ADreamProximitySketchCameraManager* Camera =
		Cast<ADreamProximitySketchCameraManager>(Controller->PlayerCameraManager);
	if (!IsValid(Camera) || !Camera->StartWorldRecovery(TargetNormalRadius, RecoveryDuration, bRestoreEntireWorld))
		return false;

	// 先提交相机状态并标记使用，再发送事件，防止蓝图回调重入后同一机关重复触发。
	// 广播之后不再访问所属 Actor，允许事件中立即销毁机关或卸载它的流送关卡。
	bHasTriggered = true;
	OnWorldRecoveryTriggered.Broadcast(Interactor);
	return true;
}
