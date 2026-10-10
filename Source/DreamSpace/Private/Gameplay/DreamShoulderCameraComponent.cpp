#include "DreamShoulderCameraComponent.h"

#include "DreamSceneCapturePresentationComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "SceneView.h"

namespace
{
	// 参数名同时用于生成脚本和材质资产。只给声明了 Amount 的材质创建 MID，未知材质保持可见。
	const FName ClipAmountParameter(TEXT("DreamOwnerClipAmount"));
	const FName ClipRadiusParameter(TEXT("DreamOwnerClipRadius"));
	const FName ClipFeatherParameter(TEXT("DreamOwnerClipFeather"));
	const FName ClipCameraLocalParameter(TEXT("DreamOwnerClipCameraLocal"));
	const FName ClipCameraForwardParameter(TEXT("DreamOwnerClipCameraForward"));
}

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

FQuat UDreamShoulderCameraComponent::MakeMiniatureInspectionRotation(const FVector& ViewDirection) const
{
	const FVector Up = GetGravityUp();
	FVector Horizontal = FVector::VectorPlaneProject(ViewDirection, Up).GetSafeNormal();
	// 探索镜头恰好从头顶/脚底看下来时，水平投影会退化；沿角色正前方建立稳定的观察方向。
	if (Horizontal.IsNearlyZero())
		Horizontal = FVector::VectorPlaneProject(GetOwner()->GetActorForwardVector(), Up).GetSafeNormal();
	if (Horizontal.IsNearlyZero())
		Horizontal = FRotationMatrix::MakeFromZ(Up).GetUnitAxis(EAxis::X);
	const float Angle = FMath::DegreesToRadians(FMath::Clamp(MiniatureLookDownAngle, 0.0f, 60.0f));
	const FVector Forward = Horizontal * FMath::Cos(Angle) - Up * FMath::Sin(Angle);
	return FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();
}

void UDreamShoulderCameraComponent::BeginMiniatureCameraBlend()
{
	const FTransform OwnerTransform = GetOwner()->GetActorTransform();
	// Socket 是已经过避障并实际显示出来的镜头。中途反向切换也从这里开始，不能复用旧动画起点。
	MiniatureBlendStartLocal = GetSocketTransform(USpringArmComponent::SocketName, RTS_World)
		.GetRelativeTransform(OwnerTransform);
	MiniatureBlendStartPivotLocal = OwnerTransform.InverseTransformPosition(
		bHasPublishedCameraPose ? PublishedCameraPivot : GetComponentLocation() + TargetOffset);
	MiniatureCameraBlendElapsed = 0.0f;
	MiniatureCameraBlendDuration = FMath::Max(MiniatureTransitionDuration, 0.0f);
	bMiniatureCameraBlending = MiniatureCameraBlendDuration > UE_SMALL_NUMBER;
	MiniatureCameraBlendAlpha = bMiniatureCameraBlending ? 0.0f : 1.0f;
}

