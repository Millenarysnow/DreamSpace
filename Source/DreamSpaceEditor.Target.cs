// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;
using System.Collections.Generic;

public class DreamSpaceEditorTarget : TargetRules
{
	public DreamSpaceEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		// 项目使用安装版引擎；显式允许本项目目标覆盖编辑器共享环境的告警设置。
		bOverrideBuildEnvironment = true;

		ExtraModuleNames.AddRange( new string[] { "DreamSpace" } );
	}
}
