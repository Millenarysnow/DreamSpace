# -*- coding: utf-8 -*-
"""真实运行菜单到 TEST，并保存过渡关键帧。

脚本只用于开发验收：地图、材质和蓝图不会被保存。截图来自实际游戏视口，因此同时
包含 3D 场景、菜单 Logo 和跨地图接力用的 Slate 快照。时间采样使用 PIE 的游戏时间，
单独记录同步加载的墙钟时间，避免把加载耗时误判成镜头动画已经完成。
"""

import json
import math
import time
import traceback
from pathlib import Path

import unreal


OUTPUT = Path(unreal.Paths.project_saved_dir()).resolve() / "MainMenu"
FRAMES = OUTPUT / "TransitionFrames"
OUTPUT.mkdir(parents=True, exist_ok=True)
FRAMES.mkdir(parents=True, exist_ok=True)
for old in FRAMES.glob("Frame_*.png"):
    old.unlink()
REPORT = OUTPUT / "capture_report.json"
MENU = "/Game/DreamPresentation/MainMenu/Maps/L_DreamMainMenu"
GAMEPLAY = "/Game/0_/Maps/TEST"
EMBEDDED = "-DreamMenuEmbedded" in unreal.SystemLibrary.get_command_line()
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(MENU):
    raise RuntimeError("无法加载菜单地图")
original_aa = unreal.SystemLibrary.get_console_variable_int_value("r.AntiAliasingMethod")
compile_errors = {}
for material_name in ("M_MenuHatching", "M_MenuPaper"):
    material = unreal.EditorAssetLibrary.load_asset(
        "/Game/DreamPresentation/MainMenu/Materials/" + material_name)
    compile_errors[material_name] = list(unreal.MaterialEditingLibrary.recompile_material(material))
    if compile_errors[material_name]:
        raise RuntimeError("菜单材质编译失败：" + str(compile_errors))
level.editor_set_viewport_realtime(False)
level.editor_set_viewport_realtime(True)
level.editor_set_game_view(True)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
started_wall = time.monotonic()
state = {
    "phase": "warmup", "phase_wall": started_wall, "finished": False,
    "pending_screenshot": None, "clicked_wall": None, "last_game_time": None,
    "report": {"captures": [], "samples": []},
}


def world():
    """只读取真正 PIE World，避免把编辑器世界误当成正在运行的游戏。"""
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def set_phase(name):
    state["phase"] = name
    state["phase_wall"] = time.monotonic()
    unreal.log("DREAM_MENU_CAPTURE_PHASE " + name)


def command(value):
    unreal.SystemLibrary.execute_console_command(world(), value)


def begin_play():
    """默认固定 16:9 新窗口；传 -DreamMenuEmbedded 可再次核对原编辑器嵌入式视口。"""
    if EMBEDDED:
        level.editor_request_begin_play()
    else:
        current_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        unreal.SystemLibrary.execute_console_command(current_editor, "dream.TestMenuBeginPIE 1280 720")


def vec(value):
    return [value.x, value.y, value.z]


def rot(value):
    return [value.pitch, value.yaw, value.roll]


def length_delta(a, b):
    return math.sqrt(sum((x - y) * (x - y) for x, y in zip(a, b)))


def shortest_angle(a, b):
    return max(abs((x - y + 180.0) % 360.0 - 180.0) for x, y in zip(a, b))


def menu_sample():
    controller = unreal.GameplayStatics.get_player_controller(world(), 0)
    camera = controller.player_camera_manager
    scene = controller.get_view_target()
    return {
        "world": world().get_outer().get_name(),
        "game_time": unreal.GameplayStatics.get_time_seconds(world()),
        "camera_location": vec(camera.get_camera_location()),
        "camera_rotation": rot(camera.get_camera_rotation()),
        "fov": camera.get_fov_angle(),
        "building_rotation": scene.get_rotation_angle(),
    }


def gameplay_sample():
    current_world = world()
    controller = unreal.GameplayStatics.get_player_controller(current_world, 0)
    pawn = unreal.GameplayStatics.get_player_pawn(current_world, 0)
    camera = controller.player_camera_manager
    material = camera.get_sketch_material_instance()
    return {
        "world": current_world.get_outer().get_name(),
        "game_time": unreal.GameplayStatics.get_time_seconds(current_world),
        "camera_location": vec(camera.get_camera_location()),
        "camera_rotation": rot(camera.get_camera_rotation()),
        "fov": camera.get_fov_angle(),
        "arrival_active": controller.is_menu_arrival_active(),
        "cursor_visible": controller.show_mouse_cursor,
        "pawn_location": vec(pawn.get_actor_location()) if pawn else None,
        "normal_radius": camera.get_effective_normal_radius() if pawn else None,
        "rendered_radius": material.get_scalar_parameter_value("NormalRadius") if material else None,
    }


