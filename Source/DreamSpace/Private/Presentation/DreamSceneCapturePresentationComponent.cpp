#include "DreamSceneCapturePresentationComponent.h"

#include "DreamSceneCaptureAnchor.h"
#include "DreamShoulderCameraComponent.h"
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
	// 手办即使开启显示角色，也使用完整身体。明确清除主视角局部剔除的 UserFlags 第 6 位，
	// 防止捕获位置恰好与主镜头相同时复用剔除；主相机的参数仍只属于自己的 MID。
	CaptureComponent->PostProcessSettings.bOverride_UserFlags = true;
	CaptureComponent->PostProcessSettings.UserFlags &= ~UDreamShoulderCameraComponent::OwnerClipViewFlag;
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

bool UDreamSceneCapturePresentationComponent::MapViewRayToDisplayUV(
	const FVector& ViewRayOrigin, const FVector& ViewRayDirection,
	const FTransform& DisplayWorldTransform, const FBox& DisplayLocalBounds,
	FVector& OutDisplayHitPoint, FVector2D& OutUV, FString& OutFailureReason)
{
	OutDisplayHitPoint = FVector::ZeroVector;
	OutUV = FVector2D::ZeroVector;
	OutFailureReason.Reset();
	const FVector Scale = DisplayWorldTransform.GetScale3D();
	if (!DisplayLocalBounds.IsValid || DisplayLocalBounds.GetSize().X <= 0.0 ||
		DisplayLocalBounds.GetSize().Y <= 0.0 || DisplayLocalBounds.GetExtent().Z > 1.0 ||
		Scale.X <= 0.0 || Scale.Y <= 0.0 || Scale.Z <= 0.0)
	{
		OutFailureReason = TEXT("显示网格必须是正缩放的 XY 薄矩形平面");
		return false;
	}

	const FVector RayDirection = ViewRayDirection.GetSafeNormal();
	const FVector PlaneNormal = DisplayWorldTransform.GetRotation().GetAxisZ();
	const double RayDotNormal = FVector::DotProduct(RayDirection, PlaneNormal);
	// 默认 Plane 的正面朝局部 +Z；从背面、与平面平行或零方向的射线均不可点击。
	if (RayDotNormal >= -KINDA_SMALL_NUMBER)
	{
		OutFailureReason = TEXT("鼠标射线平行于显示面，或位于显示面背后");
		return false;
	}
	const FVector FrameCenter = DisplayWorldTransform.TransformPosition(DisplayLocalBounds.GetCenter());
	const double HitDistance = FVector::DotProduct(FrameCenter - ViewRayOrigin, PlaneNormal) / RayDotNormal;
	if (HitDistance <= 0.0)
	{
		OutFailureReason = TEXT("显示面位于鼠标射线起点后方");
		return false;
	}
	const FVector DisplayHit = ViewRayOrigin + RayDirection * HitDistance;
	const FVector LocalHit = DisplayWorldTransform.InverseTransformPosition(DisplayHit);
	constexpr double EdgeTolerance = 0.01;
	if (LocalHit.X < DisplayLocalBounds.Min.X - EdgeTolerance ||
		LocalHit.X > DisplayLocalBounds.Max.X + EdgeTolerance ||
		LocalHit.Y < DisplayLocalBounds.Min.Y - EdgeTolerance ||
		LocalHit.Y > DisplayLocalBounds.Max.Y + EdgeTolerance)
	{
		OutFailureReason = TEXT("点击位置在手办矩形显示面之外");
		return false;
	}

	// BasicShapes/Plane 的 UV0：局部 X 从 Min 到 Max 对应 U=0..1，局部 Y 对应 V=0..1。
	// 先逆变换回网格局部空间，可同时处理挂点旋转、父节点缩放和实际相机被墙推近。
	// 180° 图像修正已经体现在 DisplayWorldTransform 中，不要在此重复翻转 UV。
	OutUV = FVector2D(
		FMath::Clamp((LocalHit.X - DisplayLocalBounds.Min.X) / DisplayLocalBounds.GetSize().X, 0.0, 1.0),
		FMath::Clamp((LocalHit.Y - DisplayLocalBounds.Min.Y) / DisplayLocalBounds.GetSize().Y, 0.0, 1.0));
	OutDisplayHitPoint = DisplayHit;
	return true;
}

