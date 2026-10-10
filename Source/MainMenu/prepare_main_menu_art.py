# -*- coding: utf-8 -*-
"""把用户提供的 Logo 和 A4 纸照片整理成菜单可直接导入的 PNG。

本脚本在普通 Python（Pillow + NumPy）中运行，不修改原文件。
输出存入 RawContent/MainMenu，生成的 UE 资源会内嵌图像，不依赖开发机器路径。
"""

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageOps


PROJECT = Path(__file__).resolve().parents[2]
OUTPUT = PROJECT / "RawContent" / "MainMenu"


def prepare_logo(source):
    """保留艺术字轮廓、透明度和原色的轻微深浅差，只把彩色变为石墨灰。"""
    image = Image.open(source).convert("RGBA")
    bounds = image.getchannel("A").getbbox()
    if bounds is None:
        raise RuntimeError("Logo 完全透明，无法生成菜单素材")
    image = image.crop(bounds)
    rgba = np.array(image, dtype=np.float32)
    luminance = rgba[:, :, :3] @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    # 深色主体和浅色小装饰都保持可辨认；透明边缘仍沿用原始 alpha，不出现白边。
    graphite = 44.0 + luminance * 0.14
    rgba[:, :, :3] = graphite[:, :, None]
    image = Image.fromarray(np.uint8(np.clip(rgba, 0, 255)))
    image = ImageOps.contain(image, (1024, 1024), Image.Resampling.LANCZOS)
    # 预留 12 px 透明边，双线性采样不会从纹理边缘读出实心颜色。
    padded = Image.new("RGBA", (image.width + 24, image.height + 24), (0, 0, 0, 0))
    padded.paste(image, (12, 12))
    padded.save(OUTPUT / "Logo_Graphite.png")
    return {"source": str(source), "size": list(padded.size)}


def prepare_paper(source):
    """旋转竖向纸照片并适配横屏，把摄影阴影压成轻微纸纹以保留建筑笔划的对比。"""
    paper = Image.open(source).convert("L").transpose(Image.Transpose.ROTATE_90)
    paper = ImageOps.fit(paper, (2048, 1152), Image.Resampling.LANCZOS)
    values = np.asarray(paper, dtype=np.float32)
    low, high = np.percentile(values, [0.3, 99.7])
    normalized = np.clip((values - low) / max(high - low, 1.0), 0.0, 1.0)
    # 纹理本身保持 211～253 的温和亮度范围；材质再用 PaperStrength 控制最终浓度。
    # 不生成运动噪声：停留在菜单时，纸张固定，只有建筑在转动。
    gray = np.uint8(211.0 + 42.0 * normalized ** 0.85)
    Image.fromarray(gray).convert("RGB").save(OUTPUT / "Paper_Crumpled.png")
    return {"source": str(source), "size": [2048, 1152], "source_percentiles": [float(low), float(high)]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, default=PROJECT.parent / "_assets")
    args = parser.parse_args()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    report = {
        "logo": prepare_logo(args.assets / "name" / "梦间DreamSpace-Logo-2048.png"),
        "paper": prepare_paper(args.assets / "paper_img" / "M}CJ~C9~7K24I8~Q8YIME62.jpg"),
    }
    (OUTPUT / "art_sources.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
