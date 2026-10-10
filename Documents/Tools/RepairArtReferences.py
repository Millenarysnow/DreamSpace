"""Restore audited visual references and component Nanite compatibility."""

import json
import os
from pathlib import Path
import sys

import unreal

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from AuditArtRepair import component_snapshot, path_of


ROOT = Path(unreal.Paths.project_saved_dir()) / "ArtRepair"
REFERENCE = json.loads((ROOT / "reference.json").read_text(encoding="utf-8-sig"))
MODE = os.environ.get("DREAMSPACE_ART_REPAIR_MODE", "plan")
RENDER_KEYS = {"mesh", "materials", "overrides", "decal_material", "child_actor_class", "disallow_nanite"}
TRANSLUCENT_NANITE_MESHES = {
    "/Game/0_/Meshes/Upstairs/dainti.dainti",
    "/Game/0_/Meshes/Doors/Door_01/Geometry/Door_02.Door_02",
}
CHANGES = []
SKIPPED = []


def find_component(source_components, name, component):
    candidate = source_components.get(name)
    if candidate and candidate["class"] == path_of(component.get_class()):
        return candidate
    # Reparenting Rom introduced an inherited mesh named w. UE renamed the
    # existing art decal w_0 to keep both components.
    suffix = "_GEN_VARIABLE"
    bare_name = name[:-len(suffix)] if name.endswith(suffix) else name
    if bare_name == "w_0":
        original_name = "w" + (suffix if name.endswith(suffix) else "")
        candidate = source_components.get(original_name)
        if candidate and candidate["class"] == path_of(component.get_class()):
            return candidate
    return None


def protected(snapshot):
    return {k: v for k, v in snapshot.items() if k not in RENDER_KEYS}


def templates(blueprint):
    result = {}
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    library = unreal.SubobjectDataBlueprintFunctionLibrary
    for handle in subsystem.k2_gather_subobject_data_for_blueprint(blueprint):
        obj = library.get_object(library.get_data(handle))
        if isinstance(obj, unreal.ActorComponent):
            result[path_of(obj)] = obj
    return result


def load_reference(path, expected_type):
    obj = unreal.load_class(None, path) if expected_type is unreal.Class else unreal.load_asset(path)
    if obj is None or not isinstance(obj, expected_type):
        raise RuntimeError("Invalid reference {} (expected {})".format(path, expected_type))
    return obj


def patch(component, source, apply):
    before = component_snapshot(component)
    assignments = []
    for key, property_name, expected_type in [
        ("mesh", "static_mesh", unreal.StaticMesh),
        ("decal_material", "decal_material", unreal.MaterialInterface),
        ("child_actor_class", "child_actor_class", unreal.Class),
    ]:
        if key not in before or key not in source:
            continue
        current = before[key]
        intended = source[key]
        if current == "None":
            current = None
        if intended == "None":
            intended = None
        if current == intended or not intended:
            continue
        if current:
            SKIPPED.append({"object": path_of(component), "field": key, "current": current, "reference": intended})
            continue
        assignments.append((property_name, intended, expected_type))
    if apply and assignments:
        component.modify()
    for property_name, intended, expected_type in assignments:
        CHANGES.append({"object": path_of(component), "field": property_name, "before": None, "after": intended})
        if apply:
            component.set_editor_property(property_name, load_reference(intended, expected_type))

    if isinstance(component, unreal.StaticMeshComponent):
        overrides = list(component.get_editor_property("override_materials"))
        for index, intended in enumerate(source.get("materials", [])):
            if not intended or not intended.startswith("/Game/"):
                continue
            current = path_of(component.get_material(index))
            if current == intended:
                continue
            override = path_of(overrides[index]) if index < len(overrides) else None
            if override:
                SKIPPED.append({"object": path_of(component), "field": "material[{}]".format(index),
                                "current": current, "reference": intended})
                continue
            # A restored mesh already supplies its own default material. Only
            # restore an override when its intended effective material differs.
            if not apply and before.get("mesh") is None and not source.get("overrides", []):
                continue
            CHANGES.append({"object": path_of(component), "field": "material[{}]".format(index),
                            "before": current, "after": intended})
            if apply:
                component.modify()
                component.set_material(index, load_reference(intended, unreal.MaterialInterface))
    if apply and protected(before) != protected(component_snapshot(component)):
        raise RuntimeError("Non-render property changed: " + path_of(component))


