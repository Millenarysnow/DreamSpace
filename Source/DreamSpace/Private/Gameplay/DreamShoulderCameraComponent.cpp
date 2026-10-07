#include "DreamShoulderCameraComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"

static bool bDreamShoulderCameraDebug = false;
static FAutoConsoleVariableRef CVarDreamShoulderCameraDebug(
	TEXT("dream.DebugShoulderCamera"), bDreamShoulderCameraDebug,
	TEXT("显示越肩相机的理想位置、安全位置、距离恢复和肩位收窄。0=关闭，1=开启。"), ECVF_Default);

UDreamShoulderCameraComponent::UDreamShoulderCameraComponent()
{
	TargetArmLength = 210.0f;
	SocketOffset = FVector(0.0f, 45.0f, 0.0f);
	ProbeSize = 18.0f;
	ProbeChannel = ECC_Camera;
	bDoCollisionTest = true;
	bEnableCameraLag = true;
	bEnableCameraRotationLag = false;
}

float UDreamShoulderCameraComponent::Damp(float Current, float Target, float HalfLife, float DeltaTime)
{
	// 指数衰减与帧率无关；不能使用固定每帧 Lerp 系数，否则 30/60/120 FPS 的手感会不同。
	if (HalfLife <= UE_SMALL_NUMBER)
		return Target;
	const float Alpha = 1.0f - FMath::Exp(-0.69314718056f * FMath::Max(DeltaTime, 0.0f) / HalfLife);
	return FMath::Lerp(Current, Target, Alpha);
}

FVector UDreamShoulderCameraComponent::GetGravityUp() const
{
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
		if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
			return -Movement->GetGravityDirection().GetSafeNormal();
	return GetComponentQuat().GetAxisZ();
}

UDreamShoulderCameraComponent::FCameraSweep UDreamShoulderCameraComponent::SweepCamera(
	const FVector& Start, const FVector& End, float Radius) const
{
	FCameraSweep Result;
	Result.SafeDistance = FVector::Distance(Start, End);
	if (!GetWorld() || Result.SafeDistance <= UE_SMALL_NUMBER)
		return Result;

	// 与原生 SpringArm 一样使用简单碰撞和 Camera 通道，并忽略持有相机的整个 Actor。
	// 关卡可以单独配置 Camera 响应，不需要改变 Pawn 通行或 Visibility 交互射线的规则。
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DreamShoulderCamera), false, GetOwner());
	GetWorld()->SweepSingleByChannel(Result.Hit, Start, End, FQuat::Identity, ProbeChannel,
		FCollisionShape::MakeSphere(FMath::Max(Radius, 1.0f)), Params);
	if (Result.Hit.bBlockingHit)
	{
		// 起点已在障碍内部时没有沿这条射线可证明安全的距离，退回枢轴并通过调试暴露此情况。
		// 不把负距离或穿透深度当成有效臂长；这类关卡配置仍需修正碰撞体或出生位置。
		Result.SafeDistance = Result.Hit.bStartPenetrating
			? 0.0f : FMath::Max(0.0f, Result.Hit.Distance - FMath::Max(CollisionSafetyMargin, 0.0f));
	}
	return Result;
}

FTransform UDreamShoulderCameraComponent::GetIdealCameraTransform(float InArmLength) const
{
	const FVector Pivot = bHasCameraState ? ObserverPivot : GetComponentLocation() + TargetOffset;
	const FRotator Rotation = bHasCameraState ? ObserverRotation : GetTargetRotation();
	// TargetOffset 在世界空间，SocketOffset 在相机旋转空间；与 UE 原生 SpringArm 的约定一致。
	return FTransform(Rotation,
		Pivot + Rotation.RotateVector(SocketOffset - FVector(FMath::Max(InArmLength, 0.0f), 0.0f, 0.0f)));
}

