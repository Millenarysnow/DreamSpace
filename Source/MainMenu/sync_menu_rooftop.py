# -*- coding: utf-8 -*-
"""增量同步 TEST 的天台出生点，不重建菜单网格、材质，也不保存 TEST。

在编译好的独立 UE Python commandlet 中运行。唯一写入是菜单展示 Actor 的
天台取景参数与左右布局；后续策划移动 PlayerStart 后可再次同步。
"""
import json
from pathlib import Path
import unreal

MENU = "/Game/DreamPresentation/MainMenu/Maps/L_DreamMainMenu"
GAMEPLAY = "/Game/0_/Maps/TEST"
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not level.load_level(GAMEPLAY):
    raise RuntimeError("无法加载 TEST")
starts = [actor for actor in editor.get_all_level_actors() if isinstance(actor, unreal.PlayerStart)]
if len(starts) != 1:
    raise RuntimeError("当前同步要求 TEST 有唯一 PlayerStart，实际数量：%d" % len(starts))
player_location = starts[0].get_actor_location()
player_rotation = starts[0].get_actor_rotation()
if not level.load_level(MENU):
    raise RuntimeError("无法加载菜单")
scenes = [actor for actor in editor.get_all_level_actors() if actor.actor_has_tag("DreamMainMenuScene")]
if len(scenes) != 1:
    raise RuntimeError("菜单应有唯一 DreamMainMenuScene")
scene = scenes[0]
# 现有菜单快照直接沿用 TEST 原始建筑坐标，没有缩放；仅展示时围绕 Pivot 自转。
# 记录未自转的坐标系，用它把真实天台映射到展示角度，再在推进期间平滑归位。
scene.modify()
scene.set_editor_property("gameplay_building_transform", scene.get_actor_transform())
scene.set_editor_property("rooftop_player_location", player_location)
scene.set_editor_property("rooftop_player_rotation", player_rotation)
scene.set_editor_property("logo_on_right", True)
scene.update_camera(16.0 / 9.0)
if not level.save_current_level():
    raise RuntimeError("无法保存菜单天台参数")
output = Path(unreal.Paths.project_saved_dir()) / "MainMenu" / "rooftop_sync.json"
output.write_text(json.dumps({"menu": MENU, "gameplay": GAMEPLAY,
                              "player_start": [player_location.x, player_location.y, player_location.z],
                              "rotation": [player_rotation.pitch, player_rotation.yaw, player_rotation.roll],
                              "logo_on_right": True}, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.log("DREAM_ROOFTOP_SYNC " + str(output))
