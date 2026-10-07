#include "DreamInteractionCollision.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Engine/OverlapResult.h"

namespace
{
	/**
	 * Actor 可以用无碰撞的 SceneComponent 做根，再把一个或多个网格/碰撞盒挂在其下。
	 * UE 的 SetActorLocation(..., true) 只扫根组件，因此这里必须逐个检查真正随根移动的碰撞体。
	 */
	bool IsMovingCollisionComponent(const AActor* Actor, const UPrimitiveComponent* Component)
	{
		const USceneComponent* Root = Actor ? Actor->GetRootComponent() : nullptr;
		return Root && Component && Component->IsRegistered() && Component->IsQueryCollisionEnabled()
			&& (Component == Root || Component->IsAttachedTo(Root));
	}
}

float DreamInteractionCollision::FindSafeMoveFraction(
	const AActor* Actor, const FTransform& TargetActorTransform)
{
	const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	if (!World || !Actor->GetActorEnableCollision())
		return 1.0f;

	const FTransform CurrentActorTransform = Actor->GetActorTransform();
	FComponentQueryParams QueryParams(SCENE_QUERY_STAT(DreamInteractionMove), Actor);
	QueryParams.bIgnoreTouches = true;
	float SafeFraction = 1.0f;

	TArray<UPrimitiveComponent*> Components;
	Actor->GetComponents(Components);
	for (UPrimitiveComponent* Component : Components)
	{
		if (!IsMovingCollisionComponent(Actor, Component))
			continue;

		// 组件相对 Actor 的姿态包含其自身偏移，也支持根是 SceneComponent 的蓝图。
		// 用当前世界姿态建立相对变换，再预测本小步结束时每个组件实际会到达哪里。
		const FTransform RelativeToActor = Component->GetComponentTransform().GetRelativeTransform(CurrentActorTransform);
		const FTransform TargetComponentTransform = RelativeToActor * TargetActorTransform;
		const FVector Start = Component->GetComponentLocation();
		const FVector End = TargetComponentTransform.GetLocation();
		const float Distance = FVector::Distance(Start, End);
		bool bSweepFoundBlocker = false;

		if (Distance > KINDA_SMALL_NUMBER)
		{
			TArray<FHitResult> Hits;
			// ComponentSweepMulti 使用组件自身的简单碰撞形状及双方碰撞响应；
			// 它只平移起始朝向的形状，旋转部分由调用方的小步和下面的终点重叠检查补足。
			World->ComponentSweepMulti(Hits, Component, Start, End, Component->GetComponentQuat(), QueryParams);
			for (const FHitResult& Hit : Hits)
			{
				if (Hit.bBlockingHit && Hit.GetActor() != Actor)
				{
					bSweepFoundBlocker = true;
					// 离接触面留半厘米，避免浮点误差把下一帧的起点放到障碍内部。
					SafeFraction = FMath::Min(SafeFraction,
						FMath::Max(0.0f, Hit.Time - 0.5f / Distance));
				}
			}
		}

		// 纯平移已有完整扫掠，重复做终点重叠会把关卡中预先贴合的表面误判为阻挡。
		// 只有组件朝向确实变化时，才需要补查 UE 扫掠不支持的旋转体积。
		if (!TargetComponentTransform.GetRotation().Equals(Component->GetComponentQuat(), KINDA_SMALL_NUMBER))
		{
			TArray<FOverlapResult> Overlaps;
			World->ComponentOverlapMulti(Overlaps, Component, TargetComponentTransform.GetLocation(),
				TargetComponentTransform.GetRotation(), QueryParams);
			for (const FOverlapResult& Overlap : Overlaps)
			{
				if (Overlap.bBlockingHit && Overlap.GetActor() != Actor)
				{
					// 终点落在障碍内部时，若平移扫掠已经找到了接触点，应保留扫掠的安全比例，
					// 让物体实际走到障碍前再回弹。没有扫掠命中则可能是原地旋转造成的穿入，
					// 此时保守地停在上一姿态，不能直接把目标朝向写入 Actor。
					if (!bSweepFoundBlocker)
						return 0.0f;
				}
			}
		}
	}

	return SafeFraction;
}

float DreamInteractionCollision::GetMaxCollisionRadius(const AActor* Actor, const FVector& PivotWorldLocation)
{
	if (!Actor || !Actor->GetActorEnableCollision())
		return 0.0f;

	float Radius = 0.0f;
	TArray<UPrimitiveComponent*> Components;
	Actor->GetComponents(Components);
	for (const UPrimitiveComponent* Component : Components)
	{
		if (IsMovingCollisionComponent(Actor, Component))
		{
			// 包围球可能比真实形状大，但能保证旋转子步不会因门板太长而跨过薄障碍。
			Radius = FMath::Max(Radius,
				FVector::Distance(PivotWorldLocation, Component->Bounds.Origin) + Component->Bounds.SphereRadius);
		}
	}
	return Radius;
}
