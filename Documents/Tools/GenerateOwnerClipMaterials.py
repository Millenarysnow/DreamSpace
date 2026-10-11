"""在 UE 编辑器 Python 环境中生成越肩角色的局部剔除材质；不保存原始模板资产。"""

import unreal


ROOT = '/Game/DreamCamera/Materials'
SOURCE_ROOT = '/Game/Characters/Mannequins/Materials'
LIBRARY = unreal.MaterialEditingLibrary

# 使用 Custom 节点表达小型连续函数；最终仍交给引擎的 DitherTemporalAA 和 ShadowReplace。
# 这里不直接调用 clip/discard，保留 UE 的遮罩材质编译、深度预通道与时间抗锯齿流程。
CLIP_CODE = r'''
// 第 6 位只由 FollowCamera 设置；未授权的 SceneCapture 和普通摄影相机保持原材质。
// 位置/朝向核对进一步隔离分屏与切镜头：每个角色的 MID 只属于自己的实际观察姿态。
if ((((int)UserFlags) & 64) == 0
    || length(ViewCameraLocal - ExpectedCameraLocal) > 2.0
    || dot(normalize(View.ViewForward), normalize(ExpectedCameraForward)) < 0.999)
{
    return 1.0;
}
// 位置差由图表中的 Subtract 节点先完成，保留 UE 大世界坐标精度，然后按世界厘米求长度。
// Radius 是外半径，内核逐渐剔除，Feather 宽度内用 smoothstep 留出柔和边缘。
float distanceToCamera = length(CameraRelativePosition);
float edge = saturate((distanceToCamera - max(0.0, Radius - Feather)) / max(Feather, 1.0));
edge = edge * edge * (3.0 - 2.0 * edge);
return 1.0 - saturate(Amount) * (1.0 - edge);
'''


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def duplicate_if_missing(source, name):
    """只创建本功能自己的副本；重跑脚本时复用已有资产，不替换用户已调好的实例参数。"""
    path = ROOT + '/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return require(unreal.load_asset(path), '无法加载 ' + path)
    return require(unreal.EditorAssetLibrary.duplicate_asset(source, path), '无法复制 ' + source)


def node(material, node_type, x, y, description):
    result = require(LIBRARY.create_material_expression(material, node_type, x, y), '无法创建表达式')
    result.set_editor_property('desc', 'DreamOwnerClip: ' + description)
    return result


def connect(source, target, input_name, output_name=''):
    # 部分引擎节点的首输入在 Python API 中没有名字（例如 TransformPosition），使用实际公开名称。
    names = LIBRARY.get_material_expression_input_names(target)
    if input_name == 'Input' and input_name not in names:
        input_name = names[0]
    require(LIBRARY.connect_material_expressions(source, output_name, target, input_name),
            '无法连接 {} -> {}.{}，有效输入 {}'.format(source.get_name(), target.get_name(), input_name, names))


def parameter(material, parameter_type, name, value, x, y, description):
    result = node(material, parameter_type, x, y, description)
    result.set_editor_property('parameter_name', name)
    result.set_editor_property('default_value', value)
    result.set_editor_property('group', 'DreamOwnerClip')
    return result


