#include "DreamRotatableComponent.h"
#include "DreamPivotPointComponent.h"
#include "DreamInteractionCollision.h"
#include "DreamRotationSupport.h"
#include "DreamPuzzleDebug.h"
#include "DreamCharacter.h"
#include "DreamHUD.h"
#include "DreamSpace.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"

namespace
{
	// 钥匙条件是当前谜题的固定规则；反馈角度与时长独立于正常步进，0 秒开门也能给出可见反馈。
	constexpr float LockedFeedbackDuration = 0.35f;
	constexpr float LockedFeedbackAngleDegrees = 2.0f;
}

UDreamRotatableComponent::UDreamRotatableComponent()
{
	// Tick 同时承担平滑转动插值和调试绘制两件事。
	PrimaryComponentTick.bCanEverTick = true;
}

void UDreamRotatableComponent::BeginPlay()
{
	Super::BeginPlay();

	// 提前解析枢轴点并缓存，避免每次触发都做查找；
	// 找不到时提前报警，让配置错误在开局就暴露，而不是等到玩家交互时才静默失败。
	CachedPivot = ResolvePivot();
	UE_LOG(LogDreamSpace, Verbose,
		TEXT("可转动组件初始化：Actor=%s Pivot=%s Duration=%.3f"),
		*GetNameSafe(GetOwner()), *GetNameSafe(CachedPivot.Get()), RotationDuration);
	if (!CachedPivot && GetOwner())
	{
		UE_LOG(LogDreamSpace, Warning,
			TEXT("可转动组件：Actor [%s] 上找不到枢轴点组件（配置名称：%s），该组件将不会响应交互。"),
			*GetOwner()->GetName(), *PivotComponentName.ToString());
	}
}

void UDreamRotatableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopLockedFeedback();
	Super::EndPlay(EndPlayReason);
}

void UDreamRotatableComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	StopLockedFeedback();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UDreamRotatableComponent::Deactivate()
{
	// 停用后引擎不会再 Tick，必须立即回正，不能留下尚未完成的锁定晃动姿态。
	StopLockedFeedback();
	Super::Deactivate();
}

void UDreamRotatableComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 调试绘制与转动插值互不干扰，各自独立判断。
	DrawDebugRotationAxis();

	if (!bRotating)
		return;

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		bRotating = false;
		bReturning = false;
		bPlayingLockedFeedback = false;
		return;
	}

	// 只在真正运动的帧查询角色；同一帧所有碰撞子步复用这份名单，避免反复遍历世界。
	// 下一帧重新判断底座与移动模式，角色走下或跳离后立即停止跟随，重力值则自然保留。
	TArray<ACharacter*> StandingCharacters;
	GatherStandingCharacters(StandingCharacters);

	if (bPlayingLockedFeedback)
	{
		UpdateLockedFeedback(DeltaTime, StandingCharacters);
		return;
	}

	if (bReturning)
	{
		ReturnElapsed += DeltaTime;
		const float RawReturnAlpha = ReturnDuration <= 0.0f
			? 1.0f : FMath::Clamp(ReturnElapsed / ReturnDuration, 0.0f, 1.0f);
		const float Ease = RawReturnAlpha * RawReturnAlpha * (3.0f - 2.0f * RawReturnAlpha);
		const FTransform ReturnTransform = GetActorTransformAtAlpha(SafeRotationAlpha * (1.0f - Ease));
		ApplyActorTransform(ReturnTransform, StandingCharacters);
		if (RawReturnAlpha >= 1.0f)
		{
			ApplyActorTransform(StartActorTransform, StandingCharacters);
			bReturning = false;
			bRotating = false;
		}
		return;
	}

	// 计算插值进度；RotationDuration 为 0 时一帧到位。
	RotationElapsed += DeltaTime;
	const float RawAlpha =
		RotationDuration <= 0.0f ? 1.0f : FMath::Clamp(RotationElapsed / RotationDuration, 0.0f, 1.0f);
	// SmoothStep 缓动：两端速度为 0，机关转动的手感更稳，也避免到位瞬间的顿挫。
	const float Alpha = RawAlpha * RawAlpha * (3.0f - 2.0f * RawAlpha);

	if (bConsiderCollision)
	{
		if (!AdvanceWithCollision(Alpha, StandingCharacters))
		{
			BeginReturn();
			return;
		}
	}
	else
	{
		const FTransform NextTransform = GetActorTransformAtAlpha(Alpha);
		ApplyActorTransform(NextTransform, StandingCharacters);
	}

	if (RawAlpha >= 1.0f)
		bRotating = false;
}

