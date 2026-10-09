# -*- coding: utf-8 -*-
"""在 UE 编辑器 Python 环境中，从 TEST 生成独立的白色建筑描边预览。

只读源关卡及源网格，写入 /Game/DreamPresentation/WhitePreview 下的专用资源。
生成的是当前编辑器可见状态的静态快照，不带原蓝图的解谜和移动逻辑。
使用方法见 Documents/DreamSpace_白色建筑描边预览.md。
"""

import json
import math
from pathlib import Path

import unreal


SOURCE_MAP = "/Game/0_/Maps/TEST"
ASSET_ROOT = "/Game/DreamPresentation/WhitePreview"
PREVIEW_MAP = ASSET_ROOT + "/Maps/L_WhiteOutlinePreview"
MATERIAL_ROOT = ASSET_ROOT + "/Materials"
SCRIPT_DIR = Path(__file__).resolve().parent

# 默认参数只决定本次生成的效果。生成后可直接在材质实例中调节，
# 无需重写 HLSL；重新运行整个脚本时会恢复这些默认参数并重新拍摄建筑快照。
OUTLINE_PARAMETERS = {
    "SilhouetteWidth": (1.15, "外轮廓采样半径（输出像素）；越大线越粗。"),
    "StructureWidth": (0.95, "内部结构采样半径（输出像素）；建议小于外轮廓。"),
    "StructureStrength": (0.9, "内部结构线浓度；0 为关闭，1 为完整黑色。"),
    "NormalThreshold": (0.25, "法线转折阈值；提高后只保留更明显的墙角和台阶。"),
    "DepthThreshold": (0.006, "倒数深度曲率阈值；提高可过滤很浅的遮挡细节。"),
    "ShadingStrength": (0.16, "白色表面的浅灰明暗强度；0 为完全纯白。"),
    "LineOpacity": (1.0, "所有线条的整体浓度；0 为关闭描边。"),
    "MaxSubjectDepth": (1000000.0, "主体最大深度（厘米）；空背景始终输出纯白。"),
}


def require(condition, message):
    """关键步骤失败时立即停止，避免把缺网格或未编译成功的资源误报为完成。"""
    if not condition:
        raise RuntimeError(message)


def asset_of_type(name, folder, asset_class, factory):
    """只创建或更新专用目录内的资源；同名但类型不同的资源不会被覆盖。"""
    require(folder.startswith(ASSET_ROOT + "/"), "禁止写入预览目录以外的资源")
    path = folder + "/" + name
    asset = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if asset:
        require(isinstance(asset, asset_class), "同名资源类型不匹配：" + path)
    else:
        unreal.EditorAssetLibrary.make_directory(folder)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, asset_class, factory)
    require(asset is not None, "创建资源失败：" + path)
    return asset


def expression(material, expression_class, x, y, description):
    """给生成的材质节点写入中文说明，使材质编辑器里的图也能独立阅读。"""
    node = unreal.MaterialEditingLibrary.create_material_expression(material, expression_class, x, y)
    require(node is not None, "创建材质节点失败：" + description)
    node.set_editor_property("desc", description)
    return node


def clear_material_graph(material):
    """复制节点列表后逐一删除，确保重新生成不会残留上一次的 Custom 或参数节点。"""
    # 引擎 DeleteAllMaterialExpressions 遍历的正是被删除操作修改的节点数组，
    # 重复运行时可能跳过部分节点；遍历副本可避开这一行为并断开旧输出连接。
    previous_nodes = list(unreal.MaterialEditingLibrary.get_material_expressions(material))
    for node in previous_nodes:
        unreal.MaterialEditingLibrary.delete_material_expression(material, node)
    require(unreal.MaterialEditingLibrary.get_num_material_expressions(material) == 0,
            "清理材质节点失败：" + material.get_path_name())


def connect_output(node, material_property):
    require(unreal.MaterialEditingLibrary.connect_material_property(node, "", material_property),
            "连接材质输出失败：" + str(material_property))


