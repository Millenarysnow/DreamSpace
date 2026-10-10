# -*- coding: utf-8 -*-
"""在 UE 编辑器 Python 环境中，从 TEST 生成独立的白色建筑描边预览。

只读源关卡及源网格，写入 /Game/DreamPresentation/WhitePreview 下的专用资源。
生成的是当前编辑器可见状态的静态快照，不带原蓝图的解谜和移动逻辑。
使用方法见 Documents/DreamSpace_白色建筑描边预览.md。
"""

import json
import math
import sys
from pathlib import Path

import unreal


SOURCE_MAP = "/Game/0_/Maps/TEST"
ASSET_ROOT = "/Game/DreamPresentation/WhitePreview"
PREVIEW_MAP = ASSET_ROOT + "/Maps/L_WhiteOutlinePreview"
MATERIAL_ROOT = ASSET_ROOT + "/Materials"
SCRIPT_DIR = Path(__file__).resolve().parent

# 主光同时照到固定构图中两侧的建筑表面，背面与遮挡处仍会产生真实投影。
# 相应的 LightDirection 必须同步，是从表面指向光源而非光的传播方向。
KEY_LIGHT_ROTATION = unreal.Rotator(pitch=-50.0, yaw=55.0, roll=0.0)

# 默认参数只决定本次生成的效果。生成后可直接在材质实例中调节，
# 无需重写 HLSL；重新运行整个脚本时会恢复这些默认参数并重新拍摄建筑快照。
OUTLINE_PARAMETERS = {
    "SilhouetteWidth": (1.15, "外轮廓采样半径（输出像素）；越大线越粗。"),
    "StructureWidth": (0.95, "内部结构采样半径（输出像素）；建议小于外轮廓。"),
    "StructureStrength": (0.9, "内部结构线浓度；0 为关闭，1 为完整黑色。"),
    "NormalThreshold": (0.25, "法线转折阈值；提高后只保留更明显的墙角和台阶。"),
    "DepthThreshold": (0.006, "倒数深度曲率阈值；提高可过滤很浅的遮挡细节。"),
    "ShadingStrength": (0.035, "排线之间的浅灰底色强度；0 使用纯白纸面，主要明暗由排线承担。"),
    "LineOpacity": (1.0, "所有线条的整体浓度；0 为关闭描边。"),
    "MaxSubjectDepth": (1000000.0, "主体最大深度（厘米）；空背景始终输出纯白。"),
    "StrokeVariation": (0.08, "轮廓的固定线宽变化；0 恢复等宽描边，不随时间摆动。"),
    "HatchStrength": (0.88, "阴影排线的整体浓度；0 关闭排线，恢复白色描边预览。"),
    "HatchSpacing": (48.0, "表面基础排线间距（厘米）；越小越密，世界投影不依赖模型 UV。"),
    "HatchWidth": (0.85, "排线宽度（最终输出像素）；远处用导数过滤避免摩尔纹。"),
    "HatchAngle": (12.0, "主排线相对表面投影坐标的角度（度）；交叉层再旋转 60 度。"),
    "HatchIrregularity": (0.12, "排线固定的手绘弯曲量（间距比例）；只影响面内笔划，不扰动轮廓。"),
    "HatchStrokeLength": (6.0, "单段笔划长度（基础间距的倍数）；各排线错开接笔位置。"),
    "ShadowInfluence": (0.6, "真实光照和遮挡阴影的权重；其余来自几何法线受光。"),
    "ShadowReference": (0.72, "白色受光面的亮度参考；低于参考的区域逐渐增加排线。"),
    "HatchToneBias": (0.02, "排线明暗的留白偏移；越大亮面越干净。"),
    "HatchToneContrast": (1.0, "排线明暗对比；提高后更早叠加交叉线。"),
    "HatchDebugView": (0.0, "验收视图：0 完整效果，1 明暗依据，2 仅排线，3 原始光照；正常查看保持 0。"),
}

