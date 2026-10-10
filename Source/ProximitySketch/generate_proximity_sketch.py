# -*- coding: utf-8 -*-
"""生成游戏用距离线稿材质，只写入 /Game/DreamPresentation/ProximitySketch。

在 UE 编辑器 Python commandlet 中运行。材质 Custom 节点嵌入完整 HLSL，
运行及打包无需 Python 插件或外部源码文件；原建筑材质和地图不需要改写。
"""

import importlib.util
import json
from pathlib import Path

import unreal

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = "/Game/DreamPresentation/ProximitySketch"
MATERIALS = ROOT + "/Materials"

# 仅复用已验证的材质节点创建 / 保存工具；import 不会运行预览生成器的 main。
# 重新限定写入根目录，避免生成游戏版本时覆盖菜单或独立白模预览资源。
spec = importlib.util.spec_from_file_location("proximity_material_util", SCRIPT_DIR.parent / "WhitePreview/generate_white_preview.py")
util = importlib.util.module_from_spec(spec)
spec.loader.exec_module(util)
util.ASSET_ROOT = ROOT
util.MATERIAL_ROOT = MATERIALS

PARAMETERS = dict(util.OUTLINE_PARAMETERS)
for obsolete in ("HatchDebugView",):
    PARAMETERS.pop(obsolete)
PARAMETERS.update({
    "ShadowReference": (0.8, "经曝光和基础色归一后的 HDR 受光参考；不是原贴图的亮度。"),
    "NormalRadius": (600.0, "玩家周围完全正常渲染的球形半径，单位厘米。"),
    "TransitionWidth": (150.0, "正常渲染向手绘线稿过渡的距离，单位厘米；0 近似硬切。"),
    "EffectStrength": (0.0, "默认关闭；本地玩家相机管理器有有效 Pawn 时设置为 1，防止编辑器被染成线稿。"),
    "InkDarkness": (0.006, "轮廓目标显示灰阶；0 为黑色，1 为白色。"),
    "SketchDebugView": (0.0, "0 正式效果，1 外圈明暗，2 距离遮罩，3 原始场景。"),
})


