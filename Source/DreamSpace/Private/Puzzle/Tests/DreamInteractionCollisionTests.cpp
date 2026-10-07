#include "DreamRotatableComponent.h"
#include "DreamTranslatableComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DreamPivotPointComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace
{
	/** 创建仅存在于测试世界的机关 Actor；根无碰撞，专门验证附属网格是否参与检查。 */
	AActor* SpawnMover(UWorld* World, const FVector& Location, const FVector& MeshOffset)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(Actor);
		Root->SetMobility(EComponentMobility::Movable);
		Actor->SetRootComponent(Root);
		Actor->AddInstanceComponent(Root);
		Root->RegisterComponent();

		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Actor);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Mesh->SetupAttachment(Root);
		Mesh->SetRelativeLocation(MeshOffset);
		Actor->AddInstanceComponent(Mesh);
		Mesh->RegisterComponent();
		Actor->SetActorLocation(Location);

		UDreamPivotPointComponent* Pivot = NewObject<UDreamPivotPointComponent>(Actor);
		Pivot->SetupAttachment(Root);
		Actor->AddInstanceComponent(Pivot);
		Pivot->RegisterComponent();
		return Actor;
	}

	/** 障碍物使用小立方体，使旋转测试的起点和终点均畅通，只在中段受阻。 */
	AActor* SpawnBlocker(UWorld* World, const FVector& Location, float Scale)
	{
		AActor* Blocker = World->SpawnActor<AActor>();
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Blocker);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Blocker->SetRootComponent(Mesh);
		Blocker->AddInstanceComponent(Mesh);
		Mesh->RegisterComponent();
		Blocker->SetActorLocation(Location);
		Blocker->SetActorScale3D(FVector(Scale));
		return Blocker;
	}

	/** 配置项保持 private，测试通过 UE 反射模拟详情面板设置。 */
	void SetCollisionEnabled(UActorComponent* Component, bool bEnabled)
	{
		FBoolProperty* Property = FindFProperty<FBoolProperty>(Component->GetClass(), TEXT("bConsiderCollision"));
		check(Property);
		Property->SetPropertyValue_InContainer(Component, bEnabled);
	}

	/** 通过反射模拟编辑器里把时长设置为 0，而不扩大组件的运行时公开接口。 */
	void SetDuration(UActorComponent* Component, const TCHAR* PropertyName, float Duration)
	{
		FFloatProperty* Property = FindFProperty<FFloatProperty>(Component->GetClass(), PropertyName);
		check(Property);
		Property->SetPropertyValue_InContainer(Component, Duration);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamInteractionRotationCollisionTest,
	"DreamSpace.Puzzle.RotationCollisionReturn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInteractionRotationCollisionTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AActor* Mover = SpawnMover(World, FVector::ZeroVector, FVector(100, 0, 0));
	UDreamRotatableComponent* Rotation = NewObject<UDreamRotatableComponent>(Mover);
	Mover->AddInstanceComponent(Rotation);
	Rotation->RegisterComponent();
	SetCollisionEnabled(Rotation, true);
	SpawnBlocker(World, FVector(80, 125, 0), 0.2f);

	Rotation->TriggerRotation();
	Rotation->TickComponent(0.2f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("转到障碍前已有可见进度"), Rotation->IsRotating() && Mover->GetActorRotation().Yaw > 1.0f);
	Rotation->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("中途受阻时未越过障碍"), Mover->GetActorRotation().Yaw < 45.0f);
	Rotation->TickComponent(1.0f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("受阻回弹结束后可再次交互"), Rotation->IsRotating());
	TestTrue(TEXT("受阻后精确恢复起始朝向"), Mover->GetActorRotation().Equals(FRotator::ZeroRotator, 0.01f));
	TestTrue(TEXT("受阻后精确恢复起始位置"), Mover->GetActorLocation().Equals(FVector::ZeroVector, 0.01f));
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamInteractionTranslationCollisionTest,
	"DreamSpace.Puzzle.TranslationCollisionReturn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInteractionTranslationCollisionTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AActor* Mover = SpawnMover(World, FVector::ZeroVector, FVector::ZeroVector);
	UDreamTranslatableComponent* Translation = NewObject<UDreamTranslatableComponent>(Mover);
	Mover->AddInstanceComponent(Translation);
	Translation->RegisterComponent();
	SetCollisionEnabled(Translation, true);
	SpawnBlocker(World, FVector(100, 0, 0), 0.2f);

	Translation->TriggerTranslation();
	// 第一帧直接要求移动到障碍内部，仍应扫到接触面并产生可见的前进/回弹。
	Translation->TickComponent(0.3f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("目标落在障碍内部时仍前进到接触面"), Mover->GetActorLocation().X > 1.0f);
	TestTrue(TEXT("平移受阻时未穿过障碍"), Mover->GetActorLocation().X < 40.0f);
	Translation->TickComponent(1.0f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("回弹结束后平移状态已清除"), Translation->IsTranslating());
	TestTrue(TEXT("回弹后世界位置恢复"), Mover->GetActorLocation().Equals(FVector::ZeroVector, 0.01f));
	TestTrue(TEXT("回弹后逻辑坐标恢复"), FMath::IsNearlyZero(Translation->GetCurrentTranslation(), 0.01f));

	// 同一机关下一次触发仍应尝试原来的正向运动，不能把受阻当作到达端点。
	Translation->TriggerTranslation();
	Translation->TickComponent(0.2f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("失败后再次交互仍沿原方向前进"), Mover->GetActorLocation().X > 1.0f);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamInteractionCollisionDisabledTest,
	"DreamSpace.Puzzle.CollisionDisabledCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInteractionCollisionDisabledTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AActor* Mover = SpawnMover(World, FVector::ZeroVector, FVector::ZeroVector);
	UDreamTranslatableComponent* Translation = NewObject<UDreamTranslatableComponent>(Mover);
	Mover->AddInstanceComponent(Translation);
	Translation->RegisterComponent();
	SpawnBlocker(World, FVector(100, 0, 0), 0.2f);

	Translation->TriggerTranslation();
	Translation->TickComponent(1.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("默认关闭碰撞时仍按旧行为到达目标"), Mover->GetActorLocation().Equals(FVector(100, 0, 0), 0.01f));
	TestTrue(TEXT("默认关闭碰撞时逻辑坐标到达目标"), FMath::IsNearlyEqual(Translation->GetCurrentTranslation(), 100.0f, 0.01f));
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamInteractionCollisionClearPathTest,
	"DreamSpace.Puzzle.CollisionEnabledClearPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInteractionCollisionClearPathTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AActor* RotatingActor = SpawnMover(World, FVector::ZeroVector, FVector(100, 0, 0));
	UDreamRotatableComponent* Rotation = NewObject<UDreamRotatableComponent>(RotatingActor);
	RotatingActor->AddInstanceComponent(Rotation);
	Rotation->RegisterComponent();
	SetCollisionEnabled(Rotation, true);
	AActor* SlidingActor = SpawnMover(World, FVector(500, 0, 0), FVector::ZeroVector);
	UDreamTranslatableComponent* Translation = NewObject<UDreamTranslatableComponent>(SlidingActor);
	SlidingActor->AddInstanceComponent(Translation);
	Translation->RegisterComponent();
	SetCollisionEnabled(Translation, true);

	Rotation->TriggerRotation();
	Translation->TriggerTranslation();
	Rotation->TickComponent(1.0f, LEVELTICK_All, nullptr);
	Translation->TickComponent(1.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("开启碰撞且无障碍时转动完整 90 度"), RotatingActor->GetActorRotation().Equals(FRotator(0, 90, 0), 0.01f));
	TestTrue(TEXT("开启碰撞且无障碍时平移完整 100 厘米"), SlidingActor->GetActorLocation().Equals(FVector(600, 0, 0), 0.01f));
	TestFalse(TEXT("无障碍到位后转动状态结束"), Rotation->IsRotating());
	TestFalse(TEXT("无障碍到位后平移状态结束"), Translation->IsTranslating());
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamInteractionCollisionInstantTest,
	"DreamSpace.Puzzle.ZeroDurationStillChecksCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInteractionCollisionInstantTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AActor* RotatingActor = SpawnMover(World, FVector::ZeroVector, FVector(100, 0, 0));
	UDreamRotatableComponent* Rotation = NewObject<UDreamRotatableComponent>(RotatingActor);
	RotatingActor->AddInstanceComponent(Rotation);
	Rotation->RegisterComponent();
	SetCollisionEnabled(Rotation, true);
	SetDuration(Rotation, TEXT("RotationDuration"), 0.0f);
	SpawnBlocker(World, FVector(80, 125, 0), 0.2f);

	AActor* SlidingActor = SpawnMover(World, FVector(500, 0, 0), FVector::ZeroVector);
	UDreamTranslatableComponent* Translation = NewObject<UDreamTranslatableComponent>(SlidingActor);
	SlidingActor->AddInstanceComponent(Translation);
	Translation->RegisterComponent();
	SetCollisionEnabled(Translation, true);
	SetDuration(Translation, TEXT("TranslationDuration"), 0.0f);
	SpawnBlocker(World, FVector(600, 0, 0), 0.2f);

	Rotation->TriggerRotation();
	Translation->TriggerTranslation();
	TestTrue(TEXT("0 秒旋转遇阻时立即回到起始姿态"), RotatingActor->GetActorRotation().Equals(FRotator::ZeroRotator, 0.01f));
	TestTrue(TEXT("0 秒平移遇阻时保持起始位置"), SlidingActor->GetActorLocation().Equals(FVector(500, 0, 0), 0.01f));
	TestTrue(TEXT("0 秒平移遇阻时保持逻辑坐标"), FMath::IsNearlyZero(Translation->GetCurrentTranslation(), 0.01f));
	World->DestroyWorld(false);
	return true;
}
#endif
