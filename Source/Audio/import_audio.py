# -*- coding: utf-8 -*-
"""在 UE 5.8 编辑器 Python 环境导入三段音频，不修改任何地图或已有资源。

需要先运行 prepare_audio.py。初次导入建立循环、音频分组、压缩与预加载配置；
重新运行时保留已经存在的 SoundWave 及用户调整，只检查路径、类型和播放属性。
可在新的编辑器进程重新运行以检查磁盘上的资源；报告记录实际时长，失败会让 commandlet 返回错误。
"""

import hashlib
import json
from pathlib import Path

import unreal


PROJECT = Path(unreal.Paths.project_dir()).resolve()
MANIFEST = PROJECT / "RawContent" / "Audio" / "audio_sources.json"
OUTPUT = Path(unreal.Paths.project_saved_dir()).resolve() / "Audio"


def require(condition, message):
    """把缺文件、导入失败和循环设置错误明确标记为验收失败，避免写出看似成功的报告。"""
    if not condition:
        raise RuntimeError(message)


def main():
    records = json.loads(MANIFEST.read_text(encoding="utf-8"))["audio"]
    report = []
    unreal.EditorAssetLibrary.make_directory("/Game/Audio/BGM")
    for record in records:
        source = PROJECT / record["project_source"]
        require(source.is_file(), "项目内音频来源不存在：" + str(source))
        require(hashlib.sha256(source.read_bytes()).hexdigest() == record["sha256"],
                "音频与来源记录不一致，请重新运行 prepare_audio.py：" + source.name)
        path = record["asset"]
        existing = unreal.EditorAssetLibrary.does_asset_exist(path)
        if existing:
            sound = unreal.EditorAssetLibrary.load_asset(path)
        else:
            # 使用 UE 自带 MP3 导入器，不通过系统播放软件转录，不改变声道数、采样率或原曲时长。
            task = unreal.AssetImportTask()
            task.set_editor_property("filename", str(source))
            task.set_editor_property("destination_path", "/Game/Audio/BGM")
            task.set_editor_property("destination_name", path.rsplit("/", 1)[1])
            task.set_editor_property("automated", True)
            task.set_editor_property("save", False)
            task.set_editor_property("factory", unreal.SoundFactory())
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
            require(task.get_editor_property("imported_object_paths"), "导入 MP3 失败：" + source.name)
            sound = unreal.EditorAssetLibrary.load_asset(path)
        require(isinstance(sound, unreal.SoundWave), "目标资源不是 SoundWave：" + path)
        if not existing:
            sound.set_editor_property("looping", record["looping"])
            sound.set_editor_property("compression_quality", 80)
            sound.set_editor_property("virtualization_mode", unreal.VirtualizationMode.PLAY_WHEN_SILENT)
            if record["looping"]:
                # 长音乐使用流式缓存并预取首块，避免全部解压常驻内存；Music 分组方便后续统一调节。
                sound.set_editor_property("sound_group", unreal.SoundGroup.SOUNDGROUP_MUSIC)
                sound.set_editor_property("loading_behavior", unreal.SoundWaveLoadingBehavior.PRIME_ON_LOAD)
            else:
                # 短提示音内联加载，点击开始时不依赖流式首块到达；它必须自然结束，不能循环。
                sound.set_editor_property("loading_behavior", unreal.SoundWaveLoadingBehavior.FORCE_INLINE)
            require(unreal.EditorAssetLibrary.save_loaded_asset(sound, only_if_is_dirty=False),
                    "保存音频资源失败：" + path)

        require(sound.get_editor_property("looping") == record["looping"], "循环设置与用途不一致：" + path)
        duration = float(sound.get_editor_property("duration"))
        require(duration > 0.0, "导入后音频时长为零：" + path)
        report.append({
            "asset": sound.get_path_name(),
            "created": not existing,
            "duration_seconds": duration,
            "channels": sound.get_editor_property("num_channels"),
            "sample_rate": sound.get_editor_property("imported_sample_rate"),
            "looping": sound.get_editor_property("looping"),
            "loading_behavior": str(sound.get_editor_property("loading_behavior")),
            "source_sha256": record["sha256"],
        })
        unreal.log("DREAM_AUDIO_IMPORTED " + json.dumps(report[-1], ensure_ascii=False))
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / "import_report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("DREAM_AUDIO_IMPORT_OK " + str(OUTPUT / "import_report.json"))


if __name__ == "__main__":
    main()
