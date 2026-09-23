#include "DreamTranslatableComponent.h"

#include "DreamPivotPointComponent.h"
#include "DreamPuzzleDebug.h"
#include "DreamSpace.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Actor.h"

UDreamTranslatableComponent::UDreamTranslatableComponent()
{
	// Tick 同时负责平滑插值和实时调试绘制，因此组件需要具备逐帧更新能力。
	PrimaryComponentTick.bCanEverTick = true;

	// 允许从蓝图继承出的组件在关卡实例详情面板中继续编辑配置。
	// 配合各配置项上的 EditAnywhere，可直接为每一个机关实例设置不同范围。
	bEditableWhenInherited = true;
}

void UDreamTranslatableComponent::BeginPlay()
{
	Super::BeginPlay();

	// 开局就解析并检查枢轴，能让错误配置尽早通过日志暴露；
	// 运行时若组件被动态替换，TriggerTranslation 和调试绘制仍会尝试重新解析。
	CachedPivot = ResolvePivot();
	if (!CachedPivot)
	{
		UE_LOG(LogDreamSpace, Warning,
			TEXT("可平移组件：Actor [%s] 上找不到枢轴点组件（备用名称：%s），该组件不会响应交互。"),
			*GetNameSafe(GetOwner()), *PivotComponentName.ToString());
		return;
	}

	InitializeRange();
	UE_LOG(LogDreamSpace, Verbose,
		TEXT("可平移组件初始化：Actor=%s Pivot=%s Range=[-%.2f, %.2f] Step=%.2f Duration=%.3f"),
		*GetNameSafe(GetOwner()), *GetNameSafe(CachedPivot.Get()), NegativeLimit, PositiveLimit,
		StepDistance, TranslationDuration);
}

void UDreamTranslatableComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 调试绘制每帧重新读取枢轴朝向及实例范围，所以在运行时修改参数也能立刻看到结果。
	DrawDebugTranslationRange();

	if (!bTranslating)
		return;

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		bTranslating = false;
		return;
	}

	// 将已用时间换算成 0~1 进度；时长为 0 的情况在触发函数中已经直接完成，
	// 这里仍保留保护分支，避免运行时把时长改为 0 后发生除零。
	TranslationElapsed += DeltaTime;
	const float RawAlpha = TranslationDuration <= 0.0f
		? 1.0f
		: FMath::Clamp(TranslationElapsed / TranslationDuration, 0.0f, 1.0f);

	// SmoothStep 在起点和终点的速度都为 0，比线性插值更适合抽屉、滑块等机关。
	const float SmoothedAlpha = RawAlpha * RawAlpha * (3.0f - 2.0f * RawAlpha);
	const FVector NewLocation = FMath::Lerp(StartActorLocation, TargetActorLocation, SmoothedAlpha);
	CurrentTranslation = FMath::Lerp(StartTranslation, TargetTranslation, SmoothedAlpha);

	// 只写入位置，不覆盖 Actor 在动画期间可能由其他系统维护的旋转和缩放。
	Owner->SetActorLocation(NewLocation, false, nullptr, ETeleportType::None);

	if (RawAlpha >= 1.0f)
		CompleteTranslation();
}

void UDreamTranslatableComponent::OnInteracted_Implementation(AActor* Interactor)
{
	// 交互来源不影响本组件的运动规则；Interactor 参数保留给接口统一签名和后续扩展使用。
	TriggerTranslation();
}

