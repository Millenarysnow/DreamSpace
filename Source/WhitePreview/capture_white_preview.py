# -*- coding: utf-8 -*-
"""通过真实 UE 编辑器视口截图验收白色描边，不用替代性的离线绘制。

使用 UnrealEditor.exe -ExecutePythonScript 启动此脚本，不能加 -nullrhi。
脚本仅临时设置当前视口，完成后自动退出专门启动的编辑器，不保存关卡改动。
截图与验收报告写入 Saved/WhitePreview；具体命令见配套说明文档。
"""

import json
import time
from pathlib import Path

import unreal


MAP_PATH = "/Game/DreamPresentation/WhitePreview/Maps/L_WhiteOutlinePreview"
MATERIAL_ROOT = "/Game/DreamPresentation/WhitePreview/Materials/"
OUTPUT_DIR = Path(unreal.Paths.project_saved_dir()).resolve() / "WhitePreview"
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
SCREENSHOT = OUTPUT_DIR / "WhiteOutlinePreview.png"
REPORT = OUTPUT_DIR / "capture_report.json"
if REPORT.exists():
    REPORT.unlink()

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(MAP_PATH):
    raise RuntimeError("无法加载白色预览关卡")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
camera = next((actor for actor in actors if isinstance(actor, unreal.CameraActor)
               and actor.actor_has_tag("DreamWhitePreviewCamera")), None)
if not camera:
    raise RuntimeError("预览关卡中缺少带标签的固定相机")

# 对实际加载的资源重新编译。最终截图还会等待加载和 Shader 完成，
# 从日志及像素一起验收，防止“资源能保存，但显示的是默认材质”的假成功。
compile_errors = {}
node_counts = {}
parameter_values = {}
for name in ("M_WhiteArchitecture", "M_WhiteOutlinePost", "M_WhiteOutlineAA"):
    material = unreal.EditorAssetLibrary.load_asset(MATERIAL_ROOT + name)
    if not material:
        raise RuntimeError("预览材质缺失：" + name)
    errors = list(unreal.MaterialEditingLibrary.recompile_material(material))
    compile_errors[name] = errors
    node_counts[name] = unreal.MaterialEditingLibrary.get_num_material_expressions(material)
    if errors:
        raise RuntimeError("材质编译错误：" + "\n".join(errors))
outline_instance = unreal.EditorAssetLibrary.load_asset(MATERIAL_ROOT + "MI_WhiteOutline")
for name in ("SilhouetteWidth", "StructureWidth", "StructureStrength", "NormalThreshold", "DepthThreshold", "ShadingStrength"):
    parameter_values[name] = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(outline_instance, name)

level.editor_set_viewport_realtime(True)
level.editor_set_game_view(True)
level.pilot_level_actor(camera)
level.set_exact_camera_view(True)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "r.ScreenPercentage 100")
if SCREENSHOT.exists():
    # 清除上一次验收输出，确保本次报告不会把旧图片误当成新结果。
    SCREENSHOT.unlink()

# -ExecutePythonScript 默认在脚本返回后关闭编辑器；保持其存活到截图任务结束。
# 通过每帧回调等待画面稳定，不阻塞编辑器主线程，也不使用 time.sleep。
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
started = time.monotonic()
state = {"task": None, "finished": False}
state["play_requested"] = False
state["play_verified"] = False
state["play_requested_at"] = 0.0
state["play_end_requested_at"] = 0.0
state["play_report"] = {}


def finish(error=None):
    """无论成功或超时都写入明确结果，再退出此验收实例。"""
    if state["finished"]:
        return
    state["finished"] = True
    REPORT.write_text(json.dumps({
        "map": MAP_PATH,
        "screenshot": str(SCREENSHOT),
        "screenshot_exists": SCREENSHOT.exists(),
        "compile_errors": compile_errors,
        "material_node_counts": node_counts,
        "outline_parameters": parameter_values,
        "play_preview": state["play_report"],
        "error": error,
    }, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.unregister_slate_post_tick_callback(tick_handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    if error:
        unreal.log_error("WHITE_CAPTURE_FAILED " + error)
    else:
        unreal.log("WHITE_CAPTURE_OK " + str(SCREENSHOT))


def tick(delta_seconds):
    """先积累正常渲染帧，再提交无编辑器图标的 1920×1080 截图。"""
    elapsed = time.monotonic() - started
    try:
        # 所有阶段都检查超时，不能把超时放进后续 elif 而被已完成的截图分支挡住。
        if elapsed > 180.0:
            if level.is_in_play_in_editor():
                level.editor_request_end_play()
            finish("验收任务超过 180 秒，请手动查看预览关卡")
            return
        if state["task"] is None and elapsed > 10.0:
            state["task"] = unreal.AutomationLibrary.take_high_res_screenshot(
                1920, 1080, str(SCREENSHOT), camera=camera, delay=1.0, force_game_view=True)
            if state["task"] is None:
                finish("编辑器无法创建截图任务")
        elif state["task"] is not None and state["task"].is_task_done():
            if not SCREENSHOT.exists():
                finish("截图任务已结束但文件未写入")
                return
            if not state["play_requested"]:
                # 截图后实际启动 PIE，确认用户点击“运行”也会使用预览相机，
                # 且不会生成原项目角色和调试 HUD。此检查不修改或保存源关卡。
                level.editor_request_begin_play()
                state["play_requested"] = True
                state["play_requested_at"] = time.monotonic()
            elif not state["play_verified"] and time.monotonic() - state["play_requested_at"] > 3.0:
                if not level.is_in_play_in_editor():
                    finish("无法启动 PIE 预览")
                    return
                game_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
                controller = unreal.GameplayStatics.get_player_controller(game_world, 0)
                game_mode = unreal.GameplayStatics.get_game_mode(game_world)
                if not controller or not game_mode:
                    finish("PIE 缺少预览控制器或 GameMode")
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
                expected_mode = "/Script/DreamSpace.DreamWhitePreviewGameMode"
                if (state["play_report"]["game_mode"] != expected_mode
                        or not state["play_report"]["camera_tag_matches"] or pawn or hud):
                    level.editor_request_end_play()
                    finish("PIE 预览规则或相机不符合预期，请查看 play_preview 报告")
                    return
                level.editor_request_end_play()
                state["play_verified"] = True
                state["play_end_requested_at"] = time.monotonic()
            elif state["play_verified"] and time.monotonic() - state["play_end_requested_at"] > 1.0:
                finish()
    except Exception as exc:
        finish(str(exc))


tick_handle = unreal.register_slate_post_tick_callback(tick)
