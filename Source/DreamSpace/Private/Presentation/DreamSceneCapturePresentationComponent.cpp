#include "DreamSceneCapturePresentationComponent.h"

#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/WidgetComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraTypes.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RotationMatrix.h"
#include "Widgets/Images/SImage.h"

UDreamSceneCapturePresentationComponent::UDreamSceneCapturePresentationComponent()
{
	// 捕获相机位置要在角色、携带根参考系和装配体表现都更新之后计算。
	// 使用 PostUpdateWork 可以避免读取到上一阶段的携带变换。
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UDreamSceneCapturePresentationComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!GetOwner() || !GetWorld())
	{
		UE_LOG(LogTemp, Warning, TEXT("场景缩略图表现组件缺少有效 Owner：%s"), *GetNameSafe(GetOwner()));
		SetComponentTickEnabled(false);
		return;
	}

	CreatePresentationResources();
	SetPresentationActive(bEnabledAtBeginPlay);

	// 组件默认不依赖另一套玩法逻辑；第一次捕获只在资源已创建且当前确实需要显示时执行。
	if (bPresentationActive)
		RefreshCaptureNow();
}

void UDreamSceneCapturePresentationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyPresentationResources();
	Super::EndPlay(EndPlayReason);
}

void UDreamSceneCapturePresentationComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bPresentationActive)
	{
		// 每帧更新捕获相机的位置、面片朝向和过滤列表；SceneCapture 自己负责在渲染阶段捕获。
		UpdateCaptureBlacklist();
		UpdateCaptureView();
		UpdateDisplayFacing();
	}
}

void UDreamSceneCapturePresentationComponent::CreatePresentationResources()
{
	if (!GetWorld() || CaptureActor || DisplayWidget)
		return;

	RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("SceneCaptureRenderTarget"));
	RenderTarget->RenderTargetFormat = RTF_RGBA8;
	RenderTarget->ClearColor = CaptureClearColor;
	RenderTarget->InitAutoFormat(
		FMath::Max(RenderTargetWidth, 64), FMath::Max(RenderTargetHeight, 64));
	RenderTarget->UpdateResourceImmediate(true);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = GetOwner();
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.Name = MakeUniqueObjectName(GetWorld(), ASceneCapture2D::StaticClass(), TEXT("DreamSceneCapture"));
	CaptureActor = GetWorld()->SpawnActor<ASceneCapture2D>(
		ASceneCapture2D::StaticClass(), FTransform::Identity, SpawnParameters);
	if (!CaptureActor)
	{
		UE_LOG(LogTemp, Error, TEXT("无法创建场景缩略图 SceneCapture2D：%s"), *GetNameSafe(GetOwner()));
		RenderTarget = nullptr;
		return;
	}

	CaptureActor->SetActorEnableCollision(false);
	CaptureActor->SetActorHiddenInGame(true);
	CaptureActor->SetActorTickEnabled(false);

	USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D();
	if (!CaptureComponent)
	{
		DestroyPresentationResources();
		return;
	}

	CaptureComponent->TextureTarget = RenderTarget;
	CaptureComponent->CaptureSource = CaptureSource;
	CaptureComponent->ProjectionType = ProjectionType;
	CaptureComponent->FOVAngle = CaptureFOV;
	// 黑名单模式：正常渲染场景中的 Primitive，再排除 HiddenActors。
	CaptureComponent->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	CaptureComponent->bCaptureEveryFrame = true;
	CaptureComponent->bCaptureOnMovement = false;
	CaptureComponent->bAlwaysPersistRenderingState = true;
	CaptureComponent->bConsiderUnrenderedOpaquePixelAsFullyTranslucent = true;
	if (bHideOwnerActor)
		CaptureComponent->HiddenActors.AddUnique(GetOwner());
	CaptureComponent->HiddenActors.AddUnique(CaptureActor);

	// 使用世界空间 WidgetComponent 作为原型显示面。
	// 这只是 RenderTarget 的承载方式，不属于玩法 UI，也不会添加到屏幕 HUD。
	DisplayWidget = NewObject<UWidgetComponent>(GetOwner(), TEXT("SceneCaptureDisplay"), RF_Transient);
	DisplayWidget->SetMobility(EComponentMobility::Movable);
	DisplayWidget->SetupAttachment(this);
	DisplayWidget->SetWidgetSpace(EWidgetSpace::World);
	DisplayWidget->SetGeometryMode(EWidgetGeometryMode::Plane);
	DisplayWidget->SetDrawSize(DisplaySize);
	DisplayWidget->SetPivot(FVector2D(0.5f, 0.5f));
	DisplayWidget->SetTwoSided(bDisplayTwoSided);
	DisplayWidget->SetBlendMode(EWidgetBlendMode::Opaque);
	DisplayWidget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DisplayWidget->SetGenerateOverlapEvents(false);
	DisplayWidget->SetHiddenInGame(true);
	// 显示面只负责展示捕获结果，不能再次被自己的 SceneCapture 捕获。
	DisplayWidget->SetHiddenInSceneCapture(true);
	DisplayWidget->SetRelativeTransform(DisplayRelativeTransform);
	DisplayWidget->RegisterComponent();

	// 直接使用 Slate Image 显示 RenderTarget，避免实例化 UE5.6 中抽象的 UUserWidget。
	// DisplayBrush 是成员变量，保证 SImage 在组件生命周期内始终能访问有效地址。
	DisplayBrush = FSlateBrush();
	DisplayBrush.DrawAs = ESlateBrushDrawType::Image;
	DisplayBrush.SetResourceObject(RenderTarget);
	DisplayBrush.ImageSize = DisplaySize;
	DisplaySlateWidget = SNew(SImage).Image(&DisplayBrush);
	if (!DisplaySlateWidget.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("无法创建场景缩略图 Slate Image：%s"), *GetNameSafe(GetOwner()));
		return;
	}
	DisplayWidget->SetSlateWidget(DisplaySlateWidget);
}

