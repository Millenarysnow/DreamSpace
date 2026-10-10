#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "GameFramework/Actor.h"
#include "DreamMainMenuScene.generated.h"

class UCameraComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;

/** 屏幕中的归一化构图；相机与 Logo 使用同一份布局，窗口缩放时不会各自偏移。 */
struct FDreamMainMenuLayout
{
	FVector2D BuildingMin;
	FVector2D BuildingMax;
	FVector2D LogoCenter;
	FVector2D LogoMaxSize;

	/** 横屏左右排布，窄窗口上下排布；右侧 Logo 会同时镜像建筑区域。 */
	static FDreamMainMenuLayout ForAspectRatio(float AspectRatio, bool bLogoOnRight);
};

/**
 * 开始界面的展示主体。相机固定在纸张前方，只有 BuildingPivot 下的建筑绕竖轴旋转。
 * 保存的关卡只包含白模快照，不带原关卡的机关、角色或重力逻辑。
 * 运行时为排线创建动态实例，把世界位置与法线变换回建筑坐标，让笔划随建筑运动。
 */
UCLASS()
class DREAMSPACE_API ADreamMainMenuScene : public AActor
{
	GENERATED_BODY()

public:
	ADreamMainMenuScene();
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	/** 相机与转轴互为兄弟组件，转动建筑不会顺带转动相机。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "开始界面|组件")
	TObjectPtr<USceneComponent> BuildingPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "开始界面|组件")
	TObjectPtr<UCameraComponent> MenuCamera;

	/** 2 度/秒约三分钟转一圈；设为 0 可停住构图，负值反转方向。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "开始界面|旋转", meta = (Units = "deg/s"))
	float RotationSpeed = 2.0f;

	/** 由生成脚本记录初始包围盒半尺寸，用旋转扫过的圆柱保留全周取景空间。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "开始界面|构图")
	FVector BuildingHalfExtent = FVector(1000.0, 1000.0, 2000.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "开始界面|构图")
	bool bLogoOnRight = false;

	/** 只引用菜单专用排线材质；不会修改 WhitePreview 中的预览实例。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "开始界面|资源")
	TObjectPtr<UMaterialInterface> OutlineMaterial;

	/** 透明石墨 Logo，作为关卡的硬引用参与烘焙，运行时不读取外部素材目录。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "开始界面|资源")
	TObjectPtr<UTexture2D> LogoTexture;

	/** 纯几何取景计算也用于自动化验证：旋转任意角度都应落在预留区域中。 */
	static FMinimalViewInfo ComputeCameraView(const FVector& Center, const FVector& HalfExtent,
		float AspectRatio, bool bLogoOnRight);

	/** 可在蓝图/验收脚本读出当前角度，确认转动的是建筑而非相机。 */
	UFUNCTION(BlueprintPure, Category = "开始界面")
	float GetRotationAngle() const { return RotationAngle; }

	/** 编辑器脚本或蓝图修改构图参数后可立即刷新；运行时窗口变化会自动调用。 */
	UFUNCTION(BlueprintCallable, Category = "开始界面")
	void UpdateCamera(float AspectRatio = 1.777778f);

protected:
	virtual void BeginPlay() override;

private:
	/** 动态材质必须由反射引用保活，防止长时间停在主界面时被 GC 回收。 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RuntimeOutline;

	float RotationAngle = 0.0f;
	float LastAspectRatio = 0.0f;
	bool bLastLogoOnRight = false;

	void UpdateHatchingCoordinates();
};
