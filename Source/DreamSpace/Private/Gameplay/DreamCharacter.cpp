#include "DreamCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DreamPlayerController.h"
#include "DreamSceneCapturePresentationComponent.h"
#include "DreamShoulderCameraComponent.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ADreamCharacter::ADreamCharacter()
{
	// 胶囊尺寸与 UE5.8 官方第三人称模板一致，保证角色落地、台阶和导航行为一致。
	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);

	// 控制器只负责改变相机朝向，角色本体由移动方向自动转向。
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// 使用官方模板的移动参数。UE5.8 的 CharacterMovementComponent 已经支持
	// 自定义重力方向，DoMove 会把输入投影到当前重力平面，移动组件会沿该平面转向。
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
	Movement->JumpZVelocity = 500.0f;
	Movement->AirControl = 0.35f;
	Movement->MaxWalkSpeed = 500.0f;
	Movement->MinAnalogWalkSpeed = 20.0f;
	Movement->BrakingDecelerationWalking = 2000.0f;
	Movement->BrakingDecelerationFalling = 1500.0f;

	// 角色网格直接在 C++ 中加载，去掉对 BP_ThirdPersonCharacter 的依赖。
	// 资产路径与 UE5.8 官方第三人称 C++ 示例一致；当前工程已包含这些基础资产。
	if (USkeletalMeshComponent* CharacterMesh = GetMesh())
	{
		CharacterMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -89.0f));
		CharacterMesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));

		static ConstructorHelpers::FObjectFinder<USkeletalMesh> QuinnMesh(
			TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
		if (QuinnMesh.Succeeded())
		{
			CharacterMesh->SetSkeletalMesh(QuinnMesh.Object);
			// 使用独立副本，不修改共享的模板网格/材质。两槽保留 Quinn 的原贴图和颜色继承链。
			// 相机只为支持 DreamOwnerClip 的材质建立本角色 MID；默认 Amount=0，正常距离完全可见。
			static ConstructorHelpers::FObjectFinder<UMaterialInterface> ClipBody(
				TEXT("/Game/DreamCamera/Materials/MI_QuinnOwnerClip_01.MI_QuinnOwnerClip_01"));
			static ConstructorHelpers::FObjectFinder<UMaterialInterface> ClipDetails(
				TEXT("/Game/DreamCamera/Materials/MI_QuinnOwnerClip_02.MI_QuinnOwnerClip_02"));
			if (ClipBody.Succeeded() && ClipDetails.Succeeded())
			{
				CharacterMesh->SetMaterial(0, ClipBody.Object);
				CharacterMesh->SetMaterial(1, ClipDetails.Object);
			}
			else
				UE_LOG(LogTemp, Error, TEXT("缺少局部剔除材质，请运行 Documents/Tools/GenerateOwnerClipMaterials.py；角色保持原材质可见。"));
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("无法加载第三人称角色网格 SKM_Quinn_Simple，角色将使用空网格运行。"));
		}

		static ConstructorHelpers::FClassFinder<UAnimInstance> QuinnAnim(
			TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
		if (QuinnAnim.Succeeded())
		{
			CharacterMesh->SetAnimInstanceClass(QuinnAnim.Class);
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("无法加载第三人称动画蓝图 ABP_Unarmed，角色将保持默认动画模式。"));
		}
	}

	// 越肩相机保持原有 CameraBoom 子对象名称和 SpringArm 接口，滚轮和旧资产仍能找到它。
	// 理想距离 210 cm、右肩偏移 45 cm；人物按移动方向转身，不强制朝相机方向横移/倒退。
	// 枢轴挂胶囊而非动画骨骼，默认沿角色局部向上 60 cm，重力翻转时也随胶囊一起转动。
	CameraBoom = CreateDefaultSubobject<UDreamShoulderCameraComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->bConstrainAspectRatio = false;
	// 第一版固定水平 FOV，避免室内避障同时改变距离和视场角而产生额外的缩放感。
	FollowCamera->FieldOfView = 80.0f;
	// 只有主跟随视角携带局部剔除标记。材质还核对实际镜头位置/朝向，隔离分屏和其他角色。
	// UserFlags 第 6 位由本功能保留；其他后处理功能可继续使用其余位。
	FollowCamera->PostProcessSettings.bOverride_UserFlags = true;
	FollowCamera->PostProcessSettings.UserFlags |= UDreamShoulderCameraComponent::OwnerClipViewFlag;

	// 手办显示面仍然挂在角色胶囊体右前方，位置、180 度显示朝向和相机映射
	// 与原有实现保持一致。该组件的 SceneCapture 不参与角色移动输入。
	SceneMiniature = CreateDefaultSubobject<UDreamSceneCapturePresentationComponent>(TEXT("SceneMiniature"));
	SceneMiniature->SetupAttachment(GetCapsuleComponent());
	SceneMiniature->SetRelativeLocation(FVector(75.0f, 55.0f, 35.0f));
	SceneMiniature->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
	// 旧 BP_DreamCharacter 为手办画面使用非方形高分辨率 RenderTarget。
	// 这里显式迁移这两个值，避免切换到原生角色后画质、RT 长宽比和材质采样结果变化。
	SceneMiniature->RenderTargetWidth = 2200;
	SceneMiniature->RenderTargetHeight = 2500;

	// 兼容旧的 BP_DreamCharacter：保留 Body 这个原生子对象名称，但隐藏旧圆柱，
	// 避免旧蓝图重新加载时丢失组件，同时确保新角色只显示 Quinn 骨骼网格。
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(GetCapsuleComponent());
	Body->SetVisibility(false);
	Body->SetHiddenInGame(true);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> LegacyCylinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (LegacyCylinder.Succeeded())
	{
		Body->SetStaticMesh(LegacyCylinder.Object);
	}

	// 角色自身持有输入动作引用；PlayerController 只负责把映射上下文装到本地玩家。
	static ConstructorHelpers::FObjectFinder<UInputAction> JumpInput(
		TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"));
	static ConstructorHelpers::FObjectFinder<UInputAction> MoveInput(
		TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
	static ConstructorHelpers::FObjectFinder<UInputAction> LookInput(
		TEXT("/Game/Input/Actions/IA_Look.IA_Look"));
	static ConstructorHelpers::FObjectFinder<UInputAction> MouseLookInput(
		TEXT("/Game/Input/Actions/IA_MouseLook.IA_MouseLook"));
	JumpAction = JumpInput.Object;
	MoveAction = MoveInput.Object;
	LookAction = LookInput.Object;
	MouseLookAction = MouseLookInput.Object;
}

void ADreamCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogTemp, Error,
			TEXT("DreamCharacter 需要 UEnhancedInputComponent，无法安装官方第三人称输入。"));
		return;
	}

	// 绑定前先检查动作资源，避免资源缺失时传入空指针导致难以定位的输入异常。
	if (JumpAction)
	{
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &ADreamCharacter::DoJumpStart);
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Completed, this, &ADreamCharacter::DoJumpEnd);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("DreamCharacter 缺少 IA_Jump 输入动作。"));
	}

	if (MoveAction)
	{
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ADreamCharacter::Move);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("DreamCharacter 缺少 IA_Move 输入动作。"));
	}

	if (LookAction)
	{
		EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &ADreamCharacter::Look);
	}
	if (MouseLookAction)
	{
		EnhancedInput->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ADreamCharacter::Look);
	}
	if (!LookAction && !MouseLookAction)
	{
		UE_LOG(LogTemp, Error, TEXT("DreamCharacter 缺少 IA_Look 和 IA_MouseLook 输入动作。"));
	}
}

void ADreamCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();
	DoMove(MovementVector.X, MovementVector.Y);
}

void ADreamCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxis = Value.Get<FVector2D>();
	DoLook(LookAxis.X, LookAxis.Y);
}

void ADreamCharacter::DoMove(float Right, float Forward)
{
	if (!Controller)
	{
		return;
	}

	// 以当前重力反方向作为“地面向上”，把控制器前方向投影到可行走平面。
	// 这样官方模板的 WASD/手柄输入也能继续支持项目已有的自定义重力。
	const FVector Up = -GetCharacterMovement()->GetGravityDirection();
	FVector ForwardDirection = FVector::VectorPlaneProject(
		Controller->GetControlRotation().Vector(), Up).GetSafeNormal();
	if (ForwardDirection.IsNearlyZero())
	{
		ForwardDirection = FVector::VectorPlaneProject(GetActorForwardVector(), Up).GetSafeNormal();
	}
	if (ForwardDirection.IsNearlyZero())
	{
		ForwardDirection = FVector::ForwardVector;
	}

	const FVector RightDirection = Up.Cross(ForwardDirection).GetSafeNormal();
	AddMovementInput(ForwardDirection, Forward);
	AddMovementInput(RightDirection, Right);
}

void ADreamCharacter::DoLook(float Yaw, float Pitch)
{
	// 手办交互模式下鼠标负责在显示面上选点，不能同时改变第三人称相机。
	if (const ADreamPlayerController* DreamController = Cast<ADreamPlayerController>(Controller))
	{
		if (DreamController->IsMiniatureInteractionMode())
		{
			return;
		}
	}

	if (Controller)
	{
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void ADreamCharacter::DoJumpStart()
{
	Jump();
}

void ADreamCharacter::DoJumpEnd()
{
	StopJumping();
}

void ADreamCharacter::MoveOnGravityPlane(const FVector2D& Input)
{
	DoMove(Input.X, Input.Y);
}