FTransform UDreamSceneCapturePresentationComponent::MapObserverCameraToCaptureWorld(
	const FTransform& ObserverWorldTransform,
	const FTransform& MiniatureFrameWorldTransform,
	const FTransform& CapturedSceneReferenceWorldTransform,
	float InMiniatureSceneScale)
{
	// UE 的 FTransform 乘法约定是“左侧变换先应用，再应用右侧变换”。
	// 因此这里不直接写一串容易读反的乘法，而是明确拆成三个坐标步骤：
	//
	// 1. 外部观察相机 -> 手办局部坐标；
	// 2. 除以手办缩放，把手办中的相对距离还原到真实场景单位；
	// 3. 手办局部坐标 -> 捕获场景的真实世界坐标。
	//
	// 若手办比例为 0.1，观察相机离手办中心 100 cm，SceneCapture 就会在
	// 捕获场景中离参考原点约 1000 cm 的对应位置，从而产生正确的透视视差。
	const float Scale = FMath::Max(FMath::Abs(InMiniatureSceneScale), KINDA_SMALL_NUMBER);

	FTransform MiniatureFrame = MiniatureFrameWorldTransform;
	// 手办坐标系的比例由显式参数控制；去掉组件继承的 Actor 缩放，避免重复缩放。
	MiniatureFrame.SetScale3D(FVector::OneVector);

	FTransform CameraInMiniature = ObserverWorldTransform.GetRelativeTransform(MiniatureFrame);
	CameraInMiniature.SetLocation(CameraInMiniature.GetLocation() / Scale);
	CameraInMiniature.SetScale3D(FVector::OneVector);

	// 将缩放后的相机从手办局部坐标放回捕获场景参考坐标。
	return CameraInMiniature * CapturedSceneReferenceWorldTransform;
}

void UDreamSceneCapturePresentationComponent::DestroyPresentationResources()
{
	if (DisplayWidget)
	{
		DisplayWidget->SetSlateWidget(nullptr);
		DisplayWidget->DestroyComponent();
		DisplayWidget = nullptr;
	}
	DisplaySlateWidget.Reset();
	DisplayBrush = FSlateBrush();

	if (CaptureActor)
	{
		CaptureActor->Destroy();
		CaptureActor = nullptr;
	}
	RenderTarget = nullptr;
	bPresentationActive = false;
}