void UDreamTranslatableComponent::TriggerTranslation()
{
	if (bTranslating)
	{
		UE_LOG(LogDreamSpace, Warning,
			TEXT("可平移组件：Actor [%s] 正在平移中，本次触发被忽略。"), *GetNameSafe(GetOwner()));
		return;
	}

	// 枢轴可能由蓝图在运行时销毁或替换；缓存无效时补做一次查找。
	if (!CachedPivot)
		CachedPivot = ResolvePivot();
	if (!CachedPivot)
	{
		UE_LOG(LogDreamSpace, Warning,
			TEXT("可平移组件：Actor [%s] 触发失败，没有解析到有效枢轴点组件。"),
			*GetNameSafe(GetOwner()));
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
		return;

	if (!bRangeInitialized)
		InitializeRange();
	if (!bRangeInitialized)
		return;

	// ClampMin 只约束编辑器输入，蓝图或 C++ 运行时仍可能写入负数，因此逻辑层再次做保护。
	if (StepDistance <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogDreamSpace, Warning,
			TEXT("可平移组件：Actor [%s] 的每次交互步长为 0，本次不会产生位移。"),
			*GetNameSafe(Owner));
		return;
	}

	// 触发瞬间快照枢轴方向。整段插值沿同一条直线，枢轴在动画中被旋转也不会让路径弯曲。
	ActiveAxisWorld = GetTranslationAxisWorldDir(CachedPivot);
	if (ActiveAxisWorld.IsNearlyZero())
	{
		UE_LOG(LogDreamSpace, Warning,
			TEXT("可平移组件：Actor [%s] 的枢轴轴方向无效，本次触发被忽略。"),
			*GetNameSafe(Owner));
		return;
	}

	TargetTranslation = CalculateNextTranslation();
	const float TranslationDelta = TargetTranslation - CurrentTranslation;
	if (FMath::IsNearlyZero(TranslationDelta))
	{
		UE_LOG(LogDreamSpace, Verbose,
			TEXT("可平移组件：Actor [%s] 已位于当前方向的范围端点，本次没有可用位移。"),
			*GetNameSafe(Owner));
		return;
	}

	StartActorLocation = Owner->GetActorLocation();
	StartTranslation = CurrentTranslation;
	TargetActorLocation = StartActorLocation + ActiveAxisWorld * TranslationDelta;

	UE_LOG(LogDreamSpace, Verbose,
		TEXT("可平移组件触发：Actor=%s Current=%.2f Target=%.2f Direction=%+.0f"),
		*GetNameSafe(Owner), CurrentTranslation, TargetTranslation, TranslationDirection);

	if (TranslationDuration <= 0.0f)
	{
		// 0 秒模式不进入 Tick，统一由收束函数写入目标位置并更新逻辑状态。
		CompleteTranslation();
		return;
	}

	TranslationElapsed = 0.0f;
	bTranslating = true;
}

UDreamPivotPointComponent* UDreamTranslatableComponent::ResolvePivot() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
		return nullptr;

	// FComponentReference 会保存编辑器组件选择器中的真实引用，优先级最高。
	if (UActorComponent* ReferencedComponent = PivotComponent.GetComponent(const_cast<AActor*>(Owner)))
		if (UDreamPivotPointComponent* ReferencedPivot = Cast<UDreamPivotPointComponent>(ReferencedComponent))
			return ReferencedPivot;

	TArray<UDreamPivotPointComponent*> Pivots;
	Owner->GetComponents<UDreamPivotPointComponent>(Pivots);
	if (Pivots.IsEmpty())
		return nullptr;

	// 显式填写备用名称时精确匹配；找不到该名称时不误选其他枢轴，便于尽快暴露拼写错误。
	if (!PivotComponentName.IsNone())
	{
		for (UDreamPivotPointComponent* Pivot : Pivots)
			if (Pivot && Pivot->GetFName() == PivotComponentName)
				return Pivot;
		return nullptr;
	}

	// 未作显式选择时使用第一个枢轴点，让只有一个枢轴的普通 Actor 可以零配置工作。
	return Pivots[0];
}

FVector UDreamTranslatableComponent::GetTranslationAxisWorldDir(const UDreamPivotPointComponent* Pivot) const
{
	if (!Pivot)
		return FVector::ZeroVector;

	// 枢轴组件的四元数把局部坐标轴变换到世界空间；GetAxisX/Y/Z 返回单位向量。
	const FQuat PivotRotation = Pivot->GetComponentQuat();
	switch (TranslationAxis)
	{
	case EDreamPivotRotationAxis::X:
		return PivotRotation.GetAxisX().GetSafeNormal();
	case EDreamPivotRotationAxis::Y:
		return PivotRotation.GetAxisY().GetSafeNormal();
	case EDreamPivotRotationAxis::Z:
	default:
		return PivotRotation.GetAxisZ().GetSafeNormal();
	}
}

