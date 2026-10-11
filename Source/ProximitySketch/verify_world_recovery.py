# -*- coding: utf-8 -*-
"""读取真实 GPU 截图和运行参数，核对世界恢复并生成可供人工查看的三阶段对照。

在普通 Python 中运行，需要 Pillow / NumPy；先由 capture_world_recovery.py 生成截图。
这里不复制半径插值或距离遮罩公式，只比较真实玩家视口像素与实际相机 MID 读回值。
"""

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


def verify(directory):
    """任意检查失败都明确报错，不把上次残留截图或失败的捕获报告当作通过。"""
    report = json.loads((directory / "capture_report.json").read_text(encoding="utf-8"))
    if report.get("error"):
        raise RuntimeError("GPU 捕获失败：" + report["error"])
    captures = {item["file"]: item["runtime"] for item in report["captures"]}
    names = ["Original.png", "Sketch.png", "Expanding.png", "Restored.png"]
    if set(captures) != set(names):
        raise RuntimeError("缺少完整的原图、线稿、扩张和恢复四帧")
    images = [Image.open(directory / name).convert("RGB") for name in names]
    if len({image.size for image in images}) != 1:
        raise RuntimeError("相同取景的四帧尺寸不一致，无法做有效像素比较")
    # 去除上方固定 HUD。其余区域使用整幅场景，避免选择一个已在正常范围内的小区域冒充恢复。
    if images[0].height <= 70:
        raise RuntimeError("截图高度不足，无法排除 HUD 后比较场景")
    arrays = [np.asarray(image, dtype=np.float32)[70:] for image in images]
    original, sketch, expanding, restored = arrays
    differences = {"sketch_mean_difference": float(np.abs(original - sketch).mean()),
                   "expanding_mean_difference": float(np.abs(original - expanding).mean()),
                   "restored_mean_difference": float(np.abs(original - restored).mean())}
    runtime = captures["Restored.png"]
    samples = [item for item in report["samples"] if item["active"] or item["finished"]]
    # 允许 TSR 历史、场景噪声带来少量不同；3 / 255 为本次固定视角完整恢复的容差。
    # 初始线稿须有明确像素差，中途须向原图接近，终点须回到原图，而不是仅看一个布尔状态。
    checks = {
        "initial_sketch_visible": differences["sketch_mean_difference"] > 10.0,
        "expansion_changes_real_pixels": (
            differences["restored_mean_difference"] < differences["expanding_mean_difference"]
            < differences["sketch_mean_difference"]),
        "restored_matches_original": differences["restored_mean_difference"] < 3.0,
        "restoration_finishes_and_clears_both_channels": (
            runtime["finished"] and not runtime["active"]
            and runtime["main_strength"] == runtime["smoothing_strength"] == 0.0
            and runtime["radius"] == report["target_radius"]),
        "runtime_radius_never_shrinks": bool(samples) and all(
            current["radius"] >= previous["radius"]
            for previous, current in zip(samples, samples[1:])),
        "both_render_passes_stay_in_sync": bool(samples) and all(
            abs(item["main_radius"] - item["smoothing_radius"]) < 0.01
            and abs(item["main_strength"] - item["smoothing_strength"]) < 0.001
            for item in samples),
    }
    # 生成中文标注的同镜头对照，图像本身来自真实 GPU 输出，不绘制或美化恢复边缘。
    width = 800
    height = round(images[0].height * width / images[0].width)
    comparison = Image.new("RGB", (width * 3, height + 42), "#202733")
    draw = ImageDraw.Draw(comparison)
    font_path = Path("C:/Windows/Fonts/msyh.ttc")
    font = ImageFont.truetype(str(font_path), 20) if font_path.exists() else ImageFont.load_default()
    for index, (label, image) in enumerate(zip(
            ("交互前：线稿 / 6 米正常范围", "恢复中：正常范围扩大到约 18 米", "恢复完成：包含天空和远景"), images[1:])):
        draw.text((index * width + 14, 9), label, font=font, fill="white")
        comparison.paste(image.resize((width, height), Image.Resampling.LANCZOS), (index * width, 42))
    comparison.save(directory / "comparison.png")
    summary = {"checks": checks, "metrics": differences, "image_size": images[0].size,
               "sample_count": len(samples), "passed": all(checks.values())}
    (directory / "verification.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    if not summary["passed"]:
        raise SystemExit("世界恢复 GPU 检查失败，请查看 verification.json 和同镜头截图")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path,
                        default=Path(__file__).resolve().parents[2] / "Saved/ProximitySketch/WorldRecovery")
    verify(parser.parse_args().directory.resolve())