void UDreamShoulderCameraComponent::UpdateDesiredArmLocation(
	bool bDoTrace, bool bDoLocationLag, bool bDoRotationLag, float DeltaTime)
{
	// 越肩模式不对观察旋转额外施加阻尼。重力旋转由控制器和机关同步更新，直接继承其结果。
	// 仍保留基类虚函数签名，使 SpringArm 的注册、Tick 和 Socket 查询能继续使用本组件。
	(void)bDoRotationLag;
	const float Step = FMath::Max(DeltaTime, 0.0f);
	const FVector Anchor = GetComponentLocation() + TargetOffset;
	const FVector OwnerLocation = GetOwner() ? GetOwner()->GetActorLocation() : Anchor;
	const FVector Up = GetGravityUp();
	const FRotator Rotation = GetTargetRotation();
	const bool bReset = !bHasCameraState || FVector::DistSquared(Anchor, PreviousAnchor)
		> FMath::Square(FMath::Max(TeleportResetDistance, 1.0f));

	if (bReset)
	{
		SmoothedArmLength = FMath::Max(TargetArmLength, 0.0f);
		ShoulderWeight = 1.0f;
		HeightLag = 0.0f;
		DistanceRecoveryRemaining = 0.0f;
		ShoulderRecoveryRemaining = 0.0f;
		bRecoveringDistance = false;
		bDistanceSpaceIncreasing = false;
		bShoulderSpaceIncreasing = false;
		PreviousAnchor = Anchor;
	}
	else
	{
		SmoothedArmLength = Damp(SmoothedArmLength, FMath::Max(TargetArmLength, 0.0f), ZoomHalfLife, Step);
		if (!Up.Equals(PreviousGravityUp, 0.0001f))
		{
			// 机关旋转时先把上一帧枢轴搬到新的重力参考系；不把平台公转误认为一次楼梯起伏。
			// 高度滞后是标量，始终沿当前 Up 使用，不会继续沿旧世界 Z 轴追赶。
			const FQuat GravityDelta = FQuat::FindBetweenNormals(PreviousGravityUp, Up);
			// TargetOffset 按引擎约定固定在世界空间，先剔除再旋转局部高度，最后加回原世界偏移。
			PreviousAnchor = OwnerLocation + TargetOffset
				+ GravityDelta.RotateVector(PreviousAnchor - PreviousOwnerLocation - TargetOffset);
		}
		HeightLag = bDoLocationLag
			? FMath::Clamp(Damp(HeightLag + FVector::DotProduct(PreviousAnchor - Anchor, Up),
				0.0f, HeightFollowHalfLife, Step), -FMath::Max(MaxHeightLag, 0.0f), FMath::Max(MaxHeightLag, 0.0f))
			: 0.0f;
	}

	ObserverPivot = Anchor + Up * HeightLag;
	ObserverRotation = Rotation;
	PreviousAnchor = Anchor;
	PreviousOwnerLocation = OwnerLocation;
	PreviousGravityUp = Up;
	bHasCameraState = true;

	// 高度跟随也可能让枢轴短暂落到台阶/天花板另一侧，因此先验证真实锚点到跟随枢轴的路径。
	// 这一步只限制避障枢轴；手办仍使用上面的无碰撞 ObserverPivot，避免被障碍带着抖动。
	FVector CollisionPivot = ObserverPivot;
	if (bDoTrace && !Anchor.Equals(CollisionPivot, 0.001f))
	{
		const FCameraSweep PivotSweep = SweepCamera(Anchor, CollisionPivot, ProbeSize);
		CollisionPivot = Anchor + (CollisionPivot - Anchor).GetSafeNormal() * PivotSweep.SafeDistance;
	}

	const FVector FullLocalOffset = SocketOffset - FVector(SmoothedArmLength, 0.0f, 0.0f);
	float TargetShoulderWeight = 1.0f;
	if (bDoTrace && !FMath::IsNearlyZero(SocketOffset.Y))
	{
		// 用同样大小的预警球比较完整肩位和中轴，只有侧向偏移明显减少可用距离时才收窄。
		// 正后方的大墙会同时挡住两条路径，不会仅因为墙近就反复左右移动镜头。
		const FVector FullEnd = CollisionPivot + Rotation.RotateVector(FullLocalOffset);
		FVector CenterOffset = FullLocalOffset;
		CenterOffset.Y = 0.0f;
		const FVector CenterEnd = CollisionPivot + Rotation.RotateVector(CenterOffset);
		const float PredictionRadius = FMath::Max(ProbeSize, 1.0f) + FMath::Max(AnticipationPadding, 0.0f);
		const FCameraSweep FullSweep = SweepCamera(CollisionPivot, FullEnd, PredictionRadius);
		const FCameraSweep CenterSweep = SweepCamera(CollisionPivot, CenterEnd, PredictionRadius);
		// 比较后退方向上的有效距离，而非斜射线长度，避免肩偏移本身的勾股差触发误收窄。
		const float FullFraction = FullSweep.SafeDistance / FMath::Max(FVector::Distance(CollisionPivot, FullEnd), 1.0f);
		const float CenterFraction = CenterSweep.SafeDistance / FMath::Max(FVector::Distance(CollisionPivot, CenterEnd), 1.0f);
		const float Benefit = FMath::Max(0.0f, (CenterFraction - FullFraction) * SmoothedArmLength);
		TargetShoulderWeight = 1.0f - FMath::Clamp(Benefit / FMath::Max(ShoulderNarrowingDistance, 1.0f), 0.0f, 1.0f);
	}

	// 等待从“目标肩位开始变宽”这一刻开始，而非仅从最后一次收窄开始。
	// 否则在柱子旁停留很久后，计时早已耗尽，一帧无遮挡就会立刻向外移动。
	const bool bWiderShoulderAvailable = TargetShoulderWeight > ShoulderWeight + 0.001f;
	if (bWiderShoulderAvailable && !bShoulderSpaceIncreasing)
		ShoulderRecoveryRemaining = FMath::Max(RecoveryDelay, 0.0f);
	const float ShoulderRecoveryStep = FMath::Max(0.0f, Step - ShoulderRecoveryRemaining);
	ShoulderRecoveryRemaining = FMath::Max(0.0f, ShoulderRecoveryRemaining - Step);
	bShoulderSpaceIncreasing = bWiderShoulderAvailable;
	if (TargetShoulderWeight < ShoulderWeight - 0.001f)
	{
		ShoulderWeight = Damp(ShoulderWeight, TargetShoulderWeight, ShoulderRetractionHalfLife, Step);
		ShoulderRecoveryRemaining = FMath::Max(RecoveryDelay, 0.0f);
	}
	else if (ShoulderRecoveryRemaining <= 0.0f)
	{
		ShoulderWeight = Damp(ShoulderWeight, TargetShoulderWeight, ShoulderRecoveryHalfLife, ShoulderRecoveryStep);
	}

	FVector LocalOffset = FullLocalOffset;
	LocalOffset.Y *= ShoulderWeight;
	const FVector Desired = CollisionPivot + Rotation.RotateVector(LocalOffset);
	const FVector Ray = Desired - CollisionPivot;
	const float DesiredDistance = Ray.Size();
	FCameraSweep ActualSweep;
	ActualSweep.SafeDistance = DesiredDistance;
	float AllowedDistance = DesiredDistance;
	if (bDoTrace)
	{
		// 肩位插值后的实际路径必须重新检查。不能直接混合两条安全路径的端点，墙角中间可能有墙。
		ActualSweep = SweepCamera(CollisionPivot, Desired, ProbeSize);
		const FCameraSweep Prediction = SweepCamera(CollisionPivot, Desired,
			FMath::Max(ProbeSize, 1.0f) + FMath::Max(AnticipationPadding, 0.0f));
		// 预警球比实际球大：它的起点穿透不能证明相机已经穿透，此时只信任实际球的结果。
		AllowedDistance = Prediction.Hit.bStartPenetrating
			? ActualSweep.SafeDistance : FMath::Min(ActualSweep.SafeDistance, Prediction.SafeDistance);
	}

	const bool bObstructed = AllowedDistance < DesiredDistance - 0.1f;
	const bool bMoreDistanceAvailable = bRecoveringDistance && AllowedDistance > CameraDistance + 0.01f;
	if (bMoreDistanceAvailable && !bDistanceSpaceIncreasing)
		DistanceRecoveryRemaining = FMath::Max(RecoveryDelay, 0.0f);
	const float DistanceRecoveryStep = FMath::Max(0.0f, Step - DistanceRecoveryRemaining);
	DistanceRecoveryRemaining = FMath::Max(0.0f, DistanceRecoveryRemaining - Step);
	bDistanceSpaceIncreasing = bMoreDistanceAvailable;
	if (bReset || !bDoTrace || (!bObstructed && !bRecoveringDistance))
	{
		// 正常开放空间只有缩放这一层阻尼，避免再叠加碰撞恢复而使滚轮产生过量延迟。
		CameraDistance = AllowedDistance;
	}
	else if (AllowedDistance < CameraDistance - 0.01f)
	{
		CameraDistance = Damp(CameraDistance, AllowedDistance, RetractionHalfLife, Step);
		DistanceRecoveryRemaining = FMath::Max(RecoveryDelay, 0.0f);
	}
	else if (DistanceRecoveryRemaining <= 0.0f)
	{
		CameraDistance = Damp(CameraDistance, AllowedDistance, RecoveryHalfLife, DistanceRecoveryStep);
	}
	// 安全距离是硬约束，墙突然进入路径时必须及时夹紧；任何平滑都不能让镜头留在墙内。
	CameraDistance = FMath::Clamp(CameraDistance, 0.0f, ActualSweep.SafeDistance);
	bRecoveringDistance = bObstructed || CameraDistance < DesiredDistance - 0.1f;
	if (!bRecoveringDistance)
		CameraDistance = FMath::Min(DesiredDistance, ActualSweep.SafeDistance);

	const FVector Actual = CollisionPivot + Ray.GetSafeNormal() * CameraDistance;
	UnfixedCameraPosition = GetIdealCameraTransform(SmoothedArmLength).GetLocation();
	bIsCameraFixed = !Actual.Equals(UnfixedCameraPosition, 0.1f);
	PreviousDesiredLoc = ObserverPivot;
	PreviousArmOrigin = Anchor;
	PreviousDesiredRot = Rotation;
	// 发布基类 Socket 缓存即可驱动 FollowCamera，不需要在角色 Tick 或控制器里再次移动相机。
	const FTransform RelativeCamera = FTransform(Rotation, Actual).GetRelativeTransform(GetComponentTransform());
	RelativeSocketLocation = RelativeCamera.GetLocation();
	RelativeSocketRotation = RelativeCamera.GetRotation();
	UpdateChildTransforms();
	UpdateOwnerVisibility(Actual);
	if (bDreamShoulderCameraDebug)
		DrawCameraDebug(CollisionPivot, UnfixedCameraPosition, Actual, ActualSweep);
}

