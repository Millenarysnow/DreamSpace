#include "DreamSceneCapturePresentationComponent.h"

#include "DreamSceneCaptureAnchor.h"
#include "EngineUtils.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ChildActorComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "UObject/SoftObjectPath.h"
#include "Components/LineBatchComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

// 控制台变量：在世界中绘制 SceneCapture 相机、锚点和手办面片的方向关系。
// 用法：在游戏中按 ~ 打开控制台，输入 dream.DebugSceneCapture 1 开启，输入 dream.DebugSceneCapture 0 关闭。
static bool bDreamSceneCaptureDebugDraw = false;
static FAutoConsoleVariableRef CVarDreamSceneCaptureDebugDraw(
	TEXT("dream.DebugSceneCapture"),
	bDreamSceneCaptureDebugDraw,
	TEXT("是否在游戏中绘制场景缩略图的捕获相机、锚点与面片朝向调试信息。0=关闭，1=开启。"),
	ECVF_Default);

namespace
{
	/**
	 * 窗口模式使用的观察视线（世界空间，从观察者指向面片中心）。
	 *
	 * 第三人称相机的前方向穿过角色，并不穿过面片；而面片中心对应锚点，
	 * “观察者从哪个角度看这个建筑”就是这条视线。用相机前方向会让画面相对
	 * 面片偏转一个随面片挂点和臂长变化的夹角。观察位置取 SpringArm 未经碰撞
	 * 缩短的理想臂位置，因此镜头被挤近不会改变这个方向。
	 */
	FVector ResolveObserverViewDirection(
		const FTransform& ObserverWorldTransform, const FVector& FrameLocation)
	{
		const FVector LineOfSight = (FrameLocation - ObserverWorldTransform.GetLocation()).GetSafeNormal();
		return LineOfSight.IsNearlyZero()
			? ObserverWorldTransform.GetRotation().GetForwardVector()
			: LineOfSight;
	}
}

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

	// 在游戏开始时扫描当前世界里已经存在的 Actor，把带指定普通 Actor Tag 的对象
	// 自动并入捕获黑名单。使用 AddUnique 保留 Details 中已有的手动配置，同时避免
	// 同一对象重复加入；这项扫描只在 BeginPlay 执行一次，不会在每帧遍历世界。
	static const FName HiddenFromCaptureTag(TEXT("HiddenFromCapture"));
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(HiddenFromCaptureTag))
		{
			ActorsToHideFromCapture.AddUnique(*It);
		}
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
		if (bDreamSceneCaptureDebugDraw)
			DrawCaptureDebug();
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

FTransform UDreamSceneCapturePresentationComponent::MapObserverWindowToCaptureWorld(
	const FTransform& ObserverWorldTransform,
	const FTransform& MiniatureFrameWorldTransform,
	const FTransform& CapturedSceneReferenceWorldTransform,
	float InMiniatureSceneScale)
{
	// 手办坐标系和锚点都只提供**位置**，两者的旋转都不参与映射。
	//
	// 手办组件的旋转属于显示层（原型角色为了让面片朝外设了 180°，见 DreamCharacter 的
	// SetRelativeRotation），锚点旋转属于取景参考系；只要把它们乘进方向，方位角就会
	// 整体翻转，捕获相机跑到玩家相机的另一侧。被捕获的场景始终与世界轴对齐。
	const FVector FrameLocation = MiniatureFrameWorldTransform.GetLocation();
	const FVector AnchorLocation = CapturedSceneReferenceWorldTransform.GetLocation();
	const FVector ObserverLocation = ObserverWorldTransform.GetLocation();

	// 光轴必须穿过面片中心，这样 RT 的四角才落在面片的四角上、锚点落在画面正中。
	// 用观察者前方向代替会让画面相对面片偏转，第三人称相机的前方向穿过的是角色。
	const FVector ViewDirection = ResolveObserverViewDirection(ObserverWorldTransform, FrameLocation);
	const FVector Up = ObserverWorldTransform.GetRotation().GetUpVector();

	// 取景距离 = 观察者到面片的距离 ÷ 手办比例。眼睛离面片越远，等效的真实相机
	// 就退得越远；碰撞推近用的是理想臂长，所以这个距离在贴墙时不会抖动。
	const float Scale = FMath::Max(FMath::Abs(InMiniatureSceneScale), KINDA_SMALL_NUMBER);
	const float WindowDistance = FVector::Distance(ObserverLocation, FrameLocation) / Scale;

	// 捕获相机相对锚点的偏移方向，与观察者相对面片中心的偏移方向相同；
	// 因此两台相机从对应的一侧沿同一条视线看向各自的窗口中心。
	const FQuat CaptureRotation = FRotationMatrix::MakeFromXZ(ViewDirection, Up).ToQuat();
	return FTransform(CaptureRotation, AnchorLocation - ViewDirection * WindowDistance);
}

