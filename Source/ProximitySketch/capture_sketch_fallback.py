# -*- coding: utf-8 -*-
"""在 TEST 中对照 Nanite 与回退网格的栏杆、广告文字。

在专用 UnrealEditor.exe 进程通过 -ExecutePythonScript 运行，可使用
-SketchDetailLabel=FallbackBefore / FallbackAfter 分开保存证据。
本脚本仅改变临时 PIE 世界和进程内 CVar，不保存地图或材质；结束后退出
专用编辑器。需要启用 PythonScriptPlugin、EditorScriptingUtilities。
"""

import importlib.util
from pathlib import Path

import unreal


# 复用现有截图流程：等待 TSR 历史稳定，通过真实玩家相机管理器提交后处理，
# 同时冻结人物动画。不能用不经过玩家相机的 SceneCapture 替代游玩画面。
spec = importlib.util.spec_from_file_location(
    "fallback_capture", Path(__file__).with_name("capture_sketch_details.py"))
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)

rigs = [
    # 近景栏杆位于正常渲染范围内、天空在范围外，可分辨“几何被删除”和“墨线变浅”。
    ("Railing", unreal.Vector(-530, 320, 570), unreal.Vector(-710, -30, 455), 80.0),
    # 外观采用原有广告文字验收相机，强制整栋建筑进入线稿区。
    ("Exterior", capture.room_origin + unreal.Vector(-1450, -3100, 1380),
     capture.room_origin + unreal.Vector(0, -500, 600), 65.0),
]

# 同一几何位置依次比较完整 Nanite 和非 Nanite 回退路径。不要把 r.Nanite=0
# 写进项目配置；此处只用于专用验收进程，复现与反馈截图一致的断杆形态。
capture.VIEWS = [(name + suffix, position, target, fov)
                 for name, position, target, fov in rigs
                 for suffix in ("Nanite", "Fallback")]
capture.CASES = [(index, percentage, mode)
                 for index in range(len(capture.VIEWS))
                 for percentage in (100, 50)
                 for mode in ("Original", "Sketch")]

# 扩大视口并固定与反馈截图相同的画面比例，让前后像素位置可比较。
# 这是引擎内部的编辑器视口命令，不依赖鼠标点击，也不改变项目保存的相机。
unreal.SystemLibrary.execute_console_command(
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(),
    "LevelEditor.ToggleImmersive")
capture.camera.camera_component.set_editor_property("constrain_aspect_ratio", True)
capture.camera.camera_component.set_editor_property("aspect_ratio", 1685.0 / 640.0)

base_configure = capture.configure


def configure():
    """明确重置每组 CVar，确保关闭效果或切换 Nanite 不泄漏至下一组。"""
    index, percentage, mode = capture.CASES[capture.state["index"]]
    name, position, target, _ = capture.VIEWS[index]
    direction = (target - position).normal()
    capture.state["pawn"].set_actor_location(
        position + direction * 210 - unreal.Vector(0, 0, 60), False, False)
    base_configure()
    capture.command("r.Nanite %d" % (0 if name.endswith("Fallback") else 1))
    capture.command("dream.Sketch.Radius %d" % (600 if name.startswith("Railing") else 0))
    capture.command("dream.Sketch.Transition %d" % (150 if name.startswith("Railing") else 0))
    # 原图组完全移除两个后处理，而不只切换主通道的 Debug 输出，直接验证
    # 无线稿时几何是否已缺失。下一组必须先恢复开关，再等待材质和 TSR 稳定。
    capture.command("dream.Sketch.Enabled %d" % (0 if mode == "Original" else 1))
    capture.command("dream.Sketch.Debug 0")
    capture.state["capture"].update(
        nanite=unreal.SystemLibrary.get_console_variable_int_value("r.Nanite"),
        sketch_enabled=(mode != "Original"),
        pawn_position=capture.xyz(capture.state["pawn"].get_actor_location()),
        camera_rotation=capture.xyz(unreal.Vector(
            capture.state["camera"].get_actor_rotation().pitch,
            capture.state["camera"].get_actor_rotation().yaw,
            capture.state["camera"].get_actor_rotation().roll)))


capture.configure = configure
