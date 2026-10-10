#include "DreamRubiksCubeComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DreamCharacter.h"
#include "DreamPlayerController.h"
#include "DreamSceneCaptureAnchor.h"
#include "DreamSceneCapturePresentationComponent.h"
#include "DreamShoulderCameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/World.h"
#include "Math/RotationMatrix.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace
{
/** 隔离世界只生成测试模型，不写入用户地图；根为普通 SceneComponent，验证组件可以直接插拔。 */
struct FRubiksFixture
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AActor* Owner = nullptr;
	AActor* Interactor = nullptr;
	UDreamRubiksCubeComponent* Cube = nullptr;

	FRubiksFixture()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Owner = World->SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(Owner);
		Root->SetMobility(EComponentMobility::Movable);
		Owner->SetRootComponent(Root);
		Owner->AddInstanceComponent(Root);
		Root->RegisterComponent();
		Interactor = World->SpawnActor<AActor>();
		Cube = NewObject<UDreamRubiksCubeComponent>(Owner);
		Owner->AddInstanceComponent(Cube);
		Cube->RegisterComponent();
		// 瞬时世界未执行正常 Actor BeginPlay，显式激活组件以模拟游戏生命周期。
		Cube->Activate(true);
		check(Cube->RebuildCube());
	}

	~FRubiksFixture()
	{
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}

	TArray<FTransform> Snapshot() const
	{
		TArray<FTransform> Transforms;
		for (int32 Index = 0; Index < 8; ++Index)
			Transforms.Add(Cube->GetCornerComponent(Index)->GetRelativeTransform());
		return Transforms;
	}

	/** 以 Actor 当前姿态把局部射线变成真实世界射线，覆盖偏移、整体旋转和缩放。 */
	FVector Origin(const FVector& Local) const { return Owner->GetActorTransform().TransformPosition(Local); }
	FVector Direction(const FVector& Local) const
	{
		return Owner->GetActorTransform().TransformVector(Local).GetSafeNormal();
	}
};

void SetFloat(UObject* Object, const TCHAR* Name, float Value)
{
	FFloatProperty* Property = FindFProperty<FFloatProperty>(Object->GetClass(), Name);
	check(Property);
	Property->SetPropertyValue_InContainer(Object, Value);
}

void SetBool(UObject* Object, const TCHAR* Name, bool Value)
{
	FBoolProperty* Property = FindFProperty<FBoolProperty>(Object->GetClass(), Name);
	check(Property);
	Property->SetPropertyValue_InContainer(Object, Value);
}

/** 模拟关卡详情面板的自定义组件选择器，不为测试扩大实际蓝图接口。 */
void BindCorners(UDreamRubiksCubeComponent* Cube, const TArray<USceneComponent*>& Roots)
{
	FArrayProperty* Property = FindFProperty<FArrayProperty>(Cube->GetClass(), TEXT("CornerComponents"));
	check(Property);
	FScriptArrayHelper Array(Property, Property->ContainerPtrToValuePtr<void>(Cube));
	Array.Resize(Roots.Num());
	for (int32 Index = 0; Index < Roots.Num(); ++Index)
	{
		FComponentReference* Reference = reinterpret_cast<FComponentReference*>(Array.GetRawPtr(Index));
		Reference->OverrideComponent = Roots[Index];
	}
}

/** 比较完整姿态，既检查位置，也检查颜色贴纸随角块的朝向；仅比较排列会漏掉角块扭转。 */
bool MatchesSnapshot(const FRubiksFixture& Scene, const TArray<FTransform>& Snapshot)
{
	for (int32 Index = 0; Index < 8; ++Index)
		if (!Scene.Cube->GetCornerComponent(Index)->GetRelativeTransform().Equals(Snapshot[Index], 0.0001f))
			return false;
	return true;
}