void UDreamShoulderCameraComponent::UpdateOwnerVisibility(const FVector& CameraLocation)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!bHideOwnerWhenTooClose || !Character || !Character->IsLocallyControlled() || !IsActive())
	{
		RestoreOwnerVisibility();
		return;
	}
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	const float Distance = FVector::Distance(CameraLocation, Character->GetActorLocation());
	const float HideAt = FMath::Max(OwnerHideDistance, 0.0f);
	const float ShowAt = FMath::Max(OwnerShowDistance, HideAt + 1.0f);
	if (HiddenOwnerMesh.IsValid() && (HiddenOwnerMesh.Get() != Mesh || Distance >= ShowAt))
		RestoreOwnerVisibility();
	if (!HiddenOwnerMesh.IsValid() && Mesh && Distance < HideAt)
	{
		bSavedOwnerNoSee = Mesh->bOwnerNoSee;
		HiddenOwnerMesh = Mesh;
		Mesh->SetOwnerNoSee(true);
	}
}

void UDreamShoulderCameraComponent::RestoreOwnerVisibility()
{
	if (USkeletalMeshComponent* Mesh = HiddenOwnerMesh.Get())
		Mesh->SetOwnerNoSee(bSavedOwnerNoSee);
	HiddenOwnerMesh.Reset();
}

