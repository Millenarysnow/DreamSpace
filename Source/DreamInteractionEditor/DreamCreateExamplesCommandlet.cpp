#include "DreamCreateExamplesCommandlet.h"
#include "DreamExampleDefinitions.h"
#include "InteractiveAssemblyDefinition.h"
#include "InteractiveAssemblyActor.h"
#include "MainGameMode.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionOneMinus.h"

namespace
{
/**
 * 创建场景缩略图显示材质。
 *
 * SceneCapture 的 SCS_SceneColorHDR 输出约定为：RGB 保存场景颜色，A 保存反向不透明度。
 * 因此材质必须把 RGB 作为已完成光照的 Emissive 使用，并将 OneMinus(A) 接到 Opacity。
 * 这里生成独立资产，运行时只创建 MID 和设置 RenderTarget，不依赖编辑器材质编译能力。
 */
bool SaveSceneCaptureDisplayMaterial(const FString& BasePath)
{
	const FString PackageName = BasePath + TEXT("Materials/M_SceneCaptureDisplay");
	auto* Package = CreatePackage(*PackageName);
	Package->MarkAsFullyLoaded();
	auto* Material = NewObject<UMaterial>(
		Package, TEXT("M_SceneCaptureDisplay"), RF_Public | RF_Standalone);
	if (!Material)
		return false;

	Material->MaterialDomain = MD_Surface;
	Material->BlendMode = BLEND_Translucent;
	Material->TwoSided = true;
	Material->SetShadingModel(MSM_Unlit);

	auto* SceneTexture = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
	SceneTexture->ParameterName = TEXT("SceneCaptureTexture");
	SceneTexture->ExpressionGUID = FGuid::NewGuid();
	SceneTexture->SamplerType = SAMPLERTYPE_LinearColor;
	SceneTexture->Texture = LoadObject<UTexture2D>(
		nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
	SceneTexture->MaterialExpressionEditorX = -320;
	SceneTexture->MaterialExpressionEditorY = 0;
	Material->GetExpressionCollection().AddExpression(SceneTexture);

	auto* InvertOpacity = NewObject<UMaterialExpressionOneMinus>(Material);
	InvertOpacity->MaterialExpressionEditorX = -80;
	InvertOpacity->MaterialExpressionEditorY = 180;
	Material->GetExpressionCollection().AddExpression(InvertOpacity);

	// TextureSample 输出 0 是 RGB，输出 4 是 A。A 为反向不透明度，所以先经过 OneMinus。
	SceneTexture->ConnectExpression(&Material->GetEditorOnlyData()->EmissiveColor, 0);
	SceneTexture->ConnectExpression(&InvertOpacity->Input, 4);
	InvertOpacity->ConnectExpression(&Material->GetEditorOnlyData()->Opacity, 0);

	Material->PostEditChange();
	FAssetRegistryModule::AssetCreated(Material);
	Package->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const FString Filename =
		FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
	return UPackage::SavePackage(Package, Material, *Filename, SaveArgs);
}
}

UDreamCreateExamplesCommandlet::UDreamCreateExamplesCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}
int32 UDreamCreateExamplesCommandlet::Main(const FString& Params)
{
	const FString Base = TEXT("/Game/DreamInteraction/");
	// 单独生成缩略图材质，不触碰现有定义资产和地图。
	// 这样表现层迭代不需要使用 -ReplaceExamples 覆盖策划已经调整过的示例内容。
	if (FParse::Param(*Params, TEXT("EnsureSceneCaptureMaterial")))
	{
		const bool bSaved = SaveSceneCaptureDisplayMaterial(Base);
		UE_LOG(LogTemp, Display, TEXT("场景缩略图透明材质生成：%s"), bSaved ? TEXT("成功") : TEXT("失败"));
		return bSaved ? 0 : 1;
	}
	if (FPackageName::DoesPackageExist(Base + TEXT("Maps/InteractionDemo")) &&
		!FParse::Param(*Params, TEXT("ReplaceExamples")))
	{
		UE_LOG(LogTemp, Error, TEXT("验收关卡已存在；需要重新生成时请显式传入 -ReplaceExamples。"));
		return 1;
	}
	// 生成简单可染色材质，让旋转前后的位置关系在原型场景中也能辨认。
	const FString MaterialPackageName = Base + TEXT("Materials/M_Prototype");
	auto* MaterialPackage = CreatePackage(*MaterialPackageName);
	MaterialPackage->MarkAsFullyLoaded();
	auto* Material = NewObject<UMaterial>(MaterialPackage, TEXT("M_Prototype"), RF_Public | RF_Standalone);
	auto* Tint = NewObject<UMaterialExpressionVectorParameter>(Material);
	Tint->ParameterName = TEXT("Tint");
	Tint->DefaultValue = FLinearColor::White;
	Material->GetExpressionCollection().AddExpression(Tint);
	Material->GetEditorOnlyData()->BaseColor.Expression = Tint;
	auto* Roughness = NewObject<UMaterialExpressionConstant>(Material);
	Roughness->R = 0.65;
	Material->GetExpressionCollection().AddExpression(Roughness);
	Material->GetEditorOnlyData()->Roughness.Expression = Roughness;
	Material->PostEditChange();
	FAssetRegistryModule::AssetCreated(Material);
	FSavePackageArgs MaterialArgs;
	MaterialArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const FString MaterialFile =
		FPackageName::LongPackageNameToFilename(MaterialPackageName, FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(MaterialFile), true);
	if (!UPackage::SavePackage(MaterialPackage, Material, *MaterialFile, MaterialArgs))
		return 1;
	if (!SaveSceneCaptureDisplayMaterial(Base))
		return 1;
	auto SaveAsset = [&](FString Name, auto Populate) -> UInteractiveAssemblyDefinition*
	{
		const FString PackageName = Base + TEXT("Definitions/") + Name;
		auto* Package = CreatePackage(*PackageName);
		// 该命令已获得 ReplaceExamples 参数，整个包由生成器重建，不从旧包合并遗留子对象。
		Package->MarkAsFullyLoaded();
		auto* D = NewObject<UInteractiveAssemblyDefinition>(Package, *Name, RF_Public | RF_Standalone);
		Populate(*D);
		TArray<FString> Errors;
		if (!D->ValidateDefinition(Errors))
		{
			for (const auto& Error : Errors)
				UE_LOG(LogTemp, Error, TEXT("%s"), *Error);
			return nullptr;
		}
		FAssetRegistryModule::AssetCreated(D);
		Package->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		const FString File =
			FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
		if (!UPackage::SavePackage(Package, D, *File, Args))
		{
			UE_LOG(LogTemp, Error, TEXT("无法保存示例定义：%s"), *File);
			return nullptr;
		}
		return D;
	};
	auto* Cube = SaveAsset(TEXT("DA_LayeredCube"), DreamExamples::MakeCube);
	auto* Key = SaveAsset(TEXT("DA_GiantKey"), DreamExamples::MakeKey);
	auto* Room = SaveAsset(TEXT("DA_GravityRoom"), DreamExamples::MakeGravityRoom);
	auto* Door = SaveAsset(TEXT("DA_KeyDoor"), DreamExamples::MakeDoor);
	if (!Cube || !Key || !Room || !Door)
		return 1;
	const FString MapName = Base + TEXT("Maps/InteractionDemo");
	auto* Package = CreatePackage(*MapName);
	Package->MarkAsFullyLoaded();
	auto* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("InteractionDemo"), Package);
	if (!World)
		return 1;
	World->GetWorldSettings()->DefaultGameMode = AMainGameMode::StaticClass();
	auto* Floor = World->SpawnActor<AStaticMeshActor>();
	Floor->SetActorLabel(TEXT("Demo_Ground"));
	Floor->GetStaticMeshComponent()->SetStaticMesh(
		LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Floor->GetStaticMeshComponent()->SetWorldScale3D(FVector(60, 60, 0.2));
	Floor->SetActorLocation(FVector(0, 0, -10));
	auto Spawn = [&](UInteractiveAssemblyDefinition* D, FVector Position, int32 Id)
	{
		auto* A = World->SpawnActorDeferred<AInteractiveAssemblyActor>(
			AInteractiveAssemblyActor::StaticClass(), FTransform(Position));
		A->Definition = D;
		A->AssemblyId = FGuid(0xD5A, 1, 0, Id);
		A->FinishSpawning(FTransform(Position));
		A->SetActorLabel(D->GetName());
	};
	Spawn(Cube, FVector(-700, 0, 60), 1);
	Spawn(Key, FVector(0, -500, 220), 2);
	Spawn(Door, FVector(600, -500, 160), 3);
	Spawn(Room, FVector(800, 800, 550), 4);
	auto* Sun = World->SpawnActor<ADirectionalLight>();
	Sun->SetActorRotation(FRotator(-50, -35, 0));
	auto* Light = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetIntensity(4);
	auto* Sky = World->SpawnActor<ASkyLight>();
	Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Sky->GetLightComponent()->SetIntensity(0.8);
	auto* Start = World->SpawnActor<APlayerStart>();
	Start->SetActorLocation(FVector(650, 800, 450));
	Start->SetActorRotation(FRotator(0, 180, 0));
	auto Label = [&](FVector Location, const FString& Text)
	{
		auto* A = World->SpawnActor<ATextRenderActor>();
		A->SetActorLocation(Location);
		A->SetActorRotation(FRotator(0, -90, 0));
		A->GetTextRender()->SetText(FText::FromString(Text));
		A->GetTextRender()->SetWorldSize(45);
		A->GetTextRender()->SetTextRenderColor(FColor(120, 200, 255));
	};
	Label(FVector(-900, -250, 60), TEXT("LAYERED CUBE"));
	Label(FVector(-180, -740, 60), TEXT("SHRINK & PICK UP"));
	Label(FVector(350, -740, 60), TEXT("KEY DOOR"));
	Label(FVector(400, 350, 60), TEXT("ROTATING GRAVITY ROOM"));
	FAssetRegistryModule::AssetCreated(World);
	World->MarkPackageDirty();
	const FString MapFile = FPackageName::LongPackageNameToFilename(MapName, FPackageName::GetMapPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(MapFile), true);
	const bool Saved = FEditorFileUtils::SaveMap(World, MapFile);
	World->DestroyWorld(false);
	UE_LOG(LogTemp, Display, TEXT("DreamSpace 验收内容生成：%s"), Saved ? TEXT("成功") : TEXT("失败"));
	return Saved ? 0 : 1;
}
