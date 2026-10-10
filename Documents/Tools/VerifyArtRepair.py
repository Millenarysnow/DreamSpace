"""Compare saved Unreal audits and original file hashes after the art repair."""

import hashlib
import json
from pathlib import Path
import re


PROJECT = Path(__file__).resolve().parents[2]
ROOT = PROJECT / "Saved" / "ArtRepair"
VISUAL = {"mesh", "materials", "overrides", "decal_material", "child_actor_class", "disallow_nanite"}
TRANSLUCENT_NANITE_MESHES = {
    "/Game/0_/Meshes/Upstairs/dainti.dainti",
    "/Game/0_/Meshes/Doors/Door_01/Geometry/Door_02.Door_02",
}


def read(name):
    return json.loads((ROOT / name).read_text(encoding="utf-8-sig"))


def file_hash(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest().upper()


def normalized(value):
    if isinstance(value, str):
        def round_velocity(match):
            return "max_angular_velocity: {:.3f}".format(float(match[1]))
        return re.sub(r"max_angular_velocity: ([0-9.]+)", round_velocity, value)
    if isinstance(value, dict):
        return {k: normalized(v) for k, v in value.items()}
    if isinstance(value, list):
        return [normalized(v) for v in value]
    return value


def compare_record(before, after, label, failures):
    for key, value in before.items():
        if key in VISUAL or key == "components":
            continue
        if key not in after or normalized(value) != normalized(after[key]):
            failures.append({"object": label, "field": key, "before": value, "after": after.get(key)})


def compare_components(before, after, label, failures, identity="name"):
    targets = {c[identity]: c for c in after}
    checked = 0
    for component in before:
        key = component[identity]
        current = targets.get(key)
        if current is None:
            failures.append({"object": label, "removed_component": key})
            continue
        compare_record(component, current, label + "/" + component["name"], failures)
        checked += 1
    additions = sorted(set(targets) - {c[identity] for c in before})
    return checked, additions


def text_export(path):
    raw = path.read_bytes()
    text = raw.decode("utf-16") if raw[:2] in {b"\xff\xfe", b"\xfe\xff"} else raw.decode("utf-8-sig")
    stack = []
    objects = {}
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("Begin "):
            export = re.search(r'ExportPath="([^"]+)"', line)
            name = re.search(r'Name="?([^" ]+)', line)
            key = export[1] if export else "/".join(stack) + "/" + (name[1] if name else line)
            stack.append(key)
            objects.setdefault(key, [])
        elif line.startswith("End "):
            if stack:
                stack.pop()
        elif stack and line:
            objects[stack[-1]].append(line)
    return {k: sorted(v) for k, v in objects.items()}


def compiler_generated_objects(objects):
    intermediate_nodes = {key for key, lines in objects.items() if "bIsIntermediateNode=True" in lines}
    generated = set()
    for key in objects:
        if not key.startswith("/Script/Engine.EdGraph'"):
            continue
        graph_path = key.split("'", 1)[1][:-1]
        children = {child for child in objects if "'" in child
                    and child.split("'", 1)[1].startswith(graph_path + ".")}
        if children and children.issubset(intermediate_nodes):
            generated.update(children)
            generated.add(key)
    return generated


def main():
    before = read("before.json")
    resources = read("resources.json")
    after_stage = "after-final" if (ROOT / "after-final.json").is_file() else "after"
    after = read(after_stage + ".json")
    reference = read("reference.json")
    failures = []
    actors = {a["name"]: a for a in after["actors"]}
    components_checked = 0
    new_components = []
    for actor in before["actors"]:
        current = actors.get(actor["name"])
        if current is None:
            failures.append({"removed_actor": actor["name"]})
            continue
        compare_record(actor, current, actor["name"], failures)
        checked, additions = compare_components(actor["components"], current["components"], actor["name"], failures)
        components_checked += checked
        new_components.extend({"actor": actor["name"], "component": c} for c in additions)
    blueprint_components_checked = 0
    for package, blueprint in resources["blueprints"].items():
        current = after["blueprints"].get(package)
        if current is None:
            failures.append({"removed_blueprint": package})
            continue
        if blueprint["parent"] != current["parent"]:
            failures.append({"changed_parent": package, "before": blueprint["parent"], "after": current["parent"]})
        checked, _ = compare_components(blueprint["components"], current["components"], package, failures, identity="path")
        blueprint_components_checked += checked
    original_hashes = {e["RelativePath"].replace("\\", "/"): e["SHA256"] for e in read("original-file-hashes.json")}
    graph_differences = []
    ignored_compiler_graph_differences = []
    for path in (ROOT / "resources-exports").glob("*Blueprints*.t3d"):
        target = ROOT / (after_stage + "-exports") / path.name
        if target.is_file():
            original = text_export(path)
            current = text_export(target)
            root = next(k for k in original if k.startswith("/Script/Engine.Blueprint'"))
            package = root.split("'", 1)[1].split(".", 1)[0]
            relative = "Content/" + package[6:] + ".uasset"
            # Byte-identical assets prove their source graphs are unchanged;
            # transient compiler graphs get fresh pin GUIDs on every load.
            if original_hashes.get(relative) == file_hash(PROJECT / relative):
                continue
            generated = compiler_generated_objects(original) & compiler_generated_objects(current)
            for key in sorted(set(original) | set(current)):
                if original.get(key) != current.get(key):
                    # These graphs contain only compiler intermediate nodes;
                    # their temporary pin GUIDs are regenerated on every load.
                    if key in generated:
                        ignored_compiler_graph_differences.append({"file": path.name, "object": key})
                        continue
                    graph_differences.append({"file": path.name, "object": key,
                                              "before": original.get(key), "after": current.get(key)})
    changed_originals = []
    current_hashes = {}
    backup_hash_failures = []
    for entry in read("original-file-hashes.json"):
        path = PROJECT / entry["RelativePath"]
        digest = file_hash(path)
        relative = entry["RelativePath"].replace("\\", "/")
        current_hashes[relative] = digest
        if digest != entry["SHA256"]:
            changed_originals.append(entry["RelativePath"])
        if file_hash(ROOT / "Backup-original" / entry["RelativePath"]) != entry["SHA256"]:
            backup_hash_failures.append(entry["RelativePath"])
    expected_changed = {".gitignore", "Content/0_/Maps/TEST.umap"}
    for receipt in ("repair-blueprints.json", "repair-rendering.json"):
        if (ROOT / receipt).is_file():
            expected_changed.update("Content/" + package[6:] + ".uasset"
                                    for package in read(receipt)["saved"] if package != after["map"])
    unexpected_changed_originals = [p for p in changed_originals
                                   if p.replace("\\", "/") not in expected_changed]
    original_meshes = ["Content/" + asset["package"][6:] + ".uasset" for asset in after["all_assets"]
                       if asset["class"] == "/Script/Engine.StaticMesh"
                       and "Content/" + asset["package"][6:] + ".uasset" in original_hashes]
    changed_original_meshes = [p for p in original_meshes if current_hashes[p] != original_hashes[p]]
    copied_dependencies = read("dependency-plan.json")
    copied_dependency_failures = [entry["RelativePath"] for entry in copied_dependencies
                                  if file_hash(PROJECT / entry["RelativePath"]) != entry["SHA256"]]
    visual_mismatches = []
    nanite_components_checked = []
    preserved_slot_differences = []
    source_actors = {a["name"]: a for a in reference["actors"]}
    for actor in after["actors"]:
        source = source_actors.get(actor["name"])
        if not source:
            continue
        source_components = {c["name"]: c for c in source["components"]}
        for component in actor["components"]:
            if component.get("mesh") in TRANSLUCENT_NANITE_MESHES:
                nanite_components_checked.append({"actor": actor["name"], "component": component["name"],
                                                 "disallow_nanite": component.get("disallow_nanite")})
                if not component.get("disallow_nanite"):
                    visual_mismatches.append({"actor": actor["name"], "component": component["name"],
                                              "field": "disallow_nanite", "current": False, "reference": True})
            name = component["name"]
            intended = source_components.get("w" if name == "w_0" else name)
            if not intended or component["class"] != intended["class"]:
                continue
            for key in ("mesh", "decal_material", "materials", "child_actor_class"):
                if key in intended and component.get(key) != intended[key]:
                    if (key == "materials" and component.get("mesh") == intended.get("mesh")
                            and len(component[key]) < len(intended[key])
                            and component[key] == intended[key][:len(component[key])]):
                        relative = "Content/" + component["mesh"].split(".", 1)[0][6:] + ".uasset"
                        if original_hashes.get(relative) == file_hash(PROJECT / relative):
                            preserved_slot_differences.append({"actor": actor["name"], "component": name,
                                "current_slots": len(component[key]), "art_slots": len(intended[key]),
                                "mesh_unchanged": True})
                            continue
                    visual_mismatches.append({"actor": actor["name"], "component": component["name"],
                                              "field": key, "current": component.get(key), "reference": intended[key]})
    report = {
        "missing_before": len(before["missing"]),
        "missing_after": len(after["missing"]),
        "audit_stage": after_stage,
        "copied_dependencies_checked": len(copied_dependencies),
        "copied_dependency_hash_failures": copied_dependency_failures,
        "backup_files_checked": len(original_hashes),
        "backup_hash_failures": backup_hash_failures,
        "original_static_meshes_checked": len(original_meshes),
        "changed_original_meshes": changed_original_meshes,
        "actors_before": len(before["actors"]),
        "actors_after": len(after["actors"]),
        "existing_actors_checked": len(before["actors"]),
        "existing_components_checked": components_checked,
        "blueprint_components_checked": blueprint_components_checked,
        "original_blueprint_parents_checked": len(resources["blueprints"]),
        "protected_property_failures": failures,
        "graph_differences": graph_differences,
        "ignored_compiler_graph_differences": ignored_compiler_graph_differences,
        "new_components_on_existing_actors": new_components,
        "changed_original_files": changed_originals,
        "unexpected_changed_originals": unexpected_changed_originals,
        "visual_mismatches": visual_mismatches,
        "translucent_nanite_components_checked": nanite_components_checked,
        "preserved_mesh_material_slot_differences": preserved_slot_differences,
        "max_angular_velocity_comparison_precision": "0.001 degrees/s (UE serialization rounds 3600 to 3599.999756)",
    }
    (ROOT / "verification.json").write_text(json.dumps(report, ensure_ascii=True, indent=2), encoding="utf-8")
    print(json.dumps({k: len(v) if isinstance(v, list) else v for k, v in report.items()}, indent=2))
    if (failures or after["missing"] or graph_differences or new_components
            or visual_mismatches or copied_dependency_failures or backup_hash_failures
            or unexpected_changed_originals or changed_original_meshes):
        raise RuntimeError("Repair verification needs inspection; see verification.json")


if __name__ == "__main__":
    main()
