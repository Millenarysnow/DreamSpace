# -*- coding: utf-8 -*-
"""通过真实 UE 编辑器视口验收白色铅笔描边，并采集可复核的动画帧。

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
SMALL_SCREENSHOT = OUTPUT_DIR / "WhitePencilPreview_1280x720.png"
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
for name in ("SilhouetteWidth", "StructureWidth", "StructureStrength", "NormalThreshold",
             "DepthThreshold", "ShadingStrength", "StrokeVariation"):
    parameter_values[name] = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(outline_instance, name)

# 记录实际资源参数，定帧通过临时动态实例实现，不把验收时间写回 .uasset。
pencil_instance = unreal.EditorAssetLibrary.load_asset(MATERIAL_ROOT + "MI_WhitePencil")
if not pencil_instance:
    raise RuntimeError("缺少可调铅笔实例 MI_WhitePencil，请先更新材质")
pencil_values = {}
for name in ("WobbleAmplitude", "WobbleSpeed", "StrokeScale", "GraphiteSoftness",
             "PressureVariation", "GraphiteGrain", "SmoothingStrength", "AnimationTime"):
    pencil_values[name] = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(pencil_instance, name)
if pencil_values["AnimationTime"] >= 0.0 or pencil_values["WobbleSpeed"] <= 0.0:
    raise RuntimeError("验收需要实时铅笔动画：AnimationTime 应为 -1，WobbleSpeed 应大于 0")

# 先冻结几个 1080p 时间点，并重复完整周期的起点；循环闭合可排除随机闪烁。
# 随后采集一个 960×540 的完整循环，方便把真实渲染帧组合成动图供用户验收。
period = 2.0 / pencil_values["WobbleSpeed"]
CAPTURES = [
    # 第一张只用于触发目标分辨率的 Nanite/渲染历史预热，不参与循环首尾比较。
    (1920, 1080, OUTPUT_DIR / "WhitePencilPreview_Warmup.png", 0.0),
    (1920, 1080, SCREENSHOT, 0.0),
    (1920, 1080, OUTPUT_DIR / "WhitePencilPreview_T1.png", period * 0.25),
    (1920, 1080, OUTPUT_DIR / "WhitePencilPreview_T2.png", period * 0.5),
    (1920, 1080, OUTPUT_DIR / "WhitePencilPreview_Loop.png", period),
    (1280, 720, SMALL_SCREENSHOT, 0.0),
]
ANIMATION_FRAMES = 24
LIVE_CAPTURES = [OUTPUT_DIR / "WhitePencilPreview_LiveA.png", OUTPUT_DIR / "WhitePencilPreview_LiveB.png"]
CAPTURES.extend((960, 540, OUTPUT_DIR / ("WhitePencilFrame_%02d.png" % index),
                 period * index / ANIMATION_FRAMES) for index in range(ANIMATION_FRAMES))

# UE 5.8 的 True 仅移除“关闭实时”覆盖项，不能保证用户保存的基础状态已开启。
# 定帧截图主动请求渲染，真实时间驱动另在 PIE 中验收，避免把静态编辑器当成运行时。
level.editor_set_viewport_realtime(False)
level.editor_set_viewport_realtime(True)
level.editor_set_game_view(True)
level.pilot_level_actor(camera)
level.set_exact_camera_view(True)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "r.ScreenPercentage 100")
unreal.SystemLibrary.execute_console_command(world, "r.HighResScreenshotDelay 16")
unreal.SystemLibrary.execute_console_command(world, "r.RenderTimeFrozen 0")
unreal.SystemLibrary.execute_console_command(world, "r.Test.OverrideTimeMaterialExpressions -1")
post = next((actor for actor in actors if isinstance(actor, unreal.PostProcessVolume)
             and actor.get_actor_label() == "WhitePreview_PostProcess"), None)
if not post:
    raise RuntimeError("预览关卡缺少专用后处理体积")
settings = post.get_editor_property("settings")
original_settings = post.get_editor_property("settings")
weighted = settings.get_editor_property("weighted_blendables")
blendables = list(weighted.get_editor_property("array"))
if not any(item.get_editor_property("object") == pencil_instance for item in blendables):
    raise RuntimeError("预览关卡未引用铅笔材质实例")
pencil_dynamic = unreal.MaterialLibrary.create_dynamic_material_instance(world, pencil_instance)
if not pencil_dynamic:
    raise RuntimeError("无法创建铅笔验收的临时动态实例")
for item in blendables:
    if item.get_editor_property("object") == pencil_instance:
        item.set_editor_property("object", pencil_dynamic)
weighted.set_editor_property("array", blendables)
settings.set_editor_property("weighted_blendables", weighted)
post.set_editor_property("settings", settings)
for _, _, path, _ in CAPTURES:
    if path.exists():
        # 清除上一次验收输出，确保报告不会把旧图片误当成新结果。
        path.unlink()
for path in LIVE_CAPTURES:
    if path.exists():
        path.unlink()

# -ExecutePythonScript 默认在脚本返回后关闭编辑器；保持其存活到截图任务结束。
# 通过每帧回调等待画面稳定，不阻塞编辑器主线程，也不使用 time.sleep。
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
started = time.monotonic()
state = {"task": None, "finished": False}
state["capture_index"] = 0
state["play_requested"] = False
state["play_verified"] = False
state["play_requested_at"] = 0.0
state["play_end_requested_at"] = 0.0
state["play_report"] = {}
state["live_capture_index"] = 0
state["live_requested_at"] = 0.0
state["live_capture_times"] = []


def finish(error=None):
    """无论成功或超时都写入明确结果，再退出此验收实例。"""
    if state["finished"]:
        return
    state["finished"] = True
    REPORT.write_text(json.dumps({
        "map": MAP_PATH,
        "screenshot": str(SCREENSHOT),
        "screenshot_exists": SCREENSHOT.exists(),
        "captures": [{"width": width, "height": height, "path": str(path),
                      "animation_time": seconds, "exists": path.exists()}
                     for width, height, path, seconds in CAPTURES]
                    + [{"width": 1920, "height": 1080, "path": str(path),
                        "animation_time": -1.0, "exists": path.exists(), "pie": True} for path in LIVE_CAPTURES],
        "animation_period_seconds": period,
        "compile_errors": compile_errors,
        "material_node_counts": node_counts,
        "outline_parameters": parameter_values,
        "pencil_parameters": pencil_values,
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
    """异步定帧截图，恢复实时动画后运行 PIE；等待期间保持编辑器正常渲染。"""
    elapsed = time.monotonic() - started
    try:
        # 所有阶段都检查超时，不能把超时放进后续 elif 而被已完成的截图分支挡住。
        if elapsed > 240.0:
            if level.is_in_play_in_editor():
                level.editor_request_end_play()
            finish("验收任务超过 240 秒，请手动查看预览关卡")
            return
        if state["task"] is None and elapsed > 10.0:
            width, height, path, seconds = CAPTURES[state["capture_index"]]
            pencil_dynamic.set_scalar_parameter_value("AnimationTime", seconds)
            state["task"] = unreal.AutomationLibrary.take_high_res_screenshot(
                width, height, str(path), camera=camera, delay=0.4, force_game_view=True)
            if state["task"] is None:
                finish("编辑器无法创建截图任务")
        elif state["task"] is not None and state["task"].is_task_done():
            _, _, path, _ = CAPTURES[state["capture_index"]]
            if not path.exists():
                finish("截图任务已结束但文件未写入")
                return
            if state["capture_index"] + 1 < len(CAPTURES):
                state["capture_index"] += 1
                state["task"] = None
                return
            if not state["play_requested"]:
                pencil_dynamic.set_scalar_parameter_value("AnimationTime", -1.0)
                # PIE 使用保存的实时材质实例，定帧动态实例不参与运行时验收。
                post.set_editor_property("settings", original_settings)
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
                state["play_verified"] = True
            elif state["play_verified"] and state["live_capture_index"] < len(LIVE_CAPTURES):
                # GameViewport 的截图会实际使用游戏世界时间，且经过已保存的材质实例。
                # 两次请求至少相隔 0.6 秒，足以检查默认幅度的可见线条运动。
                index = state["live_capture_index"]
                path = LIVE_CAPTURES[index]
                if state["live_requested_at"] == 0.0:
                    game_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
                    screenshot_command = 'HighResShot 1920x1080 filename="%s"' % path.as_posix()
                    unreal.SystemLibrary.execute_console_command(game_world, screenshot_command)
                    state["live_requested_at"] = time.monotonic()
                    state["live_capture_times"].append(unreal.GameplayStatics.get_real_time_seconds(game_world))
                elif path.exists() and time.monotonic() - state["live_requested_at"] > 0.6:
                    state["live_capture_index"] += 1
                    state["live_requested_at"] = 0.0
            elif state["play_verified"] and state["play_end_requested_at"] == 0.0:
                state["play_report"]["realtime_capture_seconds"] = state["live_capture_times"]
                level.editor_request_end_play()
                state["play_end_requested_at"] = time.monotonic()
            elif state["play_verified"] and time.monotonic() - state["play_end_requested_at"] > 1.0:
                finish()
    except Exception as exc:
        finish(str(exc))


tick_handle = unreal.register_slate_post_tick_callback(tick)
