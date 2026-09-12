using UnrealBuildTool;
using System.IO;

public class DreamSpace : ModuleRules
{
 public DreamSpace(ReadOnlyTargetRules Target) : base(Target)
 {
  PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
  // 场景缩略图表现使用 UWidgetComponent 承载 RenderTarget，因此需要 UMG。
  // 该依赖只属于表现层，不会改变交互运行时的状态和事务边界。
  PublicDependencyModuleNames.AddRange(new string[]{"Core","CoreUObject","Engine","InputCore","GameplayTags","EnhancedInput","UMG","Slate","SlateCore"});
  // 当前保留一个运行时模块，但按职责划分公开接口；交互层不包含游戏/表现层头文件。
  foreach (string Area in new string[]{"Interaction","Gameplay","Presentation","Examples"})
   PublicIncludePaths.Add(Path.Combine(ModuleDirectory,"Public",Area));
 }
}
