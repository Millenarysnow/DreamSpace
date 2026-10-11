"""迁移 TEST 所用 BP_DreamCharacter 的旧网格覆盖值，不修改关卡或角色其它组件。"""
import json
from pathlib import Path
import unreal

PATH = '/Game/DreamInteraction/BP/BP_DreamCharacter'
MESH = '/Game/DreamCharacters/WinsomeGirl/SK_DreamWinsomeGirl'
ANIMATION = '/Game/WinsomeGirl/Maps/ThirdPersonExampleMap/Mannequin/Animations/ThirdPerson_AnimBP'


def snapshot(character):
    mesh = character.get_editor_property('mesh')
    movement = character.get_editor_property('character_movement')
    capsule = character.get_editor_property('capsule_component')
    miniature = character.get_editor_property('scene_miniature')
    camera = character.get_editor_property('camera_boom')
    return {
        'mesh': mesh.get_editor_property('skeletal_mesh_asset').get_path_name(),
        'animation': mesh.get_editor_property('anim_class').get_path_name(),
        'mesh_location': str(mesh.get_editor_property('relative_location')),
        'mesh_rotation': str(mesh.get_editor_property('relative_rotation')),
        'mesh_scale': str(mesh.get_editor_property('relative_scale3d')),
        'overrides': [m.get_path_name() if m else None for m in mesh.get_editor_property('override_materials')],
        'capsule': [capsule.get_editor_property('capsule_radius'), capsule.get_editor_property('capsule_half_height')],
        'walk_speed': movement.get_editor_property('max_walk_speed'),
        'jump_velocity': movement.get_editor_property('jump_z_velocity'),
        'camera_location': str(camera.get_editor_property('relative_location')),
        'camera_arm': camera.get_editor_property('target_arm_length'),
        'miniature_location': str(miniature.get_editor_property('relative_location')),
        'miniature_rt': [miniature.get_editor_property('render_target_width'), miniature.get_editor_property('render_target_height')],
        'body_hidden': character.get_editor_property('body').get_editor_property('hidden_in_game'),
    }


def main():
    blueprint = unreal.load_asset(PATH)
    if not blueprint:
        raise RuntimeError('无法加载 ' + PATH)
    character = unreal.get_default_object(blueprint.generated_class())
    before = snapshot(character)
    mesh = character.get_editor_property('mesh')
    model = unreal.load_asset(MESH)
    animation = unreal.load_asset(ANIMATION)
    if not model or not animation:
        raise RuntimeError('角色资产尚未生成')
    mesh.set_editor_property('skeletal_mesh_asset', model)
    mesh.set_editor_property('anim_class', animation.generated_class())
    mesh.set_editor_property('relative_location', unreal.Vector(0, 0, -92.368843))
    mesh.set_editor_property('relative_rotation', unreal.Rotator(pitch=0, yaw=-90, roll=0))
    mesh.set_editor_property('relative_scale3d', unreal.Vector(1, 1, 1))
    mesh.set_editor_property('override_materials', [])
    after = snapshot(character)
    visual_fields = {'mesh', 'animation', 'mesh_location', 'mesh_rotation', 'mesh_scale', 'overrides'}
    for key in before.keys() - visual_fields:
        if before[key] != after[key]:
            raise RuntimeError('迁移改变了玩法属性 ' + key)
    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
        raise RuntimeError('无法保存角色蓝图')
    output = Path(unreal.Paths.project_saved_dir()) / 'WinsomeGirl' / 'blueprint-sync.json'
    output.write_text(json.dumps({'blueprint': PATH, 'before': before, 'after': after}, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('WINSOME_SYNC_COMPLETE ' + str(output))


if __name__ == '__main__':
    main()
