#include "DreamCharacter.h"
#include "DreamOccupantComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "UObject/ConstructorHelpers.h"
ADreamCharacter::ADreamCharacter()
{
	GetCapsuleComponent()->InitCapsuleSize(34, 88);
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->MaxWalkSpeed = 420;
	GetCharacterMovement()->JumpZVelocity = 450;
	Occupant = CreateDefaultSubobject<UDreamOccupantComponent>(TEXT("Occupant"));
	// 第一版单机玩家使用固定身份，跨地图重载后仍可从存档恢复同一持有者。
	Occupant->OccupantId = FGuid(0xD5A, 0, 0, 1);
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	CameraBoom->TargetArmLength = 420;
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->SetRelativeLocation(FVector(0, 0, 60));
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bConstrainAspectRatio = false;
	// 开发角色使用引擎基础模型，不依赖项目中的角色蓝图或动画蓝图。
	auto* Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(GetCapsuleComponent());
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
		Body->SetStaticMesh(Cylinder.Object);
	Body->SetRelativeScale3D(FVector(0.5, 0.5, 1.4));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void ADreamCharacter::MoveOnGravityPlane(const FVector2D& Input)
{
	if (!Controller || Input.IsNearlyZero())
		return;
	const FVector Up = -GetCharacterMovement()->GetGravityDirection();
	FVector Forward = FVector::VectorPlaneProject(Controller->GetControlRotation().Vector(), Up).GetSafeNormal();
	if (Forward.IsNearlyZero())
		Forward = GetActorForwardVector();
	const FVector Right = Up.Cross(Forward).GetSafeNormal();
	AddMovementInput(Forward, Input.Y);
	AddMovementInput(Right, Input.X);
	const FVector Facing = (Forward * Input.Y + Right * Input.X).GetSafeNormal();
	SetActorRotation(FRotationMatrix::MakeFromXZ(Facing, Up).ToQuat());
}
