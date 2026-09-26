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
	 * 捕获视线与面片法线共用的观察方向（世界空间，从观察者指向手办）。
	 *
	 * 捕获相机沿这个方向看锚点，面片也必须沿这个方向的反方向朝外；两者只要
	 * 用了不同的方向，画面内容和面片朝向就会互相错开，看起来像额外倾斜。
	 * - bAlongLineOfSight（默认）：取“观察者 -> 面片中心”的视线，
	 *   dir = normalize(面片位置 − 观察者位置)。这是真实手办的取景方式，面片严格
	 *   垂直于视线，不会有透视压缩；观察者位置取 SpringArm 未经碰撞缩短的理想臂位置，
	 *   因此镜头被挤近不会改变这个方向；
	 * - 否则取观察相机前方向：转动量与鼠标绕转 1:1 对应，但与面片之间会差一个
	 *   随面片偏移和臂长变化的夹角。
	 */
	FVector ResolveObserverViewDirection(
		const FTransform& ObserverWorldTransform, const FVector& FrameLocation, bool bAlongLineOfSight)
	{
		const FVector Forward = ObserverWorldTransform.GetRotation().GetForwardVector();
		if (!bAlongLineOfSight)
			return Forward;
		const FVector LineOfSight = (FrameLocation - ObserverWorldTransform.GetLocation()).GetSafeNormal();
		return LineOfSight.IsNearlyZero() ? Forward : LineOfSight;
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

FTransform UDreamSceneCapturePresentationComponent::MapObserverOrbitToCaptureWorld(
	const FTransform& ObserverWorldTransform,
	const FTransform& MiniatureFrameWorldTransform,
	const FTransform& CapturedSceneReferenceWorldTransform,
	float OrbitRadius,
	bool bAlongLineOfSightToFrame)
{
	// 两个参考系都只用位置：取景距离由 OrbitRadius 显式给出，
	// 朝向在下面单独说明，不允许组件或锚点自带的缩放再次改变捕获距离。
	const FVector FrameLocation = MiniatureFrameWorldTransform.GetLocation();
	const FVector AnchorLocation = CapturedSceneReferenceWorldTransform.GetLocation();

	// 默认按“眼睛 -> 面片中心”的视线取景：捕获相机放在锚点沿视线方向的反侧，
	// 玩家绕角色转到哪一侧、俯到什么角度，就从同一侧、同一俯角看建筑。
	// 这条视线里的观察位置来自理想臂长，所以画面不会随镜头被挤近而改变。
	// 关掉该选项时退化为相机前方向（转动 1:1，但面片会与视线差一个夹角）。
	// UpdateDisplayFacing 使用同一个函数计算面片法线，保证画面与面片朝向一致。
	const FVector ViewDirection = ResolveObserverViewDirection(
		ObserverWorldTransform, FrameLocation, bAlongLineOfSightToFrame);

	// 上方向取观察相机的上方向，与 FaceCamera 面片的计算方式一致，
	// 这样 RT 画面的“上”和面片在主视口中的“上”是同一个方向。
	const FVector Up = ObserverWorldTransform.GetRotation().GetUpVector();

	// 视线与上方向都不经过手办坐标系或锚点的旋转。
	//
	// 手办坐标系（面片组件）自带的旋转描述的是“面片朝向哪一边”，例如原型角色为了让
	// 面片朝外设了 180°（见 DreamCharacter 的 SetRelativeRotation）。那是显示层的朝向，
	// 不是被捕获场景的朝向。一旦把它乘进捕获方向，方位角会被整体翻转 180°，
	// 捕获相机就跑到玩家相机的另一侧——玩家转到手办连线的正后方时最明显。
	// 锚点旋转同理：它只描述参考系朝向，不该改变“相机在玩家的哪一侧”这个
	// 世界空间的事实。因此两者都只提供原点，不参与方向计算。
	//
	// 捕获相机站在锚点背后、沿视线看向锚点：位置 = 锚点 − 视线 × 半径。
	// MakeFromXZ 保持 X（视线）不变，只把上方向正交化到垂直于视线的平面上。
	const float Radius = FMath::Max(OrbitRadius, 0.0f);
	const FQuat CaptureRotation = FRotationMatrix::MakeFromXZ(ViewDirection, Up).ToQuat();
	return FTransform(CaptureRotation, AnchorLocation - ViewDirection * Radius);
}

FTransform UDreamSceneCapturePresentationComponent::MapObserverCameraToCaptureWorld(
	const FTransform& ObserverWorldTransform,
	const FTransform& MiniatureFrameWorldTransform,
	const FTransform& CapturedSceneReferenceWorldTransform,
	float InMiniatureSceneScale)
{
	// 与固定轨道同一套约定：手办坐标系和锚点都只提供**位置**，两者的旋转都不参与映射。
	//
	// 手办组件的旋转属于显示层（原型角色为了让面片朝外设了 180°，见 DreamCharacter 的
	// SetRelativeRotation），锚点旋转属于取景参考系；只要把它们乘进方向，方位角就会
	// 整体翻转，捕获相机跑到玩家相机的另一侧。被捕获的场景始终与世界轴对齐，
	// 玩家的观察位姿原样缩放即可得到正确的视差。
	const float Scale = FMath::Max(FMath::Abs(InMiniatureSceneScale), KINDA_SMALL_NUMBER);
	const FVector ObserverOffset =
		ObserverWorldTransform.GetLocation() - MiniatureFrameWorldTransform.GetLocation();

	return FTransform(
		ObserverWorldTransform.GetRotation(),
		CapturedSceneReferenceWorldTransform.GetLocation() + ObserverOffset / Scale);
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
		const FTransform ObserverWorld(PlayerPOV.Rotation, ResolveObserverLocation(PlayerPOV));
		const FTransform MiniatureFrame = ResolveDisplayPlaneWorldTransform();
		const FTransform SceneReference = ResolveCapturedSceneReference();
		USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D();

		if (bUseFixedCaptureOrbit)
		{
			// 固定半径轨道：捕获相机绕锚点转动、看向锚点，距离恒为 CaptureDistance。
			// 捕获相机总是正对锚点，画面中心就是面片中心，因此使用组件自己的投影参数，
			// 不复制主相机的 FOV（主相机的 FOV 描述的是整个屏幕，而不是手办这块小窗口）。
			CaptureActor->SetActorTransform(MapObserverOrbitToCaptureWorld(
				ObserverWorld, MiniatureFrame, SceneReference, CaptureDistance,
				bOrbitAlongLineOfSightToDisplay));
			if (CaptureComponent)
			{
				CaptureComponent->ProjectionType = ProjectionType;
				CaptureComponent->FOVAngle = CaptureFOV;
			}
			return;
		}

		// 等比映射：观察相机相对面片的完整位姿按 MiniatureSceneScale 还原到被捕获场景，
		// 沿用主相机的旋转、投影和 FOV，得到与真实手办一致的视差。
		CaptureActor->SetActorTransform(MapObserverCameraToCaptureWorld(
			ObserverWorld, MiniatureFrame, SceneReference, MiniatureSceneScale));
		if (CaptureComponent)
		{
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

	// UnfixedCameraPosition 是 SpringArm 按 TargetArmLength 算出、尚未因碰撞缩短的
	// 镜头位置（含相机延迟）。SpringArm 在 TG_PostPhysics 更新，本组件在
	// TG_PostUpdateWork 读取，因此拿到的是当前帧的值。SpringArm 首次更新前它是
	// 零向量，此时退回真实相机位置。
	const FVector UnfixedLocation = SpringArm->GetUnfixedCameraPosition();
	return UnfixedLocation.IsZero() ? POV.Location : UnfixedLocation;
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
	// 映射使用的是观察相机的实际朝向：视线模式下捕获相机沿“观察位置 -> 面片中心”
	// 看过去，其余情况沿观察相机前方向。两者都在世界空间比较，不再绕道手办坐标系。
	const FVector ObserverForward = ResolveObserverViewDirection(
		FTransform(POV.Rotation, ObserverLocation), MiniatureFrame.GetLocation(),
		bOrbitAlongLineOfSightToDisplay);

	// 被捕获场景一侧：锚点（黄）、捕获相机视锥（品红）、捕获视线（品红虚线）。
	const FVector AnchorLocation = SceneReference.GetLocation();
	DrawDebugSphere(World, AnchorLocation, 40.0f, 16, FColor::Yellow, false, -1.0f, 0, 2.0f);
	DrawDebugCoordinateSystem(World, AnchorLocation, SceneReference.Rotator(), 150.0f, false, -1.0f, 0, 2.0f);
	DrawDebugCamera(World, CaptureWorld.GetLocation(), CaptureWorld.Rotator(), CaptureComponent->FOVAngle,
		12.0f, FColor::Magenta, false, -1.0f, 0);
	DrawDebugLine(World, CaptureWorld.GetLocation(), AnchorLocation, FColor::Magenta, false, -1.0f, 0, 2.0f);
	DrawDebugString(World, CaptureWorld.GetLocation() + FVector(0.0f, 0.0f, 80.0f),
		TEXT("SceneCapture"), nullptr, FColor::Magenta, 0.0f, true);

	// 手办一侧：以面片中心为起点画三根方向线。绿线与红线应当重合，
	// 蓝线（面片法线）应当与它们正好反向。
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
		const FString Text = FString::Printf(
			TEXT("[dream.DebugSceneCapture]\n")
			TEXT("玩家相机  Pitch %.1f  Yaw %.1f   真实臂长 %.0f  理想臂长 %.0f\n")
			TEXT("捕获相机  Pitch %.1f  Yaw %.1f   到锚点 %.0f  FOV %.1f\n")
			TEXT("绿(观察视线)-红(捕获相机前方向) 夹角 %.2f°   蓝(面片法线)与 -绿 夹角 %.2f°\n")
			TEXT("位置  捕获 %s  锚点 %s"),
			POV.Rotation.Pitch, POV.Rotation.Yaw,
			FVector::Distance(POV.Location, FrameLocation), FVector::Distance(ObserverLocation, FrameLocation),
			CaptureRotator.Pitch, CaptureRotator.Yaw,
			FVector::Distance(CaptureWorld.GetLocation(), AnchorLocation), CaptureComponent->FOVAngle,
			AngleBetween(ObserverForward, CaptureForward),
			PlaneNormal.IsNearlyZero() ? -1.0 : AngleBetween(PlaneNormal, -ObserverForward),
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
