#include "DreamRotationSupport.h"

#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"

void DreamRotationSupport::GatherStandingCharacters(const AActor* Platform, TArray<ACharacter*>& OutCharacters)
{
	OutCharacters.Reset();
	const USceneComponent* Root = Platform ? Platform->GetRootComponent() : nullptr;
	if (!Root || !Platform->GetWorld())
		return;

	for (TActorIterator<ACharacter> It(Platform->GetWorld()); It; ++It)
	{
		ACharacter* Character = *It;
		const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		// UE5.8 用 MovementBaseObject 表示底座。必须同时检查所属 Actor、附着关系和
		// 地面移动模式，不能仅用空间重叠把侧面经过或已起跳的角色误认为平台乘客。
		const UPrimitiveComponent* Base =
			Movement ? Cast<UPrimitiveComponent>(Movement->GetMovementBaseObject()) : nullptr;
		if (Movement && Movement->IsMovingOnGround() && Base && Base->GetOwner() == Platform &&
			(Base == Root || Base->IsAttachedTo(Root)))
		{
			OutCharacters.Add(Character);
		}
	}
}

FTransform DreamRotationSupport::GetStandingCharacterTransform(
	const AActor* Platform, const ACharacter* Character, const FTransform& TargetPlatformTransform)
{
	const FTransform CurrentTransform = Platform->GetActorTransform();
	const FQuat DeltaRotation = TargetPlatformTransform.GetRotation() * CurrentTransform.GetRotation().Inverse();
	// 旋转机关不改变缩放，直接使用刚体增量即可同时表达自转和偏心枢轴公转。
	// 必须搬运整个胶囊中心；只移动脚底点会在倾斜时让胶囊穿入表面或悬空。
	const FVector NewLocation =
		TargetPlatformTransform.GetLocation() +
		DeltaRotation.RotateVector(Character->GetActorLocation() - CurrentTransform.GetLocation());
	return FTransform(DeltaRotation * Character->GetActorQuat(), NewLocation, Character->GetActorScale3D());
}

void DreamRotationSupport::ApplyActorTransform(
	AActor* Platform, const FTransform& TargetPlatformTransform, TConstArrayView<ACharacter*> StandingCharacters)
{
	if (!Platform)
		return;

	const FTransform PreviousTransform = Platform->GetActorTransform();
	Platform->SetActorLocationAndRotation(TargetPlatformTransform.GetLocation(), TargetPlatformTransform.GetRotation(),
		false, nullptr, ETeleportType::None);
	const FTransform ActualTransform = Platform->GetActorTransform();
	const FQuat DeltaRotation = ActualTransform.GetRotation() * PreviousTransform.GetRotation().Inverse();

	for (ACharacter* Character : StandingCharacters)
	{
		if (!IsValid(Character) || !Character->GetCharacterMovement())
			continue;
		UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		const FVector NewLocation =
			ActualTransform.GetLocation() +
			DeltaRotation.RotateVector(Character->GetActorLocation() - PreviousTransform.GetLocation());
		const FQuat NewRotation = DeltaRotation * Character->GetActorQuat();

		// 从角色当前世界重力累积旋转，不预设起始方向为 -Z。
		// 离台后的方向自然保留，进入下一座平台也不会被重置成平台的绝对朝向。
		Movement->SetGravityDirection(DeltaRotation.RotateVector(Movement->GetGravityDirection()));
		Movement->Velocity = DeltaRotation.RotateVector(Movement->Velocity);
		Character->SetActorLocationAndRotation(NewLocation, NewRotation, false, nullptr, ETeleportType::None);
		if (AController* Controller = Character->GetController())
		{
			// 完整四元数保留转轴信息，180 度翻转时也不会因仅比较重力向量而丢失相机旋转。
			Controller->SetControlRotation((DeltaRotation * Controller->GetControlRotation().Quaternion()).Rotator());
		}

		// 当前函数已完成底座搬运。刷新缓存可避免 CharacterMovement 在自己的 Tick
		// 再搬运同一段运动、再旋转一次相机，并让下次落地查询使用新重力方向。
		Movement->SaveBaseLocation();
		Movement->bForceNextFloorCheck = true;
	}
}
