#include "DreamPlayerController.h"
#include "DreamCharacter.h"
#include "DreamSceneCapturePresentationComponent.h"
#include "DreamInteractableInterface.h"
#include "DreamSpace.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "Components/ActorComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/HitResult.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "UObject/ConstructorHelpers.h"

// 调试开关只影响点击时的可视化，不参与拾取或玩法。捕获组件已把调试线从 RT 中剔除。
static bool bDreamMiniatureInteractionDebugDraw = false;
static FAutoConsoleVariableRef CVarDreamMiniatureInteractionDebugDraw(
	TEXT("dream.DebugMiniatureInteraction"), bDreamMiniatureInteractionDebugDraw,
	TEXT("显示手办点击的玩家射线、捕获射线和命中点。0=关闭，1=开启。"), ECVF_Default);

ADreamPlayerController::ADreamPlayerController()
{
	// 直接加载官方模板的两个映射上下文：IMC_Default 提供移动、跳跃和手柄视角，
	// IMC_MouseLook 提供鼠标二维视角。映射资产不是蓝图，运行时由原生控制器管理。
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> DefaultContext(
		TEXT("/Game/Input/IMC_Default.IMC_Default"));
	if (DefaultContext.Succeeded())
	{
		DefaultMappingContexts.Add(DefaultContext.Object);
	}
	else
	{
		UE_LOG(LogDreamSpace, Error, TEXT("无法加载官方第三人称输入映射 IMC_Default。"));
	}

	static ConstructorHelpers::FObjectFinder<UInputMappingContext> MouseLookContext(
		TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook"));
	if (MouseLookContext.Succeeded())
	{
		DefaultMappingContexts.Add(MouseLookContext.Object);
	}
	else
	{
		UE_LOG(LogDreamSpace, Error, TEXT("无法加载官方第三人称鼠标输入映射 IMC_MouseLook。"));
	}
}

void ADreamPlayerController::BeginPlay()
{
	Super::BeginPlay();
	// BeginPlay 通常已经有 LocalPlayer；如果初始化顺序不同，ReceivedPlayer
	// 也会再次尝试，确保 Enhanced Input 映射不会因为过早检查而漏装。
	ApplyInputMapping();
}

void ADreamPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();
	ApplyInputMapping();
}

void ADreamPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (bMiniatureInteractionMode && IsLocalController())
		SetMiniatureInteractionMode(false);

	// 退出时移除项目交互和官方模板映射，避免 PIE 的下一次会话继承旧上下文。
	if (GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Input =
			GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (bMappingApplied && Mapping)
			{
				Input->RemoveMappingContext(Mapping);
			}
			for (UInputMappingContext* Context : AppliedDefaultMappingContexts)
			{
				if (Context)
				{
					Input->RemoveMappingContext(Context);
				}
			}
		}
	}
	bMappingApplied = false;
	bDefaultMappingsApplied = false;
	AppliedDefaultMappingContexts.Reset();
	Super::EndPlay(Reason);
}

void ADreamPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	auto* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		UE_LOG(LogDreamSpace, Error,
			TEXT("玩家控制器输入组件不是 UEnhancedInputComponent，无法安装运行时输入映射。实际类型：%s"),
			*GetNameSafe(InputComponent));
		return;
	}
	if (!Mapping)
	{
		Mapping = NewObject<UInputMappingContext>(this, TEXT("DreamRuntimeInteractionMapping"));

		// 辅助 lambda：创建一个瞬时按钮动作并只绑定项目交互，不重复绑定官方模板输入。
		auto Button = [&](FKey Key, auto Method)
		{
			auto* Action = NewObject<UInputAction>(this,
				MakeUniqueObjectName(this, UInputAction::StaticClass(),
					FName(*FString::Printf(TEXT("DreamAction_%s"), *Key.ToString()))));
			Action->ValueType = EInputActionValueType::Boolean;
			Actions.Add(Action);
			Mapping->MapKey(Action, Key);
			Input->BindAction(Action, ETriggerEvent::Started, this, Method);
		};
		Button(EKeys::E, &ADreamPlayerController::Interact);
		Button(EKeys::Tab, &ADreamPlayerController::ToggleMiniatureInteractionMode);
		Button(EKeys::LeftMouseButton, &ADreamPlayerController::InteractWithMiniature);

		// 滚轮缩放不属于官方模板输入，因此保留一个本地 Axis1D 动作。
		auto* ZoomAction = NewObject<UInputAction>(this);
		Actions.Add(ZoomAction);
		ZoomAction->ValueType = EInputActionValueType::Axis1D;
		Mapping->MapKey(ZoomAction, EKeys::MouseWheelAxis);
		Input->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &ADreamPlayerController::ZoomCamera);
	}

	ApplyInputMapping();
}