void UDreamShoulderCameraComponent::PublishCameraPose(
	const FVector& Pivot, const FVector& Location, const FQuat& Rotation, bool bDoTrace, float DeltaTime)
{
	FVector ActualPivot = Pivot;
	FVector ActualLocation = Location;
	FQuat ActualRotation = Rotation;
	if (bMiniatureCameraBlending)
	{
		MiniatureCameraBlendElapsed += FMath::Max(DeltaTime, 0.0f);
		const float Time = FMath::Clamp(MiniatureCameraBlendElapsed / MiniatureCameraBlendDuration, 0.0f, 1.0f);
		// 有限时长的缓入缓出曲线，起止速度均为零；不依赖每帧 Lerp 系数，因此不同帧率同时抵达。
		MiniatureCameraBlendAlpha = Time * Time * (3.0f - 2.0f * Time);
		const FTransform OwnerTransform = GetOwner()->GetActorTransform();
		const FTransform Start = MiniatureBlendStartLocal * OwnerTransform;
		ActualPivot = FMath::Lerp(OwnerTransform.TransformPosition(MiniatureBlendStartPivotLocal), Pivot, MiniatureCameraBlendAlpha);
		ActualLocation = FMath::Lerp(Start.GetLocation(), Location, MiniatureCameraBlendAlpha);
		ActualRotation = FQuat::Slerp(Start.GetRotation(), Rotation, MiniatureCameraBlendAlpha).GetNormalized();
		if (bDoTrace)
		{
			// 安全的起终点不代表中间构图安全。先保证过渡枢轴可达，再对本帧镜头执行真实球扫掠。
			const FCameraSweep PivotSweep = SweepCamera(Pivot, ActualPivot, ProbeSize);
			ActualPivot = Pivot + (ActualPivot - Pivot).GetSafeNormal() * PivotSweep.SafeDistance;
			const FVector Ray = ActualLocation - ActualPivot;
			const FCameraSweep CameraSweep = SweepCamera(ActualPivot, ActualLocation, ProbeSize);
			const FVector SafeLocation = ActualPivot + Ray.GetSafeNormal() * CameraSweep.SafeDistance;
			bIsCameraFixed |= !SafeLocation.Equals(ActualLocation, 0.1f);
			ActualLocation = SafeLocation;
		}
		if (Time >= 1.0f)
			bMiniatureCameraBlending = false;
	}
	PublishedCameraPivot = ActualPivot;
	bHasPublishedCameraPose = true;
	const FTransform RelativeCamera = FTransform(ActualRotation, ActualLocation).GetRelativeTransform(GetComponentTransform());
	RelativeSocketLocation = RelativeCamera.GetLocation();
	RelativeSocketRotation = RelativeCamera.GetRotation();
	UpdateChildTransforms();
	UpdateOwnerClipping(ActualLocation, ActualRotation.Rotator(), DeltaTime);
}

bool UDreamShoulderCameraComponent::BeginMiniatureFocus(
	UDreamSceneCapturePresentationComponent* Miniature, const FMinimalViewInfo& PlayerPOV)
{
	if (!IsValid(Miniature) || !Miniature->IsInspecting() || Miniature->GetOwner() != GetOwner() || !IsActive())
		return false;
	const FVector Forward = (Miniature->GetDisplayCenter() - PlayerPOV.Location).GetSafeNormal();
	if (Forward.IsNearlyZero())
		return false;
	const FQuat Rotation = MakeMiniatureInspectionRotation(Forward);
	BeginMiniatureCameraBlend();
	MiniatureFocusLocalRotation = GetOwner()->GetActorQuat().Inverse() * Rotation;
	MiniatureFocusTarget = Miniature;
	// 先建立观察目标，但以零时间发布过渡起点；按 Tab 的这一帧仍保持原姿态，下一帧才开始移动。
	UpdateMiniatureFocus(bDoCollisionTest, 0.0f);
	return true;
}

void UDreamShoulderCameraComponent::EndMiniatureFocus(bool bBlend)
{
	if (MiniatureFocusTarget.IsExplicitlyNull() && bBlend)
		return;
	if (bBlend && IsRegistered() && IsActive())
		BeginMiniatureCameraBlend();
	else
	{
		bMiniatureCameraBlending = false;
		MiniatureCameraBlendAlpha = 1.0f;
	}
	MiniatureFocusTarget.Reset();
	// 只重建探索跟随历史，保留局部剔除的当前强度；退出动画不能突然把贴近镜头的人物表面补回来。
	bHasCameraState = false;
	if (IsRegistered() && IsActive())
		UpdateDesiredArmLocation(bDoCollisionTest, false, false, 0.0f);
}

