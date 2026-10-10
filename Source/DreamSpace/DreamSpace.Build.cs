using UnrealBuildTool;
using System.IO;

public class DreamSpace : ModuleRules
{
 public DreamSpace(ReadOnlyTargetRules Target) : base(Target)
 {
  PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
  // 场景缩略图使用引擎静态网格和动态材质直接显示 RenderTarget，不引入玩法依赖。
  PublicDependencyModuleNames.AddRange(new string[]{"Core","CoreUObject","Engine","InputCore","GameplayTags","EnhancedInput"});
  // 开始界面用原生 Slate 绘制透明 Logo 和淡出遮罩，资源仍由关卡的 UObject 引用管理。
  // Slate 只属于表现层的私有实现，不让解谜组件依赖界面框架。
  PrivateDependencyModuleNames.AddRange(new string[]{"Slate","SlateCore"});
  // 运行时模块按职责划分公开接口；旧的 Interaction/Examples 目录已随可配置交互框架移除。
  // Puzzle 目录承载新的组件式交互框架（枢轴点、可转动组件等）。
  foreach (string Area in new string[]{"Gameplay","Presentation","Puzzle"})
   PublicIncludePaths.Add(Path.Combine(ModuleDirectory,"Public",Area));
  // 模块根目录也加入包含路径，使任意子目录都能直接包含 DreamSpace.h（模块日志分类等）。
  PublicIncludePaths.Add(ModuleDirectory);
 }
}
