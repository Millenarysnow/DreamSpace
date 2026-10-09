#include "DreamMiniatureExtractableComponent.h"

#include "DreamDroppedItem.h"
#include "DreamSpace.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"

UDreamMiniatureExtractableComponent::UDreamMiniatureExtractableComponent()
{
	// 仅持有预览期间启用 Tick，以处理蓝图直接调用时操作者或预览被销毁的情况。
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bAutoActivate = true;
	bEditableWhenInherited = true;
	DropActorClass = ADreamDroppedItem::StaticClass();
}

void UDreamMiniatureExtractableComponent::BeginPlay()
{
	Super::BeginPlay();
	// AutoActivate 会开启 Tick；静置物体不需要逐帧扫描，只在实际持有预览时检查生命周期。
	SetComponentTickEnabled(bExtracting);
}

void UDreamMiniatureExtractableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Actor 从外部销毁或关卡卸载时，不能把仍处于预览状态的掉落物留在场景中。
	EndExtract(false);
	Super::EndPlay(EndPlayReason);
}

void UDreamMiniatureExtractableComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// 动态移除组件时不一定经过 EndPlay，也必须恢复源模型并删除尚未提交的预览。
	EndExtract(false);
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UDreamMiniatureExtractableComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bExtracting && (!ExtractInteractor.IsValid() || !IsValid(PreviewItem) || !IsActive()))
		EndExtract(false);
}

void UDreamMiniatureExtractableComponent::Deactivate()
{
	EndExtract(false);
	Super::Deactivate();
}

void UDreamMiniatureExtractableComponent::OnInteracted_Implementation(AActor* Interactor)
{
	// 旧交互接口只有操作者，没有显示面交点。世界 E 键不能据此凭空生成掉落物；
	// 控制器的手办拾取路径会调用带完整参数的 BeginExtract。
	UE_LOG(LogDreamSpace, Verbose, TEXT("%s 只能在手办模式按住左键拖出。"), *GetNameSafe(GetOwner()));
}

UStaticMeshComponent* UDreamMiniatureExtractableComponent::ResolveSourceMesh(UPrimitiveComponent* HitComponent) const
{
	AActor* Owner = GetOwner();
	if (!Owner)
		return nullptr;
	// 明确填写的组件引用失效时直接拒绝，避免配置打错后取出另一个模型。
	const bool bHasExplicitMesh =
		!SourceMeshComponent.ComponentProperty.IsNone() || !SourceMeshComponent.PathToComponent.IsEmpty() ||
		SourceMeshComponent.OverrideComponent.IsValid() || SourceMeshComponent.OtherActor.IsValid();
	if (bHasExplicitMesh)
	{
		UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(SourceMeshComponent.GetComponent(Owner));
		return Mesh && Mesh->GetOwner() == Owner ? Mesh : nullptr;
	}
	if (UStaticMeshComponent* HitMesh = Cast<UStaticMeshComponent>(HitComponent))
		if (HitMesh->GetOwner() == Owner)
			return HitMesh;
	return Owner->FindComponentByClass<UStaticMeshComponent>();
}

bool UDreamMiniatureExtractableComponent::BeginExtract(AActor* Interactor, UPrimitiveComponent* HitComponent,
	const FVector& InitialDisplayPoint, const FVector& DisplayFrontNormal)
{
	if (bExtracting || bExtracted)
		return false;
	AActor* Owner = GetOwner();
	UStaticMeshComponent* Source = ResolveSourceMesh(HitComponent);
	if (!IsActive() || !IsValid(Interactor) || !IsValid(Owner) || !GetWorld() || Owner == Interactor ||
		Interactor->GetWorld() != GetWorld() || Owner->IsHidden() || !IsValid(Source) || !Source->GetStaticMesh() ||
		Cast<UInstancedStaticMeshComponent>(Source) || Source->IsSimulatingPhysics() || !Source->IsVisible() ||
		!DropActorClass || InitialDisplayPoint.ContainsNaN() || DisplayFrontNormal.ContainsNaN() ||
		DisplayFrontNormal.IsNearlyZero() || !FMath::IsFinite(ExitMarginUV) || !FMath::IsFinite(SurfaceClearance))
	{
		UE_LOG(LogDreamSpace, Warning, TEXT("手办取出未开始：请检查 %s 的静态网格、掉落物类、组件状态和点击位置。"),
			*GetNameSafe(Owner));
		return false;
	}

	// 先确保预览 Actor 与网格复制成功，再改变房间模型；创建失败时房间状态完全不变。
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamDroppedItem* Item = GetWorld()->SpawnActor<ADreamDroppedItem>(
		DropActorClass, FTransform(Source->GetComponentQuat(), InitialDisplayPoint), Spawn);
	if (!Item)
		return false;
	if (!Item->InitializeFromSource(Source, DropScaleMultiplier))
	{
		Item->Destroy();
		return false;
	}

	PreviewItem = Item;
	ExtractInteractor = Interactor;
	bSourceWasHidden = Owner->IsHidden();
	bSourceHadCollision = Owner->GetActorEnableCollision();
	bExtracting = true;
	bOutsideDisplay = false;
	// 隐藏整个源 Actor，RT 会在下一帧同步失去这个模型；同时关闭源碰撞，
	// 避免玩家在拖动期间撞到一个看不见的房间物体。
	Owner->SetActorHiddenInGame(true);
	Owner->SetActorEnableCollision(false);
	UpdateExtract(InitialDisplayPoint, FVector2D(0.5, 0.5), DisplayFrontNormal);
	SetComponentTickEnabled(bExtracting);
	return bExtracting;
}

