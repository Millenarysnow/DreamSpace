# -*- coding: utf-8 -*-
"""真实 UE 渲染器的白盒版本截图与 PIE 验收脚本。

脚本不会修改源 TEST 关卡，只加载独立的 WhiteBoxPreview，检查两个材质能否
重新编译，并确认点击“运行”时使用预览 GameMode、固定相机且没有 Pawn/HUD。
"""

import json
import time
from pathlib import Path

import unreal


MAP_PATH = "/Game/DreamPresentation/WhiteBoxPreview/Maps/L_WhiteBoxPreview"
MATERIAL_ROOT = "/Game/DreamPresentation/WhiteBoxPreview/Materials/"
OUTPUT_DIR = Path(unreal.Paths.project_saved_dir()).resolve() / "WhitePreview"
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
SCREENSHOT = OUTPUT_DIR / "WhiteBoxPreview.png"
SMALL_SCREENSHOT = OUTPUT_DIR / "WhiteBoxPreview_1280x720.png"
CAPTURES = [(1920, 1080, SCREENSHOT), (1280, 720, SMALL_SCREENSHOT)]
REPORT = OUTPUT_DIR / "white_box_capture_report.json"
if REPORT.exists():
    REPORT.unlink()

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(MAP_PATH):
    raise RuntimeError("无法加载白盒预览关卡")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
camera = next((actor for actor in actors if isinstance(actor, unreal.CameraActor)
               and actor.actor_has_tag("DreamWhitePreviewCamera")), None)
if not camera:
    raise RuntimeError("白盒预览关卡缺少固定相机")

# 确认关卡只启用白盒渐变，不残留上一版描边或线条抗锯齿。
volumes = [actor for actor in actors if isinstance(actor, unreal.PostProcessVolume)]
if len(volumes) != 1:
    raise RuntimeError("白盒关卡应只有一个后处理体积")
weighted = volumes[0].get_editor_property("settings").get_editor_property("weighted_blendables")
active_blendables = [item.get_editor_property("object").get_path_name()
                    for item in weighted.get_editor_property("array")
                    if item.get_editor_property("object") and item.get_editor_property("weight") > 0.0]
if active_blendables != [MATERIAL_ROOT + "MI_WhiteBoxPost.MI_WhiteBoxPost"]:
    raise RuntimeError("白盒关卡还启用了其他后处理：" + str(active_blendables))

compile_errors = {}
node_counts = {}
for name in ("M_WhiteBoxSurface", "M_WhiteBoxPost"):
    material = unreal.EditorAssetLibrary.load_asset(MATERIAL_ROOT + name)
    if not material:
        raise RuntimeError("白盒材质缺失：" + name)
    errors = list(unreal.MaterialEditingLibrary.recompile_material(material))
    compile_errors[name] = errors
    node_counts[name] = unreal.MaterialEditingLibrary.get_num_material_expressions(material)
    if errors:
        raise RuntimeError("白盒材质编译错误：" + "\n".join(errors))

white_box_instance = unreal.EditorAssetLibrary.load_asset(MATERIAL_ROOT + "MI_WhiteBoxPost")
parameter_values = {}
for name in ("EdgeWidth", "EdgeDarkness", "EdgeFalloff", "NormalThreshold",
             "DepthThreshold", "FaceBase", "FaceContrast", "EdgeGray"):
    parameter_values[name] = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(
        white_box_instance, name)

# UE 5.8 的 True 实际是移除一个“关闭实时”的覆盖项，直接调用可能触发 ensure。
# 先添加再移除这个临时项，保持验收视口实时渲染，并避免启动阶段的无效警告。
level.editor_set_viewport_realtime(False)
level.editor_set_viewport_realtime(True)
level.editor_set_game_view(True)
level.pilot_level_actor(camera)
level.set_exact_camera_view(True)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "r.ScreenPercentage 100")
for _, _, path in CAPTURES:
    if path.exists():
        path.unlink()

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
started = time.monotonic()
state = {
    "task": None,
    "finished": False,
    "capture_index": 0,
    "play_requested": False,
    "play_verified": False,
    "play_requested_at": 0.0,
    "play_end_requested_at": 0.0,
    "play_report": {},
}


