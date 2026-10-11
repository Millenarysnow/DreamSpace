# -*- coding: utf-8 -*-
"""在独立编辑器进程里用真实 PIE 和音频混音器验收 BGM，不保存关卡。

运行时需要声音开启，不能加 -nosound。脚本检查实际播放组件、曲尾循环、Slate 真实点击、
重复开始请求、淡出和自然结束、退出 / 重启 PIE、直接运行 TEST；同时导出混音 WAV，
便于检查“组件显示播放”但没有输出声音的情况。报告写入 Saved/Audio/verification_report.json。
"""

import json
import math
import time
import wave
from array import array
from pathlib import Path

import unreal


OUTPUT = Path(unreal.Paths.project_saved_dir()).resolve() / "Audio"
OUTPUT.mkdir(parents=True, exist_ok=True)
MENU = "/Game/DreamPresentation/MainMenu/Maps/L_DreamMainMenu"
GAME = "/Game/0_/Maps/TEST"
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {"phase": "warmup", "at": time.monotonic(), "started": time.monotonic(),
         "checks": {}, "finished": False, "recording": False}
play_settings = unreal.get_default_object(unreal.load_class(None, "/Script/UnrealEd.LevelEditorPlaySettings"))
original_audio_settings = {name: play_settings.get_editor_property(name)
                           for name in ("EnableGameSound", "SoloAudioInFirstPIEClient")}
menu_defaults = unreal.get_default_object(unreal.DreamMainMenuPlayerController)
original_fade_out = menu_defaults.get_editor_property("fade_out_duration")
misc_settings = unreal.get_default_object(unreal.load_class(None, "/Script/UnrealEd.LevelEditorMiscSettings"))
original_background_audio = misc_settings.get_editor_property("bAllowBackgroundAudio")

# RenderOffscreen 没有窗口获得焦点，UE 会静音非活动音频设备。
# 仅在本验收进程里将第一份 PIE 设备设为独奏并开启游戏声音，保证录到真正的混音输出。
# 这些默认对象改动不保存配置文件；finish 会还原，正常游戏不需要强制夺取音频焦点。
for setting in original_audio_settings:
    play_settings.set_editor_property(setting, True)
# 编辑器 Tick 还有独立的后台音频开关：失焦时会直接把 FApp 音量设成 0，不能只覆盖 UnfocusedVolumeMultiplier。
misc_settings.set_editor_property("bAllowBackgroundAudio", True)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def game_world():
    return editor.get_game_world()


def audio_for(world):
    # UE 5.8 的 Python 不导出 BlueprintInternalUseOnly 的 GetGameInstanceSubsystem；
    # 通过实例所属 GameInstance 查找已经创建的原生子系统，不能自行 new 一份假管理器。
    instance = unreal.GameplayStatics.get_game_instance(world)
    matches = [item for item in unreal.ObjectIterator(unreal.DreamAudioSubsystem)
               if item.get_outer() == instance]
    require(len(matches) == 1, "当前 GameInstance 未安装唯一音频子系统")
    return matches[0]


def check_music(world, name):
    """读取真正被设备播放的声音和状态，而不是复述配置中的资源路径。"""
    audio = audio_for(world)
    component = audio.get_music_component()
    require(component is not None and component.is_playing(), "BGM 未实际播放：" + name)
    sound = component.get_editor_property("sound")
    require(sound.get_name() == name, "当前播放的 BGM 错误：" + sound.get_path_name())
    require(sound.get_editor_property("looping"), "BGM 资源未开启循环")
    require(not component.get_editor_property("allow_spatialization"), "BGM 意外开启空间化")
    return audio, component


def check_released(path):
    """结束 PIE 会 GC 掉 Python 包装的 UObject；通过路径查询，不能再调用已失效的旧包装对象。"""
    remaining = unreal.find_object(None, path)
    require(remaining is None, "结束 PIE 后音频组件仍存在：" + path)


def record_start(world, duration):
    unreal.AudioMixerLibrary.start_recording_output(world, duration)
    state["recording"] = True


def record_stop(world, name):
    unreal.AudioMixerLibrary.stop_recording_output(
        world, unreal.AudioRecordingExportType.WAV_FILE, name, str(OUTPUT))
    state["recording"] = False


