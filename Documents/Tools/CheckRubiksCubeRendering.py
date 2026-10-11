"""在独立 D3D12 UE 编辑器中导出二阶魔方的实际渲染预览。

使用 UnrealEditor.exe -ExecutePythonScript 启动；只创建瞬时空场景，不保存或修改地图。
输出还原状态、完整转动和打乱后的 PNG，用于验证贴纸颜色与实际角块层几何。
目标关卡的玩家操作手感仍需要 PIE 验收。
"""

import os
import time
import traceback
import unreal


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
output_dir = os.path.join(unreal.Paths.project_saved_dir(), "RubiksCube", "RenderChecks")
os.makedirs(output_dir, exist_ok=True)

cube_class = unreal.load_class(None, "/Game/DreamInteraction/BP/BP_DreamRubiksCube.BP_DreamRubiksCube_C")
if not cube_class:
    raise RuntimeError("请先执行 GenerateRubiksCubeExample.py 创建示例蓝图。")
actor = actors.spawn_actor_from_class(cube_class, unreal.Vector(0, 0, 0))
component = actor.get_component_by_class(unreal.DreamRubiksCubeComponent)
if not component.rebuild_cube():
    raise RuntimeError("魔方组件重建失败。")
component.activate(True)

# 捕获 +X 红、+Y 绿、+Z 白三面，使用 BaseColor 隔离临时场景照明差异。
# 此图只证明模型和颜色实际提交给 UE 渲染器，不等同于目标关卡的 Lumen/曝光验收。
capture_actor = actors.spawn_actor_from_class(
    unreal.SceneCapture2D, unreal.Vector(220, 260, 210), unreal.Rotator()
)
capture_actor.set_actor_rotation(
    unreal.MathLibrary.find_look_at_rotation(capture_actor.get_actor_location(), unreal.Vector()), False
)
capture = capture_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
target = unreal.RenderingLibrary.create_render_target2d(
    world, 900, 760, unreal.TextureRenderTargetFormat.RTF_RGBA8
)
capture.set_editor_property("texture_target", target)
capture.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_BASE_COLOR)
capture.set_editor_property("fov_angle", 35.0)
capture.set_editor_property("capture_every_frame", False)
capture.set_editor_property("capture_on_movement", False)
capture.set_editor_property("always_persist_rendering_state", True)

cases = ["Solved", "TurnCompleted", "Shuffled"]
case_index = 0
frame = 0
started = time.monotonic()


def configure_case(index):
    if index == 0:
        assert component.reset_cube()
    elif index == 1:
        # 使用组件正式的瞬时入口提交完整转动；动画中间帧由 C++ 自动化测试覆盖。
        assert component.rotate_layer(unreal.DreamRubiksCubeAxis.Z, 1, 1, False)
    else:
        assert component.shuffle_cube(20, 1337)


def tick(delta):
    global case_index, frame
    try:
        if time.monotonic() - started < 15.0:
            capture.capture_scene()
            return
        if frame == 0:
            unreal.AutomationLibrary.finish_loading_before_screenshot()
            configure_case(case_index)
        capture.capture_scene()
        frame += 1
        if frame >= 40:
            unreal.RenderingLibrary.export_render_target(
                world, target, output_dir, cases[case_index] + ".png"
            )
            unreal.log("RUBIKS_RENDER_SAVED " + cases[case_index])
            case_index += 1
            frame = 0
            if case_index >= len(cases):
                unreal.unregister_slate_post_tick_callback(callback)
                unreal.log("RUBIKS_RENDER_COMPLETE")
                unreal.EditorPythonScripting.set_keep_python_script_alive(False)
                unreal.SystemLibrary.quit_editor()
    except Exception:
        unreal.log_error(traceback.format_exc())
        unreal.unregister_slate_post_tick_callback(callback)
        unreal.SystemLibrary.quit_editor()


callback = unreal.register_slate_post_tick_callback(tick)