def finish(error=None):
    """写入可复核报告后关闭验收编辑器实例。"""
    if state["finished"]:
        return
    state["finished"] = True
    REPORT.write_text(json.dumps({
        "map": MAP_PATH,
        "screenshot": str(SCREENSHOT),
        "screenshot_exists": SCREENSHOT.exists(),
        "screenshots": {"%dx%d" % (width, height): {"path": str(path), "exists": path.exists()}
                        for width, height, path in CAPTURES},
        "compile_errors": compile_errors,
        "active_blendables": active_blendables,
        "material_node_counts": node_counts,
        "white_box_parameters": parameter_values,
        "play_preview": state["play_report"],
        "error": error,
    }, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.unregister_slate_post_tick_callback(tick_handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    if error:
        unreal.log_error("WHITE_BOX_CAPTURE_FAILED " + error)
    else:
        unreal.log("WHITE_BOX_CAPTURE_OK " + str(SCREENSHOT))


def tick(delta_seconds):
    """等待资源稳定、截图，然后实际运行 PIE 验证独立预览关卡。"""
    elapsed = time.monotonic() - started
    try:
        if elapsed > 180.0:
            if level.is_in_play_in_editor():
                level.editor_request_end_play()
            finish("白盒验收任务超过 180 秒")
            return
        if state["task"] is None and elapsed > 10.0:
            width, height, path = CAPTURES[state["capture_index"]]
            state["task"] = unreal.AutomationLibrary.take_high_res_screenshot(
                width, height, str(path), camera=camera, delay=1.0, force_game_view=True)
            if state["task"] is None:
                finish("无法创建白盒截图任务")
        elif state["task"] is not None and state["task"].is_task_done():
            if not CAPTURES[state["capture_index"]][2].exists():
                finish("白盒截图任务完成但文件未写入")
                return
            # 第二种分辨率验证屏幕采样坐标和渐变宽度随视口缩放仍然正确。
            if state["capture_index"] + 1 < len(CAPTURES):
                state["capture_index"] += 1
                state["task"] = None
                return
            if not state["play_requested"]:
                level.editor_request_begin_play()
                state["play_requested"] = True
                state["play_requested_at"] = time.monotonic()
            elif not state["play_verified"] and time.monotonic() - state["play_requested_at"] > 3.0:
                if not level.is_in_play_in_editor():
                    finish("白盒 PIE 无法启动")
                    return
                game_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
                controller = unreal.GameplayStatics.get_player_controller(game_world, 0)
                game_mode = unreal.GameplayStatics.get_game_mode(game_world)
                if not controller or not game_mode:
                    finish("白盒 PIE 缺少控制器或 GameMode")
                    return
                view_target = controller.get_view_target()
                pawn = unreal.GameplayStatics.get_player_pawn(game_world, 0)
                hud = controller.get_hud()
                state["play_report"] = {
                    "game_mode": game_mode.get_class().get_path_name(),
                    "view_target": view_target.get_name() if view_target else None,
                    "camera_tag_matches": bool(view_target and view_target.actor_has_tag("DreamWhitePreviewCamera")),
                    "pawn": pawn.get_name() if pawn else None,
                    "hud": hud.get_name() if hud else None,
                }
                if (state["play_report"]["game_mode"] != "/Script/DreamSpace.DreamWhitePreviewGameMode"
                        or not state["play_report"]["camera_tag_matches"] or pawn or hud):
                    level.editor_request_end_play()
                    finish("白盒 PIE GameMode、相机或空场景检查失败")
                    return
                level.editor_request_end_play()
                state["play_verified"] = True
                state["play_end_requested_at"] = time.monotonic()
            elif state["play_verified"] and time.monotonic() - state["play_end_requested_at"] > 1.0:
                finish()
    except Exception as exc:
        finish(str(exc))


tick_handle = unreal.register_slate_post_tick_callback(tick)
