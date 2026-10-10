#include "DreamProximitySketchCameraManager.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SceneComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamProximitySketchLifecycleTest,
	"DreamSpace.Presentation.ProximitySketch.PlayerLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamProximitySketchLifecycleTest::RunTest(const FString&)
{
	// 使用两个实际控制器和两次占有，验证世界位置传递、实例隔离和后处理缓存。
	// 不重写 smoothstep 做自证；半径边界、平面过渡和 GPU 像素另由真实截图验收。
	IConsoleVariable* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("dream.Sketch.Enabled"));
	const int32 PreviousEnabled = Enabled->GetInt();
	const uint32 PreviousFlags = Enabled->GetFlags();
	Enabled->Set(1, ECVF_SetByConsole);
	const auto RestoreConsole = [&]()
	{
		// 完整还原数值和优先级，测试在正常编辑器里运行时也不留下控制台覆盖。
		Enabled->SetFlags(static_cast<EConsoleVariableFlags>(PreviousFlags));
		Enabled->Set(PreviousEnabled, static_cast<EConsoleVariableFlags>(PreviousFlags & ECVF_SetByMask));
	};
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	APlayerController* First = World->SpawnActor<APlayerController>();
	APlayerController* Second = World->SpawnActor<APlayerController>();
	// UE 5.8 以 ULocalPlayer 判断没有 NetDriver 的控制器是否属于本机。
	// 瞬时测试世界也明确安装本地玩家，不能依赖 Standalone 的隐式判定。
	First->Player = NewObject<ULocalPlayer>(GEngine);
	Second->Player = NewObject<ULocalPlayer>(GEngine);
	ADreamProximitySketchCameraManager* FirstCamera = World->SpawnActor<ADreamProximitySketchCameraManager>();
	ADreamProximitySketchCameraManager* SecondCamera = World->SpawnActor<ADreamProximitySketchCameraManager>();
	FirstCamera->PCOwner = First;
	SecondCamera->PCOwner = Second;
	APawn* FirstPawn = World->SpawnActor<APawn>();
	APawn* Replacement = World->SpawnActor<APawn>();
	const FVector FirstPosition(320.0, -140.0, 720.0);
	const FVector SecondPosition(-1100.0, 200.0, 50.0);
	// 空 APawn 没有 RootComponent，测试显式创建一个根，使世界变换能真正生效。
	for (APawn* Pawn : { FirstPawn, Replacement })
	{
		USceneComponent* Root = NewObject<USceneComponent>(Pawn);
		Pawn->SetRootComponent(Root);
		Pawn->AddInstanceComponent(Root);
		Root->RegisterComponent();
	}
	FirstPawn->SetActorLocation(FirstPosition);
	Replacement->SetActorLocation(SecondPosition);
	First->Possess(FirstPawn);
	Second->Possess(Replacement);
	FMinimalViewInfo View;
	FirstCamera->ApplyCameraModifiers(0.016f, View);
	SecondCamera->ApplyCameraModifiers(0.016f, View);
	UMaterialInstanceDynamic* Instance = FirstCamera->GetSketchMaterialInstance();
	if (!TestNotNull(TEXT("有 Pawn 时创建距离线稿动态实例"), Instance))
	{
		World->DestroyWorld(false);
		RestoreConsole();
		return false;
	}
	TestNotEqual(TEXT("不同本地玩家不共享动态实例"), Instance, SecondCamera->GetSketchMaterialInstance());
	const FLinearColor PositionValue = Instance->K2_GetVectorParameterValue(TEXT("PlayerPosition"));
	TestTrue(TEXT("中心来自 Pawn 的 XYZ，含垂直高度"), FVector(PositionValue.R, PositionValue.G, PositionValue.B).Equals(FirstPosition, 0.01));
	const TArray<FPostProcessSettings>* Settings = nullptr;
	const TArray<float>* Weights = nullptr;
	FirstCamera->GetCachedPostProcessBlends(Settings, Weights);
	TestEqual(TEXT("仅向此相机添加一次后处理"), Settings->Num(), 1);
	TestTrue(TEXT("缓存引用当前玩家的实例"), (*Settings)[0].WeightedBlendables.Array[0].Object == Instance);
	TestEqual(TEXT("缓存同时包含距离受限的线稿平滑"), (*Settings)[0].WeightedBlendables.Array.Num(), 2);
	TestNotNull(TEXT("平滑有独立的动态实例"), FirstCamera->SmoothingInstance.Get());

	// 镜头移动不改变球心；连续调用不累积 blendable。被取消占有时则立即停止提交。
	View.Location = FVector(9900.0, 800.0, 4000.0);
	FirstCamera->ApplyCameraModifiers(0.016f, View);
	FirstCamera->GetCachedPostProcessBlends(Settings, Weights);
	TestEqual(TEXT("每帧缓存仍只有一次后处理"), Settings->Num(), 1);
	TestTrue(TEXT("镜头远离时球心仍为 Pawn"), Instance->K2_GetVectorParameterValue(TEXT("PlayerPosition")).Equals(PositionValue, 0.001f));
	First->UnPossess();
	FirstCamera->ApplyCameraModifiers(0.016f, View);
	FirstCamera->GetCachedPostProcessBlends(Settings, Weights);
	TestEqual(TEXT("失去 Pawn 后不提交后处理"), Settings->Num(), 0);
	TestEqual(TEXT("失去 Pawn 后实例权重归零"), Instance->K2_GetScalarParameterValue(TEXT("EffectStrength")), 0.0f);

	Second->UnPossess();
	First->Possess(Replacement);
	FirstCamera->ApplyCameraModifiers(0.016f, View);
	const FLinearColor ReplacedPosition = Instance->K2_GetVectorParameterValue(TEXT("PlayerPosition"));
	TestTrue(TEXT("换角色后同一实例更新为新 Pawn 位置"), FVector(ReplacedPosition.R, ReplacedPosition.G, ReplacedPosition.B).Equals(SecondPosition, 0.01));
	Enabled->Set(0, ECVF_SetByConsole);
	FirstCamera->ApplyCameraModifiers(0.016f, View);
	FirstCamera->GetCachedPostProcessBlends(Settings, Weights);
	TestEqual(TEXT("关闭开关完全移除当前帧通道"), Settings->Num(), 0);
	RestoreConsole();
	World->DestroyWorld(false);
	return true;
}

