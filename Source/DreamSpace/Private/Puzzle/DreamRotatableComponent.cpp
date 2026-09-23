#include "DreamRotatableComponent.h"
#include "DreamPivotPointComponent.h"
#include "DreamPuzzleDebug.h"
#include "DreamSpace.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Actor.h"

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
		return;
	}

	// 计算插值进度；RotationDuration 为 0 时一帧到位。
	RotationElapsed += DeltaTime;
	const float RawAlpha =
		RotationDuration <= 0.0f ? 1.0f : FMath::Clamp(RotationElapsed / RotationDuration, 0.0f, 1.0f);
	// SmoothStep 缓动：两端速度为 0，机关转动的手感更稳，也避免到位瞬间的顿挫。
	const float Alpha = RawAlpha * RawAlpha * (3.0f - 2.0f * RawAlpha);

	// 在“恒等 -> 完整步进”之间球面插值出本帧的增量旋转。
	const FQuat PartialQuat = FQuat::Slerp(FQuat::Identity, FullStepQuat, Alpha);

	// 姿态 = 绕枢轴点公转（位置绕轴旋转）+ 自转（朝向乘上同一个增量旋转）。
	const FVector StartLocation = StartActorTransform.GetLocation();
	const FVector NewLocation = PivotWorldLocation + PartialQuat * (StartLocation - PivotWorldLocation);
	const FQuat NewRotation = PartialQuat * StartActorTransform.GetRotation();
	Owner->SetActorLocationAndRotation(NewLocation, NewRotation, false, nullptr, ETeleportType::None);

	if (RawAlpha >= 1.0f)
		bRotating = false;
}

void UDreamRotatableComponent::OnInteracted_Implementation(AActor* Interactor)
{
	// 交互入口只转发到 TriggerRotation，蓝图或其他系统也可以直接调用后者。
	TriggerRotation();
}

void UDreamRotatableComponent::TriggerRotation()
{
	UE_LOG(LogDreamSpace, Verbose, TEXT("可转动组件触发：Actor=%s CachedPivot=%s"),
		*GetNameSafe(GetOwner()), *GetNameSafe(CachedPivot.Get()));
	if (bRotating)
	{
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

	if (RotationDuration <= 0.0f)
	{
		// 时长为 0：瞬间到位，不进入 Tick 插值流程。
		const FVector StartLocation = StartActorTransform.GetLocation();
		Owner->SetActorLocationAndRotation(PivotWorldLocation + FullStepQuat * (StartLocation - PivotWorldLocation),
			FullStepQuat * StartActorTransform.GetRotation(), false, nullptr, ETeleportType::None);
		return;
	}

	RotationElapsed = 0.0f;
	bRotating = true;
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
