# -*- coding: utf-8 -*-
"""用真实 UE 渲染器验收白色建筑的手绘阴影排线。

使用 UnrealEditor.exe -ExecutePythonScript 启动，不能加 -nullrhi。
截图包括完整效果、关闭排线、仅排线、明暗依据、原始光照及两个分辨率，最后运行 PIE。
调试参数只写入临时动态实例；本脚本不会保存资源或关卡。
"""

import json
import time
from pathlib import Path

import unreal


MAP_PATH = "/Game/DreamPresentation/WhitePreview/Maps/L_WhiteOutlinePreview"
MATERIAL_ROOT = "/Game/DreamPresentation/WhitePreview/Materials/"
OUTPUT_DIR = Path(unreal.Paths.project_saved_dir()).resolve() / "WhitePreview"
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
REPORT = OUTPUT_DIR / "hatching_capture_report.json"
SCREENSHOT = OUTPUT_DIR / "WhiteHatchingPreview.png"
PIE_SCREENSHOT = OUTPUT_DIR / "WhiteHatchingPreview_PIE.png"

# 第一张预热目标分辨率的渲染历史和 Nanite 细节；正式截图再检查重复帧的稳定性。
# 后面的调试截图可验证明暗排线确实参与了表面渲染，避免仅通过材质编译判定完成。
CAPTURES = [
    (1920, 1080, "WhiteHatchingPreview_Warmup.png", {}),
    (1920, 1080, "WhiteHatchingPreview_Warmup2.png", {}),
    (1920, 1080, SCREENSHOT.name, {}),
    (1920, 1080, "WhiteHatchingPreview_Repeat.png", {}),
    (1280, 720, "WhiteHatchingPreview_1280x720.png", {}),
    (1920, 1080, "WhiteHatchingPreview_NoHatch.png", {"HatchStrength": 0.0}),
    (1920, 1080, "WhiteHatchingPreview_Tone.png", {"HatchDebugView": 1.0}),
    (1920, 1080, "WhiteHatchingPreview_NormalTone.png", {"HatchDebugView": 1.0, "ShadowInfluence": 0.0}),
    (1920, 1080, "WhiteHatchingPreview_Strokes.png", {"HatchDebugView": 2.0}),
    (1920, 1080, "WhiteHatchingPreview_Lighting.png", {"HatchDebugView": 3.0}),
]
# PIE 与编辑器截图走不同的渲染路径，轮廓抗锯齿和 Nanite 细节可能不同。
# 运行时也采集同环境的调试对照，避免跨渲染路径误判排线缺失。
PIE_CAPTURES = [
    ("WhiteHatchingPreview_PIE_Warmup.png", {}),
    (PIE_SCREENSHOT.name, {}),
    ("WhiteHatchingPreview_PIE_Repeat.png", {}),
    ("WhiteHatchingPreview_PIE_NoHatch.png", {"HatchStrength": 0.0}),
    ("WhiteHatchingPreview_PIE_Strokes.png", {"HatchDebugView": 2.0}),
]
# 每次截图均重置所有可能的调试覆盖项，防止上一张的明暗权重沿用到下一张。
capture_parameters = set().union(*(overrides for _, _, _, overrides in CAPTURES))
for path in ([REPORT] + [OUTPUT_DIR / name for _, _, name, _ in CAPTURES]
             + [OUTPUT_DIR / name for name, _ in PIE_CAPTURES]):
    if path.exists():
        path.unlink()

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(MAP_PATH):
    raise RuntimeError("无法加载白色排线预览关卡")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
camera = next((actor for actor in actors if isinstance(actor, unreal.CameraActor)
               and actor.actor_has_tag("DreamWhitePreviewCamera")), None)
post = next((actor for actor in actors if isinstance(actor, unreal.PostProcessVolume)
             and actor.get_actor_label() == "WhitePreview_PostProcess"), None)
if not camera or not post:
    raise RuntimeError("预览关卡缺少固定相机或专用后处理体积")

