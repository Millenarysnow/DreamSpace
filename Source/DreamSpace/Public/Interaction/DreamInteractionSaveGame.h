#pragma once
#include "GameFramework/SaveGame.h"
#include "DreamInteractionTypes.h"
#include "DreamInteractionSaveGame.generated.h"
/** 版本化的完整世界交互快照；不包含 Mesh、组件名称或数组索引。 */
UCLASS()
class DREAMSPACE_API UDreamInteractionSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	int32 SaveVersion = 1;
	UPROPERTY(SaveGame)
	FString MapPackage;
	UPROPERTY(SaveGame)
	FDateTime SavedAtUtc;
	UPROPERTY(SaveGame)
	TArray<FDreamAssemblyState> AssemblyStates;
	UPROPERTY(SaveGame)
	TArray<FDreamOccupantState> OccupantStates;
};
