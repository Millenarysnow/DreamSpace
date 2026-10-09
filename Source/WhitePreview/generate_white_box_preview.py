# -*- coding: utf-8 -*-
"""生成第二套白盒风格的建筑预览资源。

上一套 WhiteOutlinePreview 仍然保留。本脚本复用它的静态网格快照、取景和
预览 GameMode，但把资源写到独立的 WhiteBoxPreview 目录，避免两种视觉方案
互相覆盖。新后处理只输出灰度：建筑面的边缘更深，离开边缘后逐步变白，
形成类似白盒建模软件的三维世界观感。
"""

import json
import math
import sys
from pathlib import Path

import unreal


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

# 复用上一版已经验证过的快照和关卡搭建逻辑；下面只替换资源目录、材质和参数。
import generate_white_preview as base


base.ASSET_ROOT = "/Game/DreamPresentation/WhiteBoxPreview"
base.PREVIEW_MAP = base.ASSET_ROOT + "/Maps/L_WhiteBoxPreview"
base.MATERIAL_ROOT = base.ASSET_ROOT + "/Materials"
base.SCRIPT_DIR = SCRIPT_DIR

# 所有参数都保持灰度白盒语义。材质实例生成后可直接调节，不必重写 HLSL。
WHITE_BOX_PARAMETERS = {
    "EdgeWidth": (22.0, "1080p 下的面边缘渐变宽度（像素）；随视口高度自动缩放。"),
    "EdgeDarkness": (0.30, "柔和边缘压暗程度；0 关闭渐变，默认不产生黑色硬线。"),
    "EdgeFalloff": (1.60, "边缘向面中心的连续衰减；越小，灰色过渡区域越宽。"),
    "NormalThreshold": (0.32, "法线折线阈值；越高越只保留明显的折面。"),
    "DepthThreshold": (0.018, "扣除平面透视变化后的深度偏差阈值；识别遮挡和断面。"),
    "FaceBase": (0.82, "背光面基础灰度；与法线明暗一起保留白盒的体积。"),
    "FaceContrast": (0.10, "真实场景阴影的灰度强度；保留走廊与内凹结构的层次。"),
    "EdgeGray": (0.12, "边缘目标灰度；默认只混合 30%，不会变成纯黑描边。"),
    "MaxSubjectDepth": (1000000.0, "主体最大深度（厘米）；超过后视为纯白背景。"),
}


def expression(material, expression_class, x, y, description):
    """创建带中文说明的材质节点，便于在编辑器里继续调整白盒效果。"""
    return base.expression(material, expression_class, x, y, description)


