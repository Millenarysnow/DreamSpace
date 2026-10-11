#include "DreamMainMenuScene.h"
#include "DreamMenuTransitionSubsystem.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"

FDreamMainMenuLayout FDreamMainMenuLayout::ForAspectRatio(float AspectRatio, bool bLogoOnRight)
{
	if (AspectRatio < 1.2f)
	{
		// 竖屏或很窄的编辑器窗口采用上下布局，保留文字与建筑之间的空白。
		return { FVector2D(0.08, 0.30), FVector2D(0.92, 0.94),
			FVector2D(0.50, 0.15), FVector2D(0.44, 0.18) };
	}
	if (bLogoOnRight)
	{
		return { FVector2D(0.08, 0.10), FVector2D(0.57, 0.91),
			FVector2D(0.785, 0.49), FVector2D(0.22, 0.25) };
	}
	return { FVector2D(0.43, 0.10), FVector2D(0.92, 0.91),
		FVector2D(0.215, 0.49), FVector2D(0.22, 0.25) };
}

ADreamMainMenuScene::ADreamMainMenuScene()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("MenuRoot"));
	BuildingPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BuildingPivot"));
	BuildingPivot->SetupAttachment(RootComponent);
	MenuCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("MenuCamera"));
	MenuCamera->SetupAttachment(RootComponent);
	MenuCamera->bConstrainAspectRatio = false;
	// 固定使用水平 FOV，让我们按窗口比例计算出的纵向空间与引擎实际投影一致。
	MenuCamera->bOverrideAspectRatioAxisConstraint = true;
	MenuCamera->SetAspectRatioAxisConstraint(AspectRatio_MaintainXFOV);
	Tags.Add(TEXT("DreamMainMenuScene"));
}

FMinimalViewInfo ADreamMainMenuScene::ComputeCameraView(const FVector& Center, const FVector& HalfExtent,
	float AspectRatio, bool bLogoOnRight)
{
	FMinimalViewInfo View;
	View.FOV = 40.0f;
	View.AspectRatio = FMath::Max(AspectRatio, 0.1f);
	View.bConstrainAspectRatio = false;
	View.AspectRatioAxisConstraint = AspectRatio_MaintainXFOV;
	View.Rotation = FRotator(-18.0f, 55.0f, 0.0f);
	const FRotationMatrix Basis(View.Rotation);
	const FVector Forward = Basis.GetUnitAxis(EAxis::X);
	const FVector Right = Basis.GetUnitAxis(EAxis::Y);
	const FVector Up = Basis.GetUnitAxis(EAxis::Z);
	const double TanX = FMath::Tan(FMath::DegreesToRadians(View.FOV * 0.5f));
	const double TanY = TanX / View.AspectRatio;
	const FDreamMainMenuLayout Layout = FDreamMainMenuLayout::ForAspectRatio(View.AspectRatio, bLogoOnRight);
	const FVector2D ScreenCenter = (Layout.BuildingMin + Layout.BuildingMax) * 0.5;
	const double CenterX = ScreenCenter.X * 2.0 - 1.0;
	const double CenterY = 1.0 - ScreenCenter.Y * 2.0;
	const double MinX = Layout.BuildingMin.X * 2.0 - 1.0;
	const double MaxX = Layout.BuildingMax.X * 2.0 - 1.0;
	const double MinY = 1.0 - Layout.BuildingMax.Y * 2.0;
	const double MaxY = 1.0 - Layout.BuildingMin.Y * 2.0;

	// 建筑绕 Z 轴转动后，任意顶点 XY 距离不超过原包围盒对角线半径。
	// 用包住这根圆柱的盒子求距离，覆盖 360 度，而不是只对开局角度做取景。
	const double Radius = FMath::Max(FVector2D(HalfExtent.X, HalfExtent.Y).Length(), 1.0);
	const double Height = FMath::Max(FMath::Abs(HalfExtent.Z), 1.0);
	double Distance = 1.0;
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector Offset((Corner & 1) ? Radius : -Radius,
			(Corner & 2) ? Radius : -Radius, (Corner & 4) ? Height : -Height);
		const double Depth = FVector::DotProduct(Offset, Forward);
		const double X = FVector::DotProduct(Offset, Right) / TanX;
		const double Y = FVector::DotProduct(Offset, Up) / TanY;
		// 同时考虑角点深度和偏心构图：相机平移后只看 FOV 会漏算靠近镜头的角点。
		Distance = FMath::Max(Distance, (X - MaxX * Depth) / (MaxX - CenterX));
		Distance = FMath::Max(Distance, (MinX * Depth - X) / (CenterX - MinX));
		Distance = FMath::Max(Distance, (Y - MaxY * Depth) / (MaxY - CenterY));
		Distance = FMath::Max(Distance, (MinY * Depth - Y) / (CenterY - MinY));
		Distance = FMath::Max(Distance, -Depth + 10.0);
	}
	Distance *= 1.04;
	// 平移镜头而不改变朝向，把建筑中心投影到预留区域中央；左右面不会因歪斜镜头失衡。
	View.Location = Center - Forward * Distance - Right * (CenterX * Distance * TanX)
		- Up * (CenterY * Distance * TanY);
	return View;
}