namespace
{
/**
 * 真实 GPU 验收用：固定当前镜头，再平移 Pawn，证明遮罩围绕玩家而非摄像机。
 * Python 不公开运行世界的 SpawnActor，所以用仅存在于开发测试构建的命令。
 * 摄像机与位移只存在于 PIE 世界，结束运行即销毁，命令不保存地图或材质。
 */
FAutoConsoleCommandWithWorldAndArgs MoveSketchPawnCommand(
	TEXT("dream.TestSketchMovePawn"), TEXT("开发验收：固定相机，将 Pawn 平移指定的 X Y Z 厘米。"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		APlayerCameraManager* Manager = PC ? PC->PlayerCameraManager.Get() : nullptr;
		if (!Pawn || !Cast<ADreamProximitySketchCameraManager>(Manager) || Args.Num() != 3)
			return;
		ACameraActor* Camera = World->SpawnActor<ACameraActor>(Manager->GetCameraLocation(), Manager->GetCameraRotation());
		if (!Camera)
			return;
		Camera->GetCameraComponent()->SetFieldOfView(Manager->GetFOVAngle());
		// 复用玩家当前的自由视口比例，避免 CameraActor 默认的 16:9 添加黑边、
		// 改变横向构图而干扰“固定相机、移动球心”的像素对照。
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		PC->SetViewTarget(Camera);
		Pawn->SetActorLocation(Pawn->GetActorLocation()
			+ FVector(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), FCString::Atod(*Args[2])),
			false, nullptr, ETeleportType::TeleportPhysics);
	}));
}
#endif
