# -*- coding: utf-8 -*-
"""在普通 Python 中检查 UE 截图并组合铅笔动图，需要 Pillow 与 NumPy。

输入必须是 capture_white_preview.py 刚生成的真实 UE 渲染结果。
此脚本只读取预览截图、写入验收报告和动图，不操作任何 Unreal 项目资源。
"""

import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter


OUTPUT_DIR = Path(__file__).resolve().parents[2] / "Saved" / "WhitePreview"


def pixels(name):
    """统一读取 RGB，避免 PNG 的 Alpha 或调色板影响跨帧差异计算。"""
    with Image.open(OUTPUT_DIR / name) as picture:
        return np.asarray(picture.convert("RGB"), dtype=np.int16)


def difference(first, second):
    """按每像素最大的通道差检查变化；2 灰阶以下视为渲染舍入误差。"""
    if first.shape != second.shape:
        raise RuntimeError("待比较的截图分辨率不一致")
    delta = np.max(np.abs(first - second), axis=2)
    return delta, {"mean_byte_difference": float(delta.mean()),
                   "changed_pixels_over_2": int(np.count_nonzero(delta > 2)),
                   "changed_fraction_over_2": float(np.mean(delta > 2)),
                   "changed_fraction_over_8": float(np.mean(delta > 8))}


def main():
    """验证真实时间、循环闭合、背景和分辨率，并输出使用原始渲染帧的动图。"""
    report_path = OUTPUT_DIR / "pencil_pixel_report.json"
    if report_path.exists():
        report_path.unlink()
    capture = json.loads((OUTPUT_DIR / "capture_report.json").read_text(encoding="utf-8"))
    if capture["error"] or any(capture["compile_errors"].values()):
        raise RuntimeError("UE 材质或截图验收失败")

    first = pixels("WhiteOutlinePreview.png")
    changed = pixels("WhitePencilPreview_T1.png")
    loop = pixels("WhitePencilPreview_Loop.png")
    live_a = pixels("WhitePencilPreview_LiveA.png")
    live_b = pixels("WhitePencilPreview_LiveB.png")
    motion_delta, motion = difference(first, changed)
    _, closure = difference(first, loop)
    _, realtime = difference(live_a, live_b)

    # 定帧变化和真实 Time 节点变化都必须存在，避免只保存了参数却没有可见动画。
    # 完整周期的首尾应近似一致；允许 GI / TSR 的少量收敛误差。
    if motion["changed_pixels_over_2"] < 500 or realtime["changed_pixels_over_2"] < 500:
        raise RuntimeError("线条时间变化不足，检查 Time 节点和后处理引用")
    if closure["mean_byte_difference"] > 0.35 or closure["changed_fraction_over_8"] > 0.0005:
        raise RuntimeError("完整动画周期未闭合，检查时间波形或场景变化")

    # 用两帧的笔迹并集扩大 3 像素；绝大多数运动应发生在这条窄带中。
    # 这能区分轻微线条摆动与建筑整体漂移、全屏噪声等错误。
    stroke_mask = (np.min(first, axis=2) < 210) | (np.min(changed, axis=2) < 210)
    stroke_band = np.asarray(Image.fromarray(stroke_mask.astype(np.uint8) * 255)
                             .filter(ImageFilter.MaxFilter(7))) > 0
    # 宽阔浅灰面可能有约 2～3 灰阶的 GI / TSR 差异，定位运动时使用 8 灰阶阈值。
    # 循环检查也单独限制这种较明显差异，不能仅因总体平均值很低就判定动画闭合。
    changed_pixels = motion_delta > 8
    near_stroke_fraction = float(np.mean(stroke_band[changed_pixels]))
    if near_stroke_fraction < 0.98:
        raise RuntimeError("运动扩散至笔迹之外，检查白色面或背景是否发生闪烁")

    sizes = set()
    for item in capture["captures"]:
        picture = pixels(Path(item["path"]).name)
        height, width, _ = picture.shape
        if (width, height) != (item["width"], item["height"]):
            raise RuntimeError("截图尺寸与报告不一致")
        if np.max(picture, axis=2).max() != 255 or np.min(picture) >= 180:
            raise RuntimeError("截图没有可见笔迹或白纸")
        if np.max(np.max(picture, axis=2) - np.min(picture, axis=2)) > 1:
            raise RuntimeError("黑白预览出现彩色像素")
        border = np.concatenate((picture[:12].reshape(-1, 3), picture[-12:].reshape(-1, 3),
                                 picture[:, :12].reshape(-1, 3), picture[:, -12:].reshape(-1, 3)))
        if np.min(border) != 255:
            raise RuntimeError("画布边缘不是纯白，检查背景或取景")
        sizes.add((width, height))

    # 给所有 GIF 帧使用同一个 256 级灰度调色板，不进行抖色。
    # 调色板抖色会自行增加视觉噪点，妨碍判断着色器的真实石墨颗粒。
    palette = Image.new("P", (1, 1))
    palette.putpalette([channel for value in range(256) for channel in (value, value, value)])
    frame_captures = [item for item in capture["captures"]
                      if Path(item["path"]).name.startswith("WhitePencilFrame_")]
    frames = []
    for item in frame_captures:
        with Image.open(item["path"]) as source:
            frames.append(source.convert("RGB").quantize(palette=palette, dither=Image.Dither.NONE))
    if len(frames) < 2:
        raise RuntimeError("循环预览缺少动画帧")
    duration = round(capture["animation_period_seconds"] * 1000.0 / len(frames) / 10.0) * 10
    animation = OUTPUT_DIR / "WhitePencilPreview.gif"
    frames[0].save(animation, save_all=True, append_images=frames[1:], loop=0,
                   duration=duration, optimize=False, disposal=2)

    # 建筑顶部的放大图用于辨认亚像素摆动；最近邻放大不会补造新线条或颗粒。
    detail = OUTPUT_DIR / "WhitePencilDetail.gif"
    details = [frame.crop((340, 70, 600, 290)).resize((780, 660), Image.Resampling.NEAREST)
               for frame in frames]
    details[0].save(detail, save_all=True, append_images=details[1:], loop=0,
                    duration=duration, optimize=False, disposal=2)

    result = {"passed": True, "fixed_time_motion": motion, "loop_closure": closure,
              "realtime_motion": realtime, "motion_near_strokes_fraction": near_stroke_fraction,
              "checked_resolutions": sorted(sizes), "animation_frames": len(frames),
              "gif_frame_duration_ms": duration, "animation": str(animation), "detail": str(detail)}
    report_path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