compile_errors = {}
node_counts = {}
time_node_counts = {}
for name in ("M_WhiteArchitecture", "M_WhiteOutlinePost", "M_WhiteOutlineAA"):
    material = unreal.EditorAssetLibrary.load_asset(MATERIAL_ROOT + name)
    if not material:
        raise RuntimeError("预览材质缺失：" + name)
    errors = list(unreal.MaterialEditingLibrary.recompile_material(material))
    compile_errors[name] = errors
    nodes = list(unreal.MaterialEditingLibrary.get_material_expressions(material))
    node_counts[name] = len(nodes)
    time_node_counts[name] = sum(isinstance(node, unreal.MaterialExpressionTime) for node in nodes)
    if errors or time_node_counts[name]:
        raise RuntimeError("材质编译失败或仍包含时间动画节点：" + name + str(errors))

outline_instance = unreal.EditorAssetLibrary.load_asset(MATERIAL_ROOT + "MI_WhiteOutline")
smoothing_instance = unreal.EditorAssetLibrary.load_asset(MATERIAL_ROOT + "MI_WhitePencil")
if not outline_instance or not smoothing_instance:
    raise RuntimeError("缺少描边或线稿平滑实例")
parameter_values = {}
for name in ("SilhouetteWidth", "StructureWidth", "StructureStrength", "NormalThreshold", "DepthThreshold",
             "ShadingStrength", "StrokeVariation", "HatchStrength", "HatchSpacing", "HatchWidth", "HatchAngle",
             "HatchIrregularity", "HatchStrokeLength", "ShadowInfluence", "ShadowReference",
             "HatchToneBias", "HatchToneContrast", "HatchDebugView"):
    parameter_values[name] = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(outline_instance, name)
if parameter_values["HatchStrength"] <= 0.0 or parameter_values["HatchDebugView"] != 0.0:
    raise RuntimeError("预览应开启排线并关闭调试视图")

settings = post.get_editor_property("settings")
weighted = settings.get_editor_property("weighted_blendables")
blendables = list(weighted.get_editor_property("array"))
active_paths = [item.get_editor_property("object").get_path_name() for item in blendables
                if item.get_editor_property("object") and item.get_editor_property("weight") > 0.0]
expected_paths = [outline_instance.get_path_name(), smoothing_instance.get_path_name()]
if active_paths != expected_paths:
    raise RuntimeError("预览后处理引用与预期不一致：" + str(active_paths))

# 保存值和资源对象，而非反射结构体副本。UE 的嵌套结构可能共享底层引用，
# 直接缓存 settings 再修改其 array，无法保证恢复时仍是原始材质实例。
saved_blendables = [(item.get_editor_property("weight"), item.get_editor_property("object"))
                   for item in blendables]


def set_blendables(volume, replacements):
    """用全新的反射结构体替换引用，避免动态调试实例泄漏到 PIE 世界。"""
    entries = []
    for weight, material in replacements:
        entry = unreal.WeightedBlendable()
        entry.set_editor_property("weight", weight)
        entry.set_editor_property("object", material)
        entries.append(entry)
    replacement = unreal.WeightedBlendables()
    replacement.set_editor_property("array", entries)
    volume_settings = volume.get_editor_property("settings")
    volume_settings.set_editor_property("weighted_blendables", replacement)
    volume.set_editor_property("settings", volume_settings)


# 只把描边实例临时替换为动态实例，验收中随时切换排线和明暗调试输出。
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
original_aa_method = unreal.SystemLibrary.get_console_variable_int_value("r.AntiAliasingMethod")
outline_dynamic = unreal.MaterialLibrary.create_dynamic_material_instance(world, outline_instance)
if not outline_dynamic:
    raise RuntimeError("无法创建排线验收的临时动态实例")
set_blendables(post, [(weight, outline_dynamic if material == outline_instance else material)
                      for weight, material in saved_blendables])


def restore_saved_materials():
    """同时重置临时参数和实际引用；发生截图异常时也不留下调试视图。"""
    for parameter in capture_parameters:
        outline_dynamic.set_scalar_parameter_value(parameter, parameter_values[parameter])
    set_blendables(post, saved_blendables)

# UE 5.8 的 True 只是移除“关闭实时”覆盖项；定帧截图会主动请求渲染。
level.editor_set_viewport_realtime(False)
level.editor_set_viewport_realtime(True)
level.editor_set_game_view(True)
level.pilot_level_actor(camera)
level.set_exact_camera_view(True)
unreal.SystemLibrary.execute_console_command(world, "r.ScreenPercentage 100")
# 专用展示使用 FXAA，避免 TSR 的逐帧抖动把静止深度线变成变化的边缘。
unreal.SystemLibrary.execute_console_command(world, "r.AntiAliasingMethod 1")
unreal.SystemLibrary.execute_console_command(world, "r.HighResScreenshotDelay 16")

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
started = time.monotonic()
state = {"task": None, "finished": False, "capture_index": 0,
         "play_requested_at": 0.0, "play_verified": False, "play_report": {},
         "pie_requested_at": 0.0, "pie_capture_index": 0,
         "runtime_post": None, "runtime_dynamic": None, "play_end_requested_at": 0.0}


