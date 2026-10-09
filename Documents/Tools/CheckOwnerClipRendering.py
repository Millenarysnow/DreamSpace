"""在独立 UE 编辑器进程中输出局部剔除对比图；只创建瞬时场景，不保存地图或资产。"""

import os
import time
import traceback
import unreal


# 必须通过完整 UnrealEditor.exe 的 -ExecutePythonScript 启动，且启用实际 D3D12 渲染。
# 不要在已有未保存地图的编辑器里直接执行：本脚本会创建空白地图，并在检查完成后退出进程。
# Python 返回后仍需等待 Slate 帧回调，保活开关避免编辑器在图像导出前提前关闭。
unreal.log('CAMERA_CLIP_RENDER starting GPU editor')
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actor_system = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor_system = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor_system.get_editor_world()
output_dir = os.path.join(unreal.Paths.project_saved_dir(), 'CameraClip', 'RenderChecks')
os.makedirs(output_dir, exist_ok=True)

# 使用参考姿势和与游戏相同的两槽材质，只验证材质像素，不依赖关卡、输入或角色控制器。
# Rotator 使用命名参数，避免 Python 构造参数顺序与 C++ 习惯不同而产生错误构图。
actor = actor_system.spawn_actor_from_class(
    unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0), unreal.Rotator(pitch=0, yaw=-90, roll=0))
mesh = actor.get_component_by_class(unreal.SkeletalMeshComponent)
mesh.set_skeletal_mesh_asset(unreal.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple'))
materials = []
for slot in range(2):
    parent = unreal.load_asset('/Game/DreamCamera/Materials/MI_QuinnOwnerClip_0' + str(slot + 1))
    instance = unreal.MaterialLibrary.create_dynamic_material_instance(world, parent)
    mesh.set_material(slot, instance)
    materials.append(instance)

light = actor_system.spawn_actor_from_class(
    unreal.DirectionalLight, unreal.Vector(0, 0, 400), unreal.Rotator(pitch=-35, yaw=-20, roll=0))
light.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(5.0)
fill = actor_system.spawn_actor_from_class(
    unreal.DirectionalLight, unreal.Vector(0, 0, 400), unreal.Rotator(pitch=-15, yaw=160, roll=0))
fill.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(2.0)

# 取镜头靠近肩背且能看到下肢的固定构图。BaseColor 隔离临时场景的照明/曝光差异，
# 黑色背景表示没有几何体；它不能证明实际室内光照、Lumen 或 TSR 运动观感也通过验收。
capture_actor = actor_system.spawn_actor_from_class(
    unreal.SceneCapture2D, unreal.Vector(-55, 25, 150), unreal.Rotator(pitch=-35, yaw=0, roll=0))
capture = capture_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
target = unreal.RenderingLibrary.create_render_target2d(
    world, 960, 720, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.set_editor_property('texture_target', target)
capture.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_BASE_COLOR)
capture.set_editor_property('fov_angle', 80.0)
capture.set_editor_property('capture_every_frame', False)
capture.set_editor_property('capture_on_movement', False)
capture.set_editor_property('always_persist_rendering_state', True)
capture.set_editor_property('post_process_blend_weight', 1.0)
unreal.SystemLibrary.execute_console_command(world, 'r.RayTracing.ForceAllRayTracingEffects 0')

# 每组依次指定：文件名、连续强度、外半径、视角标记、是否故意发布不匹配的镜头位置。
# 最后两组应与原图一致：同一构图也需要专用标记和正确视角身份才能进行局部剔除。
cases = [
    ('NearOriginal', 0.0, 85.0, 64, False),
    ('NearSoftClip', 1.0, 85.0, 64, False),
    ('NearHalfClip', 0.5, 62.0, 64, False),
    ('NearCaptureNoFlag', 1.0, 85.0, 0, False),
    ('NearDifferentView', 1.0, 85.0, 64, True),
]


def configure(case):
    name, amount, radius, flags, different_view = case
    settings = capture.get_editor_property('post_process_settings')
    settings.set_editor_property('override_user_flags', True)
    settings.set_editor_property('user_flags', flags)
    settings.set_editor_property('override_auto_exposure_method', True)
    settings.set_editor_property('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL)
    settings.set_editor_property('override_auto_exposure_bias', True)
    settings.set_editor_property('auto_exposure_bias', 1.0)
    capture.set_editor_property('post_process_settings', settings)
    local_camera = actor.get_actor_transform().inverse_transform_location(capture_actor.get_actor_location())
    if different_view:
        local_camera.x += 30.0
    forward = capture_actor.get_actor_forward_vector()
    unreal.log('CAMERA_CLIP_RENDER actual rotation={} forward={} local={}'.format(
        capture_actor.get_actor_rotation(), forward, local_camera))
    for instance in materials:
        instance.set_scalar_parameter_value('DreamOwnerClipAmount', amount)
        instance.set_scalar_parameter_value('DreamOwnerClipRadius', radius)
        instance.set_scalar_parameter_value('DreamOwnerClipFeather', 18.0)
        instance.set_vector_parameter_value(
            'DreamOwnerClipCameraLocal', unreal.LinearColor(local_camera.x, local_camera.y, local_camera.z, 0))
        instance.set_vector_parameter_value(
            'DreamOwnerClipCameraForward', unreal.LinearColor(forward.x, forward.y, forward.z, 0))
    unreal.log('CAMERA_CLIP_RENDER configuring ' + name)


case_index = 0
frame = 0
started = time.monotonic()


def tick(delta):
    global case_index, frame
    try:
        # 初始化后给实际渲染器、材质和纹理上传留出时间。导图前额外等待加载完成，
        # 每组再积累 90 帧，避免把刚设置参数但渲染线程尚未消费的前一组图像导出。
        if time.monotonic() - started < 20.0:
            capture.capture_scene()
            return
        if frame == 0:
            unreal.AutomationLibrary.finish_loading_before_screenshot()
            configure(cases[case_index])
        capture.capture_scene()
        frame += 1
        if frame >= 90:
            unreal.RenderingLibrary.export_render_target(
                world, target, output_dir, cases[case_index][0] + '.png')
            unreal.log('CAMERA_CLIP_RENDER saved ' + cases[case_index][0])
            case_index += 1
            frame = 0
            if case_index >= len(cases):
                unreal.unregister_slate_post_tick_callback(callback)
                unreal.log('CAMERA_CLIP_RENDER complete')
                unreal.EditorPythonScripting.set_keep_python_script_alive(False)
                unreal.SystemLibrary.quit_editor()
    except Exception:
        # 错误时也退出独立进程；日志保留 Python 堆栈，不把未完成的截图当作通过记录。
        unreal.log_error(traceback.format_exc())
        unreal.unregister_slate_post_tick_callback(callback)
        unreal.SystemLibrary.quit_editor()


callback = unreal.register_slate_post_tick_callback(tick)
