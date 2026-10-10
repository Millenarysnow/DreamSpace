# -*- coding: utf-8 -*-
"""在真实 TEST 的 PIE 中验收距离线稿，所有测试调整仅存在于运行世界。

截图包含默认视角、原始场景、距离遮罩、关闭效果、半径扩大和玩家移动。
此脚本只供专门启动的验收编辑器运行，不保存 TEST、不改建筑原材料。
"""

import json
import time
from pathlib import Path

import unreal

MAP = "/Game/0_/Maps/TEST"
OUTPUT = Path(unreal.Paths.project_saved_dir()).resolve() / "ProximitySketch"
OUTPUT.mkdir(parents=True, exist_ok=True)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(MAP):
    raise RuntimeError("无法加载 TEST")
material = unreal.EditorAssetLibrary.load_asset("/Game/DreamPresentation/ProximitySketch/Materials/M_ProximitySketch")
errors = list(unreal.MaterialEditingLibrary.recompile_material(material))
aa_material = unreal.EditorAssetLibrary.load_asset("/Game/DreamPresentation/ProximitySketch/Materials/M_ProximitySketchAA")
errors += list(unreal.MaterialEditingLibrary.recompile_material(aa_material))
miniature_material = unreal.EditorAssetLibrary.load_asset("/Game/DreamPresentation/ProximitySketch/Materials/M_ProximityMiniatureDisplay")
errors += list(unreal.MaterialEditingLibrary.recompile_material(miniature_material))
if errors:
    raise RuntimeError("距离线稿材质图编译失败：" + str(errors))
original_aa = unreal.SystemLibrary.get_console_variable_int_value("r.AntiAliasingMethod")
original_cvars = {name: unreal.SystemLibrary.get_console_variable_float_value("dream.Sketch." + name)
                  for name in ("Enabled", "Radius", "Transition", "Debug")}
started = time.monotonic()
state = {"phase": "warmup", "at": started, "finished": False, "report": {}, "index": 0}
unreal.EditorPythonScripting.set_keep_python_script_alive(True)


def world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def execute(command):
    unreal.SystemLibrary.execute_console_command(world(), command)


def phase(name):
    state["phase"] = name
    state["at"] = time.monotonic()


def xyz(vector):
    """UE 的 FVector 不是普通 Python 元组，报告显式读取三个分量。"""
    return [vector.x, vector.y, vector.z]


def capture(name):
    """原生 Shot 捕获当前真实游戏视口，避免 CameraActor 截图绕过相机的 blendable。"""
    path = OUTPUT / name
    numbered = path.with_name(path.stem + "00000" + path.suffix)
    if numbered.exists():
        numbered.unlink()
    if path.exists():
        path.unlink()
    execute('Shot filename="%s" nosuffix' % path.as_posix())
    state["pending_file"] = path
    state["numbered_file"] = numbered