def create_white_box_surface():
    """创建白盒专用表面材质：白色、粗糙、非金属，保留真实几何法线。"""
    material = base.asset_of_type(
        "M_WhiteBoxSurface", base.MATERIAL_ROOT, unreal.Material, unreal.MaterialFactoryNew())
    base.clear_material_graph(material)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("used_with_nanite", True)

    white = expression(material, unreal.MaterialExpressionConstant3Vector, -500, -120,
                       "白盒统一白色，不读取原建筑颜色贴图")
    white.set_editor_property("constant", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    roughness = expression(material, unreal.MaterialExpressionConstant, -500, 60,
                           "高粗糙度，避免高光破坏黑白白盒观感")
    roughness.set_editor_property("r", 1.0)
    zero = expression(material, unreal.MaterialExpressionConstant, -500, 240,
                      "非金属并关闭镜面反射")
    zero.set_editor_property("r", 0.0)
    base.connect_output(white, unreal.MaterialProperty.MP_BASE_COLOR)
    base.connect_output(roughness, unreal.MaterialProperty.MP_ROUGHNESS)
    base.connect_output(zero, unreal.MaterialProperty.MP_METALLIC)
    base.connect_output(zero, unreal.MaterialProperty.MP_SPECULAR)
    base.compile_and_save(material)
    return material


def create_white_box_post():
    """创建边缘深、面中心渐白的白盒后处理和可调材质实例。"""
    material = base.asset_of_type(
        "M_WhiteBoxPost", base.MATERIAL_ROOT, unreal.Material, unreal.MaterialFactoryNew())
    base.clear_material_graph(material)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property(
        "blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    material.set_editor_property("blendable_priority", 0)

    custom = expression(material, unreal.MaterialExpressionCustom, 0, 0,
                        "白盒边缘深色、面中心渐白、全画面黑白灰")
    custom.set_editor_property("description", "白盒面级渐变")
    custom.set_editor_property("code", (SCRIPT_DIR / "WhiteBoxPost.hlsl").read_text(encoding="utf-8"))
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)

    sources = {}
    # 这些 SceneTexture 节点用于声明 Custom HLSL 的场景缓冲依赖。
    for index, (name, texture_id) in enumerate([
        ("SceneColor", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0),
        ("DepthBuffer", unreal.SceneTextureId.PPI_SCENE_DEPTH),
        ("NormalBuffer", unreal.SceneTextureId.PPI_WORLD_NORMAL),
    ]):
        node = expression(material, unreal.MaterialExpressionSceneTexture,
                          -800, index * 170, "白盒场景缓冲：" + name)
        node.set_editor_property("scene_texture_id", texture_id)
        sources[name] = node

    for index, (name, (value, description)) in enumerate(WHITE_BOX_PARAMETERS.items()):
        node = expression(material, unreal.MaterialExpressionScalarParameter,
                          -1400, index * 145, description)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        node.set_editor_property("group", "白盒边缘渐变")
        sources[name] = node

    vector = expression(material, unreal.MaterialExpressionVectorParameter, -800, 600,
                        "世界空间白盒光方向，只影响灰度明暗")
    vector.set_editor_property("parameter_name", "LightDirection")
    vector.set_editor_property("default_value", unreal.LinearColor(-0.35, -0.45, 0.82, 0.0))
    vector.set_editor_property("group", "白盒边缘渐变")
    sources["LightDirection"] = vector

    custom_inputs = []
    for name in sources:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        custom_inputs.append(custom_input)
    custom.set_editor_property("inputs", custom_inputs)
    for name, node in sources.items():
        base.require(unreal.MaterialEditingLibrary.connect_material_expressions(node, "", custom, name),
                     "连接白盒后处理输入失败：" + name)
    base.connect_output(custom, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    base.compile_and_save(material)

    instance = base.asset_of_type(
        "MI_WhiteBoxPost", base.MATERIAL_ROOT, unreal.MaterialInstanceConstant,
        unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(instance, material)
    for name, (value, _) in WHITE_BOX_PARAMETERS.items():
        # UE 5.8.2 的 setter 返回值固定为 False，所以通过读回值判断是否成功。
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(instance, name, value)
        actual = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(instance, name)
        base.require(math.isclose(actual, value, rel_tol=1e-5, abs_tol=1e-6),
                     "白盒参数读回不一致：" + name)
    unreal.MaterialEditingLibrary.update_material_instance(instance)
    base.require(unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False),
                 "保存白盒材质实例失败")
    return instance


def main():
    records, skipped = base.snapshot_components()
    surface = create_white_box_surface()
    post = create_white_box_post()
    # 白盒方案的边缘在材质内逐步变淡；不叠加线稿版的黑线抗锯齿后处理。
    report = base.create_preview_level(records, surface, post, None)
    # 早期试做版曾生成这个材质，现在关卡已经不再引用它。
    # 仅清理本预览目录的旧资源，使重复运行脚本后目录仍与最终方案一致。
    obsolete_aa = base.MATERIAL_ROOT + "/M_WhiteOutlineAA"
    if unreal.EditorAssetLibrary.does_asset_exist(obsolete_aa):
        base.require(unreal.EditorAssetLibrary.delete_asset(obsolete_aa),
                     "清理白盒版旧抗锯齿资源失败")
    report.update({
        "variant": "white_box",
        "source_map": base.SOURCE_MAP,
        "preview_map": base.PREVIEW_MAP,
        "surface_material": base.ASSET_ROOT + "/Materials/M_WhiteBoxSurface",
        "post_material": base.ASSET_ROOT + "/Materials/MI_WhiteBoxPost",
        "parameters": {name: value for name, (value, _) in WHITE_BOX_PARAMETERS.items()},
        "skipped_components": skipped,
        "source_components": [record["source"] for record in records],
    })
    output = Path(unreal.Paths.project_saved_dir()) / "WhitePreview" / "white_box_generation_report.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("WHITE_BOX_PREVIEW_OK meshes=%d map=%s report=%s" %
               (len(records), base.PREVIEW_MAP, output))


if __name__ == "__main__":
    main()