int32 CountMeshes(const AActor* Owner)
{
	TArray<UStaticMeshComponent*> Meshes;
	Owner->GetComponents(Meshes);
	return Meshes.Num();
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRubiksLayerTest, "DreamSpace.Puzzle.RubiksCube.SixLayersAndExactInverse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRubiksLayerTest::RunTest(const FString& Parameters)
{
	FRubiksFixture Scene;
	const TArray<FTransform> Initial = Scene.Snapshot();
	TestTrue(TEXT("添加组件即可生成还原状态，不要求枢轴或专用 Actor"), Scene.Cube->IsSolved());
	TestEqual(TEXT("默认外观只生成八个主体和二十四张贴纸"), CountMeshes(Scene.Owner), 32);
	for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		FVector AxisVector = FVector::ZeroVector;
		AxisVector[AxisIndex] = 1.0;
		for (int32 Layer : {-1, 1})
		{
			for (int32 Direction : {-1, 1})
			{
				Scene.Cube->ResetCube();
				const EDreamRubiksCubeAxis Axis = static_cast<EDreamRubiksCubeAxis>(AxisIndex);
				TestTrue(
					TEXT("六种层、正负方向均接受合法一步"), Scene.Cube->RotateLayer(Axis, Layer, Direction, false));
				TestFalse(TEXT("单独转一层不应还原"), Scene.Cube->IsSolved());
				int32 Changed = 0;
				const FQuat Rotation(AxisVector, Direction * HALF_PI);
				for (int32 Index = 0; Index < 8; ++Index)
				{
					const bool bInLayer = Initial[Index].GetLocation()[AxisIndex] * Layer > 0.0;
					const FTransform Current = Scene.Cube->GetCornerComponent(Index)->GetRelativeTransform();
					if (!Current.Equals(Initial[Index], 0.0001f))
						++Changed;
					if (bInLayer)
					{
						TestTrue(TEXT("选中层按预期的正负 90 度公转"),
							Current.GetLocation().Equals(Rotation.RotateVector(Initial[Index].GetLocation()), 0.0001f));
						TestTrue(TEXT("选中层的贴纸朝向也跟随旋转"),
							Current.GetRotation().Equals(Rotation * Initial[Index].GetRotation(), 0.0001f));
					}
					else
						TestTrue(TEXT("未选中半层完整姿态不变"), Current.Equals(Initial[Index], 0.0001f));
				}
				TestEqual(TEXT("一次转动严格只改变四个角块"), Changed, 4);
				TestTrue(TEXT("逆操作被接受"), Scene.Cube->RotateLayer(Axis, Layer, -Direction, false));
				TestTrue(TEXT("逆操作精确恢复位置与朝向"), MatchesSnapshot(Scene, Initial) && Scene.Cube->IsSolved());
				for (int32 Step = 0; Step < 4; ++Step)
					Scene.Cube->RotateLayer(Axis, Layer, Direction, false);
				TestTrue(TEXT("四次同方向转动精确回到起始状态"), MatchesSnapshot(Scene, Initial));
			}
		}
	}
	TestFalse(TEXT("非法轴值被拒绝"), Scene.Cube->RotateLayer(static_cast<EDreamRubiksCubeAxis>(255), 1, 1, false));
	TestFalse(TEXT("不存在中间层零"), Scene.Cube->RotateLayer(EDreamRubiksCubeAxis::X, 0, 1, false));
	TestFalse(TEXT("非法方向被拒绝"), Scene.Cube->RotateLayer(EDreamRubiksCubeAxis::X, 1, 2, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRubiksLongSequenceTest,
	"DreamSpace.Puzzle.RubiksCube.LongSequenceUndoAndWholeCubeSolved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRubiksLongSequenceTest::RunTest(const FString& Parameters)
{
	FRubiksFixture Scene;
	const TArray<FTransform> Initial = Scene.Snapshot();
	FRandomStream Random(4242);
	for (int32 Step = 0; Step < 512; ++Step)
	{
		Scene.Cube->RotateLayer(static_cast<EDreamRubiksCubeAxis>(Random.RandRange(0, 2)),
			Random.RandRange(0, 1) != 0 ? 1 : -1, Random.RandRange(0, 1) != 0 ? 1 : -1, false);
		TSet<FIntVector> Coordinates;
		for (int32 Index = 0; Index < 8; ++Index)
			Coordinates.Add(Scene.Cube->GetCornerCoordinate(Index));
		TestEqual(TEXT("长序列中的八个角块一直占据八个不同整数格"), Coordinates.Num(), 8);
	}
	TestEqual(TEXT("每次合法转动记入玩家步数"), Scene.Cube->GetMoveCount(), 512);
	for (int32 Step = 0; Step < 512; ++Step)
		TestTrue(TEXT("长序列每步都可撤销"), Scene.Cube->UndoLastMove(false));
	TestTrue(TEXT("大量反向操作后没有浮点位置和朝向漂移"), MatchesSnapshot(Scene, Initial));
	TestTrue(TEXT("逆序撤销恢复六面同色"), Scene.Cube->IsSolved());
	TestEqual(TEXT("全部撤销后步数清零"), Scene.Cube->GetMoveCount(), 0);
	TestFalse(TEXT("空历史不会产生虚假撤销"), Scene.Cube->UndoLastMove(false));
	// 二阶魔方没有固定中心块，两层同向转动等于整颗魔方转向，应仍判为还原。
	Scene.Cube->RotateLayer(EDreamRubiksCubeAxis::Y, -1, 1, false);
	Scene.Cube->RotateLayer(EDreamRubiksCubeAxis::Y, 1, 1, false);
	TestTrue(TEXT("整体转向后的六面同色也算还原"), Scene.Cube->IsSolved());
	TestFalse(TEXT("整体转向确实改变了角块的固定世界排列"), MatchesSnapshot(Scene, Initial));
	Scene.Cube->RotateLayer(EDreamRubiksCubeAxis::X, 1, -1, false);
	TestFalse(TEXT("整体转向后再转单层会重新打乱"), Scene.Cube->IsSolved());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRubiksShuffleTest,
	"DreamSpace.Puzzle.RubiksCube.DeterministicShuffleResetAndRebuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRubiksShuffleTest::RunTest(const FString& Parameters)
{
	FRubiksFixture Scene;
	const TArray<FTransform> Initial = Scene.Snapshot();
	TestTrue(TEXT("固定种子打乱成功"), Scene.Cube->ShuffleCube(20, 2026));
	const TArray<FTransform> Shuffled = Scene.Snapshot();
	TestFalse(TEXT("打乱始终提供未还原题面"), Scene.Cube->IsSolved());
	TestEqual(TEXT("打乱不计入玩家步数"), Scene.Cube->GetMoveCount(), 0);
	TestFalse(TEXT("打乱不进入玩家撤销历史"), Scene.Cube->UndoLastMove(false));
	Scene.Cube->ShuffleCube(20, 2026);
	TestTrue(TEXT("同一个种子重复得到同一个题面"), MatchesSnapshot(Scene, Shuffled));
	Scene.Cube->ShuffleCube(20, 2027);
	TestFalse(TEXT("另一个种子能得到不同题面"), MatchesSnapshot(Scene, Shuffled));
	Scene.Cube->ResetCube();
	TestTrue(TEXT("复位恢复原始姿态"), MatchesSnapshot(Scene, Initial) && Scene.Cube->IsSolved());
	for (int32 Index = 0; Index < 3; ++Index)
		TestTrue(TEXT("编辑器重建按钮可以反复使用"), Scene.Cube->RebuildCube());
	TestEqual(TEXT("重建不会累积重复网格或重复碰撞"), CountMeshes(Scene.Owner), 32);
	SetBool(Scene.Cube, TEXT("bShuffleOnBeginPlay"), true);
	Scene.Cube->BeginPlay();
	TestFalse(TEXT("组件 BeginPlay 的开局打乱配置生效"), Scene.Cube->IsSolved());
	Scene.Cube->DestroyComponent();
	TestEqual(TEXT("移除组件会清理它自动生成的网格与碰撞"), CountMeshes(Scene.Owner), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRubiksAnimationTest,
	"DreamSpace.Puzzle.RubiksCube.AnimationReleaseAndInterruptedTransactions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRubiksAnimationTest::RunTest(const FString& Parameters)
{
	FRubiksFixture Scene;
	SetFloat(Scene.Cube, TEXT("TurnDuration"), 1.0f);
	const TArray<FTransform> Initial = Scene.Snapshot();
	TestTrue(TEXT("动画可以开始"), Scene.Cube->RotateLayer(EDreamRubiksCubeAxis::Z, 1, 1));
	Scene.Cube->TickComponent(0.5f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("半程动画尚未完成"), Scene.Cube->IsTurning());
	TestEqual(TEXT("动画中不会提前提交玩家步数"), Scene.Cube->GetMoveCount(), 0);
	TestFalse(TEXT("动画中不会提前算作还原"), Scene.Cube->IsSolved());
	TestFalse(TEXT("运动中拒绝另一动作"), Scene.Cube->RotateLayer(EDreamRubiksCubeAxis::X, 1, 1));
	TestFalse(TEXT("运动中拒绝重建"), Scene.Cube->RebuildCube());
	TestFalse(TEXT("运动中拒绝打乱"), Scene.Cube->ShuffleCube());
	TestFalse(TEXT("运动中拒绝复位"), Scene.Cube->ResetCube());
	Scene.Cube->Deactivate();
	TestFalse(TEXT("停用立即结束未完成动画"), Scene.Cube->IsTurning());
	TestTrue(TEXT("停用取消动画并完整恢复上一步"), MatchesSnapshot(Scene, Initial));
	TestEqual(TEXT("取消动作没有记入历史"), Scene.Cube->GetMoveCount(), 0);
	Scene.Cube->Activate(true);
	Scene.Cube->RotateLayer(EDreamRubiksCubeAxis::Z, 1, 1);
	Scene.Cube->EndDrag();
	Scene.Cube->TickComponent(2.0f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("松手后动画仍完整结束"), Scene.Cube->IsTurning());
	TestEqual(TEXT("大帧间隔也只提交一次 90 度动作"), Scene.Cube->GetMoveCount(), 1);
	TestTrue(TEXT("动画撤销开始"), Scene.Cube->UndoLastMove());
	Scene.Cube->TickComponent(0.4f, LEVELTICK_All, nullptr);
	Scene.Cube->Deactivate();
	TestEqual(TEXT("取消撤销仍保留原历史，便于下次再撤销"), Scene.Cube->GetMoveCount(), 1);
	Scene.Cube->Activate(true);
	Scene.Cube->UndoLastMove();
	Scene.Cube->TickComponent(2.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("完整撤销动画恢复还原"), Scene.Cube->IsSolved() && MatchesSnapshot(Scene, Initial));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRubiksGestureTest,
	"DreamSpace.Puzzle.RubiksCube.RayGestureThresholdPlaneAndOneMovePerGrab",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRubiksGestureTest::RunTest(const FString& Parameters)
{
	FRubiksFixture Scene;
	SetFloat(Scene.Cube, TEXT("TurnDuration"), 0.0f);
	const TArray<FTransform> Initial = Scene.Snapshot();
	// 整体移动、旋转和非等比正缩放后，局部面方向及命中的格子仍应正确。
	Scene.Owner->SetActorTransform(FTransform(FRotator(25, 40, 15), FVector(400, -250, 90), FVector(1.5, 0.8, 2.0)));
	const FVector Start(-300, 26, 26);
	const FVector Forward = Scene.Direction(FVector::ForwardVector);
	TestTrue(TEXT("真实碰撞射线可以抓取经过整体变换的负 X 面"),
		Scene.Cube->BeginDrag(Scene.Interactor, Scene.Origin(Start), Forward));
	TestFalse(TEXT("另一个操作者不能重复抓取占用的组件"),
		Scene.Cube->BeginDrag(Scene.Interactor, Scene.Origin(Start), Forward));
	for (int32 Pixel = 1; Pixel <= 4; ++Pixel)
	{
		Scene.Cube->RebaseDragRay(Scene.Origin(Start + FVector(0, Pixel - 1, 0)), Forward);
		Scene.Cube->UpdateDrag(Scene.Origin(Start + FVector(0, Pixel, 0)), Forward, FVector2D(1, 0));
	}
	TestEqual(TEXT("多帧小抖动尚未越过阈值不转动"), Scene.Cube->GetMoveCount(), 0);
	Scene.Cube->RebaseDragRay(Scene.Origin(Start + FVector(0, 4, 0)), Forward);
	Scene.Cube->UpdateDrag(Scene.Origin(Start + FVector(0, 6, 0)), Forward, FVector2D(2, 0));
	TestEqual(TEXT("重采样不清空跨帧累积的拖动阈值"), Scene.Cube->GetMoveCount(), 1);
	const FQuat Expected(FVector::UpVector, -HALF_PI);
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const bool bSelected = Initial[Index].GetLocation().Z > 0.0;
		TestTrue(TEXT("负 X 面向正 Y 拖动，只带动正 Z 层向负方向"),
			Scene.Cube->GetCornerComponent(Index)->GetRelativeLocation().Equals(
				bSelected ? Expected.RotateVector(Initial[Index].GetLocation()) : Initial[Index].GetLocation(),
				0.001f));
	}
	Scene.Cube->UpdateDrag(Scene.Origin(Start + FVector(0, 200, 0)), Forward, FVector2D(194, 0));
	Scene.Cube->SuspendDragInput();
	Scene.Cube->UpdateDrag(Scene.Origin(Start), Forward, FVector2D(-200, 0));
	Scene.Cube->UpdateDrag(Scene.Origin(Start + FVector(0, -20, 0)), Forward, FVector2D(-20, 0));
	TestEqual(TEXT("长按、移出轮廓和重新进入仍只执行一次动作"), Scene.Cube->GetMoveCount(), 1);
	Scene.Cube->EndDrag();
	Scene.Cube->ResetCube();
	Scene.Cube->BeginDrag(Scene.Interactor, Scene.Origin(Start), Forward);
	Scene.Cube->UpdateDrag(Scene.Origin(Start + FVector(0, 2, 0)), Forward, FVector2D(2, 0));
	Scene.Cube->SuspendDragInput();
	Scene.Cube->UpdateDrag(Scene.Origin(Start + FVector(0, 60, 0)), Forward, FVector2D(58, 0));
	TestEqual(TEXT("重新进入显示面首帧只采样，不把跨边界位移当成转动"), Scene.Cube->GetMoveCount(), 0);
	Scene.Cube->UpdateDrag(Scene.Origin(Start + FVector(0, 54, 0)), Forward, FVector2D(-6, 0));
	TestEqual(TEXT("重新采样后可继续在无限抓取平面拖动"), Scene.Cube->GetMoveCount(), 1);
	Scene.Cube->EndDrag();
	Scene.Cube->ResetCube();
	TestFalse(TEXT("未命中角块的射线不开始会话"),
		Scene.Cube->BeginDrag(Scene.Interactor, Scene.Origin(FVector(-300, 300, 300)), Forward));
	Scene.Cube->BeginDrag(Scene.Interactor, Scene.Origin(Start), Forward);
	Scene.Cube->UpdateDrag(Scene.Origin(Start), Scene.Direction(FVector::RightVector), FVector2D::ZeroVector);
	TestEqual(TEXT("平行于抓取面的退化射线只暂停，无虚假动作"), Scene.Cube->GetMoveCount(), 0);
	Scene.Interactor->Destroy();
	Scene.Cube->TickComponent(0.0f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("操作者销毁后组件释放抓取状态"), Scene.Cube->IsDragging());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRubiksCustomCornersTest,
	"DreamSpace.Puzzle.RubiksCube.CustomCornersPreserveAuthoredTransforms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRubiksCustomCornersTest::RunTest(const FString& Parameters)
{
	FRubiksFixture Scene;
	SetBool(Scene.Cube, TEXT("bGenerateVisuals"), false);
	TArray<USceneComponent*> Roots;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		UBoxComponent* Corner = NewObject<UBoxComponent>(Scene.Owner);
		Corner->SetMobility(EComponentMobility::Movable);
		Corner->SetupAttachment(Scene.Owner->GetRootComponent());
		Corner->SetBoxExtent(FVector(20));
		Corner->SetCollisionProfileName(TEXT("BlockAllDynamic"));
		Corner->SetRelativeTransform(FTransform(FRotator(10, 20, 30),
			FVector((Index & 4) != 0 ? 35 : -35, (Index & 2) != 0 ? 35 : -35, (Index & 1) != 0 ? 35 : -35),
			FVector(0.8, 1.2, 1.0)));
		Scene.Owner->AddInstanceComponent(Corner);
		Corner->RegisterComponent();
		Roots.Add(Corner);
	}
	BindCorners(Scene.Cube, Roots);
	TestTrue(TEXT("组件选择器可绑定现有关卡的八个独立角块"), Scene.Cube->RebuildCube());
	TestEqual(TEXT("自定义模式会移除默认的主体和贴纸"), CountMeshes(Scene.Owner), 0);
	const TArray<FTransform> Initial = Scene.Snapshot();
	Scene.Cube->RotateLayer(EDreamRubiksCubeAxis::Y, 1, 1, false);
	Scene.Cube->UndoLastMove(false);
	TestTrue(TEXT("转动/撤销保留自定义缩放和预旋转"), MatchesSnapshot(Scene, Initial));
	Scene.Cube->ShuffleCube();
	Scene.Cube->RebuildCube();
	TestTrue(TEXT("重建不会把打乱姿态重新定义为还原"), MatchesSnapshot(Scene, Initial));
	Roots[0]->SetMobility(EComponentMobility::Static);
	TestFalse(TEXT("角块配置失效后不会报告虚假的还原状态"), Scene.Cube->IsSolved());
	TestFalse(TEXT("静态角块禁止运动"), Scene.Cube->RotateLayer(EDreamRubiksCubeAxis::X, -1, 1, false));
	Roots[0]->SetMobility(EComponentMobility::Movable);
	TArray<USceneComponent*> Repeated = Roots;
	Repeated[1] = Repeated[0];
	BindCorners(Scene.Cube, Repeated);
	TestFalse(TEXT("重复角块引用被拒绝"), Scene.Cube->RebuildCube());
	BindCorners(Scene.Cube, Roots);
	TestTrue(TEXT("修正配置后可以重新构建"), Scene.Cube->RebuildCube());
	Scene.Cube->DestroyComponent();
	TestTrue(TEXT("移除魔方组件不会销毁用户自己绑定的角块"), IsValid(Roots[0]) && IsValid(Roots[7]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRubiksControllerTest,
	"DreamSpace.Puzzle.RubiksCube.WorldEAndMiniaturePickingControllerLocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRubiksControllerTest::RunTest(const FString& Parameters)
{
	FRubiksFixture Scene;
	SetFloat(Scene.Cube, TEXT("TurnDuration"), 0.0f);
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamCharacter* Character =
		Scene.World->SpawnActor<ADreamCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
	ADreamPlayerController* Controller = Scene.World->SpawnActor<ADreamPlayerController>();
	Controller->Player = NewObject<ULocalPlayer>(GEngine);
	Scene.World->AddController(Controller);
	Controller->SpawnPlayerCameraManager();
	Controller->Possess(Character);
	Controller->SetViewTarget(Character);
	Scene.Owner->SetActorLocation(FVector(400, 0, 100));
	FMinimalViewInfo View;
	View.Location = Scene.Owner->GetActorLocation() + FVector(-250, 26, 26);
	View.Rotation = FRotator::ZeroRotator;
	Scene.World->TimeSeconds = 1.0;
	Controller->PlayerCameraManager->UpdateCamera(0.0f);
	Controller->PlayerCameraManager->SetCameraCachePOV(View);
	Controller->SetIgnoreLookInput(true);
	Controller->Interact();
	TestTrue(TEXT("普通 E 经真实世界首个碰撞命中进入魔方拖动"), Controller->IsDraggingInteraction());
	TestTrue(
		TEXT("魔方拖动复用控制器移动和视角锁"), Controller->IsMoveInputIgnored() && Controller->IsLookInputIgnored());
	Scene.Cube->UpdateDrag(View.Location + FVector(0, 8, 0), FVector::ForwardVector, FVector2D(8, 0));
	TestEqual(TEXT("世界 E 对应射线实际转动一层"), Scene.Cube->GetMoveCount(), 1);
	Controller->EndWorldDrag();
	TestFalse(TEXT("松开 E 释放魔方抓取和本次移动锁"),
		Controller->IsDraggingInteraction() || Controller->IsMoveInputIgnored());
	TestTrue(TEXT("松开 E 保留其它系统已有视角锁"), Controller->IsLookInputIgnored());
	Controller->SetIgnoreLookInput(false);
	Scene.Cube->ResetCube();

	// 复用真实表现组件生成的面片和捕获相机，覆盖手办完整射线映射与控制器分发路径。
	UDreamShoulderCameraComponent* Shoulder = CastChecked<UDreamShoulderCameraComponent>(Character->CameraBoom);
	Shoulder->Activate(true);
	Shoulder->TickComponent(0.0f, LEVELTICK_All, nullptr);
	Scene.World->SpawnActor<ADreamSceneCaptureAnchor>();
	UDreamSceneCapturePresentationComponent* Miniature = Character->SceneMiniature;
	Miniature->RenderTargetWidth = 256;
	Miniature->RenderTargetHeight = 256;
	Miniature->BeginPlay();
	UStaticMeshComponent* Display = nullptr;
	TArray<UStaticMeshComponent*> Meshes;
	Character->GetComponents(Meshes);
	for (UStaticMeshComponent* Mesh : Meshes)
		if (Mesh->GetFName() == TEXT("SceneCaptureDisplayMesh"))
			Display = Mesh;
	ASceneCapture2D* Capture = nullptr;
	for (AActor* Actor : Scene.World->PersistentLevel->Actors)
		if (ASceneCapture2D* Candidate = Cast<ASceneCapture2D>(Actor))
			Capture = Candidate;
	if (!TestNotNull(TEXT("表现组件生成真实显示面"), Display) ||
		!TestNotNull(TEXT("表现组件生成真实捕获相机"), Capture))
		return false;
	Scene.Owner->SetActorLocation(FVector(12000, 26, 1026));
	Capture->SetActorLocationAndRotation(FVector(10000, 0, 1000), FRotator::ZeroRotator);
	Display->SetWorldTransform(
		FTransform(FRotationMatrix::MakeFromXZ(FVector::RightVector, -FVector::ForwardVector).ToQuat(),
			FVector(0, 300, 100), FVector(0.8)));
	const FVector Camera = Display->GetComponentLocation() - FVector(350, 0, 0);
	Controller->bMiniatureInteractionMode = true;
	Controller->InteractWithMiniatureRay(Camera, Display->GetComponentLocation() - Camera);
	TestTrue(TEXT("手办左键真实映射射线命中并开始魔方交互"), Controller->IsDraggingInteraction());
	// 256 像素 RT 的反投影会取整，足够的 UV 位移才对应超过魔方 5 cm 的实际面内阈值。
	const FVector DisplayPoint = Display->GetComponentTransform().TransformPosition(FVector(4, 0, 0));
	FVector DisplayHit, Origin, Direction;
	FString Reason;
	TestTrue(TEXT("手办相邻显示像素能得到捕获世界射线"),
		Miniature->TryMapViewRayToCaptureRay(Camera, DisplayPoint - Camera, DisplayHit, Origin, Direction, Reason));
	Scene.Cube->UpdateDrag(Origin, Direction, FVector2D(5, 0));
	TestEqual(TEXT("映射后的持续射线实际提交魔方转动"), Scene.Cube->GetMoveCount(), 1);
	Controller->EndMiniatureDrag();
	TestFalse(TEXT("手办松开左键释放拖动锁"), Controller->IsDraggingInteraction() || Controller->IsMoveInputIgnored());
	Scene.Cube->ResetCube();
	Controller->InteractWithMiniatureRay(Camera, Display->GetComponentLocation() - Camera);
	Scene.Cube->Deactivate();
	Controller->UpdateActiveDrag();
	TestFalse(TEXT("停用目标后控制器释放所有本次交互锁"),
		Controller->IsDraggingInteraction() || Controller->IsMoveInputIgnored() || Controller->IsLookInputIgnored());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRubiksEventsTest,
	"DreamSpace.Puzzle.RubiksCube.BlueprintVictoryOnlyOnCompletedSolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRubiksEventsTest::RunTest(const FString& Parameters)
{
	FRubiksFixture Scene;
	// 瞬时世界没有正常 Actor 初始化，AActor::ProcessEvent 默认阻止脚本事件；仅在本测试中
	// 使用引擎编辑器执行保护开关。离开作用域自动恢复，不改变其它测试或运行时组件的事件规则。
	TGuardValue<bool> AllowDynamicEvents(GAllowActorScriptExecutionInEditor, true);
	AActor* VictoryReceiver = Scene.World->SpawnActor<AActor>();
	AActor* StateIndicator = Scene.World->SpawnActor<AActor>();
	// 使用现有带 UFUNCTION 的 Actor 方法接收动态蓝图事件，避免为测试新增运行时反射类。
	Scene.Cube->OnCubeSolved.AddDynamic(VictoryReceiver, &AActor::K2_DestroyActor);
	Scene.Cube->OnSolvedStateChanged.AddDynamic(StateIndicator, &AActor::SetActorHiddenInGame);
	Scene.Cube->RotateLayer(EDreamRubiksCubeAxis::X, 1, 1, false);
	TestFalse(TEXT("转到未还原时状态事件传入 false"), StateIndicator->IsHidden());
	Scene.Cube->ResetCube();
	TestTrue(TEXT("复位时状态事件传入 true"), StateIndicator->IsHidden());
	TestTrue(TEXT("复位不触发玩家通关事件"), IsValid(VictoryReceiver));
	Scene.Cube->ShuffleCube();
	TestTrue(TEXT("打乱也不触发玩家通关事件"), IsValid(VictoryReceiver));
	Scene.Cube->ResetCube();
	Scene.Cube->RotateLayer(EDreamRubiksCubeAxis::X, 1, 1, false);
	Scene.Cube->UndoLastMove(true);
	Scene.Cube->TickComponent(0.05f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("还原动画途中不提前触发通关"), IsValid(VictoryReceiver));
	Scene.Cube->TickComponent(1.0f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("真实还原完成后动态事件确实到达蓝图接收者"), IsValid(VictoryReceiver));
	TestTrue(TEXT("真实还原同时更新状态指示"), StateIndicator->IsHidden());
	return true;
}
#endif
