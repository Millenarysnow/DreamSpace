#include "DreamPasswordChest.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DreamCharacter.h"
#include "DreamPlayerController.h"
#include "DreamSceneCapturePresentationComponent.h"
#include "DreamSpace.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ADreamPasswordChest::ADreamPasswordChest()
{
	// 上锁、已领取以及无人领取的静止状态均不需要 Tick；动画启动时才开启。
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ChestRoot")));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowMaterial(
		TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));

	// 默认方块边长为 100 cm。将网格和简单碰撞放在同一组件上，直接拖入关卡即可被 E 命中。
	auto MakePanel = [&](const TCHAR* Name, USceneComponent* Parent, const FVector& Position, const FVector& Scale)
	{
		UStaticMeshComponent* Panel = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Panel->SetupAttachment(Parent);
		Panel->SetStaticMesh(Cube.Object);
		Panel->SetRelativeLocation(Position);
		Panel->SetRelativeScale3D(Scale);
		Panel->SetMobility(EComponentMobility::Movable);
		Panel->SetCollisionProfileName(TEXT("BlockAll"));
		Panel->SetGenerateOverlapEvents(false);
		return Panel;
	};
	BaseMesh = MakePanel(TEXT("Base"), RootComponent, FVector(0, 0, 4), FVector(0.9, 0.7, 0.08));
	SideMeshes.Add(MakePanel(TEXT("Front"), RootComponent, FVector(0, 31, 29), FVector(0.9, 0.08, 0.42)));
	SideMeshes.Add(MakePanel(TEXT("Back"), RootComponent, FVector(0, -31, 29), FVector(0.9, 0.08, 0.42)));
	SideMeshes.Add(MakePanel(TEXT("Left"), RootComponent, FVector(-41, 0, 29), FVector(0.08, 0.54, 0.42)));
	SideMeshes.Add(MakePanel(TEXT("Right"), RootComponent, FVector(41, 0, 29), FVector(0.08, 0.54, 0.42)));

	LidPivot = CreateDefaultSubobject<USceneComponent>(TEXT("LidPivot"));
	LidPivot->SetupAttachment(RootComponent);
	LidPivot->SetRelativeLocation(FVector(0, -35, 50));
	LidMesh = MakePanel(TEXT("Lid"), LidPivot, FVector(0, 35, 4), FVector(0.9, 0.7, 0.08));
	// 运动盖子可阻挡视线，但不推动玩家或挤动第三人称相机，避免开盖动画穿入角色造成弹飞。
	LidMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	LidMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	GlowRoot = CreateDefaultSubobject<USceneComponent>(TEXT("GlowRoot"));
	GlowRoot->SetupAttachment(RootComponent);
	// 光点位于箱沿内侧略下方，探索镜头从正面俯视时可见，不会被默认的前侧板完全挡住。
	GlowRoot->SetRelativeLocation(FVector(0, 0, 42));
	GlowRoot->SetHiddenInGame(true, true);
	GlowMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Glow"));
	GlowMesh->SetupAttachment(GlowRoot);
	GlowMesh->SetStaticMesh(Sphere.Object);
	GlowMesh->SetRelativeScale3D(FVector(0.1));
	GlowMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GlowMesh->SetGenerateOverlapEvents(false);
	GlowMesh->SetCastShadow(false);
	GlowMesh->SetHiddenInGame(true);
	if (GlowMaterial.Succeeded())
		GlowMesh->SetMaterial(0, GlowMaterial.Object);
	GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlowLight"));
	GlowLight->SetupAttachment(GlowRoot);
	GlowLight->SetLightColor(FLinearColor(0.5f, 0.85f, 1.0f));
	GlowLight->SetIntensity(600.0f);
	GlowLight->SetAttenuationRadius(100.0f);
	GlowLight->SetCastShadows(false);
	GlowLight->SetHiddenInGame(true);
}