bool UDreamSceneCapturePresentationComponent::TryMapViewRayToCaptureRay(
	const FVector& ViewRayOrigin, const FVector& ViewRayDirection,
	FVector& OutDisplayHitPoint, FVector& OutCaptureRayOrigin,
	FVector& OutCaptureRayDirection, FString& OutFailureReason) const
{
	OutDisplayHitPoint = FVector::ZeroVector;
	OutCaptureRayOrigin = FVector::ZeroVector;
	OutCaptureRayDirection = FVector::ZeroVector;
	OutFailureReason.Reset();
	if (!bPresentationActive || !DisplayMesh || !CaptureActor || !RenderTarget)
	{
		OutFailureReason = TEXT("手办未启用，或显示/捕获资源尚未创建");
		return false;
	}
	const UStaticMesh* MeshAsset = DisplayMesh->GetStaticMesh();
	USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D();
	if (!MeshAsset || !CaptureComponent || RenderTarget->SizeX <= 0 || RenderTarget->SizeY <= 0)
	{
		OutFailureReason = TEXT("显示网格、捕获相机或 RT 尺寸无效");
		return false;
	}
	if (CaptureComponent->ProjectionType != ECameraProjectionMode::Perspective)
	{
		OutFailureReason = TEXT("当前手办拾取仅支持透视 SceneCapture");
		return false;
	}

	FVector2D UV;
	if (!MapViewRayToDisplayUV(ViewRayOrigin, ViewRayDirection,
		DisplayMesh->GetComponentTransform(), MeshAsset->GetBoundingBox(),
		OutDisplayHitPoint, UV, OutFailureReason))
	{
		return false;
	}

	// 严格窗口中可简化为 normalize(P - O)，但 80×80 面片配 2200×2500 RT 时，
	// 画面已发生非等比拉伸，不能直接复制方向，也不能拒绝全部点击。
	// 引擎根据 Capture 当前姿态、FOV、RT 宽高比及自定义投影矩阵反投影同一个 UV；
	// 这仍然是数学射线映射，不读取 RT 颜色或深度，也不需要显示面开启碰撞。
	FVector NearPlaneOrigin;
	if (!UGameplayStatics::DeprojectSceneCaptureComponentToWorld(
		CaptureComponent, UV, NearPlaneOrigin, OutCaptureRayDirection) ||
		OutCaptureRayDirection.ContainsNaN() || OutCaptureRayDirection.IsNearlyZero())
	{
		OutFailureReason = TEXT("SceneCapture 投影反算失败");
		OutCaptureRayDirection = FVector::ZeroVector;
		return false;
	}
	// 引擎返回近裁剪面上的起点；透视相机的同一条射线穿过光心，按本功能约定从光心发射。
	OutCaptureRayOrigin = CaptureComponent->GetComponentLocation();
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
	if (!CaptureActor || !GetWorld())
		return;

	FMinimalViewInfo PlayerPOV;
	if (bFollowPlayerCamera && GetPlayerCameraPOV(PlayerPOV))
	{
		// 手办坐标系以面片中心为原点，而不是组件原点：面片相对组件的偏移
		// 不能被当成观察视差。观察位置默认取 SpringArm 未经碰撞修正的理想位置，
		// 镜头被障碍推近时手办视角保持不变。
		const FTransform ObserverWorld = GetObserverTransform(PlayerPOV);
		const FTransform MiniatureFrame = ResolveDisplayPlaneWorldTransform();
		const FTransform SceneReference = ResolveCapturedSceneReference();
		USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D();

		// 严格窗口映射：捕获相机的位置、朝向和视场角三者必须同时与面片对齐，
		// 否则画面会相对面片偏转或缩放，“透过手办看世界”就不成立。
		CaptureActor->SetActorTransform(MapObserverWindowToCaptureWorld(
			ObserverWorld, MiniatureFrame, SceneReference, MiniatureSceneScale));
		const float WindowObserverDistance = FVector::Distance(
			ObserverWorld.GetLocation(), MiniatureFrame.GetLocation());
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
						DisplayWorldSize.X, WindowObserverDistance);
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

FTransform UDreamSceneCapturePresentationComponent::GetObserverTransform(const FMinimalViewInfo& POV) const
{
	const FTransform ActualObserver(POV.Rotation, POV.Location);
	if (!bIgnoreSpringArmCollision || !GetWorld())
		return ActualObserver;
	const APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	const AActor* ViewTarget = PC ? PC->GetViewTarget() : nullptr;
	if (!ViewTarget)
		return ActualObserver;

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
		return ActualObserver;

	// 手办用固定的 ObserverArmLength 计算观察位置，而不是实时的 TargetArmLength。
	// 这样玩家用滚轮改实际相机臂长时，手办视角保持不变。
	// bFollowCameraZoom 开启时才跟随实际臂长。
	if (const UDreamShoulderCameraComponent* ShoulderCamera = Cast<UDreamShoulderCameraComponent>(SpringArm))
	{
		// 固定观察距离继续使用旧的 420 cm 景别；开启缩放跟随后才取平滑后的新臂长。
		// 位置和旋转必须来自同一次相机更新，不能混用真实位置、理想旋转和未阻尼的滚轮目标。
		const float ArmLength = bFollowCameraZoom ? ShoulderCamera->GetSmoothedArmLength() : ObserverArmLength;
		return ShoulderCamera->GetIdealCameraTransform(ArmLength);
	}

	// 普通 SpringArm 的回退路径：TargetOffset 为世界空间，SocketOffset 由目标相机旋转变换。
	// 旧公式把两者的空间颠倒；零偏移时看不出问题，增加越肩或自定义重力后会产生明显偏差。
	const float ArmLength = bFollowCameraZoom ? SpringArm->TargetArmLength : ObserverArmLength;
	const FRotator ArmRotation = SpringArm->GetTargetRotation();
	const FVector DesiredLocation = SpringArm->GetComponentLocation() + SpringArm->TargetOffset
		+ ArmRotation.RotateVector(SpringArm->SocketOffset - FVector(ArmLength, 0.0f, 0.0f));
	return FTransform(ArmRotation, DesiredLocation);
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
	const FVector ObserverLocation = GetObserverTransform(POV).GetLocation();
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