void UDreamRotatableComponent::OnInteracted_Implementation(AActor* Interactor)
{
	// 将真实操作者传给统一入口；世界 E、手办点击和蓝图调用执行同一份钥匙判断。
	TriggerRotation(Interactor);
}

void UDreamRotatableComponent::TriggerRotation(AActor* Interactor)
{
	UE_LOG(LogDreamSpace, Verbose, TEXT("可转动组件触发：Actor=%s CachedPivot=%s"),
		*GetNameSafe(GetOwner()), *GetNameSafe(CachedPivot.Get()));
	const ADreamCharacter* Character = IsValid(Interactor) ? Cast<ADreamCharacter>(Interactor) : nullptr;
	const bool bMissingKey = bRequiresKey &&
		(!Character || Character->GetWorld() != GetWorld() || !Character->bHasKey);
	if (bMissingKey)
		ShowNeedsKeyMessage(Interactor);
	if (bRotating)
	{
		// 反复敲击锁定机关可以刷新提示，但不重启晃动、不改快照，也不污染日志为正常转动警告。
		if (bPlayingLockedFeedback)
			return;
		const AActor* Owner = GetOwner();
		UE_LOG(LogDreamSpace, Warning, TEXT("可转动组件：Actor [%s] 正在转动中，本次触发被忽略。"),
			*GetNameSafe(Owner));
		return;
	}

	// 运行期枢轴点可能被蓝图增删，缓存失效时实时补一次解析。
	if (!CachedPivot)
		CachedPivot = ResolvePivot();
	if (!CachedPivot)
	{
		UE_LOG(LogDreamSpace, Warning,
			TEXT("可转动组件：Actor [%s] 触发失败，没有解析到有效枢轴点组件。"),
			*GetNameSafe(GetOwner()));
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
		return;

	// 快照触发瞬间的转轴与姿态；整段插值都基于这组固定数据，见头文件中的注释。
	const FVector AxisDir = GetRotationAxisWorldDir(CachedPivot);
	PivotWorldLocation = CachedPivot->GetComponentLocation();
	FullStepQuat = FQuat(AxisDir, FMath::DegreesToRadians(StepAngleDegrees));
	StartActorTransform = Owner->GetActorTransform();

	if (bMissingKey)
	{
		// 保存本次反馈专用的小角度，后续带符号的进度会让它正反晃动，完全不使用正常步进角。
		// 钥匙在动画中途获得也需等回正后再次交互，避免在偏移姿态上继续执行一次完整开门。
		const float Direction = StepAngleDegrees < 0.0f ? -1.0f : 1.0f;
		FullStepQuat = FQuat(AxisDir, FMath::DegreesToRadians(LockedFeedbackAngleDegrees * Direction));
		LockedFeedbackElapsed = 0.0f;
		bReturning = false;
		bPlayingLockedFeedback = true;
		bRotating = true;
		return;
	}

	if (RotationDuration <= 0.0f)
	{
		TArray<ACharacter*> StandingCharacters;
		GatherStandingCharacters(StandingCharacters);
		// 0 秒仍要检查整段旋转弧线；受阻时没有动画时间，立即精确回到原位。
		SafeRotationAlpha = 0.0f;
		if (bConsiderCollision && !AdvanceWithCollision(1.0f, StandingCharacters))
		{
			ApplyActorTransform(StartActorTransform, StandingCharacters);
			return;
		}
		if (!bConsiderCollision)
		{
			const FTransform TargetTransform = GetActorTransformAtAlpha(1.0f);
			ApplyActorTransform(TargetTransform, StandingCharacters);
		}
		return;
	}

	RotationElapsed = 0.0f;
	SafeRotationAlpha = 0.0f;
	bReturning = false;
	bRotating = true;
}

FTransform UDreamRotatableComponent::GetActorTransformAtAlpha(float Alpha) const
{
	// 使用触发瞬间固定的枢轴、起始姿态与完整步进四元数。
	// 同一个 Alpha 在前进和回弹时得到完全相同的姿态，不会产生累计漂移。
	const FQuat PartialQuat = FQuat::Slerp(FQuat::Identity, FullStepQuat, Alpha);
	return GetActorTransformForRotation(PartialQuat);
}

FTransform UDreamRotatableComponent::GetActorTransformForRotation(const FQuat& Rotation) const
{
	const FVector NewLocation = PivotWorldLocation
		+ Rotation * (StartActorTransform.GetLocation() - PivotWorldLocation);
	return FTransform(Rotation * StartActorTransform.GetRotation(), NewLocation,
		StartActorTransform.GetScale3D());
}

void UDreamRotatableComponent::ShowNeedsKeyMessage(AActor* Interactor) const
{
	// 仅使用本次角色的控制器和 HUD；无操作者或跨世界调用仍拒绝转动，但不能通知无关的玩家。
	const ADreamCharacter* Character = IsValid(Interactor) ? Cast<ADreamCharacter>(Interactor) : nullptr;
	if (!Character || Character->GetWorld() != GetWorld())
		return;
	if (const APlayerController* Controller = Cast<APlayerController>(Character->GetController()))
		if (ADreamHUD* HUD = Cast<ADreamHUD>(Controller->GetHUD()))
			HUD->ShowNeedsKeyMessage();
}

void UDreamRotatableComponent::UpdateLockedFeedback(float DeltaTime, const TArray<ACharacter*>& StandingCharacters)
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !FMath::IsFinite(DeltaTime))
	{
		StopLockedFeedback();
		return;
	}
	const float FromTime = LockedFeedbackElapsed;
	const float TargetTime = FMath::Min(LockedFeedbackDuration, FromTime + FMath::Max(0.0f, DeltaTime));
	TArray<AActor*> MovingActors;
	MovingActors.Add(Owner);
	float Radius = 0.0f;
	if (bConsiderCollision)
		Radius = DreamInteractionCollision::GetMaxCollisionRadius(Owner, PivotWorldLocation);
	for (ACharacter* Character : StandingCharacters)
	{
		MovingActors.Add(Character);
		if (bConsiderCollision)
			Radius = FMath::Max(Radius, DreamInteractionCollision::GetMaxCollisionRadius(Character, PivotWorldLocation));
	}

	// 一帧可能跨越正反两次摆动，因此不能只检查最终姿态。按时间细分完整曲线，
	// 并以最大角速度约束最远碰撞点每步不超过 2 cm，偏心长门和站立角色都不会跨过薄障碍。
	const float MaxAngularSpeed = FMath::DegreesToRadians(LockedFeedbackAngleDegrees) *
		(4.0f * PI + 2.0f) / LockedFeedbackDuration;
	const float MaxTimeStep = FMath::Min(1.0f / 120.0f, 2.0f / FMath::Max(Radius * MaxAngularSpeed, KINDA_SMALL_NUMBER));
	const int32 Steps = bConsiderCollision ? FMath::Max(1, FMath::CeilToInt((TargetTime - FromTime) / MaxTimeStep)) : 1;
	for (int32 Index = 1; Index <= Steps; ++Index)
	{
		const float SampleTime = FMath::Lerp(FromTime, TargetTime, static_cast<float>(Index) / Steps);
		const float Progress = SampleTime / LockedFeedbackDuration;
		// 两次往复、逐渐衰减；正负值都围绕触发时的原姿态计算，避免每帧累加造成长期角度漂移。
		const float Alpha = FMath::Sin(4.0f * PI * Progress) * FMath::Square(1.0f - Progress);
		const FTransform Target = GetActorTransformAtAlpha(Alpha);
		if (bConsiderCollision)
		{
			bool bBlocked = DreamInteractionCollision::FindSafeMoveFraction(Owner, Target, MovingActors) < 1.0f;
			for (const ACharacter* Character : StandingCharacters)
			{
				const FTransform CharacterTarget = GetStandingCharacterTransform(Character, Target);
				bBlocked |= DreamInteractionCollision::FindSafeMoveFraction(Character, CharacterTarget, MovingActors) < 1.0f;
			}
			if (bBlocked)
			{
				StopLockedFeedback();
				return;
			}
		}
		ApplyActorTransform(Target, StandingCharacters);
	}
	LockedFeedbackElapsed = TargetTime;
	if (LockedFeedbackElapsed >= LockedFeedbackDuration)
		StopLockedFeedback();
}