void UDreamTranslatableComponent::InitializeRange()
{
	AActor* Owner = GetOwner();
	if (!Owner || !CachedPivot)
		return;

	const FVector AxisWorld = GetTranslationAxisWorldDir(CachedPivot);
	if (AxisWorld.IsNearlyZero())
		return;

	const FVector OwnerLocation = Owner->GetActorLocation();
	if (bUsePivotAsRangeOrigin)
	{
		// 使用枢轴零点时，Actor 可以与枢轴存在垂直偏移；这里仅记录它在所选轴上的投影坐标。
		// 实际移动通过“目标坐标 - 当前坐标”的增量完成，不会吸附或丢失垂直偏移。
		RangeOriginWorld = CachedPivot->GetComponentLocation();
		CurrentTranslation = FVector::DotProduct(OwnerLocation - RangeOriginWorld, AxisWorld);
	}
	else
	{
		// 默认语义：Actor 开始游戏的位置就是 0，正负范围分别向轴两侧展开。
		RangeOriginWorld = OwnerLocation;
		CurrentTranslation = 0.0f;
	}

	// 如果关卡初始摆放已经超出范围，不在 BeginPlay 瞬移 Actor，也不伪造它的逻辑坐标；
	// 保留真实坐标并报警，CalculateNextTranslation 会让第一次交互直接回到最近端点。
	const float SafeNegativeLimit = FMath::Max(0.0f, NegativeLimit);
	const float SafePositiveLimit = FMath::Max(0.0f, PositiveLimit);
	const float ClampedTranslation = FMath::Clamp(CurrentTranslation, -SafeNegativeLimit, SafePositiveLimit);
	if (!FMath::IsNearlyEqual(CurrentTranslation, ClampedTranslation))
	{
		UE_LOG(LogDreamSpace, Warning,
			TEXT("可平移组件：Actor [%s] 的初始轴坐标 %.2f 超出范围 [-%.2f, %.2f]，首次交互会回到最近端点。"),
			*GetNameSafe(Owner), CurrentTranslation, SafeNegativeLimit, SafePositiveLimit);
	}

	StartTranslation = CurrentTranslation;
	TargetTranslation = CurrentTranslation;
	TranslationDirection = 1.0f;
	bRangeInitialized = true;
}

float UDreamTranslatableComponent::CalculateNextTranslation()
{
	const float SafeNegativeLimit = FMath::Max(0.0f, NegativeLimit);
	const float SafePositiveLimit = FMath::Max(0.0f, PositiveLimit);
	const float MinTranslation = -SafeNegativeLimit;
	const float MaxTranslation = SafePositiveLimit;
	const float SafeStep = FMath::Max(0.0f, StepDistance);

	// 初始摆放越界，或运行时把范围缩小到 Actor 内侧时，直接把“最近端点”作为下一目标。
	// 这里不能先篡改 CurrentTranslation，否则世界位移增量会少算越界的那一段距离。
	if (CurrentTranslation > MaxTranslation)
	{
		TranslationDirection = -1.0f;
		return MaxTranslation;
	}
	if (CurrentTranslation < MinTranslation)
	{
		TranslationDirection = 1.0f;
		return MinTranslation;
	}

	if (bReverseAtLimits)
	{
		// 只有“正准备继续冲出端点”时才翻转方向；这样到达端点的本次动画仍保持原方向，
		// 下一次交互才自然地从端点向另一侧返回。
		if (TranslationDirection > 0.0f && CurrentTranslation >= MaxTranslation - KINDA_SMALL_NUMBER)
			TranslationDirection = -1.0f;
		else if (TranslationDirection < 0.0f && CurrentTranslation <= MinTranslation + KINDA_SMALL_NUMBER)
			TranslationDirection = 1.0f;
	}

	return FMath::Clamp(CurrentTranslation + TranslationDirection * SafeStep, MinTranslation, MaxTranslation);
}

