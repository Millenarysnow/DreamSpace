#include "DreamKeyPickupComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DreamCharacter.h"
#include "DreamDroppedKey.h"
#include "DreamHUD.h"
#include "DreamMiniatureExtractableComponent.h"
#include "DreamPlayerController.h"
#include "DreamSceneCapturePresentationComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace
{
/** 用瞬时世界完成取出与拾取，不保存用户资产；所有 Actor 与 WorldContext 在测试结束时清理。 */
struct FKeyPickupFixture
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AActor* Source = nullptr;
	UStaticMeshComponent* SourceMesh = nullptr;
	UDreamMiniatureExtractableComponent* Extract = nullptr;
	ADreamCharacter* Character = nullptr;
	ADreamPlayerController* Controller = nullptr;
	ADreamHUD* HUD = nullptr;
	const FVector DisplayPoint = FVector(0, 300, 500);

	FKeyPickupFixture()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Source = World->SpawnActor<AActor>();
		SourceMesh = NewObject<UStaticMeshComponent>(Source);
		SourceMesh->SetMobility(EComponentMobility::Movable);
		SourceMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		SourceMesh->SetCollisionProfileName(TEXT("BlockAll"));
		Source->SetRootComponent(SourceMesh);
		Source->AddInstanceComponent(SourceMesh);
		SourceMesh->RegisterComponent();
		Source->SetActorLocation(FVector(12000, 0, 1000));
		Extract = NewObject<UDreamMiniatureExtractableComponent>(Source);
		Source->AddInstanceComponent(Extract);
		Extract->RegisterComponent();
		Extract->Activate(true);
		// 模拟详情面板选择钥匙掉落物类，随后走真实的 BeginExtract/EndExtract 路径。
		FClassProperty* DropClass = FindFProperty<FClassProperty>(Extract->GetClass(), TEXT("DropActorClass"));
		check(DropClass);
		DropClass->SetObjectPropertyValue_InContainer(Extract, ADreamDroppedKey::StaticClass());

		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Character = World->SpawnActor<ADreamCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
		Controller = World->SpawnActor<ADreamPlayerController>();
		Controller->Player = NewObject<ULocalPlayer>(GEngine);
		// 与 HUD 相同，隔离世界需显式补齐 PostInitializeComponents 通常创建的相机管理器。
		Controller->SpawnPlayerCameraManager();
		Controller->Possess(Character);
		// 隔离世界没有客户端 RPC 生命周期，直接执行引擎创建 HUD 的实现，仍通过 GetHUD 验证角色通知。
		Controller->ClientSetHUD_Implementation(ADreamHUD::StaticClass());
		HUD = CastChecked<ADreamHUD>(Controller->GetHUD());
	}

	bool Begin()
	{
		if (!Extract->BeginExtract(Character, SourceMesh, DisplayPoint, -FVector::ForwardVector))
			return false;
		// 未初始化 Actor 的瞬时世界不会执行原生组件 AutoActivate，显式模拟游戏中的激活状态。
		Extract->GetPreviewItem()->FindComponentByClass<UDreamKeyPickupComponent>()->Activate(true);
		return true;
	}

	ADreamDroppedKey* Commit()
	{
		if (!Begin())
			return nullptr;
		ADreamDroppedKey* Key = Cast<ADreamDroppedKey>(Extract->GetPreviewItem());
		Extract->UpdateExtract(DisplayPoint + FVector(0, 90, 0), FVector2D(1.2, 0.5), -FVector::ForwardVector);
		return Extract->EndExtract(true) ? Key : nullptr;
	}

	void AimAt(AActor* Target)
	{
		// 注入确定的相机缓存，Interact 仍执行正常的世界射线及接口分发，不直接绕过控制器拾取。
		FMinimalViewInfo View;
		View.Location = Target->GetActorLocation() - FVector(150, 0, 0);
		View.Rotation = FRotator::ZeroRotator;
		World->TimeSeconds = 1.0;
		Controller->PlayerCameraManager->UpdateCamera(0.0f);
		Controller->PlayerCameraManager->SetCameraCachePOV(View);
	}

	~FKeyPickupFixture()
	{
		if (IsValid(Extract))
			Extract->EndExtract(false);
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamKeyPickupControllerTest,
	"DreamSpace.Puzzle.KeyPickup.ExtractThenWorldEConsumesKeyAndShowsTimedMessage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamKeyPickupControllerTest::RunTest(const FString& Parameters)
{
	FKeyPickupFixture Scene;
	TestFalse(TEXT("角色初始没有钥匙"), Scene.Character->bHasKey);
	TestFalse(TEXT("未拾取时没有中央提示"), Scene.HUD->IsKeyAcquiredMessageVisible());
	ADreamDroppedKey* Key = Scene.Commit();
	if (!TestNotNull(TEXT("取出组件生成真实的钥匙掉落物"), Key))
		return false;
	TestFalse(TEXT("提交后移除原房间模型"), IsValid(Scene.Source));
	USphereComponent* Collision = Key->FindComponentByClass<USphereComponent>();
	TestTrue(TEXT("钥匙掉落保留物理模拟"), Collision->IsSimulatingPhysics());
	TestEqual(TEXT("掉落钥匙可被默认 E 射线命中"), Collision->GetCollisionResponseToChannel(ECC_Visibility), ECR_Block);
	TestEqual(TEXT("掉落钥匙不挤动第三人称相机"), Collision->GetCollisionResponseToChannel(ECC_Camera), ECR_Ignore);
	TArray<AActor*> HiddenActors;
	Scene.Character->SceneMiniature->GetCaptureHiddenActors(HiddenActors);
	TestTrue(TEXT("手办捕获和捕获拾取都排除掉落钥匙"), HiddenActors.Contains(Key));
	Scene.AimAt(Key);

	Scene.Controller->SetMiniatureInteractionMode(true);
	Scene.Controller->Interact();
	TestTrue(TEXT("手办模式按 E 不会领取世界钥匙"), IsValid(Key) && !Scene.Character->bHasKey);
	Scene.Controller->SetMiniatureInteractionMode(false);
	// 在相机与钥匙之间放真实阻挡盒，验证拾取不能穿墙。
	AActor* Wall = Scene.World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
	Box->SetMobility(EComponentMobility::Movable);
	Box->SetBoxExtent(FVector(10));
	Box->SetCollisionProfileName(TEXT("BlockAll"));
	Wall->SetRootComponent(Box);
	Wall->AddInstanceComponent(Box);
	Box->RegisterComponent();
	Wall->SetActorLocation(Key->GetActorLocation() - FVector(75, 0, 0));
	AddExpectedMessagePlain(TEXT("没有实现 IDreamInteractableInterface"), ELogVerbosity::Warning);
	Scene.Controller->Interact();
	TestTrue(TEXT("墙体挡住 E 射线时钥匙仍留在世界"), IsValid(Key) && !Scene.Character->bHasKey);
	TestFalse(TEXT("未成功拾取不能显示获得提示"), Scene.HUD->IsKeyAcquiredMessageVisible());
	Wall->Destroy();

	Scene.Controller->Interact();
	TestTrue(TEXT("通过真实 E 射线与接口分发设置人物持有 bool"), Scene.Character->bHasKey);
	TestFalse(TEXT("成功拾取后掉落钥匙从世界移除"), IsValid(Key));
	TestTrue(TEXT("成功拾取时中央提示处于有效期"), Scene.HUD->IsKeyAcquiredMessageVisible());
	// 直接推进测试世界时钟，不阻塞自动化等待；重复按 E 不能刷新已消费钥匙的提示。
	Scene.World->RealTimeSeconds = 0.5;
	Scene.Controller->Interact();
	Scene.World->RealTimeSeconds = 2.9;
	TestTrue(TEXT("提示在默认三秒有效期内可见"), Scene.HUD->IsKeyAcquiredMessageVisible());
	Scene.World->RealTimeSeconds = 3.1;
	TestFalse(TEXT("提示按时消失，重复 E 不会延长提示"), Scene.HUD->IsKeyAcquiredMessageVisible());
	TestTrue(TEXT("提示到期不清除人物已获得钥匙状态"), Scene.Character->bHasKey);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamKeyPickupPreviewTest,
	"DreamSpace.Puzzle.KeyPickup.PreviewCancelAndInvalidInteractorNeverGrantKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamKeyPickupPreviewTest::RunTest(const FString& Parameters)
{
	FKeyPickupFixture Scene;
	TestTrue(TEXT("开始钥匙模型预览"), Scene.Begin());
	ADreamDroppedKey* Key = Cast<ADreamDroppedKey>(Scene.Extract->GetPreviewItem());
	if (!TestNotNull(TEXT("预览生成钥匙类型"), Key))
		return false;
	UDreamKeyPickupComponent* Pickup = Key->FindComponentByClass<UDreamKeyPickupComponent>();
	TestFalse(TEXT("预览未启用拾取"), Pickup->IsPickupEnabled());
	IDreamInteractableInterface::Execute_OnInteracted(Pickup, Scene.Character);
	TestTrue(TEXT("直接调用预览接口也不会领取钥匙"), IsValid(Key) && !Scene.Character->bHasKey);
	Scene.Extract->EndExtract(false);
	TestFalse(TEXT("取消删除预览钥匙"), IsValid(Key));
	TestTrue(TEXT("取消恢复房间源模型"), IsValid(Scene.Source) && !Scene.Source->IsHidden());
	TestFalse(TEXT("取消不触发获得提示"), Scene.HUD->IsKeyAcquiredMessageVisible());

	Key = Scene.Commit();
	if (!TestNotNull(TEXT("取消后仍可重新取出"), Key))
		return false;
	Pickup = Key->FindComponentByClass<UDreamKeyPickupComponent>();
	IDreamInteractableInterface::Execute_OnInteracted(Pickup, nullptr);
	IDreamInteractableInterface::Execute_OnInteracted(Pickup, Scene.World->SpawnActor<AActor>());
	TestTrue(TEXT("空操作者及普通 Actor 不能消费钥匙"), IsValid(Key) && Pickup->IsPickupEnabled());
	Pickup->Deactivate();
	IDreamInteractableInterface::Execute_OnInteracted(Pickup, Scene.Character);
	TestTrue(TEXT("停用拾取组件不改变持有状态"), IsValid(Key) && !Scene.Character->bHasKey);
	Pickup->Activate(true);
	IDreamInteractableInterface::Execute_OnInteracted(Pickup, Scene.Character);
	TestTrue(TEXT("重新激活后允许正常领取"), Scene.Character->bHasKey && !IsValid(Key));
	return true;
}
#endif
