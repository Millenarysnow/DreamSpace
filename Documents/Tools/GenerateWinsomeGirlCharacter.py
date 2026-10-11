"""UE 编辑器 Python：为 DreamCharacter 生成配套的 WinsomeGirl 模型和近镜头材质。

所有写入都位于 /Game/DreamCharacters/WinsomeGirl；源素材只读取。
在独立 UnrealEditor-Cmd 的 -run=pythonscript 中执行，可重复运行。
"""
import importlib.util
import json
from pathlib import Path
import unreal

ROOT = '/Game/DreamCharacters/WinsomeGirl'
SOURCE = '/Game/WinsomeGirl/Materials/'
LIBRARY = unreal.MaterialEditingLibrary


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def duplicate(source, destination):
    if unreal.EditorAssetLibrary.does_asset_exist(destination):
        return require(unreal.load_asset(destination), '无法加载 ' + destination)
    return require(unreal.EditorAssetLibrary.duplicate_asset(source, destination), '无法复制 ' + source)


def main():
    generator_path = Path(__file__).with_name('GenerateOwnerClipMaterials.py')
    spec = importlib.util.spec_from_file_location('dream_owner_clip_generator', generator_path)
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    materials = {}
    for name in ('M_BaseMat', 'M_Skin', 'M_Face', 'M_Hair', 'M_TRN_Shake'):
        material = duplicate(SOURCE + name, ROOT + '/Materials/' + name + 'OwnerClip')
        generator.add_local_clip(material)
        materials[name] = material
        require(unreal.EditorAssetLibrary.save_loaded_asset(material), '无法保存材质 ' + name)
    for name in ('MI_Cloth', 'MI_Outer'):
        instance = duplicate(SOURCE + name, ROOT + '/Materials/' + name + 'OwnerClip')
        LIBRARY.set_material_instance_parent(instance, materials['M_BaseMat'])
        require(unreal.EditorAssetLibrary.save_loaded_asset(instance), '无法保存实例 ' + name)
        materials[name] = instance

    mesh = duplicate('/Game/WinsomeGirl/Models/SK_Chara01', ROOT + '/SK_DreamWinsomeGirl')
    # 根据源网格逐槽映射，保留衣服/外套/皮肤/头发/透明片/脸的分区和原始材质参数。
    # 素材的 ClothMat 槽原本为空，用衣服材质补齐，避免默认灰色材质或近镜头警告。
    slots = []
    source_mesh = require(unreal.load_asset('/Game/WinsomeGirl/Models/SK_Chara01'), '缺少源模型')
    for slot in source_mesh.get_editor_property('materials'):
        original = slot.get_editor_property('material_interface')
        material = materials[original.get_name() if original else 'MI_Cloth']
        slot.set_editor_property('material_interface', material)
        slots.append(slot)
    mesh.set_editor_property('materials', slots)
    require(unreal.EditorAssetLibrary.save_loaded_asset(mesh), '无法保存角色网格')

    animation = require(unreal.load_asset(
        '/Game/WinsomeGirl/Maps/ThirdPersonExampleMap/Mannequin/Animations/ThirdPerson_AnimBP'), '缺少配套动画蓝图')
    require(mesh.get_editor_property('skeleton') == animation.get_editor_property('target_skeleton'), '模型与动画骨架不匹配')
    require(str(animation.get_editor_property('status')) == str(unreal.BlueprintStatus.BS_UP_TO_DATE), '配套动画蓝图未成功编译')
    report = {
        'mesh': mesh.get_path_name(), 'animation_class': animation.generated_class().get_path_name(),
        'skeleton': mesh.get_editor_property('skeleton').get_path_name(),
        'materials': [slot.material_interface.get_path_name() for slot in slots],
        'scalar_parameters': {name: [str(p) for p in LIBRARY.get_scalar_parameter_names(mat)] for name, mat in materials.items()},
    }
    output = Path(unreal.Paths.project_saved_dir()) / 'WinsomeGirl' / 'generation.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('WINSOME_GENERATE_COMPLETE ' + str(output))


if __name__ == '__main__':
    main()
