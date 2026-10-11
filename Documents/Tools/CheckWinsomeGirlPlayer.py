"""在独立 UE5.8 D3D12 编辑器中运行 TEST，检查新角色的生成、材质、移动和跳跃。

使用 UnrealEditor.exe -ExecutePythonScript -RenderOffscreen；检查期间不保存地图。
报告和主视口截图写入 Saved/WinsomeGirl。脚本结束后退出该独立编辑器。
"""
import json
import time
import traceback
from pathlib import Path
import unreal

OUTPUT = Path(unreal.Paths.project_saved_dir()).resolve() / 'WinsomeGirl'
OUTPUT.mkdir(parents=True, exist_ok=True)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
started = time.monotonic()
state = {'phase': 'warmup', 'at': started, 'finished': False}
report = {'map': '/Game/0_/Maps/TEST', 'material_compile_errors': {}}
callback = None
unreal.EditorPythonScripting.set_keep_python_script_alive(True)


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def phase(name):
    state['phase'], state['at'] = name, time.monotonic()
    unreal.log('WINSOME_CHECK_PHASE ' + name)


def finish(error=None):
    if state['finished']:
        return
    state['finished'] = True
    report['error'] = error
    (OUTPUT / 'play-verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    if level.is_in_play_in_editor():
        level.editor_request_end_play()
    if callback is not None:
        unreal.unregister_slate_post_tick_callback(callback)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    (unreal.log_error if error else unreal.log)('WINSOME_CHECK_COMPLETE ' + str(error or OUTPUT))
    unreal.SystemLibrary.quit_editor()


def screenshot(name):
    # 固定 16:9 输出，独立于该机器编辑器面板留下的狭长视口尺寸。
    unreal.SystemLibrary.execute_console_command(state['world'], 'HighResShot 1280x720 filename="%s"' % (OUTPUT / name).as_posix())


def sample():
    pawn = state['pawn']
    animation = state['mesh'].get_anim_instance()
    left = state['mesh'].get_socket_transform('foot_l', unreal.RelativeTransformSpace.RTS_COMPONENT).translation
    right = state['mesh'].get_socket_transform('foot_r', unreal.RelativeTransformSpace.RTS_COMPONENT).translation
    return {
        'location': [pawn.get_actor_location().x, pawn.get_actor_location().y, pawn.get_actor_location().z],
        'velocity': str(pawn.get_velocity()),
        'falling': pawn.get_editor_property('character_movement').is_falling(),
        'animation_speed': animation.get_editor_property('speed'),
        'animation_in_air': animation.get_editor_property('IsInAir?'),
        'left_foot': [left.x, left.y, left.z],
        'right_foot': [right.x, right.y, right.z],
    }


def tick(delta):
    if state['finished']:
        return
    try:
        now = time.monotonic()
        elapsed = now - state['at']
        require(now - started < 240.0, '角色检查超过 240 秒')
        current = state['phase']
        if current == 'warmup' and elapsed > 8.0:
            # finish_loading 会泵送 Slate；先改变阶段，避免回调重入而重复启动 PIE。
            phase('loading')
            unreal.AutomationLibrary.finish_loading_before_screenshot()
            level.editor_request_begin_play()
            phase('spawn')
        elif current == 'spawn' and elapsed > 8.0:
            world = require(editor.get_game_world(), 'PIE 未启动')
            pawn = require(unreal.GameplayStatics.get_player_pawn(world, 0), 'TEST 未生成玩家')
            require(isinstance(pawn, unreal.DreamCharacter), '玩家未保留 DreamCharacter 玩法')
            mesh = pawn.get_editor_property('mesh')
            require(mesh.get_editor_property('skeletal_mesh_asset').get_path_name().startswith('/Game/DreamCharacters/WinsomeGirl/'), '仍在使用旧模型')
            animation = require(mesh.get_anim_instance(), '动画实例未创建')
            require('ThirdPerson_AnimBP' in animation.get_class().get_path_name(), '配套动画未生效')
            materials = [mesh.get_material(slot) for slot in range(mesh.get_num_materials())]
            require(len(materials) == 7 and all(isinstance(m, unreal.MaterialInstanceDynamic) for m in materials), '七个材质槽未接入角色独立 MID')
            state.update(world=world, pawn=pawn, mesh=mesh,
                         controller=unreal.GameplayStatics.get_player_controller(world, 0))
            report.update(pawn_class=pawn.get_class().get_path_name(),
                          mesh=mesh.get_editor_property('skeletal_mesh_asset').get_path_name(),
                          animation_class=animation.get_class().get_path_name(),
                          material_count=len(materials),
                          original_location=str(pawn.get_actor_location()))
            report['idle'] = sample()
            report['feet_world_z'] = [mesh.get_socket_location(bone).z for bone in ('foot_l', 'foot_r')]
            report['capsule_bottom_z'] = pawn.get_actor_location().z - pawn.get_editor_property('capsule_component').get_scaled_capsule_half_height()
            screenshot('Exploration_TEST.png')
            phase('idle_screenshot')
        elif current == 'idle_screenshot' and elapsed > 2.0:
            require((OUTPUT / 'Exploration_TEST.png').is_file(), '探索主视口截图未保存')
            state['controller'].set_control_rotation(unreal.Rotator(pitch=-12, yaw=165, roll=0))
            phase('portrait')
        elif current == 'portrait' and elapsed > 2.0:
            screenshot('Character_TEST.png')
            phase('portrait_screenshot')
        elif current == 'portrait_screenshot' and elapsed > 2.0:
            require((OUTPUT / 'Character_TEST.png').is_file(), '正面主视口截图未保存')
            state['controller'].set_control_rotation(unreal.Rotator(pitch=0, yaw=0, roll=0))
            state['move_origin'] = state['pawn'].get_actor_location()
            phase('move')
        elif current == 'move':
            state['pawn'].do_move(0.0, 1.0)
            if elapsed > 0.4:
                report['moving'] = sample()
                require(report['moving']['animation_speed'] > 20.0, '移动未驱动跑步动画')
                require(state['pawn'].get_actor_location().distance(state['move_origin']) > 10.0, '角色未正常移动')
                require(report['moving']['left_foot'] != report['idle']['left_foot'], '移动时骨骼姿势未变化')
                screenshot('Running_TEST.png')
                phase('stop')
        elif current == 'stop' and elapsed > 1.5:
            state['jump_origin'] = state['pawn'].get_actor_location().z
            state['pawn'].do_jump_start()
            phase('jump')
        elif current == 'jump' and elapsed > 0.25:
            report['jumping'] = sample()
            require(report['jumping']['falling'] and report['jumping']['animation_in_air'], '跳跃未驱动空中动画')
            require(state['pawn'].get_actor_location().z > state['jump_origin'] + 20, '角色未正常起跳')
            screenshot('Jumping_TEST.png')
            state['pawn'].do_jump_end()
            phase('land')
        elif current == 'land' and elapsed > 2.0:
            report['landed'] = sample()
            require(not report['landed']['falling'] and not report['landed']['animation_in_air'], '落地后动画未恢复')
            require((OUTPUT / 'Running_TEST.png').is_file() and (OUTPUT / 'Jumping_TEST.png').is_file(), '运动截图未保存')
            finish()
    except Exception:
        finish(traceback.format_exc())


try:
    for filename in ('Exploration_TEST.png', 'Character_TEST.png', 'Running_TEST.png', 'Jumping_TEST.png'):
        (OUTPUT / filename).unlink(missing_ok=True)
    for name in ('M_BaseMat', 'M_Skin', 'M_Face', 'M_Hair', 'M_TRN_Shake'):
        material = require(unreal.load_asset('/Game/DreamCharacters/WinsomeGirl/Materials/' + name + 'OwnerClip'), '缺少角色材质 ' + name)
        errors = list(unreal.MaterialEditingLibrary.recompile_material(material))
        report['material_compile_errors'][name] = errors
        require(not errors, 'GPU 材质编译失败: ' + name + ' ' + str(errors))
    require(level.load_level(report['map']), '无法加载 TEST')
    # UE 5.8 的 True 会移除本子系统的 override；先建立它，避免缺失 override 的 ensure。
    level.editor_set_viewport_realtime(False)
    level.editor_set_viewport_realtime(True)
    level.editor_set_game_view(True)
    callback = unreal.register_slate_post_tick_callback(tick)
except Exception:
    finish(traceback.format_exc())