bool UDreamShoulderCameraComponent::UpdateMiniatureFocus(bool bDoTrace, float DeltaTime)
{
	UDreamSceneCapturePresentationComponent* Miniature = MiniatureFocusTarget.Get();
	if (!Miniature || !Miniature->IsInspecting() || !Miniature->IsPresentationActive())
	{
		if (!MiniatureFocusTarget.IsExplicitlyNull())
		{
			// 目标失效属于生命周期清理，不继续播放对旧对象的动画；外层立即重建探索 Socket。
			MiniatureFocusTarget.Reset();
			bHasCameraState = false;
			bMiniatureCameraBlending = false;
			MiniatureCameraBlendAlpha = 1.0f;
		}
		return false;
	}

	UCameraComponent* Camera = GetOwner()->FindComponentByClass<UCameraComponent>();
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const APlayerController* Controller = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	int32 Width = 0, Height = 0;
	if (Controller)
		Controller->GetViewportSize(Width, Height);
	const ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
	if (Width > 0 && Height > 0 && LocalPlayer)
	{
		// 分屏时按该玩家实际占用的子视口计算，不能用整个游戏窗口的宽高比。
		Width = FMath::Max(1, FMath::RoundToInt(Width * LocalPlayer->Size.X));
		Height = FMath::Max(1, FMath::RoundToInt(Height * LocalPlayer->Size.Y));
	}
	if (Width <= 0 || Height <= 0)
	{
		Width = 1920;
		Height = FMath::Max(1, FMath::RoundToInt(Width / FMath::Max(Camera ? Camera->AspectRatio : 16.0f / 9.0f, 0.1f)));
	}
	FMinimalViewInfo View;
	if (Camera)
		Camera->GetCameraView(0.0f, View);
	FSceneViewProjectionData Projection;
	const FIntRect ViewRect(0, 0, Width, Height);
	Projection.SetViewRectangle(ViewRect);
	// 由引擎处理 MaintainX/MaintainY/MajorAxisFOV 和相机宽高比配置，避免复制投影公式。
	// 投影矩阵两条对角线分别为水平/垂直半张角的 cot，乘半边长就是各方向的最小取景距离。
	FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(View,
		LocalPlayer ? LocalPlayer->AspectRatioAxisConstraint.GetValue() : AspectRatio_MaintainXFOV, ViewRect, Projection);
	const FVector2D DisplaySize = Miniature->GetDisplaySize();
	const float FramingDistance = 0.5f * FMath::Max(
		DisplaySize.X * Projection.ProjectionMatrix.M[0][0], DisplaySize.Y * Projection.ProjectionMatrix.M[1][1]);
	const float Distance = FMath::Max(ProbeSize + CollisionSafetyMargin,
		FramingDistance / FMath::Clamp(MiniatureScreenFill, 0.1f, 0.9f));
	const FVector Center = Miniature->GetDisplayCenter();
	const FQuat Rotation = (GetOwner()->GetActorQuat() * MiniatureFocusLocalRotation).GetNormalized();
	const FVector Backward = -Rotation.GetForwardVector();
	UnfixedCameraPosition = Center + Backward * Distance;
	FCameraSweep Sweep;
	Sweep.SafeDistance = Distance;
	if (bDoTrace)
		Sweep = SweepCamera(Center, UnfixedCameraPosition, ProbeSize);
	const FVector Actual = Center + Backward * Sweep.SafeDistance;
	bIsCameraFixed = !Actual.Equals(UnfixedCameraPosition, 0.1f);

	// 只沿中心视线收近，碰撞前后光轴都穿过显示面中心；不存在越肩偏移造成的偏心。
	// 探索参数和历史不在这里修改；公共发布入口只平滑 Tab 切换，不影响右键展示旋转。
	PublishCameraPose(Center, Actual, Rotation, bDoTrace, DeltaTime);
	if (bDreamShoulderCameraDebug)
		DrawCameraDebug(Center, UnfixedCameraPosition, Actual, Sweep);
	return true;
}

void UDreamShoulderCameraComponent::UpdateDesiredArmLocation(
	bool bDoTrace, bool bDoLocationLag, bool bDoRotationLag, float DeltaTime)
{
	// 越肩模式不对观察旋转额外施加阻尼。重力旋转由控制器和机关同步更新，直接继承其结果。
	// 仍保留基类虚函数签名，使 SpringArm 的注册、Tick 和 Socket 查询能继续使用本组件。
	(void)bDoRotationLag;
	const float Step = FMath::Max(DeltaTime, 0.0f);
	if (UpdateMiniatureFocus(bDoTrace, Step))
		return;
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
	PublishCameraPose(CollisionPivot, Actual, Rotation.Quaternion(), bDoTrace, Step);
	if (bDreamShoulderCameraDebug)
		DrawCameraDebug(CollisionPivot, UnfixedCameraPosition, Actual, ActualSweep);
}

