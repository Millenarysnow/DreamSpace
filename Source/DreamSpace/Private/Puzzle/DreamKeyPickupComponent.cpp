#include "DreamKeyPickupComponent.h"

#include "Components/PrimitiveComponent.h"
#include "DreamCharacter.h"
#include "DreamPlayerController.h"
#include "DreamSpace.h"

UDreamKeyPickupComponent::UDreamKeyPickupComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

void UDreamKeyPickupComponent::EnablePickup()
{
	if (AActor* Owner = GetOwner())
	{
		if (UPrimitiveComponent* Collision = Cast<UPrimitiveComponent>(Owner->GetRootComponent()))
		{
			// 复用掉落物的物理球体作为命中范围，不依赖钥匙网格是否有简单碰撞。
			// 这里只打开已有 E 射线使用的 Visibility 响应，保持刚体和 Camera 通道的设置。
			Collision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			bPickupEnabled = true;
		}
	}
}

void UDreamKeyPickupComponent::OnInteracted_Implementation(AActor* Interactor)
{
	AActor* Owner = GetOwner();
	ADreamCharacter* Character = Cast<ADreamCharacter>(Interactor);
	if (!IsActive() || !IsPickupEnabled() || !IsValid(Owner) || !IsValid(Character) ||
		Character->GetWorld() != GetWorld())
		return;

	// 接口也可能被蓝图直接调用；保持与控制器一致的约束，手办模式和持续拖动期间不能拾取。
	if (const ADreamPlayerController* Controller = Cast<ADreamPlayerController>(Character->GetController()))
	{
		if (Controller->IsMiniatureInteractionMode() || Controller->IsDraggingInteraction())
			return;
	}

	bConsumed = true;
	// 先确认世界中的钥匙能够被移除，再授予持有状态，避免销毁失败导致无限重复领取。
	if (!Owner->Destroy())
	{
		bConsumed = false;
		UE_LOG(LogDreamSpace, Warning, TEXT("钥匙拾取失败：无法移除掉落物 %s。"), *GetNameSafe(Owner));
		return;
	}
	Character->AcquireKey();
}
