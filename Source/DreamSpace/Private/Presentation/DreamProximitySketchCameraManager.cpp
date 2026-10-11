#include "DreamProximitySketchCameraManager.h"

#include "Components/DirectionalLightComponent.h"
#include "DreamSpace.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
// -1 表示采用相机 / DefaultGame.ini 的设置。控制台覆盖仅用于快速视觉调参，
// 既不保存关卡，也不改变抗锯齿、材质或碰撞。位置始终由各自 Pawn 独立提供。
TAutoConsoleVariable<int32> CVarSketchEnabled(TEXT("dream.Sketch.Enabled"), -1,
	TEXT("距离线稿开关：-1 使用配置，0 关闭，1 开启。"));
TAutoConsoleVariable<float> CVarSketchRadius(TEXT("dream.Sketch.Radius"), -1.0f,
	TEXT("正常渲染球形半径（厘米）；-1 使用相机配置。"));
TAutoConsoleVariable<float> CVarSketchTransition(TEXT("dream.Sketch.Transition"), -1.0f,
	TEXT("线稿过渡宽度（厘米）；-1 使用相机配置，0 近似硬切。"));
TAutoConsoleVariable<int32> CVarSketchDebug(TEXT("dream.Sketch.Debug"), -1,
	TEXT("-1 使用相机设置；0 正式效果，1 明暗，2 距离遮罩，3 原始场景。"));
}

