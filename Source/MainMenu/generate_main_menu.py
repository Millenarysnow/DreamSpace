# -*- coding: utf-8 -*-
"""从现有 WhitePreview 的已保存资源生成独立菜单，不保存或改写源预览和 TEST。

先运行 prepare_main_menu_art.py、编译 DreamSpaceEditor，再在 UE Python commandlet 中运行。
输出全部限制在 /Game/DreamPresentation/MainMenu，布局和输入由原生 C++ 类负责。
"""

import importlib.util
import json
import math
from pathlib import Path

import unreal


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT = SCRIPT_DIR.parents[1]
ROOT = "/Game/DreamPresentation/MainMenu"
MATERIALS = ROOT + "/Materials"
MENU_MAP = ROOT + "/Maps/L_DreamMainMenu"
PREVIEW = "/Game/DreamPresentation/WhitePreview"

# 复用现有生成器的安全创建、中文节点说明与保存检查，只更换其写入范围。
# 以 import 方式加载不执行它的 main，尤其不会重建或保存 WhitePreview。
spec = importlib.util.spec_from_file_location("dream_preview_util", SCRIPT_DIR.parent / "WhitePreview" / "generate_white_preview.py")
util = importlib.util.module_from_spec(spec)
spec.loader.exec_module(util)
util.ASSET_ROOT = ROOT
util.MATERIAL_ROOT = MATERIALS
require = util.require


def import_texture(filename, name):
    """导入整理后的 PNG；纹理与关卡都是硬引用，因此烘焙后无需外部图片。"""
    source = PROJECT / "RawContent" / "MainMenu" / filename
    require(source.exists(), "请先整理美术素材：" + str(source))
    task = unreal.AssetImportTask()
    for key, value in {"filename": str(source), "destination_path": ROOT + "/Textures",
                       "destination_name": name, "automated": True, "replace_existing": True,
                       "save": True}.items():
        task.set_editor_property(key, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.EditorAssetLibrary.load_asset(ROOT + "/Textures/" + name)
    require(isinstance(texture, unreal.Texture2D), "贴图导入失败：" + name)
    # UI 压缩保留 Logo 透明边；纸纹使用同样的无损像素，避免褶皱被压成块。
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("srgb", True)
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    require(unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False), "保存贴图失败")
    return texture


