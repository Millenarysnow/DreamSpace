# -*- coding: utf-8 -*-
"""真实运行菜单、截取含 Logo 的视口，并在空白处点击进入 TEST。

此脚本在专门启动的验收编辑器中使用 -ExecutePythonScript 运行。
截图和报告写入 Saved/MainMenu；运行时调整均不保存到关卡或材质实例。
"""

import json
import time
from pathlib import Path

import unreal


OUTPUT = Path(unreal.Paths.project_saved_dir()).resolve() / "MainMenu"
OUTPUT.mkdir(parents=True, exist_ok=True)
REPORT = OUTPUT / "capture_report.json"
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
require_map = "/Game/DreamPresentation/MainMenu/Maps/L_DreamMainMenu"
if not level.load_level(require_map):
    raise RuntimeError("无法加载菜单地图")
original_aa = unreal.SystemLibrary.get_console_variable_int_value("r.AntiAliasingMethod")
errors = {}
for name in ("M_MenuHatching", "M_MenuPaper"):
    material = unreal.EditorAssetLibrary.load_asset("/Game/DreamPresentation/MainMenu/Materials/" + name)
    errors[name] = list(unreal.MaterialEditingLibrary.recompile_material(material))
    if errors[name]:
        raise RuntimeError("菜单材质编译失败：" + str(errors))
level.editor_set_viewport_realtime(False)
level.editor_set_viewport_realtime(True)
level.editor_set_game_view(True)
started = time.monotonic()
state = {"phase": "warmup", "at": started, "report": {}, "finished": False}
unreal.EditorPythonScripting.set_keep_python_script_alive(True)


def game_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def finish(error=None):
    """报告必须明确标识失败，且结束本次 PIE，恢复共享的抗锯齿状态。"""
    if state["finished"]:
        return
    state["finished"] = True
    REPORT.write_text(json.dumps({"map": require_map, "compile_errors": errors,
                                 "play": state["report"], "error": error},
                                ensure_ascii=False, indent=2), encoding="utf-8")
    if level.is_in_play_in_editor():
        level.editor_request_end_play()
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    (unreal.log_error if error else unreal.log)("DREAM_MENU_CAPTURE " + str(error or REPORT))


def screenshot(world, filename):
    """使用测试构建的命令捕获 3D 与 Slate，而非会漏掉 Logo 的 HighResShot。"""
    path = OUTPUT / filename
    if path.exists():
        path.unlink()
    unreal.SystemLibrary.execute_console_command(world, 'dream.TestMenuScreenshot "%s"' % path.as_posix())


def set_phase(name):
    state["phase"] = name
    state["at"] = time.monotonic()


def tick(delta):
    try:
        now = time.monotonic()
        elapsed = now - state["at"]
        if now - started > 180.0:
            finish("验收超过 180 秒")
            return
        phase = state["phase"]
        if phase == "warmup" and elapsed > 8.0:
            level.editor_request_begin_play()
            set_phase("play")
        elif phase == "play" and elapsed > 6.0:
            world = game_world()
            controller = unreal.GameplayStatics.get_player_controller(world, 0)
            mode = unreal.GameplayStatics.get_game_mode(world)
            scene = controller.get_view_target() if controller else None
            if not controller or controller.get_class().get_path_name() != "/Script/DreamSpace.DreamMainMenuPlayerController":
                raise RuntimeError("菜单控制器不正确")
            if mode.get_class().get_path_name() != "/Script/DreamSpace.DreamMainMenuGameMode":
                raise RuntimeError("菜单 GameMode 不正确")
            if unreal.GameplayStatics.get_player_pawn(world, 0) or controller.get_hud():
                raise RuntimeError("菜单不应生成 Pawn 或 HUD")
            state["controller"] = controller
            state["scene"] = scene
            state["report"]["menu_game_mode"] = mode.get_class().get_path_name()
            state["report"]["menu_controller"] = controller.get_class().get_path_name()
            state["report"]["rotation_first"] = scene.get_rotation_angle()
            state["report"]["anti_aliasing_before"] = original_aa
            state["report"]["anti_aliasing_menu"] = unreal.SystemLibrary.get_console_variable_int_value("r.AntiAliasingMethod")
            screenshot(world, "MainMenu_PIE.png")
            set_phase("rotation")
        elif phase == "rotation" and elapsed > 5.0:
            if not (OUTPUT / "MainMenu_PIE.png").exists():
                raise RuntimeError("含 Logo 的菜单截图未生成")
            scene = state["scene"]
            state["report"]["rotation_second"] = scene.get_rotation_angle()
            if abs(state["report"]["rotation_second"] - state["report"]["rotation_first"]) < 3.0:
                raise RuntimeError("建筑没有持续旋转")
            screenshot(game_world(), "MainMenu_Rotated_PIE.png")
            set_phase("click")
        elif phase == "click" and elapsed > 1.5:
            unreal.SystemLibrary.execute_console_command(game_world(), "dream.TestMenuClick 0.96 0.96")
            if not state["controller"].is_starting_game():
                raise RuntimeError("在菜单右下留白处点击未触发开始")
            state["report"]["blank_area_click_accepted"] = True
            set_phase("fade")
        elif phase == "fade" and elapsed > 0.35:
            screenshot(game_world(), "MainMenu_Fade_PIE.png")
            set_phase("travel")
        elif phase == "travel" and elapsed > 5.0:
            world = game_world()
            package = world.get_outer().get_name()
            if not package.endswith("_TEST") and not package.endswith("/TEST"):
                raise RuntimeError("点击后没有打开 TEST：" + package)
            controller = unreal.GameplayStatics.get_player_controller(world, 0)
            pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
            if not controller or not pawn:
                raise RuntimeError("TEST 未生成游玩控制器和角色")
            state["report"].update({"gameplay_world": package,
                                     "gameplay_controller": controller.get_class().get_path_name(),
                                     "gameplay_pawn": pawn.get_class().get_path_name(),
                                     "cursor_visible_after_travel": controller.get_editor_property("show_mouse_cursor"),
                                     "anti_aliasing_gameplay": unreal.SystemLibrary.get_console_variable_int_value("r.AntiAliasingMethod")})
            if state["report"]["anti_aliasing_gameplay"] != original_aa:
                raise RuntimeError("进入 TEST 后抗锯齿未恢复")
            screenshot(world, "Gameplay_TEST_PIE.png")
            level.editor_request_end_play()
            set_phase("end")
        elif phase == "end" and elapsed > 2.0:
            finish()
    except Exception as error:
        finish(str(error))


handle = unreal.register_slate_post_tick_callback(tick)
