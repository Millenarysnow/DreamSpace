# -*- coding: utf-8 -*-
"""将用户提供的三段 MP3 原样复制到项目内，记录来源和校验值。

这是普通 Python 脚本，不依赖 UE 编辑器或第三方音频库。UE 5.8 可以直接导入 MP3，
因此不需要有损重编码或改动原音乐；短英文文件名让资源重新导入不依赖外部中文路径。
默认素材目录是项目旁边的 _assets/bgm，也可通过 --assets 指向另一个来源目录。
"""

import argparse
import hashlib
import json
import shutil
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[2]
SOURCES = (
    ("SW_MenuMusic", "1 梦境核心音效_胡安霍制作的音效_(1 Dreamcore_爱给网_aigei_com.mp3", True),
    ("SW_GameplayMusic", "午后回声  梦核情绪_爱给网_aigei_com.mp3", True),
    ("SW_EnterGame", "请进入游戏(se_enter_game)_爱给网_aigei_com.mp3", False),
)


def main():
    """先检查全部来源，再逐个复制；只有副本写入 RawContent，用户的原文件始终保持原样。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, default=PROJECT.parent / "_assets" / "bgm")
    args = parser.parse_args()
    paths = [args.assets / filename for _, filename, _ in SOURCES]
    for source in paths:
        if not source.is_file():
            raise FileNotFoundError("缺少音频来源：" + str(source))

    output = PROJECT / "RawContent" / "Audio"
    output.mkdir(parents=True, exist_ok=True)
    records = []
    for (name, original, looping), source in zip(SOURCES, paths):
        target = output / (name + ".mp3")
        shutil.copyfile(source, target)
        records.append({
            "asset": "/Game/Audio/BGM/" + name,
            "source_filename": original,
            "project_source": "RawContent/Audio/" + target.name,
            "sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
            "bytes": target.stat().st_size,
            "looping": looping,
        })
        print(name + ": " + str(target.stat().st_size) + " bytes")
    (output / "audio_sources.json").write_text(
        json.dumps({"source_directory": "_assets/bgm", "audio": records}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
