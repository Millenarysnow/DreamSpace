#pragma once
#include "GameFramework/Character.h"
#include "DreamCharacter.generated.h"
class UDreamSceneCapturePresentationComponent;
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
	UPROPERTY(VisibleAnywhere, Category = "相机")
	TObjectPtr<USpringArmComponent> CameraBoom;
	UPROPERTY(VisibleAnywhere, Category = "相机")
	TObjectPtr<UCameraComponent> FollowCamera;
	/**
	 * 玩家手中当前世界的场景缩略图表现。
	 * 这是独立的 SceneCapture/RenderTarget 组件，不参与任何交互逻辑。
	 */
	UPROPERTY(VisibleAnywhere, Category = "表现")
	TObjectPtr<UDreamSceneCapturePresentationComponent> SceneMiniature;
};