float UDreamSceneCapturePresentationComponent::ComputeWindowFieldOfView(
	float DisplayWorldWidth, float ObserverDistance)
{
	// 面片在观察者眼中的张角：2·atan((宽/2) / 距离)。RT 使用同一宽高比，
	// 因此水平与竖直张角相同，一个值即可同时满足两者。
	// 距离过小或被手动配置成负数时，用下限避免取景器崩成极广角。
	const float HalfWidth = FMath::Max(DisplayWorldWidth, 0.01f) * 0.5f;
	const float Distance = FMath::Max(ObserverDistance, KINDA_SMALL_NUMBER);
	return FMath::Clamp(
		FMath::RadiansToDegrees(2.0f * FMath::Atan2(HalfWidth, Distance)), 1.0f, 170.0f);
}

bool UDreamSceneCapturePresentationComponent::MapWindowClickToCaptureRay(
	const FVector& ViewRayOrigin, const FVector& ViewRayDirection,
	const FTransform& DisplayWorldTransform, const FBox& DisplayLocalBounds,
	const FVector& WindowObserverLocation, const FVector& CaptureLocation,
	bool bImageRotated180Degrees, FVector& OutDisplayHitPoint,
	FVector& OutCaptureRayOrigin, FVector& OutCaptureRayDirection)
{
	OutDisplayHitPoint = FVector::ZeroVector;
	OutCaptureRayOrigin = FVector::ZeroVector;
	OutCaptureRayDirection = FVector::ZeroVector;
	if (!DisplayLocalBounds.IsValid)
		return false;

	const FVector RayDirection = ViewRayDirection.GetSafeNormal();
	const FVector PlaneNormal = DisplayWorldTransform.GetRotation().GetAxisZ();
	if (RayDirection.IsNearlyZero())
		return false;

	// BasicShapes/Plane 的局部 +Z 是正面。只接受从正面射来的点击：既避免玩家
	// 绕到手办背后仍能操作，也避免与平面平行时除以接近零的数。
	const float RayDotNormal = FVector::DotProduct(RayDirection, PlaneNormal);
	if (RayDotNormal >= -KINDA_SMALL_NUMBER)
		return false;

	// 显示网格没有查询碰撞，因此直接与其真实世界平面求交。Bounds 中心而不是
	// 组件原点用于兼容原始网格枢轴不在中心的平面资产。
	const FVector FrameCenter = DisplayWorldTransform.TransformPosition(DisplayLocalBounds.GetCenter());
	const float HitDistance = FVector::DotProduct(FrameCenter - ViewRayOrigin, PlaneNormal) / RayDotNormal;
	if (HitDistance <= 0.0f)
		return false;
	const FVector DisplayHit = ViewRayOrigin + RayDirection * HitDistance;
	const FVector LocalHit = DisplayWorldTransform.InverseTransformPosition(DisplayHit);
	constexpr float EdgeTolerance = 0.01f;
	if (LocalHit.X < DisplayLocalBounds.Min.X - EdgeTolerance ||
		LocalHit.X > DisplayLocalBounds.Max.X + EdgeTolerance ||
		LocalHit.Y < DisplayLocalBounds.Min.Y - EdgeTolerance ||
		LocalHit.Y > DisplayLocalBounds.Max.Y + EdgeTolerance)
	{
		return false;
	}

	// 在严格窗口模式中，捕获相机 C = A + (O - F) / s，显示面上的点 P 对应
	// Q = A + (P - F) / s，因此 normalize(Q - C) = normalize(P - O)。
	// O 必须是生成该帧 RT 时使用的“理想观察位置”；不能直接换成受墙面碰撞或
	// 滚轮影响的实际玩家相机位置。显示面的前 180° 修正关闭时，基础 Plane
	// 贴图方向相对捕获画面翻转，所以先将 P 关于平面中心翻转再计算方向。
	const FVector SourcePoint = bImageRotated180Degrees
		? DisplayHit : FrameCenter * 2.0f - DisplayHit;
	const FVector CaptureDirection = (SourcePoint - WindowObserverLocation).GetSafeNormal();
	if (CaptureDirection.IsNearlyZero())
		return false;

	OutDisplayHitPoint = DisplayHit;
	OutCaptureRayOrigin = CaptureLocation;
	OutCaptureRayDirection = CaptureDirection;
	return true;
}