def finish(error=None):
    """每条退出路径都结束 PIE、恢复共享调试 CVar，并在报告中明确失败原因。"""
    if state["finished"]:
        return
    state["finished"] = True
    if world():
        for name, value in original_cvars.items():
            execute("dream.Sketch.%s %s" % (name, value))
    if level.is_in_play_in_editor():
        level.editor_request_end_play()
    (OUTPUT / "capture_report.json").write_text(json.dumps({
        "map": MAP, "compile_errors": errors, "aa_before": original_aa,
        "play": state["report"], "error": error,
    }, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    (unreal.log_error if error else unreal.log)("PROXIMITY_CAPTURE " + str(error or OUTPUT))


def record_runtime():
    """读回真正送入玩家相机的 MID，并记录 Pawn、镜头和方向光，而非只检查编辑器资源。"""
    controller = unreal.GameplayStatics.get_player_controller(world(), 0)
    pawn = unreal.GameplayStatics.get_player_pawn(world(), 0)
    manager = controller.player_camera_manager
    if manager.get_class().get_path_name() != "/Script/DreamSpace.DreamProximitySketchCameraManager":
        raise RuntimeError("游玩控制器没有使用距离线稿相机：" + manager.get_class().get_path_name())
    instance = manager.get_sketch_material_instance()
    if not instance:
        raise RuntimeError("真实游戏相机没有创建距离线稿 MID")
    state["controller"] = controller
    state["pawn"] = pawn
    state["manager"] = manager
    state["original_location"] = pawn.get_actor_location()
    state["hatch_strength"] = instance.get_scalar_parameter_value("HatchStrength")
    position = instance.get_vector_parameter_value("PlayerPosition")
    state["report"].update({
        "game_mode": unreal.GameplayStatics.get_game_mode(world()).get_class().get_path_name(),
        "controller": controller.get_class().get_path_name(), "pawn": pawn.get_class().get_path_name(),
        "camera_manager": manager.get_class().get_path_name(), "material": instance.get_path_name(),
        "pawn_position": xyz(pawn.get_actor_location()),
        "camera_position": xyz(manager.get_camera_location()),
        "shader_player_position": [position.r, position.g, position.b],
        "normal_radius": instance.get_scalar_parameter_value("NormalRadius"),
        "transition_width": instance.get_scalar_parameter_value("TransitionWidth"),
        "aa_runtime": unreal.SystemLibrary.get_console_variable_int_value("r.AntiAliasingMethod"),
    })
    if state["report"]["aa_runtime"] != original_aa:
        raise RuntimeError("距离线稿意外修改全局抗锯齿")
    # 暂停角色移动 / 动画只是为了让对照帧几何一致，不保存或禁用正式玩法。
    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    if movement:
        movement.stop_movement_immediately()
        movement.set_component_tick_enabled(False)
    mesh = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
    if mesh:
        mesh.set_component_tick_enabled(False)
    state["report"]["posts"] = []
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world(), unreal.PostProcessVolume):
        settings = actor.get_editor_property("settings")
        state["report"]["posts"].append({"name": actor.get_name(), "unbound": actor.get_editor_property("unbound"),
            "exposure_method": str(settings.get_editor_property("auto_exposure_method")),
            "exposure_override": settings.get_editor_property("override_auto_exposure_method"),
            "exposure_bias": settings.get_editor_property("auto_exposure_bias")})


# 每一项先更新运行时参数，等待原场景的 TSR 历史充分收敛后截图。
# 禁用与原始输入的对照验证接入透明性；半径扩大和中心移动验证实际世界距离。
CAPTURES = [
    ("ProximitySketch_Default.png", ["dream.Sketch.Debug 0"]),
    ("ProximitySketch_Repeat.png", []),
    ("ProximitySketch_NoHatching.png", []),
    ("ProximitySketch_Mask.png", ["dream.Sketch.Debug 2"]),
    ("ProximitySketch_Original.png", ["dream.Sketch.Debug 3"]),
    ("ProximitySketch_Disabled.png", ["dream.Sketch.Enabled 0"]),
    ("ProximitySketch_LargeRadius.png", ["dream.Sketch.Enabled 1", "dream.Sketch.Debug 0", "dream.Sketch.Radius 1200"]),
    ("ProximitySketch_LargeMask.png", ["dream.Sketch.Debug 2"]),
    ("ProximitySketch_Moved.png", ["dream.Sketch.Debug 0", "dream.Sketch.Radius -1"]),
    ("ProximitySketch_MovedMask.png", ["dream.Sketch.Debug 2"]),
]


def tick(delta):
    try:
        now = time.monotonic()
        elapsed = now - state["at"]
        if now - started > 220:
            finish("验收超过 220 秒")
            return
        if state["phase"] == "warmup" and elapsed > 10:
            level.editor_request_begin_play()
            phase("play")
        elif state["phase"] == "play" and elapsed > 8:
            if not level.is_in_play_in_editor():
                raise RuntimeError("未能进入 PIE")
            record_runtime()
            phase("setup")
        elif state["phase"] == "setup":
            if state["index"] >= len(CAPTURES):
                finish()
                return
            name, commands = CAPTURES[state["index"]]
            # 仅覆盖此相机的瞬时 MID，确认远处面内线条确实来自排线算法。
            state["manager"].get_sketch_material_instance().set_scalar_parameter_value(
                "HatchStrength", 0.0 if name == "ProximitySketch_NoHatching.png" else state["hatch_strength"])
            for command in commands:
                execute(command)
            if name == "ProximitySketch_Moved.png":
                # 保持观察相机固定、只移动 Pawn，可直接证明变化来自玩家球心而不是镜头距离。
                execute("dream.TestSketchMovePawn 500 0 200")
            phase("settle")
        elif state["phase"] == "settle" and elapsed > 4:
            capture(CAPTURES[state["index"]][0])
            phase("screenshot")
        elif state["phase"] == "screenshot" and elapsed > 1:
            # UE 5.8 的 Shot 即使有 nosuffix 也会追加 00000，读回后统一文件名。
            if not state["pending_file"].exists() and state["numbered_file"].exists():
                state["numbered_file"].replace(state["pending_file"])
            if not state["pending_file"].exists():
                if elapsed > 12:
                    raise RuntimeError("真实游戏视口截图未写入：" + str(state["pending_file"]))
                return
            state["report"].setdefault("screenshots", []).append(str(state["pending_file"]))
            if state["index"] == len(CAPTURES) - 1:
                manager = state["manager"]
                shader_position = manager.get_sketch_material_instance().get_vector_parameter_value("PlayerPosition")
                state["report"]["moved_pawn_position"] = xyz(state["pawn"].get_actor_location())
                state["report"]["moved_shader_position"] = [shader_position.r, shader_position.g, shader_position.b]
                state["report"]["moved_camera_position"] = xyz(manager.get_camera_location())
                state["report"]["aa_after_captures"] = unreal.SystemLibrary.get_console_variable_int_value("r.AntiAliasingMethod")
            state["index"] += 1
            phase("setup")
    except Exception as exc:
        finish(str(exc))


handle = unreal.register_slate_post_tick_callback(tick)
