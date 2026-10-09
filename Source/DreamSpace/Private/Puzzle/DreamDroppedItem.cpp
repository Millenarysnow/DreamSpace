#include "DreamDroppedItem.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

ADreamDroppedItem::ADreamDroppedItem()
{
	PrimaryActorTick.bCanEverTick = false;
	DropCollision = CreateDefaultSubobject<USphereComponent>(TEXT("DropCollision"));
	SetRootComponent(DropCollision);
	DropCollision->SetMobility(EComponentMobility::Movable);
	DropCollision->InitSphereRadius(5.0f);
	DropCollision->SetCollisionProfileName(TEXT("PhysicsActor"));
	// 掉落物不被手办再次捕获，也不能用看不见的球体挡住捕获拾取或第三人称相机。
	DropCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	DropCollision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	DropCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DropCollision->SetSimulatePhysics(false);

	// 美术网格只负责显示，物理形状统一由球体提供，避免源网格缺少简单碰撞时无法模拟。
	DropMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DropMesh"));
	DropMesh->SetupAttachment(DropCollision);
	DropMesh->SetMobility(EComponentMobility::Movable);
	DropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DropMesh->SetGenerateOverlapEvents(false);
	DropMesh->SetHiddenInSceneCapture(true);
}

bool ADreamDroppedItem::InitializeFromSource(const UStaticMeshComponent* Source, float ScaleMultiplier)
{
	if (!IsValid(Source) || !Source->GetStaticMesh() || !FMath::IsFinite(ScaleMultiplier) || ScaleMultiplier <= 0.0f)
		return false;

	UStaticMesh* MeshAsset = Source->GetStaticMesh();
	// 保留组件的世界缩放，而不是只复制 Actor 缩放：美术蓝图常在网格组件上单独缩放。
	const FVector DropScale = Source->GetComponentScale() * ScaleMultiplier;
	const FBox Bounds = MeshAsset->GetBoundingBox();
	const FVector Extents = Bounds.GetExtent() * DropScale.GetAbs();
	const double Radius = FMath::Max(5.0, Extents.Size());
	if (DropScale.ContainsNaN() || DropScale.GetAbs().GetMin() <= UE_SMALL_NUMBER || !Bounds.IsValid ||
		!FMath::IsFinite(Radius))
		return false;
	DropMesh->SetStaticMesh(MeshAsset);
	DropMesh->SetRelativeScale3D(DropScale);
	for (int32 Index = 0; Index < Source->GetNumMaterials(); ++Index)
		DropMesh->SetMaterial(Index, Source->GetMaterial(Index));

	// 网格原点不一定在几何中心；把可见模型移到球体中心，物理掉落时才不会绕远处的原点翻滚。
	DropMesh->SetRelativeLocation(-Bounds.GetCenter() * DropScale);
	DropCollision->SetSphereRadius(Radius, true);
	// 蓝图子类添加的表现部件同样遵守预览约定；后续拾取碰撞可在 OnDroppedFromMiniature 中启用。
	TInlineComponentArray<UPrimitiveComponent*> Primitives(this);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		Primitive->SetSimulatePhysics(false);
		Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Primitive->SetHiddenInSceneCapture(true);
	}
	return true;
}

bool ADreamDroppedItem::CanActivateDrop(const AActor* SourceActor) const
{
	if (!GetWorld() || !DropMesh->GetStaticMesh())
		return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DreamMiniatureDropPlacement), false, this);
	Params.AddIgnoredActor(SourceActor);
	return !GetWorld()->OverlapBlockingTestByProfile(
		GetActorLocation(), GetActorQuat(), TEXT("PhysicsActor"), FCollisionShape::MakeSphere(GetDropRadius()), Params);
}

bool ADreamDroppedItem::ActivateDrop()
{
	if (!DropMesh->GetStaticMesh())
		return false;
	// 先开启碰撞，再启动刚体；预览阶段完全无碰撞，不会挡住鼠标或挤开玩家。
	DropCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DropCollision->SetSimulatePhysics(true);
	return DropCollision->IsSimulatingPhysics();
}

float ADreamDroppedItem::GetDropRadius() const
{
	return DropCollision->GetScaledSphereRadius();
}
