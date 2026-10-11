# -*- coding: utf-8 -*-
"""在 TEST 的真实 PIE 视口中检查细栏杆和文字，截图只写入 Saved。

通过独立 UnrealEditor.exe 的 -ExecutePythonScript 运行，并启用
PythonScriptPlugin、EditorScriptingUtilities。可传入 -SketchDetailLabel=Before
或 After 来保留修复前后的对照；脚本结束后退出该独立编辑器。
所有相机、角色位置和材质参数调整只发生在临时编辑器 / PIE 世界，不保存资源。
"""

import json
import re
import time
import traceback
from pathlib import Path

import unreal


label_match = re.search(r"-SketchDetailLabel=(\w+)", unreal.SystemLibrary.get_command_line())
LABEL = label_match.group(1) if label_match else "After"
OUTPUT = Path(unreal.Paths.project_saved_dir()).resolve() / "ProximitySketch/DetailFix" / LABEL
OUTPUT.mkdir(parents=True, exist_ok=True)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level("/Game/0_/Maps/TEST"):
    raise RuntimeError("无法加载 TEST")
editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
room = next(actor for actor in editor.get_all_level_actors() if actor.get_name() == "Rom_C_1")
room_origin = room.get_actor_location()
# 临时相机随 PIE 世界复制。运行时通过 Tag 找到副本，再切换玩家的 ViewTarget，
# 因而截图仍经过真实 DreamProximitySketchCameraManager，而不是独立 SceneCapture。
camera = editor.spawn_actor_from_class(unreal.CameraActor, room_origin)
camera.tags = ["DreamSketchDetailCamera"]
camera.camera_component.set_editor_property("constrain_aspect_ratio", False)

VIEWS = [
    ("Player", None, None, 80.0),
    ("Exterior", room_origin + unreal.Vector(-1450, -3100, 1380),
     room_origin + unreal.Vector(0, -500, 600), 65.0),
]
# 同一视角比较原图、主线稿、关闭末端平滑和重复帧，再用半分辨率渲染检查 TSR。
# 外观视角将正常半径临时设为 0，保证整栋建筑处于线稿区，避免范围变化掩盖缺线。
CASES = [(view, percentage, mode) for view in range(len(VIEWS))
         for percentage in (100, 50)
         for mode in ("Original", "Sketch", "NoAA", "Repeat", "Mask")]
started = time.monotonic()
state = {"phase": "warmup", "at": started, "index": 0, "done": False,
         "report": {"label": LABEL, "map": "/Game/0_/Maps/TEST", "captures": []}}
unreal.EditorPythonScripting.set_keep_python_script_alive(True)


def world():
    """每次获取实际 PIE 世界，避免持有已结束世界或错误的编辑器世界。"""
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def command(value):
    """调试 CVar 只在专用验收进程中使用，正式默认配置保持在项目资源中。"""
    unreal.SystemLibrary.execute_console_command(world(), value)


def phase(value):
    """通过 Slate 帧回调等待渲染线程和 TSR 历史，不阻塞编辑器主线程。"""
    state["phase"] = value
    state["at"] = time.monotonic()


def xyz(value):
    """FVector 显式转为普通数字，便于跨进程比较相机和玩家位置。"""
    return [value.x, value.y, value.z]


def finish(error=None):
    """成功或异常均输出报告并退出专用进程，绝不保存验收时的地图和 MID。"""
    if state["done"]:
        return
    state["done"] = True
    state["report"]["error"] = error
    (OUTPUT / "capture_report.json").write_text(
        json.dumps(state["report"], ensure_ascii=False, indent=2), encoding="utf-8")
    if level.is_in_play_in_editor():
        level.editor_request_end_play()
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    (unreal.log_error if error else unreal.log)("SKETCH_DETAIL_CAPTURE " + str(error or OUTPUT))
    unreal.SystemLibrary.quit_editor()


