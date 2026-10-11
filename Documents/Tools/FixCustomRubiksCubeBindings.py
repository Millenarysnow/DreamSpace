"""修正当前自定义魔方蓝图的旋转中心与八角块绑定，保留模型、材质和摆放。

在独立 UE Python 命令行进程中运行。默认只预览修正方案；指定
DREAMSPACE_APPLY_RUBIKS_BINDINGS=1 时，保存这一张蓝图，不保存任何地图。
组件名与保存前的位置均写入 Saved/RubiksCube/CustomBindingsFix.json，方便复查。
"""

import json
import os
from pathlib import Path
import unreal


ASSET = "/Game/DreamInteraction/BP/BP_DreamRubiksCube"
blueprint = unreal.load_asset(ASSET)
if not blueprint:
    raise RuntimeError("找不到待修正的魔方蓝图：" + ASSET)
subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
objects = []
by_name = {}
for handle in subsystem.k2_gather_subobject_data_for_blueprint(blueprint):
    data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)
    obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object_for_blueprint(data, blueprint)
    if obj:
        objects.append(obj)
        # SCS 模板对象通常名为 Mesh2_GEN_VARIABLE，保存的组件引用则使用 Mesh2。
        # 子对象数据的变量名才是蓝图运行时创建组件时的稳定名称。
        variable_name = str(unreal.SubobjectDataBlueprintFunctionLibrary.get_variable_name(data))
        by_name[variable_name] = obj
cubes = [obj for obj in objects if isinstance(obj, unreal.DreamRubiksCubeComponent)]
if len(cubes) != 1:
    raise RuntimeError("需要恰好一个魔方组件，避免误改其它交互配置。")
cube = cubes[0]
if cube.get_editor_property("generate_visuals"):
    raise RuntimeError("当前蓝图仍使用默认外观，不应自动修改自定义绑定。")
references = list(cube.get_editor_property("corner_components"))
if len(references) != 8:
    raise RuntimeError("自定义角块数组必须已有八项有效绑定。")

# 从已有的组件路径/属性引用定位模板，不创建新引用，也不修改网格层级和碰撞资源。
by_name.update({obj.get_name().removesuffix("_GEN_VARIABLE"): obj for obj in objects})
corners = []
for reference in references:
    name = str(reference.get_editor_property("path_to_component"))
    # 未填写路径时 Python 可能把 UE 的 NAME_None 暴露为字符串 "None"。
    if name in ("", "None"):
        name = str(reference.get_editor_property("component_property"))
    corner = by_name.get(name)
    if not isinstance(corner, unreal.SceneComponent):
        raise RuntimeError("绑定不能定位到蓝图角块模板：" + name)
    corners.append(corner)
if len({corner.get_name() for corner in corners}) != 8:
    raise RuntimeError("角块绑定重复，不能自动判断八个身份。")

# 以八个角块中心的包围区间中点作为此次修正的旋转中心。
# 只改中心参数与数组顺序；现有角块间距不完全等距，模型摆放交由关卡作者后续调校。
positions = [corner.get_editor_property("relative_location") for corner in corners]
center_values = [(min(getattr(p, axis) for p in positions) + max(getattr(p, axis) for p in positions)) * 0.5
                 for axis in ("x", "y", "z")]
center = unreal.Vector(*center_values)
ordered = [None] * 8
ordered_names = [None] * 8
for reference, corner, position in zip(references, corners, positions):
    offset = [getattr(position, axis) - getattr(center, axis) for axis in ("x", "y", "z")]
    if min(abs(value) for value in offset) <= 1e-4:
        raise RuntimeError("角块位于中心平面，无法判断正负半层：" + corner.get_name())
    index = (4 if offset[0] > 0 else 0) | (2 if offset[1] > 0 else 0) | (1 if offset[2] > 0 else 0)
    if ordered[index] is not None:
        raise RuntimeError("两个角块落在相同半空间，请先整理模型布局。")
    ordered[index] = reference
    # 报告使用详情面板中的组件名称，去掉 UE 给 SCS 模板追加的内部后缀。
    ordered_names[index] = corner.get_name().removesuffix("_GEN_VARIABLE")

before_center = cube.get_editor_property("cube_center")
report = {
    "asset": ASSET,
    "before_center": [before_center.x, before_center.y, before_center.z],
    "center": center_values,
    "before_order": [str(ref.get_editor_property("path_to_component")) for ref in references],
    "order": ordered_names,
    "positions": {corner.get_name().removesuffix("_GEN_VARIABLE"): [p.x, p.y, p.z]
                  for corner, p in zip(corners, positions)},
    "saved": False,
}
if os.environ.get("DREAMSPACE_APPLY_RUBIKS_BINDINGS") == "1":
    cube.set_editor_property("cube_center", center)
    cube.set_editor_property("corner_components", ordered)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint):
        raise RuntimeError("蓝图修正保存失败。")
    report["saved"] = True
output = Path(unreal.Paths.project_saved_dir()) / "RubiksCube" / "CustomBindingsFix.json"
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.log("CUSTOM_RUBIKS_BINDINGS " + json.dumps(report, ensure_ascii=False))
