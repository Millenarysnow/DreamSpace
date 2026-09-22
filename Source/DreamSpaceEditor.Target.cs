// DreamSpace 编辑器目标：旧的 DreamInteractionEditor 模块已随可配置交互框架移除。
using UnrealBuildTool;
using System.Collections.Generic;

public class DreamSpaceEditorTarget : TargetRules
{
	public DreamSpaceEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		// 迁移到 UE 5.8 的默认构建设置与包含顺序，消除与 UnrealEditor 共享构建环境的冲突。
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;

		ExtraModuleNames.AddRange( new string[] { "DreamSpace" } );
	}
}