def setup_runtime():
    """读取真实游戏 MID；冻结角色的运动和动画，使不同模式的几何可以比较。"""
    controller = unreal.GameplayStatics.get_player_controller(world(), 0)
    pawn = unreal.GameplayStatics.get_player_pawn(world(), 0)
    manager = controller.player_camera_manager
    instance = manager.get_sketch_material_instance()
    if not instance:
        raise RuntimeError("实际玩家相机没有线稿 MID")
    # 原生平滑实例是私有 UPROPERTY，Python 无权读属性；从实际已存活的 MID
    # 中按 Outer 和父材质筛选，只读取本相机实例，不需要扩大生产代码的公开接口。
    smoothing = next(material for material in unreal.ObjectIterator(unreal.MaterialInstanceDynamic)
                     if material.get_outer() == manager
                     and material.get_base_material().get_name() == "M_ProximitySketchAA")
    state.update(controller=controller, pawn=pawn, manager=manager, instance=instance,
                 smoothing=smoothing)
    state["camera"] = next(actor for actor in unreal.GameplayStatics.get_all_actors_of_class(world(), unreal.CameraActor)
                           if actor.actor_has_tag("DreamSketchDetailCamera"))
    state["default_position"] = manager.get_camera_location()
    state["default_rotation"] = manager.get_camera_rotation()
    state["default_fov"] = manager.get_fov_angle()
    state["smoothing_strength"] = state["smoothing"].get_scalar_parameter_value("SmoothingStrength")
    state["report"].update(pawn=pawn.get_class().get_path_name(), pawn_position=xyz(pawn.get_actor_location()),
                           camera_manager=manager.get_class().get_path_name(),
                           aa_method=unreal.SystemLibrary.get_console_variable_int_value("r.AntiAliasingMethod"))
    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    if movement:
        movement.stop_movement_immediately()
        movement.set_component_tick_enabled(False)
    for mesh in pawn.get_components_by_class(unreal.SkeletalMeshComponent):
        mesh.set_component_tick_enabled(False)
    # PIE 本身沿用编辑器视口的分辨率策略，明确启用手动百分比以实际覆盖 TSR 输入尺寸。
    command("r.DynamicRes.OperationMode 0")
    command("r.ScreenPercentage.Default.Desktop.Mode 0")
    command("r.Editor.Viewport.OverridePIEScreenPercentage 0")
    command("dream.Sketch.Enabled 1")


def configure():
    """每组重新设置所有覆盖项，避免原图模式、半径或关闭平滑泄漏到下一组。"""
    view_index, percentage, mode = CASES[state["index"]]
    name, position, target, fov = VIEWS[view_index]
    active_camera = state["camera"]
    if position is None:
        position, rotation, fov = state["default_position"], state["default_rotation"], state["default_fov"]
    else:
        rotation = unreal.MathLibrary.find_look_at_rotation(position, target)
    active_camera.set_actor_location_and_rotation(position, rotation, False, False)
    active_camera.camera_component.set_field_of_view(fov)
    state["controller"].set_view_target_with_blend(active_camera, 0.0)
    command("r.ScreenPercentage %s" % percentage)
    command("dream.Sketch.Radius %s" % (-1 if view_index == 0 else 0))
    command("dream.Sketch.Transition %s" % (-1 if view_index == 0 else 0))
    command("dream.Sketch.Debug %s" % (3 if mode == "Original" else (2 if mode == "Mask" else 0)))
    state["smoothing"].set_scalar_parameter_value(
        "SmoothingStrength", 0.0 if mode == "NoAA" else state["smoothing_strength"])
    state["path"] = OUTPUT / ("%s_%s_%s.png" % (name, percentage, mode))
    state["capture"] = {"file": state["path"].name, "view": name, "screen_percentage": percentage,
                        "mode": mode, "camera_position": xyz(position), "fov": fov}
    phase("settle")


def tick(delta):
    """等待加载、PIE 和实际截图文件；任何失败都留下明确报告，不能当作通过。"""
    try:
        now = time.monotonic()
        elapsed = now - state["at"]
        if now - started > 360:
            finish("细节验收超过 360 秒")
        elif state["phase"] == "warmup" and elapsed > 12:
            unreal.AutomationLibrary.finish_loading_before_screenshot()
            level.editor_request_begin_play()
            phase("play")
        elif state["phase"] == "play" and elapsed > 8:
            if not level.is_in_play_in_editor():
                raise RuntimeError("未能进入 PIE")
            setup_runtime()
            phase("setup")
        elif state["phase"] == "setup":
            if state["index"] >= len(CASES):
                finish()
            else:
                configure()
        elif state["phase"] == "settle" and elapsed > 4:
            path = state["path"]
            numbered = path.with_name(path.stem + "00000" + path.suffix)
            for candidate in (path, numbered):
                if candidate.exists():
                    candidate.unlink()
            command('Shot filename="%s" nosuffix' % path.as_posix())
            state["numbered"] = numbered
            phase("screenshot")
        elif state["phase"] == "screenshot" and elapsed > 1:
            path = state["path"]
            if not path.exists() and state["numbered"].exists():
                state["numbered"].replace(path)
            if not path.exists():
                if elapsed > 12:
                    raise RuntimeError("截图文件未生成：" + str(path))
                return
            state["report"]["captures"].append(state["capture"])
            unreal.log("SKETCH_DETAIL_FRAME " + path.name)
            state["index"] += 1
            phase("setup")
    except Exception:
        finish(traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