void UDreamTranslatableComponent::CompleteTranslation()
{
	AActor* Owner = GetOwner();
	if (Owner)
		Owner->SetActorLocation(TargetActorLocation, false, nullptr, ETeleportType::None);

	CurrentTranslation = TargetTranslation;
	TranslationElapsed = 0.0f;
	bTranslating = false;
}

void UDreamTranslatableComponent::DrawDebugTranslationRange() const
{
	if (!DreamPuzzleDebug::IsPivotDebugDrawEnabled())
		return;

	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner)
		return;

	// 调试模式下允许在 BeginPlay 前或缓存失效后直接解析，保证视图尽可能反映当前配置。
	const UDreamPivotPointComponent* Pivot = CachedPivot ? CachedPivot.Get() : ResolvePivot();
	if (!Pivot)
		return;

	const FVector AxisWorld = GetTranslationAxisWorldDir(Pivot);
	if (AxisWorld.IsNearlyZero())
		return;

	// BeginPlay 后使用固定零点，确保画出的就是 Actor 实际受约束的范围；
	// 极早期尚未初始化时则使用当前 Actor/枢轴位置作为预览零点。
	const FVector DebugOrigin = bRangeInitialized
		? RangeOriginWorld
		: (bUsePivotAsRangeOrigin ? Pivot->GetComponentLocation() : Owner->GetActorLocation());
	const float SafeNegativeLimit = FMath::Max(0.0f, NegativeLimit);
	const float SafePositiveLimit = FMath::Max(0.0f, PositiveLimit);
	const FVector NegativeEnd = DebugOrigin - AxisWorld * SafeNegativeLimit;
	const FVector PositiveEnd = DebugOrigin + AxisWorld * SafePositiveLimit;

	// 红色表示负轴范围，绿色表示正轴范围；两条线从同一个黄色零点向两侧展开。
	DrawDebugDirectionalArrow(World, DebugOrigin, NegativeEnd, 14.0f, FColor::Red,
		false, -1.0f, 0, DebugRangeThickness);
	DrawDebugDirectionalArrow(World, DebugOrigin, PositiveEnd, 14.0f, FColor::Green,
		false, -1.0f, 0, DebugRangeThickness);

	// 两侧端点使用与范围线相同的颜色，零点使用黄色，便于快速确认不对称范围。
	DrawDebugSphere(World, NegativeEnd, DebugEndpointRadius, 12, FColor::Red,
		false, -1.0f, 0, DebugRangeThickness);
	DrawDebugSphere(World, PositiveEnd, DebugEndpointRadius, 12, FColor::Green,
		false, -1.0f, 0, DebugRangeThickness);
	DrawDebugSphere(World, DebugOrigin, DebugEndpointRadius * 0.75f, 10, FColor::Yellow,
		false, -1.0f, 0, DebugRangeThickness);

	// 青色球表示 Actor 当前世界位置。若 Actor 与轴线存在垂直偏移，额外画一条细线帮助理解投影关系。
	const FVector OwnerLocation = Owner->GetActorLocation();
	DrawDebugSphere(World, OwnerLocation, DebugEndpointRadius * 0.75f, 10, FColor::Cyan,
		false, -1.0f, 0, DebugRangeThickness);
	const FVector ProjectedLocation = DebugOrigin
		+ AxisWorld * FVector::DotProduct(OwnerLocation - DebugOrigin, AxisWorld);
	if (!OwnerLocation.Equals(ProjectedLocation, 0.1f))
		DrawDebugLine(World, OwnerLocation, ProjectedLocation, FColor::Cyan,
			false, -1.0f, 0, FMath::Max(1.0f, DebugRangeThickness * 0.5f));
}