def create_menu_outline(center):
    """复制已有材质图，沿用现有调参；只为菜单版本增加建筑局部坐标输入。"""
    destination = MATERIALS + "/M_MenuHatching"
    if not unreal.EditorAssetLibrary.does_asset_exist(destination):
        require(unreal.EditorAssetLibrary.duplicate_asset(PREVIEW + "/Materials/M_WhiteOutlinePost", destination),
                "复制描边材质失败")
    material = unreal.EditorAssetLibrary.load_asset(destination)
    nodes = list(unreal.MaterialEditingLibrary.get_material_expressions(material))
    custom = next((node for node in nodes if isinstance(node, unreal.MaterialExpressionCustom)), None)
    require(custom is not None, "描边材质缺少排线 Custom 节点")
    original = (SCRIPT_DIR.parent / "WhitePreview" / "WhiteOutline.hlsl").read_text(encoding="utf-8")
    old_block = "float3 projectionWeights = pow(abs(centerNormal), 8.0);"
    require(original.count(old_block) == 1, "源排线代码的坐标入口已改变，请同步菜单生成器")
    # 明暗依据仍由世界法线和真实灯光计算。只有笔划投影使用局部位置/法线，
    # 因而旋转中光照能自然变化，铅笔纹理却不会相对墙面滑动或切换投影方向。
    local_block = """// 菜单专用：把表面位置与法线逆旋转到建筑的初始局部坐标。
float3 relativePosition = SurfacePosition - BuildingOrigin.rgb;
float3 localPosition = float3(dot(relativePosition, BuildingAxisX.rgb),
    dot(relativePosition, BuildingAxisY.rgb), dot(relativePosition, BuildingAxisZ.rgb));
float3 localNormal = float3(dot(centerNormal, BuildingAxisX.rgb),
    dot(centerNormal, BuildingAxisY.rgb), dot(centerNormal, BuildingAxisZ.rgb));
float3 projectionWeights = pow(abs(localNormal), 8.0);"""
    code = original.replace(old_block, local_block).replace(
        "float3 surface = SurfacePosition * centerMask / max(HatchSpacing, 1.0);",
        "float3 surface = localPosition * centerMask / max(HatchSpacing, 1.0);")
    custom.set_editor_property("code", code)
    custom.set_editor_property("description", "菜单建筑局部坐标铅笔排线")
    sources = {}
    for node in nodes:
        if isinstance(node, (unreal.MaterialExpressionScalarParameter, unreal.MaterialExpressionVectorParameter)):
            sources[str(node.get_editor_property("parameter_name"))] = node
        elif isinstance(node, unreal.MaterialExpressionSceneTexture):
            texture_id = node.get_editor_property("scene_texture_id")
            names = {unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0: "SceneColor",
                     unreal.SceneTextureId.PPI_SCENE_DEPTH: "DepthBuffer",
                     unreal.SceneTextureId.PPI_WORLD_NORMAL: "NormalBuffer"}
            if texture_id in names:
                sources[names[texture_id]] = node
        elif isinstance(node, unreal.MaterialExpressionWorldPosition):
            sources["SurfacePosition"] = node
    for index, (name, value) in enumerate([
        ("BuildingOrigin", unreal.LinearColor(*center, 0.0)),
        ("BuildingAxisX", unreal.LinearColor(1.0, 0.0, 0.0, 0.0)),
        ("BuildingAxisY", unreal.LinearColor(0.0, 1.0, 0.0, 0.0)),
        ("BuildingAxisZ", unreal.LinearColor(0.0, 0.0, 1.0, 0.0)),
    ]):
        node = next((node for node in nodes if isinstance(node, unreal.MaterialExpressionVectorParameter)
                     and str(node.get_editor_property("parameter_name")) == name), None)
        if node is None:
            node = util.expression(material, unreal.MaterialExpressionVectorParameter, -500, 1100 + index * 150,
                                   "由菜单展示 Actor 更新的局部坐标：" + name)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        node.set_editor_property("group", "建筑局部坐标（运行时自动更新）")
        sources[name] = node
    # 按原顺序重建全部输入，并显式重连已有节点和四个新增节点，
    # 避免复制材质中的旧连接依赖之前的数组槽号。
    input_names = [str(item.get_editor_property("input_name"))
                   for item in custom.get_editor_property("inputs")]
    for name in sources:
        if name not in input_names:
            input_names.append(name)
    inputs = []
    for name in input_names:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        inputs.append(item)
    custom.set_editor_property("inputs", inputs)
    for name, node in sources.items():
        require(unreal.MaterialEditingLibrary.connect_material_expressions(node, "", custom, name), "连接局部坐标失败")
    util.compile_and_save(material)
    instance = util.asset_of_type("MI_MenuHatching", MATERIALS, unreal.MaterialInstanceConstant,
                                  unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(instance, material)
    source_instance = unreal.EditorAssetLibrary.load_asset(PREVIEW + "/Materials/MI_WhiteOutline")
    for name in util.OUTLINE_PARAMETERS:
        value = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(source_instance, name)
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(instance, name, value)
    for name in ("InkColor", "LightDirection"):
        value = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(source_instance, name)
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(instance, name, value)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(instance, "HatchDebugView", 0.0)
    unreal.MaterialEditingLibrary.update_material_instance(instance)
    require(unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False), "保存菜单排线实例失败")
    return instance