float UDreamShoulderCameraComponent::QueryOwnerSurfaceDistance(
	const USkeletalMeshComponent* Mesh, const FVector& CameraLocation) const
{
	// 物理资产的简单体随动画骨骼运动，比胶囊中心能更准确地感知肩背、头部和伸出的手臂。
	// false 表示真实表面距离，不使用“到骨骼中心距离”的快速近似；进入简单体内部返回 0。
	FClosestPointOnPhysicsAsset Closest;
	if (Mesh && Mesh->GetClosestPointOnPhysicsAsset(CameraLocation, Closest, false)
		&& FMath::IsFinite(Closest.Distance) && Closest.Distance >= 0.0f)
		return Closest.Distance;

	// 没有物理资产时退回胶囊表面。沿胶囊局部 Z 的线段求最近点，支持平台翻转与非世界 Z 重力。
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const float Radius = Capsule->GetScaledCapsuleRadius();
		const float AxisHalfLength = FMath::Max(0.0f, Capsule->GetScaledCapsuleHalfHeight() - Radius);
		const FVector Axis = Capsule->GetUpVector();
		const FVector Center = Capsule->GetComponentLocation();
		const float AlongAxis = FMath::Clamp(FVector::DotProduct(CameraLocation - Center, Axis),
			-AxisHalfLength, AxisHalfLength);
		return FMath::Max(0.0f, FVector::Distance(CameraLocation, Center + Axis * AlongAxis) - Radius);
	}
	return UE_BIG_NUMBER;
}

void UDreamShoulderCameraComponent::BindOwnerClipMaterials(USkeletalMeshComponent* Mesh)
{
	bool bBindingsMatch = ClippedOwnerMesh.Get() == Mesh && Mesh->GetNumMaterials() == OwnerClipMaterials.Num();
	for (int32 Slot = 0; bBindingsMatch && Slot < OwnerClipMaterials.Num(); ++Slot)
		bBindingsMatch = Mesh->GetMaterial(Slot) == (OwnerClipMaterials[Slot]
			? static_cast<UMaterialInterface*>(OwnerClipMaterials[Slot].Get()) : OriginalOwnerMaterials[Slot].Get());
	if (bBindingsMatch)
		return;

	// 网格或材质槽被换装逻辑替换后，先释放自己仍控制的槽，再为新材质建立独立实例。
	RestoreOwnerClipMaterials();
	ClippedOwnerMesh = Mesh;
	for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
	{
		UMaterialInterface* Source = Mesh->GetMaterial(Slot);
		OriginalOwnerMaterials.Add(Source);
		float DefaultAmount = 0.0f;
		UMaterialInstanceDynamic* Instance = nullptr;
		if (Source && Source->GetScalarParameterValue(FMaterialParameterInfo(ClipAmountParameter), DefaultAmount))
		{
			Instance = UMaterialInstanceDynamic::Create(Source, this);
			Instance->SetScalarParameterValue(ClipAmountParameter, 0.0f);
			Mesh->SetMaterial(Slot, Instance);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("越肩相机：材质 %s 未接入 DreamOwnerClipAmount，槽 %d 保持原材质可见。"),
				*GetNameSafe(Source), Slot);
		}
		OwnerClipMaterials.Add(Instance);
	}
}

void UDreamShoulderCameraComponent::UpdateOwnerClipping(
	const FVector& CameraLocation, const FRotator& CameraRotation, float DeltaTime)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	const APlayerController* Controller = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!bEnableOwnerLocalClipping || !Character || !Character->IsLocallyControlled() || !IsActive()
		|| !Controller || Controller->GetViewTarget() != Character || !Character->GetMesh())
	{
		RestoreOwnerClipMaterials();
		return;
	}
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	BindOwnerClipMaterials(Mesh);
	OwnerSurfaceDistance = QueryOwnerSurfaceDistance(Mesh, CameraLocation);
	const float FullAt = FMath::Max(0.0f, OwnerClipFullDistance);
	const float StartAt = FMath::Max(FullAt + 1.0f, OwnerClipStartDistance);
	const float Proximity = FMath::Clamp((StartAt - OwnerSurfaceDistance) / (StartAt - FullAt), 0.0f, 1.0f);
	// Smoothstep 在开始/完全接近处的导数为 0，不会把距离跨阈值变成整个网格的一次切换。
	const float TargetAmount = Proximity * Proximity * (3.0f - 2.0f * Proximity);
	const float MinRadius = FMath::Max(1.0f, OwnerClipMinRadius);
	const float TargetRadius = FMath::Lerp(MinRadius, FMath::Max(MinRadius, OwnerClipMaxRadius), TargetAmount);
	OwnerClipAmount = Damp(OwnerClipAmount, TargetAmount,
		TargetAmount > OwnerClipAmount ? OwnerClipInHalfLife : OwnerClipOutHalfLife, DeltaTime);
	OwnerClipRadius = Damp(OwnerClipRadius, TargetRadius,
		TargetRadius > OwnerClipRadius ? OwnerClipInHalfLife : OwnerClipOutHalfLife, DeltaTime);

	// 球心直接采用本帧真实镜头。发布网格局部坐标用于视角身份核对，避免世界大坐标精度损失。
	// 实际球形距离在材质中以世界厘米计算；角色缩放和重力翻转不改变剔除半径的单位。
	const FVector LocalCamera = Mesh->GetComponentTransform().InverseTransformPosition(CameraLocation);
	const FVector Forward = CameraRotation.Vector();
	for (UMaterialInstanceDynamic* Instance : OwnerClipMaterials)
	{
		if (!Instance)
			continue;
		Instance->SetScalarParameterValue(ClipAmountParameter, OwnerClipAmount);
		Instance->SetScalarParameterValue(ClipRadiusParameter, OwnerClipRadius);
		Instance->SetScalarParameterValue(ClipFeatherParameter, FMath::Clamp(OwnerClipFeather, 1.0f, OwnerClipRadius));
		Instance->SetVectorParameterValue(ClipCameraLocalParameter, FLinearColor(LocalCamera.X, LocalCamera.Y, LocalCamera.Z));
		Instance->SetVectorParameterValue(ClipCameraForwardParameter, FLinearColor(Forward.X, Forward.Y, Forward.Z));
	}
}

