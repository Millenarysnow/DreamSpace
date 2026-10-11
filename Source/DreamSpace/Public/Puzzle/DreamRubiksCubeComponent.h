#pragma once

#include "DreamDragInteractionComponent.h"
#include "DreamRubiksCubeComponent.generated.h"

class UMaterialInterface;
class UStaticMesh;
class USceneComponent;

/** 魔方的局部轴；正方向与所属 Actor 的 X/Y/Z 一致，随 Actor 整体移动、旋转。 */
UENUM(BlueprintType)
enum class EDreamRubiksCubeAxis : uint8
{
	X,
	Y,
	Z
};

/** 每次已提交的四分之一圈转动，可在蓝图中驱动音效或教学提示。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FDreamRubiksCubeTurnEvent, EDreamRubiksCubeAxis, Axis, int32, LayerSign, int32, Direction);
/** 还原状态变化，包括复位和打乱；适合驱动状态指示灯。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamRubiksCubeStateEvent, bool, bIsSolved);
/** 玩家通过转动或撤销从未还原进入还原状态时触发，开局、复位和打乱不触发通关事件。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDreamRubiksCubeSolvedEvent);

/**
 * 可插拔的二阶魔方组件：八个角块、六面颜色、分层 90° 动画、打乱、撤销和还原检测。
 *
 * 在任意 Actor 蓝图上添加 Dream Rubiks Cube，把根组件设为 Movable，即可在 BeginPlay
 * 生成默认模型。编辑器中可点击 Rebuild Cube 预览；不需要专用 Actor 父类或新增输入资产。
 * 也可关闭“生成默认外观”，用组件选择器绑定同一 Actor 上的八个独立角块根组件。
 *
 * 输入复用现有持续交互：普通视口按住 E，手办中按住左键，在命中的面上沿横/纵方向拖动。
 * 超过阈值后按面法向和拖动方向确定旋转轴，命中角块所在的正/负半层决定转动哪四块。
 * 每次抓取最多提交一步，松开后已提交的动画继续完成，不会留下半转的非法排列。
 *
 * 逻辑位置和三个朝向基向量始终用整数保存，视觉四元数仅用于插值，不参与下一步逻辑推导。
 * 这样反复转动不会累积浮点漂移；还原按六面同色判断，允许整个魔方相对于初始方向转向。
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (DreamPuzzle), HideCategories = ("自由交互|枢轴", "自由交互|碰撞"),
	meta = (BlueprintSpawnableComponent, DisplayName = "Dream Rubiks Cube"))
class DREAMSPACE_API UDreamRubiksCubeComponent : public UDreamDragInteractionComponent
{
	GENERATED_BODY()

public:
	UDreamRubiksCubeComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Deactivate() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void TickComponent(
		float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual bool BeginDrag(AActor* Interactor, const FVector& RayOrigin, const FVector& RayDirection) override;
	virtual void EndDrag() override;

	/**
	 * 从配置重新构建完整魔方；编辑器按钮可用于预览。播放期间非拖动、非转动时也能调用。
	 * 自定义角块以重建时的姿态作为新的还原姿态，因此重新绑定模型后需要调用本函数。
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "二阶魔方")
	bool RebuildCube();

	/**
	 * 转动指定局部轴的正/负半层。LayerSign 和 Direction 均只接受 -1 或 +1。
	 * Direction = +1 表示围绕局部正轴的正 90° 四元数旋转；与当前观察面无关。
	 * 正在抓取、转动、停用或配置无效时返回 false，不会覆盖已有动作。
	 */
	UFUNCTION(BlueprintCallable, Category = "二阶魔方")
	bool RotateLayer(EDreamRubiksCubeAxis Axis, int32 LayerSign, int32 Direction, bool bAnimate = true);

	/** 恢复八个角块的初始姿态并清空玩家历史；复位不会触发 OnCubeSolved。 */
	UFUNCTION(BlueprintCallable, Category = "二阶魔方")
	bool ResetCube();

	/**
	 * 先复位再执行 1~200 个合法随机动作；固定 Seed 可复现同一题面，打乱不计玩家步数。
	 * 避免连续两步使用相同轴，并保证最后确实处于未还原状态；打乱立即完成、不播放动画。
	 */
	UFUNCTION(BlueprintCallable, Category = "二阶魔方")
	bool ShuffleCube(int32 NumMoves = 20, int32 Seed = 1337);

	/** 撤销最后一步玩家动作；动画完成后才移除历史，撤销一步会将玩家步数减一。 */
	UFUNCTION(BlueprintCallable, Category = "二阶魔方")
	bool UndoLastMove(bool bAnimate = true);

	/** 动画尚未结束时返回 false；只有八个角块均在合法位置、六面同色才算还原。 */
	UFUNCTION(BlueprintPure, Category = "二阶魔方")
	bool IsSolved() const
	{
		return bInitialized && !bTurning && bSolved && IsConfigurationValid() && AreCornerRootsValid();
	}
	UFUNCTION(BlueprintPure, Category = "二阶魔方")
	bool IsTurning() const { return bTurning; }
	UFUNCTION(BlueprintPure, Category = "二阶魔方")
	int32 GetMoveCount() const { return MoveHistory.Num(); }

	/**
	 * 固定角块身份对应的当前坐标，仅取 -1/+1；索引按 XYZ 位编码，X 是最高位。
	 * 例如 0=---、1=--+、2=-+-、4=+--、7=+++；非法索引返回零向量。
	 */
	UFUNCTION(BlueprintPure, Category = "二阶魔方")
	FIntVector GetCornerCoordinate(int32 CornerIndex) const;
	/** 返回同一身份的角块根组件，可用于蓝图效果、调试或检查实际模型。 */
	UFUNCTION(BlueprintPure, Category = "二阶魔方")
	USceneComponent* GetCornerComponent(int32 CornerIndex) const;

	UPROPERTY(BlueprintAssignable, Category = "二阶魔方")
	FDreamRubiksCubeTurnEvent OnTurnCompleted;
	UPROPERTY(BlueprintAssignable, Category = "二阶魔方")
	FDreamRubiksCubeStateEvent OnSolvedStateChanged;
	UPROPERTY(BlueprintAssignable, Category = "二阶魔方")
	FDreamRubiksCubeSolvedEvent OnCubeSolved;

