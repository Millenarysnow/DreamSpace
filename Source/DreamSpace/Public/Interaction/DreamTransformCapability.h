#pragma once
#include "DreamInteractionCapability.h"
#include "DreamTransformCapability.generated.h"

/** 同一通用能力覆盖装配体、房间和选择集合的移动、旋转与均匀缩放。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class DREAMSPACE_API UDreamTransformCapability : public UDreamInteractionCapability
{
	GENERATED_BODY()
public:
	UDreamTransformCapability();
	UPROPERTY(EditAnywhere, Category = "变换")
	bool bAllowRotation = true;
	UPROPERTY(EditAnywhere, Category = "变换")
	bool bAllowScaling = false;
	UPROPERTY(EditAnywhere, Category = "变换")
	bool bAllowTranslation = false;
	UPROPERTY(EditAnywhere, Category = "变换")
	FVector DefaultRotationAxis = FVector::UpVector;
	UPROPERTY(EditAnywhere, Category = "变换")
	bool bAllowInputAxis = false;
	UPROPERTY(EditAnywhere, Category = "变换")
	float RotationStepDegrees = 90;
	UPROPERTY(EditAnywhere, Category = "变换")
	float TranslationStep = 0;
	UPROPERTY(EditAnywhere, Category = "变换")
	float ScaleStep = 0;
	UPROPERTY(EditAnywhere, Category = "限制")
	float MaximumRotationPerOperation = 180;
	UPROPERTY(EditAnywhere, Category = "限制")
	float MaximumTranslationPerOperation = 2000;
	/** 尺寸限制是相对定义默认尺寸的比例，重复操作不能突破范围。 */
	UPROPERTY(EditAnywhere, Category = "限制")
	float MinimumScale = 0.1f;
	UPROPERTY(EditAnywhere, Category = "限制")
	float MaximumScale = 4;
	UPROPERTY(EditAnywhere, Category = "坐标系")
	EDreamReferenceFrame ReferenceFrame = EDreamReferenceFrame::Assembly;
	UPROPERTY(EditAnywhere, Category = "坐标系")
	FQuat CustomFrameRotation = FQuat::Identity;
	UPROPERTY(EditAnywhere, Category = "Pivot")
	EDreamPivotMode PivotMode = EDreamPivotMode::SelectionCenter;
	UPROPERTY(EditAnywhere, Category = "Pivot")
	FGuid PivotNodeId;
	UPROPERTY(EditAnywhere, Category = "Pivot")
	FName PivotSocket;
	UPROPERTY(EditAnywhere, Category = "Pivot")
	FVector PivotPoint = FVector::ZeroVector;
	/**
	 * 先消除重复父子选择，再把 Pivot、轴和位置统一到世界参考系进行计算。
	 * 结果写回各目标父坐标系中的局部变换；未选中的后代通过父链自然跟随。
	 * 输入增量相对会话初始状态累计，缩放范围相对定义默认尺寸校验。
	 */
	virtual bool BuildResult(
		const FDreamCapabilityContext& C, FDreamInteractionCommand& Command, FText& Failure) const override;
	virtual void ValidateConfiguration(
		const UInteractiveAssemblyDefinition& Definition, TArray<FString>& Errors) const override;
};