void ADreamMainMenuScene::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// 编辑器驾驶此 Actor 时也能看到默认 16:9 构图；运行时再使用真实窗口比例。
	UpdateCamera(16.0f / 9.0f);
}

void ADreamMainMenuScene::BeginPlay()
{
	Super::BeginPlay();
	// 生成的关卡已经保存好父子关系。这次检查让手动加入、带同一标签的展示网格也能跟随。
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TEXT("DreamMainMenuMesh")))
		{
			It->AttachToComponent(BuildingPivot, FAttachmentTransformRules::KeepWorldTransform);
		}
	}
	if (OutlineMaterial)
	{
		RuntimeOutline = UMaterialInstanceDynamic::Create(OutlineMaterial, this);
		for (TActorIterator<APostProcessVolume> It(GetWorld()); It; ++It)
		{
			if (!It->ActorHasTag(TEXT("DreamMainMenuPostProcess")))
				continue;
			for (FWeightedBlendable& Entry : It->Settings.WeightedBlendables.Array)
			{
				if (Entry.Object == OutlineMaterial)
					Entry.Object = RuntimeOutline;
			}
		}
	}
	UpdateHatchingCoordinates();
}

void ADreamMainMenuScene::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// 飞入由控制器统一推进。此时窗口变化只更新投影比例，不重新运行全建筑取景，
	// 否则一次窗口缩放会把已经靠近天台的镜头突然弹回菜单远景。
	if (bApproachingRooftop)
	{
		if (const APlayerController* Controller = GetWorld()->GetFirstPlayerController())
		{
			int32 Width = 0, Height = 0;
			Controller->GetViewportSize(Width, Height);
			if (Width > 0 && Height > 0)
				MenuCamera->SetAspectRatio(static_cast<float>(Width) / Height);
		}
		return;
	}
	// 角度按秒累计并保持在一圈以内，帧率改变不影响速度，久置也不会损失浮点精度。
	RotationAngle = FMath::Fmod(RotationAngle + RotationSpeed * DeltaSeconds, 360.0f);
	BuildingPivot->SetRelativeRotation(FRotator(0.0f, RotationAngle, 0.0f));
	UpdateHatchingCoordinates();
	if (const APlayerController* Controller = GetWorld()->GetFirstPlayerController())
	{
		int32 Width = 0, Height = 0;
		Controller->GetViewportSize(Width, Height);
		if (Width > 0 && Height > 0)
		{
			const float Aspect = static_cast<float>(Width) / Height;
			if (!FMath::IsNearlyEqual(Aspect, LastAspectRatio, 0.001f) || bLogoOnRight != bLastLogoOnRight)
				UpdateCamera(Aspect);
		}
	}
}

void ADreamMainMenuScene::UpdateCamera(float AspectRatio)
{
	if (bApproachingRooftop)
		return;
	const FMinimalViewInfo View = ComputeCameraView(BuildingPivot->GetComponentLocation(),
		BuildingHalfExtent, AspectRatio, bLogoOnRight);
	MenuCamera->SetWorldLocationAndRotation(View.Location, View.Rotation);
	MenuCamera->SetFieldOfView(View.FOV);
	MenuCamera->SetAspectRatio(View.AspectRatio);
	LastAspectRatio = AspectRatio;
	bLastLogoOnRight = bLogoOnRight;
}