def compile_and_save(material):
    """UE 5.8 的重编译接口直接返回错误列表；有错误时不继续生成关卡。"""
    errors = unreal.MaterialEditingLibrary.recompile_material(material)
    require(not errors, "材质编译失败：" + material.get_path_name() + "\n" + "\n".join(errors))
    require(unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False),
            "保存材质失败：" + material.get_path_name())


def create_white_material():
    """保留网格几何法线，移除颜色、金属、污渍和法线贴图，形成统一白色表面。"""
    material = asset_of_type("M_WhiteArchitecture", MATERIAL_ROOT, unreal.Material, unreal.MaterialFactoryNew())
    clear_material_graph(material)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    # 导入建筑中可能有单面墙或开放的截面；双面材质让其背面也能参与白模预览。
    material.set_editor_property("two_sided", True)
    # 源建筑包含 Nanite 网格；提前保存此用途，避免运行时临时补编译或打包时缺 Shader。
    material.set_editor_property("used_with_nanite", True)
    color = expression(material, unreal.MaterialExpressionConstant3Vector, -500, -100, "统一纯白底色，不读取原贴图")
    color.set_editor_property("constant", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    roughness = expression(material, unreal.MaterialExpressionConstant, -500, 100, "高粗糙度，避免金属或塑料高光")
    roughness.set_editor_property("r", 1.0)
    zero = expression(material, unreal.MaterialExpressionConstant, -500, 250, "非金属，关闭镜面反射")
    zero.set_editor_property("r", 0.0)
    connect_output(color, unreal.MaterialProperty.MP_BASE_COLOR)
    connect_output(roughness, unreal.MaterialProperty.MP_ROUGHNESS)
    connect_output(zero, unreal.MaterialProperty.MP_METALLIC)
    connect_output(zero, unreal.MaterialProperty.MP_SPECULAR)
    compile_and_save(material)
    return material


def create_outline_material():
    """创建有中文说明和可调参数的后处理材质，并保存实际使用的材质实例。"""
    material = asset_of_type("M_WhiteOutlinePost", MATERIAL_ROOT, unreal.Material, unreal.MaterialFactoryNew())
    clear_material_graph(material)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    material.set_editor_property("blendable_priority", 0)
    custom = expression(material, unreal.MaterialExpressionCustom, 0, 0, "纯白背景、浅灰形体与细黑结构线")
    custom.set_editor_property("description", "白色建筑描边")
    custom.set_editor_property("code", (SCRIPT_DIR / "WhiteOutline.hlsl").read_text(encoding="utf-8"))
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)

    # 显式连接 SceneTexture 节点，令编译器知道 Custom 代码需要哪些缓冲。
    # 只在 HLSL 内调用 SceneTextureLookup 不会自动声明 GBuffer 的使用依赖。
    sources = {}
    for index, (name, texture_id) in enumerate([
        ("SceneColor", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0),
        ("DepthBuffer", unreal.SceneTextureId.PPI_SCENE_DEPTH),
        ("NormalBuffer", unreal.SceneTextureId.PPI_WORLD_NORMAL),
    ]):
        node = expression(material, unreal.MaterialExpressionSceneTexture, -700, index * 150, "场景缓冲依赖：" + name)
        node.set_editor_property("scene_texture_id", texture_id)
        sources[name] = node

    for index, (name, (value, description)) in enumerate(OUTLINE_PARAMETERS.items()):
        node = expression(material, unreal.MaterialExpressionScalarParameter, -1300, index * 150, description)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        node.set_editor_property("group", "描边与白色明暗")
        sources[name] = node

    for index, (name, value, description) in enumerate([
        ("InkColor", unreal.LinearColor(0.006, 0.006, 0.006, 1.0), "线条墨色：默认接近黑色"),
        ("LightDirection", unreal.LinearColor(-0.35, -0.45, 0.82, 0.0), "浅灰形体的世界空间光方向"),
    ]):
        node = expression(material, unreal.MaterialExpressionVectorParameter, -700, 600 + index * 180, description)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        node.set_editor_property("group", "描边与白色明暗")
        sources[name] = node

    # CustomInput 在此版本不接受构造参数，先创建空结构再写入反射属性。
    custom_inputs = []
    for name in sources:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        custom_inputs.append(custom_input)
    custom.set_editor_property("inputs", custom_inputs)
    for name, node in sources.items():
        require(unreal.MaterialEditingLibrary.connect_material_expressions(node, "", custom, name),
                "连接后处理输入失败：" + name)
    connect_output(custom, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    compile_and_save(material)

    instance = asset_of_type("MI_WhiteOutline", MATERIAL_ROOT, unreal.MaterialInstanceConstant,
                             unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(instance, material)
    for name, (value, _) in OUTLINE_PARAMETERS.items():
        # UE 5.8.2 此 setter 的引擎实现始终返回 False，不能用返回值判断成败。
        # 写入后实际读回参数，既兼容引擎行为，也能确认材质实例确实得到期望值。
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(instance, name, value)
        actual = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(instance, name)
        require(math.isclose(actual, value, rel_tol=1e-5, abs_tol=1e-6), "描边参数读回不一致：" + name)
    unreal.MaterialEditingLibrary.update_material_instance(instance)
    require(unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False), "保存描边实例失败")
    return instance


def create_smoothing_material():
    """只对最终黑线进行边缘方向平滑，避免色调映射后的新线条产生阶梯锯齿。"""
    material = asset_of_type("M_WhiteOutlineAA", MATERIAL_ROOT, unreal.Material, unreal.MaterialFactoryNew())
    clear_material_graph(material)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    # 同一后处理阶段按优先级从小到大执行；必须先描边、再读取描边后的颜色。
    material.set_editor_property("blendable_priority", 10)
    custom = expression(material, unreal.MaterialExpressionCustom, 0, 0, "沿局部墨线方向平滑，保持白色留白")
    custom.set_editor_property("description", "白色建筑线条抗锯齿")
    custom.set_editor_property("code", (SCRIPT_DIR / "WhiteOutlineAA.hlsl").read_text(encoding="utf-8"))
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs = []
    for name in ("SceneColor", "SmoothingStrength"):
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    custom.set_editor_property("inputs", inputs)
    scene = expression(material, unreal.MaterialExpressionSceneTexture, -500, 0, "上一个后处理的颜色，包含已生成的描边")
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    strength = expression(material, unreal.MaterialExpressionScalarParameter, -500, 180, "平滑强度；0 关闭，1 完整边缘抗锯齿")
    strength.set_editor_property("parameter_name", "SmoothingStrength")
    strength.set_editor_property("default_value", 1.0)
    strength.set_editor_property("group", "线条平滑")
    for node, name in ((scene, "SceneColor"), (strength, "SmoothingStrength")):
        require(unreal.MaterialEditingLibrary.connect_material_expressions(node, "", custom, name), "连接平滑输入失败")
    connect_output(custom, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    compile_and_save(material)
    return material


def snapshot_components():
    """读取源关卡和递归子 Actor，记录可渲染组件的世界变换，随后即可卸载源关卡。"""
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    require(level.load_level(SOURCE_MAP), "无法打开源关卡：" + SOURCE_MAP)
    editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    queue = list(editor.get_all_level_actors())
    visited = set()
    records = []
    skipped = []

    while queue:
        actor = queue.pop()
        if actor.get_path_name() in visited:
            continue
        visited.add(actor.get_path_name())
        # 显式遍历 ChildActorComponent，保证门窗等嵌套蓝图中的网格不会漏掉。
        for child_component in actor.get_components_by_class(unreal.ChildActorComponent):
            child = child_component.get_editor_property("child_actor")
            if child:
                queue.append(child)
        if actor.get_editor_property("hidden") or actor.is_temporarily_hidden_in_editor(include_parent=True):
            continue
        for component in actor.get_components_by_class(unreal.StaticMeshComponent):
            mesh = component.get_editor_property("static_mesh")
            reason = None
            if not mesh:
                reason = "空网格引用"
            elif "EngineSky/" in mesh.get_path_name():
                reason = "原关卡天空背景"
            elif component.get_editor_property("hidden_in_game") or not component.get_editor_property("visible"):
                reason = "不可见组件"
            if reason:
                skipped.append({"actor": actor.get_actor_label(), "component": component.get_name(), "reason": reason})
                continue

            # 实例化网格按每个实例的世界变换拆为普通 StaticMeshActor。
            # 这样仍可使用相同白材质，不需要把原 Instanced 组件或蓝图带入预览。
            if isinstance(component, unreal.InstancedStaticMeshComponent):
                transforms = [component.get_instance_transform(index, world_space=True)
                              for index in range(component.get_instance_count())]
            else:
                transforms = [component.get_world_transform()]
            for index, transform in enumerate(transforms):
                records.append({
                    "label": actor.get_actor_label() + "_" + component.get_name() + "_" + str(index),
                    "mesh": mesh, "transform": transform,
                    "source": component.get_path_name(),
                })
    require(records, "源关卡没有可显示的静态网格，停止生成")
    return records, skipped


def configure_post_settings(settings):
    """预览采用固定曝光，关闭晕影、景深和辉光，维持整块纯白画布。"""
    values = {
        "auto_exposure_method": unreal.AutoExposureMethod.AEM_MANUAL,
        "auto_exposure_apply_physical_camera_exposure": False,
        "auto_exposure_bias": 0.0,
        "bloom_intensity": 0.0,
        "vignette_intensity": 0.0,
        "motion_blur_amount": 0.0,
        "depth_of_field_fstop": 32.0,
        "scene_fringe_intensity": 0.0,
    }
    for name, value in values.items():
        settings.set_editor_property("override_" + name, True)
        settings.set_editor_property(name, value)
    return settings


def create_preview_level(records, white_material, outline_instance, smoothing_material):
    """只生成静态建筑、灯光、相机和后处理，专用规则确保 PIE 没有玩法 HUD。"""
    # 在卸载 TEST 后创建空白临时世界，最后另存为预览路径；不保存原关卡。
    require(unreal.EditorLoadingAndSavingUtils.new_blank_map(False) is not None, "创建空白预览关卡失败")
    editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    low = [math.inf] * 3
    high = [-math.inf] * 3

    for record in records:
        transform = record["transform"]
        actor = editor.spawn_actor_from_class(unreal.StaticMeshActor, transform.translation, transform.rotation.rotator())
        require(actor is not None, "创建预览网格失败：" + record["label"])
        actor.set_actor_label("White_" + record["label"])
        actor.set_folder_path("WhiteArchitecture")
        actor.set_actor_scale3d(transform.scale3d)
        component = actor.static_mesh_component
        component.set_mobility(unreal.ComponentMobility.MOVABLE)
        component.set_static_mesh(record["mesh"])
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        # 覆盖组件的每一个材质槽，不修改 StaticMesh 资源上的默认材质。
        for slot in range(max(1, component.get_num_materials())):
            component.set_material(slot, white_material)
        actor.set_editor_property("tags", ["DreamWhitePreviewMesh"])
        origin, extent = actor.get_actor_bounds(False)
        for axis, (center, radius) in enumerate(zip((origin.x, origin.y, origin.z), (extent.x, extent.y, extent.z))):
            low[axis] = min(low[axis], center - radius)
            high[axis] = max(high[axis], center + radius)

    # 柔和白光只提供少量接触关系，主要的形体可读性由后处理法线明暗负责。
    light = editor.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 5000), unreal.Rotator(-50, -35, 0))
    light.set_actor_label("WhitePreview_KeyLight")
    light.set_folder_path("WhitePreviewSetup")
    light.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    light.light_component.set_editor_property("intensity", 3.0)
    light.light_component.set_editor_property("light_source_angle", 5.0)

    post = editor.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector())
    post.set_actor_label("WhitePreview_PostProcess")
    post.set_folder_path("WhitePreviewSetup")
    post.set_editor_property("unbound", True)
    post.set_editor_property("priority", 100.0)
    post.set_editor_property("settings", configure_post_settings(post.get_editor_property("settings")))
    post.add_or_update_blendable(outline_instance, 1.0)
    post.add_or_update_blendable(smoothing_material, 1.0)

    # 用包围盒的八个角计算透视取景距离，包含主体的所有楼层和底部。
    # 固定三分之四视角便于同时看到外形、台阶、走道与内凹结构。
    target = unreal.Vector(*[(a + b) * 0.5 for a, b in zip(low, high)])
    yaw = math.radians(-125.0)
    elevation = math.radians(18.0)
    outward = [math.cos(elevation) * math.cos(yaw), math.cos(elevation) * math.sin(yaw), math.sin(elevation)]
    forward = [-value for value in outward]
    right = [-math.sin(yaw), math.cos(yaw), 0.0]
    up = [-math.sin(elevation) * math.cos(yaw), -math.sin(elevation) * math.sin(yaw), math.cos(elevation)]
    horizontal_fov = 40.0
    aspect = 16.0 / 9.0
    tan_horizontal = math.tan(math.radians(horizontal_fov * 0.5))
    tan_vertical = tan_horizontal / aspect
    distance = 0.0
    for x in (low[0], high[0]):
        for y in (low[1], high[1]):
            for z in (low[2], high[2]):
                relative = [x - target.x, y - target.y, z - target.z]
                dot_forward = sum(a * b for a, b in zip(relative, forward))
                x_extent = abs(sum(a * b for a, b in zip(relative, right)))
                y_extent = abs(sum(a * b for a, b in zip(relative, up)))
                distance = max(distance, x_extent / tan_horizontal - dot_forward,
                               y_extent / tan_vertical - dot_forward)
    distance *= 1.18
    camera_location = unreal.Vector(target.x + outward[0] * distance,
                                    target.y + outward[1] * distance,
                                    target.z + outward[2] * distance)
    camera_rotation = unreal.MathLibrary.find_look_at_rotation(camera_location, target)
    camera = editor.spawn_actor_from_class(unreal.CameraActor, camera_location, camera_rotation)
    camera.set_actor_label("WhitePreview_Camera")
    camera.set_folder_path("WhitePreviewSetup")
    camera.set_editor_property("tags", ["DreamWhitePreviewCamera"])
    camera.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
    camera.camera_component.set_editor_property("field_of_view", horizontal_fov)
    camera.camera_component.set_editor_property("aspect_ratio", aspect)
    camera.camera_component.set_editor_property("constrain_aspect_ratio", False)

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    mode = unreal.load_class(None, "/Script/DreamSpace.DreamWhitePreviewGameMode")
    require(mode is not None, "未编译预览 GameMode，请先编译 DreamSpaceEditor")
    world.get_world_settings().set_editor_property("default_game_mode", mode)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(camera_location, camera_rotation)
    unreal.EditorAssetLibrary.make_directory(ASSET_ROOT + "/Maps")
    require(unreal.EditorLoadingAndSavingUtils.save_map(world, PREVIEW_MAP), "保存预览关卡失败")
    return {"bounds_min": low, "bounds_max": high, "camera_location": [camera_location.x, camera_location.y, camera_location.z],
            "camera_rotation": [camera_rotation.pitch, camera_rotation.yaw, camera_rotation.roll], "mesh_count": len(records)}


def main():
    records, skipped = snapshot_components()
    white_material = create_white_material()
    outline_instance = create_outline_material()
    smoothing_material = create_smoothing_material()
    report = create_preview_level(records, white_material, outline_instance, smoothing_material)
    report.update({"source_map": SOURCE_MAP, "preview_map": PREVIEW_MAP, "skipped_components": skipped,
                   "source_components": [record["source"] for record in records]})
    output = Path(unreal.Paths.project_saved_dir()) / "WhitePreview" / "generation_report.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("WHITE_PREVIEW_OK meshes=%d map=%s report=%s" % (len(records), PREVIEW_MAP, output))


if __name__ == "__main__":
    main()