void ADreamPlayerController::ApplyInputMapping()
{
	if (!IsLocalController() || !GetLocalPlayer())
		return;

	if (UEnhancedInputLocalPlayerSubsystem* Input =
		GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		if (!bDefaultMappingsApplied)
		{
			for (UInputMappingContext* Context : DefaultMappingContexts)
			{
				if (Context)
				{
					Input->AddMappingContext(Context, 0);
					AppliedDefaultMappingContexts.Add(Context);
				}
			}
			bDefaultMappingsApplied = true;
			UE_LOG(LogDreamSpace, Verbose, TEXT("已安装官方第三人称输入映射上下文。"));
		}

		if (!bMappingApplied && Mapping)
		{
			Input->AddMappingContext(Mapping, 1);
			bMappingApplied = true;
			UE_LOG(LogDreamSpace, Verbose,
				TEXT("已安装 DreamSpace 交互输入映射（E、Tab、手办左键和滚轮）。"));
		}
	}
}

void ADreamPlayerController::UpdateRotation(float Delta)
{
	// 在自定义重力场景中，把控制旋转转换到重力相对空间再叠加输入，保证相机姿态始终贴合当前重力方向。
	auto* ControlledCharacter = Cast<ADreamCharacter>(GetPawn());
	if (!ControlledCharacter)
		return;
	const FVector Down = ControlledCharacter->GetCharacterMovement()->GetGravityDirection();
	const FQuat ToWorld = FQuat::FindBetweenNormals(FVector::DownVector, Down);
	FRotator Local = (ToWorld.Inverse() * GetControlRotation().Quaternion()).Rotator();
	Local.Yaw += RotationInput.Yaw;
	Local.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Local.Pitch + RotationInput.Pitch), -80.0, 80.0);
	Local.Roll = 0;
	SetControlRotation((ToWorld * Local.Quaternion()).Rotator());
}
void ADreamPlayerController::ZoomCamera(const FInputActionValue& Value)
{
	const float WheelDelta = Value.Get<float>();
	if (FMath::IsNearlyZero(WheelDelta))
		return;

	auto* ControlledCharacter = Cast<ADreamCharacter>(GetPawn());
	if (!ControlledCharacter)
		return;
	USpringArmComponent* SpringArm = ControlledCharacter->FindComponentByClass<USpringArmComponent>();
	if (!SpringArm)
		return;

	// 向上滚动（WheelDelta > 0）拉近相机，向下滚动拉远。
	// CameraZoomStep 为负时可以反转方向。
	const float NewLength = FMath::Clamp(
		SpringArm->TargetArmLength - WheelDelta * CameraZoomStep,
		MinCameraArmLength, MaxCameraArmLength);
	SpringArm->TargetArmLength = NewLength;
}
void ADreamPlayerController::Interact()
{
	// 手办模式下 E 不应越过手办去触发主视口中心的世界物体。
	if (bMiniatureInteractionMode)
		return;
	// 从相机中心向前做射线检测，命中后调用该 Actor 上所有实现了可交互接口的组件。
	// 控制器只做“触发”，具体行为（转动、开关门等）完全由组件自身决定。
	FVector Origin;
	FRotator Rotation;
	GetPlayerViewPoint(Origin, Rotation);
	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(GetPawn());
	const FVector TraceEnd = Origin + Rotation.Vector() * InteractTraceDistance;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, Origin, TraceEnd, InteractTraceChannel, Params);

	// 每次按 E 都把本次实际射线短暂留在世界中：绿色表示命中，红色表示未命中。
	// 使用有限显示时长，既能在按键后观察射线，也不会让旧调试线永久堆积在场景里。
	const FColor TraceColor = bHit ? FColor::Green : FColor::Red;
	DrawDebugLine(GetWorld(), Origin, bHit ? Hit.ImpactPoint : TraceEnd, TraceColor,
		false, 2.5f, 0, 2.0f);
	if (bHit)
	{
		// 球标记真实碰撞点，法线箭头显示命中表面的朝向，方便判断射线是否打在预期碰撞体上。
		DrawDebugSphere(GetWorld(), Hit.ImpactPoint, 8.0f, 12, FColor::Yellow,
			false, 2.5f, 0, 2.0f);
		DrawDebugDirectionalArrow(GetWorld(), Hit.ImpactPoint,
			Hit.ImpactPoint + Hit.ImpactNormal * 35.0f, 8.0f, FColor::Cyan,
			false, 2.5f, 0, 1.5f);
	}

	if (!bHit)
	{
		UE_LOG(LogDreamSpace, Verbose,
			TEXT("E 交互未命中任何对象。通道=%s，距离=%.1f，起点=%s，终点=%s。请确认目标 PrimitiveComponent 阻挡该通道。"),
			*UEnum::GetValueAsString(InteractTraceChannel.GetValue()), InteractTraceDistance,
			*Origin.ToString(), *TraceEnd.ToString());
		return;
	}

	DispatchInteraction(Hit.GetActor(), Hit.GetComponent());
}