def request_screenshot(name, sample=None):
    """直接读取指定 PIE 视口；串行确认落盘，保证连续帧与采样状态一一对应。"""
    if state["pending_screenshot"] is not None:
        return False
    path = (FRAMES / name) if name.startswith("Frame_") else (OUTPUT / name)
    if path.exists():
        path.unlink()
    state["pending_screenshot"] = (path, time.monotonic())
    state["report"]["captures"].append({"file": str(path.relative_to(OUTPUT)), "sample": sample})
    # 直接读取当前 PIE 视口，连续序列和关键帧都保留 Slate 接力层，可回看实际淡化。
    command('dream.TestMenuScreenshot "%s"' % path.as_posix())
    return True


def finish(error=None):
    """无论脚本成功还是异常，都结束 PIE 并退出专用验收编辑器。"""
    if state["finished"]:
        return
    state["finished"] = True
    state["report"]["error"] = error
    REPORT.write_text(json.dumps({"menu": MENU, "gameplay": GAMEPLAY,
                                  "compile_errors": compile_errors,
                                  "play": state["report"]},
                                 ensure_ascii=False, indent=2), encoding="utf-8")
    if level.is_in_play_in_editor():
        level.editor_request_end_play()
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    (unreal.log_error if error else unreal.log)("DREAM_MENU_CAPTURE " + str(error or REPORT))
    unreal.SystemLibrary.quit_editor()


def sample_transition():
    """按游戏时间记录相机位置、FOV、门控状态和线稿半径，供视觉回看。"""
    result = gameplay_sample()
    state["report"]["samples"].append(result)
    return result


def verify_player_view(runtime):
    """实际玩家 POV 来自 FollowCamera；终点回到完整原视角，含碰撞后的肩位和原 FOV。"""
    pawn = unreal.GameplayStatics.get_player_pawn(world(), 0)
    camera = pawn.get_component_by_class(unreal.CameraComponent)
    result = {"position_error_cm": length_delta(runtime["camera_location"], vec(camera.get_world_location())),
              "rotation_error_deg": shortest_angle(runtime["camera_rotation"], rot(camera.get_world_rotation())),
              "fov_error_deg": abs(runtime["fov"] - camera.field_of_view),
              "radius_error_cm": abs(runtime["rendered_radius"] - runtime["normal_radius"])}
    if result["position_error_cm"] > 1.0 or result["rotation_error_deg"] > 0.1 \
            or result["fov_error_deg"] > 0.01 or result["radius_error_cm"] > 0.01:
        raise RuntimeError("玩家视角接管未精确返回原相机：" + str(result))
    return result


