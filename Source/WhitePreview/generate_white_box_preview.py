# -*- coding: utf-8 -*-
"""生成第二套白盒风格的建筑预览资源。

上一套 WhiteOutlinePreview 仍然保留。本脚本复用它的静态网格快照、取景和
预览 GameMode，但把资源写到独立的 WhiteBoxPreview 目录。当前方案使用真实
白色光照表现干净块面，后处理只抬亮并统一灰度，不再给每条几何边缘加渐变。
默认更新已有白盒关卡的材质和灯光；传入 --rebuild 才重新拍摄 TEST 的快照。
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

# 后处理只调整整幅画面的明暗范围，所有参数都不依赖屏幕尺寸或邻域采样。
# 最深阴影保持浅灰；亮面和白色环境达到白点后输出纯白，形成白模的留白感。
WHITE_BOX_PARAMETERS = {
    "ShadowFloor": (0.68, "最深阴影的灰度下限；越高整体越白，越低体积对比越强。"),
    "WhitePoint": (1.0, "场景亮度达到此值后成为纯白；降低可扩大亮面的白色区域。"),
    "MidtoneLift": (1.0, "中间调提亮曲线；小于 1 时抬亮侧面，保留柔和投影。"),
}
SURFACE_FILL = 0.025
KEY_INTENSITY = 2.0
KEY_SOURCE_ANGLE = 5.0
KEY_ROTATION = (-55.0, 60.0, 0.0)
BACKGROUND_TAG = "DreamWhitePreviewBackground"


def expression(material, expression_class, x, y, description):
    """创建带中文说明的材质节点，便于在编辑器里继续调整白盒效果。"""
    return base.expression(material, expression_class, x, y, description)


def create_white_box_surface():
    """白色粗糙表面保留真实几何法线，并用少量均匀补光托起背光面。"""
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
    # 这项低强度自发光只模拟无纹理的均匀环境补光，不产生高光或边缘效果。
    # 白盒关卡关闭 Lumen，补光不会反过来照亮邻近网格，也不会引入间接光噪点。
    fill = expression(material, unreal.MaterialExpressionScalarParameter, -500, 400,
                      "均匀白色补光，避免无天空光时背面全黑；不是辉光效果")
    fill.set_editor_property("parameter_name", "FillBrightness")
    fill.set_editor_property("default_value", SURFACE_FILL)
    fill.set_editor_property("group", "白盒表面")
    base.connect_output(white, unreal.MaterialProperty.MP_BASE_COLOR)
    base.connect_output(roughness, unreal.MaterialProperty.MP_ROUGHNESS)
    base.connect_output(zero, unreal.MaterialProperty.MP_METALLIC)
    base.connect_output(zero, unreal.MaterialProperty.MP_SPECULAR)
    base.connect_output(fill, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    base.compile_and_save(material)
    return material


def create_white_box_background():
    """创建无纹理的白色环境材质，让背景与建筑共享正常的抗锯齿流程。"""
    material = base.asset_of_type(
        "M_WhiteBoxBackground", base.MATERIAL_ROOT, unreal.Material, unreal.MaterialFactoryNew())
    base.clear_material_graph(material)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("is_sky", True)
    white = expression(material, unreal.MaterialExpressionConstant3Vector, -500, 0,
                       "无纹理白色环境；高于后处理白点，最终显示为纯白")
    white.set_editor_property("constant", unreal.LinearColor(16.0, 16.0, 16.0, 1.0))
    base.connect_output(white, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    base.compile_and_save(material)
    return material


def create_white_box_post():
    """对已完成抗锯齿的光照画面做灰度提亮，保留干净的块面和真实投影。"""
    material = base.asset_of_type(
        "M_WhiteBoxPost", base.MATERIAL_ROOT, unreal.Material, unreal.MaterialFactoryNew())
    base.clear_material_graph(material)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property(
        "blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    material.set_editor_property("blendable_priority", 0)

    custom = expression(material, unreal.MaterialExpressionCustom, 0, 0,
                        "真实块面光照提亮、阴影下限、全画面黑白灰；不生成描边")
    custom.set_editor_property("description", "干净白盒灰度映射")
    custom.set_editor_property("code", (SCRIPT_DIR / "WhiteBoxPost.hlsl").read_text(encoding="utf-8"))
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)

    # 只读取引擎已解析的颜色。深度和法线不再进入后处理，避免在细结构上重建硬边。
    scene = expression(material, unreal.MaterialExpressionSceneTexture, -800, 0,
                       "已经过抗锯齿的白色光照画面，包含真实投影")
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    sources = {"SceneColor": scene}

    for index, (name, (value, description)) in enumerate(WHITE_BOX_PARAMETERS.items()):
        node = expression(material, unreal.MaterialExpressionScalarParameter,
                          -1400, index * 145, description)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        node.set_editor_property("group", "白盒明暗")
        sources[name] = node

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
    # 旧实例保存过面边缘参数；清除历史覆盖，实例面板只显示本版仍有作用的参数。
    unreal.MaterialEditingLibrary.clear_all_material_instance_parameters(instance)
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


def configure_white_box_level(surface, post_instance, background_material):
    """只更新白盒世界的组件覆盖和显示设置，保留已有建筑快照及固定相机。"""
    editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = editor.get_all_level_actors()
    meshes = [actor for actor in actors if actor.actor_has_tag("DreamWhitePreviewMesh")]
    volumes = [actor for actor in actors if isinstance(actor, unreal.PostProcessVolume)]
    lights = [actor for actor in actors if isinstance(actor, unreal.DirectionalLight)]
    base.require(meshes and len(volumes) == 1 and len(lights) == 1,
                 "白盒关卡缺少建筑，或后处理／主光数量不唯一")
    for actor in meshes:
        component = actor.static_mesh_component
        for slot in range(max(1, component.get_num_materials())):
            component.set_material(slot, surface)

    light = lights[0]
    # 主光从相机前上方斜照建筑，让两个主要侧面都能受光；不同夹角保留块面差异。
    # 沿用旧线稿的侧后方光会使一面全暗、另一面过白，因此白盒单独配置方向。
    # UE Python 反射结构的构造参数顺序不作为 Pitch/Yaw/Roll 约定使用。
    # 显式命名并读回实际旋转，避免主光反向照向天空而使顶面和侧面全部落入阴影。
    light.set_actor_rotation(unreal.Rotator(pitch=KEY_ROTATION[0], yaw=KEY_ROTATION[1],
                                           roll=KEY_ROTATION[2]), teleport_physics=True)
    actual_rotation = light.get_actor_rotation()
    for actual, expected in zip((actual_rotation.pitch, actual_rotation.yaw, actual_rotation.roll), KEY_ROTATION):
        base.require(math.isclose(actual, expected, abs_tol=0.001), "主光实际旋转与白盒配置不一致")
    light.light_component.set_editor_property("intensity", KEY_INTENSITY)
    light.light_component.set_editor_property("light_source_angle", KEY_SOURCE_ANGLE)

    # 所有显示设置局限在白盒体积内，不改项目的全局渲染配置或其他预览方案。
    # 去掉 AO、Lumen 和局部曝光后，平整墙面只留下稳定的受光灰度；凹处仍有主光投影。
    volume = volumes[0]
    settings = base.configure_post_settings(volume.get_editor_property("settings"))
    values = {
        "dynamic_global_illumination_method": unreal.DynamicGlobalIlluminationMethod.NONE,
        "reflection_method": unreal.ReflectionMethod.NONE,
        "ambient_occlusion_intensity": 0.0,
        "local_exposure_highlight_contrast_scale": 1.0,
        "local_exposure_shadow_contrast_scale": 1.0,
    }
    for name, value in values.items():
        settings.set_editor_property("override_" + name, True)
        settings.set_editor_property(name, value)
    # 白盒只需要一个灰度映射阶段，重复运行也不会叠加旧的渐变或铅笔描边。
    weighted = settings.get_editor_property("weighted_blendables")
    weighted.set_editor_property("array", [])
    settings.set_editor_property("weighted_blendables", weighted)
    volume.set_editor_property("settings", settings)
    volume.add_or_update_blendable(post_instance, 1.0)

    backgrounds = [actor for actor in actors if actor.actor_has_tag(BACKGROUND_TAG)]
    base.require(len(backgrounds) <= 1, "白盒背景数量不唯一")
    background = backgrounds[0] if backgrounds else editor.spawn_actor_from_class(
        unreal.StaticMeshActor, unreal.Vector())
    base.require(background is not None, "创建白色环境失败")
    background.set_actor_label("WhiteBox_WhiteEnvironment")
    background.set_folder_path("WhitePreviewSetup")
    background.set_editor_property("tags", [BACKGROUND_TAG])
    background.set_actor_scale3d(unreal.Vector(2000.0, 2000.0, 2000.0))
    sphere = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Sphere")
    base.require(sphere is not None, "无法读取引擎基础球体")
    component = background.static_mesh_component
    component.set_mobility(unreal.ComponentMobility.MOVABLE)
    component.set_static_mesh(sphere)
    component.set_material(0, background_material)
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    # 环境只负责白色背景，不参与建筑的碰撞、投影或距离场遮挡。
    component.set_editor_property("cast_shadow", False)
    component.set_editor_property("affect_distance_field_lighting", False)
    component.set_editor_property("visible_in_ray_tracing", False)
    component.set_editor_property("receives_decals", False)

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    base.require(unreal.EditorLoadingAndSavingUtils.save_map(world, base.PREVIEW_MAP),
                 "保存干净白盒关卡失败")
    return {"mesh_count": len(meshes), "background_actor": background.get_actor_label(),
            "key_intensity": KEY_INTENSITY, "key_source_angle": KEY_SOURCE_ANGLE,
            "key_rotation": list(KEY_ROTATION),
            "surface_fill": SURFACE_FILL, "ambient_occlusion_intensity": 0.0,
            "global_illumination": "None", "reflections": "None"}


def main():
    # 已有白盒关卡直接增量更新；只有首次生成或显式 --rebuild 时读取源关卡。
    # 这样调整风格时不会重新展开蓝图，也不会因源关卡的缺失引用改变现有快照。
    rebuild = "--rebuild" in sys.argv or not unreal.EditorAssetLibrary.does_asset_exist(base.PREVIEW_MAP)
    report = {}
    if rebuild:
        records, skipped = base.snapshot_components()
    else:
        base.require(unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(base.PREVIEW_MAP),
                     "无法打开现有白盒预览关卡")
    surface = create_white_box_surface()
    background = create_white_box_background()
    post = create_white_box_post()
    if rebuild:
        report = base.create_preview_level(records, surface, post, None)
        report["skipped_components"] = skipped
        report["source_components"] = [record["source"] for record in records]
    report.update(configure_white_box_level(surface, post, background))
    report.update({
        "variant": "clean_white_box",
        "rebuilt_snapshot": rebuild,
        "source_map": base.SOURCE_MAP,
        "preview_map": base.PREVIEW_MAP,
        "surface_material": base.ASSET_ROOT + "/Materials/M_WhiteBoxSurface",
        "post_material": base.ASSET_ROOT + "/Materials/MI_WhiteBoxPost",
        "background_material": base.ASSET_ROOT + "/Materials/M_WhiteBoxBackground",
        "parameters": {name: value for name, (value, _) in WHITE_BOX_PARAMETERS.items()},
    })
    output = Path(unreal.Paths.project_saved_dir()) / "WhitePreview" / "white_box_generation_report.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("WHITE_BOX_PREVIEW_OK meshes=%d map=%s report=%s" %
               (report["mesh_count"], base.PREVIEW_MAP, output))


if __name__ == "__main__":
    main()