void ADreamPlayerController::ToggleMiniatureInteractionMode()
{
	SetMiniatureInteractionMode(!bMiniatureInteractionMode);
}

void ADreamPlayerController::SetMiniatureInteractionMode(bool bEnabled)
{
	if (bMiniatureInteractionMode == bEnabled || !IsLocalController())
		return;
	bMiniatureInteractionMode = bEnabled;
	bShowMouseCursor = bEnabled;
	if (bEnabled)
	{
		// GameAndUI 仍把 Enhanced Input 的 Tab/左键交给控制器，同时允许鼠标
		// 自由移动到手办画面任意位置；没有聚焦 Widget，故无需依赖 UMG。
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);
	}
	else
	{
		// 恢复原来的第三人称鼠标捕获方式；Look 的保护条件随状态一起解除。
		SetInputMode(FInputModeGameOnly());
	}
	UE_LOG(LogDreamSpace, Log, TEXT("手办交互模式：%s"), bEnabled ? TEXT("开启") : TEXT("关闭"));
}

bool ADreamPlayerController::GetMiniatureClickDebug(
	FVector2D& OutPosition, FString& OutMessage, FLinearColor& OutColor) const
{
	if (!bDreamMiniatureInteractionDebugDraw || !GetWorld() ||
		MiniatureDebugMessage.IsEmpty() || GetWorld()->GetRealTimeSeconds() > MiniatureDebugUntil)
		return false;
	OutPosition = MiniatureDebugPosition;
	OutMessage = MiniatureDebugMessage;
	OutColor = FLinearColor(MiniatureDebugColor);
	return true;
}

void ADreamPlayerController::ReportMiniatureClick(const FString& Message, const FColor& Color)
{
	if (!bDreamMiniatureInteractionDebugDraw)
		return;
	float MouseX = 0.0f, MouseY = 0.0f;
	GetMousePosition(MouseX, MouseY);
	MiniatureDebugPosition = FVector2D(MouseX, MouseY);
	MiniatureDebugMessage = Message;
	MiniatureDebugColor = Color;
	MiniatureDebugUntil = GetWorld()->GetRealTimeSeconds() + 6.0;
	UE_LOG(LogDreamSpace, Log, TEXT("[手办点击] %s"), *Message);
}

