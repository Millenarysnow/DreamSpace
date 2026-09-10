#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "DreamInteractionTypes.h"
#include "DreamInteractionSaveGame.generated.h"

/**
 * 交互状态存档。
 * 保存的是稳定 ID 和逻辑状态，不保存 Mesh 名称、组件顺序或当前世界坐标缓存。
 */
UCLASS()
class DREAMSPACE_API UDreamInteractionSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category = "存档")
	int32 SaveVersion = 1;

	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category = "存档")
	FDateTime SavedAtUtc;

	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category = "存档")
	TArray<FDreamAssemblyState> AssemblyStates;

	void CaptureAssemblyState(const FDreamAssemblyState& State);
	const FDreamAssemblyState* FindAssemblyState(const FGuid& AssemblyId) const;
};

