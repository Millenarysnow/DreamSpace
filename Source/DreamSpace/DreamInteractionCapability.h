#pragma once

#include "CoreMinimal.h"
#include "DreamInteractionTypes.h"
#include "DreamInteractionCapability.generated.h"

class AInteractiveAssemblyActor;

/**
 * 所有交互能力的 C++ 基类。
 * 能力对象只负责规则和命令生成，真正修改逻辑状态必须经过世界子系统的事务入口。
 */
UCLASS(Abstract, BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced)
class DREAMSPACE_API UDreamInteractionCapability : public UObject
{
	GENERATED_BODY()

public:
	/** 能力标签用于 UI、任务和目标解析器筛选。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "能力")
	FGameplayTag CapabilityTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "能力")
	TArray<EDreamInteractionMode> AllowedModes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "能力")
	bool bEnabledByDefault = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "能力")
	bool bInheritToChildren = true;

	/** 阶段 1 的统一生命周期入口。 */
	virtual bool CanStart(const AInteractiveAssemblyActor& Target,
		const FDreamInteractionIntent& Intent,
		EDreamInteractionMode Mode,
		FText& OutFailure) const;

	virtual void Begin(AInteractiveAssemblyActor& Target, const FDreamInteractionIntent& Intent);
	virtual bool Preview(AInteractiveAssemblyActor& Target,
		const FDreamInteractionIntent& Intent,
		FText& OutFailure) const;
	virtual bool Validate(const AInteractiveAssemblyActor& Target,
		const FDreamInteractionCommand& Command,
		FText& OutFailure) const;
	virtual bool BuildCommand(AInteractiveAssemblyActor& Target,
		const FDreamInteractionIntent& Intent,
		FDreamInteractionCommand& OutCommand,
		FText& OutFailure) const;
	virtual void Cancel(AInteractiveAssemblyActor& Target);

	bool SupportsMode(EDreamInteractionMode Mode) const;
};