void ADreamPlayerController::InteractWithMiniature()
{
	if (!bMiniatureInteractionMode || !GetWorld())
		return;

	// 实际相机可能因 SpringArm 碰撞或滚轮缩放偏离捕获使用的理想观察位置。
	// 鼠标视线只用来求面片交点；随后按 RT 投影重新计算捕获相机的射线。
	FVector ViewRayOrigin, ViewRayDirection;
	if (!DeprojectMousePositionToWorld(ViewRayOrigin, ViewRayDirection))
	{
		ReportMiniatureClick(TEXT("无法取得鼠标视线，请将光标移入游戏视口"), FColor::Red);
		return;
	}
	InteractWithMiniatureRay(ViewRayOrigin, ViewRayDirection);
}

void ADreamPlayerController::InteractWithMiniatureRay(const FVector& ViewRayOrigin, const FVector& ViewRayDirection)
{
	const ADreamCharacter* ControlledCharacter = Cast<ADreamCharacter>(GetPawn());
	const UDreamSceneCapturePresentationComponent* Miniature =
		ControlledCharacter ? ControlledCharacter->SceneMiniature.Get() : nullptr;
	if (!Miniature)
	{
		ReportMiniatureClick(TEXT("当前角色没有手办表现组件"), FColor::Red);
		return;
	}

	FVector DisplayHit, CaptureOrigin, CaptureDirection;
	FString FailureReason;
	if (!Miniature->TryMapViewRayToCaptureRay(ViewRayOrigin, ViewRayDirection,
		DisplayHit, CaptureOrigin, CaptureDirection, FailureReason))
	{
		// 失败也绘制尝试射线，不能把全部调试放到映射成功之后。
		if (bDreamMiniatureInteractionDebugDraw)
			DrawDebugLine(GetWorld(), ViewRayOrigin, ViewRayOrigin + ViewRayDirection * 1000.0,
				FColor::Red, false, 6.0f, 1, 2.0f);
		ReportMiniatureClick(FailureReason, FColor::Red);
		return;
	}
	if (bDreamMiniatureInteractionDebugDraw)
	{
		DrawDebugLine(GetWorld(), ViewRayOrigin, DisplayHit, FColor::Cyan, false, 6.0f, 1, 2.0f);
		DrawDebugSphere(GetWorld(), DisplayHit, 3.0f, 12, FColor::Cyan, false, 6.0f, 1, 1.5f);
	}

	// 显示网格无碰撞，仍需防止穿过真实墙体点击它；忽略持有手办的角色自身。
	FCollisionQueryParams ViewParams(SCENE_QUERY_STAT(DreamMiniatureViewOcclusion), true);
	ViewParams.AddIgnoredActor(GetPawn());
	FHitResult ViewBlocker;
	const FVector SafeDisplayEnd = DisplayHit - ViewRayDirection.GetSafeNormal();
	if (GetWorld()->LineTraceSingleByChannel(
		ViewBlocker, ViewRayOrigin, SafeDisplayEnd, InteractTraceChannel, ViewParams))
	{
		if (bDreamMiniatureInteractionDebugDraw)
			DrawDebugLine(GetWorld(), ViewRayOrigin, ViewBlocker.ImpactPoint,
				FColor::Orange, false, 6.0f, 1, 2.0f);
		ReportMiniatureClick(FString::Printf(TEXT("显示面被近处物体遮挡：%s"),
			*GetNameSafe(ViewBlocker.GetActor())), FColor::Orange);
		return;
	}

	// 从 Capture 光心重新做真实世界检测。它距离建筑通常有上万厘米，
	// 因此不能复用普通 E 交互的 600 cm 距离；捕获黑名单同时参与碰撞过滤。
	FCollisionQueryParams CaptureParams(SCENE_QUERY_STAT(DreamMiniatureCapturePick), true);
	CaptureParams.AddIgnoredActor(GetPawn());
	TArray<AActor*> CaptureHiddenActors;
	Miniature->GetCaptureHiddenActors(CaptureHiddenActors);
	for (AActor* HiddenActor : CaptureHiddenActors)
		CaptureParams.AddIgnoredActor(HiddenActor);
	const FVector CaptureEnd = CaptureOrigin + CaptureDirection * MiniatureInteractTraceDistance;
	FHitResult CaptureHit;
	const bool bCapturedHit = GetWorld()->LineTraceSingleByChannel(
		CaptureHit, CaptureOrigin, CaptureEnd, InteractTraceChannel, CaptureParams);
	if (bDreamMiniatureInteractionDebugDraw)
	{
		DrawDebugLine(GetWorld(), CaptureOrigin, bCapturedHit ? CaptureHit.ImpactPoint : CaptureEnd,
			bCapturedHit ? FColor::Green : FColor::Red, false, 6.0f, 0, 2.0f);
		if (bCapturedHit)
			DrawDebugSphere(GetWorld(), CaptureHit.ImpactPoint, 12.0f, 12, FColor::Yellow, false, 6.0f);
	}

	AActor* HitActor = bCapturedHit ? CaptureHit.GetActor() : nullptr;
	UActorComponent* HitComponent = bCapturedHit ? CaptureHit.GetComponent() : nullptr;
	if (!HitActor)
	{
		ReportMiniatureClick(TEXT("已映射；捕获射线未命中，请检查目标碰撞、通道和检测距离"), FColor::Red);
		return;
	}
	// 首个非交互物体仍是遮挡物，不能穿过去寻找后方机关。
	const bool bHitComponentInteractable = HitComponent &&
		HitComponent->GetClass()->ImplementsInterface(UDreamInteractableInterface::StaticClass());
	if (!bHitComponentInteractable &&
		HitActor->GetComponentsByInterface(UDreamInteractableInterface::StaticClass()).IsEmpty())
	{
		ReportMiniatureClick(FString::Printf(TEXT("已命中 %s / %s，但该 Actor 没有交互组件"),
			*GetNameSafe(HitActor), *GetNameSafe(HitComponent)), FColor::Yellow);
		return;
	}
	DispatchInteraction(HitActor, HitComponent);
	// 这里只能确认已分发；组件是否正在运动或缺少枢轴，应由组件自身日志解释。
	ReportMiniatureClick(FString::Printf(TEXT("已向 %s / %s 发送交互"),
		*GetNameSafe(HitActor), *GetNameSafe(HitComponent)), FColor::Green);
}