def tick(delta_seconds):
    # TakeScreenshot / 地图切换会嵌套刷新 Slate；同一回调不能在重入时重复请求 PIE 或截图。
    if state.get("ticking"):
        return
    state["ticking"] = True
    try:
        now = time.monotonic()
        elapsed = now - state["phase_wall"]
        if now - started_wall > 240.0:
            raise RuntimeError("菜单过渡验收超过 240 秒")

        pending = state["pending_screenshot"]
        if pending:
            path, requested = pending
            if path.exists():
                state["pending_screenshot"] = None
            elif now - requested > 15.0:
                raise RuntimeError("截图未生成：" + str(path))

        phase = state["phase"]
        if phase == "warmup" and elapsed > 8.0:
            unreal.AutomationLibrary.finish_loading_before_screenshot()
            begin_play()
            set_phase("menu")
        elif phase == "menu" and elapsed > 6.0:
            current_world = world()
            controller = unreal.GameplayStatics.get_player_controller(current_world, 0)
            mode = unreal.GameplayStatics.get_game_mode(current_world)
            if not controller or controller.get_class().get_path_name() != "/Script/DreamSpace.DreamMainMenuPlayerController":
                raise RuntimeError("菜单控制器不正确")
            if mode.get_class().get_path_name() != "/Script/DreamSpace.DreamMainMenuGameMode":
                raise RuntimeError("菜单 GameMode 不正确")
            if unreal.GameplayStatics.get_player_pawn(current_world, 0) or controller.get_hud():
                raise RuntimeError("菜单不应生成 Pawn 或 HUD")
            state["controller"] = controller
            state["scene"] = controller.get_view_target()
            state["report"].update({
                "menu_controller": controller.get_class().get_path_name(),
                "menu_game_mode": mode.get_class().get_path_name(),
                "logo_on_right": state["scene"].logo_on_right,
                "anti_aliasing_before": original_aa,
                "anti_aliasing_menu": unreal.SystemLibrary.get_console_variable_int_value("r.AntiAliasingMethod"),
            })
            state["report"]["menu_initial"] = menu_sample()
            request_screenshot("MainMenu_PIE.png", state["report"]["menu_initial"])
            set_phase("rotation")
        elif phase == "rotation" and elapsed > 5.0 and not state["pending_screenshot"]:
            state["report"]["menu_rotated"] = menu_sample()
            if abs(state["report"]["menu_rotated"]["building_rotation"] -
                   state["report"]["menu_initial"]["building_rotation"]) < 3.0:
                raise RuntimeError("建筑没有持续旋转")
            request_screenshot("MainMenu_Rotated_PIE.png", state["report"]["menu_rotated"])
            set_phase("click")
        elif phase == "click" and elapsed > 1.0 and not state["pending_screenshot"]:
            scene = state["scene"]
            facing = scene.rooftop_player_rotation
            axes = [unreal.MathLibrary.get_forward_vector(facing), unreal.MathLibrary.get_right_vector(facing),
                    unreal.MathLibrary.get_up_vector(facing)]
            rotate = lambda v: axes[0] * v.x + axes[1] * v.y + axes[2] * v.z
            entry = scene.rooftop_player_location + rotate(scene.rooftop_camera_offset)
            focus = scene.rooftop_player_location + rotate(scene.rooftop_focus_offset)
            state["expected_entry"] = {"location": vec(entry),
                                       "rotation": rot(unreal.MathLibrary.find_look_at_rotation(entry, focus)),
                                       "fov": scene.rooftop_field_of_view}
            state["clicked_wall"] = now
            state["clicked_game_time"] = unreal.GameplayStatics.get_time_seconds(world())
            command("dream.TestMenuClick 0.96 0.96")
            if not state["controller"].is_starting_game():
                raise RuntimeError("右下留白处点击未触发开始")
            state["report"]["blank_area_click_accepted"] = True
            state["approach_shot_times"] = [(0.8, "Rooftop_Approach_08.png"),
                                             (1.8, "Rooftop_Approach_18.png"),
                                             (2.8, "Rooftop_Approach_28.png"),
                                             (3.8, "Rooftop_MenuEnd.png")]
            state["next_continuous"] = 0.0
            set_phase("approach")
        elif phase == "approach":
            current_world = world()
            package = current_world.get_outer().get_name()
            if package.endswith("_TEST") or package.endswith("/TEST"):
                # 地图已加载。第一帧必须仍是接力阶段；相机时间从这里开始计。
                runtime = sample_transition()
                if not runtime["arrival_active"]:
                    raise RuntimeError("TEST 第一帧没有天台接力门控")
                state["controller"] = unreal.GameplayStatics.get_player_controller(current_world, 0)
                state["pawn"] = unreal.GameplayStatics.get_player_pawn(current_world, 0)
                entry_error = length_delta(runtime["camera_location"], state["expected_entry"]["location"])
                if entry_error > 1.0 or shortest_angle(runtime["camera_rotation"], state["expected_entry"]["rotation"]) > 0.1:
                    raise RuntimeError("跨地图首帧天台取景不一致：" + str(runtime))
                state["arrival_start_game"] = runtime["game_time"]
                state["report"].update({
                    "gameplay_world": package,
                    "gameplay_controller": state["controller"].get_class().get_path_name(),
                    "gameplay_pawn": unreal.GameplayStatics.get_player_pawn(current_world, 0).get_class().get_path_name(),
                    "first_arrival": runtime,
                    "entry_error_cm": entry_error,
                    "loading_wall_seconds": now - state["last_menu_wall"],
                    "anti_aliasing_gameplay": unreal.SystemLibrary.get_console_variable_int_value("r.AntiAliasingMethod"),
                })
                if state["report"]["anti_aliasing_gameplay"] != original_aa:
                    raise RuntimeError("进入 TEST 后抗锯齿未恢复")
                # 在发现 TEST 的同一回调截首帧，不能等下个 Tick 被首次渲染长帧吃掉时长。
                request_screenshot("Rooftop_GameEntry.png", runtime)
                state["arrival_shots"] = [(0.3, "Rooftop_Crossfade.png"),
                                           (0.65, "Rooftop_GameSketch.png"),
                                           (1.4, "Player_Landing.png")]
                state["next_continuous"] = 0.0
                # 同时走原始键盘输入和蓝图触摸入口，两者在落镜头完成前都不能改变角色视角。
                state["blocked_rotation"] = rot(state["controller"].get_control_rotation())
                state["pawn"].do_move(1.0, 0.0)
                state["pawn"].do_look(20.0, 15.0)
                state["pawn"].do_jump_start()
                command("dream.TestPlayerKey W Press")
                command("dream.TestPlayerKey MouseX Axis 20")
                command("dream.TestPlayerKey SpaceBar Press")
                set_phase("arrival")
            else:
                state["last_menu_wall"] = now
                elapsed_game = unreal.GameplayStatics.get_time_seconds(current_world) - state["clicked_game_time"]
                menu_state = menu_sample()
                state["report"]["samples"].append({"stage": "menu_approach",
                                                    "phase_time": elapsed_game, **menu_state})
                if state["approach_shot_times"] and elapsed_game >= state["approach_shot_times"][0][0] \
                        and not state["pending_screenshot"]:
                    _, filename = state["approach_shot_times"].pop(0)
                    request_screenshot(filename, menu_state)
                elif elapsed_game >= state["next_continuous"] and not state["pending_screenshot"]:
                    request_screenshot("Frame_%04d.png" % len(state["report"]["samples"]), menu_state)
                    state["next_continuous"] = elapsed_game + 0.15
        elif phase == "arrival":
            runtime = sample_transition()
            phase_time = runtime["game_time"] - state["arrival_start_game"]
            runtime["stage"] = "game_arrival"
            runtime["phase_time"] = phase_time
            if runtime["arrival_active"] and shortest_angle(rot(state["controller"].get_control_rotation()),
                                                           state["blocked_rotation"]) > 0.01:
                raise RuntimeError("入场期间误接收视角输入")
            if state["arrival_shots"] and phase_time >= state["arrival_shots"][0][0] \
                    and not state["pending_screenshot"]:
                _, filename = state["arrival_shots"].pop(0)
                request_screenshot(filename, runtime)
            elif phase_time >= state["next_continuous"] and not state["pending_screenshot"]:
                request_screenshot("Frame_%04d.png" % len(state["report"]["samples"]), runtime)
                state["next_continuous"] = phase_time + 0.15
            if not runtime["arrival_active"]:
                command("dream.TestPlayerKey W Release")
                command("dream.TestPlayerKey SpaceBar Release")
                state["report"].update({
                    "arrival_finished_game_seconds": phase_time,
                    "cursor_visible_after_travel": runtime["cursor_visible"],
                    "fov_after_arrival": runtime["fov"],
                    "player_view": verify_player_view(runtime),
                    "input_blocked_during_arrival": True,
                })
                if runtime["cursor_visible"]:
                    raise RuntimeError("进入游玩后光标仍然显示")
                set_phase("post_arrival")
        elif phase == "post_arrival" and elapsed > 0.7 and not state["pending_screenshot"]:
            request_screenshot("Gameplay_TEST_PIE.png", sample_transition())
            state["before_move"] = vec(unreal.GameplayStatics.get_player_pawn(world(), 0).get_actor_location())
            state["before_look"] = rot(state["controller"].get_control_rotation())
            command("dream.TestPlayerKey W Press")
            command("dream.TestPlayerKey MouseX Axis 8")
            set_phase("input")
        elif phase == "input" and elapsed > 0.4:
            command("dream.TestPlayerKey W Release")
            pawn = unreal.GameplayStatics.get_player_pawn(world(), 0)
            moved = length_delta(vec(pawn.get_actor_location()), state["before_move"])
            turned = shortest_angle(rot(state["controller"].get_control_rotation()), state["before_look"])
            state["report"].update({"move_after_arrival_cm": moved, "look_after_arrival_deg": turned})
            if moved < 2.0 or turned < 0.1:
                raise RuntimeError("接管后移动或鼠标观察未恢复：%s cm / %s deg" % (moved, turned))
            command("dream.TestPlayerKey SpaceBar Press")
            state["jump_z"] = pawn.get_actor_location().z
            set_phase("jump")
        elif phase == "jump" and elapsed > 0.25:
            command("dream.TestPlayerKey SpaceBar Release")
            pawn = unreal.GameplayStatics.get_player_pawn(world(), 0)
            state["report"]["jump_delta_z_cm"] = pawn.get_actor_location().z - state["jump_z"]
            if state["report"]["jump_delta_z_cm"] < 3.0:
                raise RuntimeError("接管后跳跃未恢复")
            level.editor_request_end_play()
            set_phase("direct_load")
        elif phase == "direct_load" and elapsed > 2.0 and not level.is_in_play_in_editor():
            if not level.load_level(GAMEPLAY):
                raise RuntimeError("直接 TEST 无法加载")
            begin_play()
            set_phase("direct_play")
        elif phase == "direct_play" and elapsed > 5.0:
            runtime = gameplay_sample()
            if runtime["arrival_active"]:
                raise RuntimeError("直接 PIE TEST 错误继承菜单入场")
            state["report"]["direct_test_no_arrival"] = True
            state["report"]["direct_player_view"] = verify_player_view(runtime)
            request_screenshot("Gameplay_Direct_TEST_PIE.png", runtime)
            set_phase("finish")
        elif phase == "finish" and not state["pending_screenshot"]:
            finish()
    except Exception:
        finish(traceback.format_exc())
    finally:
        state["ticking"] = False


handle = unreal.register_slate_post_tick_callback(tick)