void UDreamSceneCapturePresentationComponent::SetPresentationActive(bool bActive)
{
	bPresentationActive = bActive && CaptureActor && DisplayWidget && RenderTarget && DisplaySlateWidget.IsValid();
	if (DisplayWidget)
	{
		DisplayWidget->SetHiddenInGame(!bPresentationActive);
		DisplayWidget->SetVisibility(bPresentationActive);
	}
	if (CaptureActor)
	{
		if (USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D())
		{
			CaptureComponent->bCaptureEveryFrame = bPresentationActive;
			CaptureComponent->SetComponentTickEnabled(bPresentationActive);
		}
	}
	if (bPresentationActive)
		UpdateCaptureBlacklist();
}

void UDreamSceneCapturePresentationComponent::UpdateCaptureBlacklist()
{
	if (!CaptureActor)
		return;
	USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D();
	if (!CaptureComponent)
		return;

	// 重新生成黑名单，允许关卡运行时动态修改 ActorsToHideFromCapture。
	// PRM_RenderScenePrimitives 表示“普通场景全部捕获，明确列出的 Actor 排除”。
	CaptureComponent->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	CaptureComponent->HiddenActors.Reset();
	if (bHideOwnerActor)
		CaptureComponent->HiddenActors.AddUnique(GetOwner());
	CaptureComponent->HiddenActors.AddUnique(CaptureActor);

	for (AActor* Actor : ActorsToHideFromCapture)
		if (IsValid(Actor))
			CaptureComponent->HiddenActors.AddUnique(Actor);
}

void UDreamSceneCapturePresentationComponent::UpdateCaptureView()
{
	if (!CaptureActor || !GetWorld())
		return;

	FMinimalViewInfo PlayerPOV;
	if (bFollowPlayerCamera && GetPlayerCameraPOV(PlayerPOV))
	{
		// 这里的 MiniatureFrame 是“手办局部坐标 -> 外部真实世界”的锚点，
		// 而不是显示面本身。SceneCapture 最终仍接收一个真实世界 Transform。
		const FTransform ObserverWorld(PlayerPOV.Rotation, PlayerPOV.Location);
		const FTransform MiniatureFrame = GetComponentTransform();
		const FTransform SceneReference = ResolveCapturedSceneReference();
		const FTransform CaptureWorld = MapObserverCameraToCaptureWorld(
			ObserverWorld, MiniatureFrame, SceneReference, MiniatureSceneScale);

		CaptureActor->SetActorTransform(CaptureWorld);
		if (USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D())
		{
			// 均匀缩放不会改变透视 FOV；同步投影模式和 FOV 可以避免观察相机切换
			// 或第三人称镜头调整时，手办画面仍使用旧投影参数。
			CaptureComponent->ProjectionType = PlayerPOV.ProjectionMode;
			if (PlayerPOV.ProjectionMode == ECameraProjectionMode::Perspective)
				CaptureComponent->FOVAngle = PlayerPOV.FOV;
			else
				CaptureComponent->OrthoWidth = PlayerPOV.OrthoWidth;
		}
		return;
	}

	FBox SceneBounds(ForceInit);

	if (IsValid(CaptureTargetActor))
		SceneBounds += CaptureTargetActor->GetComponentsBoundingBox(true, false);
	for (AActor* Actor : ActorsToFrame)
		if (IsValid(Actor))
			SceneBounds += Actor->GetComponentsBoundingBox(true, false);

	FVector Target = IsValid(CaptureTargetActor) ? CaptureTargetActor->GetActorLocation() : FVector::ZeroVector;
	float Radius = CaptureDistance;
	if (SceneBounds.IsValid)
	{
		Target = SceneBounds.GetCenter();
		Radius = FMath::Max(SceneBounds.GetExtent().Size(), 1.0f);
	}
	Target += CaptureTargetOffset;

	FRotator Rotation = CaptureRotation;
	const float HalfFOVRadians = FMath::DegreesToRadians(FMath::Clamp(CaptureFOV, 5.0f, 170.0f) * 0.5f);
	const float FrustumDistance = Radius / FMath::Max(FMath::Tan(HalfFOVRadians), 0.001f) * AutoFramePadding;
	const float Distance = SceneBounds.IsValid ? FrustumDistance : CaptureDistance;
	const FVector Location = Target - Rotation.Vector() * Distance;

	CaptureActor->SetActorLocationAndRotation(Location, Rotation);
}

