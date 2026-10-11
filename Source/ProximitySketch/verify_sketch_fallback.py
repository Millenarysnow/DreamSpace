# -*- coding: utf-8 -*-
"""检查真实 GPU 截图中的回退栏杆，并生成同相机的修复前后对照图。

在含 Pillow / NumPy 的普通 Python 中运行；默认读取 capture_sketch_fallback.py
生成的 FallbackReproAfter，可用 --after 指定新的采样目录。--before 可选，只用于
展示旧问题，不要求旧截图通过检查。输出写入 Saved，不修改 Unreal 资源。
"""

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
DETAIL = ROOT / "Saved/ProximitySketch/DetailFix"
# 固定相机的左下区域只含近圈栏杆与背景，避开玩家、手办和 HUD。
# 用完整 Nanite 原图中的深色像素作为参考，不能把整个白色背景计入“恢复率”。
RAILING_ROI = (0, 590, 800, 960)
VIEWS = ("Railing", "Exterior")
PERCENTAGES = (100, 50)
MODES = ("Original", "Sketch")


def read_report(directory):
    """先核对实际采样状态，缺帧或模式错误的截图不能用于像素验收。"""
    report = json.loads((directory / "capture_report.json").read_text(encoding="utf-8"))
    if report.get("error") is not None or report.get("map") != "/Game/0_/Maps/TEST":
        raise ValueError("采样失败或地图不正确：" + str(directory))
    if report.get("aa_method") != 4:
        raise ValueError("当前固定像素阈值要求 TSR 采样")
    frames = {item["file"]: item for item in report["captures"]}
    expected = {f"{view}{path}_{percentage}_{mode}.png"
                for view in VIEWS for path in ("Nanite", "Fallback")
                for percentage in PERCENTAGES for mode in MODES}
    if len(report["captures"]) != 16 or set(frames) != expected:
        raise ValueError("需要完整、无重复的 16 帧采样")
    for filename, frame in frames.items():
        view, percentage, mode = Path(filename).stem.split("_")
        if (frame["view"] != view or frame["mode"] != mode
                or frame["screen_percentage"] != int(percentage)
                or frame["nanite"] != int(view.endswith("Nanite"))
                or frame["sketch_enabled"] != (mode == "Sketch")):
            raise ValueError("实际采样模式与文件名不一致：" + filename)
        # 原图、线稿和两种几何路径必须使用同一玩家位置及相机。否则范围或
        # 视角变化也可能让栏杆增多，不能把它误判为本次资源修复的效果。
        reference = frames[f"{view.removesuffix('Nanite').removesuffix('Fallback')}Nanite_{percentage}_Original.png"]
        for key in ("camera_position", "camera_rotation", "pawn_position", "fov"):
            if not np.allclose(frame[key], reference[key], rtol=0, atol=1e-5):
                raise ValueError("采样几何位置不一致：" + filename + " / " + key)
    return frames


def railing_coverage(directory):
    """以同批 Nanite 原图为参考，检查 fallback 是否仍丢掉近处栏杆。

    深色判定对原图和平滑后的线稿使用不同容限，允许 TSR 和线宽的轻微变化。
    这只是指定 ROI 内的栏杆覆盖检查；广告字形仍需查看生成的对照图。
    """
    metrics = {}
    for percentage in PERCENTAGES:
        with Image.open(directory / f"RailingNanite_{percentage}_Original.png") as image:
            size = image.size
            reference = np.asarray(image.convert("RGB").crop(RAILING_ROI), dtype=np.float32).mean(axis=2) < 140
        if size != (2109, 1146):
            raise ValueError("固定 ROI 要求 2109 × 1146 视口，请使用指定采样脚本")
        count = int(reference.sum())
        if count < 20000:
            raise ValueError("参考区域中的栏杆像素太少，请核对相机与画面")
        for mode, threshold in (("Original", 150), ("Sketch", 200)):
            with Image.open(directory / f"RailingFallback_{percentage}_{mode}.png") as image:
                if image.size != size:
                    raise ValueError("同组截图尺寸不一致")
                visible = np.asarray(image.convert("RGB").crop(RAILING_ROI), dtype=np.float32).mean(axis=2) < threshold
            metrics[f"{percentage}_{mode}"] = {
                "reference_pixels": count,
                "coverage": float(np.count_nonzero(visible & reference) / count),
            }
    return metrics


def comparison(before, after, output):
    """仅裁剪并并排摆放真实截图，不修改笔画；文字行保留原尺寸便于比对。"""
    font_path = Path("C:/Windows/Fonts/msyh.ttc")
    if not font_path.exists():
        return  # 跨平台运行仍可验证像素；缺少中文字库时不生成乱码对照图。
    font = ImageFont.truetype(str(font_path), 24)
    canvas = Image.new("RGB", (1632, 864), "#eef1f5")
    draw = ImageDraw.Draw(canvas)
    draw.text((16, 10), "Nanite 回退路径 · 100% 渲染 · 同一相机和玩家位置", font=font, fill="#182233")
    for column, (directory, label) in enumerate(((before, "修复前"), (after, "修复后"))):
        x = 16 + column * 816
        draw.text((x, 50), label + "：近处栏杆（线稿开启）", font=font, fill="#182233")
        with Image.open(directory / "RailingFallback_100_Sketch.png") as image:
            canvas.paste(image.convert("RGB").crop(RAILING_ROI), (x, 90))
        draw.text((x, 482), label + "：远处广告文字（线稿开启）", font=font, fill="#182233")
        with Image.open(directory / "ExteriorFallback_100_Sketch.png") as image:
            # 此区域含“南海”和旁边走廊栏杆，位于完全线稿区，避开玩家遮挡。
            canvas.paste(image.convert("RGB").crop((170, 650, 970, 974)), (x, 524))
    canvas.save(output)


def main():
    """所有修复后组合覆盖率均须达标，旧图只提供复现证据。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--after", type=Path, default=DETAIL / "FallbackReproAfter")
    parser.add_argument("--before", type=Path)
    args = parser.parse_args()
    after_frames = read_report(args.after)
    metrics = railing_coverage(args.after)
    report = {"roi": RAILING_ROI, "after": metrics,
              "checks": {name: value["coverage"] > 0.98 for name, value in metrics.items()}}
    DETAIL.mkdir(parents=True, exist_ok=True)
    if args.before:
        before_frames = read_report(args.before)
        for filename, frame in after_frames.items():
            if frame != before_frames[filename]:
                raise ValueError("修复前后采样参数不一致：" + filename)
        report["before"] = railing_coverage(args.before)
        comparison(args.before, args.after, DETAIL / "fallback_comparison.png")
    (DETAIL / "fallback_verification.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if not all(report["checks"].values()):
        raise SystemExit("回退栏杆像素检查未通过，请核对实际截图")


if __name__ == "__main__":
    main()
