using UnrealBuildTool;
using System.IO;

public class DreamSpace : ModuleRules
{
 public DreamSpace(ReadOnlyTargetRules Target) : base(Target)
 {
  PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
  PublicDependencyModuleNames.AddRange(new string[]{"Core","CoreUObject","Engine","InputCore","GameplayTags","EnhancedInput"});
  // 当前保留一个运行时模块，但按职责划分公开接口；交互层不包含游戏/表现层头文件。
  foreach (string Area in new string[]{"Interaction","Gameplay","Presentation","Examples"})
   PublicIncludePaths.Add(Path.Combine(ModuleDirectory,"Public",Area));
 }
}
