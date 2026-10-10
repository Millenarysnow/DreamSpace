#include "DreamMiniatureExtractableComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DreamCharacter.h"
#include "DreamDroppedItem.h"
#include "DreamDraggableComponent.h"
#include "DreamPlayerController.h"
#include "DreamSceneCaptureAnchor.h"
#include "DreamSceneCapturePresentationComponent.h"
#include "DreamShoulderCameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Math/RotationMatrix.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace
{
/** 只改瞬时对象的反射属性，模拟详情面板；自动化不会保存或修改用户的关卡及蓝图。 */
void SetExtractionBool(UObject* Object, const TCHAR* Name, bool Value)
{
	FBoolProperty* Property = FindFProperty<FBoolProperty>(Object->GetClass(), Name);
	check(Property);
	Property->SetPropertyValue_InContainer(Object, Value);
}

template <typename T> T* AddExtractionTestComponent(AActor* Actor)
{
	T* Component = NewObject<T>(Actor);
	Actor->AddInstanceComponent(Component);
	Component->RegisterComponent();
	// 隔离世界不执行 PIE 的完整初始化，显式激活以模拟运行时的 AutoActivate。
	Component->Activate(true);
	return Component;
}

struct FExtractionFixture
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AActor* Source = nullptr;
	AActor* Interactor = nullptr;
	UStaticMeshComponent* Mesh = nullptr;
	UDreamMiniatureExtractableComponent* Extract = nullptr;
	const FVector DisplayPoint = FVector(0, 300, 1000);
	const FVector FrontNormal = -FVector::ForwardVector;

	FExtractionFixture()
	{
		// 销毁预览与源 Actor 会检查 WorldContext；为测试世界建立独立上下文，模拟实际游戏生命周期。
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Source = World->SpawnActor<AActor>();
		Mesh = NewObject<UStaticMeshComponent>(Source);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Source->SetRootComponent(Mesh);
		Source->AddInstanceComponent(Mesh);
		Mesh->RegisterComponent();
		Source->SetActorLocation(FVector(12000, 0, 1000));
		Interactor = World->SpawnActor<AActor>();
		Extract = AddExtractionTestComponent<UDreamMiniatureExtractableComponent>(Source);
	}

	bool Begin() { return Extract->BeginExtract(Interactor, Mesh, DisplayPoint, FrontNormal); }

	~FExtractionFixture()
	{
		// 组件级清理覆盖未进入完整 BeginPlay 的瞬时世界，避免测试预览逃逸到其它测试。
		if (IsValid(Extract))
			Extract->EndExtract(false);
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMiniatureExtractionPlaneTest,
	"DreamSpace.Puzzle.Extraction.DisplayPlaneKeepsOutsideUV",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMiniatureExtractionPlaneTest::RunTest(const FString& Parameters)
{
	const FBox Bounds(FVector(-50, -50, 0), FVector(50, 50, 0));
	const FTransform Display(FQuat(FVector::UpVector, PI), FVector(100, 200, 300), FVector(0.8, 1.2, 1));
	const FVector Camera(100, 200, 900);
	FVector Point;
	FVector2D UV;
	FString Reason;
	const FVector Outside = Display.TransformPosition(FVector(75, -70, 0));
	TestFalse(
		TEXT("原有捕获拾取仍拒绝显示面外的点"), UDreamSceneCapturePresentationComponent::MapViewRayToDisplayUV(
													Camera, Outside - Camera, Display, Bounds, Point, UV, Reason));
	TestTrue(TEXT("取出几何允许显示面外的点"), UDreamSceneCapturePresentationComponent::MapViewRayToDisplayUV(Camera,
												   Outside - Camera, Display, Bounds, Point, UV, Reason, false));
	TestTrue(TEXT("旋转与非均匀缩放下保留完整的面外 UV"), UV.Equals(FVector2D(1.25, -0.2), 0.001));
	TestTrue(TEXT("输出世界交点与鼠标实际指向一致"), Point.Equals(Outside, 0.001));
	TestFalse(TEXT("无限平面仍拒绝背面点击"),
		UDreamSceneCapturePresentationComponent::MapViewRayToDisplayUV(
			FVector(100, 200, 0), FVector::UpVector, Display, Bounds, Point, UV, Reason, false));
	TestFalse(TEXT("零射线不能被当成越界取出"), UDreamSceneCapturePresentationComponent::MapViewRayToDisplayUV(Camera,
													FVector::ZeroVector, Display, Bounds, Point, UV, Reason, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMiniatureExtractionLifecycleTest,
	"DreamSpace.Puzzle.Extraction.PreviewCancelAndSinglePhysicalDrop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMiniatureExtractionLifecycleTest::RunTest(const FString& Parameters)
{
	FExtractionFixture Scene;
	const FTransform SourceTransform = Scene.Source->GetActorTransform();
	Scene.Mesh->SetWorldScale3D(FVector(2, 1, 0.5));
	TestTrue(TEXT("房间模型不需要枢轴即可开始取出"), Scene.Begin());
	ADreamDroppedItem* Preview = Scene.Extract->GetPreviewItem();
	if (!TestNotNull(TEXT("创建缩小模型预览"), Preview))
		return false;
	UStaticMeshComponent* PreviewMesh = Preview->FindComponentByClass<UStaticMeshComponent>();
	USphereComponent* Sphere = Preview->FindComponentByClass<USphereComponent>();
	TestTrue(
		TEXT("抓取期间原模型隐藏并暂停碰撞"), Scene.Source->IsHidden() && !Scene.Source->GetActorEnableCollision());
	TestTrue(TEXT("预览复制真实网格、材质和组件世界缩放"),
		PreviewMesh->GetStaticMesh() == Scene.Mesh->GetStaticMesh() &&
			PreviewMesh->GetMaterial(0) == Scene.Mesh->GetMaterial(0) &&
			PreviewMesh->GetComponentScale().Equals(FVector(0.2, 0.1, 0.05), 0.001));
	TestTrue(TEXT("预览隐藏于 SceneCapture，尚未开启物理和碰撞"),
		PreviewMesh->bHiddenInSceneCapture && !Sphere->IsSimulatingPhysics() &&
			Sphere->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	TestFalse(TEXT("同一模型不能同时开始第二次取出"), Scene.Begin());
	Scene.Extract->UpdateExtract(Scene.DisplayPoint + FVector(0, 80, 0), FVector2D(1.01, 0.5), Scene.FrontNormal);
	TestFalse(TEXT("边缘阈值内松开会取消，不能误生成掉落物"), Scene.Extract->EndExtract(true));
	TestFalse(TEXT("取消后删除预览"), IsValid(Preview));
	TestTrue(
		TEXT("取消后恢复房间物体的碰撞和可见性"), !Scene.Source->IsHidden() && Scene.Source->GetActorEnableCollision());
	TestTrue(TEXT("取消不改变源 Actor 的姿态"), Scene.Source->GetActorLocation().Equals(SourceTransform.GetLocation()));

	Scene.Source->SetActorEnableCollision(false);
	Scene.Begin();
	Scene.Extract->EndExtract(false);
	TestFalse(TEXT("原来无碰撞的源模型取消后仍保持无碰撞"), Scene.Source->GetActorEnableCollision());
	Scene.Source->SetActorEnableCollision(true);
	TestTrue(TEXT("取消后可以重新抓取"), Scene.Begin());
	Scene.Extract->UpdateExtract(Scene.DisplayPoint + FVector(0, 90, 0), FVector2D(1.2, 0.5), Scene.FrontNormal);
	Preview = Scene.Extract->GetPreviewItem();
	TestTrue(TEXT("成功拖出并松开后提交"), Scene.Extract->EndExtract(true));
	TestFalse(TEXT("默认成功后移除整个源 Actor"), IsValid(Scene.Source));
	TestTrue(TEXT("掉落物继续存在并开启物理"),
		IsValid(Preview) && Preview->FindComponentByClass<USphereComponent>()->IsSimulatingPhysics());
	TestEqual(TEXT("掉落物球体不遮挡手办拾取"),
		Preview->FindComponentByClass<USphereComponent>()->GetCollisionResponseToChannel(ECC_Visibility), ECR_Ignore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMiniatureExtractionCleanupTest,
	"DreamSpace.Puzzle.Extraction.BlockedDropAndInterruptedPreviewRestoreSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMiniatureExtractionCleanupTest::RunTest(const FString& Parameters)
{
	FExtractionFixture Scene;
	TestTrue(TEXT("开始阻挡测试的预览"), Scene.Begin());
	Scene.Extract->UpdateExtract(Scene.DisplayPoint + FVector(0, 90, 0), FVector2D(-0.2, 0.5), Scene.FrontNormal);
	AActor* Blocker = Scene.World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Blocker);
	Box->SetMobility(EComponentMobility::Movable);
	Box->SetBoxExtent(FVector(40));
	Box->SetCollisionProfileName(TEXT("BlockAll"));
	Blocker->SetRootComponent(Box);
	Blocker->AddInstanceComponent(Box);
	Box->RegisterComponent();
	Blocker->SetActorLocation(Scene.Extract->GetPreviewItem()->GetActorLocation());
	TestFalse(TEXT("释放位置落在墙内时取消，不能提交初始穿透刚体"), Scene.Extract->EndExtract(true));
	TestTrue(TEXT("释放受阻后恢复源模型"), !Scene.Source->IsHidden() && Scene.Source->GetActorEnableCollision());
	Blocker->Destroy();

	Scene.Begin();
	ADreamDroppedItem* Preview = Scene.Extract->GetPreviewItem();
	Preview->Destroy();
	Scene.Extract->TickComponent(0.016f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("预览从外部销毁会结束会话"), Scene.Extract->IsExtracting());
	TestFalse(TEXT("预览销毁后不会遗留隐藏源模型"), Scene.Source->IsHidden());
	Scene.Begin();
	Preview = Scene.Extract->GetPreviewItem();
	Scene.Extract->Deactivate();
	TestFalse(TEXT("停用立即恢复源模型并删除预览"), Scene.Source->IsHidden() || IsValid(Preview));
	Scene.Extract->Activate(true);
	Scene.Begin();
	Preview = Scene.Extract->GetPreviewItem();
	Scene.Extract->DestroyComponent();
	TestFalse(TEXT("动态移除组件同样恢复源模型并删除预览"), Scene.Source->IsHidden() || IsValid(Preview));

	Scene.Extract = AddExtractionTestComponent<UDreamMiniatureExtractableComponent>(Scene.Source);
	SetExtractionBool(Scene.Extract, TEXT("bDestroySourceActor"), false);
	Scene.Begin();
	Scene.Extract->UpdateExtract(Scene.DisplayPoint + FVector(0, 90, 0), FVector2D(0.5, 1.2), Scene.FrontNormal);
	TestTrue(TEXT("可保留源 Actor 引用而完成取出"), Scene.Extract->EndExtract(true));
	TestTrue(TEXT("保留源 Actor 时永久隐藏、无碰撞且已取出"), IsValid(Scene.Source) && Scene.Source->IsHidden() &&
																  !Scene.Source->GetActorEnableCollision() &&
																  Scene.Extract->IsExtracted());
	TestFalse(TEXT("已经取出的源 Actor 不能再次生成掉落物"), Scene.Begin());
	Scene.Extract->EndExtract(false);
	TestTrue(TEXT("成功后的清理不能重新显示源模型"), Scene.Source->IsHidden());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMiniatureExtractionControllerTest,
	"DreamSpace.Puzzle.Extraction.ControllerCapturePickOutsideDragAndCancel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMiniatureExtractionControllerTest::RunTest(const FString& Parameters)
{
	FExtractionFixture Scene;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamCharacter* Character =
		Scene.World->SpawnActor<ADreamCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
	Scene.World->SpawnActor<ADreamSceneCaptureAnchor>();
	UDreamSceneCapturePresentationComponent* Miniature = Character->SceneMiniature;
	Miniature->BeginPlay();
	UStaticMeshComponent* Display = nullptr;
	TInlineComponentArray<UStaticMeshComponent*> CharacterMeshes(Character);
	for (UStaticMeshComponent* Mesh : CharacterMeshes)
		if (Mesh->GetFName() == TEXT("SceneCaptureDisplayMesh"))
			Display = Mesh;
	ASceneCapture2D* Capture = nullptr;
	for (AActor* Actor : Scene.World->PersistentLevel->Actors)
		if (ASceneCapture2D* Candidate = Cast<ASceneCapture2D>(Actor))
			Capture = Candidate;
	if (!TestNotNull(TEXT("真实表现层生成显示面"), Display) || !TestNotNull(TEXT("真实表现层生成捕获相机"), Capture))
		return false;
	ADreamPlayerController* Controller = Scene.World->SpawnActor<ADreamPlayerController>();
	Controller->Player = NewObject<ULocalPlayer>(GEngine);
	Scene.World->AddController(Controller);
	Controller->SpawnPlayerCameraManager();
	Controller->Possess(Character);
	Controller->SetViewTarget(Character);
	UDreamShoulderCameraComponent* ShoulderCamera = CastChecked<UDreamShoulderCameraComponent>(Character->CameraBoom);
	// 本测试手动注入固定投影，只验证取出生命周期；镜头过渡由专门的主视口相机测试覆盖。
	ShoulderCamera->MiniatureTransitionDuration = 0.0f;
	ShoulderCamera->Activate(true);
	ShoulderCamera->TickComponent(0.0f, LEVELTICK_All, nullptr);
	// Tab 现在会真正改变主相机与显示姿态。固定测试投影必须在进入模式后注入，
	// 不能被居中操作覆盖；测试仍走正常控制器取出入口，不绕过可见面片求交。
	auto ConfigureProjection = [&]()
	{
		Capture->SetActorLocationAndRotation(FVector(10000, 0, 1000), FRotator::ZeroRotator);
		Display->SetWorldTransform(
			FTransform(FRotationMatrix::MakeFromXZ(FVector::RightVector, -FVector::ForwardVector).ToQuat(),
				FVector(0, 300, 100), FVector(0.8)));
	};
	Controller->SetMiniatureInteractionMode(true);
	ConfigureProjection();
	const FVector Camera = Display->GetComponentLocation() - FVector(350, 0, 0);
	const FVector CenterRay = Display->GetComponentLocation() - Camera;
	Controller->SetIgnoreLookInput(true);
	UDreamDraggableComponent* OtherDrag = AddExtractionTestComponent<UDreamDraggableComponent>(Scene.Source);
	Controller->InteractWithMiniatureRay(Camera, CenterRay);
	TestTrue(TEXT("完整捕获拾取入口开始取出并锁定输入"),
		Controller->IsDraggingInteraction() && Scene.Extract->IsExtracting() && Controller->IsMoveInputIgnored());
	TestFalse(TEXT("同 Actor 的其它拖动组件不同时启动"), OtherDrag->IsDragging());
	ADreamDroppedItem* Preview = Scene.Extract->GetPreviewItem();
	if (!TestNotNull(TEXT("控制器生成预览"), Preview))
		return false;
	const FVector Outside = Display->GetComponentTransform().TransformPosition(FVector(75, 0, 0));
	TestTrue(TEXT("鼠标越过手办边缘后仍能驱动预览"), Controller->UpdateMiniatureExtractRay(Camera, Outside - Camera));
	TestTrue(TEXT("预览位于玩家旁边，而非捕获世界的远处模型位置"), Preview->GetActorLocation().X < 1000.0);
	Controller->CancelMiniatureDrag();
	TestFalse(TEXT("输入 Canceled 不提交，并结束取出会话"), IsValid(Preview) || Scene.Source->IsHidden() ||
		Controller->IsDraggingInteraction());
	TestTrue(TEXT("取消取出后仍保持 Tab 居中观察的移动锁"), Controller->IsMoveInputIgnored());
	TestTrue(TEXT("取消后保留其他系统的视角锁"), Controller->IsLookInputIgnored());
	Controller->SetIgnoreLookInput(false);

	Controller->InteractWithMiniatureRay(Camera, CenterRay);
	Controller->UpdateMiniatureExtractRay(Camera, Outside - Camera);
	Controller->SetMiniatureInteractionMode(false);
	TestFalse(TEXT("Tab 模式切换取消面外取出并恢复输入"),
		Scene.Source->IsHidden() || Controller->IsDraggingInteraction() || Controller->IsLookInputIgnored() ||
			Controller->IsMoveInputIgnored());
	Controller->SetMiniatureInteractionMode(true);
	ConfigureProjection();
	Controller->InteractWithMiniatureRay(Camera, CenterRay);
	Controller->UpdateMiniatureExtractRay(Camera, Outside - Camera);
	Controller->UnPossess();
	TestFalse(TEXT("失去角色时取消而非提交"), Scene.Source->IsHidden() || Controller->IsDraggingInteraction());
	Controller->Possess(Character);
	// 失去角色现在会同时退出 Tab，重新控制后需重新进入观察，旧会话不会续接。
	Controller->SetMiniatureInteractionMode(true);
	ConfigureProjection();
	Controller->InteractWithMiniatureRay(Camera, CenterRay);
	Controller->UpdateMiniatureExtractRay(Camera, Outside - Camera);
	Preview = Scene.Extract->GetPreviewItem();
	// 无原生游戏视口的自动化注入最后一帧视线，再走正常松开的提交入口。
	Controller->EndActiveDrag(true);
	Controller->SetMiniatureInteractionMode(false);
	TestTrue(TEXT("正常提交后只保留物理掉落物并释放控制器输入锁"),
		!IsValid(Scene.Source) && IsValid(Preview) &&
			Preview->FindComponentByClass<USphereComponent>()->IsSimulatingPhysics() &&
			!Controller->IsDraggingInteraction() && !Controller->IsMoveInputIgnored() &&
			!Controller->IsLookInputIgnored());
	return true;
}
#endif