void ADreamPasswordChest::BeginPlay()
{
	Super::BeginPlay();
	ClosedLidRotation = LidPivot->GetRelativeRotation().Quaternion();
	GlowHomeTransform = GlowRoot->GetRelativeTransform();
	SetGlowVisible(false);
	if (!IsFourDigitPassword(UnlockPassword))
		UE_LOG(LogDreamSpace, Warning, TEXT("密码箱 %s 的密码必须是四位 ASCII 数字，例如 0007；当前配置将拒绝解锁。"), *GetName());
}

bool ADreamPasswordChest::IsFourDigitPassword(const FString& Password)
{
	if (Password.Len() != 4)
		return false;
	for (TCHAR Digit : Password)
		if (Digit < TEXT('0') || Digit > TEXT('9'))
			return false;
	return true;
}

bool ADreamPasswordChest::CanInteract(const ADreamCharacter* Character) const
{
	if (!IsValid(Character) || Character->GetWorld() != GetWorld() || Character->bHasMiniature ||
		IsActorBeingDestroyed() || FVector::DistSquared(Character->GetActorLocation(), GetActorLocation()) > FMath::Square(InteractionDistance))
		return false;
	const ADreamPlayerController* Controller = Cast<ADreamPlayerController>(Character->GetController());
	return Controller && Controller->GetPawn() == Character && !Controller->IsMiniatureInteractionMode() &&
		!Controller->IsDraggingInteraction() && (!Controller->IsEnteringPassword() || Controller->GetActivePasswordChest() == this);
}

void ADreamPasswordChest::OnInteracted_Implementation(AActor* Interactor)
{
	ADreamCharacter* Character = Cast<ADreamCharacter>(Interactor);
	if (!CanInteract(Character))
		return;
	if (State == EDreamPasswordChestState::Locked)
		CastChecked<ADreamPlayerController>(Character->GetController())->BeginPasswordEntry(this);
	else if (State == EDreamPasswordChestState::AwaitingPickup)
		// 原玩家中途失去控制权时不吞掉奖励。箱子已经打开，无需重复输入密码。
		StartAttraction(Character);
}

bool ADreamPasswordChest::TryUnlock(const FString& Password, ADreamCharacter* Character)
{
	if (State != EDreamPasswordChestState::Locked || !CanInteract(Character) ||
		!IsFourDigitPassword(UnlockPassword) || !IsFourDigitPassword(Password) || Password != UnlockPassword)
		return false;
	Collector = Character;
	CollectorController = Cast<ADreamPlayerController>(Character->GetController());
	State = EDreamPasswordChestState::Opening;
	StateElapsed = 0.0f;
	SetActorTickEnabled(true);
	// 先退出输入会话再通知蓝图；音效回调即使重入，也无法再次解锁或重复增加输入锁。
	CollectorController->ClosePasswordEntry();
	OnChestUnlocked();
	return true;
}

bool ADreamPasswordChest::IsCollectorValid() const
{
	const ADreamCharacter* Character = Collector.Get();
	const ADreamPlayerController* Controller = CollectorController.Get();
	return IsValid(Character) && IsValid(Controller) && Character->GetWorld() == GetWorld() &&
		Controller->GetPawn() == Character && Character->GetController() == Controller && !Character->bHasMiniature;
}

void ADreamPasswordChest::SetGlowVisible(bool bVisible)
{
	// 同步整个特效层级，蓝图附加在 GlowRoot 下的粒子也沿用相同的显示生命周期。
	GlowRoot->SetHiddenInGame(!bVisible, true);
}

void ADreamPasswordChest::StartAttraction(ADreamCharacter* Character)
{
	Collector = Character;
	CollectorController = Cast<ADreamPlayerController>(Character->GetController());
	AttractionStart = GlowRoot->GetComponentLocation();
	State = EDreamPasswordChestState::Attracting;
	StateElapsed = 0.0f;
	SetGlowVisible(true);
	SetActorTickEnabled(true);
}

void ADreamPasswordChest::ReturnGlowToChest()
{
	Collector.Reset();
	CollectorController.Reset();
	GlowRoot->SetRelativeTransform(GlowHomeTransform);
	SetGlowVisible(true);
	State = EDreamPasswordChestState::AwaitingPickup;
	StateElapsed = 0.0f;
	SetActorTickEnabled(false);
}

