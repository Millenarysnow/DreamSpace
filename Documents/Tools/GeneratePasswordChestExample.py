# -*- coding: utf-8 -*-
"""在独立 UE 编辑器 Python commandlet 中生成密码箱蓝图、材质与示例关卡。

所有资源仅写入 /Game/DreamInteraction/PasswordChest。已有同名资源时保留用户修改，
不加载或保存正式关卡。生成的玩法只依赖原生 C++，运行或打包无需 Python 插件。
"""

import unreal


ROOT = "/Game/DreamInteraction/PasswordChest"
BLUEPRINT = ROOT + "/BP_DreamPasswordChest"
EXAMPLE_MAP = ROOT + "/L_PasswordChestExample"


def require(condition, message):
    """让资源创建/保存失败明确中止，避免日志报告成功但示例资源并未落盘。"""
    if not condition:
        raise RuntimeError(message)


def material(name, color, emissive=False):
    """建立可在编辑器继续调参的简单材质，已有资源直接复用，不清空用户的材质图。"""
    path = ROOT + "/Materials/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    result = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, ROOT + "/Materials", unreal.Material, unreal.MaterialFactoryNew()
    )
    require(result is not None, "无法创建材质：" + path)
    result.set_editor_property(
        "shading_model", unreal.MaterialShadingModel.MSM_UNLIT if emissive else unreal.MaterialShadingModel.MSM_DEFAULT_LIT
    )
    result.set_editor_property("used_with_nanite", True)
    node = unreal.MaterialEditingLibrary.create_material_expression(result, unreal.MaterialExpressionVectorParameter, -450, 0)
    node.set_editor_property("parameter_name", "GlowColor" if emissive else "BaseColor")
    node.set_editor_property("default_value", unreal.LinearColor(*color, 1.0))
    node.set_editor_property("desc", "光点自发光颜色与强度" if emissive else "密码箱示例的统一表面颜色")
    output = unreal.MaterialProperty.MP_EMISSIVE_COLOR if emissive else unreal.MaterialProperty.MP_BASE_COLOR
    unreal.MaterialEditingLibrary.connect_material_property(node, "", output)
    if not emissive:
        roughness = unreal.MaterialEditingLibrary.create_material_expression(result, unreal.MaterialExpressionConstant, -450, 170)
        roughness.set_editor_property("r", 0.6)
        roughness.set_editor_property("desc", "粗糙表面减少高光，便于看清开盖和内部光点")
        unreal.MaterialEditingLibrary.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.recompile_material(result)
    require(unreal.EditorAssetLibrary.save_loaded_asset(result), "无法保存材质：" + path)
    return result


def create_blueprint(chest_class, surface, glow):
    """使用原生 Actor 父类继承全部玩法，只在蓝图默认组件上配置独立的示例材质。"""
    if unreal.EditorAssetLibrary.does_asset_exist(BLUEPRINT):
        blueprint = unreal.load_asset(BLUEPRINT)
        require(blueprint is not None, "无法加载已有密码箱蓝图")
        return blueprint
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", chest_class)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "BP_DreamPasswordChest", ROOT, unreal.Blueprint, factory
    )
    require(blueprint is not None, "无法创建密码箱蓝图")
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    for handle in subsystem.k2_gather_subobject_data_for_blueprint(blueprint):
        data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)
        obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object_for_blueprint(data, blueprint)
        if isinstance(obj, unreal.StaticMeshComponent):
            # 原生子对象名称稳定，光点使用高亮无光照材质，箱底/侧板/盖子使用深色材质。
            obj.set_material(0, glow if obj.get_name() == "Glow" else surface)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    require(unreal.EditorAssetLibrary.save_loaded_asset(blueprint), "无法保存密码箱蓝图")
    return blueprint


