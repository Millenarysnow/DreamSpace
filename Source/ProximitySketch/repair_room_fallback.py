# -*- coding: utf-8 -*-
"""修复 TEST 房间回退网格的细栏杆、立体广告文字丢失。

通过专用完整 UnrealEditor.exe 的 -ExecutePythonScript 运行，启用
PythonScriptPlugin、EditorScriptingUtilities。本工具只保存
/Game/0_/Meshes/Room/room；地图、蓝图和后处理材质不需要重新生成。
"""

import hashlib
import json
import shutil
import struct
from pathlib import Path

import unreal


ASSET = "/Game/0_/Meshes/Room/room"
ROOT = Path(unreal.Paths.project_dir()).resolve()
OUTPUT = ROOT / "Saved/ProximitySketch/DetailFix/RoomFallbackRepair"
CHANGED_FIELDS = ("generate_fallback", "fallback_target",
                  "fallback_percent_triangles", "fallback_relative_error")
NANITE_FIELDS = ("enabled", "explicit_tangents", "lerp_u_vs", "separable", "voxel_ndf",
                 "voxel_opacity", "shape_preservation", "position_precision", "normal_precision",
                 "tangent_precision", "bone_weight_precision", "target_minimum_residency_in_kb",
                 "keep_percent_triangles", "trim_relative_error", "max_edge_length_factor",
                 "num_rays", "voxel_level", "ray_back_up", "displacement_uv_channel",
                 "displacement_maps", "nanite_assembly_data")


def serialized(value):
    """结构体导出值而不是 Python 内存地址，便于重建前后准确核对。"""
    if hasattr(value, "export_text"):
        return value.export_text()
    if isinstance(value, unreal.Object):
        return value.get_path_name()
    return str(value)


def source_signature(mesh):
    """哈希原始顶点、三角形索引与 UV，确认本次只重建派生渲染数据。

    不能用 get_num_triangles() 代替源几何校验：Nanite 网格的这个接口读回
    实际 fallback 缓冲，修复后它应当增加；源 MeshDescription 则应完全不变。
    当前房间为导入的紧凑 ID 网格，每个 ID 在读取前验证，避免把稀疏数组
    或不完整读取误报为通过。
    """
    description = mesh.get_static_mesh_description(0)
    if not description:
        raise RuntimeError("房间没有原始 MeshDescription")
    digest = hashlib.sha256()
    vertices = description.get_vertex_count()
    triangles = description.get_triangle_count()
    instances = description.get_vertex_instance_count()
    for index in range(vertices):
        vertex = unreal.VertexID(index)
        if not description.is_vertex_valid(vertex):
            raise RuntimeError("源顶点 ID 不连续，请扩展校验后再修复")
        position = description.get_vertex_position(vertex)
        digest.update(struct.pack("<fff", position.x, position.y, position.z))
    for index in range(triangles):
        triangle = unreal.TriangleID(index)
        if not description.is_triangle_valid(triangle):
            raise RuntimeError("源三角形 ID 不连续，请扩展校验后再修复")
        corners = description.get_triangle_vertex_instances(triangle)
        ids = [corner.get_editor_property("id_value") for corner in corners]
        group = description.get_triangle_polygon_group(triangle).get_editor_property("id_value")
        digest.update(struct.pack("<iiii", *ids, group))
    # 房间只含一个 UV 通道。读取全部 vertex instance 的 UV，既核对纹理映射，
    # 也核对 instance 与真实顶点的关联，不能只检查总顶点数恰好相同。
    for index in range(instances):
        instance = unreal.VertexInstanceID(index)
        if not description.is_vertex_instance_valid(instance):
            raise RuntimeError("源顶点实例 ID 不连续，请扩展校验后再修复")
        vertex = description.get_vertex_instance_vertex(instance).get_editor_property("id_value")
        uv = description.get_vertex_instance_uv(instance, 0)
        digest.update(struct.pack("<iff", vertex, uv.x, uv.y))
    return {"vertices": vertices, "triangles": triangles, "instances": instances,
            "position_topology_uv_sha256": digest.hexdigest()}