def create_smoothing():
    """复用已有细线平滑算法，增加距离遮罩，使正常内圈不再经过额外 FXAA。"""
    material = util.asset_of_type("M_ProximitySketchAA", MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    nodes = list(unreal.MaterialEditingLibrary.get_material_expressions(material))

    def obtain(cls, x, y, description, predicate=lambda node: True):
        node = next((node for node in nodes if isinstance(node, cls) and predicate(node)), None)
        if node is None:
            node = util.expression(material, cls, x, y, description)
            nodes.append(node)
        return node

    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    material.set_editor_property("blendable_priority", 10)
    custom = obtain(unreal.MaterialExpressionCustom, 0, 0, "只在玩家范围外对石墨细线做方向平滑")
    custom.set_editor_property("description", "距离线稿局部平滑")
    prefix = (SCRIPT_DIR / "ProximitySketchAA.hlsl").read_text(encoding="utf-8")
    smoothing = (SCRIPT_DIR.parent / "WhitePreview/WhiteOutlineAA.hlsl").read_text(encoding="utf-8")
    util.require(smoothing.count("return smoothed;") == 1, "预览平滑出口改变，请同步游戏生成器")
    smoothing = smoothing.replace("return smoothed;", "return lerp(SceneColor.rgb, smoothed, sketchWeight);")
    custom.set_editor_property("code", prefix.replace("// PROXIMITY_AA_IMPLEMENTATION", smoothing))
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    sources = {}
    for index, (name, texture) in enumerate([("SceneColor", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0),
                                           ("DepthBuffer", unreal.SceneTextureId.PPI_SCENE_DEPTH),
                                           ("MiniatureDepth", unreal.SceneTextureId.PPI_CUSTOM_DEPTH),
                                           ("MiniatureStencil", unreal.SceneTextureId.PPI_CUSTOM_STENCIL)]):
        node = obtain(unreal.MaterialExpressionSceneTexture, -700, index * 150, "平滑场景缓冲：" + name,
                      lambda node: node.get_editor_property("scene_texture_id") == texture)
        node.set_editor_property("scene_texture_id", texture)
        sources[name] = node
    for index, name in enumerate(("NormalRadius", "TransitionWidth", "EffectStrength", "MaxSubjectDepth", "SketchDebugView", "SmoothingStrength")):
        value, description = (1.0, "只平滑新增细线的强度，0 关闭，1 完整平滑") if name == "SmoothingStrength" else PARAMETERS[name]
        node = obtain(unreal.MaterialExpressionScalarParameter, -1300, index * 150, description,
                      lambda node: str(node.get_editor_property("parameter_name")) == name)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        node.set_editor_property("group", "距离范围与线稿平滑")
        sources[name] = node
    position = obtain(unreal.MaterialExpressionVectorParameter, -700, 400, "与主线稿同步的玩家世界位置")
    position.set_editor_property("parameter_name", "PlayerPosition")
    position.set_editor_property("default_value", unreal.LinearColor(0, 0, 0, 0))
    sources["PlayerPosition"] = position
    inputs = []
    for name in sources:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        inputs.append(entry)
    custom.set_editor_property("inputs", inputs)
    for name, node in sources.items():
        util.require(unreal.MaterialEditingLibrary.connect_material_expressions(node, "", custom, name), "平滑输入连接失败：" + name)
    util.connect_output(custom, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    util.compile_and_save(material)
    instance = util.asset_of_type("MI_ProximitySketchAA", MATERIALS, unreal.MaterialInstanceConstant,
                                 unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(instance, material)
    unreal.MaterialEditingLibrary.update_material_instance(instance)
    util.require(unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False), "保存平滑实例失败")
    return instance


def create_miniature_display():
    """复制原手办显示材质的颜色 / 透明度图，只为距离判定增加透明深度写入。"""
    source = "/Game/DreamInteraction/Materials/M_SceneCaptureDisplay"
    destination = MATERIALS + "/M_ProximityMiniatureDisplay"
    if not unreal.EditorAssetLibrary.does_asset_exist(destination):
        util.require(unreal.EditorAssetLibrary.duplicate_asset(source, destination), "复制手办显示材质失败")
    material = unreal.EditorAssetLibrary.load_asset(destination)
    util.require(isinstance(material, unreal.Material), "手办显示材质类型不正确")
    material.set_editor_property("allow_translucent_custom_depth_writes", True)
    # 完全透明的 RT 背景不写深度；可见的手办像素才获得 241 标记。
    material.set_editor_property("opacity_mask_clip_value", 0.05)
    util.compile_and_save(material)
    return material


def main():
    """所有输入显式连接，使异步 GPU 编译也能得到正确的缓冲和曝光依赖。"""
    material = util.asset_of_type("M_ProximitySketch", MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    # 游玩控制器 CDO 硬引用此资源，UE 在载入时可能把表达式 Root 住。
    # 增量更新复用节点而不删除它们，避免 DeleteMaterialExpression 的 !IsRooted 断言。
    # 第一次生成没有节点，下面的助手会正常创建；多次运行不会堆积同名参数。
    nodes = list(unreal.MaterialEditingLibrary.get_material_expressions(material))

    def node_for(node_class, x, y, description, predicate=lambda node: True):
        node = next((node for node in nodes if isinstance(node, node_class) and predicate(node)), None)
        if node is None:
            node = util.expression(material, node_class, x, y, description)
            nodes.append(node)
        node.set_editor_property("desc", description)
        return node

    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    # 在自动曝光 / TSR 完成后合成，原始内圈不会因为白纸参与计量而变暗。
    # 深度、法线及世界位置在 HLSL 中显式去抖，不修改全局抗锯齿。
    material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    material.set_editor_property("blendable_priority", 0)
    custom = node_for(unreal.MaterialExpressionCustom, 0, 0,
                             "原始场景曝光与 TSR 完成后，按玩家世界距离合成手绘排线")
    custom.set_editor_property("description", "玩家距离手绘线稿")
    custom.set_editor_property("code", (SCRIPT_DIR / "ProximitySketch.hlsl").read_text(encoding="utf-8"))
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    sources = {}
    for index, (name, texture) in enumerate([
        ("SceneColor", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0),
        ("LightingColor", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT2),
        ("DepthBuffer", unreal.SceneTextureId.PPI_SCENE_DEPTH),
        ("NormalBuffer", unreal.SceneTextureId.PPI_WORLD_NORMAL),
        ("BaseColorBuffer", unreal.SceneTextureId.PPI_BASE_COLOR),
        ("MetallicBuffer", unreal.SceneTextureId.PPI_METALLIC),
        ("MiniatureDepth", unreal.SceneTextureId.PPI_CUSTOM_DEPTH),
        ("MiniatureStencil", unreal.SceneTextureId.PPI_CUSTOM_STENCIL),
    ]):
        node = node_for(unreal.MaterialExpressionSceneTexture, -700, index * 150,
                        "场景缓冲依赖：" + name, lambda node: node.get_editor_property("scene_texture_id") == texture)
        node.set_editor_property("scene_texture_id", texture)
        sources[name] = node
    sources["SurfacePosition"] = node_for(unreal.MaterialExpressionWorldPosition, -700, 800,
                                                "当前可见表面的绝对世界位置；与玩家位置计算三维距离")
    sources["Exposure"] = node_for(unreal.MaterialExpressionEyeAdaptation, -700, 950,
                                         "声明曝光缓冲依赖，HDR 光照只用于估算排线明暗")
    for index, (name, (value, description)) in enumerate(PARAMETERS.items()):
        node = node_for(unreal.MaterialExpressionScalarParameter, -1400, index * 150, description,
                        lambda node: str(node.get_editor_property("parameter_name")) == name)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        node.set_editor_property("group", "距离范围" if name in ("NormalRadius", "TransitionWidth", "EffectStrength", "SketchDebugView") else "线稿与阴影")
        sources[name] = node
    for index, (name, value, description) in enumerate([
        ("PlayerPosition", unreal.LinearColor(0, 0, 0, 0), "由当前相机每帧更新的 Pawn 世界位置，不使用摄像机位置"),
        ("LightDirection", unreal.LinearColor(-0.36869, -0.52654, 0.76604, 0), "相机管理器自动同步主方向光；无方向光时采用此默认方向"),
    ]):
        node = node_for(unreal.MaterialExpressionVectorParameter, -700, 1150 + index * 150, description,
                        lambda node: str(node.get_editor_property("parameter_name")) == name)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        node.set_editor_property("group", "运行时位置与方向")
        sources[name] = node
    inputs = []
    for name in sources:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        inputs.append(entry)
    custom.set_editor_property("inputs", inputs)
    for name, node in sources.items():
        util.require(unreal.MaterialEditingLibrary.connect_material_expressions(node, "", custom, name), "连接失败：" + name)
    util.connect_output(custom, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    util.compile_and_save(material)
    instance = util.asset_of_type("MI_ProximitySketch", MATERIALS, unreal.MaterialInstanceConstant,
                                 unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(instance, material)
    # 保留现有实例的美术调参；默认值来自父材质，不清空用户的覆盖项。
    unreal.MaterialEditingLibrary.update_material_instance(instance)
    util.require(unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False), "保存距离线稿实例失败")
    smoothing = create_smoothing()
    miniature = create_miniature_display()
    output = Path(unreal.Paths.project_saved_dir()).resolve() / "ProximitySketch"
    output.mkdir(parents=True, exist_ok=True)
    (output / "generation_report.json").write_text(json.dumps({
        "material": material.get_path_name(), "instance": instance.get_path_name(),
        "smoothing": smoothing.get_path_name(),
        "miniature_display": miniature.get_path_name(),
        "stage": "BL_SceneColorAfterTonemapping", "defaults": {name: value for name, (value, _) in PARAMETERS.items()},
    }, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("PROXIMITY_SKETCH_GENERATED " + instance.get_path_name())


if __name__ == "__main__":
    main()
