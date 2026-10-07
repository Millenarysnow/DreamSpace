#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpringArmComponent.h"
#include "DreamShoulderCameraComponent.generated.h"

class USkeletalMeshComponent;

/**
 * 探索模式的越肩相机：保留 SpringArm 的挂接接口，独立处理室内避障的时间连续性。
 *
 * TargetArmLength / SocketOffset 始终表示玩家要求的理想构图，碰撞不能反写它们。
 * 每帧先计算稳定的观察枢轴，再收窄肩位、查询安全距离，最后发布 SpringEndpoint。
 * 旋转直接采用控制器朝向，因此人物仍可按移动方向转身，鼠标观察也不会被阻尼拖慢。
 */
UCLASS(ClassGroup = Camera, meta = (BlueprintSpawnableComponent), HideCategories = (Lag))
class DREAMSPACE_API UDreamShoulderCameraComponent : public USpringArmComponent
{
	GENERATED_BODY()

public:
	UDreamShoulderCameraComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnUnregister() override;
	virtual void Deactivate() override;
	virtual void ApplyWorldOffset(const FVector& InOffset, bool bWorldShift) override;

	/**
	 * 返回未经过避障的观察姿态，供手办取景复用同一套枢轴、旋转和肩位坐标约定。
	 * InArmLength 是调用方选择的观察距离；手办可使用固定距离，也可使用平滑缩放距离。
	 * 不包含碰撞收近或肩位收窄，避免路过门框时手办中的建筑也跟着晃动。
	 */
	FTransform GetIdealCameraTransform(float InArmLength) const;

	/** 缩放后的理想臂长，尚未经过碰撞收近；与直接跳变的 TargetArmLength 区分。 */
	float GetSmoothedArmLength() const { return SmoothedArmLength; }

	/** 当前肩位比例，1 表示完整右肩，0 表示回到中轴；供调试与运行时观察使用。 */
	float GetShoulderWeight() const { return ShoulderWeight; }

	/** 传送或主动切镜头时清除历史，下一帧直接建立新姿态，不沿旧位置缓慢追赶。 */
	UFUNCTION(BlueprintCallable, Category = "相机|越肩")
	void ResetCameraState();