def add_local_clip(material):
    # 保留原有头发遮罩或透明度；不透明材质则以 1 作为原始覆盖率。
    if 'DreamOwnerClipAmount' in [str(name) for name in LIBRARY.get_scalar_parameter_names(material)]:
        unreal.log('CAMERA_CLIP_GENERATE 已接入局部剔除，保留现有图表: ' + material.get_path_name())
        return
    require(not material.get_editor_property('use_material_attributes'), '该生成器需要独立的材质属性图')
    blend = material.get_editor_property('blend_mode')
    require(blend in (unreal.BlendMode.BLEND_OPAQUE, unreal.BlendMode.BLEND_MASKED,
                      unreal.BlendMode.BLEND_TRANSLUCENT), '不支持该材质的混合模式')
    translucent = blend == unreal.BlendMode.BLEND_TRANSLUCENT
    opacity_property = unreal.MaterialProperty.MP_OPACITY if translucent else unreal.MaterialProperty.MP_OPACITY_MASK
    original_mask = LIBRARY.get_material_property_input_node(material, opacity_property)
    original_output = LIBRARY.get_material_property_input_node_output_name(material, opacity_property) if original_mask else ''
    if original_mask is None:
        original_mask = node(material, unreal.MaterialExpressionConstant, -500, 1300, '原始不透明覆盖率')
        original_mask.set_editor_property('r', 1.0)

    world = node(material, unreal.MaterialExpressionWorldPosition, -1800, 1600, '动画后表面世界位置')
    camera = node(material, unreal.MaterialExpressionCameraPositionWS, -1800, 1800, '当前渲染视角的镜头位置')
    relative = node(material, unreal.MaterialExpressionSubtract, -1500, 1600, '先完成大世界坐标位置差，再求厘米长度')
    connect(world, relative, 'A', 'XYZ')
    connect(camera, relative, 'B')
    camera_local = node(material, unreal.MaterialExpressionTransformPosition, -1500, 1800, '转换到网格局部坐标核对视角')
    camera_local.set_editor_property('transform_source_type', unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    camera_local.set_editor_property('transform_type', unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    connect(camera, camera_local, 'Input')
    flags = node(material, unreal.MaterialExpressionViewProperty, -1500, 2000, '主视角 UserFlags 第 6 位')
    flags.set_editor_property('property', unreal.MaterialExposedViewProperty.MEVP_POST_VOLUME_USER_FLAGS)

    amount = parameter(material, unreal.MaterialExpressionScalarParameter, 'DreamOwnerClipAmount', 0.0,
                       -1800, 2200, '默认关闭；运行时连续强度 0 至 1')
    radius = parameter(material, unreal.MaterialExpressionScalarParameter, 'DreamOwnerClipRadius', 38.0,
                       -1800, 2400, '局部球外半径，世界厘米')
    feather = parameter(material, unreal.MaterialExpressionScalarParameter, 'DreamOwnerClipFeather', 18.0,
                        -1800, 2600, '球缘柔和过渡宽度，世界厘米')
    expected_camera = parameter(material, unreal.MaterialExpressionVectorParameter, 'DreamOwnerClipCameraLocal',
                                unreal.LinearColor(0, 0, 0, 0), -1800, 2800, '本角色实际主镜头位置，网格局部坐标')
    expected_forward = parameter(material, unreal.MaterialExpressionVectorParameter, 'DreamOwnerClipCameraForward',
                                 unreal.LinearColor(1, 0, 0, 0), -1800, 3000, '本角色实际主镜头世界前方向')

    mask = node(material, unreal.MaterialExpressionCustom, -1100, 1600, '局部球形软剔除；匹配本地主视角')
    mask.set_editor_property('description', 'DreamOwnerLocalClip')
    mask.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    inputs = [('CameraRelativePosition', relative), ('ViewCameraLocal', camera_local),
              ('UserFlags', flags), ('Amount', amount), ('Radius', radius), ('Feather', feather),
              ('ExpectedCameraLocal', expected_camera), ('ExpectedCameraForward', expected_forward)]
    custom_inputs = []
    for name, _ in inputs:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property('input_name', name)
        custom_inputs.append(custom_input)
    mask.set_editor_property('inputs', custom_inputs)
    mask.set_editor_property('code', CLIP_CODE)
    for name, source in inputs:
        connect(source, mask, name)

    coverage, coverage_output = mask, ''
    if not translucent:
        dither = node(material, unreal.MaterialExpressionMaterialFunctionCall, -800, 1600, '引擎时间抖动，只柔化局部边缘')
        dither.set_editor_property('material_function', require(unreal.load_asset(
            '/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA'), '缺少引擎 DitherTemporalAA'))
        connect(mask, dither, 'Alpha Threshold')
        coverage, coverage_output = dither, 'Result'
    shadow = node(material, unreal.MaterialExpressionShadowReplace, -500, 1600, '投影时保留完整原始遮罩')
    opaque = node(material, unreal.MaterialExpressionConstant, -800, 1900, '阴影中的局部遮罩固定为 1')
    opaque.set_editor_property('r', 1.0)
    connect(coverage, shadow, 'Default', coverage_output)
    connect(opaque, shadow, 'Shadow')
    combined = node(material, unreal.MaterialExpressionMultiply, -200, 1400, '原模板遮罩乘局部遮罩')
    connect(original_mask, combined, 'A', original_output)
    connect(shadow, combined, 'B')
    require(LIBRARY.connect_material_property(combined, '', opacity_property), '无法发布局部覆盖率')
    if not translucent:
        material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    errors = LIBRARY.recompile_material(material)
    require(not errors, '局部剔除材质编译失败: ' + str(errors))


def generate():
    unreal.EditorAssetLibrary.make_directory(ROOT)
    material = duplicate_if_missing(SOURCE_ROOT + '/M_Mannequin', 'M_DreamOwnerClip')
    add_local_clip(material)
    # Quinn_02 继承 Quinn_01，因此副本也保留同样的两层链，纹理和身体分区颜色不会丢失。
    first = duplicate_if_missing(SOURCE_ROOT + '/Quinn/MI_Quinn_01', 'MI_QuinnOwnerClip_01')
    second = duplicate_if_missing(SOURCE_ROOT + '/Quinn/MI_Quinn_02', 'MI_QuinnOwnerClip_02')
    LIBRARY.set_material_instance_parent(first, material)
    LIBRARY.set_material_instance_parent(second, first)
    for asset in [material, first, second]:
        require(unreal.EditorAssetLibrary.save_loaded_asset(asset), '无法保存 ' + asset.get_path_name())
        unreal.log('CAMERA_CLIP_GENERATE 已保存: ' + asset.get_path_name())


if __name__ == '__main__':
    generate()