def analyze_recording(name):
    """检查混音器导出的实际 PCM，组件 IsPlaying 不能替代非静音输出与曲尾后声音的证据。"""
    path = OUTPUT / (name + ".wav")
    require(path.is_file(), "没有导出混音录音：" + name)
    with wave.open(str(path), "rb") as recording:
        require(recording.getsampwidth() == 2, "混音录音不是预期的 16-bit PCM：" + name)
        channels = recording.getnchannels()
        rate = recording.getframerate()
        samples = array("h", recording.readframes(recording.getnframes()))
    require(len(samples) > 0, "混音录音为空：" + name)
    rms = math.sqrt(sum(value * value for value in samples) / len(samples)) / 32768.0
    tail = samples[-rate * channels:]
    tail_rms = math.sqrt(sum(value * value for value in tail) / len(tail)) / 32768.0
    peak = max(abs(value) for value in samples) / 32768.0
    require(rms > 0.0001 and tail_rms > 0.0001, "混音输出静音 / 曲尾之后没有声音：" + name)
    require(peak < 0.999, "混音输出发生削波：" + name)
    return {"file": name + ".wav", "duration_seconds": len(samples) / (rate * channels),
            "sample_rate": rate, "channels": channels, "rms": rms, "tail_rms": tail_rms, "peak": peak}


def phase(name):
    state["phase"] = name
    state["at"] = time.monotonic()
    unreal.log("DREAM_AUDIO_VERIFY_PHASE " + name)