void UDreamRotatableComponent::StopLockedFeedback()
{
	if (!bPlayingLockedFeedback)
		return;
	if (IsValid(GetOwner()))
	{
		// 最后直接写入快照，消除 sin/四元数插值尾端误差；生命周期中断也走相同恢复流程。
		TArray<ACharacter*> StandingCharacters;
		GatherStandingCharacters(StandingCharacters);
		ApplyActorTransform(StartActorTransform, StandingCharacters);
	}
	bPlayingLockedFeedback = false;
	bRotating = false;
	bReturning = false;
	LockedFeedbackElapsed = 0.0f;
}

bool UDreamRotatableComponent::AdvanceWithCollision(float TargetAlpha, const TArray<ACharacter*>& StandingCharacters)
{
	AActor* Owner = GetOwner();
	if (!Owner)
		return false;

	// UE 不扫旋转体积：把本帧需前进的弧线切成小段。除最大 2° 外，
	// 还限制最远碰撞点每段的弧长到 2 cm，门板很长时也不会一步跨过薄障碍。
	const float FullAngle = 2.0f * FMath::Acos(FMath::Clamp(FMath::Abs(FullStepQuat.W), 0.0f, 1.0f));
	float Radius = DreamInteractionCollision::GetMaxCollisionRadius(Owner, PivotWorldLocation);
	TArray<AActor*> MovingActors;
	MovingActors.Add(Owner);
	for (ACharacter* Character : StandingCharacters)
	{
		MovingActors.Add(Character);
		// 角色也会绕枢轴公转，胶囊可能比平台更远；把它纳入半径估计，避免扫过薄障碍。
		Radius = FMath::Max(Radius, DreamInteractionCollision::GetMaxCollisionRadius(Character, PivotWorldLocation));
	}
	const float MaxAlphaStep = FMath::Min(1.0f,
		FMath::Min(FMath::DegreesToRadians(2.0f) / FMath::Max(FullAngle, KINDA_SMALL_NUMBER),
			2.0f / FMath::Max(FullAngle * Radius, KINDA_SMALL_NUMBER)));
	const int32 Steps = FMath::Max(1, FMath::CeilToInt((TargetAlpha - SafeRotationAlpha) / MaxAlphaStep));
	const float FromAlpha = SafeRotationAlpha;

	for (int32 Index = 1; Index <= Steps; ++Index)
	{
		const float NextAlpha = FMath::Lerp(FromAlpha, TargetAlpha, static_cast<float>(Index) / Steps);
		const FTransform NextTransform = GetActorTransformAtAlpha(NextAlpha);
		// 站立角色与平台同步移动，不能把角色仍位于上一子步的胶囊误判成平台前方的障碍。
		// 同时分别检查每个角色的目标姿态，保证忽略乘客不会使乘客穿入墙壁或天花板。
		if (DreamInteractionCollision::FindSafeMoveFraction(Owner, NextTransform, MovingActors) < 1.0f)
			return false;
		for (const ACharacter* Character : StandingCharacters)
		{
			const FTransform CharacterTarget = GetStandingCharacterTransform(Character, NextTransform);
			if (DreamInteractionCollision::FindSafeMoveFraction(Character, CharacterTarget, MovingActors) < 1.0f)
				return false;
		}
		ApplyActorTransform(NextTransform, StandingCharacters);
		SafeRotationAlpha = NextAlpha;
	}
	return true;
}

