#include "DreamSceneCapturePresentationComponent.h"

#include "DreamSceneCaptureAnchor.h"
#include "EngineUtils.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraTypes.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "UObject/SoftObjectPath.h"

UDreamSceneCapturePresentationComponent::UDreamSceneCapturePresentationComponent()
{
	// 捕获相机位置要在角色、携带根参考系和装配体表现都更新之后计算。
	// 使用 PostUpdateWork 可以避免读取到上一阶段的携带变换。
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	// 用软引用保留默认资产路径，既能在 Details 面板中看到明确配置，
	// 又不会在组件构造阶段强制同步加载材质和网格。
	DisplayMeshAsset = TSoftObjectPtr<UStaticMesh>(
		FSoftObjectPath(TEXT("/Engine/BasicShapes/Plane.Plane")));
	DisplayMaterialAsset = TSoftObjectPtr<UMaterialInterface>(
		FSoftObjectPath(TEXT("/Game/DreamInteraction/Materials/M_SceneCaptureDisplay.M_SceneCaptureDisplay")));
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

	// 玩家通常由 GameMode 在运行时生成，不能依赖编辑器给角色填写关卡引用。
	// 仅在当前世界查找专用锚点类及其固定 Tag，避免 PIE 时绑定到编辑器世界，
	// 也避免把碰巧同名/同 Tag 的普通 Actor 当作锚点。关卡约定只放一个，找到
	// 第一个匹配实例便结束；角色重新生成时，新组件也会在这里重新完成绑定。
	CameraOrbitAnchorActor = nullptr;
	for (TActorIterator<ADreamSceneCaptureAnchor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(ADreamSceneCaptureAnchor::AnchorTag))
		{
			CameraOrbitAnchorActor = *It;
			CameraOrbitAnchorActor->SetActorHiddenInGame(true);
			break;
		}
	}
	if (!IsValid(CameraOrbitAnchorActor))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("场景缩略图未找到带 %s Tag 的 DreamSceneCaptureAnchor，将沿用原有参考坐标：%s"),
			*ADreamSceneCaptureAnchor::AnchorTag.ToString(), *GetNameSafe(GetOwner()));
	}

	// 先完成锚点绑定，再创建/启用捕获资源，避免第一帧使用错误的场景中心。
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
	if (!GetWorld() || CaptureActor || DisplayMesh)
		return;

	RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("SceneCaptureRenderTarget"));
	// SceneColorHDR 的 Alpha 是反向不透明度，需要使用带 Alpha 的浮点 RT 保存原始结果。
	RenderTarget->RenderTargetFormat = RTF_RGBA16f;
	RenderTarget->bForceLinearGamma = true;
	RenderTarget->AddressX = TA_Clamp;
	RenderTarget->AddressY = TA_Clamp;
	RenderTarget->bAutoGenerateMips = false;
	RenderTarget->ClearColor = FLinearColor(
		CaptureClearColor.R, CaptureClearColor.G, CaptureClearColor.B, 1.0f);
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

	// 使用静态平面直接采样 SceneCapture RT。这样材质可以明确执行
	// Opacity = 1 - RT.A，而不会经过 Slate 的额外渲染目标和透明度处理。
	DisplayMesh = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("SceneCaptureDisplayMesh"), RF_Transient);
	DisplayMesh->SetMobility(EComponentMobility::Movable);
	DisplayMesh->SetupAttachment(this);
	DisplayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DisplayMesh->SetGenerateOverlapEvents(false);
	DisplayMesh->SetCastShadow(false);
	DisplayMesh->SetHiddenInGame(true);
	DisplayMesh->SetHiddenInSceneCapture(true);
	DisplayMesh->SetRelativeTransform(DisplayRelativeTransform);

	UStaticMesh* MeshAsset = DisplayMeshAsset.LoadSynchronous();
	if (!MeshAsset)
		MeshAsset = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (!MeshAsset)
	{
		UE_LOG(LogTemp, Error, TEXT("无法加载场景缩略图显示平面：%s"), *GetNameSafe(GetOwner()));
		DisplayMesh->DestroyComponent();
		DisplayMesh = nullptr;
		return;
	}
	DisplayMesh->SetStaticMesh(MeshAsset);

	// BasicShapes/Plane 默认尺寸约为 100 cm；使用资源 Bounds 计算比例，
	// 即使后续换成不同原始尺寸的自定义平面，DisplayWorldSize 仍保持厘米语义。
	const FVector2D BaseExtent(
		FMath::Max(MeshAsset->GetBounds().BoxExtent.X * 2.0f, 0.01f),
		FMath::Max(MeshAsset->GetBounds().BoxExtent.Y * 2.0f, 0.01f));
	FTransform MeshRelative = DisplayRelativeTransform;
	MeshRelative.SetScale3D(MeshRelative.GetScale3D() * FVector(
		DisplayWorldSize.X / BaseExtent.X, DisplayWorldSize.Y / BaseExtent.Y, 1.0f));
	DisplayMesh->SetRelativeTransform(MeshRelative);

	UMaterialInterface* MaterialAsset = DisplayMaterialAsset.LoadSynchronous();
	if (!MaterialAsset)
		MaterialAsset = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/DreamInteraction/Materials/M_SceneCaptureDisplay.M_SceneCaptureDisplay"));
	if (!MaterialAsset)
	{
		UE_LOG(LogTemp, Error,
			TEXT("无法加载场景缩略图透明材质；请先运行 EnsureSceneCaptureMaterial：%s"),
			*GetNameSafe(GetOwner()));
		DisplayMesh->DestroyComponent();
		DisplayMesh = nullptr;
		return;
	}
	DisplayMaterialInstance = UMaterialInstanceDynamic::Create(MaterialAsset, this);
	if (!DisplayMaterialInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("无法创建场景缩略图动态材质：%s"), *GetNameSafe(GetOwner()));
		DisplayMesh->DestroyComponent();
		DisplayMesh = nullptr;
		return;
	}
	DisplayMaterialInstance->SetTextureParameterValue(DisplayTextureParameterName, RenderTarget);
	DisplayMesh->SetMaterial(0, DisplayMaterialInstance);
	DisplayMesh->RegisterComponent();
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
	if (DisplayMesh)
	{
		DisplayMesh->DestroyComponent();
		DisplayMesh = nullptr;
	}
	DisplayMaterialInstance = nullptr;

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
	bPresentationActive = bActive && CaptureActor && DisplayMesh && RenderTarget && DisplayMaterialInstance;
	if (DisplayMesh)
	{
		DisplayMesh->SetHiddenInGame(!bPresentationActive);
		DisplayMesh->SetVisibility(bPresentationActive);
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

	// 大气、云和雾由独立渲染通道生成，不属于 HiddenActors 能剔除的普通网格。
	// 只修改这台捕获相机的 ShowFlags；不隐藏真实世界的天空，也不关闭 SkyLighting，
	// 因此主视口仍有天空，手办中的建筑仍可接受已有天空光的照明。
	CaptureComponent->ShowFlags.SetAtmosphere(!bHideAtmosphere);
	CaptureComponent->ShowFlags.SetCloud(!bHideClouds);
	CaptureComponent->ShowFlags.SetFog(!bHideFog);
	CaptureComponent->ShowFlags.SetVolumetricFog(!bHideFog);

	// 重新生成黑名单，允许关卡运行时动态修改 ActorsToHideFromCapture。
	// PRM_RenderScenePrimitives 表示“普通场景全部捕获，明确列出的 Actor 排除”。
	CaptureComponent->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	CaptureComponent->HiddenActors.Reset();
	CaptureComponent->ClearHiddenComponents();
	auto HideActor = [CaptureComponent](AActor* Actor)
	{
		if (!IsValid(Actor))
			return;
		CaptureComponent->HiddenActors.AddUnique(Actor);
		// 天空蓝图可能通过 ChildActorComponent 包含实际的天空网格。
		// 同时剔除这些子 Actor 的 Primitive，避免只隐藏蓝图外壳却漏掉天空球。
		CaptureComponent->HideActorComponents(Actor, true);
	};
	if (bHideOwnerActor)
		HideActor(GetOwner());
	HideActor(CaptureActor);
	if (IsValid(CameraOrbitAnchorActor))
	{
		// 专用锚点本身已默认隐藏；仍纳入捕获黑名单，兼容派生蓝图增加辅助网格。
		HideActor(CameraOrbitAnchorActor);
	}

	for (AActor* Actor : ActorsToHideFromCapture)
		HideActor(Actor);
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
		FTransform FinalCaptureWorld = CaptureWorld;
		if (IsValid(CameraOrbitAnchorActor) && bAimCaptureCameraAtOrbitAnchor)
		{
			const FVector ToAnchor =
				(CameraOrbitAnchorActor->GetActorLocation() - CaptureWorld.GetLocation()).GetSafeNormal();
			if (!ToAnchor.IsNearlyZero())
			{
				// 位置仍由外部观察相机经过缩放映射得到，保证环绕半径和视差正确；
				// 这里只把朝向约束到锚点，避免内层画面中心随着玩家移动漂移。
				FRotator AnchorRotation = ToAnchor.Rotation();
				// 保留外部相机的滚转，避免相机导演有 Roll 时画面突然归零。
				AnchorRotation.Roll = CaptureWorld.Rotator().Roll;
				FinalCaptureWorld.SetRotation(AnchorRotation.Quaternion());
			}
		}

		CaptureActor->SetActorTransform(FinalCaptureWorld);
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
	// 相机轨道锚点优先级最高：它同时定义手办内部的稳定中心和捕获相机的轨道原点。
	// 没有锚点时再沿用原有的参考 Actor/Transform 配置，保证旧关卡行为不变。
	if (IsValid(CameraOrbitAnchorActor))
	{
		FTransform AnchorTransform = CameraOrbitAnchorActor->GetActorTransform();
		// 锚点的缩放只用于编辑器可视化，不应再次改变真实场景的坐标比例。
		AnchorTransform.SetScale3D(FVector::OneVector);
		return AnchorTransform;
	}

	// 参考 Actor 适合场景有明确根节点的关卡；没有配置时，显式 Transform 默认就是世界坐标系。
	return IsValid(CapturedSceneReferenceActor)
		? CapturedSceneReferenceActor->GetActorTransform()
		: CapturedSceneReferenceTransform;
}

