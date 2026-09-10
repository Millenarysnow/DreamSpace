using UnrealBuildTool;
/** 内容生成与资产工具只进入编辑器，打包游戏不依赖 UnrealEd。 */
public class DreamInteractionEditor : ModuleRules
{
 public DreamInteractionEditor(ReadOnlyTargetRules Target) : base(Target)
 {
  PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
  PrivateDependencyModuleNames.AddRange(new string[]{"Core","CoreUObject","Engine","InputCore","UnrealEd","AssetRegistry","DreamSpace"});
 }
}