def create_paper_material(texture):
    """纸张放在线稿平滑之后，统一调制白背景和建筑，不改变前面的几何描边判断。"""
    material = util.asset_of_type("M_MenuPaper", MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    util.clear_material_graph(material)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    material.set_editor_property("blendable_priority", 20)
    color = util.expression(material, unreal.MaterialExpressionSceneTexture, -600, -200, "已经描边和平滑的建筑画面")
    color.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    uv = util.expression(material, unreal.MaterialExpressionScreenPosition, -1000, 0, "固定于视口的纸张 UV")
    paper = util.expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -600, 0, "用户提供的轻皱 A4 纸照片")
    paper.set_editor_property("parameter_name", "PaperTexture")
    paper.set_editor_property("texture", texture)
    # UE 5.8 的 ScreenPosition 有 ViewportUV / PixelPosition 两个输出，不再使用映射枚举属性。
    # 编辑器把纹理 Coordinates 引脚显示名缩写为 UVs；空名字选择首个 UV 输入，兼容该缩写。
    require(unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", paper, ""), "连接纸张 UV 失败")
    strength = util.expression(material, unreal.MaterialExpressionScalarParameter, -600, 230, "纸张褶皱浓度，降低可使画面更干净")
    strength.set_editor_property("parameter_name", "PaperStrength")
    strength.set_editor_property("default_value", 0.48)
    tint = util.expression(material, unreal.MaterialExpressionVectorParameter, -600, 400, "接近白色的暖纸色，避免强烈泛黄")
    tint.set_editor_property("parameter_name", "PaperTint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.965, 0.958, 0.94, 1.0))
    custom = util.expression(material, unreal.MaterialExpressionCustom, 0, 0, "让建筑和空背景融入同一张皱纸")
    custom.set_editor_property("code", (SCRIPT_DIR / "MainMenuPaper.hlsl").read_text(encoding="utf-8"))
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    sources = {"SceneColor": color, "PaperSample": paper, "PaperStrength": strength, "PaperTint": tint}
    inputs = []
    for name in sources:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        inputs.append(item)
    custom.set_editor_property("inputs", inputs)
    for name, node in sources.items():
        require(unreal.MaterialEditingLibrary.connect_material_expressions(node, "", custom, name), "连接纸张输入失败")
    util.connect_output(custom, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    util.compile_and_save(material)
    instance = util.asset_of_type("MI_MenuPaper", MATERIALS, unreal.MaterialInstanceConstant,
                                  unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(instance, material)
    unreal.MaterialEditingLibrary.update_material_instance(instance)
    require(unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False), "保存纸张实例失败")
    return instance


def main():
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    require(level.load_level(PREVIEW + "/Maps/L_WhiteOutlinePreview"), "无法加载现有铅笔预览")
    actors = list(editor.get_all_level_actors())
    meshes = [actor for actor in actors if actor.actor_has_tag("DreamWhitePreviewMesh")]
    lights = [actor for actor in actors if isinstance(actor, unreal.DirectionalLight)]
    posts = [actor for actor in actors if actor.actor_has_tag("DreamWhiteHatchingPreview")]
    require(meshes and len(lights) == 1 and len(posts) == 1, "预览主体、主光或后处理不完整")
    low, high = [math.inf] * 3, [-math.inf] * 3
    records = []
    for actor in meshes:
        component = actor.static_mesh_component
        records.append({"label": actor.get_actor_label(), "transform": actor.get_actor_transform(),
                        "mesh": component.get_editor_property("static_mesh"),
                        "materials": [component.get_material(slot) for slot in range(component.get_num_materials())]})
        origin, extent = actor.get_actor_bounds(False)
        for axis, (value, half) in enumerate(zip((origin.x, origin.y, origin.z), (extent.x, extent.y, extent.z))):
            low[axis] = min(low[axis], value - half)
            high[axis] = max(high[axis], value + half)
    center = [(a + b) * 0.5 for a, b in zip(low, high)]
    half_extent = [(b - a) * 0.5 for a, b in zip(low, high)]
    light_transform = lights[0].get_actor_transform()
    light_intensity = lights[0].light_component.get_editor_property("intensity")
    light_angle = lights[0].light_component.get_editor_property("light_source_angle")
    post_settings = posts[0].get_editor_property("settings")

    logo = import_texture("Logo_Graphite.png", "T_MenuLogo")
    paper = import_texture("Paper_Crumpled.png", "T_MenuPaper")
    outline = create_menu_outline(center)
    paper_material = create_paper_material(paper)
    smoothing = unreal.EditorAssetLibrary.load_asset(PREVIEW + "/Materials/MI_WhitePencil")
    require(smoothing is not None, "缺少已有线稿抗锯齿实例")

    # 将源关卡完全卸载后创建新世界。源建筑网格仍为只读引用，材质覆盖只在新组件上。
    require(unreal.EditorLoadingAndSavingUtils.new_blank_map(False), "创建菜单关卡失败")
    scene_class = unreal.load_class(None, "/Script/DreamSpace.DreamMainMenuScene")
    mode_class = unreal.load_class(None, "/Script/DreamSpace.DreamMainMenuGameMode")
    require(scene_class and mode_class, "菜单 C++ 类未编译")
    scene = editor.spawn_actor_from_class(scene_class, unreal.Vector(*center))
    require(scene is not None, "创建菜单展示 Actor 失败")
    scene.set_actor_label("MainMenu_BuildingAndCamera")
    scene.set_editor_property("building_half_extent", unreal.Vector(*half_extent))
    scene.set_editor_property("logo_texture", logo)
    scene.set_editor_property("outline_material", outline)
    scene.update_camera(16.0 / 9.0)
    pivot = scene.get_editor_property("building_pivot")
    for record in records:
        transform = record["transform"]
        actor = editor.spawn_actor_from_class(unreal.StaticMeshActor, transform.translation, transform.rotation.rotator())
        require(actor is not None, "创建菜单网格失败")
        actor.set_actor_label(record["label"].replace("White_", "Menu_", 1))
        actor.set_folder_path("MainMenu/Architecture")
        actor.set_actor_scale3d(transform.scale3d)
        component = actor.static_mesh_component
        component.set_mobility(unreal.ComponentMobility.MOVABLE)
        component.set_static_mesh(record["mesh"])
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        for slot, material in enumerate(record["materials"]):
            component.set_material(slot, material)
        actor.set_editor_property("tags", ["DreamMainMenuMesh"])
        actor.attach_to_component(pivot, "", unreal.AttachmentRule.KEEP_WORLD,
                                  unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
    light = editor.spawn_actor_from_class(unreal.DirectionalLight, light_transform.translation, light_transform.rotation.rotator())
    light.set_actor_label("MainMenu_KeyLight")
    light.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    light.light_component.set_editor_property("intensity", light_intensity)
    light.light_component.set_editor_property("light_source_angle", light_angle)
    post = editor.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector())
    post.set_actor_label("MainMenu_PostProcess")
    post.set_editor_property("unbound", True)
    post.set_editor_property("priority", 100.0)
    weighted = post_settings.get_editor_property("weighted_blendables")
    weighted.set_editor_property("array", [])
    post_settings.set_editor_property("weighted_blendables", weighted)
    post.set_editor_property("settings", post_settings)
    post.set_editor_property("tags", ["DreamWhiteHatchingPreview", "DreamMainMenuPostProcess"])
    for material in (outline, smoothing, paper_material):
        post.add_or_update_blendable(material, 1.0)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", mode_class)
    camera = scene.get_editor_property("menu_camera")
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
        camera.get_world_location(), camera.get_world_rotation())
    unreal.EditorAssetLibrary.make_directory(ROOT + "/Maps")
    require(unreal.EditorLoadingAndSavingUtils.save_map(world, MENU_MAP), "保存菜单关卡失败")
    report = {"map": MENU_MAP, "source_preview": PREVIEW + "/Maps/L_WhiteOutlinePreview",
              "mesh_count": len(records), "bounds_min": low, "bounds_max": high,
              "building_center": center, "half_extent": half_extent,
              "rotation_speed": scene.get_editor_property("rotation_speed"),
              "logo": logo.get_path_name(), "paper": paper.get_path_name(),
              "game_mode": mode_class.get_path_name(), "gameplay_map": "/Game/0_/Maps/TEST"}
    output = Path(unreal.Paths.project_saved_dir()) / "MainMenu" / "generation_report.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("DREAM_MAIN_MENU_GENERATED " + str(output))


if __name__ == "__main__":
    main()
