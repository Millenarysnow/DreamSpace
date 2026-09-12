#include "DreamSceneCapturePresentationComponent.h"

#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
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
		// 每帧更新捕获相机的位置和过滤列表；SceneCapture 自己负责在渲染阶段捕获。
		UpdateCaptureBlacklist();
		UpdateCaptureView();
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
	if (bUsePlayerCameraRotation)
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
			if (PC->PlayerCameraManager)
				Rotation = PC->PlayerCameraManager->GetCameraRotation();

	const float HalfFOVRadians = FMath::DegreesToRadians(FMath::Clamp(CaptureFOV, 5.0f, 170.0f) * 0.5f);
	const float FrustumDistance = Radius / FMath::Max(FMath::Tan(HalfFOVRadians), 0.001f) * AutoFramePadding;
	const float Distance = SceneBounds.IsValid ? FrustumDistance : CaptureDistance;
	const FVector Location = Target - Rotation.Vector() * Distance;

	CaptureActor->SetActorLocationAndRotation(Location, Rotation);
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