void UDreamShoulderCameraComponent::RestoreOwnerClipMaterials()
{
	for (int32 Slot = 0; Slot < OwnerClipMaterials.Num(); ++Slot)
	{
		UMaterialInstanceDynamic* Instance = OwnerClipMaterials[Slot];
		if (!Instance)
			continue;
		// 即使外部还持有该 MID，也先将它恢复为不剔除状态，再解除本组件的引用。
		Instance->SetScalarParameterValue(ClipAmountParameter, 0.0f);
		if (USkeletalMeshComponent* Mesh = ClippedOwnerMesh.Get())
			if (Slot < Mesh->GetNumMaterials() && Mesh->GetMaterial(Slot) == Instance)
				Mesh->SetMaterial(Slot, OriginalOwnerMaterials[Slot]);
	}
	ClippedOwnerMesh.Reset();
	OriginalOwnerMaterials.Reset();
	OwnerClipMaterials.Reset();
	OwnerClipAmount = 0.0f;
	OwnerClipRadius = FMath::Max(1.0f, OwnerClipMinRadius);
	OwnerSurfaceDistance = 0.0f;
}

void UDreamShoulderCameraComponent::ResetCameraState()
{
	bHasCameraState = false;
	bMiniatureCameraBlending = false;
	MiniatureCameraBlendAlpha = 1.0f;
	bHasPublishedCameraPose = false;
	RestoreOwnerClipMaterials();
}

void UDreamShoulderCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	MiniatureFocusTarget.Reset();
	bMiniatureCameraBlending = false;
	MiniatureCameraBlendAlpha = 1.0f;
	RestoreOwnerClipMaterials();
	Super::EndPlay(EndPlayReason);
}

void UDreamShoulderCameraComponent::OnUnregister()
{
	MiniatureFocusTarget.Reset();
	ResetCameraState();
	Super::OnUnregister();
}

void UDreamShoulderCameraComponent::Deactivate()
{
	// 停用会关闭 Tick，不能等下一帧再清理材质；切镜头时主动恢复原材质并重建跟随历史。
	MiniatureFocusTarget.Reset();
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
	PublishedCameraPivot += InOffset;
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
			TEXT("越肩相机  理想 %.0f  实际 %.0f cm  肩位 %.2f  恢复等待 %.2f s\n阻挡: %s  起点穿透: %s  局部剔除 %.2f / %.0f cm  身体表面距离 %.0f cm"),
			SmoothedArmLength, CameraDistance, ShoulderWeight, DistanceRecoveryRemaining,
			*GetNameSafe(ActualSweep.Hit.GetActor()), ActualSweep.Hit.bStartPenetrating ? TEXT("是") : TEXT("否"),
			OwnerClipAmount, OwnerClipRadius, OwnerSurfaceDistance);
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.0f, FColor::Orange, Message);
	}
}
