#include "DreamDraggableComponent.h"
#include "DreamDragRotatableComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DreamCharacter.h"
#include "DreamPivotPointComponent.h"
#include "DreamPlayerController.h"
#include "DreamSceneCaptureAnchor.h"
#include "DreamSceneCapturePresentationComponent.h"
#include "DreamShoulderCameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Math/RotationMatrix.h"
#include "UObject/UnrealType.h"

namespace
{
/** 模拟详情面板配置，避免为了测试扩大组件的公开配置接口。 */
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

void SetAxis(UObject* Object, EDreamPivotRotationAxis Value)
{
	FEnumProperty* Property = FindFProperty<FEnumProperty>(Object->GetClass(), TEXT("InteractionAxis"));
	check(Property);
	Property->GetUnderlyingProperty()->SetIntPropertyValue(
		Property->ContainerPtrToValuePtr<void>(Object), static_cast<uint64>(Value));
}

UBoxComponent* AddDragTestBox(AActor* Actor, const FVector& Extent, const FVector& Offset = FVector::ZeroVector)
{
	UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
	Box->SetMobility(EComponentMobility::Movable);
	Box->SetBoxExtent(Extent);
	Box->SetCollisionProfileName(TEXT("BlockAll"));
	Box->SetupAttachment(Actor->GetRootComponent());
	Box->SetRelativeLocation(Offset);
	Actor->AddInstanceComponent(Box);
	Box->RegisterComponent();
	return Box;
}

/** 无碰撞根与附属碰撞体验证实际蓝图常用结构，所有对象只存在于隔离的测试世界。 */
AActor* SpawnRootActor(UWorld* World, const FVector& Location = FVector::ZeroVector)
{
	AActor* Actor = World->SpawnActor<AActor>();
	USceneComponent* Root = NewObject<USceneComponent>(Actor);
	Root->SetMobility(EComponentMobility::Movable);
	Actor->SetRootComponent(Root);
	Actor->AddInstanceComponent(Root);
	Root->RegisterComponent();
	Actor->SetActorLocation(Location);
	return Actor;
}

template <typename T> T* AddInteraction(AActor* Actor)
{
	T* Component = NewObject<T>(Actor);
	Actor->AddInstanceComponent(Component);
	Component->RegisterComponent();
	// 隔离 UWorld 没有执行整套 Actor 初始化，Register 不会像 PIE 那样自动激活。
	// 显式模拟运行时 AutoActivate，确保测试验证输入行为而非未开始播放的组件状态。
	Component->Activate(true);
	return Component;
}

struct FDragFixture
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AActor* Mover = nullptr;
	UDreamPivotPointComponent* Pivot = nullptr;
	AActor* Interactor = nullptr;

	FDragFixture(const FVector& Location = FVector::ZeroVector)
	{
		Mover = SpawnRootActor(World, Location);
		Pivot = NewObject<UDreamPivotPointComponent>(Mover);
		Pivot->SetupAttachment(Mover->GetRootComponent());
		Mover->AddInstanceComponent(Pivot);
		Pivot->RegisterComponent();
		Interactor = World->SpawnActor<AActor>();
	}

	~FDragFixture()
	{
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
	}
};

/** 在旋转平面上生成指定角度的射线，明确区分鼠标角度与物体累计角度。 */
FVector RotationRay(float Degrees)
{
	return FQuat(FVector::UpVector, FMath::DegreesToRadians(Degrees)).RotateVector(FVector(100, 0, 0)) +
		   FVector(0, 0, 500);
}