def finish(error=None):
    """完成、超时或异常均记录明确结果，退出专门启动的验收编辑器实例。"""
    if state["finished"]:
        return
    restore_saved_materials()
    state["finished"] = True
    REPORT.write_text(json.dumps({
        "map": MAP_PATH, "screenshot": str(SCREENSHOT), "compile_errors": compile_errors,
        "material_node_counts": node_counts, "time_node_counts": time_node_counts,
        "active_blendables": active_paths, "outline_parameters": parameter_values,
        "captures": [{"width": width, "height": height, "path": str(OUTPUT_DIR / name),
                      "overrides": overrides, "exists": (OUTPUT_DIR / name).exists()}
                     for width, height, name, overrides in CAPTURES]
                    + [{"width": 1920, "height": 1080, "path": str(OUTPUT_DIR / name),
                        "overrides": overrides, "pie": True, "exists": (OUTPUT_DIR / name).exists()}
                       for name, overrides in PIE_CAPTURES],
        "play_preview": state["play_report"], "error": error,
    }, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.unregister_slate_post_tick_callback(tick_handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    if error:
        unreal.log_error("WHITE_HATCH_CAPTURE_FAILED " + error)
    else:
        unreal.log("WHITE_HATCH_CAPTURE_OK " + str(SCREENSHOT))


def tick(delta_seconds):
    """等待正常渲染帧，依次截图并验证 PIE；不阻塞主线程或保存调试参数。"""
    now = time.monotonic()
    try:
        if now - started > 180.0:
            if level.is_in_play_in_editor():
                level.editor_request_end_play()
            finish("排线验收超过 180 秒")
            return
        if now - started < 10.0:
            return
        if state["capture_index"] < len(CAPTURES):
            width, height, name, overrides = CAPTURES[state["capture_index"]]
            if state["task"] is None:
                for parameter in capture_parameters:
                    outline_dynamic.set_scalar_parameter_value(parameter, overrides.get(parameter, parameter_values[parameter]))
                state["task"] = unreal.AutomationLibrary.take_high_res_screenshot(
                    width, height, str(OUTPUT_DIR / name), camera=camera, delay=0.8, force_game_view=True)
                if state["task"] is None:
                    finish("无法创建排线截图任务")
            elif state["task"].is_task_done():
                if not (OUTPUT_DIR / name).exists():
                    finish("截图任务完成但文件未写入：" + name)
                    return
                state["capture_index"] += 1
                state["task"] = None
            return

        if state["play_requested_at"] == 0.0:
            # 恢复实际资源后再启动 PIE，确认运行画面使用已保存的完整效果。
            restore_saved_materials()
            # 编辑器截图临时用 FXAA；PIE 前先恢复原设置，才能实际验证
            # GameMode 的标签开关和 EndPlay 恢复，而非被截图命令掩盖。
            unreal.SystemLibrary.execute_console_command(world, "r.AntiAliasingMethod %d" % original_aa_method)
            level.editor_request_begin_play()
            state["play_requested_at"] = now
        elif not state["play_verified"] and now - state["play_requested_at"] > 6.0:
            if not level.is_in_play_in_editor():
                finish("无法启动 PIE")
                return
            game_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            controller = unreal.GameplayStatics.get_player_controller(game_world, 0)
            game_mode = unreal.GameplayStatics.get_game_mode(game_world)
            if not controller or not game_mode:
                finish("PIE 缺少控制器或 GameMode")
                return
            target = controller.get_view_target()
            pawn = unreal.GameplayStatics.get_player_pawn(game_world, 0)
            hud = controller.get_hud()
            state["play_report"] = {"game_mode": game_mode.get_class().get_path_name(),
                                    "view_target": target.get_name() if target else None,
                                    "camera_tag_matches": bool(target and target.actor_has_tag("DreamWhitePreviewCamera")),
                                    "pawn": pawn.get_name() if pawn else None, "hud": hud.get_name() if hud else None}
            # 只检查编辑器世界不足以发现动态实例复制进 PIE 的问题；读取运行世界
            # 的后处理引用及调试参数，确认它使用的是磁盘上保存的完整效果。
            runtime_posts = [actor for actor in unreal.GameplayStatics.get_all_actors_of_class(
                game_world, unreal.PostProcessVolume) if actor.get_name() == post.get_name()]
            if len(runtime_posts) != 1:
                raise RuntimeError("PIE 缺少预期的后处理体积")
            runtime_settings = runtime_posts[0].get_editor_property("settings")
            runtime_entries = runtime_settings.get_editor_property("weighted_blendables").get_editor_property("array")
            runtime_paths = [item.get_editor_property("object").get_path_name() for item in runtime_entries
                             if item.get_editor_property("object") and item.get_editor_property("weight") > 0.0]
            state["play_report"]["active_blendables"] = runtime_paths
            state["play_report"]["hatch_debug_view"] = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(
                outline_instance, "HatchDebugView")
            state["play_report"]["anti_aliasing_before_play"] = original_aa_method
            state["play_report"]["anti_aliasing_during_play"] = unreal.SystemLibrary.get_console_variable_int_value("r.AntiAliasingMethod")
            if state["play_report"]["anti_aliasing_during_play"] != 1:
                raise RuntimeError("运行时未启用稳定线稿的 FXAA，检查后处理体积标签")
            if runtime_paths != expected_paths or state["play_report"]["hatch_debug_view"] != 0.0:
                raise RuntimeError("PIE 残留临时排线调试状态")
            state["runtime_post"] = runtime_posts[0]
            state["runtime_dynamic"] = unreal.MaterialLibrary.create_dynamic_material_instance(game_world, outline_instance)
            if not state["runtime_dynamic"]:
                raise RuntimeError("无法创建运行时排线对照实例")
            if (state["play_report"]["game_mode"] != "/Script/DreamSpace.DreamWhitePreviewGameMode"
                    or not state["play_report"]["camera_tag_matches"] or pawn or hud):
                level.editor_request_end_play()
                finish("PIE 规则或相机不符合预期")
                return
            state["play_verified"] = True
        elif state["play_verified"] and state["play_end_requested_at"] == 0.0:
            if state["pie_capture_index"] >= len(PIE_CAPTURES):
                # 运行时的对照也必须明确恢复，即使只是即将关闭的临时 PIE 世界。
                set_blendables(state["runtime_post"], saved_blendables)
                level.editor_request_end_play()
                state["play_end_requested_at"] = now
                return
            name, overrides = PIE_CAPTURES[state["pie_capture_index"]]
            if state["pie_requested_at"] == 0.0:
                dynamic = state["runtime_dynamic"]
                for parameter in capture_parameters:
                    dynamic.set_scalar_parameter_value(parameter, overrides.get(parameter, parameter_values[parameter]))
                # 前三张正式效果使用已保存资源；之后才替换为临时调试实例。
                runtime_material = dynamic if overrides else outline_instance
                set_blendables(state["runtime_post"], [(weight, runtime_material if material == outline_instance else material)
                    for weight, material in saved_blendables])
                state["pie_requested_at"] = now
            elif now - state["pie_requested_at"] > 0.8 and not state.get("pie_command_sent", False):
                game_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
                unreal.SystemLibrary.execute_console_command(game_world,
                    'HighResShot 1920x1080 filename="%s"' % (OUTPUT_DIR / name).as_posix())
                state["pie_command_sent"] = True
            elif (OUTPUT_DIR / name).exists() and now - state["pie_requested_at"] > 1.2:
                state["pie_capture_index"] += 1
                state["pie_requested_at"] = 0.0
                state["pie_command_sent"] = False
        elif state["play_end_requested_at"] and now - state["play_end_requested_at"] > 1.0:
            state["play_report"]["anti_aliasing_after_play"] = unreal.SystemLibrary.get_console_variable_int_value("r.AntiAliasingMethod")
            if state["play_report"]["anti_aliasing_after_play"] != original_aa_method:
                finish("结束 PIE 后未恢复抗锯齿设置")
                return
            finish()
    except Exception as exc:
        finish(str(exc))


tick_handle = unreal.register_slate_post_tick_callback(tick)