void UDreamMiniatureExtractableComponent::UpdateExtract(
	const FVector& DisplayPoint, const FVector2D& UnclampedUV, const FVector& DisplayFrontNormal)
{
	if (!bExtracting)
		return;
	if (!IsValid(PreviewItem) || !ExtractInteractor.IsValid() || !IsActive() || DisplayPoint.ContainsNaN() ||
		UnclampedUV.ContainsNaN() || DisplayFrontNormal.ContainsNaN() || DisplayFrontNormal.IsNearlyZero() ||
		!FMath::IsFinite(ExitMarginUV) || !FMath::IsFinite(SurfaceClearance))
	{
		EndExtract(false);
		return;
	}
	const float Margin = FMath::Max(0.0f, ExitMarginUV);
	bOutsideDisplay = UnclampedUV.X < -Margin || UnclampedUV.X > 1.0 + Margin || UnclampedUV.Y < -Margin ||
					  UnclampedUV.Y > 1.0 + Margin;
	// 预览和最终掉落都放在显示面朝向玩家的一侧，球体不与显示面或其外壳初始重叠。
	const FVector Front = DisplayFrontNormal.GetSafeNormal();
	const FVector DropLocation =
		DisplayPoint + Front * (PreviewItem->GetDropRadius() + FMath::Max(0.0f, SurfaceClearance));
	if (DropLocation.ContainsNaN())
	{
		EndExtract(false);
		return;
	}
	PreviewItem->SetActorLocation(DropLocation, false, nullptr, ETeleportType::TeleportPhysics);
}

bool UDreamMiniatureExtractableComponent::EndExtract(bool bTryDrop)
{
	if (!bExtracting)
		return false;
	AActor* Owner = GetOwner();
	ADreamDroppedItem* Item = PreviewItem.Get();
	const bool bCommit = bTryDrop && bOutsideDisplay && IsValid(Owner) && IsValid(Item) &&
						 ExtractInteractor.IsValid() && IsActive() && Item->CanActivateDrop(Owner);
	// 在 Destroy 触发组件 EndPlay 前先清空会话，防止递归取消把已经成功的掉落物销毁。
	bExtracting = false;
	bOutsideDisplay = false;
	PreviewItem = nullptr;
	ExtractInteractor.Reset();
	SetComponentTickEnabled(false);
	if (bCommit && Item->ActivateDrop())
	{
		bExtracted = true;
		UE_LOG(LogDreamSpace, Log, TEXT("已从手办取出 %s，生成掉落物 %s。"), *GetNameSafe(Owner), *GetNameSafe(Item));
		// 必须先记录成功，再销毁源 Actor；它的 EndPlay/组件销毁不会把掉落物当成预览删除。
		// 若引擎拒绝销毁，源仍保持隐藏、无碰撞且已取出，不会出现重复生成。
		if (bDestroySourceActor)
			Owner->Destroy();
		Item->OnDroppedFromMiniature();
		return true;
	}
	if (IsValid(Owner))
	{
		Owner->SetActorHiddenInGame(bSourceWasHidden);
		Owner->SetActorEnableCollision(bSourceHadCollision);
	}
	if (IsValid(Item))
		Item->Destroy();
	return false;
}
