# -*- coding: utf-8 -*-
"""在独立 UE 编辑器的 TEST / PIE 主视口中采集世界恢复效果。

通过 -ExecutePythonScript 运行，需要 PythonScriptPlugin 和 EditorScriptingUtilities。
原图、初始线稿、扩张中途和最终恢复保持同一镜头及玩家位置；使用组件实际调用的原生
StartWorldRecovery 入口，按真实游戏帧推进。截图和参数报告只写入 Saved，结束后退出
专用编辑器，不保存地图、材质或运行时半径。E 射线 / 组件分发另由原生自动化测试覆盖。
"""

import json
import time
import traceback
from pathlib import Path

import unreal


OUTPUT = Path(unreal.Paths.project_saved_dir()).resolve() / "ProximitySketch/WorldRecovery"
OUTPUT.mkdir(parents=True, exist_ok=True)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level("/Game/0_/Maps/TEST"):
    raise RuntimeError("无法加载世界恢复验收所需的 TEST")
editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
room = next(actor for actor in editor.get_all_level_actors() if actor.get_name() == "Rom_C_1")
origin = room.get_actor_location()
# 编辑器里的临时相机随 PIE 复制，仍由真实玩家相机管理器合成两个后处理通道。
# 不使用独立 SceneCapture 替代玩家视口，否则无法验证本次恢复动画的实际接入。
camera = editor.spawn_actor_from_class(unreal.CameraActor, origin)
camera.tags = ["DreamWorldRecoveryCapture"]
camera.camera_component.set_editor_property("constrain_aspect_ratio", False)
camera.camera_component.set_field_of_view(65.0)
position = origin + unreal.Vector(-1450, -3100, 1380)
target = origin + unreal.Vector(0, -500, 600)
camera.set_actor_location_and_rotation(position, unreal.MathLibrary.find_look_at_rotation(position, target), False, False)

started = time.monotonic()
state = {"phase": "warmup", "at": started, "done": False,
         "report": {"map": "/Game/0_/Maps/TEST", "captures": [], "samples": []}}
unreal.EditorPythonScripting.set_keep_python_script_alive(True)


def world():
    """只读取真正的 PIE 世界，避免将恢复请求发给编辑器世界的临时 Actor。"""
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def command(value):
    """控制台覆盖只存在于本次专用进程，不改项目默认配置。"""
    unreal.SystemLibrary.execute_console_command(world(), value)


def phase(value):
    """Slate 帧回调等待 GPU 和截图写盘，不阻塞 UE 主线程。"""
    state["phase"] = value
    state["at"] = time.monotonic()


def sample():
    """读回实际主通道和平滑通道的参数，报告动画是否真正到达玩家的渲染实例。"""
    manager = state["manager"]
    main = manager.get_sketch_material_instance()
    smoothing = state["smoothing"]
    result = {"game_time": unreal.GameplayStatics.get_time_seconds(world()),
              "radius": manager.get_effective_normal_radius(),
              "main_radius": main.get_scalar_parameter_value("NormalRadius"),
              "main_strength": main.get_scalar_parameter_value("EffectStrength"),
              "smoothing_radius": smoothing.get_scalar_parameter_value("NormalRadius"),
              "smoothing_strength": smoothing.get_scalar_parameter_value("EffectStrength"),
              "active": manager.is_world_recovery_active(), "finished": manager.has_world_recovery_finished()}
    state["report"]["samples"].append(result)
    return result


def screenshot(name, next_phase):
    """请求真实游戏视口截图，等文件写入后才进入下一阶段。"""
    path = OUTPUT / name
    numbered = path.with_name(path.stem + "00000" + path.suffix)
    for candidate in (path, numbered):
        if candidate.exists():
            candidate.unlink()
    state.update(path=path, numbered=numbered, next_phase=next_phase)
    state["report"]["captures"].append({"file": name, "runtime": sample()})
    command('Shot filename="%s" nosuffix' % path.as_posix())
    phase("screenshot")


def finish(error=None):
    """每条退出路径都结束 PIE 和专用进程；不保存临时相机或地图。"""
    if state["done"]:
        return
    state["done"] = True
    state["report"]["error"] = error
    (OUTPUT / "capture_report.json").write_text(
        json.dumps(state["report"], ensure_ascii=False, indent=2), encoding="utf-8")
    if world():
        unreal.GameplayStatics.set_game_paused(world(), False)
    if level.is_in_play_in_editor():
        level.editor_request_end_play()
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    (unreal.log_error if error else unreal.log)("WORLD_RECOVERY_CAPTURE " + str(error or OUTPUT))
    unreal.SystemLibrary.quit_editor()


