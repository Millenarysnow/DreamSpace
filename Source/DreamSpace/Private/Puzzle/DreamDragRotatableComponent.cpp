#include "DreamDragRotatableComponent.h"

#include "DreamInteractionCollision.h"
#include "DreamRotationSupport.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"

UDreamDragRotatableComponent::UDreamDragRotatableComponent()
{
	InteractionAxis = EDreamPivotRotationAxis::Z;
}

void UDreamDragRotatableComponent::OnReferenceInitialized()
{
	CurrentAngleDegrees = 0.0f;
}

bool UDreamDragRotatableComponent::TryGetRadialDirection(
	const FVector& RayOrigin, const FVector& RayDirection, FVector& OutDirection) const
{
	const double Denominator = FVector::DotProduct(RayDirection, ReferenceAxisWorld);
	if (FMath::Abs(Denominator) < 0.01)
		return false;
	const double Distance = FVector::DotProduct(ReferencePivotWorld - RayOrigin, ReferenceAxisWorld) / Denominator;
	if (Distance < 0.0 || !FMath::IsFinite(Distance))
		return false;
	const FVector Radial = RayOrigin + RayDirection * Distance - ReferencePivotWorld;
	// 枢轴中心附近的极小鼠标误差会放大成巨大角度，保留至少 1 cm 的有效抓取半径。
	if (Radial.ContainsNaN() || Radial.SizeSquared() < 1.0)
		return false;
	OutDirection = Radial.GetSafeNormal();
	return true;
}

void UDreamDragRotatableComponent::InitializeDragSample(const FVector& RayOrigin, const FVector& RayDirection)
{
	bHasRaySample = TryGetRadialDirection(RayOrigin, RayDirection, LastRadialDirection);
	bUseScreenFallback = !bHasRaySample;
}

void UDreamDragRotatableComponent::RebaseDragSample(const FVector& RayOrigin, const FVector& RayDirection)
{
	if (!bUseScreenFallback)
		bHasRaySample = TryGetRadialDirection(RayOrigin, RayDirection, LastRadialDirection);
}

void UDreamDragRotatableComponent::ApplyDragSample(
	const FVector& RayOrigin, const FVector& RayDirection, const FVector2D& PointerDelta)
{
	float DeltaDegrees = 0.0f;
	if (bUseScreenFallback)
	{
		DeltaDegrees = (PointerDelta.X - PointerDelta.Y) * FMath::Max(0.0f, FallbackDegreesPerPixel);
	}
	else
	{
		FVector Radial;
		if (!TryGetRadialDirection(RayOrigin, RayDirection, Radial))
		{
			bHasRaySample = false;
			return;
		}
		if (bHasRaySample)
		{
			// atan2 同时保留转向与角度，不用欧拉角差或无符号 acos。
			// 径向从 +179 度跨到 -179 度时结果为正向 2 度，不会错误地跳转 -358 度。
			const double Sin =
				FVector::DotProduct(ReferenceAxisWorld, FVector::CrossProduct(LastRadialDirection, Radial));
			const double Cos = FVector::DotProduct(LastRadialDirection, Radial);
			DeltaDegrees = FMath::RadiansToDegrees(FMath::Atan2(Sin, Cos));
		}
		// 与平移相同，输入在受阻或到达范围后仍被消耗，反向操作无需抵消历史越界量。
		LastRadialDirection = Radial;
		bHasRaySample = true;
	}
	DeltaDegrees *= FMath::Max(0.0f, DragSensitivity);
	if (!FMath::IsNearlyZero(DeltaDegrees))
		SetRotationAngle(CurrentAngleDegrees + DeltaDegrees);
}

FTransform UDreamDragRotatableComponent::GetTransformAtAngle(float AngleDegrees) const
{
	// 直接构造带符号轴角四元数；插值一个终点四元数会走短弧并丢失大于 180 度的路径。
	const FQuat Rotation(ReferenceAxisWorld, FMath::DegreesToRadians(AngleDegrees));
	const FVector Location =
		ReferencePivotWorld + Rotation.RotateVector(ReferenceActorTransform.GetLocation() - ReferencePivotWorld);
	return FTransform(Rotation * ReferenceActorTransform.GetRotation(), Location, ReferenceActorTransform.GetScale3D());
}

