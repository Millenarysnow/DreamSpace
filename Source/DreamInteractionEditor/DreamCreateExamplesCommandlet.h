#pragma once
#include "Commandlets/Commandlet.h"
#include "DreamCreateExamplesCommandlet.generated.h"
/**
 * 生成新验收资产与关卡；再次运行需显式传入 -ReplaceExamples，避免覆盖手工编辑。
 * 使用 -EnsureSceneCaptureMaterial 时只生成场景缩略图显示材质，不会重建地图或定义资产。
 */
UCLASS()
class UDreamCreateExamplesCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UDreamCreateExamplesCommandlet();
	virtual int32 Main(const FString& Params) override;
};
