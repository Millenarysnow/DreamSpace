#pragma once

#include "CoreMinimal.h"
#include "DreamInteractableInterface.h"
#include "GameFramework/Actor.h"
#include "DreamPasswordChest.generated.h"

class ADreamCharacter;
class ADreamPlayerController;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;

/** 密码箱的单向解锁流程；等待拾取用于原操作者中途离开后，由新角色继续领取。 */
UENUM(BlueprintType)
enum class EDreamPasswordChestState : uint8
{
	Locked UMETA(DisplayName = "上锁"),
	Opening UMETA(DisplayName = "正在开盖"),
	AwaitingPickup UMETA(DisplayName = "等待自动拾取"),
	Attracting UMETA(DisplayName = "光点飞向玩家"),
	Collected UMETA(DisplayName = "手办已领取")
};

/**
 * 可直接放入关卡的四位数字密码箱，复用世界 E 射线和项目的统一交互接口。
 *
 * 原生箱体由底板、四面侧板和独立的盖子组成，未指定美术资源也能直接验证完整流程。
 * 正确密码 -> 绕铰链开盖 -> 显示箱内光点 -> 光点自动飞向玩家 -> 授予手办。
 * 密码输入会话属于玩家控制器，箱子只保存机关与奖励状态，不接管全局输入。
 */
UCLASS(Blueprintable, meta = (DisplayName = "Dream Password Chest"))
class DREAMSPACE_API ADreamPasswordChest : public AActor, public IDreamInteractableInterface
{
	GENERATED_BODY()

public:
	ADreamPasswordChest();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnInteracted_Implementation(AActor* Interactor) override;

	/** 严格校验四个 ASCII 数字；使用字符串保留 0007 这样的前导零，不接受空格和符号。 */
	static bool IsFourDigitPassword(const FString& Password);

	/** 确认操作者仍为同世界的受控角色、未持有手办且在范围内，供输入会话持续检查。 */
	bool CanInteract(const ADreamCharacter* Character) const;

	/** 正确密码仅启动开盖，不立即发放手办；错误、无效配置和重复提交均不改变箱子状态。 */
	bool TryUnlock(const FString& Password, ADreamCharacter* Character);

	/** 当前机关状态，供蓝图音效、提示和关卡逻辑读取。 */
	UFUNCTION(BlueprintPure, Category = "密码箱")
	EDreamPasswordChestState GetChestState() const { return State; }

	/** 密码为四位字符串，默认 1234。故意不自动截断错误配置，避免设计时填错仍可打开。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "密码箱|密码", meta = (DisplayName = "四位数字密码"))
	FString UnlockPassword = TEXT("1234");

	/** 从角色胶囊中心计算的最大交互距离，单位厘米；E 射线本身仍负责墙体遮挡。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "密码箱|交互", meta = (ClampMin = "1", Units = "cm", DisplayName = "交互距离"))
	float InteractionDistance = 450.0f;

	/** 开盖时间，0 表示下一次更新立即打开；使用归一化进度保证不同帧率下的总时长一致。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "密码箱|动画", meta = (ClampMin = "0", Units = "s", DisplayName = "开盖时长"))
	float OpeningDuration = 0.8f;

	/** 围绕 LidPivot 局部 X 轴的开盖角度，正值抬起默认盖子的前沿；模型轴向不同时可设负值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "密码箱|动画", meta = (ClampMin = "-175", ClampMax = "175", Units = "deg", DisplayName = "开盖角度"))
	float OpenAngle = 105.0f;

	/** 盖子完全打开后，光点在箱内短暂停留，让玩家看清奖励再自动吸取。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "密码箱|光点", meta = (ClampMin = "0", Units = "s", DisplayName = "拾取前停留"))
	float PickupDelay = 0.35f;

	/** 光点飞向手办显示位置的时长；飞行期间持续追踪角色，玩家移动不会让目标停在旧位置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "密码箱|光点", meta = (ClampMin = "0", Units = "s", DisplayName = "吸取时长"))
	float AttractionDuration = 0.65f;

	/** 在直线吸取路径上沿箱子的局部上方叠加弧线，单位厘米，0 为直线飞行。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "密码箱|光点", meta = (ClampMin = "0", Units = "cm", DisplayName = "吸取弧线高度"))
	float AttractionArcHeight = 45.0f;

	/** 箱体底板；默认尺寸 90×70 cm，Actor 原点位于箱底，便于贴地摆放。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "密码箱|组件")
	TObjectPtr<UStaticMeshComponent> BaseMesh;

	/** 四面独立侧板形成真正中空的箱体；蓝图子类可以替换网格或隐藏占位模型。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "密码箱|组件")
	TArray<TObjectPtr<UStaticMeshComponent>> SideMeshes;

	/** 铰链独立于盖子网格的原点；替换美术模型时只需调整铰链与盖子的相对变换。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "密码箱|组件")
	TObjectPtr<USceneComponent> LidPivot;

	/** 盖子必须保持 Movable，开盖时只旋转其父级铰链，箱体与碰撞不会随之整体转动。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "密码箱|组件")
	TObjectPtr<UStaticMeshComponent> LidMesh;

	/** 奖励光点的局部位置和大小；开局隐藏，开盖后显示。可在蓝图中附加自己的 Niagara 特效。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "密码箱|组件")
	TObjectPtr<USceneComponent> GlowRoot;

	/** 默认发光球体无碰撞，不阻挡 E 射线或角色，自动拾取不依赖物理重叠事件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "密码箱|组件")
	TObjectPtr<UStaticMeshComponent> GlowMesh;

	/** 光点自带的小范围点光源，与球体一起移动，默认不投射动态阴影。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "密码箱|组件")
	TObjectPtr<UPointLightComponent> GlowLight;

protected:
	/** 仅在首次正确解锁时触发，供美术蓝图播放开锁声音。此时尚未获得手办。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "密码箱")
	void OnChestUnlocked();

	/** 光点实际抵达并授予手办后触发，箱子状态已置为已领取，回调重入不会重复发放。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "密码箱")
	void OnMiniatureCollected(ADreamCharacter* Character);

private:
	/** 验证开盖时记录的角色和控制器仍相互匹配，防止重生/换 Pawn 后奖励发给旧角色。 */
	bool IsCollectorValid() const;
	void StartAttraction(ADreamCharacter* Character);
	/** 中途失去操作者时把光点放回箱内，保留已经解锁的箱盖，后续 E 可以继续领取。 */
	void ReturnGlowToChest();
	void SetGlowVisible(bool bVisible);

	UPROPERTY(VisibleInstanceOnly, Category = "密码箱", meta = (DisplayName = "当前状态"))
	EDreamPasswordChestState State = EDreamPasswordChestState::Locked;
	TWeakObjectPtr<ADreamCharacter> Collector;
	TWeakObjectPtr<ADreamPlayerController> CollectorController;
	/** 动画起点在 BeginPlay 记录，因此支持蓝图设置的铰链初始姿态与光点偏移。 */
	FQuat ClosedLidRotation = FQuat::Identity;
	FTransform GlowHomeTransform = FTransform::Identity;
	FVector AttractionStart = FVector::ZeroVector;
	float StateElapsed = 0.0f;
};