	/** 半衰期单位为秒：经过该时长后，当前值到目标值的误差减半；0 表示立即到达。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机|跟随", meta = (ClampMin = "0.0", UIMax = "0.3"))
	float HeightFollowHalfLife = 0.06f;

	/** 只平滑重力方向上的起伏；水平移动立即跟随。限制高度滞后，防止跳跃时人物离开构图。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机|跟随", meta = (ClampMin = "0.0", UIMax = "50.0"))
	float MaxHeightLag = 18.0f;

	/** 枢轴单帧位移超过此值视为传送，自动清除历史；普通机关转动仍保持重力相对跟随。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机|跟随", meta = (ClampMin = "1.0"))
	float TeleportResetDistance = 300.0f;

	/** 滚轮仅修改 TargetArmLength，本组件按此半衰期追踪它，连续滚动不会反复重启动画。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机|缩放", meta = (ClampMin = "0.0", UIMax = "0.3"))
	float ZoomHalfLife = 0.08f;

	/** 比真正的相机碰撞球多留出的预警空间；提前收近可以减少必须立即夹紧的次数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机|避障", meta = (ClampMin = "0.0", UIMax = "40.0"))
	float AnticipationPadding = 12.0f;

	/** 距离实际碰撞点的额外间隙，避免相机长期贴着表面产生数值误差。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机|避障", meta = (ClampMin = "0.0", UIMax = "10.0"))
	float CollisionSafetyMargin = 2.0f;

	/** 预警检测要求收近时使用快速阻尼；真正的安全距离仍是不可突破的硬约束。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机|避障", meta = (ClampMin = "0.0", UIMax = "0.2"))
	float RetractionHalfLife = 0.045f;

	/** 距离恢复比收近慢；约四至五个半衰期后基本回到理想构图。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机|避障", meta = (ClampMin = "0.0", UIMax = "0.5"))
	float RecoveryHalfLife = 0.16f;

	/** 障碍消失或可用空间开始增加后先短暂停留，过滤门框边缘反复命中/未命中的跳变。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机|避障", meta = (ClampMin = "0.0", UIMax = "0.5"))
	float RecoveryDelay = 0.2f;

	/** 中轴比完整肩位多出的可用后退距离达到此值时，目标肩位完全收回中轴。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机|肩位", meta = (ClampMin = "1.0", UIMax = "150.0"))
	float ShoulderNarrowingDistance = 60.0f;

	/** 收窄肩位较快，但仍对中间姿态重新做真实碰撞检测，不假定插值路径一定安全。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机|肩位", meta = (ClampMin = "0.0", UIMax = "0.3"))
	float ShoulderRetractionHalfLife = 0.055f;

	/** 离开侧向阻挡后，肩位在恢复等待结束后按此半衰期回到右肩。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机|肩位", meta = (ClampMin = "0.0", UIMax = "0.5"))
	float ShoulderRecoveryHalfLife = 0.18f;

	/** 仅隐藏持有者视口中的人物网格，碰撞、动画和其他玩家看到的角色不受影响。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机|人物遮挡")
	bool bHideOwnerWhenTooClose = true;

	/** 镜头到胶囊中心小于此距离时隐藏人物；默认不要求 Quinn 材质支持透明度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机|人物遮挡", meta = (ClampMin = "0.0"))
	float OwnerHideDistance = 90.0f;

	/** 恢复距离大于隐藏距离，避免人物在阈值附近闪烁；运行时也会强制这两个值有间隔。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机|人物遮挡", meta = (ClampMin = "0.0"))
	float OwnerShowDistance = 115.0f;

protected:
	/** 替代默认 SpringArm 的即时碰撞回弹，仍输出原有的 SpringEndpoint Socket。 */
	virtual void UpdateDesiredArmLocation(bool bDoTrace, bool bDoLocationLag, bool bDoRotationLag, float DeltaTime) override;

private:
	/** 一次真实球扫掠的结果，同时保留命中信息，供预警规则与调试显示复用。 */
	struct FCameraSweep
	{
		float SafeDistance = 0.0f;
		FHitResult Hit;
	};

	FCameraSweep SweepCamera(const FVector& Start, const FVector& End, float Radius) const;
	FVector GetGravityUp() const;
	void UpdateOwnerVisibility(const FVector& CameraLocation);
	void RestoreOwnerVisibility();
	void DrawCameraDebug(const FVector& Pivot, const FVector& Ideal, const FVector& Actual, const FCameraSweep& ActualSweep) const;
	static float Damp(float Current, float Target, float HalfLife, float DeltaTime);

	bool bHasCameraState = false;
	bool bRecoveringDistance = false;
	bool bDistanceSpaceIncreasing = false;
	bool bShoulderSpaceIncreasing = false;
	float SmoothedArmLength = 210.0f;
	float ShoulderWeight = 1.0f;
	float CameraDistance = 0.0f;
	float DistanceRecoveryRemaining = 0.0f;
	float ShoulderRecoveryRemaining = 0.0f;
	float HeightLag = 0.0f;
	FVector PreviousAnchor = FVector::ZeroVector;
	FVector PreviousOwnerLocation = FVector::ZeroVector;
	FVector PreviousGravityUp = FVector::UpVector;
	FVector ObserverPivot = FVector::ZeroVector;
	FRotator ObserverRotation = FRotator::ZeroRotator;

	/** 只恢复本组件亲自修改过的网格状态，避免覆盖角色原先的 OwnerNoSee 配置。 */
	TWeakObjectPtr<USkeletalMeshComponent> HiddenOwnerMesh;
	bool bSavedOwnerNoSee = false;
};
