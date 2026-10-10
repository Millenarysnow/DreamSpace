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
	void GetMovingCollisionComponents(const AActor* Actor, TArray<AActor*>& MovingActors,
		TArray<UPrimitiveComponent*>& Components)
	{
		if (!Actor || !Actor->GetRootComponent())
			return;
		MovingActors.Add(const_cast<AActor*>(Actor));
		Actor->GetAttachedActors(MovingActors, false, true);
		for (AActor* MovingActor : MovingActors)
		{
			const USceneComponent* Root = MovingActor ? MovingActor->GetRootComponent() : nullptr;
			if (!Root || !MovingActor->GetActorEnableCollision())
				continue;
			TArray<UPrimitiveComponent*> ActorComponents;
			MovingActor->GetComponents(ActorComponents);
			for (UPrimitiveComponent* Component : ActorComponents)
			{
				if (Component && Component->IsRegistered() && Component->IsQueryCollisionEnabled()
					&& (Component == Root || Component->IsAttachedTo(Root)))
					Components.Add(Component);
			}
		}
	}

	bool IsNonWorseningInitialOverlap(const FHitResult& Hit, const FVector& Delta, bool bRotationChanged)
	{
		// 凸体的接触法线可能有微小偏斜，允许约 0.057 度内的近似切向平移。
		// 只有已有重叠可以离开或滑动；明显向内的运动和旋转仍保留阻挡。
		constexpr double TangentNormalTolerance = 0.001;
		return Hit.bStartPenetrating && !bRotationChanged && !Hit.ImpactNormal.IsNearlyZero()
			&& FVector::DotProduct(Hit.ImpactNormal, Delta.GetSafeNormal()) >= -TangentNormalTolerance;
	}
}

float DreamInteractionCollision::FindSafeMoveFraction(
	const AActor* Actor, const FTransform& TargetActorTransform, TConstArrayView<AActor*> IgnoredActors)
{
	const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	if (!World || !Actor->GetActorEnableCollision())
		return 1.0f;

	const FTransform CurrentActorTransform = Actor->GetActorTransform();
	FComponentQueryParams QueryParams(SCENE_QUERY_STAT(DreamInteractionMove), Actor);
	QueryParams.bIgnoreTouches = true;
	TArray<AActor*> MovingActors;
	TArray<UPrimitiveComponent*> Components;
	GetMovingCollisionComponents(Actor, MovingActors, Components);
	// 子 Actor 的门、贴纸等随父 Actor 同步移动，组内碰撞不能阻挡自己的运动。
	// 它们的查询碰撞体仍参加下面的扫掠，继续阻挡外部墙体和机关。
	QueryParams.AddIgnoredActors(MovingActors);
	for (const AActor* IgnoredActor : IgnoredActors)
		QueryParams.AddIgnoredActor(IgnoredActor);
	float SafeFraction = 1.0f;

	for (UPrimitiveComponent* Component : Components)
	{
		// 组件相对 Actor 的姿态包含其自身偏移，也支持根是 SceneComponent 的蓝图。
		// 用当前世界姿态建立相对变换，再预测本小步结束时每个组件实际会到达哪里。
		const FTransform RelativeToActor = Component->GetComponentTransform().GetRelativeTransform(CurrentActorTransform);
		const FTransform TargetComponentTransform = RelativeToActor * TargetActorTransform;
		const FVector Start = Component->GetComponentLocation();
		const FVector End = TargetComponentTransform.GetLocation();
		const float Distance = FVector::Distance(Start, End);
		const bool bRotationChanged =
			!TargetComponentTransform.GetRotation().Equals(Component->GetComponentQuat(), KINDA_SMALL_NUMBER);
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
					if (IsNonWorseningInitialOverlap(Hit, End - Start, bRotationChanged))
						continue;
					bSweepFoundBlocker = true;
					// 离接触面留半厘米，避免浮点误差把下一帧的起点放到障碍内部。
					SafeFraction = FMath::Min(SafeFraction,
						FMath::Max(0.0f, Hit.Time - 0.5f / Distance));
				}
			}
		}

		// 纯平移已有完整扫掠，重复做终点重叠会把关卡中预先贴合的表面误判为阻挡。
		// 只有组件朝向确实变化时，才需要补查 UE 扫掠不支持的旋转体积。
		if (bRotationChanged)
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
	TArray<AActor*> MovingActors;
	TArray<UPrimitiveComponent*> Components;
	GetMovingCollisionComponents(Actor, MovingActors, Components);
	for (const UPrimitiveComponent* Component : Components)
	{
		// 包围球可能比真实形状大，但能保证旋转子步不会因门板太长而跨过薄障碍。
		Radius = FMath::Max(Radius,
			FVector::Distance(PivotWorldLocation, Component->Bounds.Origin) + Component->Bounds.SphereRadius);
	}
	return Radius;
}
