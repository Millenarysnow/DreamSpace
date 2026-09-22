// DreamSpace 游戏目标。
using UnrealBuildTool;
using System.Collections.Generic;

public class DreamSpaceTarget : TargetRules
{
	public DreamSpaceTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		// 迁移到 UE 5.8 的默认构建设置与包含顺序，消除与引擎共享构建环境的冲突。
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;

		ExtraModuleNames.AddRange( new string[] { "DreamSpace" } );
	}
}
