#include "DreamWorldRecoveryComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/CameraActor.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "DreamPlayerController.h"
#include "DreamProximitySketchCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"

namespace
{
/** 临时覆盖线稿调试值，结束时连同原有优先级一起还原，避免污染后续 PIE 和其它测试。 */
struct FWorldRecoveryConsoleScope
{
	struct FOriginal
	{
		IConsoleVariable* Variable;
		FString Value;
		EConsoleVariableFlags Flags;
	};
	TArray<FOriginal> Originals;

	FWorldRecoveryConsoleScope()
	{
		for (const TCHAR* Name : { TEXT("Enabled"), TEXT("Radius"), TEXT("Transition"), TEXT("Debug") })
		{
			IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(
				*(FString(TEXT("dream.Sketch.")) + Name));
			check(Variable);
			Originals.Add({ Variable, Variable->GetString(), Variable->GetFlags() });
			Variable->Set(FCString::Strcmp(Name, TEXT("Enabled")) == 0 ? 1 : -1, ECVF_SetByConsole);
		}
	}

	~FWorldRecoveryConsoleScope()
	{
		for (const FOriginal& Original : Originals)
		{
			Original.Variable->SetFlags(static_cast<EConsoleVariableFlags>(Original.Flags));
			Original.Variable->Set(*Original.Value, static_cast<EConsoleVariableFlags>(Original.Flags & ECVF_SetByMask));
		}
	}
};

/**
 * 瞬时测试世界里使用真实控制器、Pawn、相机和可查询碰撞，验证接口最终送入两个 MID 的参数。
 * 不保存关卡、蓝图或材质；人工观感仍需在 TEST 中用正式美术资源验收。
 */
struct FWorldRecoveryFixture
{
	FWorldRecoveryConsoleScope Console;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ADreamPlayerController* Controller = nullptr;
	APawn* Pawn = nullptr;
	ADreamProximitySketchCameraManager* Camera = nullptr;
	AActor* Target = nullptr;
	UDreamWorldRecoveryComponent* Recovery = nullptr;

	FWorldRecoveryFixture()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		CreatePlayer(Controller, Pawn, Camera);
		Target = CreateBoxActor(FVector(500, 0, 100), FVector(30, 30, 30));
		Recovery = AddRecovery(Target);
	}

	APawn* CreatePawn()
	{
		APawn* Result = World->SpawnActor<APawn>();
		USceneComponent* Root = NewObject<USceneComponent>(Result);
		Result->SetRootComponent(Root);
		Result->AddInstanceComponent(Root);
		Root->RegisterComponent();
		Result->SetActorLocation(FVector(0, 0, 100));
		return Result;
	}

	void CreatePlayer(ADreamPlayerController*& OutController, APawn*& OutPawn,
		ADreamProximitySketchCameraManager*& OutCamera)
	{
		OutController = World->SpawnActor<ADreamPlayerController>();
		// UE 5.8 的本地控制器判定依赖 ULocalPlayer；隔离世界显式补齐游戏初始化时通常创建的对象。
		OutController->Player = NewObject<ULocalPlayer>(GEngine);
		World->AddController(OutController);
		OutController->SpawnPlayerCameraManager();
		OutPawn = CreatePawn();
		OutController->Possess(OutPawn);
		OutController->SetViewTarget(OutPawn);
		OutCamera = CastChecked<ADreamProximitySketchCameraManager>(OutController->PlayerCameraManager);
		OutCamera->NormalRadius = 600.0f;
		OutCamera->EffectStrength = 1.0f;
		OutCamera->bEnableProximitySketch = true;
		OutCamera->UpdateCamera(0.0f);
	}