void ADreamPasswordChest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// 分段消费本帧时间。卡顿的一帧跨过开盖、停留、吸取边界时不会额外等待下一帧。
	float RemainingTime = FMath::Max(0.0f, DeltaSeconds);
	for (int32 Phase = 0; Phase < 3; ++Phase)
	{
		if (State == EDreamPasswordChestState::Opening)
		{
			const float Duration = FMath::Max(0.0f, OpeningDuration);
			const float Step = FMath::Min(RemainingTime, FMath::Max(0.0f, Duration - StateElapsed));
			StateElapsed += Step;
			RemainingTime -= Step;
			const float Alpha = Duration > 0.0f ? FMath::Clamp(StateElapsed / Duration, 0.0f, 1.0f) : 1.0f;
			const float Smooth = Alpha * Alpha * (3.0f - 2.0f * Alpha);
			// 用初始姿态乘局部 X 轴四元数，支持箱子旋转摆放及蓝图设置的初始铰链朝向。
			LidPivot->SetRelativeRotation(ClosedLidRotation * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(OpenAngle * Smooth)));
			SetGlowVisible(Alpha >= 0.35f);
			if (Alpha < 1.0f)
				return;
			State = EDreamPasswordChestState::AwaitingPickup;
			StateElapsed = 0.0f;
		}
		else if (State == EDreamPasswordChestState::AwaitingPickup)
		{
			if (!IsCollectorValid())
			{
				ReturnGlowToChest();
				return;
			}
			const float Delay = FMath::Max(0.0f, PickupDelay);
			const float Step = FMath::Min(RemainingTime, FMath::Max(0.0f, Delay - StateElapsed));
			StateElapsed += Step;
			RemainingTime -= Step;
			if (StateElapsed < Delay)
				return;
			StartAttraction(Collector.Get());
		}
		else if (State == EDreamPasswordChestState::Attracting)
		{
			if (!IsCollectorValid())
			{
				ReturnGlowToChest();
				return;
			}
			ADreamCharacter* Character = Collector.Get();
			const float Duration = FMath::Max(0.0f, AttractionDuration);
			StateElapsed += RemainingTime;
			const float Alpha = Duration > 0.0f ? FMath::Clamp(StateElapsed / Duration, 0.0f, 1.0f) : 1.0f;
			const float Smooth = Alpha * Alpha * (3.0f - 2.0f * Alpha);
			const FVector Target = Character->SceneMiniature ? Character->SceneMiniature->GetDisplayCenter() : Character->GetActorLocation();
			GlowRoot->SetWorldLocation(FMath::Lerp(AttractionStart, Target, Smooth) +
				GetActorUpVector() * FMath::Sin(PI * Alpha) * AttractionArcHeight);
			GlowRoot->SetRelativeScale3D(GlowHomeTransform.GetScale3D() * FMath::Lerp(1.0f, 0.25f, Alpha));
			if (Alpha < 1.0f)
				return;
			// 消费奖励与关闭 Tick 在角色通知之前完成，阻止 HUD/蓝图回调重入产生第二次领取。
			State = EDreamPasswordChestState::Collected;
			SetGlowVisible(false);
			SetActorTickEnabled(false);
			Collector.Reset();
			CollectorController.Reset();
			Character->AcquireMiniature();
			OnMiniatureCollected(Character);
			return;
		}
		else
			return;
	}
}

void ADreamPasswordChest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 输入会话尚未解锁时也需要精确通知所属控制器。只关闭引用本箱子的会话，不影响其它箱子。
	if (GetWorld())
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (ADreamPlayerController* Controller = Cast<ADreamPlayerController>(It->Get()))
				if (Controller->GetActivePasswordChest() == this)
					Controller->ClosePasswordEntry();
	Collector.Reset();
	CollectorController.Reset();
	Super::EndPlay(EndPlayReason);
}