protected:
	/** 魔方只转动内部角块，直接使用 Actor 局部坐标，不要求额外枢轴。 */
	virtual bool RequiresPivot() const override { return false; }
	virtual void InitializeDragSample(const FVector& RayOrigin, const FVector& RayDirection) override;
	virtual void RebaseDragSample(const FVector& RayOrigin, const FVector& RayDirection) override;
	virtual void ApplyDragSample(
		const FVector& RayOrigin, const FVector& RayDirection, const FVector2D& PointerDelta) override;
	virtual void DrawDebugRange() const override;

private:
	/** 根组件局部空间中的魔方中心；整体摆放方向由 Actor 旋转决定。 */
	UPROPERTY(EditAnywhere, Category = "二阶魔方|外观", meta = (DisplayName = "魔方中心偏移"))
	FVector CubeCenter = FVector::ZeroVector;
	/** 单个角块边长；总边长为 2*CellSize+Gap，单位厘米。 */
	UPROPERTY(EditAnywhere, Category = "二阶魔方|外观",
		meta = (ClampMin = "1.0", UIMin = "1.0", DisplayName = "角块边长（厘米）"))
	float CellSize = 50.0f;
	UPROPERTY(EditAnywhere, Category = "二阶魔方|外观",
		meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "角块间隙（厘米）"))
	float Gap = 2.5f;
	/** 自动生成 8 个黑色主体和 24 个外侧贴纸；关闭后必须绑定八个自定义角块。 */
	UPROPERTY(EditAnywhere, Category = "二阶魔方|外观", meta = (DisplayName = "生成默认外观"))
	bool bGenerateVisuals = true;
	/** 按模型 Bounds 缩放到角块尺寸；有完整颜色/结构的自定义角块应使用角块组件绑定。 */
	UPROPERTY(
		EditAnywhere, Category = "二阶魔方|外观", meta = (EditCondition = "bGenerateVisuals", DisplayName = "主体网格"))
	TObjectPtr<UStaticMesh> BodyMesh;
	/** 默认引擎 BasicShapeMaterial；所有颜色共用该材质的颜色向量参数。 */
	UPROPERTY(
		EditAnywhere, Category = "二阶魔方|外观", meta = (EditCondition = "bGenerateVisuals", DisplayName = "颜色材质"))
	TObjectPtr<UMaterialInterface> ColorMaterial;
	UPROPERTY(EditAnywhere, Category = "二阶魔方|外观",
		meta = (EditCondition = "bGenerateVisuals", DisplayName = "颜色参数名称", AdvancedDisplay))
	FName ColorParameterName = TEXT("Color");
	/** 每张贴纸距离角块边缘的留黑宽度；运行时会限制在可见的合理范围内。 */
	UPROPERTY(EditAnywhere, Category = "二阶魔方|外观",
		meta = (EditCondition = "bGenerateVisuals", ClampMin = "0.0", UIMin = "0.0", DisplayName = "贴纸边距（厘米）"))
	float StickerMargin = 3.0f;
	/**
	 * 顺序固定为 ---、--+、-+-、-++、+--、+-+、++-、+++，与角块的初始位置对应。
	 * 每项选择同一 Actor 上直接挂在根组件下的 Movable SceneComponent，子网格随它一起转动。
	 * 自定义模型需要处于六面同色的还原姿态，并启用可被交互射线检测的查询碰撞。
	 */
	UPROPERTY(EditAnywhere, Category = "二阶魔方|外观",
		meta = (EditCondition = "!bGenerateVisuals", UseComponentPicker = true,
			AllowedClasses = "/Script/Engine.SceneComponent", DisplayName = "自定义角块组件（8 项）"))
	TArray<FComponentReference> CornerComponents;

	UPROPERTY(EditAnywhere, Category = "二阶魔方|交互",
		meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "90° 转动时长（秒）"))
	float TurnDuration = 0.25f;
	/** 局部面内拖动距离，和魔方整体缩放一起缩放；沿主方向越过阈值才提交一次转动。 */
	UPROPERTY(EditAnywhere, Category = "二阶魔方|交互",
		meta = (ClampMin = "0.1", UIMin = "0.1", DisplayName = "拖动阈值（厘米）"))
	float DragThreshold = 5.0f;
	UPROPERTY(EditAnywhere, Category = "二阶魔方|开局", meta = (DisplayName = "开局打乱"))
	bool bShuffleOnBeginPlay = false;
	UPROPERTY(EditAnywhere, Category = "二阶魔方|开局",
		meta = (EditCondition = "bShuffleOnBeginPlay", ClampMin = "1", ClampMax = "200", DisplayName = "打乱步数"))
	int32 InitialShuffleMoves = 20;
	UPROPERTY(EditAnywhere, Category = "二阶魔方|开局",
		meta = (EditCondition = "bShuffleOnBeginPlay", DisplayName = "打乱种子"))
	int32 InitialShuffleSeed = 1337;

	/** 三个整数基向量描述角块朝向，避免靠浮点四元数反推格子和贴纸法向。 */
	struct FCornerState
	{
		FIntVector Coordinate = FIntVector::ZeroValue;
		FIntVector Basis[3] = {FIntVector(1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, 0, 1)};
	};
	struct FMove
	{
		EDreamRubiksCubeAxis Axis = EDreamRubiksCubeAxis::X;
		int32 LayerSign = 1;
		int32 Direction = 1;
	};
	TArray<FCornerState> Corners;
	TArray<FMove> MoveHistory;
	/** 以下数组均按角块身份索引；初始变换保留自定义模型的缩放、偏移和预旋转。 */
	UPROPERTY(Transient, DuplicateTransient)
	TArray<TObjectPtr<USceneComponent>> CornerRoots;
	UPROPERTY(Transient, DuplicateTransient)
	TArray<TObjectPtr<USceneComponent>> GeneratedComponents;
	/** 贴纸统一使用引擎单位立方体，独立于用户选择的主体形状。硬引用确保打包时被烘焙。 */
	UPROPERTY()
	TObjectPtr<UStaticMesh> StickerMesh;
	TArray<FTransform> InitialCornerTransforms;
	bool bInitialized = false;
	bool bSolved = true;
	bool bNotifying = false;

	/** 动画只改变视觉；结束才提交整数状态和历史，异常中断可恢复到上一步完整状态。 */
	bool bTurning = false;
	bool bUndoing = false;
	float TurnElapsed = 0.0f;
	float ActiveTurnDuration = 0.0f;
	FMove ActiveMove;
	TArray<int32> TurningCorners;
	TArray<FTransform> TurningStartTransforms;

	/** 抓取时锁定面、角块所在格和投影平面，后续射线不必再次命中魔方轮廓。 */
	int32 GrabFaceAxis = 0;
	float GrabPlaneCoordinate = 0.0f;
	FVector GrabFaceNormal = FVector::ZeroVector;
	FIntVector GrabCoordinate = FIntVector::ZeroValue;
	FVector LastGrabPoint = FVector::ZeroVector;
	FVector AccumulatedDrag = FVector::ZeroVector;
	bool bHasGrabSample = false;
	bool bTurnIssuedForGesture = false;

	bool EnsureInitialized();
	bool IsConfigurationValid() const;
	bool AreCornerRootsValid() const;
	void InitializeSolvedState();
	bool BuildVisuals();
	void DestroyGeneratedVisuals();
	FTransform GetCornerTransform(int32 CornerIndex) const;
	void ApplyAllVisualStates();
	bool StartTurn(const FMove& Move, bool bAnimate, bool bUndo);
	void FinishTurn();
	void AbortTurn();
	void ApplyLogicalMove(const FMove& Move);
	bool ComputeSolved() const;
	void NotifySolvedChange(bool bPreviouslySolved, bool bAllowVictoryEvent);
	bool PickCorner(const FVector& Origin, const FVector& Direction, int32& OutIndex, FHitResult& OutHit) const;
	bool IntersectGrabPlane(const FVector& Origin, const FVector& Direction, FVector& OutPoint) const;
	static FIntVector HomeCoordinate(int32 Index);
	static FIntVector RotateQuarter(const FIntVector& Vector, EDreamRubiksCubeAxis Axis, int32 Direction);
	static FVector AxisVector(EDreamRubiksCubeAxis Axis);
};