def setup():
    """冻结人物运动 / 动画并固定镜头，避免把姿态或构图变化误判为渲染范围恢复。"""
    controller = unreal.GameplayStatics.get_player_controller(world(), 0)
    # 自定义 APlayerController 的 UE Python 反射不总是暴露 GetPawn 便捷包装，
    # GameplayStatics 由引擎读取实际索引 0 的已占有 Pawn，和日常 PIE 相同。
    pawn = unreal.GameplayStatics.get_player_pawn(world(), 0)
    manager = controller.player_camera_manager
    main = manager.get_sketch_material_instance()
    if not main:
        raise RuntimeError("玩家相机尚未创建距离线稿动态实例")
    smoothing = next(material for material in unreal.ObjectIterator(unreal.MaterialInstanceDynamic)
                     if material.get_outer() == manager
                     and material.get_base_material().get_name() == "M_ProximitySketchAA")
    active_camera = next(actor for actor in unreal.GameplayStatics.get_all_actors_of_class(world(), unreal.CameraActor)
                         if actor.actor_has_tag("DreamWorldRecoveryCapture"))
    controller.set_view_target_with_blend(active_camera, 0.0)
    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    if movement:
        movement.stop_movement_immediately()
        movement.set_component_tick_enabled(False)
    for mesh in pawn.get_components_by_class(unreal.SkeletalMeshComponent):
        mesh.set_component_tick_enabled(False)
    pawn.set_actor_location(origin + unreal.Vector(0, -500, 200), False, False)
    state.update(manager=manager, smoothing=smoothing)
    for value in ("dream.Sketch.Radius 600", "dream.Sketch.Transition 150", "dream.Sketch.Debug 0",
                  "dream.Sketch.Enabled 0", "r.DynamicRes.OperationMode 0",
                  "r.ScreenPercentage.Default.Desktop.Mode 0", "r.Editor.Viewport.OverridePIEScreenPercentage 0",
                  "r.ScreenPercentage 100"):
        command(value)
    state["report"].update(camera_manager=manager.get_class().get_path_name(),
                           target_radius=3000.0, recovery_duration=8.0,
                           aa_method=unreal.SystemLibrary.get_console_variable_int_value("r.AntiAliasingMethod"))


def tick(delta):
    """按运行时状态采集关键帧，包含正常半径扩张及末段天空 / 远景淡出。"""
    try:
        now = time.monotonic()
        elapsed = now - state["at"]
        current = state["phase"]
        if now - started > 180:
            finish("世界恢复 GPU 验收超过 180 秒")
        elif current == "warmup" and elapsed > 12:
            unreal.AutomationLibrary.finish_loading_before_screenshot()
            level.editor_request_begin_play()
            phase("play")
        elif current == "play" and elapsed > 8:
            if not level.is_in_play_in_editor():
                raise RuntimeError("未能进入 PIE")
            setup()
            phase("original")
        elif current == "original" and elapsed > 3:
            screenshot("Original.png", "enable_sketch")
        elif current == "enable_sketch":
            command("dream.Sketch.Enabled 1")
            phase("sketch")
        elif current == "sketch" and elapsed > 3:
            screenshot("Sketch.png", "start_recovery")
        elif current == "start_recovery":
            if not state["manager"].start_world_recovery(3000.0, 8.0, True):
                raise RuntimeError("实际玩家相机拒绝了世界恢复请求")
            phase("expand")
        elif current == "expand":
            runtime = sample()
            if runtime["radius"] >= 1800.0:
                # 暂停游戏时间后仍等待 Slate / GPU 绘制，采集同一恢复半径的稳定画面。
                # 这同时验证游戏暂停不会继续推进恢复，恢复后再按真实游戏时间完成剩余阶段。
                if not unreal.GameplayStatics.set_game_paused(world(), True):
                    raise RuntimeError("无法暂停以采集恢复半程")
                state["paused_radius"] = runtime["radius"]
                phase("midpoint")
        elif current == "midpoint" and elapsed > 1:
            if abs(state["manager"].get_effective_normal_radius() - state["paused_radius"]) > 0.01:
                raise RuntimeError("暂停期间恢复半径仍在推进")
            screenshot("Expanding.png", "resume")
        elif current == "resume":
            unreal.GameplayStatics.set_game_paused(world(), False)
            phase("complete")
        elif current == "complete":
            runtime = sample()
            if runtime["finished"]:
                if runtime["main_strength"] != 0.0 or runtime["smoothing_strength"] != 0.0:
                    raise RuntimeError("恢复完成后两个线稿通道未清零")
                phase("restored")
        elif current == "restored" and elapsed > 3:
            screenshot("Restored.png", "finish")
        elif current == "finish":
            finish()
        elif current == "screenshot" and elapsed > 0.5:
            path = state["path"]
            if not path.exists() and state["numbered"].exists():
                state["numbered"].replace(path)
            if path.exists():
                phase(state["next_phase"])
            elif elapsed > 12:
                raise RuntimeError("主视口截图未写入：" + str(path))
    except Exception:
        finish(traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
