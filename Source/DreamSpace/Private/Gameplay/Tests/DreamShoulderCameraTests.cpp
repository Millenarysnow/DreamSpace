#include "DreamShoulderCameraComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DreamCharacter.h"
#include "DreamPlayerController.h"
#include "DreamSceneCapturePresentationComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * 测试真实相机 Socket 与物理扫掠，不复制实现中的距离公式来伪造结果。
	 * 每项测试使用独立瞬时世界；不打开、保存或改写用户关卡和二进制资产。
	 */
	struct FShoulderCameraFixture
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		ADreamCharacter* Character = nullptr;
		ADreamPlayerController* Controller = nullptr;
		UDreamShoulderCameraComponent* Camera = nullptr;

		FShoulderCameraFixture()
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Character = World->SpawnActor<ADreamCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
			Controller = World->SpawnActor<ADreamPlayerController>();
			// 补齐本地玩家身份，让本地主视角材质和真实输入规则可以运行，无需真实窗口。
			Controller->Player = NewObject<ULocalPlayer>(GEngine);
			// 瞬时世界没有执行完整关卡初始化，手动补齐正常 PostInitializeComponents 的相机职责。
			World->AddController(Controller);
			Controller->SpawnPlayerCameraManager();
			Controller->Possess(Character);
			Controller->SetViewTarget(Character);
			Controller->SetControlRotation(FRotator::ZeroRotator);
			Camera = CastChecked<UDreamShoulderCameraComponent>(Character->CameraBoom);
			Camera->Activate(true);
			Camera->ResetCameraState();
			Step();
		}

		~FShoulderCameraFixture()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
		}

		void Step(float DeltaTime = 1.0f / 60.0f)
		{
			Camera->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
		}

		void Advance(float Seconds, int32 FPS = 60)
		{
			for (int32 Frame = 0; Frame < FMath::RoundToInt(Seconds * FPS); ++Frame)
				Step(1.0f / FPS);
		}

		FVector Position() const
		{
			return Camera->GetSocketTransform(USpringArmComponent::SocketName, RTS_World).GetLocation();
		}

		/** 障碍只阻挡 Camera，故不会改变角色通行或 Visibility 交互规则。 */
		UBoxComponent* AddBlocker(const FVector& Location, const FVector& Extent)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
			Box->SetBoxExtent(Extent);
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionResponseToAllChannels(ECR_Ignore);
			Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
			Box->SetMobility(EComponentMobility::Movable);
			Actor->SetRootComponent(Box);
			Actor->AddInstanceComponent(Box);
			Box->RegisterComponent();
			Actor->SetActorLocation(Location);
			return Box;
		}

		/** 独立重叠查询验证最终相机球未在障碍内，不依赖组件内部的安全距离缓存。 */
		bool IsCameraClear() const
		{
			FCollisionQueryParams Params;
			Params.AddIgnoredActor(Character);
			return !World->OverlapBlockingTestByChannel(Position(), FQuat::Identity, ECC_Camera,
				FCollisionShape::MakeSphere(Camera->ProbeSize), Params);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamShoulderDefaultsTest,
	"DreamSpace.Camera.Shoulder.DefaultsAndDirectLook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamShoulderDefaultsTest::RunTest(const FString& Parameters)
{
	FShoulderCameraFixture Scene;
	TestTrue(TEXT("自由探索保留身体朝移动方向转身"), Scene.Character->GetCharacterMovement()->bOrientRotationToMovement);
	TestFalse(TEXT("人物不强制朝控制器方向横移"), Scene.Character->bUseControllerRotationYaw);
	TestEqual(TEXT("越肩理想距离为 210 cm"), Scene.Camera->TargetArmLength, 210.0f);
	TestEqual(TEXT("右肩偏移为 45 cm"), Scene.Camera->SocketOffset.Y, 45.0);
	TestTrue(TEXT("开放空间实际相机位于右肩理想位置"), Scene.Position().Equals(FVector(-210, 45, 60), 0.01));
	TestEqual(TEXT("主相机 FOV 为 80 度"), Scene.Character->FollowCamera->FieldOfView, 80.0f);

	Scene.Controller->SetControlRotation(FRotator(-20, 90, 0));
	Scene.Step();
	TestTrue(TEXT("观察旋转一帧内采用控制器方向，没有旋转阻尼"),
		Scene.Camera->GetSocketTransform(USpringArmComponent::SocketName, RTS_World).GetRotation().Equals(
			Scene.Controller->GetControlRotation().Quaternion(), 0.001));
	TestTrue(TEXT("旋转后仍遵守相机空间肩偏移"), Scene.Position().Equals(
		Scene.Camera->GetComponentLocation() + Scene.Controller->GetControlRotation().RotateVector(FVector(-210, 45, 0)), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamShoulderZoomTest,
	"DreamSpace.Camera.Shoulder.SmoothZoomAcrossFrameRates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamShoulderZoomTest::RunTest(const FString& Parameters)
{
	TArray<float> Lengths;
	for (const int32 FPS : {30, 60, 120})
	{
		FShoulderCameraFixture Scene;
		const FVector Start = Scene.Position();
		Scene.Camera->TargetArmLength = 300.0f;
		Scene.Step(1.0f / FPS);
		TestTrue(TEXT("滚轮目标改变后实际位置连续，未直接跳到目标"),
			Scene.Position().X < Start.X && Scene.Position().X > -300.0);
		// 总时间统一为 0.2 秒，先前单步也计入其中，比较不同帧率得到的最终距离。
		for (int32 Frame = 1; Frame < FMath::RoundToInt(0.2f * FPS); ++Frame)
			Scene.Step(1.0f / FPS);
		Lengths.Add(Scene.Camera->GetSmoothedArmLength());
		TestTrue(TEXT("开放空间实际距离只经过缩放这一层阻尼"), Scene.Position().Equals(
			Scene.Camera->GetIdealCameraTransform(Scene.Camera->GetSmoothedArmLength()).GetLocation(), 0.01));
		Scene.Camera->TargetArmLength = 150.0f;
		Scene.Advance(1.0f, FPS);
		TestTrue(TEXT("反向连续缩放能稳定到达近距离目标"),
			FMath::IsNearlyEqual(Scene.Camera->GetSmoothedArmLength(), 150.0f, 0.05f));
	}
	TestTrue(TEXT("30 和 60 FPS 在相同时间后缩放结果一致"), FMath::IsNearlyEqual(Lengths[0], Lengths[1], 0.001f));
	TestTrue(TEXT("60 和 120 FPS 在相同时间后缩放结果一致"), FMath::IsNearlyEqual(Lengths[1], Lengths[2], 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamShoulderCollisionRecoveryTest,
	"DreamSpace.Camera.Shoulder.SafeRetractionDelayedRecoveryAndFlicker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamShoulderCollisionRecoveryTest::RunTest(const FString& Parameters)
{
	FShoulderCameraFixture Scene;
	const FVector OpenPosition = Scene.Position();
	UBoxComponent* Wall = Scene.AddBlocker(FVector(-100, 0, 60), FVector(10, 500, 500));
	Scene.Step();
	TestTrue(TEXT("墙突然挡住镜头时第一帧就收进安全区域"), Scene.Position().X > -72.0);
	TestTrue(TEXT("第一帧实际相机球没有穿墙"), Scene.IsCameraClear());
	TestEqual(TEXT("碰撞没有改写玩家理想缩放距离"), Scene.Camera->TargetArmLength, 210.0f);
	Scene.Advance(0.5f);
	const FVector Retracted = Scene.Position();

	Wall->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Scene.Step(0.1f);
	TestTrue(TEXT("短暂无遮挡仍保持收近位置，过滤门框边缘"), Scene.Position().Equals(Retracted, 0.01));
	Scene.Step(0.25f);
	TestTrue(TEXT("等待结束后开始缓慢恢复，没有直接跳回原距离"),
		Scene.Position().X < Retracted.X && Scene.Position().X > OpenPosition.X + 1.0);
	Scene.Advance(2.0f);
	TestTrue(TEXT("持续开放空间最终恢复理想构图"), Scene.Position().Equals(OpenPosition, 0.15));

	// 人为制造连续的命中/未命中交替，验证“无遮挡一帧”不会触发长距离回弹。
	Wall->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
	Scene.Step();
	Scene.Advance(0.5f);
	bool bNoOutwardJumps = true;
	bool bAlwaysClear = true;
	for (int32 Frame = 0; Frame < 40; ++Frame)
	{
		const double PreviousX = Scene.Position().X;
		Wall->SetCollisionResponseToChannel(ECC_Camera, Frame % 2 == 0 ? ECR_Ignore : ECR_Block);
		Scene.Step();
		bNoOutwardJumps &= Scene.Position().X >= PreviousX - 0.1;
		bAlwaysClear &= Scene.IsCameraClear();
	}
	TestTrue(TEXT("障碍交替命中时没有快速向外抽动"), bNoOutwardJumps);
	TestTrue(TEXT("障碍交替命中时每帧都没有穿墙"), bAlwaysClear);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamShoulderNarrowingTest,
	"DreamSpace.Camera.Shoulder.NarrowShoulderAndRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamShoulderNarrowingTest::RunTest(const FString& Parameters)
{
	FShoulderCameraFixture Scene;
	// 狭窄柱只占右肩路径，中轴仍有空间。整面后墙不用于此测试，因为左右都被挡时不应收肩。
	UBoxComponent* Pillar = Scene.AddBlocker(FVector(-170, 45, 60), FVector(20, 6, 150));
	bool bAlwaysClear = true;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Scene.Step();
		bAlwaysClear &= Scene.IsCameraClear();
	}
	TestTrue(TEXT("肩侧受阻时明显收窄肩位"), Scene.Camera->GetShoulderWeight() < 0.5f);
	TestTrue(TEXT("实际镜头靠近中轴，并获得更大的后退空间"), Scene.Position().Y < 22.5 && Scene.Position().X < -100.0);
	TestTrue(TEXT("肩位插值过程中相机球始终安全"), bAlwaysClear);
	TestEqual(TEXT("收肩不反写理想肩偏移"), Scene.Camera->SocketOffset.Y, 45.0);
	const float Narrowed = Scene.Camera->GetShoulderWeight();
	Pillar->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Scene.Step(0.1f);
	TestTrue(TEXT("肩位不会在短暂无遮挡时立刻弹回"), FMath::IsNearlyEqual(Scene.Camera->GetShoulderWeight(), Narrowed, 0.001f));
	Scene.Advance(2.0f);
	TestTrue(TEXT("障碍消失后平滑恢复完整右肩"), Scene.Camera->GetShoulderWeight() > 0.99f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamShoulderMovingCollisionTest,
	"DreamSpace.Camera.Shoulder.MovementRotationAndChannelIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamShoulderMovingCollisionTest::RunTest(const FString& Parameters)
{
	FShoulderCameraFixture Scene;
	UBoxComponent* Wall = Scene.AddBlocker(FVector(-250, 0, 60), FVector(10, 500, 500));
	bool bAlwaysClear = true;
	// 同时靠近墙面和转动视角，覆盖静止转镜头之外的另一类室内碰撞。
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		Scene.Character->SetActorLocation(FVector(-Frame, 0, 0));
		Scene.Controller->SetControlRotation(FRotator(0, FMath::Sin(Frame * 0.05f) * 45.0f, 0));
		Scene.Step();
		bAlwaysClear &= Scene.IsCameraClear();
	}
	TestTrue(TEXT("人物靠墙移动并转镜头时最终相机球每帧都安全"), bAlwaysClear);
	TestTrue(TEXT("相机使用独立 Camera 通道"), Wall->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Ignore
		&& Wall->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore);
	Wall->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Scene.Advance(2.0f);
	TestFalse(TEXT("忽略 Camera 的装饰不再触发镜头收近"), Scene.Camera->IsCollisionFixApplied());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamShoulderGravityAndTeleportTest,
	"DreamSpace.Camera.Shoulder.GravityHeightFollowAndTeleport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamShoulderGravityAndTeleportTest::RunTest(const FString& Parameters)
{
	FShoulderCameraFixture Scene;
	// 非零世界偏移应在重力翻转中保持世界方向，不能和局部胶囊挂点一起旋转。
	Scene.Camera->TargetOffset = FVector(15, 25, 10);
	Scene.Camera->ResetCameraState();
	Scene.Step();
	const FVector Start = Scene.Position();
	Scene.Character->SetActorLocation(FVector(0, 0, 20));
	Scene.Step();
	TestTrue(TEXT("高度阶跃只产生适量跟随，不直接跳完整 20 cm"), Scene.Position().Z > Start.Z && Scene.Position().Z < Start.Z + 20.0);
	const FVector BeforeTurn = Scene.Position();
	const FVector BeforeOwner = Scene.Character->GetActorLocation();
	const FQuat Turn(FVector::ForwardVector, PI * 0.5f);
	Scene.Character->SetActorRotation(Turn);
	Scene.Character->GetCharacterMovement()->SetGravityDirection(Turn.RotateVector(FVector::DownVector));
	Scene.Controller->SetControlRotation(Turn.Rotator());
	// 0 秒更新只改变参考系，保持同样的高度滞后；预期整个镜头围绕人物做相同的刚体旋转。
	Scene.Step(0.0f);
	TestTrue(TEXT("重力改变时高度滞后和肩位一起进入新参考系"),
		Scene.Position().Equals(BeforeOwner + Scene.Camera->TargetOffset
			+ Turn.RotateVector(BeforeTurn - BeforeOwner - Scene.Camera->TargetOffset), 0.01));
	TestTrue(TEXT("相机上方向与新重力反方向一致"),
		Scene.Camera->GetSocketTransform(USpringArmComponent::SocketName, RTS_World).GetRotation().GetAxisZ().Equals(
			-Scene.Character->GetCharacterMovement()->GetGravityDirection(), 0.001));

	Scene.Character->SetActorLocation(FVector(600, 0, 20));
	Scene.Step();
	const FVector Expected = Scene.Camera->GetComponentLocation() + Scene.Camera->TargetOffset
		+ Turn.RotateVector(FVector(-210, 45, 0));
	TestTrue(TEXT("远距离传送清除高度历史，不从旧位置慢慢追赶"), Scene.Position().Equals(Expected, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamShoulderOwnerLocalClipTest,
	"DreamSpace.Camera.Shoulder.ProgressiveOwnerLocalClipping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamShoulderOwnerLocalClipTest::RunTest(const FString& Parameters)
{
	FShoulderCameraFixture Scene;
	USkeletalMeshComponent* Mesh = Scene.Character->GetMesh();
	const bool OriginalOwnerNoSee = Mesh->bOwnerNoSee;
	TestEqual(TEXT("开放空间局部剔除为零"), Scene.Camera->GetOwnerClipAmount(), 0.0f);
	TestTrue(TEXT("主视角带有材质剔除专用标记"), Scene.Character->FollowCamera->PostProcessSettings.bOverride_UserFlags
		&& (Scene.Character->FollowCamera->PostProcessSettings.UserFlags & UDreamShoulderCameraComponent::OwnerClipViewFlag) != 0);
	TArray<UMaterialInstanceDynamic*> Materials;
	for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
	{
		UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(Slot));
		if (!TestNotNull(TEXT("每个身体材质槽已接入本角色独立 MID"), Material))
			return false;
		Materials.Add(Material);
	}

	UBoxComponent* Wall = Scene.AddBlocker(FVector(-35, 0, 60), FVector(5, 500, 500));
	Scene.Step();
	TestEqual(TEXT("挤入身体也不切换 OwnerNoSee"), static_cast<bool>(Mesh->bOwnerNoSee), OriginalOwnerNoSee);
	TestFalse(TEXT("身体没有被全局隐藏"), Mesh->bHiddenInGame);
	TestTrue(TEXT("首帧开始过渡而非切换整个身体"), Scene.Camera->GetOwnerClipAmount() > 0.0f
		&& Scene.Camera->GetOwnerClipAmount() < 0.5f);
	TestTrue(TEXT("局部剔除继续保持实际相机球安全"), Scene.IsCameraClear());
	Scene.Advance(1.0f);
	TestTrue(TEXT("极近时渐进到完整局部剔除"), Scene.Camera->GetOwnerClipAmount() > 0.99f);
	TestTrue(TEXT("范围扩大且受最大半径限制"), Scene.Camera->GetOwnerClipRadius() > Scene.Camera->OwnerClipMinRadius
		&& Scene.Camera->GetOwnerClipRadius() <= Scene.Camera->OwnerClipMaxRadius);
	for (UMaterialInstanceDynamic* Material : Materials)
	{
		TestEqual(TEXT("身体所有槽采用同样连续强度"), Material->K2_GetScalarParameterValue(TEXT("DreamOwnerClipAmount")),
			Scene.Camera->GetOwnerClipAmount());
		TestEqual(TEXT("材质外半径与实际缓存一致"), Material->K2_GetScalarParameterValue(TEXT("DreamOwnerClipRadius")),
			Scene.Camera->GetOwnerClipRadius());
		const FLinearColor Center = Material->K2_GetVectorParameterValue(TEXT("DreamOwnerClipCameraLocal"));
		TestTrue(TEXT("材质核对位置来自真实镜头的网格局部坐标"), FVector(Center.R, Center.G, Center.B).Equals(
			Mesh->GetComponentTransform().InverseTransformPosition(Scene.Position()), 0.001));
	}

	// 只移走骨骼网格，人物胶囊与相机不动。若仍使用人物中心距离，剔除就无法恢复。
	const FVector MeshLocation = Mesh->GetRelativeLocation();
	Mesh->AddLocalOffset(FVector(600, 0, 0));
	const float BeforeRestore = Scene.Camera->GetOwnerClipAmount();
	Scene.Step();
	TestTrue(TEXT("表面离远后开始恢复，且不瞬间归零"), Scene.Camera->GetOwnerClipAmount() < BeforeRestore
		&& Scene.Camera->GetOwnerClipAmount() > 0.0f);
	TestTrue(TEXT("表面距离随骨骼网格变化，而非固定采用人物中心距离"),
		Scene.Camera->GetOwnerSurfaceDistance() > Scene.Camera->OwnerClipStartDistance);
	Scene.Advance(2.0f);
	TestTrue(TEXT("不再接近身体时完整恢复"), Scene.Camera->GetOwnerClipAmount() < 0.0001f);
	Mesh->SetRelativeLocation(MeshLocation);
	Wall->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Scene.Advance(2.0f);
	TestTrue(TEXT("无遮挡后范围回到最小值"), FMath::IsNearlyEqual(Scene.Camera->GetOwnerClipRadius(),
		Scene.Camera->OwnerClipMinRadius, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamShoulderClipLifecycleTest,
	"DreamSpace.Camera.Shoulder.OwnerClipMaterialIsolationAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamShoulderClipLifecycleTest::RunTest(const FString& Parameters)
{
	FShoulderCameraFixture Scene;
	USkeletalMeshComponent* Mesh = Scene.Character->GetMesh();
	UMaterialInstanceDynamic* InitialMID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
	if (!TestNotNull(TEXT("角色材质初始化完成"), InitialMID))
		return false;
	UMaterialInterface* Original = InitialMID->Parent;
	Scene.AddBlocker(FVector(-35, 0, 60), FVector(5, 500, 500));
	Scene.Advance(1.0f);
	// 未被当前控制器占有的第二个角色不能共享第一角色的 MID，更不能共享剔除强度。
	ADreamCharacter* Other = Scene.World->SpawnActor<ADreamCharacter>(FVector(600, 0, 0), FRotator::ZeroRotator);
	TestTrue(TEXT("其他角色仍使用原材质实例"), Other->GetMesh()->GetMaterial(0) == Original);
	TestFalse(TEXT("其他角色没有被隐藏"), Other->GetMesh()->bOwnerNoSee);

	Scene.Camera->bEnableOwnerLocalClipping = false;
	Scene.Step();
	TestTrue(TEXT("关闭局部剔除立即还原原材质"), Mesh->GetMaterial(0) == Original);
	TestEqual(TEXT("外部持有的旧 MID 也清零"), InitialMID->K2_GetScalarParameterValue(TEXT("DreamOwnerClipAmount")), 0.0f);
	Scene.Camera->bEnableOwnerLocalClipping = true;
	Scene.Step();
	Scene.Camera->ResetCameraState();
	TestTrue(TEXT("重置时恢复原材质"), Mesh->GetMaterial(0) == Original);
	Scene.Step();
	Scene.Camera->Deactivate();
	TestTrue(TEXT("停用时不等待 Tick 即恢复原材质"), Mesh->GetMaterial(0) == Original);
	Scene.Camera->Activate(true);
	Scene.Step();
	Scene.Controller->SetViewTarget(Other);
	Scene.Step();
	TestTrue(TEXT("主视角切到其他角色时清理旧角色材质"), Mesh->GetMaterial(0) == Original);
	Scene.Controller->SetViewTarget(Scene.Character);
	Scene.Step();

	// 外部换装发生在两帧之间时，清理不能把它覆盖回老材质。
	UMaterialInterface* Replacement = Mesh->GetMaterial(1);
	Mesh->SetMaterial(0, Replacement);
	Scene.Camera->ResetCameraState();
	TestTrue(TEXT("恢复只修改本组件仍控制的材质槽"), Mesh->GetMaterial(0) == Replacement);
	Mesh->SetMaterial(0, Original);
	Scene.Step();
	Scene.Controller->UnPossess();
	Scene.Step();
	TestTrue(TEXT("失去本地控制后清理材质"), Mesh->GetMaterial(0) == Original);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamShoulderClipFrameRateTest,
	"DreamSpace.Camera.Shoulder.LocalClipFrameRatesAndCapsuleFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamShoulderClipFrameRateTest::RunTest(const FString& Parameters)
{
	TArray<float> Amounts;
	TArray<float> Radii;
	for (const int32 FPS : {30, 60, 120})
	{
		FShoulderCameraFixture Scene;
		// 去掉网格来验证无物理资产的胶囊表面回退；这也避免把参考姿势尺寸写死进测试。
		Scene.Character->GetMesh()->SetSkeletalMesh(nullptr);
		UBoxComponent* Wall = Scene.AddBlocker(FVector(-35, 0, 60), FVector(5, 500, 500));
		Scene.Advance(0.2f, FPS);
		Amounts.Add(Scene.Camera->GetOwnerClipAmount());
		Radii.Add(Scene.Camera->GetOwnerClipRadius());
		TestEqual(TEXT("位于胶囊表面内部时回退距离为零"), Scene.Camera->GetOwnerSurfaceDistance(), 0.0f);
		TestFalse(TEXT("没有材质也不能回退到整个人物隐藏"), Scene.Character->GetMesh()->bOwnerNoSee);
		const float Before = Scene.Camera->GetOwnerClipAmount();
		Scene.Step(0.0f);
		TestEqual(TEXT("零时间更新不会改变过渡进度"), Scene.Camera->GetOwnerClipAmount(), Before);
		Wall->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Scene.Advance(2.0f, FPS);
		TestTrue(TEXT("持续远离后剔除强度稳定归零"), Scene.Camera->GetOwnerClipAmount() < 0.0001f);
	}
	TestTrue(TEXT("30/60/120 FPS 相同时间的强度一致"),
		FMath::IsNearlyEqual(Amounts[0], Amounts[1], 0.0001f) && FMath::IsNearlyEqual(Amounts[1], Amounts[2], 0.0001f));
	TestTrue(TEXT("30/60/120 FPS 相同时间的范围一致"),
		FMath::IsNearlyEqual(Radii[0], Radii[1], 0.001f) && FMath::IsNearlyEqual(Radii[1], Radii[2], 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamShoulderMiniatureObserverTest,
	"DreamSpace.Camera.Shoulder.StableMiniatureObserver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamShoulderMiniatureObserverTest::RunTest(const FString& Parameters)
{
	FShoulderCameraFixture Scene;
	UDreamSceneCapturePresentationComponent* Miniature = Scene.Character->SceneMiniature;
	FMinimalViewInfo POV;
	POV.Location = Scene.Position();
	POV.Rotation = Scene.Controller->GetControlRotation();
	const FTransform BeforeCollision = Miniature->GetObserverTransform(POV);
	TestTrue(TEXT("固定手办观察距离仍是原有 420 cm，并包含新的肩偏移"),
		BeforeCollision.Equals(Scene.Camera->GetIdealCameraTransform(420.0f), 0.01));
	Scene.AddBlocker(FVector(-100, 0, 60), FVector(10, 500, 500));
	Scene.Advance(0.5f);
	POV.Location = Scene.Position();
	TestTrue(TEXT("真实镜头已因碰撞收近"), Scene.Camera->IsCollisionFixApplied());
	TestTrue(TEXT("碰撞收近不改变手办使用的理想观察姿态"), Miniature->GetObserverTransform(POV).Equals(BeforeCollision, 0.01));
	Miniature->bFollowCameraZoom = true;
	Scene.Camera->TargetArmLength = 300.0f;
	Scene.Step();
	TestTrue(TEXT("开启缩放跟随后使用平滑距离，未采用跳变的滚轮目标"),
		Miniature->GetObserverTransform(POV).Equals(
			Scene.Camera->GetIdealCameraTransform(Scene.Camera->GetSmoothedArmLength()), 0.01));
	Miniature->bIgnoreSpringArmCollision = false;
	TestTrue(TEXT("显式关闭忽略碰撞时使用真实视口姿态"),
		Miniature->GetObserverTransform(POV).Equals(FTransform(POV.Rotation, POV.Location), 0.01));

	// 验证普通 SpringArm 回退的世界/旋转空间偏移，覆盖旧零偏移代码看不出的坐标错误。
	AActor* ViewTarget = Scene.World->SpawnActor<AActor>();
	USpringArmComponent* PlainArm = NewObject<USpringArmComponent>(ViewTarget);
	ViewTarget->SetRootComponent(PlainArm);
	ViewTarget->AddInstanceComponent(PlainArm);
	PlainArm->RegisterComponent();
	ViewTarget->SetActorLocationAndRotation(FVector(200, 300, 400), FRotator(0, 90, 30));
	PlainArm->TargetOffset = FVector(10, 20, 30);
	PlainArm->SocketOffset = FVector(0, 40, 15);
	Scene.Controller->SetViewTarget(ViewTarget);
	Miniature->bIgnoreSpringArmCollision = true;
	Miniature->bFollowCameraZoom = false;
	const FVector Expected = PlainArm->GetComponentLocation() + PlainArm->TargetOffset
		+ PlainArm->GetTargetRotation().RotateVector(FVector(-420, 40, 15));
	TestTrue(TEXT("普通摇臂回退正确区分 TargetOffset 和 SocketOffset 坐标空间"),
		Miniature->GetObserverTransform(POV).GetLocation().Equals(Expected, 0.01));
	return true;
}
#endif