/** 实际地面查询建立角色底座，用于验证自由旋转真正影响站立玩家，而非仅改变测试向量。 */
ADreamCharacter* SpawnStandingCharacter(
	FDragFixture& Scene, UBoxComponent* Floor, ADreamPlayerController*& OutController)
{
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamCharacter* Character =
		Scene.World->SpawnActor<ADreamCharacter>(FVector(30, 60, 108), FRotator::ZeroRotator, Spawn);
	OutController = Scene.World->SpawnActor<ADreamPlayerController>();
	OutController->Player = NewObject<ULocalPlayer>(GEngine);
	OutController->Possess(Character);
	OutController->SetControlRotation(FRotator(-15, 0, 0));
	Scene.World->SetBegunPlay(true);
	Scene.World->TimeSeconds = 2.0;
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Movement->SetMovementMode(MOVE_Walking);
	Movement->FindFloor(Character->GetActorLocation(), Movement->CurrentFloor, false);
	Movement->SetBaseFromFloor(Movement->CurrentFloor);
	check(Movement->GetMovementBaseObject() == Floor);
	return Character;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamDragTranslationRangeTest,
	"DreamSpace.Puzzle.Drag.TranslationPivotRangeAndRegrab",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDragTranslationRangeTest::RunTest(const FString& Parameters)
{
	FDragFixture Scene(FVector(300, 50, 20));
	Scene.Pivot->SetRelativeLocation(FVector(-20, -30, 0));
	Scene.Pivot->SetRelativeRotation(FRotator(0, 90, 0));
	UDreamDraggableComponent* Drag = AddInteraction<UDreamDraggableComponent>(Scene.Mover);
	SetFloat(Drag, TEXT("NegativeLimit"), 25.0f);
	SetFloat(Drag, TEXT("PositiveLimit"), 75.0f);
	Drag->ReinitializeReference();
	TestTrue(
		TEXT("初始坐标按旋转枢轴局部 X 的世界 Y 方向投影"), FMath::IsNearlyEqual(Drag->GetCurrentTranslation(), 30.0f));
	Drag->SetTranslation(55.0f);
	TestTrue(TEXT("平移保留相对轴的垂直偏移"), Scene.Mover->GetActorLocation().Equals(FVector(300, 75, 20), 0.01f));
	TestTrue(TEXT("平移不改变 Actor 旋转"), Scene.Mover->GetActorRotation().IsNearlyZero());
	const FVector Down = FVector::DownVector;
	TestTrue(TEXT("已移动后可以开始抓取"), Drag->BeginDrag(Scene.Interactor, FVector(300, 75, 520), Down));
	TestFalse(TEXT("拖动中不允许重建参考系"), Drag->ReinitializeReference());
	Drag->UpdateDrag(FVector(300, 95, 520), Down, FVector2D::ZeroVector);
	TestTrue(TEXT("达到正端点使用固定枢轴范围，范围未随移动漂移"),
		FMath::IsNearlyEqual(Drag->GetCurrentTranslation(), 75.0f));
	Drag->UpdateDrag(FVector(300, 195, 520), Down, FVector2D::ZeroVector);
	Drag->UpdateDrag(FVector(300, 193, 520), Down, FVector2D::ZeroVector);
	TestTrue(
		TEXT("越界输入被消耗，反向两厘米立即响应"), FMath::IsNearlyEqual(Drag->GetCurrentTranslation(), 73.0f, 0.01f));
	Drag->EndDrag();
	const FVector Released = Scene.Mover->GetActorLocation();
	Drag->UpdateDrag(FVector(300, 0, 520), Down, FVector2D::ZeroVector);
	TestTrue(TEXT("松开后保留位置且不再接收拖动输入"), Scene.Mover->GetActorLocation().Equals(Released));
	Drag->BeginDrag(Scene.Interactor, FVector(300, 400, 520), Down);
	TestTrue(TEXT("重新抓取不会吸附到鼠标或重置范围"), Scene.Mover->GetActorLocation().Equals(Released));
	Drag->UpdateDrag(FVector(300, 390, 520), Down, FVector2D::ZeroVector);
	TestTrue(TEXT("重新抓取从现有坐标继续移动"), FMath::IsNearlyEqual(Drag->GetCurrentTranslation(), 63.0f, 0.01f));
	Drag->SetTranslation(-1000.0f);
	TestTrue(TEXT("负端点同样被截断"), FMath::IsNearlyEqual(Drag->GetCurrentTranslation(), -25.0f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamDragTranslationCollisionTest,
	"DreamSpace.Puzzle.Drag.TranslationCollisionStopsAndReverses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDragTranslationCollisionTest::RunTest(const FString& Parameters)
{
	FDragFixture Scene;
	AddDragTestBox(Scene.Mover, FVector(10));
	AActor* Blocker = SpawnRootActor(Scene.World, FVector(50, 0, 0));
	AddDragTestBox(Blocker, FVector(10));
	UDreamDraggableComponent* Drag = AddInteraction<UDreamDraggableComponent>(Scene.Mover);
	SetBool(Drag, TEXT("bConsiderCollision"), true);
	const float Safe = Drag->SetTranslation(100.0f);
	TestTrue(TEXT("完整平移路径的中段障碍会阻挡附属碰撞体"), Safe > 20.0f && Safe < 30.0f);
	Drag->TickComponent(1.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("受阻后停在安全位置，不回弹到抓取起点"), FMath::IsNearlyEqual(Drag->GetCurrentTranslation(), Safe));
	TestTrue(TEXT("反向立即离开障碍"), FMath::IsNearlyEqual(Drag->SetTranslation(-10.0f), -10.0f, 0.01f));
	SetBool(Drag, TEXT("bConsiderCollision"), false);
	TestTrue(
		TEXT("关闭碰撞可到达障碍另一侧的范围端点"), FMath::IsNearlyEqual(Drag->SetTranslation(100.0f), 100.0f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamDragRotationRangeTest,
	"DreamSpace.Puzzle.Drag.RotationSignedAnglesAndFixedPivot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDragRotationRangeTest::RunTest(const FString& Parameters)
{
	FDragFixture Scene(FVector(100, 0, 0));
	Scene.Pivot->SetRelativeLocation(FVector(-100, 0, 0));
	UDreamDragRotatableComponent* Drag = AddInteraction<UDreamDragRotatableComponent>(Scene.Mover);
	SetFloat(Drag, TEXT("NegativeAngleLimit"), 90.0f);
	SetFloat(Drag, TEXT("PositiveAngleLimit"), 450.0f);
	TestTrue(TEXT("旋转可以从任意径向抓取"), Drag->BeginDrag(Scene.Interactor, RotationRay(179), FVector::DownVector));
	Drag->UpdateDrag(RotationRay(-179), FVector::DownVector, FVector2D::ZeroVector);
	TestTrue(TEXT("跨正负 180 度时只连续转动两度"), FMath::IsNearlyEqual(Drag->GetCurrentAngleDegrees(), 2.0f, 0.01f));
	Drag->EndDrag();
	Drag->SetRotationAngle(270.0f);
	TestTrue(TEXT("旋转保留累计 270 度逻辑角度"), FMath::IsNearlyEqual(Drag->GetCurrentAngleDegrees(), 270.0f));
	TestTrue(TEXT("偏心枢轴同时产生正确公转"), Scene.Mover->GetActorLocation().Equals(FVector(0, -100, 0), 0.01f));
	Drag->SetRotationAngle(1000.0f);
	TestTrue(TEXT("大于一圈的范围不会被四元数短弧截断"), FMath::IsNearlyEqual(Drag->GetCurrentAngleDegrees(), 450.0f));
	TestTrue(
		TEXT("多圈旋转后枢轴世界位置仍固定"), Scene.Pivot->GetComponentLocation().Equals(FVector::ZeroVector, 0.01f));
	const FTransform BeforeRegrab = Scene.Mover->GetActorTransform();
	Drag->BeginDrag(Scene.Interactor, RotationRay(20), FVector::DownVector);
	TestTrue(TEXT("重新抓取不会重置零角度或跳变"), Scene.Mover->GetActorTransform().Equals(BeforeRegrab, 0.01f));
	Drag->UpdateDrag(RotationRay(19), FVector::DownVector, FVector2D::ZeroVector);
	TestTrue(TEXT("从正角度端点反向立即转动"), FMath::IsNearlyEqual(Drag->GetCurrentAngleDegrees(), 449.0f, 0.01f));
	Drag->SetRotationAngle(-1000.0f);
	TestTrue(TEXT("负向角度范围独立限制"), FMath::IsNearlyEqual(Drag->GetCurrentAngleDegrees(), -90.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamDragRotationCollisionTest,
	"DreamSpace.Puzzle.Drag.RotationChecksLongArcAndReverses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDragRotationCollisionTest::RunTest(const FString& Parameters)
{
	FDragFixture Scene;
	AddDragTestBox(Scene.Mover, FVector(10), FVector(100, 0, 0));
	AddDragTestBox(SpawnRootActor(Scene.World, FVector(0, 100, 0)), FVector(12));
	UDreamDragRotatableComponent* Drag = AddInteraction<UDreamDragRotatableComponent>(Scene.Mover);
	SetFloat(Drag, TEXT("PositiveAngleLimit"), 360.0f);
	SetBool(Drag, TEXT("bConsiderCollision"), true);
	const float Safe = Drag->SetRotationAngle(270.0f);
	TestTrue(TEXT("目标 270 度须经过正向 90 度障碍，而非走反向短弧"), Safe > 20.0f && Safe < 90.0f);
	Drag->TickComponent(1.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("旋转受阻后保留安全角度"), FMath::IsNearlyEqual(Drag->GetCurrentAngleDegrees(), Safe));
	TestTrue(TEXT("可从障碍前立即反向旋转"), FMath::IsNearlyEqual(Drag->SetRotationAngle(-10.0f), -10.0f, 0.01f));
	SetBool(Drag, TEXT("bConsiderCollision"), false);
	TestTrue(TEXT("关闭碰撞可完成完整 270 度路径"), FMath::IsNearlyEqual(Drag->SetRotationAngle(270.0f), 270.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamDragDegenerateInputTest,
	"DreamSpace.Puzzle.Drag.DegenerateViewsPauseAndCameraRebase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDragDegenerateInputTest::RunTest(const FString& Parameters)
{
	FDragFixture Scene;
	UDreamDraggableComponent* Translation = AddInteraction<UDreamDraggableComponent>(Scene.Mover);
	Translation->BeginDrag(Scene.Interactor, FVector(-500, 0, 0), FVector::ForwardVector);
	Translation->UpdateDrag(FVector(-500, 0, 0), FVector::ForwardVector, FVector2D(10, 0));
	TestTrue(TEXT("视线平行运动轴时备用屏幕输入仍可拖动"),
		FMath::IsNearlyEqual(Translation->GetCurrentTranslation(), 10.0f));
	Translation->EndDrag();
	Translation->SetTranslation(0.0f);
	Translation->BeginDrag(Scene.Interactor, FVector(0, 0, 500), FVector::DownVector);
	Translation->SuspendDragInput();
	Translation->UpdateDrag(FVector(80, 0, 500), FVector::DownVector, FVector2D(80, 0));
	TestTrue(TEXT("手办射线恢复后的首帧只采样，不产生跳变"), FMath::IsNearlyZero(Translation->GetCurrentTranslation()));
	Translation->UpdateDrag(FVector(90, 0, 500), FVector::DownVector, FVector2D(10, 0));
	TestTrue(TEXT("恢复后继续接收后续实际增量"), FMath::IsNearlyEqual(Translation->GetCurrentTranslation(), 10.0f));
	Translation->EndDrag();

	UDreamDragRotatableComponent* Rotation = AddInteraction<UDreamDragRotatableComponent>(Scene.Mover);
	Rotation->BeginDrag(Scene.Interactor, FVector(500, 0, 0), -FVector::ForwardVector);
	Rotation->UpdateDrag(FVector(500, 0, 0), -FVector::ForwardVector, FVector2D(10, 0));
	TestTrue(TEXT("侧看旋转平面时备用输入仍可转动"), FMath::IsNearlyEqual(Rotation->GetCurrentAngleDegrees(), 5.0f));
	Rotation->EndDrag();
	Rotation->SetRotationAngle(0.0f);
	const FVector Pivot = Scene.Pivot->GetComponentLocation();
	Rotation->BeginDrag(Scene.Interactor, Pivot + RotationRay(0), FVector::DownVector);
	// 模拟平台带动相机后，同一鼠标位置的射线变成另一径向。重采样应消除该假输入。
	Rotation->RebaseDragRay(Pivot + RotationRay(30), FVector::DownVector);
	Rotation->UpdateDrag(Pivot + RotationRay(30), FVector::DownVector, FVector2D::ZeroVector);
	TestTrue(TEXT("相机变化而鼠标静止时不会继续旋转"), FMath::IsNearlyZero(Rotation->GetCurrentAngleDegrees(), 0.01f));
	Rotation->UpdateDrag(Pivot + RotationRay(40), FVector::DownVector, FVector2D(10, 0));
	TestTrue(TEXT("相机重采样后仍能接收实际鼠标角度增量"),
		FMath::IsNearlyEqual(Rotation->GetCurrentAngleDegrees(), 10.0f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamDragGravityTest, "DreamSpace.Puzzle.Drag.StandingGravityFollowingAndLeaving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDragGravityTest::RunTest(const FString& Parameters)
{
	FDragFixture Scene;
	UBoxComponent* Floor = AddDragTestBox(Scene.Mover, FVector(200, 200, 10));
	Scene.Pivot->SetRelativeLocation(FVector(0, 60, 0));
	UDreamDragRotatableComponent* Rotation = AddInteraction<UDreamDragRotatableComponent>(Scene.Mover);
	SetAxis(Rotation, EDreamPivotRotationAxis::X);
	SetFloat(Rotation, TEXT("PositiveAngleLimit"), 360.0f);
	Rotation->ReinitializeReference();
	ADreamPlayerController* Controller = nullptr;
	ADreamCharacter* Character = SpawnStandingCharacter(Scene, Floor, Controller);
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const FVector StartLocation = Character->GetActorLocation();
	const FVector Pivot = Scene.Pivot->GetComponentLocation();
	const FQuat StartView = Controller->GetControlRotation().Quaternion();
	Rotation->SetRotationAngle(10.0f);
	TestTrue(TEXT("默认关闭重力跟随保留旧重力方向"), Movement->GetGravityDirection().Equals(FVector::DownVector));
	Rotation->SetRotationAngle(0.0f);
	SetBool(Rotation, TEXT("bRotateStandingCharacterGravity"), true);
	SetBool(Rotation, TEXT("bConsiderCollision"), true);
	bool bRemainedGrounded = true;
	for (int32 Frame = 1; Frame <= 30; ++Frame)
	{
		Rotation->SetRotationAngle(Frame * 3.0f);
		Movement->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
		Controller->UpdateRotation(1.0f / 60.0f);
		bRemainedGrounded &= Movement->IsMovingOnGround() && Movement->GetMovementBaseObject() == Floor;
	}
	const FQuat Turn(FVector::ForwardVector, HALF_PI);
	TestTrue(TEXT("碰撞开启时乘客不误挡平台，逐帧倾斜仍保持真实底座"), bRemainedGrounded);
	TestTrue(TEXT("转成墙面后重力同步变成世界 +Y"), Movement->GetGravityDirection().Equals(FVector(0, 1, 0), 0.001f));
	TestTrue(TEXT("偏心枢轴公转搬运整个胶囊且没有重复底座位移"),
		Character->GetActorLocation().Equals(Pivot + Turn.RotateVector(StartLocation - Pivot), 0.2f));
	TestTrue(TEXT("玩家胶囊向上与重力反方向对齐"),
		Character->GetActorUpVector().Equals(-Movement->GetGravityDirection(), 0.001f));
	TestTrue(TEXT("玩家相机保持平台完整旋转"),
		Controller->GetControlRotation().Quaternion().Equals(Turn * StartView, 0.001f));
	Character->DoJumpStart();
	Movement->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("新重力方向下仍可实际跳离平台"), Movement->IsFalling());
	const FVector GravityOnLeaving = Movement->GetGravityDirection();
	const FTransform AirTransform = Character->GetActorTransform();
	Rotation->SetRotationAngle(120.0f);
	TestTrue(TEXT("跳离后重力保持最后世界方向"), Movement->GetGravityDirection().Equals(GravityOnLeaving));
	TestTrue(TEXT("平台不继续搬运空中角色"), Character->GetActorTransform().Equals(AirTransform, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamDragGravityCollisionTest,
	"DreamSpace.Puzzle.Drag.StandingCharacterCollisionStopsPlatform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDragGravityCollisionTest::RunTest(const FString& Parameters)
{
	FDragFixture Scene;
	UBoxComponent* Floor = AddDragTestBox(Scene.Mover, FVector(200, 200, 10));
	Scene.Pivot->SetRelativeLocation(FVector(0, 60, 0));
	UDreamDragRotatableComponent* Rotation = AddInteraction<UDreamDragRotatableComponent>(Scene.Mover);
	SetAxis(Rotation, EDreamPivotRotationAxis::X);
	SetBool(Rotation, TEXT("bRotateStandingCharacterGravity"), true);
	SetBool(Rotation, TEXT("bConsiderCollision"), true);
	Rotation->ReinitializeReference();
	ADreamPlayerController* Controller = nullptr;
	ADreamCharacter* Character = SpawnStandingCharacter(Scene, Floor, Controller);
	const FTransform Start = Character->GetActorTransform();
	// 此障碍只挡乘客上半身，平台本身的旋转体积不会碰到它。
	AddDragTestBox(SpawnRootActor(Scene.World, FVector(30, -70, 150)), FVector(12));
	const float Safe = Rotation->SetRotationAngle(90.0f);
	TestTrue(TEXT("角色头部先受阻时平台停在同步安全角度"), Safe > 0.0f && Safe < 90.0f);
	const FQuat Turn(FVector::ForwardVector, FMath::DegreesToRadians(Safe));
	TestTrue(TEXT("受阻时角色重力对应实际安全角度"), Character->GetCharacterMovement()->GetGravityDirection().Equals(
														 Turn.RotateVector(FVector::DownVector), 0.001f));
	Rotation->SetRotationAngle(0.0f);
	TestTrue(TEXT("玩家反向拖回零度时角色姿态也恢复"), Character->GetActorTransform().Equals(Start, 0.1f));
	TestTrue(TEXT("反向拖回零度时重力恢复为向下"),
		Character->GetCharacterMovement()->GetGravityDirection().Equals(FVector::DownVector, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamDragControllerLifecycleTest,
	"DreamSpace.Puzzle.Drag.ControllerReleaseDestructionAndInputLocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDragControllerLifecycleTest::RunTest(const FString& Parameters)
{
	FDragFixture Scene;
	UDreamDraggableComponent* Drag = AddInteraction<UDreamDraggableComponent>(Scene.Mover);
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamCharacter* Pawn = Scene.World->SpawnActor<ADreamCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
	ADreamPlayerController* Controller = Scene.World->SpawnActor<ADreamPlayerController>();
	Controller->Player = NewObject<ULocalPlayer>(GEngine);
	Scene.World->AddController(Controller);
	Controller->SpawnPlayerCameraManager();
	Controller->Possess(Pawn);
	Controller->SetViewTarget(Pawn);
	// Tab 现在会建立真实的显示面聚焦会话；隔离世界没有正常 BeginPlay，显式补齐相机与捕获资源。
	// 使用原生角色而非普通 APawn，才能验证左键/E 两种输入来源和观察锁之间的实际关系。
	UDreamShoulderCameraComponent* Camera = CastChecked<UDreamShoulderCameraComponent>(Pawn->CameraBoom);
	Camera->Activate(true);
	Camera->TickComponent(0.0f, LEVELTICK_All, nullptr);
	Scene.World->SpawnActor<ADreamSceneCaptureAnchor>();
	Pawn->SceneMiniature->RenderTargetWidth = 256;
	Pawn->SceneMiniature->RenderTargetHeight = 256;
	Pawn->SceneMiniature->BeginPlay();
	Controller->SetIgnoreLookInput(true);
	Controller->DispatchInteraction(Scene.Mover, nullptr, FVector(0, 0, 500), FVector::DownVector);
	TestTrue(TEXT("控制器分发会进入持续拖动"), Controller->IsDraggingInteraction());
	TestTrue(TEXT("持续交互期间锁定移动与视角"), Controller->IsMoveInputIgnored() && Controller->IsLookInputIgnored());
	Controller->EndMiniatureDrag();
	TestTrue(TEXT("另一输入来源的松开事件不会结束 E 拖动"), Controller->IsDraggingInteraction());
	Controller->EndWorldDrag();
	TestFalse(TEXT("E 松开会结束组件和控制器会话"), Controller->IsDraggingInteraction() || Drag->IsDragging());
	TestFalse(TEXT("结束拖动释放本组件添加的移动锁"), Controller->IsMoveInputIgnored());
	TestTrue(TEXT("结束拖动保留其他系统已有的视角锁"), Controller->IsLookInputIgnored());
	Controller->SetIgnoreLookInput(false);
	Controller->DispatchInteraction(Scene.Mover, nullptr, FVector(0, 0, 500), FVector::DownVector);
	Controller->SetMiniatureInteractionMode(true);
	TestFalse(TEXT("切换输入空间立即结束旧的 E 拖动"), Drag->IsDragging() || Controller->IsDraggingInteraction());
	TestTrue(TEXT("Tab 聚焦独立保持移动和视角锁"), Controller->IsMiniatureInteractionMode()
		&& Controller->IsMoveInputIgnored() && Controller->IsLookInputIgnored());
	Controller->DispatchInteraction(Scene.Mover, nullptr, FVector(0, 0, 500), FVector::DownVector);
	Controller->EndWorldDrag();
	TestTrue(TEXT("E 松开不会结束手办左键拖动"), Drag->IsDragging());
	Controller->SetMiniatureInteractionMode(false);
	TestFalse(TEXT("从手办切回探索同样清理会话与两种输入锁"), Drag->IsDragging() || Controller->IsDraggingInteraction()
		|| Controller->IsMoveInputIgnored() || Controller->IsLookInputIgnored());
	Controller->DispatchInteraction(Scene.Mover, nullptr, FVector(0, 0, 500), FVector::DownVector);
	Drag->Deactivate();
	TestFalse(TEXT("停用组件不等待 Tick 即释放操作者"), Drag->IsDragging());
	Controller->UpdateActiveDrag();
	TestFalse(TEXT("停用后控制器恢复输入锁"), Controller->IsMoveInputIgnored() || Controller->IsLookInputIgnored());
	Drag->Activate(true);
	Controller->DispatchInteraction(Scene.Mover, nullptr, FVector(0, 0, 500), FVector::DownVector);
	Drag->DestroyComponent();
	Controller->UpdateActiveDrag();
	TestFalse(TEXT("目标组件销毁后解除会话与输入锁"),
		Controller->IsDraggingInteraction() || Controller->IsMoveInputIgnored() || Controller->IsLookInputIgnored());
	Drag = AddInteraction<UDreamDraggableComponent>(Scene.Mover);
	Controller->DispatchInteraction(Scene.Mover, nullptr, FVector(0, 0, 500), FVector::DownVector);
	Controller->UnPossess();
	TestFalse(TEXT("切换或失去 Pawn 时清理持续交互"), Drag->IsDragging() || Controller->IsMoveInputIgnored());
	Scene.Pivot->DestroyComponent();
	TestFalse(
		TEXT("没有有效枢轴时不能开始拖动"), Drag->BeginDrag(Scene.Interactor, FVector(0, 0, 500), FVector::DownVector));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMiniatureDragMappingTest,
	"DreamSpace.Puzzle.Drag.MiniatureCaptureRayStartsAndDrivesDrag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMiniatureDragMappingTest::RunTest(const FString& Parameters)
{
	FDragFixture Scene;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamCharacter* Character =
		Scene.World->SpawnActor<ADreamCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
	Scene.World->SpawnActor<ADreamSceneCaptureAnchor>();
	UDreamSceneCapturePresentationComponent* Miniature = Character->SceneMiniature;
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
	if (!TestNotNull(TEXT("真实表现层生成显示面"), Display) || !TestNotNull(TEXT("真实表现层生成捕获相机"), Capture))
		return false;

	Capture->SetActorLocationAndRotation(FVector(10000, 0, 1000), FRotator::ZeroRotator);
	Display->SetWorldTransform(
		FTransform(FRotationMatrix::MakeFromXZ(FVector::RightVector, -FVector::ForwardVector).ToQuat(),
			FVector(0, 300, 100), FVector(0.8f)));
	const FVector Camera = Display->GetComponentLocation() - FVector(350, 0, 0);
	Scene.Mover->SetActorLocation(FVector(12000, 0, 1000));
	AddDragTestBox(Scene.Mover, FVector(60));
	Scene.Pivot->SetRelativeRotation(FRotator(0, 90, 0));
	UDreamDraggableComponent* Drag = AddInteraction<UDreamDraggableComponent>(Scene.Mover);
	SetFloat(Drag, TEXT("PositiveLimit"), 2000.0f);
	SetFloat(Drag, TEXT("NegativeLimit"), 2000.0f);
	ADreamPlayerController* Controller = Scene.World->SpawnActor<ADreamPlayerController>();
	Controller->Possess(Character);
	Controller->bMiniatureInteractionMode = true;
	Controller->InteractWithMiniatureRay(Camera, Display->GetComponentLocation() - Camera);
	TestTrue(TEXT("手办真实拾取入口使用捕获射线开始持续交互"), Controller->IsDraggingInteraction());
	const FVector DisplayPoint = Display->GetComponentTransform().TransformPosition(FVector(10, 0, 0));
	FVector DisplayHit, Origin, Direction;
	FString Reason;
	TestTrue(TEXT("手办显示面移动位置能映射为捕获射线"),
		Miniature->TryMapViewRayToCaptureRay(Camera, DisplayPoint - Camera, DisplayHit, Origin, Direction, Reason));
	Drag->UpdateDrag(Origin, Direction, FVector2D::ZeroVector);
	TestTrue(TEXT("手办射线实际驱动远处 Actor 沿枢轴轴向移动"), Scene.Mover->GetActorLocation().Y > 100.0f);
	TestTrue(
		TEXT("手办拖动保留远处场景的世界坐标"), FMath::IsNearlyEqual(Scene.Mover->GetActorLocation().X, 12000.0, 0.01));
	Controller->EndMiniatureDrag();
	TestFalse(TEXT("手办左键松开结束持续交互"), Drag->IsDragging() || Controller->IsDraggingInteraction());
	return true;
}
#endif
