"""Read-only Unreal audit for TEST and its recursive package dependencies."""

import json
import os
import re
from pathlib import Path

import unreal


MAP = "/Game/0_/Maps/TEST"
STAGE = os.environ.get("DREAMSPACE_ART_AUDIT_STAGE", "before")
OUTPUT_DIR = Path(os.environ.get(
    "DREAMSPACE_ART_AUDIT_OUTPUT_DIR", str(Path(unreal.Paths.project_saved_dir()) / "ArtRepair")))
OUTPUT = OUTPUT_DIR / (STAGE + ".json")


def path_of(value):
    return value.get_path_name() if value else None


def property_text(obj, name):
    try:
        value = obj.get_editor_property(name)
        if value is None:
            return None
        if isinstance(value, unreal.Object):
            return path_of(value)
        return re.sub(r"0x[0-9A-Fa-f]+", "ADDRESS", str(value))
    except Exception:
        return None


def vector(value):
    return [value.x, value.y, value.z]


def rotation(value):
    return [value.pitch, value.yaw, value.roll]


def asset_class_path(asset):
    return str(asset.asset_class_path.package_name) + "." + str(asset.asset_class_path.asset_name)


def component_snapshot(component):
    result = {
        "name": component.get_name(),
        "path": path_of(component),
        "class": path_of(component.get_class()),
        "properties": {},
    }
    properties = ["component_tags", "is_editor_only"]
    if isinstance(component, unreal.SceneComponent):
        result.update({
            "location": vector(component.get_editor_property("relative_location")),
            "rotation": rotation(component.get_editor_property("relative_rotation")),
            "scale": vector(component.get_editor_property("relative_scale3d")),
            "attach_parent": path_of(component.get_attach_parent()),
        })
        properties += ["mobility", "absolute_location", "absolute_rotation", "absolute_scale"]
    if isinstance(component, unreal.PrimitiveComponent):
        result["collision_enabled"] = str(component.get_collision_enabled())
        result["collision_profile"] = str(component.get_collision_profile_name())
        result["collision_object_type"] = str(component.get_collision_object_type())
        result["collision_responses"] = {
            channel: str(component.get_collision_response_to_channel(getattr(unreal.CollisionChannel, channel)))
            for channel in dir(unreal.CollisionChannel)
            if channel.startswith("ECC_") and channel != "ECC_MAX"
        }
        properties += ["body_instance", "generate_overlap_events", "can_ever_affect_navigation"]
    if isinstance(component, unreal.StaticMeshComponent):
        result["mesh"] = path_of(component.get_editor_property("static_mesh"))
        result["materials"] = [path_of(component.get_material(i)) for i in range(component.get_num_materials())]
        result["overrides"] = [path_of(m) for m in component.get_editor_property("override_materials")]
        result["disallow_nanite"] = component.get_editor_property("disallow_nanite")
    if isinstance(component, unreal.DecalComponent):
        result["decal_material"] = path_of(component.get_editor_property("decal_material"))
        properties += ["decal_size", "sort_order", "fade_screen_size"]
    if isinstance(component, unreal.BoxComponent):
        properties += ["box_extent"]
    if isinstance(component, unreal.SphereComponent):
        properties += ["sphere_radius"]
    if isinstance(component, unreal.CapsuleComponent):
        properties += ["capsule_radius", "capsule_half_height"]
    if isinstance(component, unreal.ChildActorComponent):
        result["child_actor_class"] = property_text(component, "child_actor_class")
    for name in properties:
        value = property_text(component, name)
        if value is not None:
            result["properties"][name] = value
    return result