void UDreamRotatableComponent::GatherStandingCharacters(TArray<ACharacter*>& OutCharacters) const
{
	OutCharacters.Reset();
	if (bRotateStandingCharacterGravity)
		DreamRotationSupport::GatherStandingCharacters(GetOwner(), OutCharacters);
}

FTransform UDreamRotatableComponent::GetStandingCharacterTransform(
	const ACharacter* Character, const FTransform& TargetActorTransform) const
{
	return DreamRotationSupport::GetStandingCharacterTransform(GetOwner(), Character, TargetActorTransform);
}

void UDreamRotatableComponent::ApplyActorTransform(
	const FTransform& TargetActorTransform, const TArray<ACharacter*>& StandingCharacters)
{
	DreamRotationSupport::ApplyActorTransform(GetOwner(), TargetActorTransform, StandingCharacters);
}

void UDreamRotatableComponent::BeginReturn()
{
	// 受阻后整次交互失败；回弹时间按已走过的进度缩放，碰得越早退得越快。
	// 无前进距离时直接结束，避免在原地等待一个空动画。
	if (SafeRotationAlpha <= KINDA_SMALL_NUMBER)
	{
		bRotating = false;
		return;
	}
	ReturnElapsed = 0.0f;
	ReturnDuration = FMath::Max(0.08f, RotationDuration * SafeRotationAlpha);
	bReturning = true;
}

