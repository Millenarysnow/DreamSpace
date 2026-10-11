"""在 UE 编辑器 Python 环境中创建携带二阶魔方组件的示例 Actor 蓝图。

蓝图使用普通 Actor 父类，只添加一个 DreamRubiksCubeComponent。玩法均由组件实现；
运行时默认生成六色模型并打乱。已有同名资源时只验证组件存在，不覆盖用户配置。
可通过独立 UnrealEditor-Cmd 的 -run=pythonscript 执行，不需要为运行时启用 Python 插件。
"""

import unreal


ASSET_PATH = "/Game/DreamInteraction/BP/BP_DreamRubiksCube"
cube_class = unreal.load_class(None, "/Script/DreamSpace.DreamRubiksCubeComponent")
if not cube_class:
    raise RuntimeError("请先编译 DreamSpaceEditor，再生成魔方示例蓝图。")

blueprint = unreal.load_asset(ASSET_PATH) if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH) else None
if blueprint is None:
    # 使用编辑器标准资产工厂与子对象系统，生成的资源等价于手动创建 Actor 蓝图并添加组件。
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.Actor)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "BP_DreamRubiksCube", "/Game/DreamInteraction/BP", unreal.Blueprint, factory
    )
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = subsystem.k2_gather_subobject_data_for_blueprint(blueprint)
    params = unreal.AddNewSubobjectParams(
        parent_handle=handles[0], new_class=cube_class, blueprint_context=blueprint
    )
    component_handle, failure = subsystem.add_new_subobject(params)
    if str(failure):
        raise RuntimeError("添加魔方组件失败：" + str(failure))
    subsystem.rename_subobject(component_handle, "RubiksCube")
    component_data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(component_handle)
    component = unreal.SubobjectDataBlueprintFunctionLibrary.get_object_for_blueprint(component_data, blueprint)
    component.set_editor_property("shuffle_on_begin_play", True)
    # 即使以后编辑器默认根组件的 Mobility 改变，示例仍显式要求可移动根以保证内部交互有效。
    for handle in subsystem.k2_gather_subobject_data_for_blueprint(blueprint):
        data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)
        obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object_for_blueprint(data, blueprint)
        if isinstance(obj, unreal.SceneComponent):
            obj.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    unreal.EditorAssetLibrary.save_loaded_asset(blueprint)
    unreal.log("RUBIKS_EXAMPLE_CREATED " + ASSET_PATH)
else:
    # 保留已存在资源。验证时使用子对象系统，可以看到普通 Actor 蓝图 SCS 中的组件模板。
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    found = False
    for handle in subsystem.k2_gather_subobject_data_for_blueprint(blueprint):
        data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)
        obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object_for_blueprint(data, blueprint)
        if obj and obj.get_class() == cube_class:
            found = True
    if not found:
        raise RuntimeError("同名蓝图已存在但没有魔方组件，请使用另一个名称，避免覆盖现有资产。")
    unreal.log("RUBIKS_EXAMPLE_EXISTS " + ASSET_PATH)