def finish(error=None):
    """无论失败发生在哪一步，都停止本次 PIE 并保留明确报告，不能把失败当作用户听感验收通过。"""
    if state["finished"]:
        return
    state["finished"] = True
    if state["recording"] and game_world():
        record_stop(game_world(), "Interrupted")
    if level.is_in_play_in_editor():
        level.editor_request_end_play()
    for setting, value in original_audio_settings.items():
        play_settings.set_editor_property(setting, value)
    menu_defaults.set_editor_property("fade_out_duration", original_fade_out)
    misc_settings.set_editor_property("bAllowBackgroundAudio", original_background_audio)
    (OUTPUT / "verification_report.json").write_text(
        json.dumps({"passed": error is None, "error": error, "checks": state["checks"]},
                   ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    (unreal.log_error if error else unreal.log)("DREAM_AUDIO_VERIFY_RESULT " + str(error or "PASS"))


def tick(delta):
    try:
        now = time.monotonic()
        elapsed = now - state["at"]
        require(now - state["started"] < 240.0, "音频实机验收超时")
        current = state["phase"]
        if current == "warmup" and elapsed > 2.0:
            level.editor_request_begin_play()
            phase("menu")
        elif current == "menu" and elapsed > 3.0 and game_world():
            world = game_world()
            audio, music = check_music(world, "SW_MenuMusic")
            state["audio"] = audio
            state["old_menu_music"] = music
            state["menu_component_path"] = music.get_path_name()
            # 从真实 SoundWave 的末尾前一秒开始，等待越过曲尾，验证引擎循环而非计时器人工重播。
            duration = music.get_editor_property("sound").get_editor_property("duration")
            music.play(max(duration - 1.0, 0.0))
            record_start(world, 5.0)
            state["checks"]["menu_music"] = music.get_editor_property("sound").get_path_name()
            phase("menu_loop")
        elif current == "menu_loop" and elapsed > 4.0:
            world = game_world()
            check_music(world, "SW_MenuMusic")
            record_stop(world, "MenuLoop")
            state["checks"]["menu_loops_past_end"] = True
            phase("click")
        elif current == "click" and elapsed > 1.0:
            world = game_world()
            controller = unreal.GameplayStatics.get_player_controller(world, 0)
            record_start(world, 10.0)
            # 复用项目已有开发验收命令，在右下留白真实路由 Slate 左键，触发原始开始入口。
            unreal.SystemLibrary.execute_console_command(world, "dream.TestMenuClick 0.96 0.96")
            require(controller.is_starting_game(), "Slate 留白点击未触发开始")
            transition = state["audio"].get_transition_component()
            require(transition is not None and transition.is_playing(), "进入游戏音效未播放")
            state["transition"] = transition
            controller.request_start_game()
            controller.request_start_game()
            require(state["audio"].get_transition_component() == transition, "重复点击叠加了进入音效")
            state["checks"]["click_and_repeat_are_idempotent"] = True
            # 当帧读取，避免首次加载着色器造成长帧时，下一次 Python 回调已错过 0.65 秒淡出区间。
            require(state["old_menu_music"].get_play_state() == unreal.AudioComponentPlayState.FADING_OUT,
                    "菜单音乐没有进入淡出状态")
            state["checks"]["menu_fades_out"] = True
            state["click_at"] = now
            phase("travel")
        elif current == "travel":
            world = game_world()
            if world and world.get_outer().get_name().endswith("_TEST"):
                audio, music = check_music(world, "SW_GameplayMusic")
                require(audio == state["audio"], "切关后不是同一个 GameInstance 音频管理器")
                check_released(state["menu_component_path"])
                state["checks"]["old_menu_component_released"] = True
                state["game_component_path"] = music.get_path_name()
                # 时长约 3.53 秒。加载较快时可直接检查同一个音效组件仍在播放；加载较慢时它可能已自然结束。
                state["checks"]["travel_seconds"] = now - state["click_at"]
                effect = audio.get_transition_component()
                state["checks"]["transition_active_after_travel"] = bool(effect and effect.is_playing())
                if effect:
                    require(effect == state["transition"], "切关重新创建 / 重播了音效")
                phase("gameplay")
        elif current == "gameplay" and elapsed > 5.0:
            world = game_world()
            audio, music = check_music(world, "SW_GameplayMusic")
            require(audio.get_transition_component() is None, "单次进入音效没有自然结束")
            record_stop(world, "MenuToGame")
            state["checks"]["transition_finished_naturally"] = True
            state["checks"]["gameplay_music"] = music.get_editor_property("sound").get_path_name()
            duration = music.get_editor_property("sound").get_editor_property("duration")
            music.play(max(duration - 1.0, 0.0))
            record_start(world, 5.0)
            phase("gameplay_loop")
        elif current == "gameplay_loop" and elapsed > 4.0:
            check_music(game_world(), "SW_GameplayMusic")
            record_stop(game_world(), "GameplayLoop")
            state["checks"]["gameplay_loops_past_end"] = True
            level.editor_request_end_play()
            phase("restart")
        elif current == "restart" and elapsed > 2.0 and not level.is_in_play_in_editor():
            check_released(state["game_component_path"])
            require(level.load_level(MENU), "无法重新加载菜单")
            # FadeOutDuration 是 EditDefaultsOnly，不能修改已生成的控制器实例；在本进程 CDO 上配置后再开始 PIE。
            menu_defaults.set_editor_property("fade_out_duration", 0.0)
            level.editor_request_begin_play()
            phase("abort_transition")
        elif current == "abort_transition" and elapsed > 3.0 and game_world():
            world = game_world()
            audio, music = check_music(world, "SW_MenuMusic")
            require(audio.get_transition_component() is None, "新 PIE 继承旧进入音效")
            state["abort_music_path"] = music.get_path_name()
            controller = unreal.GameplayStatics.get_player_controller(world, 0)
            controller.request_start_game()
            require(audio.get_transition_component() is not None, "零淡出时长未播放音效")
            state["abort_effect_path"] = audio.get_transition_component().get_path_name()
            # 在短音效尚未结束时退出，验证设备级持久化音效也会被 GameInstance 关闭路径清理。
            level.editor_request_end_play()
            phase("direct_test")
        elif current == "direct_test" and elapsed > 2.0 and not level.is_in_play_in_editor():
            check_released(state["abort_music_path"])
            check_released(state["abort_effect_path"])
            state["checks"]["restart_and_abort_cleanup"] = True
            require(level.load_level(GAME), "无法直接加载 TEST")
            level.editor_request_begin_play()
            phase("direct_play")
        elif current == "direct_play" and elapsed > 3.0 and game_world():
            audio, music = check_music(game_world(), "SW_GameplayMusic")
            require(audio.get_transition_component() is None, "直接 TEST PIE 意外播放进入音效")
            state["checks"]["direct_test_plays_gameplay_music"] = True
            level.editor_request_end_play()
            phase("done")
        elif current == "done" and elapsed > 2.0 and not level.is_in_play_in_editor():
            # 异步写入结束后读取每段 PCM，报告同时包含真实信号电平；静音不能算播放验收通过。
            state["checks"]["recordings"] = [analyze_recording(name)
                                             for name in ("MenuLoop", "MenuToGame", "GameplayLoop")]
            finish()
    except Exception as error:
        finish(str(error))


require(level.load_level(MENU), "无法加载菜单地图")
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
handle = unreal.register_slate_post_tick_callback(tick)