UDreamPivotPointComponent* UDreamRotatableComponent::ResolvePivot() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
		return nullptr;

	// FComponentReference 是编辑器可选的真实组件引用，优先于旧的名称字段。
	if (UActorComponent* ReferencedComponent = PivotComponent.GetComponent(const_cast<AActor*>(Owner)))
		if (UDreamPivotPointComponent* ReferencedPivot = Cast<UDreamPivotPointComponent>(ReferencedComponent))
			return ReferencedPivot;

	// 先收集 Actor 上全部枢轴点组件。
	TArray<UDreamPivotPointComponent*> Pivots;
	Owner->GetComponents<UDreamPivotPointComponent>(Pivots);
	UE_LOG(LogDreamSpace, Verbose, TEXT("可转动组件解析枢轴：Actor=%s Count=%d"),
		*Owner->GetName(), Pivots.Num());
	if (Pivots.IsEmpty())
		return Owner->FindComponentByClass<UDreamPivotPointComponent>();

	// 配置了名称时按名称精确匹配；未配置时使用第一个枢轴点。
	if (!PivotComponentName.IsNone())
		for (UDreamPivotPointComponent* Pivot : Pivots)
			if (Pivot->GetFName() == PivotComponentName)
				return Pivot;
	return Pivots[0];
}

FVector UDreamRotatableComponent::GetRotationAxisWorldDir(const UDreamPivotPointComponent* Pivot) const
{
	// 枢轴点的局部 X/Y/Z 轴在世界中的方向；组件的旋转完全决定了可转动的方向。
	const FQuat PivotRotation = Pivot->GetComponentQuat();
	switch (RotationAxis)
	{
	case EDreamPivotRotationAxis::X:
		return PivotRotation.GetAxisX();
	case EDreamPivotRotationAxis::Y:
		return PivotRotation.GetAxisY();
	case EDreamPivotRotationAxis::Z:
	default:
		return PivotRotation.GetAxisZ();
	}
}

void UDreamRotatableComponent::DrawDebugRotationAxis() const
{
	if (!DreamPuzzleDebug::IsPivotDebugDrawEnabled())
		return;

	// 枢轴点未配置时画不出转轴，问题已通过 BeginPlay 的警告日志暴露。
	// CachedPivot 是 TObjectPtr，与裸指针混用三目运算会产生 C2445 类型歧义，需显式 .Get()。
	const UDreamPivotPointComponent* Pivot = CachedPivot ? CachedPivot.Get() : ResolvePivot();
	if (!Pivot)
		return;

	// 品红色双向箭头表示当前配置的转轴，与枢轴点自身的 RGB 三轴区分开。
	const FVector AxisDir = GetRotationAxisWorldDir(Pivot);
	const FVector Center = Pivot->GetComponentLocation();
	const float HalfLength = DebugAxisDrawLength * 0.5f;
	DrawDebugDirectionalArrow(GetWorld(), Center - AxisDir * HalfLength, Center + AxisDir * HalfLength, 25.0f,
		FColor::Magenta, false, -1.0f, 0, 3.0f);
}
