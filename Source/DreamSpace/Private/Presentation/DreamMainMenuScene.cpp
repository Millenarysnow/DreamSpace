#include "DreamMainMenuScene.h"

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
	const FMinimalViewInfo View = ComputeCameraView(BuildingPivot->GetComponentLocation(),
		BuildingHalfExtent, AspectRatio, bLogoOnRight);
	MenuCamera->SetWorldLocationAndRotation(View.Location, View.Rotation);
	MenuCamera->SetFieldOfView(View.FOV);
	MenuCamera->SetAspectRatio(View.AspectRatio);
	LastAspectRatio = AspectRatio;
	bLastLogoOnRight = bLogoOnRight;
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