def create_map(floor_material):
    """独立小场景包含玩家出生点、箱子和手办捕获锚点，打开后直接 PIE 即可体验。"""
    if unreal.EditorAssetLibrary.does_asset_exist(EXAMPLE_MAP):
        unreal.log("PASSWORD_CHEST_MAP_EXISTS " + EXAMPLE_MAP)
        return
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    require(world is not None, "无法创建示例关卡")
    editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    cube = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")

    def block(label, position, scale):
        actor = editor.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*position))
        actor.set_actor_label(label)
        actor.static_mesh_component.set_static_mesh(cube)
        actor.static_mesh_component.set_material(0, floor_material)
        actor.set_actor_scale3d(unreal.Vector(*scale))
        return actor

    block("Demo_Floor", (0, 0, -15), (16, 16, 0.3))
    block("Demo_BackWall", (0, -420, 140), (16, 0.2, 2.8))
    block("Demo_LeftBlock", (-180, -120, 55), (1.0, 1.0, 1.1))
    block("Demo_RightBlock", (210, -100, 90), (1.1, 1.1, 1.8))
    chest = editor.spawn_actor_from_class(unreal.load_class(None, BLUEPRINT + ".BP_DreamPasswordChest_C"), unreal.Vector())
    require(chest is not None, "无法在示例关卡放置密码箱")
    chest.set_actor_label("PasswordChest_1234")
    # 箱盖朝向 +Y；出生点位于箱子正面并保持直立，PIE 时稍向下瞄准箱子即可按 E 交互。
    start = editor.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 280, 102), unreal.Rotator(pitch=0, yaw=-90, roll=0))
    start.set_actor_label("PasswordChest_PlayerStart")
    anchor_class = unreal.load_class(None, "/Script/DreamSpace.DreamSceneCaptureAnchor")
    editor.spawn_actor_from_class(anchor_class, unreal.Vector(0, -60, 80))
    light = editor.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 350), unreal.Rotator(pitch=-55, yaw=-30, roll=0))
    light.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    light.light_component.set_editor_property("intensity", 3.0)
    light.light_component.set_editor_property("light_source_angle", 5.0)
    fill = editor.spawn_actor_from_class(unreal.PointLight, unreal.Vector(0, 200, 230))
    fill.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    # 原生 PointLight 默认使用坎德拉；示例近景只需少量补光，避免固定曝光下地面过曝。
    fill.light_component.set_editor_property("intensity", 20.0)
    fill.light_component.set_editor_property("attenuation_radius", 650.0)
    post = editor.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector())
    post.set_editor_property("unbound", True)
    settings = post.get_editor_property("settings")
    for name, value in {
        "auto_exposure_method": unreal.AutoExposureMethod.AEM_MANUAL,
        "auto_exposure_apply_physical_camera_exposure": False,
        "auto_exposure_bias": -1.0,
        "bloom_intensity": 0.6,
        "motion_blur_amount": 0.0,
    }.items():
        settings.set_editor_property("override_" + name, True)
        settings.set_editor_property(name, value)
    post.set_editor_property("settings", settings)
    mode = unreal.load_class(None, "/Script/DreamSpace.MainGameMode")
    world.get_world_settings().set_editor_property("default_game_mode", mode)
    require(unreal.EditorLoadingAndSavingUtils.save_map(world, EXAMPLE_MAP), "无法保存示例关卡")
    unreal.log("PASSWORD_CHEST_MAP_CREATED " + EXAMPLE_MAP)


def main():
    """先验证 C++ 类可用，再生成其独立示例资源，避免未编译的项目留下半成品蓝图。"""
    chest_class = unreal.load_class(None, "/Script/DreamSpace.DreamPasswordChest")
    require(chest_class is not None, "请先编译 DreamSpaceEditor，再运行密码箱示例生成器")
    surface = material("M_PasswordChestSurface", (0.035, 0.08, 0.12))
    glow = material("M_PasswordChestGlow", (3.0, 14.0, 20.0), emissive=True)
    floor = material("M_PasswordChestRoom", (0.45, 0.48, 0.52))
    create_blueprint(chest_class, surface, glow)
    create_map(floor)
    unreal.log("PASSWORD_CHEST_EXAMPLE_READY " + BLUEPRINT)


if __name__ == "__main__":
    main()
