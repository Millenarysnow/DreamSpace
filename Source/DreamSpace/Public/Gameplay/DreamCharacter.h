#pragma once
#include "GameFramework/Character.h"
#include "DreamCharacter.generated.h"
class UDreamOccupantComponent;
class USpringArmComponent;
class UCameraComponent;
/** 原生第三人称角色；移动轴来自相机在当前重力平面上的投影。 */
UCLASS()
class DREAMSPACE_API ADreamCharacter : public ACharacter
{
	GENERATED_BODY()
public:
	ADreamCharacter();
	void MoveOnGravityPlane(const FVector2D& Input);
	UPROPERTY(VisibleAnywhere, Category = "交互")
	TObjectPtr<UDreamOccupantComponent> Occupant;
	UPROPERTY(VisibleAnywhere, Category = "相机")
	TObjectPtr<USpringArmComponent> CameraBoom;
	UPROPERTY(VisibleAnywhere, Category = "相机")
	TObjectPtr<UCameraComponent> FollowCamera;
};
