#include "DreamDraggableComponent.h"

#include "DreamInteractionCollision.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Actor.h"

void UDreamDraggableComponent::OnReferenceInitialized()
{
	const FVector Offset = ReferenceActorTransform.GetLocation() - ReferencePivotWorld;
	CurrentTranslation = FVector::DotProduct(Offset, ReferenceAxisWorld);
	PerpendicularOffset = Offset - ReferenceAxisWorld * CurrentTranslation;
	// 初始化只记录真实摆放，不隐式移动越界 Actor；第一次非零输入或蓝图请求再按范围限制。
	// 这样开局或重新抓取不会跳变，关卡作者仍应把初始轴向坐标放在配置范围内。
}

bool UDreamDraggableComponent::TryGetRayCoordinate(
	const FVector& RayOrigin, const FVector& RayDirection, double& OutCoordinate) const
{
	const FVector ToRay = RayOrigin - (ReferencePivotWorld + PerpendicularOffset);
	const double Alignment = FVector::DotProduct(ReferenceAxisWorld, RayDirection);
	const double Denominator = 1.0 - Alignment * Alignment;
	if (Denominator < 0.001)
		return false;

	// 联立两条直线最近点的正交条件；轴坐标以枢轴为零点，垂直偏移不影响坐标定义。
	// 使用 double 与 UE5 的大世界坐标匹配，避免远处手办场景中的拖动输入丢失精度。
	const double AlongAxis = FVector::DotProduct(ReferenceAxisWorld, ToRay);
	const double AlongRay = FVector::DotProduct(RayDirection, ToRay);
	const double RayDistance = (Alignment * AlongAxis - AlongRay) / Denominator;
	OutCoordinate = (AlongAxis - Alignment * AlongRay) / Denominator;
	return RayDistance >= 0.0 && FMath::IsFinite(OutCoordinate);
}

void UDreamDraggableComponent::InitializeDragSample(const FVector& RayOrigin, const FVector& RayDirection)
{
	bHasRaySample = TryGetRayCoordinate(RayOrigin, RayDirection, LastRayCoordinate);
	bUseScreenFallback = !bHasRaySample;
}

void UDreamDraggableComponent::RebaseDragSample(const FVector& RayOrigin, const FVector& RayDirection)
{
	if (!bUseScreenFallback)
		bHasRaySample = TryGetRayCoordinate(RayOrigin, RayDirection, LastRayCoordinate);
}

void UDreamDraggableComponent::ApplyDragSample(
	const FVector& RayOrigin, const FVector& RayDirection, const FVector2D& PointerDelta)
{
	float Delta = 0.0f;
	if (bUseScreenFallback)
	{
		Delta = (PointerDelta.X - PointerDelta.Y) * FMath::Max(0.0f, FallbackCentimetersPerPixel);
	}
	else
	{
		double Coordinate = 0.0;
		if (!TryGetRayCoordinate(RayOrigin, RayDirection, Coordinate))
		{
			bHasRaySample = false;
			return;
		}
		if (bHasRaySample)
			Delta = Coordinate - LastRayCoordinate;
		// 每次都消耗输入，包括被范围或碰撞截断的部分。玩家反向拖动时立即有响应，
		// 不必先把鼠标退回越界距离，也不会把受阻期间的输入积攒成后续突跳。
		LastRayCoordinate = Coordinate;
		bHasRaySample = true;
	}
	Delta *= FMath::Max(0.0f, DragSensitivity);
	if (!FMath::IsNearlyZero(Delta))
		SetTranslation(CurrentTranslation + Delta);
}

float UDreamDraggableComponent::SetTranslation(float Translation)
{
	if (!FMath::IsFinite(Translation) || !EnsureReference())
		return CurrentTranslation;
	const float TargetCoordinate =
		FMath::Clamp(Translation, -FMath::Max(0.0f, NegativeLimit), FMath::Max(0.0f, PositiveLimit));
	AActor* Owner = GetOwner();
	const FVector Start = Owner->GetActorLocation();
	const FVector Target = ReferencePivotWorld + PerpendicularOffset + ReferenceAxisWorld * TargetCoordinate;
	const FTransform TargetTransform(Owner->GetActorQuat(), Target, Owner->GetActorScale3D());
	const float SafeFraction =
		bConsiderCollision ? DreamInteractionCollision::FindSafeMoveFraction(Owner, TargetTransform) : 1.0f;
	Owner->SetActorLocation(FMath::Lerp(Start, Target, SafeFraction), false, nullptr, ETeleportType::None);
	// 逻辑值从实际世界位置回读，碰撞阻挡后返回的坐标必须与画面一致。
	CurrentTranslation = FVector::DotProduct(Owner->GetActorLocation() - ReferencePivotWorld, ReferenceAxisWorld);
	return CurrentTranslation;
}

void UDreamDraggableComponent::DrawDebugRange() const
{
	if (!GetWorld() || !GetOwner())
		return;
	const FVector Center = ReferencePivotWorld + PerpendicularOffset;
	const FVector NegativeEnd = Center - ReferenceAxisWorld * FMath::Max(0.0f, NegativeLimit);
	const FVector PositiveEnd = Center + ReferenceAxisWorld * FMath::Max(0.0f, PositiveLimit);
	DrawDebugLine(GetWorld(), NegativeEnd, Center, FColor::Red, false, -1.0f, 0, 3.0f);
	DrawDebugLine(GetWorld(), Center, PositiveEnd, FColor::Green, false, -1.0f, 0, 3.0f);
	DrawDebugSphere(GetWorld(), NegativeEnd, 7.0f, 12, FColor::Red, false, -1.0f);
	DrawDebugSphere(GetWorld(), PositiveEnd, 7.0f, 12, FColor::Green, false, -1.0f);
	DrawDebugSphere(GetWorld(), GetOwner()->GetActorLocation(), 6.0f, 12, FColor::Cyan, false, -1.0f);
	DrawDebugLine(GetWorld(), ReferencePivotWorld, Center, FColor::Yellow, false, -1.0f);
}