def patch_nanite(component):
    if not isinstance(component, unreal.StaticMeshComponent):
        return
    before = component_snapshot(component)
    if before["mesh"] not in TRANSLUCENT_NANITE_MESHES or before["disallow_nanite"]:
        return
    mesh = component.get_editor_property("static_mesh")
    if not mesh.get_editor_property("nanite_settings").get_editor_property("enabled"):
        raise RuntimeError("Expected a Nanite mesh: " + before["mesh"])
    translucent = [component.get_material(i) for i in range(component.get_num_materials())
                   if component.get_material(i) and component.get_material(i).get_base_material()
                   .get_editor_property("blend_mode") == unreal.BlendMode.BLEND_TRANSLUCENT]
    if not translucent:
        raise RuntimeError("Expected translucent material: " + path_of(component))
    component.modify()
    component.set_editor_property("disallow_nanite", True)
    after = component_snapshot(component)
    if {k: v for k, v in before.items() if k != "disallow_nanite"} != {
            k: v for k, v in after.items() if k != "disallow_nanite"}:
        raise RuntimeError("Unexpected change while disabling Nanite: " + path_of(component))
    CHANGES.append({"object": path_of(component), "field": "disallow_nanite",
                    "before": False, "after": True, "mesh": before["mesh"],
                    "reason": "Nanite does not support the existing translucent material"})


def main():
    if MODE not in {"plan", "blueprints", "map", "rendering"}:
        raise RuntimeError("Invalid repair mode " + MODE)
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    saved = []
    if MODE in {"plan", "blueprints", "rendering"}:
        for package, source in REFERENCE["blueprints"].items():
            if not package.startswith("/Game/0_/Blueprints/Level/"):
                continue
            if not unreal.EditorAssetLibrary.does_asset_exist(package):
                continue
            blueprint = unreal.load_asset(package)
            objects = templates(blueprint)
            own = {p: c for p, c in objects.items() if p.startswith(package + ".")}
            original = {p: protected(component_snapshot(c)) for p, c in own.items()}
            source_components = {c["name"]: c for c in source["components"] if c["path"].startswith(package + ".")}
            start = len(CHANGES)
            for path, component in own.items():
                if MODE == "rendering":
                    patch_nanite(component)
                    continue
                reference_component = find_component(source_components, component.get_name(), component)
                if reference_component:
                    patch(component, reference_component, MODE == "blueprints")
            if MODE in {"blueprints", "rendering"} and len(CHANGES) > start:
                blueprint.modify()
                if not unreal.BlueprintEditorLibrary.compile_blueprint(blueprint):
                    raise RuntimeError("Blueprint compilation failed: " + package)
                current = {p: protected(component_snapshot(c)) for p, c in templates(blueprint).items()
                           if p.startswith(package + ".")}
                if current != original:
                    (ROOT / "invariant-failure.json").write_text(json.dumps({"package": package,
                        "before": original, "after": current}, indent=2), encoding="utf-8")
                    raise RuntimeError("Blueprint changed protected component properties: " + package)
                if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
                    raise RuntimeError("Blueprint save failed: " + package)
                saved.append(package)
                unreal.log("ART_REPAIR saved " + package)
    if MODE in {"plan", "map", "rendering"}:
        level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if not level.load_level("/Game/0_/Maps/TEST"):
            raise RuntimeError("Could not load TEST")
        references = {a["name"]: a for a in REFERENCE["actors"]}
        subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        start = len(CHANGES)
        for actor in subsystem.get_all_level_actors():
            if MODE == "rendering":
                for component in actor.get_components_by_class(unreal.StaticMeshComponent):
                    patch_nanite(component)
                continue
            source = references.get(actor.get_name())
            if not source or source["class"] != path_of(actor.get_class()):
                continue
            source_components = {c["name"]: c for c in source["components"]}
            for component in actor.get_components_by_class(unreal.ActorComponent):
                reference_component = find_component(source_components, component.get_name(), component)
                if reference_component:
                    patch(component, reference_component, MODE == "map")
        if MODE in {"map", "rendering"} and len(CHANGES) > start:
            if not level.save_current_level():
                raise RuntimeError("TEST save failed")
            saved.append("/Game/0_/Maps/TEST")
    result = {"mode": MODE, "changes": CHANGES, "skipped": SKIPPED, "saved": saved}
    receipt = ROOT / ("repair-" + MODE + ".json")
    if MODE != "plan" and receipt.exists():
        previous = json.loads(receipt.read_text(encoding="utf-8-sig"))
        result["changes"] = previous["changes"] + CHANGES
        result["saved"] = sorted(set(previous["saved"] + saved))
        result["skipped"] = previous["skipped"] + SKIPPED
    receipt.write_text(json.dumps(result, indent=2), encoding="utf-8")
    unreal.log("ART_REPAIR {}: changes={}, saved={}, preserved_nonempty_references={}".format(
        MODE, len(CHANGES), len(saved), len(SKIPPED)))


main()