# 末端只做稳定的抗锯齿。手绘感来自表面的明暗排线，不再移动图像或使用时间噪声。
# 保留材质实例路径，已有关卡仍然可以通过同一资源调节平滑强度。
SMOOTHING_PARAMETERS = {
    "SmoothingStrength": (1.0, "沿笔迹方向的抗锯齿强度；0 关闭，1 完整平滑。"),
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
    """先检查材质图编译错误再保存；GPU Shader 异步编译还需真实加载和截图验收。"""
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
    custom = expression(material, unreal.MaterialExpressionCustom, 0, 0,
                        "纯白背景、稳定轮廓与由明暗驱动的表面交叉排线")
    custom.set_editor_property("description", "白色建筑手绘排线")
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

    # 后处理的 WorldPosition 根据当前可见表面深度重建，不要求原网格有合适的 UV。
    # 使用绝对世界坐标，避免相机移动时笔划像屏幕滤镜一样在墙面上滑动。
    position = expression(material, unreal.MaterialExpressionWorldPosition, -700, 480,
                          "可见表面的绝对世界位置，用于把笔划贴在建筑表面")
    sources["SurfacePosition"] = position

    for index, (name, (value, description)) in enumerate(OUTLINE_PARAMETERS.items()):
        node = expression(material, unreal.MaterialExpressionScalarParameter, -1300, index * 150, description)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        node.set_editor_property("group", "阴影排线" if name.startswith(("Hatch", "Shadow"))
                                else "描边与白色明暗")
        sources[name] = node

    for index, (name, value, description) in enumerate([
        ("InkColor", unreal.LinearColor(0.006, 0.006, 0.006, 1.0), "线条墨色：默认接近黑色"),
        # 对应预览 DirectionalLight 的 (-50, 55, 0) 朝向的反方向，即表面指向光源。
        ("LightDirection", unreal.LinearColor(-0.36869, -0.52654, 0.76604, 0.0),
         "表面指向主光源的世界方向；与预览的方向光一致"),
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
    # 清掉上一版的覆盖项，防止已经移除的动画/旧参数仍留在实例里误导调节。
    unreal.MaterialEditingLibrary.clear_all_material_instance_parameters(instance)
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
    """对轮廓与面内排线进行稳定的边缘平滑，返回关卡使用的材质实例。"""
    material = asset_of_type("M_WhiteOutlineAA", MATERIAL_ROOT, unreal.Material, unreal.MaterialFactoryNew())
    clear_material_graph(material)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    # 同一后处理阶段按优先级从小到大执行；必须先描边、再读取描边后的颜色。
    material.set_editor_property("blendable_priority", 10)
    custom = expression(material, unreal.MaterialExpressionCustom, 0, 0,
                        "轮廓与阴影排线沿笔划方向抗锯齿，不进行时间位移")
    custom.set_editor_property("description", "白色建筑线稿平滑")
    custom.set_editor_property("code", (SCRIPT_DIR / "WhiteOutlineAA.hlsl").read_text(encoding="utf-8"))
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    scene = expression(material, unreal.MaterialExpressionSceneTexture, -500, 0, "上一个后处理的颜色，包含已生成的描边")
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    sources = {"SceneColor": scene}
    for index, (name, (value, description)) in enumerate(SMOOTHING_PARAMETERS.items()):
        node = expression(material, unreal.MaterialExpressionScalarParameter, -1100, index * 150, description)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        node.set_editor_property("group", "线稿平滑")
        sources[name] = node
    inputs = []
    for name in sources:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    custom.set_editor_property("inputs", inputs)
    for name, node in sources.items():
        require(unreal.MaterialEditingLibrary.connect_material_expressions(node, "", custom, name), "连接平滑输入失败")
    connect_output(custom, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    compile_and_save(material)

    instance = asset_of_type("MI_WhitePencil", MATERIAL_ROOT, unreal.MaterialInstanceConstant,
                             unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(instance, material)
    unreal.MaterialEditingLibrary.clear_all_material_instance_parameters(instance)
    for name, (value, _) in SMOOTHING_PARAMETERS.items():
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(instance, name, value)
        actual = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(instance, name)
        require(math.isclose(actual, value, rel_tol=1e-5, abs_tol=1e-6), "线稿平滑参数读回不一致：" + name)
    unreal.MaterialEditingLibrary.update_material_instance(instance)
    require(unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False), "保存线稿平滑实例失败")
    return instance


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

    # 光照与投影会进入后处理的明暗映射，凹处通过更密的交叉排线表现。
    light = editor.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 5000), KEY_LIGHT_ROTATION)
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
    # 线稿版使用末端抗锯齿；白盒版传入 None，只挂载自己的面明暗材质。
    if smoothing_material is not None:
        post.set_editor_property("tags", ["DreamWhiteHatchingPreview"])
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


def update_preview_materials():
    """更新线稿与主光方向，保留已有预览网格、相机及白色表面材质。"""
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    require(level.load_level(PREVIEW_MAP), "无法加载现有预览关卡：" + PREVIEW_MAP)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    volumes = [actor for actor in actors if isinstance(actor, unreal.PostProcessVolume)
               and actor.get_actor_label() == "WhitePreview_PostProcess"]
    require(len(volumes) == 1, "预览应有且仅有一个专用后处理体积")
    key_lights = [actor for actor in actors if isinstance(actor, unreal.DirectionalLight)
                  and actor.get_actor_label() == "WhitePreview_KeyLight"]
    require(len(key_lights) == 1, "预览应有且仅有一个专用主光")
    # 同步实际主光与后处理法线受光方向，让大墙保持中间调，遮挡阴影才更深。
    key_lights[0].set_actor_rotation(KEY_LIGHT_ROTATION, teleport_physics=True)
    actual_rotation = key_lights[0].get_actor_rotation()
    # UE Python 的 Rotator 位置参数顺序不是 Pitch/Yaw/Roll；必须使用具名参数，
    # 并读回核对，避免明暗公式和实际方向光指向两个不同方向。
    for actual, expected in zip((actual_rotation.pitch, actual_rotation.yaw, actual_rotation.roll),
                                (KEY_LIGHT_ROTATION.pitch, KEY_LIGHT_ROTATION.yaw, KEY_LIGHT_ROTATION.roll)):
        require(math.isclose(actual, expected, abs_tol=1e-3), "预览主光旋转读回不一致")
    outline_instance = create_outline_material()
    smoothing_instance = create_smoothing_material()

    # 将旧的平滑父材质替换为线稿平滑实例，并清理重跑时已有的同名实例引用。
    # 仅筛除这两项，其余体积设置及可能存在的附加效果都保留。
    post = volumes[0]
    settings = post.get_editor_property("settings")
    # 运行时 GameMode 按此标签临时选择 FXAA，并在退出时恢复原设置。
    tags = list(post.get_editor_property("tags"))
    if "DreamWhiteHatchingPreview" not in [str(tag) for tag in tags]:
        tags.append("DreamWhiteHatchingPreview")
    post.set_editor_property("tags", tags)
    weighted = settings.get_editor_property("weighted_blendables")
    replaced_paths = {MATERIAL_ROOT + "/M_WhiteOutlineAA.M_WhiteOutlineAA", smoothing_instance.get_path_name()}
    weighted.set_editor_property("array", [item for item in weighted.get_editor_property("array")
                                if not item.get_editor_property("object")
                                or item.get_editor_property("object").get_path_name() not in replaced_paths])
    settings.set_editor_property("weighted_blendables", weighted)
    post.set_editor_property("settings", settings)
    post.add_or_update_blendable(outline_instance, 1.0)
    post.add_or_update_blendable(smoothing_instance, 1.0)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    require(unreal.EditorLoadingAndSavingUtils.save_map(world, PREVIEW_MAP), "保存手绘排线预览引用失败")
    output = Path(unreal.Paths.project_saved_dir()) / "WhitePreview" / "hatching_generation_report.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({"preview_map": PREVIEW_MAP, "materials_only": True,
                                 "key_light_rotation": [KEY_LIGHT_ROTATION.pitch, KEY_LIGHT_ROTATION.yaw, KEY_LIGHT_ROTATION.roll],
                                 "outline_parameters": {name: value for name, (value, _) in OUTLINE_PARAMETERS.items()},
                                 "smoothing_parameters": {name: value for name, (value, _) in SMOOTHING_PARAMETERS.items()}},
                                ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("WHITE_HATCHING_OK map=%s report=%s" % (PREVIEW_MAP, output))


def main():
    # 修改着色器后采用增量更新；只有明确重建快照时才重新读取 TEST。
    if "--materials-only" in sys.argv:
        update_preview_materials()
        return
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
