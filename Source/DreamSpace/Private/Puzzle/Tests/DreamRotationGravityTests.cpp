#include "DreamRotatableComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DreamCharacter.h"
#include "DreamPivotPointComponent.h"
#include "DreamPlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace
{
	/** 用 UE 反射模拟详情面板配置，测试不依赖关卡资产，也不增加仅供测试的公开接口。 */
	void SetBool(UDreamRotatableComponent* Rotation, const TCHAR* Name, bool Value)
	{
		FBoolProperty* Property = FindFProperty<FBoolProperty>(Rotation->GetClass(), Name);
		check(Property);
		Property->SetPropertyValue_InContainer(Rotation, Value);
	}

	void SetFloat(UDreamRotatableComponent* Rotation, const TCHAR* Name, float Value)
	{
		FFloatProperty* Property = FindFProperty<FFloatProperty>(Rotation->GetClass(), Name);
		check(Property);
		Property->SetPropertyValue_InContainer(Rotation, Value);
	}

	/** 碰撞体可以挂在无碰撞的根下，与项目现有的机关蓝图结构一致。 */
	UBoxComponent* AddBox(AActor* Actor, const FVector& Extent, USceneComponent* Parent = nullptr)
	{
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Box->SetMobility(EComponentMobility::Movable);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		if (Parent)
			Box->SetupAttachment(Parent);
		else
			Actor->SetRootComponent(Box);
		Actor->AddInstanceComponent(Box);
		Box->RegisterComponent();
		return Box;
	}

	/**
	 * 每项测试创建独立的物理世界和真实 DreamCharacter，离开作用域即销毁。
	 * 不保存用户关卡；场景只有薄平台、偏心枢轴和玩家，便于区分平台运动与角色移动。
	 */
	struct FGravityFixture
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		AActor* Platform = nullptr;
		UBoxComponent* Floor = nullptr;
		UDreamPivotPointComponent* Pivot = nullptr;
		UDreamRotatableComponent* Rotation = nullptr;
		ADreamCharacter* Character = nullptr;
		ADreamPlayerController* Controller = nullptr;

		FGravityFixture()
		{
			Platform = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(Platform);
			Root->SetMobility(EComponentMobility::Movable);
			Platform->SetRootComponent(Root);
			Platform->AddInstanceComponent(Root);
			Root->RegisterComponent();
			Floor = AddBox(Platform, FVector(200.0f, 200.0f, 10.0f), Root);

			Pivot = NewObject<UDreamPivotPointComponent>(Platform);
			Pivot->SetupAttachment(Root);
			Pivot->SetRelativeLocation(FVector(0.0f, 60.0f, 0.0f));
			Platform->AddInstanceComponent(Pivot);
			Pivot->RegisterComponent();

			Rotation = NewObject<UDreamRotatableComponent>(Platform);
			Platform->AddInstanceComponent(Rotation);
			Rotation->RegisterComponent();
			FEnumProperty* Axis = FindFProperty<FEnumProperty>(Rotation->GetClass(), TEXT("RotationAxis"));
			check(Axis);
			Axis->GetUnderlyingProperty()->SetIntPropertyValue(Axis->ContainerPtrToValuePtr<void>(Rotation),
				static_cast<uint64>(EDreamPivotRotationAxis::X));
			SetFloat(Rotation, TEXT("RotationDuration"), 1.0f);

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Character = World->SpawnActor<ADreamCharacter>(FVector(30.0f, 60.0f, 108.0f),
				FRotator::ZeroRotator, SpawnParams);
			Controller = World->SpawnActor<ADreamPlayerController>();
			// UE5.8 没有 NetDriver 时根据 ULocalPlayer 判断本地控制器；缺少它会跳过移动更新，
			// 即便手动 Tick 也无法验证行走或跳跃。这里补齐身份，不需要创建真实屏幕视口。
			Controller->Player = NewObject<ULocalPlayer>(GEngine);
			Controller->Possess(Character);
			Controller->SetControlRotation(FRotator(-15.0f, 0.0f, 0.0f));
			// 编辑器构建下 CharacterMovement 会在世界开始播放后的前 1 秒暂缓进入 Falling，
			// 测试直接标记播放状态，避免该引擎保护分支掩盖本功能的离台行为。
			World->SetBegunPlay(true);
			World->TimeSeconds = 2.0;
			UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
			Movement->bRunPhysicsWithNoController = true;
			Movement->SetMovementMode(MOVE_Walking);
			// 先做真实地面查询并建立底座，不用 SetBase 硬把一个空中角色标记成站立。
			Movement->FindFloor(Character->GetActorLocation(), Movement->CurrentFloor, false);
			Movement->SetBaseFromFloor(Movement->CurrentFloor);
		}

		~FGravityFixture()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
		}

		void EnableGravity()
		{
			SetBool(Rotation, TEXT("bRotateStandingCharacterGravity"), true);
		}

		/** 障碍只挡角色上半身的旋转弧线，平台本身不会碰到它。 */
		void AddHeadBlocker()
		{
			AActor* Blocker = World->SpawnActor<AActor>();
			AddBox(Blocker, FVector(12.0f));
			Blocker->SetActorLocation(FVector(30.0f, -70.0f, 150.0f));
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRotationGravityDisabledTest,
	"DreamSpace.Puzzle.RotationGravity.DisabledCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRotationGravityDisabledTest::RunTest(const FString& Parameters)
{
	FGravityFixture Scene;
	const FTransform Start = Scene.Character->GetActorTransform();
	SetFloat(Scene.Rotation, TEXT("RotationDuration"), 0.0f);
	Scene.Rotation->TriggerRotation();
	TestTrue(TEXT("默认关闭开关仍让平台完成绕 X 轴 90 度旋转"),
		Scene.Platform->GetActorQuat().GetAxisZ().Equals(FVector(0.0f, -1.0f, 0.0f), 0.001f));
	TestTrue(TEXT("默认关闭开关不改变角色重力"),
		Scene.Character->GetCharacterMovement()->GetGravityDirection().Equals(FVector::DownVector));
	TestTrue(TEXT("默认关闭开关不额外搬运角色，保留原生底座行为"),
		Scene.Character->GetActorTransform().Equals(Start, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRotationGravityWalkingTest,
	"DreamSpace.Puzzle.RotationGravity.SmoothStandingWalkingAndLeaving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRotationGravityWalkingTest::RunTest(const FString& Parameters)
{
	FGravityFixture Scene;
	Scene.EnableGravity();
	UCharacterMovementComponent* Movement = Scene.Character->GetCharacterMovement();
	TestTrue(TEXT("测试角色具备可执行输入驱动移动的本地玩家身份"), Scene.Character->IsLocallyControlled());
	TestTrue(TEXT("测试角色通过真实地面查询站在平台上"), Movement->GetMovementBaseObject() == Scene.Floor);
	const FVector StartLocal = Scene.Platform->GetActorTransform().InverseTransformPosition(Scene.Character->GetActorLocation());
	const FQuat StartView = Scene.Controller->GetControlRotation().Quaternion();
	Scene.Rotation->TriggerRotation();
	bool bStayedGrounded = true;
	bool bStayedAligned = true;
	// 交错模拟两个 Tick 顺序，验证刷新底座缓存后不会漏搬运或重复搬运角色。
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		if (Frame % 2 == 0)
			Movement->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
		Scene.Rotation->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
		if (Frame % 2 != 0)
			Movement->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
		Scene.Controller->UpdateRotation(1.0f / 60.0f);
		bStayedGrounded &= Movement->IsMovingOnGround() && Movement->GetMovementBaseObject() == Scene.Floor;
		bStayedAligned &= Scene.Character->GetActorUpVector().Equals(-Movement->GetGravityDirection(), 0.001f);
	}
	// 浮点累加可能让第 60 帧距 1 秒差少许，最后补帧只用于精确完成动画。
	Scene.Rotation->TickComponent(0.01f, LEVELTICK_All, nullptr);
	Movement->TickComponent(0.01f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("逐帧倾斜全过程仍然站在真实平台底座上"), bStayedGrounded);
	TestTrue(TEXT("逐帧旋转全过程胶囊向上与重力反方向一致"), bStayedAligned);
	TestTrue(TEXT("绕 X 正转 90 度后世界重力为 +Y"),
		Movement->GetGravityDirection().Equals(FVector(0.0f, 1.0f, 0.0f), 0.001f));
	TestTrue(TEXT("角色局部位置保持稳定，没有重复底座搬运或离地"),
		Scene.Platform->GetActorTransform().InverseTransformPosition(Scene.Character->GetActorLocation()).Equals(StartLocal, 0.1f));
	TestTrue(TEXT("视角完整跟随平台旋转，控制器更新没有叠加第二次旋转"),
		Scene.Controller->GetControlRotation().Quaternion().Equals(Scene.Platform->GetActorQuat() * StartView, 0.001f));

	// 使用项目实际 DoMove 入口向前走，验证新重力平面的移动和离开边缘后的下落。
	const FVector WallStart = Scene.Character->GetActorLocation();
	for (int32 Frame = 0; Frame < 75; ++Frame)
	{
		Scene.Character->DoMove(0.0f, 1.0f);
		Movement->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	}
	TestTrue(TEXT("旋转后可以沿平台表面正常向前走"), Scene.Character->GetActorLocation().X > WallStart.X + 220.0f);
	TestTrue(TEXT("走下平台边缘后进入下落，未被平台强制吸附"), Movement->IsFalling());
	const FVector GravityAfterLeaving = Movement->GetGravityDirection();
	TestTrue(TEXT("走下平台后保留 +Y 重力"), GravityAfterLeaving.Equals(FVector(0.0f, 1.0f, 0.0f), 0.001f));
	TestTrue(TEXT("离开平台后沿新的世界重力方向下落"), Movement->Velocity.Y > 1.0f);
	Scene.Rotation->TriggerRotation();
	Scene.Rotation->TickComponent(1.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("离开后原平台再次转动不会影响角色重力"), Movement->GetGravityDirection().Equals(GravityAfterLeaving));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRotationGravityInstantTest,
	"DreamSpace.Puzzle.RotationGravity.InstantAndRepeatedRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRotationGravityInstantTest::RunTest(const FString& Parameters)
{
	FGravityFixture Scene;
	Scene.EnableGravity();
	SetBool(Scene.Rotation, TEXT("bConsiderCollision"), true);
	SetFloat(Scene.Rotation, TEXT("RotationDuration"), 0.0f);
	UCharacterMovementComponent* Movement = Scene.Character->GetCharacterMovement();
	const FVector StartLocation = Scene.Character->GetActorLocation();
	const FVector PivotLocation = Scene.Pivot->GetComponentLocation();
	const FQuat StartView = Scene.Controller->GetControlRotation().Quaternion();
	Movement->Velocity = FVector(80.0f, 0.0f, 0.0f);
	for (int32 Step = 1; Step <= 4; ++Step)
	{
		Scene.Rotation->TriggerRotation();
		const FQuat Turn(FVector::ForwardVector, FMath::DegreesToRadians(90.0f * Step));
		TestTrue(TEXT("连续瞬时转动累计当前重力，可经过墙面、天花板并回到世界向下"),
			Movement->GetGravityDirection().Equals(Turn.RotateVector(FVector::DownVector), 0.001f));
		TestTrue(TEXT("开启碰撞时乘客不会误挡平台，偏心枢轴公转位置正确"),
			Scene.Character->GetActorLocation().Equals(PivotLocation + Turn.RotateVector(StartLocation - PivotLocation), 0.1f));
		TestTrue(TEXT("瞬时旋转后的视角保持完整转轴信息"),
			Scene.Controller->GetControlRotation().Quaternion().Equals(Turn * StartView, 0.001f));
	}
	TestTrue(TEXT("连续旋转没有清零行走速度"), Movement->Velocity.Equals(FVector(80.0f, 0.0f, 0.0f), 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRotationGravityJumpTest,
	"DreamSpace.Puzzle.RotationGravity.JumpAndStandingSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRotationGravityJumpTest::RunTest(const FString& Parameters)
{
	FGravityFixture Scene;
	Scene.EnableGravity();
	UCharacterMovementComponent* Movement = Scene.Character->GetCharacterMovement();
	Scene.Rotation->TriggerRotation();
	Scene.Rotation->TickComponent(0.4f, LEVELTICK_All, nullptr);
	Movement->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	const FVector GravityAtJump = Movement->GetGravityDirection();
	Scene.Character->DoJumpStart();
	Movement->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("倾斜平台上仍可通过实际跳跃输入离开"), Movement->IsFalling());
	TestTrue(TEXT("跳跃沿当前重力反方向起跳"), FVector::DotProduct(Movement->Velocity, -GravityAtJump) > 400.0f);
	// 模拟开启 StayBasedInAir 的角色：即使空中保留底座，也不应继续改变重力。
	FMovementBaseInterfaceData AirBase = MovementBaseUtility::GetMovementBaseDataFromPhysicsOwner(Scene.Floor);
	Scene.Character->SetBase(&AirBase);
	const FTransform JumpTransform = Scene.Character->GetActorTransform();
	Scene.Rotation->TickComponent(0.6f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("空中保留底座的角色也停止跟随，且保留起跳时重力"), Movement->GetGravityDirection().Equals(GravityAtJump));
	TestTrue(TEXT("平台不会强制搬运已经跳离的角色"), Scene.Character->GetActorTransform().Equals(JumpTransform, 0.001f));

	// 地面移动模式本身不代表站在本平台；没有底座的邻近角色也必须被排除。
	Scene.Character->SetActorLocation(FVector(600.0f, 0.0f, 300.0f));
	Movement->SetMovementMode(MOVE_Walking);
	Scene.Character->SetBase(static_cast<FMovementBaseInterfaceData*>(nullptr));
	const FVector NearbyGravity = Movement->GetGravityDirection();
	Scene.Rotation->TriggerRotation();
	Scene.Rotation->TickComponent(1.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("邻近但未站在本平台的角色不受重力开关影响"), Movement->GetGravityDirection().Equals(NearbyGravity));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRotationGravityCollisionTest,
	"DreamSpace.Puzzle.RotationGravity.PassengerCollisionReturn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRotationGravityCollisionTest::RunTest(const FString& Parameters)
{
	FGravityFixture Scene;
	Scene.EnableGravity();
	SetBool(Scene.Rotation, TEXT("bConsiderCollision"), true);
	Scene.AddHeadBlocker();
	const FTransform Start = Scene.Character->GetActorTransform();
	const FQuat StartView = Scene.Controller->GetControlRotation().Quaternion();
	Scene.Rotation->TriggerRotation();
	Scene.Rotation->TickComponent(0.2f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("碰到角色头部障碍之前，平台和乘客已有旋转进度"),
		!Scene.Character->GetCharacterMovement()->GetGravityDirection().Equals(FVector::DownVector));
	Scene.Rotation->TickComponent(0.3f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("乘客被挡时整座平台没有继续转到 90 度"), Scene.Platform->GetActorQuat().GetAxisZ().Z > 0.5f);
	Scene.Rotation->TickComponent(1.0f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("乘客受阻后回弹完成，可以重新交互"), Scene.Rotation->IsRotating());
	TestTrue(TEXT("回弹后恢复平台起始姿态"), Scene.Platform->GetActorQuat().Equals(FQuat::Identity, 0.001f));
	TestTrue(TEXT("仍站在平台上的角色随回弹恢复位置和胶囊朝向"), Scene.Character->GetActorTransform().Equals(Start, 0.1f));
	TestTrue(TEXT("回弹后站立角色重力和相机都同步恢复"),
		Scene.Character->GetCharacterMovement()->GetGravityDirection().Equals(FVector::DownVector, 0.001f)
		&& Scene.Controller->GetControlRotation().Quaternion().Equals(StartView, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRotationGravityInstantCollisionTest,
	"DreamSpace.Puzzle.RotationGravity.InstantCollisionRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRotationGravityInstantCollisionTest::RunTest(const FString& Parameters)
{
	FGravityFixture Scene;
	Scene.EnableGravity();
	SetBool(Scene.Rotation, TEXT("bConsiderCollision"), true);
	SetFloat(Scene.Rotation, TEXT("RotationDuration"), 0.0f);
	Scene.AddHeadBlocker();
	const FTransform Start = Scene.Character->GetActorTransform();
	Scene.Rotation->TriggerRotation();
	TestTrue(TEXT("0 秒受阻时平台立即恢复起点"), Scene.Platform->GetActorQuat().Equals(FQuat::Identity, 0.001f));
	TestTrue(TEXT("0 秒受阻时角色位置、朝向和重力没有残留中途进度"),
		Scene.Character->GetActorTransform().Equals(Start, 0.1f)
		&& Scene.Character->GetCharacterMovement()->GetGravityDirection().Equals(FVector::DownVector, 0.001f));
	return true;
}
#endif
