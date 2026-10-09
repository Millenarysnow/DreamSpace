#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "DreamCharacter.generated.h"

class UCameraComponent;
class UDreamSceneCapturePresentationComponent;
class UInputAction;
class USpringArmComponent;
class UStaticMeshComponent;
struct FInputActionValue;

/**
 * DreamSpace 使用的原生第三人称角色。
 *
 * 角色的移动、视角和跳跃输入沿用 UE5.8 官方 C++ 第三人称模板的职责划分；
 * 主相机使用 DreamShoulderCamera 提供近距离越肩和室内避障，身体仍朝移动方向转身。
 * 但这里把输入资源、Manny/Quinn 角色表现和项目自己的手办相机组件都放进
 * C++ 默认对象中，因此运行时不需要依赖角色蓝图或 GameMode 蓝图。
 */
UCLASS()
class DREAMSPACE_API ADreamCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ADreamCharacter();

	/** 由 Enhanced Input 触发的原生输入绑定。 */
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** 处理来自键盘、手柄或触摸界面的二维移动输入。 */
	UFUNCTION(BlueprintCallable, Category = "输入")
	virtual void DoMove(float Right, float Forward);

	/** 处理第三人称相机的水平/垂直视角输入。 */
	UFUNCTION(BlueprintCallable, Category = "输入")
	virtual void DoLook(float Yaw, float Pitch);

	/** 处理跳跃按下和松开，保留官方模板给触摸界面的调用入口。 */
	UFUNCTION(BlueprintCallable, Category = "输入")
	virtual void DoJumpStart();

	UFUNCTION(BlueprintCallable, Category = "输入")
	virtual void DoJumpEnd();

	/**
	 * 兼容旧测试入口：把二维输入转交给官方模板式移动逻辑。
	 * 新代码应优先调用 DoMove，保留该函数可以避免已有自动化测试失效。
	 */
	void MoveOnGravityPlane(const FVector2D& Input);

	/** 原生子对象是 DreamShoulderCamera，保留 SpringArm 类型接口供滚轮和旧资产读取。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "相机")
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** 玩家主视口相机。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "相机")
	TObjectPtr<UCameraComponent> FollowCamera;

	/** 玩家手中当前世界的场景缩略图表现组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "表现")
	TObjectPtr<UDreamSceneCapturePresentationComponent> SceneMiniature;

	/** 官方模板使用的跳跃输入动作，由 C++ 构造函数从 Content/Input 加载。 */
	UPROPERTY(EditDefaultsOnly, Category = "输入")
	TObjectPtr<UInputAction> JumpAction;

	/** 官方模板使用的移动输入动作。 */
	UPROPERTY(EditDefaultsOnly, Category = "输入")
	TObjectPtr<UInputAction> MoveAction;

	/** 手柄右摇杆等非鼠标视角输入。 */
	UPROPERTY(EditDefaultsOnly, Category = "输入")
	TObjectPtr<UInputAction> LookAction;

	/** 鼠标二维视角输入。 */
	UPROPERTY(EditDefaultsOnly, Category = "输入")
	TObjectPtr<UInputAction> MouseLookAction;

	/**
	 * 旧角色蓝图曾经保存过名为 Body 的占位圆柱组件。
	 * 现在角色显示由继承的 SkeletalMeshComponent 负责，但保留同名组件并隐藏，
	 * 让旧资产在重新加载时仍能安全解析，不会把圆柱叠加到新角色身上。
	 */
	UPROPERTY(VisibleAnywhere, Category = "兼容")
	TObjectPtr<UStaticMeshComponent> Body;

protected:
	/** Enhanced Input 的移动回调。 */
	void Move(const FInputActionValue& Value);

	/** Enhanced Input 的视角回调。 */
	void Look(const FInputActionValue& Value);

public:
	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};