bool UDreamSceneCapturePresentationComponent::GetPlayerCameraPOV(FMinimalViewInfo& OutPOV) const
{
	if (!GetWorld())
		return false;
	const APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (!PC || !PC->PlayerCameraManager)
		return false;
	OutPOV = PC->PlayerCameraManager->GetCameraCacheView();
	return true;
}

FTransform UDreamSceneCapturePresentationComponent::ResolveCapturedSceneReference() const
{
	// 参考 Actor 适合场景有明确根节点的关卡；没有配置时，显式 Transform 默认就是世界坐标系。
	return IsValid(CapturedSceneReferenceActor)
		? CapturedSceneReferenceActor->GetActorTransform()
		: CapturedSceneReferenceTransform;
}

void UDreamSceneCapturePresentationComponent::UpdateDisplayFacing()
{
	if (!DisplayWidget || DisplayFacingMode == EDreamMiniatureFacingMode::Fixed)
		return;

	FMinimalViewInfo POV;
	if (!GetPlayerCameraPOV(POV))
		return;

	// 先按组件坐标计算显示面位置，保持它相对手部锚点的偏移不变。
	const FTransform ComponentWorld = GetComponentTransform();
	const FVector DisplayLocation = ComponentWorld.TransformPosition(DisplayRelativeTransform.GetLocation());
	FVector ToCamera = (POV.Location - DisplayLocation).GetSafeNormal();
	if (ToCamera.IsNearlyZero())
		return;

	FVector Up = DisplayUpDirection.GetSafeNormal();
	if (DisplayFacingMode == EDreamMiniatureFacingMode::FaceCameraAroundWorldUp)
	{
		// 圆柱 Billboard：只在指定上方向的平面内转动，避免镜头从头顶/脚底看时翻面。
		ToCamera = FVector::VectorPlaneProject(ToCamera, Up).GetSafeNormal();
		if (ToCamera.IsNearlyZero())
			return;
	}
	else
	{
		// 完全面向相机时，把上方向投影到面片所在平面，避免产生滚转跳变。
		Up = FVector::VectorPlaneProject(Up, ToCamera).GetSafeNormal();
		if (Up.IsNearlyZero())
			Up = FVector::VectorPlaneProject(FVector::UpVector, ToCamera).GetSafeNormal();
		if (Up.IsNearlyZero())
			return;
	}

	// UWidgetComponent 的 Plane 几何体局部 -Y 是正面法线，
	// 所以让世界 Y 指向 -ToCamera，局部 -Y 就会朝向观察者。
	const FQuat FacingRotation = FRotationMatrix::MakeFromYZ(-ToCamera, Up).ToQuat();
	FTransform DisplayWorld(FacingRotation, DisplayLocation, DisplayRelativeTransform.GetScale3D());
	DisplayWidget->SetWorldTransform(DisplayWorld);
}

void UDreamSceneCapturePresentationComponent::CaptureOnce()
{
	if (!CaptureActor)
		return;
	if (USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D())
	{
		// CaptureScene 是手动捕获接口；与 bCaptureEveryFrame 同时调用会触发引擎的
		// “major inefficiency” 警告。因此只在这一次捕获期间临时关闭自动捕获。
		const bool bWasCapturingEveryFrame = CaptureComponent->bCaptureEveryFrame;
		CaptureComponent->bCaptureEveryFrame = false;
		CaptureComponent->CaptureScene();
		CaptureComponent->bCaptureEveryFrame = bWasCapturingEveryFrame;
	}
}

void UDreamSceneCapturePresentationComponent::RefreshCaptureNow()
{
	if (!CaptureActor || !DisplayWidget)
		return;

	UpdateCaptureBlacklist();
	UpdateCaptureView();
	SetPresentationActive(true);
	CaptureOnce();
}

void UDreamSceneCapturePresentationComponent::SetPresentationEnabled(bool bEnabled)
{
	SetPresentationActive(bEnabled);
	if (bEnabled)
		RefreshCaptureNow();
}