def main():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    options = unreal.AssetRegistryDependencyOptions(
        include_soft_package_references=True,
        include_hard_package_references=True,
        include_searchable_names=False,
        include_soft_management_references=False,
        include_hard_management_references=False,
    )
    content = Path(unreal.Paths.project_content_dir())
    queue = [MAP]
    packages = {}
    missing = {}
    while queue:
        package = queue.pop()
        if package in packages or not package.startswith("/Game/"):
            continue
        assets = registry.get_assets_by_package_name(package)
        dependencies = sorted(str(x) for x in (registry.get_dependencies(package, options) or []))
        exists = any((content / (package[6:] + ext)).is_file() for ext in (".uasset", ".umap"))
        packages[package] = {
            "exists": exists,
            "assets": [{"name": str(a.asset_name), "class": asset_class_path(a)} for a in assets],
            "dependencies": dependencies,
        }
        for dependency in dependencies:
            if dependency.startswith("/Game/"):
                queue.append(dependency)
                if not any((content / (dependency[6:] + ext)).is_file() for ext in (".uasset", ".umap")):
                    missing.setdefault(dependency, []).append(package)

    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level.load_level(MAP):
        raise RuntimeError("Could not load " + MAP)
    actors = []
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in subsystem.get_all_level_actors():
        actors.append({
            "name": actor.get_name(),
            "label": actor.get_actor_label(),
            "class": path_of(actor.get_class()),
            "guid": property_text(actor, "actor_guid"),
            "location": vector(actor.get_actor_location()),
            "rotation": rotation(actor.get_actor_rotation()),
            "scale": vector(actor.get_actor_scale3d()),
            "attach_parent": path_of(actor.get_attach_parent_actor()),
            "tags": property_text(actor, "tags"),
            "components": sorted(
                (component_snapshot(c) for c in actor.get_components_by_class(unreal.ActorComponent)),
                key=lambda c: c["name"],
            ),
        })
    blueprints = {}
    subobjects = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    subobject_library = unreal.SubobjectDataBlueprintFunctionLibrary
    for package, info in packages.items():
        if not any(a["class"] == "/Script/Engine.Blueprint" for a in info["assets"]):
            continue
        blueprint = unreal.load_asset(package)
        if blueprint is None:
            raise RuntimeError("Could not load blueprint " + package)
        templates = {}
        for handle in subobjects.k2_gather_subobject_data_for_blueprint(blueprint):
            data = subobject_library.get_data(handle)
            obj = subobject_library.get_object(data)
            if isinstance(obj, unreal.ActorComponent):
                templates[path_of(obj)] = component_snapshot(obj)
        asset_data = registry.get_assets_by_package_name(package)[0]
        blueprints[package] = {
            "parent": asset_data.get_tag_value("ParentClass"),
            "components": sorted(templates.values(), key=lambda c: c["path"]),
        }
    if os.environ.get("DREAMSPACE_ART_AUDIT_EXPORT") == "1":
        export_dir = OUTPUT_DIR / (STAGE + "-exports")
        export_dir.mkdir(parents=True, exist_ok=True)
        targets = [(MAP, unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world())]
        targets += [(p, unreal.load_asset(p)) for p in blueprints if p.startswith("/Game/0_/Blueprints/Level/")]
        for package, obj in targets:
            task = unreal.AssetExportTask()
            task.object = obj
            task.filename = str(export_dir / (package[6:].replace("/", "_") + ".t3d"))
            task.automated = True
            task.prompt = False
            task.replace_identical = True
            task.exporter = unreal.LevelExporterT3D() if isinstance(obj, unreal.World) else unreal.ObjectExporterT3D()
            if not unreal.Exporter.run_asset_export_task(task):
                raise RuntimeError("Export failed: " + package + " " + str(task.errors))
    result = {
        "map": MAP,
        "stage": STAGE,
        "packages": dict(sorted(packages.items())),
        "missing": dict(sorted(missing.items())),
        "actors": sorted(actors, key=lambda a: a["name"]),
        "blueprints": dict(sorted(blueprints.items())),
        "all_assets": [
            {"package": str(a.package_name), "name": str(a.asset_name), "class": asset_class_path(a)}
            for a in registry.get_assets_by_path("/Game", recursive=True)
        ],
    }
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(json.dumps(result, ensure_ascii=True, indent=2), encoding="utf-8")
    unreal.log("ART_AUDIT {}: actors={}, packages={}, missing={}, output={}".format(
        STAGE, len(actors), len(packages), len(missing), OUTPUT))


if __name__ == "__main__":
    main()