def snapshot(mesh, editor):
    """保存本次构建前后的材质、碰撞配置、普通 LOD 和源几何证据。"""
    settings = mesh.get_editor_property("nanite_settings")
    body = mesh.get_editor_property("body_setup")
    lod_count = editor.get_lod_count(mesh)
    return {
        "fallback_triangles": mesh.get_num_triangles(0),
        "fallback_vertices": mesh.get_num_vertices(0),
        "nanite_settings": settings.export_text(),
        "protected": {
            "source": source_signature(mesh),
            "nanite": {name: serialized(settings.get_editor_property(name))
                       for name in NANITE_FIELDS},
            # UV 密度是重建派生值，材质引用和槽位名字才是需要保留的美术输入。
            "materials": [[serialized(slot.get_editor_property(name))
                           for name in ("material_interface", "material_slot_name", "imported_material_slot_name")]
                          for slot in mesh.get_editor_property("static_materials")],
            "collision": {name: serialized(body.get_editor_property(name))
                          for name in ("agg_geom", "collision_trace_flag", "default_instance",
                                       "walkable_slope_override", "double_sided_geometry")},
            "mesh_properties": {name: serialized(mesh.get_editor_property(name))
                                for name in ("lod_group", "lod_for_collision", "complex_collision_mesh",
                                             "customized_collision", "light_map_coordinate_index", "light_map_resolution")},
            "lod_build": [editor.get_lod_build_settings(mesh, index).export_text()
                          for index in range(lod_count)],
            "lod_reduction": [editor.get_lod_reduction_settings(mesh, index).export_text()
                              for index in range(lod_count)],
        },
    }


def main():
    """只对指定房间应用完整 fallback，并以实际渲染三角形数量验证构建。"""
    OUTPUT.mkdir(parents=True, exist_ok=True)
    mesh = unreal.load_asset(ASSET)
    editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if not mesh or not editor:
        raise RuntimeError("需要专用完整编辑器环境；不要使用 -run=pythonscript commandlet")
    before = snapshot(mesh, editor)
    settings = mesh.get_editor_property("nanite_settings")
    if not settings.get_editor_property("enabled"):
        raise RuntimeError("房间已经关闭 Nanite，请先检查资源是否与本次修复的输入一致")
    targets = dict(zip(CHANGED_FIELDS, (unreal.NaniteGenerateFallback.ENABLED,
                       unreal.NaniteFallbackTarget.PERCENT_TRIANGLES, 1.0, 0.0)))
    changed = any(settings.get_editor_property(name) != value for name, value in targets.items())
    if changed:
        # 第一份备份永不覆盖；重跑脚本也不能把修复后的文件伪装成原始备份。
        backup = OUTPUT / "room_before.uasset"
        if not backup.exists():
            shutil.copy2(ROOT / "Content/0_/Meshes/Room/room.uasset", backup)
        # Auto 按整栋房间的相对误差简化，细杆和立体字远小于误差容限，会在
        # 回退几何中被删除。指定 100% 三角形并关闭误差简化，强制保留这些面。
        for name, value in targets.items():
            settings.set_editor_property(name, value)
        # apply_changes=True 触发完整构建；仅修改属性并保存不能证明渲染缓冲更新。
        editor.set_nanite_settings(mesh, settings, True)
    after = snapshot(mesh, editor)
    report = {"asset": ASSET, "changed": changed, "before": before, "after": after,
              "protected_equal": before["protected"] == after["protected"],
              "full_source_triangles": after["fallback_triangles"] == after["protected"]["source"]["triangles"]}
    (OUTPUT / "repair_report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    if not report["protected_equal"] or not report["full_source_triangles"]:
        raise RuntimeError("构建校验失败，未保存；请检查 " + str(OUTPUT / "repair_report.json"))
    if changed and not unreal.EditorAssetLibrary.save_loaded_asset(mesh):
        raise RuntimeError("房间资源保存失败")
    unreal.log("ROOM_FALLBACK_REPAIRED " + str(OUTPUT / "repair_report.json"))


if __name__ == "__main__":
    try:
        main()
    finally:
        # 无论是否发生异常都退出专用验收进程，不保存失败的临时调整。
        unreal.SystemLibrary.quit_editor()