void ADreamPlayerController::DispatchInteraction(AActor* HitActor, UActorComponent* HitComponent)
{
	if (!HitActor && HitComponent)
		HitActor = HitComponent->GetOwner();
	if (!HitActor && !HitComponent)
		return;

	TArray<UActorComponent*> Interactables;
	if (HitActor)
		Interactables = HitActor->GetComponentsByInterface(UDreamInteractableInterface::StaticClass());

	// 如果射线直接命中一个实现接口的 PrimitiveComponent，也支持它自身作为
	// 交互目标；随后再触发所属 Actor 的其他交互组件，并避免重复调用。
	TSet<UActorComponent*> Dispatched;
	if (HitComponent && HitComponent->GetClass()->ImplementsInterface(UDreamInteractableInterface::StaticClass()))
	{
		IDreamInteractableInterface::Execute_OnInteracted(HitComponent, GetPawn());
		Dispatched.Add(HitComponent);
	}
	for (UActorComponent* Component : Interactables)
	{
		if (Component && !Dispatched.Contains(Component))
		{
			IDreamInteractableInterface::Execute_OnInteracted(Component, GetPawn());
			Dispatched.Add(Component);
		}
	}

	UE_LOG(LogDreamSpace, Verbose,
		TEXT("交互命中 Actor=%s Component=%s，可交互组件=%d。"),
		*GetNameSafe(HitActor), *GetNameSafe(HitComponent), Dispatched.Num());
	if (Dispatched.IsEmpty())
		UE_LOG(LogDreamSpace, Warning,
			TEXT("交互命中了 Actor [%s]，但其上没有实现 IDreamInteractableInterface 的组件。"),
			*GetNameSafe(HitActor));
}