bool UDreamSceneCapturePresentationComponent::TryMapViewRayToCaptureRay(
	const FVector& ViewRayOrigin, const FVector& ViewRayDirection,
	FVector& OutDisplayHitPoint, FVector& OutCaptureRayOrigin,
	FVector& OutCaptureRayDirection) const
{
	OutDisplayHitPoint = FVector::ZeroVector;
	OutCaptureRayOrigin = FVector::ZeroVector;
	OutCaptureRayDirection = FVector::ZeroVector;
	if (!bPresentationActive || !bHasWindowObserver || !DisplayMesh || !CaptureActor || !RenderTarget ||
		!bFollowPlayerCamera || !bMatchCaptureFOVToDisplay ||
		ProjectionType != ECameraProjectionMode::Perspective ||
		DisplayFacingMode != EDreamMiniatureFacingMode::FaceCamera)
	{
		return false;
	}

	const UStaticMesh* MeshAsset = DisplayMesh->GetStaticMesh();
	const USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D();
	if (!MeshAsset || !CaptureComponent || RenderTarget->SizeX <= 0 || RenderTarget->SizeY <= 0 ||
		DisplayWorldSize.X <= 0.0f || DisplayWorldSize.Y <= 0.0f)
	{
		return false;
	}

	// 直接复用真实世界射线的方向，要求面片与 RT 使用相同宽高比，且捕获 FOV
	// 没有被极端距离触发的 1°/170° 限幅改变。否则显示图像已经不是严格窗口。
	const float DisplayAspect = DisplayWorldSize.X / DisplayWorldSize.Y;
	const float RenderAspect = static_cast<float>(RenderTarget->SizeX) / RenderTarget->SizeY;
	const float UnclampedFOV = FMath::RadiansToDegrees(
		2.0f * FMath::Atan2(DisplayWorldSize.X * 0.5f,
			FMath::Max(LastWindowObserverDistance, KINDA_SMALL_NUMBER)));
	if (!FMath::IsNearlyEqual(DisplayAspect, RenderAspect, 0.01f) ||
		!FMath::IsNearlyEqual(CaptureComponent->FOVAngle, UnclampedFOV, 0.05f) ||
		FVector::DotProduct(DisplayMesh->GetUpVector(), -CaptureActor->GetActorForwardVector()) < 0.995f)
	{
		return false;
	}

	const FBoxSphereBounds MeshBounds = MeshAsset->GetBounds();
	const FVector DisplayScale = DisplayMesh->GetComponentScale();
	const FVector2D ActualDisplaySize(
		MeshBounds.BoxExtent.X * 2.0 * DisplayScale.X,
		MeshBounds.BoxExtent.Y * 2.0 * DisplayScale.Y);
	// FOV 根据 DisplayWorldSize 推导，父节点或 DisplayRelativeTransform 额外
	// 缩放会改变玩家点到的物理位置，却不改变 RT 的 FOV。此时直接转移方向
	// 会发生偏移，必须拒绝；需要调整大小时应改 DisplayWorldSize 并重建资源。
	if (DisplayScale.X <= 0.0 || DisplayScale.Y <= 0.0 || DisplayScale.Z <= 0.0 ||
		!ActualDisplaySize.Equals(DisplayWorldSize, 0.01))
	{
		return false;
	}
	// 显示资产必须是以中心为基准的薄矩形平面。自定义三维外壳仍可用来表现，
	// 但不能把它的三维包围盒误当成一整块可点击的 RT 窗口。
	if (MeshBounds.BoxExtent.X <= 0.0f || MeshBounds.BoxExtent.Y <= 0.0f ||
		MeshBounds.BoxExtent.Z > 1.0f ||
		!DisplayMesh->GetComponentTransform().TransformPosition(MeshBounds.Origin)
			.Equals(DisplayMesh->GetComponentLocation(), 0.5f))
	{
		return false;
	}
	const FBox LocalBounds(MeshBounds.Origin - MeshBounds.BoxExtent,
		MeshBounds.Origin + MeshBounds.BoxExtent);
	if (!MapWindowClickToCaptureRay(
		ViewRayOrigin, ViewRayDirection, DisplayMesh->GetComponentTransform(), LocalBounds,
		LastWindowObserverLocation, CaptureActor->GetActorLocation(),
		bRotateDisplayImage180Degrees, OutDisplayHitPoint, OutCaptureRayOrigin,
		OutCaptureRayDirection))
	{
		return false;
	}
	return true;
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
	bHasWindowObserver = false;
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

void UDreamSceneCapturePresentationComponent::GetCaptureHiddenActors(TArray<AActor*>& OutActors) const
{
	OutActors.Reset();
	auto AddWithChildActors = [&OutActors](AActor* RootActor)
	{
		TArray<AActor*> Pending;
		Pending.Add(RootActor);
		while (!Pending.IsEmpty())
		{
			AActor* Actor = Pending.Pop(EAllowShrinking::No);
			if (!IsValid(Actor) || OutActors.Contains(Actor))
				continue;
			OutActors.Add(Actor);
			// SceneCapture 的 HideActorComponents(Actor, true) 会递归排除
			// ChildActorComponent 中的可渲染网格。碰撞拾取也必须略过对应的
			// ChildActor，否则天空蓝图等仍可能在 RT 中不可见却挡住交互射线。
			TInlineComponentArray<UChildActorComponent*> ChildComponents(Actor);
			for (const UChildActorComponent* ChildComponent : ChildComponents)
				if (AActor* ChildActor = ChildComponent->GetChildActor())
					Pending.Add(ChildActor);
		}
	};
	if (bHideOwnerActor)
		AddWithChildActors(GetOwner());
	AddWithChildActors(CaptureActor.Get());
	AddWithChildActors(CameraOrbitAnchorActor.Get());
	for (AActor* Actor : ActorsToHideFromCapture)
		AddWithChildActors(Actor);
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
	// 捕获与交互拾取共用同一组 Actor 黑名单；否则玩家可能点到 RT 中根本
	// 没有显示的天空球、角色或临时装饰。
	TArray<AActor*> HiddenActors;
	GetCaptureHiddenActors(HiddenActors);
	for (AActor* Actor : HiddenActors)
		HideActor(Actor);

	// 调试线由世界的 LineBatcher 组件渲染，它们也是普通 Primitive，会被 SceneCapture
	// 拍进手办画面（包括 dream.DebugSceneCapture 画在锚点附近的捕获相机视锥）。
	// 统一从捕获中剔除，调试信息只出现在主视口。
	for (const UWorld::ELineBatcherType Type : {
		UWorld::ELineBatcherType::World, UWorld::ELineBatcherType::WorldPersistent,
		UWorld::ELineBatcherType::Foreground, UWorld::ELineBatcherType::ForegroundPersistent })
	{
		if (ULineBatchComponent* LineBatcher = GetWorld() ? GetWorld()->GetLineBatcher(Type) : nullptr)
			CaptureComponent->HideComponent(LineBatcher);
	}
}

void UDreamSceneCapturePresentationComponent::UpdateCaptureView()
{
	bHasWindowObserver = false;
	if (!CaptureActor || !GetWorld())
		return;

	FMinimalViewInfo PlayerPOV;
	if (bFollowPlayerCamera && GetPlayerCameraPOV(PlayerPOV))
	{
		// 手办坐标系以面片中心为原点，而不是组件原点：面片相对组件的偏移
		// 不能被当成观察视差。观察位置默认取 SpringArm 未经碰撞修正的理想位置，
		// 镜头被障碍推近时手办视角保持不变。
		const FTransform ObserverWorld(PlayerPOV.Rotation, ResolveObserverLocation(PlayerPOV));
		const FTransform MiniatureFrame = ResolveDisplayPlaneWorldTransform();
		const FTransform SceneReference = ResolveCapturedSceneReference();
		USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D();

		// 严格窗口映射：捕获相机的位置、朝向和视场角三者必须同时与面片对齐，
		// 否则画面会相对面片偏转或缩放，“透过手办看世界”就不成立。
		CaptureActor->SetActorTransform(MapObserverWindowToCaptureWorld(
			ObserverWorld, MiniatureFrame, SceneReference, MiniatureSceneScale));
		LastWindowObserverLocation = ObserverWorld.GetLocation();
		LastWindowObserverDistance = FVector::Distance(
			ObserverWorld.GetLocation(), MiniatureFrame.GetLocation());
		bHasWindowObserver = true;
		if (CaptureComponent)
		{
			CaptureComponent->ProjectionType = ProjectionType;
			if (ProjectionType == ECameraProjectionMode::Perspective)
			{
				if (bMatchCaptureFOVToDisplay)
				{
					// 视场角 = 面片在观察者眼中的张角。这样 RT 的整幅画面恰好覆盖
					// 顺着面片看过去的那一束角度，面片边缘对应画面边缘。
					CaptureComponent->FOVAngle = ComputeWindowFieldOfView(
						DisplayWorldSize.X, LastWindowObserverDistance);
				}
				else
				{
					CaptureComponent->FOVAngle = CaptureFOV;
				}
			}
			else
			{
				CaptureComponent->OrthoWidth = DisplayWorldSize.X;
			}
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
	if (bAimCaptureCameraAtOrbitAnchor && IsValid(CameraOrbitAnchorActor))
	{
		// 未跟随玩家相机时，允许锚点把固定取景的朝向锁定到场景中心。
		// 跟随玩家的分支在上面已经直接使用观察相机旋转，不会走到这里。
		const FVector ToAnchor =
			(CameraOrbitAnchorActor->GetActorLocation() - Target).GetSafeNormal();
		if (!ToAnchor.IsNearlyZero())
		{
			Rotation = ToAnchor.Rotation();
			Rotation.Roll = CaptureRotation.Roll;
		}
	}
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

FTransform UDreamSceneCapturePresentationComponent::ResolveDisplayPlaneWorldTransform() const
{
	// 手办坐标系：原点在面片中心，朝向跟随组件（手部）。不使用 DisplayMesh 的
	// 世界旋转，因为 FaceCamera 会让面片每帧转向相机，若以它为参考系，
	// 观察方向会被自身抵消，手办里的建筑永远只显示同一个侧面。
	FTransform Frame = GetComponentTransform();
	Frame.SetLocation(Frame.TransformPosition(DisplayRelativeTransform.GetLocation()));
	Frame.SetScale3D(FVector::OneVector);
	return Frame;
}

FVector UDreamSceneCapturePresentationComponent::ResolveObserverLocation(const FMinimalViewInfo& POV) const
{
	if (!bIgnoreSpringArmCollision || !GetWorld())
		return POV.Location;
	const APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	const AActor* ViewTarget = PC ? PC->GetViewTarget() : nullptr;
	if (!ViewTarget)
		return POV.Location;

	// 从当前激活的相机沿父级向上找驱动它的 SpringArm；找不到时回退到任意 SpringArm。
	const USpringArmComponent* SpringArm = nullptr;
	TInlineComponentArray<UCameraComponent*> Cameras(ViewTarget);
	for (const UCameraComponent* Camera : Cameras)
	{
		if (!Camera->IsActive())
			continue;
		for (const USceneComponent* Parent = Camera->GetAttachParent(); Parent; Parent = Parent->GetAttachParent())
		{
			if (const USpringArmComponent* Arm = Cast<USpringArmComponent>(Parent))
			{
				SpringArm = Arm;
				break;
			}
		}
		if (SpringArm)
			break;
	}
	if (!SpringArm)
		SpringArm = ViewTarget->FindComponentByClass<USpringArmComponent>();
	if (!SpringArm)
		return POV.Location;

	// 手办用固定的 ObserverArmLength 计算观察位置，而不是实时的 TargetArmLength。
	// 这样玩家用滚轮改实际相机臂长时，手办视角保持不变。
	// bFollowCameraZoom 开启时才跟随实际臂长。
	const float ArmLength = bFollowCameraZoom ? SpringArm->TargetArmLength : ObserverArmLength;
	const FVector SocketOffset = SpringArm->SocketOffset;
	const FVector TargetOffset = SpringArm->TargetOffset;

	// 复现 SpringArm 的位置计算：枢轴 = ComponentLocation + SocketOffset，
	// 镜头理想位置 = 枢轴 + TargetOffset - CameraForward × ArmLength。
	// 这里不用 GetUnfixedCameraPosition()，因为它返回的是 TargetArmLength 那根臂。
	const FTransform ArmWorld = SpringArm->GetComponentTransform();
	const FVector PivotWorld = ArmWorld.TransformPosition(SocketOffset);
	const FQuat ArmRotation = (SpringArm->bUsePawnControlRotation && ViewTarget->GetInstigatorController())
		? ViewTarget->GetInstigatorController()->GetControlRotation().Quaternion()
		: ArmWorld.GetRotation();
	const FVector DesiredLocation = PivotWorld + ArmRotation.RotateVector(TargetOffset - FVector(ArmLength, 0.0f, 0.0f));

	// SpringArm 首次更新前 GetUnfixedCameraPosition 是零向量；这里也做同样的检查。
	return DesiredLocation.IsZero() ? POV.Location : DesiredLocation;
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

	// 面片法线直接取自捕获相机的光轴：RT 画面本来就是垂直于这条轴的成像平面，
	// 让面片也垂直于它，画面上下的方向就在构造上不会与面片错开。
	// 若改成“面片 -> 真实相机位置”，面片偏在角色一侧、镜头又被挤近时，
	// 两者会相差一个随臂长变化的夹角，看起来像额外倾斜。
	// 不跟随玩家相机时没有捕获视线可依，退回朝向真实相机。
	FVector ToCamera;
	if (bFollowPlayerCamera && CaptureActor)
	{
		ToCamera = -CaptureActor->GetActorForwardVector();
	}
	else
	{
		ToCamera = (POV.Location - DisplayLocation).GetSafeNormal();
	}
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

void UDreamSceneCapturePresentationComponent::DrawCaptureDebug() const
{
#if ENABLE_DRAW_DEBUG
	const UWorld* World = GetWorld();
	if (!World || !CaptureActor)
		return;
	const USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D();
	FMinimalViewInfo POV;
	if (!CaptureComponent || !GetPlayerCameraPOV(POV))
		return;

	const FTransform CaptureWorld = CaptureActor->GetActorTransform();
	const FTransform SceneReference = ResolveCapturedSceneReference();
	const FTransform MiniatureFrame = ResolveDisplayPlaneWorldTransform();
	const FVector ObserverLocation = ResolveObserverLocation(POV);
	const FVector CaptureForward = CaptureWorld.GetRotation().GetForwardVector();
	const FVector PlaneNormal = DisplayMesh ? DisplayMesh->GetUpVector() : FVector::ZeroVector;
	// 映射使用的观察方向：从理想观察位置指向面片中心的视线。世界空间直接比较，
	// 不绕道手办坐标系。
	const FVector ObserverForward = ResolveObserverViewDirection(
		FTransform(POV.Rotation, ObserverLocation), MiniatureFrame.GetLocation());

	// 被捕获场景一侧：锚点（黄）、捕获相机视锥（品红）、捕获视线（品红虚线）。
	const FVector AnchorLocation = SceneReference.GetLocation();
	DrawDebugSphere(World, AnchorLocation, 40.0f, 16, FColor::Yellow, false, -1.0f, 0, 2.0f);
	DrawDebugCoordinateSystem(World, AnchorLocation, SceneReference.Rotator(), 150.0f, false, -1.0f, 0, 2.0f);
	DrawDebugCamera(World, CaptureWorld.GetLocation(), CaptureWorld.Rotator(), CaptureComponent->FOVAngle,
		12.0f, FColor::Magenta, false, -1.0f, 0);
	DrawDebugLine(World, CaptureWorld.GetLocation(), AnchorLocation, FColor::Magenta, false, -1.0f, 0, 2.0f);
	DrawDebugString(World, CaptureWorld.GetLocation() + FVector(0.0f, 0.0f, 80.0f),
		TEXT("SceneCapture"), nullptr, FColor::Magenta, 0.0f, true);

	// 手办一侧：以面片中心为起点画方向线。捕获相机光轴穿过面片中心（锚点），
	// 所以红线（捕获相机前方向）应与绿线（观察视线）重合，蓝线（面片法线）与它们正好反向。
	const FVector FrameLocation = MiniatureFrame.GetLocation();
	constexpr float ArrowLength = 60.0f;
	DrawDebugDirectionalArrow(World, FrameLocation, FrameLocation + ObserverForward * ArrowLength,
		6.0f, FColor::Green, false, -1.0f, SDPG_Foreground, 1.5f);
	DrawDebugDirectionalArrow(World, FrameLocation, FrameLocation + CaptureForward * ArrowLength,
		6.0f, FColor::Red, false, -1.0f, SDPG_Foreground, 1.0f);
	if (!PlaneNormal.IsNearlyZero())
		DrawDebugDirectionalArrow(World, FrameLocation, FrameLocation + PlaneNormal * ArrowLength,
			6.0f, FColor::Blue, false, -1.0f, SDPG_Foreground, 1.5f);
	DrawDebugCoordinateSystem(World, FrameLocation, MiniatureFrame.Rotator(), 20.0f, false, -1.0f, SDPG_Foreground, 1.0f);

	// 观察者位置：真实相机（橙）与未经碰撞修正的理想位置（白），推近时两者分开。
	DrawDebugSphere(World, POV.Location, 6.0f, 8, FColor::Orange, false, -1.0f, 0, 1.0f);
	DrawDebugSphere(World, ObserverLocation, 6.0f, 8, FColor::White, false, -1.0f, 0, 1.0f);
	DrawDebugLine(World, POV.Location, ObserverLocation, FColor::White, false, -1.0f, 0, 1.0f);

	if (GEngine)
	{
		const auto AngleBetween = [](const FVector& A, const FVector& B)
		{
			return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(A.GetSafeNormal() | B.GetSafeNormal(), -1.0, 1.0)));
		};
		const FRotator CaptureRotator = CaptureWorld.Rotator();
		const float ObserverDistance = FVector::Distance(ObserverLocation, FrameLocation);
		const FString Text = FString::Printf(
			TEXT("[dream.DebugSceneCapture]\n")
			TEXT("玩家相机  Pitch %.1f  Yaw %.1f   真实臂长 %.0f  理想臂长 %.0f\n")
			TEXT("捕获相机  Pitch %.1f  Yaw %.1f   到锚点 %.0f  FOV %.1f (面片张角 %.1f)\n")
			TEXT("绿(观察视线)-红(捕获光轴) 夹角 %.2f°   蓝(面片法线)与 -绿 夹角 %.2f°\n")
			TEXT("窗口景别  眼睛到面片 %.0f  ⇒ 覆盖真实范围 ≈ %.0f cm (比例 %.3f)\n")
			TEXT("位置  捕获 %s  锚点 %s"),
			POV.Rotation.Pitch, POV.Rotation.Yaw,
			FVector::Distance(POV.Location, FrameLocation), ObserverDistance,
			CaptureRotator.Pitch, CaptureRotator.Yaw,
			FVector::Distance(CaptureWorld.GetLocation(), AnchorLocation), CaptureComponent->FOVAngle,
			ComputeWindowFieldOfView(DisplayWorldSize.X, ObserverDistance),
			AngleBetween(ObserverForward, CaptureForward),
			PlaneNormal.IsNearlyZero() ? -1.0 : AngleBetween(PlaneNormal, -ObserverForward),
			ObserverDistance, DisplayWorldSize.X / FMath::Max(MiniatureSceneScale, KINDA_SMALL_NUMBER),
			MiniatureSceneScale,
			*CaptureWorld.GetLocation().ToCompactString(), *AnchorLocation.ToCompactString());
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.0f, FColor::Cyan, Text, false);
	}
#endif
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