void UDreamShoulderCameraComponent::ResetCameraState()
{
	bHasCameraState = false;
	RestoreOwnerVisibility();
}

void UDreamShoulderCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreOwnerVisibility();
	Super::EndPlay(EndPlayReason);
}

void UDreamShoulderCameraComponent::OnUnregister()
{
	ResetCameraState();
	Super::OnUnregister();
}

void UDreamShoulderCameraComponent::Deactivate()
{
	// 停用会关闭 Tick，不能等下一帧再清理近距隐藏；切镜头时主动恢复人物并重建跟随历史。
	ResetCameraState();
	Super::Deactivate();
}

void UDreamShoulderCameraComponent::ApplyWorldOffset(const FVector& InOffset, bool bWorldShift)
{
	Super::ApplyWorldOffset(InOffset, bWorldShift);
	// 世界原点迁移不是玩家传送，所有绝对位置缓存一起平移，保留正在进行的恢复过程。
	PreviousAnchor += InOffset;
	PreviousOwnerLocation += InOffset;
	ObserverPivot += InOffset;
}

void UDreamShoulderCameraComponent::DrawCameraDebug(
	const FVector& Pivot, const FVector& Ideal, const FVector& Actual, const FCameraSweep& ActualSweep) const
{
	UWorld* World = GetWorld();
	if (!World)
		return;
	DrawDebugSphere(World, Pivot, 5.0f, 12, FColor::Green);
	DrawDebugSphere(World, Ideal, ProbeSize, 16, FColor::White);
	DrawDebugSphere(World, Actual, ProbeSize, 16, FColor::Orange);
	DrawDebugLine(World, Pivot, Ideal, FColor::White);
	DrawDebugLine(World, Pivot, Actual, FColor::Orange);
	if (ActualSweep.Hit.bBlockingHit)
		DrawDebugPoint(World, ActualSweep.Hit.ImpactPoint, 10.0f, FColor::Red);
	if (GEngine)
	{
		const FString Message = FString::Printf(
			TEXT("越肩相机  理想 %.0f  实际 %.0f cm  肩位 %.2f  恢复等待 %.2f s\n阻挡: %s  起点穿透: %s  人物隐藏: %s"),
			SmoothedArmLength, CameraDistance, ShoulderWeight, DistanceRecoveryRemaining,
			*GetNameSafe(ActualSweep.Hit.GetActor()), ActualSweep.Hit.bStartPenetrating ? TEXT("是") : TEXT("否"),
			HiddenOwnerMesh.IsValid() ? TEXT("是") : TEXT("否"));
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.0f, FColor::Orange, Message);
	}
}