	AActor* CreateBoxActor(const FVector& Location, const FVector& Extent)
	{
		AActor* Result = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Result);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Result->SetRootComponent(Box);
		Result->AddInstanceComponent(Box);
		Box->RegisterComponent();
		Result->SetActorLocation(Location);
		return Result;
	}

	UDreamWorldRecoveryComponent* AddRecovery(AActor* Actor)
	{
		UDreamWorldRecoveryComponent* Result = NewObject<UDreamWorldRecoveryComponent>(Actor);
		Actor->AddInstanceComponent(Result);
		Result->RegisterComponent();
		// 未执行 Actor 初始化的世界不会自动激活原生组件，显式模拟正常游戏中的 AutoActivate。
		Result->Activate(true);
		return Result;
	}

	void AimAtTarget()
	{
		// 注入确定的相机缓存，但交互仍走控制器的真实 Visibility 射线和父 Actor 组件分发。
		World->TimeSeconds = 1.0;
		Camera->UpdateCamera(0.0f);
		FMinimalViewInfo View;
		View.Location = Target->GetActorLocation() - FVector(150, 0, 0);
		View.Rotation = FRotator::ZeroRotator;
		Camera->SetCameraCachePOV(View);
	}

	UMaterialInstanceDynamic* Smoothing() const
	{
		const TArray<FPostProcessSettings>* Settings = nullptr;
		const TArray<float>* Weights = nullptr;
		Camera->GetCachedPostProcessBlends(Settings, Weights);
		if (!Settings || Settings->IsEmpty() || (*Settings)[0].WeightedBlendables.Array.Num() != 2)
			return nullptr;
		return Cast<UMaterialInstanceDynamic>((*Settings)[0].WeightedBlendables.Array[1].Object);
	}

	~FWorldRecoveryFixture()
	{
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldRecoveryInteractionTest,
	"DreamSpace.Puzzle.WorldRecovery.WorldTraceAndPlayerIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldRecoveryInteractionTest::RunTest(const FString&)
{
	FWorldRecoveryFixture Scene;
	Scene.Recovery->TargetNormalRadius = 1800.0f;
	Scene.Recovery->RecoveryDuration = 2.0f;
	Scene.Recovery->bRestoreEntireWorld = false;
	ADreamPlayerController* SecondController = nullptr;
	APawn* SecondPawn = nullptr;
	ADreamProximitySketchCameraManager* SecondCamera = nullptr;
	Scene.CreatePlayer(SecondController, SecondPawn, SecondCamera);

	// 墙体遮挡必须仍然阻止世界交互；组件不能为了找到玩家而绕过控制器已有的射线约束。
	AActor* Wall = Scene.CreateBoxActor(Scene.Target->GetActorLocation() - FVector(90, 0, 0), FVector(10, 100, 100));
	// 给测试墙体挂一个停用的同类组件，避免控制器把“预期被墙挡住”的射线记录成无交互目标警告；
	// 组件停用后不会响应，仍能覆盖真正的遮挡路径。
	UDreamWorldRecoveryComponent* WallRecovery = Scene.AddRecovery(Wall);
	WallRecovery->Deactivate();
	Scene.AimAtTarget();
	Scene.Controller->Interact();
	TestFalse(TEXT("隔墙按 E 不触发恢复，也不消耗单次机会"), Scene.Recovery->HasTriggeredWorldRecovery());
	Wall->Destroy();
	Scene.Recovery->Deactivate();
	Scene.AimAtTarget();
	Scene.Controller->Interact();
	TestFalse(TEXT("停用组件不响应已有交互接口"), Scene.Recovery->HasTriggeredWorldRecovery());
	Scene.Recovery->Activate(true);
	Scene.AimAtTarget();
	Scene.Controller->Interact();
	TestTrue(TEXT("父 Actor 的实际 E 射线自动分发到世界恢复组件"), Scene.Recovery->HasTriggeredWorldRecovery());
	TestTrue(TEXT("恢复请求交给当前玩家相机"), Scene.Camera->IsWorldRecoveryActive());
	TestFalse(TEXT("请求不锁定角色移动"), Scene.Controller->IsMoveInputIgnored());
	TestFalse(TEXT("请求不锁定角色视角"), Scene.Controller->IsLookInputIgnored());
	TestFalse(TEXT("重复调用不再次消耗或重启动画"), Scene.Recovery->TriggerWorldRecovery(Scene.Pawn));
	Scene.Camera->UpdateCamera(1.0f);
	UMaterialInstanceDynamic* Main = Scene.Camera->GetSketchMaterialInstance();
	UMaterialInstanceDynamic* Smoothing = Scene.Smoothing();
	if (!TestNotNull(TEXT("主通道存在本地动态实例"), Main)
		|| !TestNotNull(TEXT("范围受限的平滑通道仍存在"), Smoothing))
		return false;
	TestEqual(TEXT("一半时长真正写入 1200 cm 半径"), Main->K2_GetScalarParameterValue(TEXT("NormalRadius")), 1200.0f);
	TestEqual(TEXT("平滑通道使用完全相同的半径"), Smoothing->K2_GetScalarParameterValue(TEXT("NormalRadius")), 1200.0f);
	TestTrue(TEXT("球心仍来自玩家 XYZ，不是机关位置"),
		Main->K2_GetVectorParameterValue(TEXT("PlayerPosition")).Equals(FLinearColor(0, 0, 100, 0), 0.001f));
	SecondCamera->UpdateCamera(1.0f);
	TestEqual(TEXT("另一位本地玩家保持原半径"),
		SecondCamera->GetSketchMaterialInstance()->K2_GetScalarParameterValue(TEXT("NormalRadius")), 600.0f);
	TestFalse(TEXT("另一位玩家没有共享恢复进度"), SecondCamera->IsWorldRecoveryActive());

	// 组件不持有动画所有权：触发后销毁机关，恢复仍精确完成，并保持有限目标下的外圈线稿。
	Scene.Target->Destroy();
	Scene.Camera->UpdateCamera(5.0f);
	TestTrue(TEXT("机关销毁后动画仍完成"), Scene.Camera->HasWorldRecoveryFinished());
	TestEqual(TEXT("有限恢复精确落到目标半径"), Main->K2_GetScalarParameterValue(TEXT("NormalRadius")), 1800.0f);
	TestEqual(TEXT("阶段扩张保留外圈线稿"), Main->K2_GetScalarParameterValue(TEXT("EffectStrength")), 1.0f);
	TestEqual(TEXT("原配置半径没有被写回"), Scene.Camera->NormalRadius, 600.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldRecoveryAnimationTest,
	"DreamSpace.Puzzle.WorldRecovery.ViewBlendAndEntireWorldFade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldRecoveryAnimationTest::RunTest(const FString&)
{
	FWorldRecoveryFixture Scene;
	TestTrue(TEXT("接受默认全世界恢复请求"), Scene.Camera->StartWorldRecovery(10000.0f, 4.0f));
	// 视角混合会计算当前与待进入的两个 ViewTarget。本测试使用引擎真正的相机混合入口，
	// 确认插值不会因为每帧两次 ApplyCameraModifiers 而加速，不单独测试一份复制的公式。
	ACameraActor* NextView = Scene.World->SpawnActor<ACameraActor>();
	NextView->SetActorLocation(FVector(1500, 200, 900));
	Scene.Controller->SetViewTargetWithBlend(NextView, 4.0f, VTBlend_Linear, 0.0f, false);
	Scene.Camera->UpdateCamera(2.0f);
	UMaterialInstanceDynamic* Main = Scene.Camera->GetSketchMaterialInstance();
	if (!TestNotNull(TEXT("混合视角时主通道有效"), Main))
		return false;
	TestEqual(TEXT("两个 ViewTarget 只推进一次，半程为 5300 cm"), Main->K2_GetScalarParameterValue(TEXT("NormalRadius")), 5300.0f);
	TestTrue(TEXT("半程仍在动画中"), Scene.Camera->IsWorldRecoveryActive());
	TestEqual(TEXT("前 75% 主要通过球形范围扩张恢复"), Main->K2_GetScalarParameterValue(TEXT("EffectStrength")), 1.0f);
	Scene.Camera->UpdateCamera(0.0f);
	TestEqual(TEXT("零游戏时间不推进恢复"), Scene.Camera->GetEffectiveNormalRadius(), 5300.0f);
	Scene.Camera->UpdateCamera(1.5f);
	TestEqual(TEXT("末段平滑淡出剩余天空与远景线稿"), Main->K2_GetScalarParameterValue(TEXT("EffectStrength")), 0.5f);
	UMaterialInstanceDynamic* Smoothing = Scene.Smoothing();
	if (TestNotNull(TEXT("淡出时保留配套平滑实例"), Smoothing))
		TestEqual(TEXT("两个通道同步淡出"), Smoothing->K2_GetScalarParameterValue(TEXT("EffectStrength")), 0.5f);
	Scene.Camera->UpdateCamera(0.5f);
	const TArray<FPostProcessSettings>* Settings = nullptr;
	const TArray<float>* Weights = nullptr;
	Scene.Camera->GetCachedPostProcessBlends(Settings, Weights);
	TestEqual(TEXT("全世界恢复后不再提交线稿通道"), Settings->Num(), 0);
	TestEqual(TEXT("保留的瞬时 MID 权重清零"), Main->K2_GetScalarParameterValue(TEXT("EffectStrength")), 0.0f);
	TestTrue(TEXT("动画结束可由蓝图查询"), Scene.Camera->HasWorldRecoveryFinished());
	TestFalse(TEXT("已恢复世界不能被新请求退回线稿"), Scene.Camera->StartWorldRecovery(20000.0f, 1.0f, false));
	TestTrue(TEXT("全局线稿默认开关没有被改写"), Scene.Camera->bEnableProximitySketch);
	TestEqual(TEXT("美术配置强度没有被改写"), Scene.Camera->EffectStrength, 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldRecoveryContinuationTest,
	"DreamSpace.Puzzle.WorldRecovery.ContinuationAndLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldRecoveryContinuationTest::RunTest(const FString&)
{
	FWorldRecoveryFixture Scene;
	TestFalse(TEXT("空交互者不消耗机会"), Scene.Recovery->TriggerWorldRecovery(nullptr));
	APawn* Unpossessed = Scene.CreatePawn();
	TestFalse(TEXT("未占有的 Pawn 不触发恢复"), Scene.Recovery->TriggerWorldRecovery(Unpossessed));
	TestFalse(TEXT("失败请求保持组件未使用"), Scene.Recovery->HasTriggeredWorldRecovery());
	Scene.Recovery->TargetNormalRadius = 1800.0f;
	Scene.Recovery->RecoveryDuration = 2.0f;
	Scene.Recovery->bRestoreEntireWorld = false;
	TestTrue(TEXT("有效阶段请求开始恢复"), Scene.Recovery->TriggerWorldRecovery(Scene.Pawn));
	Scene.Camera->UpdateCamera(1.0f);
	const float MidRadius = Scene.Camera->GetEffectiveNormalRadius();
	TestFalse(TEXT("相同目标不重置进度"), Scene.Camera->StartWorldRecovery(1800.0f, 20.0f, false));
	TestFalse(TEXT("更小目标不会覆盖已有较大的恢复目标"), Scene.Camera->StartWorldRecovery(1500.0f, 1.0f, false));
	TestEqual(TEXT("失败请求不改变当前半径"), Scene.Camera->GetEffectiveNormalRadius(), MidRadius);
	TestTrue(TEXT("更大目标从当前半径继续"), Scene.Camera->StartWorldRecovery(3000.0f, 2.0f, false));
	TestEqual(TEXT("重新请求的第一帧没有跳变"), Scene.Camera->GetEffectiveNormalRadius(), MidRadius);
	Scene.Controller->UnPossess();
	Scene.Camera->UpdateCamera(10.0f);
	TestEqual(TEXT("失去 Pawn 时暂停恢复"), Scene.Camera->GetEffectiveNormalRadius(), MidRadius);
	TestFalse(TEXT("失去 Pawn 时不接受新目标"), Scene.Camera->StartWorldRecovery(4000.0f, 1.0f, false));
	Scene.Controller->Possess(Unpossessed);
	Scene.Controller->SetViewTarget(Unpossessed);
	Scene.Camera->UpdateCamera(2.0f);
	TestEqual(TEXT("换角色后继续完成当前目标"), Scene.Camera->GetEffectiveNormalRadius(), 3000.0f);
	TestTrue(TEXT("同半径可升级为全世界恢复"), Scene.Camera->StartWorldRecovery(3000.0f, 4.0f, true));
	Scene.Camera->UpdateCamera(3.5f);
	const float FadedStrength = Scene.Camera->GetSketchMaterialInstance()->K2_GetScalarParameterValue(TEXT("EffectStrength"));
	TestTrue(TEXT("末段可接受更大半径"), Scene.Camera->StartWorldRecovery(6000.0f, 4.0f, false));
	Scene.Camera->UpdateCamera(1.0f);
	TestEqual(TEXT("重入不会把已淡出的线稿重新变浓"),
		Scene.Camera->GetSketchMaterialInstance()->K2_GetScalarParameterValue(TEXT("EffectStrength")), FadedStrength);
	Scene.Camera->UpdateCamera(4.0f);
	TestEqual(TEXT("后来有限扩张不能撤销已经开始的全世界恢复"),
		Scene.Camera->GetSketchMaterialInstance()->K2_GetScalarParameterValue(TEXT("EffectStrength")), 0.0f);

	// 使用第二位玩家验证调试半径作为起点、零时长和参数检查，同时保持两份结果独立。
	ADreamPlayerController* OtherController = nullptr;
	APawn* OtherPawn = nullptr;
	ADreamProximitySketchCameraManager* OtherCamera = nullptr;
	Scene.CreatePlayer(OtherController, OtherPawn, OtherCamera);
	IConsoleVariable* Radius = IConsoleManager::Get().FindConsoleVariable(TEXT("dream.Sketch.Radius"));
	Radius->Set(900.0f, ECVF_SetByConsole);
	TestEqual(TEXT("恢复前读取实际控制台半径"), OtherCamera->GetEffectiveNormalRadius(), 900.0f);
	TestFalse(TEXT("负半径请求被拒绝"), OtherCamera->StartWorldRecovery(-10.0f, 1.0f, false));
	TestTrue(TEXT("零时长有限恢复立即到达"), OtherCamera->StartWorldRecovery(1500.0f, 0.0f, false));
	OtherCamera->UpdateCamera(0.0f);
	TestEqual(TEXT("恢复半径不会被残留控制台值锁住"),
		OtherCamera->GetSketchMaterialInstance()->K2_GetScalarParameterValue(TEXT("NormalRadius")), 1500.0f);
	TestEqual(TEXT("交互不改写共享控制台值"), Radius->GetFloat(), 900.0f);
	TestTrue(TEXT("零时长全世界恢复立即完成"), OtherCamera->StartWorldRecovery(1500.0f, 0.0f, true));
	OtherCamera->UpdateCamera(0.0f);
	TestFalse(TEXT("零时长结束后无未完成动画"), OtherCamera->IsWorldRecoveryActive());
	TestEqual(TEXT("零时长也清零线稿通道"),
		OtherCamera->GetSketchMaterialInstance()->K2_GetScalarParameterValue(TEXT("EffectStrength")), 0.0f);
	return true;
}
#endif