ADreamProximitySketchCameraManager::ADreamProximitySketchCameraManager()
{
	// 调试时临时切到 CameraActor 也继续围绕被控制的 Pawn，不能把范围中心
	// 偷换成镜头位置。菜单并不使用这个相机管理器，所以没有 Pawn 时会跳过。
	bAlwaysApplyModifiers = true;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Game/DreamPresentation/ProximitySketch/Materials/MI_ProximitySketch.MI_ProximitySketch"));
	if (Material.Succeeded())
		SketchMaterial = Material.Object;
	else
		UE_LOG(LogDreamSpace, Error, TEXT("距离线稿材质缺失，请先运行 generate_proximity_sketch.py。"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Smoothing(
		TEXT("/Game/DreamPresentation/ProximitySketch/Materials/MI_ProximitySketchAA.MI_ProximitySketchAA"));
	if (Smoothing.Succeeded())
		SmoothingMaterial = Smoothing.Object;
	else
		UE_LOG(LogDreamSpace, Error, TEXT("距离线稿平滑材质缺失，请先运行 generate_proximity_sketch.py。"));
}

float ADreamProximitySketchCameraManager::GetEffectiveNormalRadius() const
{
	// 恢复请求一旦被接受，动画半径就成为本玩家的运行时覆盖值。控制台仅提供恢复前的起点，
	// 不清空或改写共享 CVar，防止另一个本地玩家、下一次 PIE 或美术调试配置被连带修改。
	if (bWorldRecoveryActive || bWorldRecoveryFinished)
		return WorldRecoveryCurrentRadius;
	const float Override = CVarSketchRadius.GetValueOnGameThread();
	const float Radius = Override < 0.0f ? NormalRadius : Override;
	return FMath::IsFinite(Radius) ? FMath::Max(0.0f, Radius) : 0.0f;
}

bool ADreamProximitySketchCameraManager::StartWorldRecovery(
	float TargetRadius, float Duration, bool bRestoreEntireWorld)
{
	// 组件的交互参数最终来自 Pawn，蓝图也可直接调用本入口，所以相机端再次检查本地操控关系。
	// 没有有效 Pawn 时不消耗恢复机会；换角色后仍可继续使用同一个相机的恢复进度。
	const int32 EnabledOverride = CVarSketchEnabled.GetValueOnGameThread();
	const bool bEnabled = EnabledOverride < 0 ? bEnableProximitySketch : EnabledOverride != 0;
	if (!IsValid(PCOwner) || !PCOwner->IsLocalController() || !IsValid(PCOwner->GetPawn())
		|| GetNetMode() == NM_DedicatedServer || !bEnabled || !SketchMaterial
		|| !FMath::IsFinite(EffectStrength) || EffectStrength <= 0.0f
		|| !FMath::IsFinite(TargetRadius) || TargetRadius < 0.0f || !FMath::IsFinite(Duration)
		|| (bWorldRecoveryFinished && bRestoreEntireWorldRequested))
		return false;

	const float CurrentRadius = GetEffectiveNormalRadius();
	const float ExistingTarget = bWorldRecoveryActive ? WorldRecoveryTargetRadius : CurrentRadius;
	const bool bExpandsFurther = TargetRadius > ExistingTarget;
	const bool bPromotesToEntireWorld = bRestoreEntireWorld && !bRestoreEntireWorldRequested;
	// 同目标重复交互既不回缩，也不重置计时；否则按住 / 连点交互键会让恢复永远无法结束。
	// 从阶段扩张升级为全世界恢复时，即使半径没有增加，也允许执行最后的线稿淡出。
	if (!bExpandsFurther && !bPromotesToEntireWorld)
		return false;

	WorldRecoveryStartRadius = CurrentRadius;
	WorldRecoveryCurrentRadius = CurrentRadius;
	WorldRecoveryTargetRadius = FMath::Max(TargetRadius, ExistingTarget);
	WorldRecoveryStartStrength = WorldRecoveryCurrentStrength;
	WorldRecoveryDuration = FMath::Max(0.0f, Duration);
	WorldRecoveryElapsed = 0.0f;
	// 全世界恢复一旦开始，后来的阶段扩张不能把它降级回线稿，避免多个机关互相撤销结果。
	bRestoreEntireWorldRequested |= bRestoreEntireWorld;
	bWorldRecoveryFinished = false;
	bWorldRecoveryActive = true;
	if (WorldRecoveryDuration <= 0.0f)
		CompleteWorldRecovery();
	return true;
}

void ADreamProximitySketchCameraManager::UpdateCamera(float DeltaTime)
{
	// 引擎混合两个 ViewTarget 时会各自执行 ApplyCameraModifiers，必须在更外层推进一次。
	// DeltaTime 来自游戏相机更新，随游戏时间缩放；暂停或临时取消占有时保持当前进度，
	// 重新占有后从同一半径继续，因此不会在没有玩家视图的间隙悄悄完成恢复。
	if (IsValid(PCOwner) && PCOwner->IsLocalController() && IsValid(PCOwner->GetPawn())
		&& GetWorld() && !GetWorld()->IsPaused())
		UpdateWorldRecovery(DeltaTime);
	Super::UpdateCamera(DeltaTime);
}

void ADreamProximitySketchCameraManager::UpdateWorldRecovery(float DeltaTime)
{
	if (!bWorldRecoveryActive || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f)
		return;
	WorldRecoveryElapsed = FMath::Min(WorldRecoveryDuration, WorldRecoveryElapsed + DeltaTime);
	const float Alpha = WorldRecoveryElapsed / WorldRecoveryDuration;
	// 固定时长的 SmoothStep 在起点和终点速度为零；按累计游戏时间求值，帧率不会改变动画时长。
	const float Ease = Alpha * Alpha * (3.0f - 2.0f * Alpha);
	WorldRecoveryCurrentRadius = FMath::Lerp(WorldRecoveryStartRadius, WorldRecoveryTargetRadius, Ease);
	if (bRestoreEntireWorldRequested)
	{
		// 距离遮罩会让天空 / 无有效深度的像素一直处于线稿区，有限半径也不能覆盖无限远处。
		// 先用前 75% 时段呈现范围扩张，再在最后 25% 平滑移除剩余线稿，避免结束时天空突然跳色。
		const float RecoveryFadeAlpha = FMath::Clamp((Alpha - 0.75f) / 0.25f, 0.0f, 1.0f);
		const float FadeEase = RecoveryFadeAlpha * RecoveryFadeAlpha * (3.0f - 2.0f * RecoveryFadeAlpha);
		WorldRecoveryCurrentStrength = FMath::Lerp(WorldRecoveryStartStrength, 0.0f, FadeEase);
	}
	if (Alpha >= 1.0f)
		CompleteWorldRecovery();
}

void ADreamProximitySketchCameraManager::CompleteWorldRecovery()
{
	WorldRecoveryCurrentRadius = WorldRecoveryTargetRadius;
	WorldRecoveryCurrentStrength = bRestoreEntireWorldRequested ? 0.0f : 1.0f;
	bWorldRecoveryActive = false;
	bWorldRecoveryFinished = true;
}

void ADreamProximitySketchCameraManager::ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	// 父类每帧清理后处理缓存并执行已有镜头修饰器；在它之后添加本效果，
	// 避免被清空，也不会绕过镜头震动、既有景深或其它相机功能。
	Super::ApplyCameraModifiers(DeltaTime, InOutPOV);
	const APawn* Pawn = PCOwner ? PCOwner->GetPawn() : nullptr;
	const int32 EnabledOverride = CVarSketchEnabled.GetValueOnGameThread();
	const bool bEnabled = EnabledOverride < 0 ? bEnableProximitySketch : EnabledOverride != 0;
	const float EffectiveStrength = FMath::Clamp(EffectStrength, 0.0f, 1.0f) * WorldRecoveryCurrentStrength;
	if (!bEnabled || !FMath::IsFinite(EffectiveStrength) || EffectiveStrength <= 0.0f
		|| !IsValid(Pawn) || !PCOwner->IsLocalController()
		|| !SketchMaterial || GetNetMode() == NM_DedicatedServer)
	{
		// 未占有 Pawn、角色被销毁或被取消控制时不向本帧缓存提交任何材质。
		// 保留实例供再次占有时复用，但清零它，防止调试读取到上个角色的有效权重。
		if (SketchInstance)
			SketchInstance->SetScalarParameterValue(TEXT("EffectStrength"), 0.0f);
		if (SmoothingInstance)
			SmoothingInstance->SetScalarParameterValue(TEXT("EffectStrength"), 0.0f);
		return;
	}
	if (!SketchInstance)
	{
		SketchInstance = UMaterialInstanceDynamic::Create(SketchMaterial, this);
		SketchSettings.AddBlendable(SketchInstance, 1.0f);
		if (SmoothingMaterial)
		{
			SmoothingInstance = UMaterialInstanceDynamic::Create(SmoothingMaterial, this);
			SketchSettings.AddBlendable(SmoothingInstance, 1.0f);
		}
	}

	const float WidthOverride = CVarSketchTransition.GetValueOnGameThread();
	const int32 DebugOverride = CVarSketchDebug.GetValueOnGameThread();
	const FVector Center = Pawn->GetActorLocation();
	SketchInstance->SetVectorParameterValue(TEXT("PlayerPosition"), FLinearColor(Center.X, Center.Y, Center.Z, 0.0f));
	SketchInstance->SetScalarParameterValue(TEXT("NormalRadius"), GetEffectiveNormalRadius());
	SketchInstance->SetScalarParameterValue(TEXT("TransitionWidth"), FMath::Max(0.0f, WidthOverride < 0.0f ? TransitionWidth : WidthOverride));
	SketchInstance->SetScalarParameterValue(TEXT("EffectStrength"), EffectiveStrength);
	SketchInstance->SetScalarParameterValue(TEXT("SketchDebugView"), FMath::Clamp(DebugOverride < 0 ? SketchDebugView : DebugOverride, 0, 3));
	if (SmoothingInstance)
	{
		// 从实际主 MID 读回，包含材质实例中的覆盖值。两阶段严格使用同一
		// 中心、半径和有效深度，避免局部平滑在正常内圈产生一圈模糊边。
		SmoothingInstance->SetVectorParameterValue(TEXT("PlayerPosition"), FLinearColor(Center.X, Center.Y, Center.Z, 0.0f));
		for (const FName Name : { FName(TEXT("NormalRadius")), FName(TEXT("TransitionWidth")),
			FName(TEXT("EffectStrength")), FName(TEXT("SketchDebugView")), FName(TEXT("MaxSubjectDepth")) })
			SmoothingInstance->SetScalarParameterValue(Name, SketchInstance->K2_GetScalarParameterValue(Name));
	}
	UpdateLightDirection();
	// 相机缓存仅应用于此本地玩家的最终视图，SceneCapture 不会遍历它。
	// 使用 Override 顺序，让蓝图相机的 PostProcessBlendWeight=0 也不意外关闭效果。
	AddCachedPPBlend(SketchSettings, 1.0f, VTBlendOrder_Override);
}

void ADreamProximitySketchCameraManager::UpdateLightDirection()
{
	if (!KeyLight.IsValid())
	{
		float Brightest = -1.0f;
		for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
		{
			const UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(It->GetLightComponent());
			if (Light && Light->IsVisible() && Light->Intensity > Brightest)
			{
				Brightest = Light->Intensity;
				KeyLight = *It;
			}
		}
	}
	if (KeyLight.IsValid())
	{
		// 方向光的 ForwardVector 指向光的传播方向，排线需要表面指向光源的反方向。
		const FVector Direction = -KeyLight->GetLightComponent()->GetForwardVector();
		SketchInstance->SetVectorParameterValue(TEXT("LightDirection"), FLinearColor(Direction.X, Direction.Y, Direction.Z, 0.0f));
	}
}

void ADreamProximitySketchCameraManager::EndPlay(const EEndPlayReason::Type Reason)
{
	// 场景切换 / PIE 结束只释放瞬时引用，不保存玩家参数，不修改任何共享 MI。
	SketchSettings.WeightedBlendables.Array.Reset();
	SketchInstance = nullptr;
	SmoothingInstance = nullptr;
	KeyLight.Reset();
	Super::EndPlay(Reason);
}