void UDreamSceneCapturePresentationComponent::UpdateDisplayFacing()
{
	if (!DisplayMesh || DisplayFacingMode == EDreamMiniatureFacingMode::Fixed)
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
		// 完全面向相机时优先使用相机自身的上方向，使面片与主视口的画面坐标
		// 一致；再投影到面片所在平面，避免相机有轻微滚转时出现拉伸或跳变。
		Up = FVector::VectorPlaneProject(POV.Rotation.RotateVector(FVector::UpVector), ToCamera).GetSafeNormal();
		if (Up.IsNearlyZero())
			Up = FVector::VectorPlaneProject(FVector::UpVector, ToCamera).GetSafeNormal();
		if (Up.IsNearlyZero())
			return;
	}

	// BasicShapes/Plane 的局部 +Z 是正面法线，所以让世界 Z 指向观察者。
	// X 轴取“上方向 x 法线”，可以稳定地保留面片的竖直方向。
	FVector Right = Up.Cross(ToCamera).GetSafeNormal();
	if (Right.IsNearlyZero())
		return;
	if (bRotateDisplayImage180Degrees)
	{
		// 基础 Plane 的 UV 方向与 SceneCapture 输出的画面坐标相差 180 度。
		// 同时反转面片的两个平面轴，就能修正“上下、左右同时颠倒”的现象；
		// 法线轴 ToCamera 保持不变，所以面片仍然朝向观察相机。
		Right = -Right;
	}
	const FQuat FacingRotation = FRotationMatrix::MakeFromXZ(Right, ToCamera).ToQuat();
	// 保留 CreatePresentationResources 根据 Mesh Bounds 和 DisplayWorldSize 算出的真实尺寸。
	const FVector WorldScale = DisplayMesh->GetComponentScale();
	FTransform DisplayWorld(FacingRotation, DisplayLocation, WorldScale);
	DisplayMesh->SetWorldTransform(DisplayWorld);
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
	if (!CaptureActor || !DisplayMesh)
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