float UDreamDragRotatableComponent::SetRotationAngle(float AngleDegrees)
{
	if (!FMath::IsFinite(AngleDegrees) || !EnsureReference())
		return CurrentAngleDegrees;
	const float TargetAngle =
		FMath::Clamp(AngleDegrees, -FMath::Max(0.0f, NegativeAngleLimit), FMath::Max(0.0f, PositiveAngleLimit));
	const float DeltaDegrees = TargetAngle - CurrentAngleDegrees;
	if (FMath::IsNearlyZero(DeltaDegrees))
		return CurrentAngleDegrees;

	AActor* Owner = GetOwner();
	TArray<ACharacter*> StandingCharacters;
	if (bRotateStandingCharacterGravity)
		DreamRotationSupport::GatherStandingCharacters(Owner, StandingCharacters);
	if (!bConsiderCollision)
	{
		// 无碰撞时可以直接写入绝对轴角的终点。逻辑角度仍保留整圈信息，
		// 角色按实际刚体姿态差同步，省去没有查询需求的大量微步。
		DreamRotationSupport::ApplyActorTransform(Owner, GetTransformAtAngle(TargetAngle), StandingCharacters);
		CurrentAngleDegrees = TargetAngle;
		return CurrentAngleDegrees;
	}

	// 碰撞必须沿带符号的累计角度检查完整弧线，不能仅比较起点与终点四元数。
	float MaxStepRadians = FMath::DegreesToRadians(2.0f);
	TArray<AActor*> MovingActors;
	MovingActors.Add(Owner);
	float Radius = DreamInteractionCollision::GetMaxCollisionRadius(Owner, ReferencePivotWorld);
	for (ACharacter* Character : StandingCharacters)
	{
		MovingActors.Add(Character);
		Radius = FMath::Max(Radius, DreamInteractionCollision::GetMaxCollisionRadius(Character, ReferencePivotWorld));
	}
	// 沿用已有旋转组件精度：每步最多 2 度，最远碰撞点的弧长最多约 2 cm。
	MaxStepRadians = FMath::Min(MaxStepRadians, 2.0f / FMath::Max(Radius, 1.0f));
	const int32 Steps =
		FMath::Max(1, FMath::CeilToInt(FMath::Abs(FMath::DegreesToRadians(DeltaDegrees)) / MaxStepRadians));
	const float StartAngle = CurrentAngleDegrees;
	for (int32 Index = 1; Index <= Steps; ++Index)
	{
		const float NextAngle = FMath::Lerp(StartAngle, TargetAngle, static_cast<float>(Index) / Steps);
		const FTransform TargetTransform = GetTransformAtAngle(NextAngle);
		// 忽略同步运动物体之间的碰撞，再逐个检查平台和乘客对外部物体的运动。
		// 旋转受阻时停在上一完整安全姿态，不使用仅对平移有意义的部分扫掠比例。
		if (DreamInteractionCollision::FindSafeMoveFraction(Owner, TargetTransform, MovingActors) < 1.0f)
			break;
		bool bPassengerBlocked = false;
		for (const ACharacter* Character : StandingCharacters)
		{
			const FTransform CharacterTarget =
				DreamRotationSupport::GetStandingCharacterTransform(Owner, Character, TargetTransform);
			if (DreamInteractionCollision::FindSafeMoveFraction(Character, CharacterTarget, MovingActors) < 1.0f)
			{
				bPassengerBlocked = true;
				break;
			}
		}
		if (bPassengerBlocked)
			break;
		DreamRotationSupport::ApplyActorTransform(Owner, TargetTransform, StandingCharacters);
		CurrentAngleDegrees = NextAngle;
	}
	return CurrentAngleDegrees;
}

void UDreamDragRotatableComponent::DrawDebugRange() const
{
	if (!GetWorld())
		return;
	FVector Radial =
		FVector::VectorPlaneProject(ReferenceActorTransform.GetLocation() - ReferencePivotWorld, ReferenceAxisWorld)
			.GetSafeNormal();
	if (Radial.IsNearlyZero())
	{
		FVector Other;
		ReferenceAxisWorld.FindBestAxisVectors(Radial, Other);
	}
	const float Radius = FMath::Max(1.0f, DebugArcRadius);
	auto PointAt = [&](float Angle)
	{
		return ReferencePivotWorld +
			   FQuat(ReferenceAxisWorld, FMath::DegreesToRadians(Angle)).RotateVector(Radial) * Radius;
	};
	const float Negative = -FMath::Max(0.0f, NegativeAngleLimit);
	const float Positive = FMath::Max(0.0f, PositiveAngleLimit);
	// 多圈范围的调试轨迹会重叠，仍画出首尾径向及当前角度；分段数上限防止调试造成卡顿。
	const int32 Segments = FMath::Clamp(FMath::CeilToInt((Positive - Negative) / 5.0f), 1, 720);
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float From = FMath::Lerp(Negative, Positive, static_cast<float>(Index) / Segments);
		const float To = FMath::Lerp(Negative, Positive, static_cast<float>(Index + 1) / Segments);
		DrawDebugLine(
			GetWorld(), PointAt(From), PointAt(To), From < 0.0f ? FColor::Red : FColor::Green, false, -1.0f, 0, 2.0f);
	}
	DrawDebugLine(GetWorld(), ReferencePivotWorld, PointAt(Negative), FColor::Red, false, -1.0f);
	DrawDebugLine(GetWorld(), ReferencePivotWorld, PointAt(Positive), FColor::Green, false, -1.0f);
	DrawDebugLine(GetWorld(), ReferencePivotWorld, PointAt(CurrentAngleDegrees), FColor::Cyan, false, -1.0f, 0, 3.0f);
	DrawDebugDirectionalArrow(GetWorld(), ReferencePivotWorld, ReferencePivotWorld + ReferenceAxisWorld * Radius, 15.0f,
		FColor::Magenta, false, -1.0f, 0, 3.0f);
}