FMinimalViewInfo ADreamMainMenuScene::GetGameplayEntryView() const
{
	FMinimalViewInfo View;
	View.Location = RooftopPlayerLocation + RooftopPlayerRotation.RotateVector(RooftopCameraOffset);
	const FVector Focus = RooftopPlayerLocation + RooftopPlayerRotation.RotateVector(RooftopFocusOffset);
	View.Rotation = (Focus - View.Location).Rotation();
	View.FOV = RooftopFieldOfView;
	View.AspectRatio = MenuCamera->AspectRatio;
	View.bConstrainAspectRatio = false;
	View.AspectRatioAxisConstraint = AspectRatio_MaintainXFOV;
	return View;
}

void ADreamMainMenuScene::BeginRooftopApproach(float Duration)
{
	if (bApproachingRooftop)
		return;
	bApproachingRooftop = true;
	ApproachDuration = FMath::Max(Duration, 0.01f);
	// 以最短路径回到玩法建筑的初始姿态，359° 只需退回 1°，不会在飞入时额外绕一圈。
	ApproachStartAngle = FMath::UnwindDegrees(RotationAngle);
	MenuCamera->GetCameraView(0.0f, ApproachStartView);
}

void ADreamMainMenuScene::AdvanceRooftopApproach(float Elapsed)
{
	if (!bApproachingRooftop)
		return;
	const float Time = FMath::Clamp(Elapsed, 0.0f, ApproachDuration);
	const float T = Time / ApproachDuration;
	const float Alpha = UDreamMenuTransitionSubsystem::Ease(T);
	// 五次 Hermite 插值同时约束角度、角速度和角加速度：开头延续 2°/秒，
	// 终点回到 0° 且完全静止。归位贯穿整个推进，不能在切图那一帧突然归零。
	const float VelocityBasis = T - 6.0f * T * T * T + 8.0f * T * T * T * T - 3.0f * T * T * T * T * T;
	RotationAngle = ApproachStartAngle * (1.0f - Alpha) + RotationSpeed * ApproachDuration * VelocityBasis;
	BuildingPivot->SetRelativeRotation(FRotator(0.0f, RotationAngle, 0.0f));
	UpdateHatchingCoordinates();

	const FMinimalViewInfo Target = GetGameplayEntryView();
	const FTransform Display = BuildingPivot->GetComponentTransform();
	const FVector TargetLocation = Display.TransformPosition(
		GameplayBuildingTransform.InverseTransformPosition(Target.Location));
	const FQuat TargetRotation = Display.GetRotation() * GameplayBuildingTransform.GetRotation().Inverse()
		* Target.Rotation.Quaternion();
	// 大距离推进采用对数距离：最后几十米不会比前半段冲得更快。
	// 从天台终点回看起点的距离指数缩短，朝向以四元数插值消除角度跨 ±180° 的跳变。
	const FVector Offset = ApproachStartView.Location - TargetLocation;
	const double Distance = Offset.Length();
	const double SoftDistance = 900.0;
	const double Remaining = FMath::Exp(FMath::Lerp(FMath::Loge(Distance + SoftDistance),
		FMath::Loge(SoftDistance), static_cast<double>(Alpha))) - SoftDistance;
	const FVector Position = TargetLocation + Offset.GetSafeNormal() * Remaining;
	const FQuat Rotation = FQuat::Slerp(ApproachStartView.Rotation.Quaternion(), TargetRotation, Alpha).GetNormalized();
	MenuCamera->SetWorldLocationAndRotation(Position, Rotation);
	MenuCamera->SetFieldOfView(FMath::Lerp(ApproachStartView.FOV, Target.FOV, Alpha));
}

void ADreamMainMenuScene::UpdateHatchingCoordinates()
{
	if (!RuntimeOutline)
		return;
	const FVector Origin = BuildingPivot->GetComponentLocation();
	const FQuat Rotation = BuildingPivot->GetComponentQuat();
	const auto ColorOf = [](const FVector& Value) { return FLinearColor(Value.X, Value.Y, Value.Z, 0.0f); };
	RuntimeOutline->SetVectorParameterValue(TEXT("BuildingOrigin"), ColorOf(Origin));
	// 点积世界坐标与这三个世界轴就是逆旋转；法线也必须使用同一组轴来混合三平面排线。
	RuntimeOutline->SetVectorParameterValue(TEXT("BuildingAxisX"), ColorOf(Rotation.GetAxisX()));
	RuntimeOutline->SetVectorParameterValue(TEXT("BuildingAxisY"), ColorOf(Rotation.GetAxisY()));
	RuntimeOutline->SetVectorParameterValue(TEXT("BuildingAxisZ"), ColorOf(Rotation.GetAxisZ()));
}
