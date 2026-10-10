"""GPU render verification for TEST without saving scene or asset changes."""

import json
import math
from pathlib import Path
import time
import traceback

import unreal


ROOT = Path(unreal.Paths.project_saved_dir()).resolve() / "ArtRepair"
OUTPUT = ROOT / "Render-final"
OUTPUT.mkdir(parents=True, exist_ok=True)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level("/Game/0_/Maps/TEST"):
    raise RuntimeError("Could not load TEST")
editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
actors = editor.get_all_level_actors()
compile_errors = {}
materials = set()
for actor in actors:
    for mesh in actor.get_components_by_class(unreal.StaticMeshComponent):
        for i in range(mesh.get_num_materials()):
            material = mesh.get_material(i)
            if material:
                materials.add(material.get_base_material())
    for decal in actor.get_components_by_class(unreal.DecalComponent):
        material = decal.get_editor_property("decal_material")
        if material:
            materials.add(material.get_base_material())
for material in materials:
    errors = list(unreal.MaterialEditingLibrary.recompile_material(material))
    if errors:
        compile_errors[material.get_path_name()] = errors


def overview():
    low = [float("inf")] * 3
    high = [float("-inf")] * 3
    for actor in actors:
        if not actor.get_class().get_path_name().startswith("/Game/0_/Blueprints/Level/"):
            continue
        origin, extent = actor.get_actor_bounds(False, True)
        for i, axis in enumerate(("x", "y", "z")):
            low[i] = min(low[i], getattr(origin, axis) - getattr(extent, axis))
            high[i] = max(high[i], getattr(origin, axis) + getattr(extent, axis))
    target = unreal.Vector(*[(a + b) * 0.5 for a, b in zip(low, high)])
    yaw = math.radians(-125.0)
    elevation = math.radians(18.0)
    outward = [math.cos(elevation) * math.cos(yaw), math.cos(elevation) * math.sin(yaw), math.sin(elevation)]
    forward = [-x for x in outward]
    right = [-math.sin(yaw), math.cos(yaw), 0.0]
    up = [-math.sin(elevation) * math.cos(yaw), -math.sin(elevation) * math.sin(yaw), math.cos(elevation)]
    tan_h = math.tan(math.radians(20.0))
    tan_v = tan_h / (16.0 / 9.0)
    distance = 0.0
    for x in (low[0], high[0]):
        for y in (low[1], high[1]):
            for z in (low[2], high[2]):
                relative = [x - target.x, y - target.y, z - target.z]
                depth = sum(a * b for a, b in zip(relative, forward))
                distance = max(distance,
                    abs(sum(a * b for a, b in zip(relative, right))) / tan_h - depth,
                    abs(sum(a * b for a, b in zip(relative, up))) / tan_v - depth)
    distance *= 1.1
    position = unreal.Vector(target.x + outward[0] * distance, target.y + outward[1] * distance, target.z + outward[2] * distance)
    return position, unreal.MathLibrary.find_look_at_rotation(position, target), 40.0


overview_position, overview_rotation, overview_fov = overview()
room = next(a for a in actors if a.get_name() == "Rom_C_1")
room_origin = room.get_actor_location()
interior_position = room_origin + unreal.Vector(380, -1000, 610)
interior_target = room_origin + unreal.Vector(-60, -680, 490)
cases = [
    ("Overview", overview_position, overview_rotation, overview_fov),
    ("Exterior", room_origin + unreal.Vector(-1450, -3100, 1380),
     unreal.MathLibrary.find_look_at_rotation(room_origin + unreal.Vector(-1450, -3100, 1380), room_origin + unreal.Vector(0, -500, 600)), 65.0),
    ("Interior", interior_position, unreal.MathLibrary.find_look_at_rotation(interior_position, interior_target), 80.0),
]
camera = editor.spawn_actor_from_class(unreal.CameraActor, overview_position, overview_rotation)
level.editor_set_game_view(True)
level.pilot_level_actor(camera)
level.set_exact_camera_view(True)
unreal.SystemLibrary.execute_console_command(world, "r.ScreenPercentage 100")
unreal.SystemLibrary.execute_console_command(world, "r.RayTracing.ForceAllRayTracingEffects 0")
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state = {"index": 0, "task": None, "changed": time.monotonic(), "done": False}
started = time.monotonic()


def configure(index):
    name, position, rotation, fov = cases[index]
    camera.set_actor_location_and_rotation(position, rotation, False, False)
    camera.camera_component.set_editor_property("field_of_view", fov)
    camera.camera_component.set_editor_property("constrain_aspect_ratio", False)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(position, rotation)
    state["changed"] = time.monotonic()
    state["task"] = None
    unreal.log("ART_CAPTURE camera " + name)


def finish(error=None):
    if state["done"]:
        return
    state["done"] = True
    result = {
        "map": "/Game/0_/Maps/TEST",
        "base_materials_compiled": len(materials),
        "compile_errors": compile_errors,
        "screenshots": [str(OUTPUT / (c[0] + ".png")) for c in cases if (OUTPUT / (c[0] + ".png")).is_file()],
        "error": error,
        "scene_saved": False,
    }
    (ROOT / "render-verification.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    unreal.unregister_slate_post_tick_callback(callback)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.log("ART_CAPTURE finished " + str(result))
    unreal.SystemLibrary.quit_editor()


def tick(delta):
    try:
        if time.monotonic() - started > 300:
            finish("GPU capture timeout")
            return
        if state["task"] is None and time.monotonic() - state["changed"] > 15:
            unreal.AutomationLibrary.finish_loading_before_screenshot()
            path = OUTPUT / (cases[state["index"]][0] + ".png")
            state["capture_requested"] = time.time()
            state["task"] = unreal.AutomationLibrary.take_high_res_screenshot(1600, 900, str(path), camera=camera, delay=1.0, force_game_view=True)
            if state["task"] is None:
                finish("Screenshot task unavailable")
        elif state["task"] is not None and state["task"].is_task_done():
            path = OUTPUT / (cases[state["index"]][0] + ".png")
            if not path.is_file() or path.stat().st_mtime < state["capture_requested"]:
                finish("Missing or stale screenshot " + str(path))
                return
            state["index"] += 1
            if state["index"] == len(cases):
                finish("Material compilation failed" if compile_errors else None)
            else:
                configure(state["index"])
    except Exception:
        finish(traceback.format_exc())


configure(0)
callback = unreal.register_slate_post_tick_callback(tick)
