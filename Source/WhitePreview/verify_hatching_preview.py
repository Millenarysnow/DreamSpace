# -*- coding: utf-8 -*-
"""检查真实 UE 截图中的手绘阴影排线，需要普通 Python、Pillow 与 NumPy。

输入来自 capture_white_preview.py，本脚本不操作 Unreal 资源。
检查排线的可见贡献、暗部相对亮部的笔划浓度、稳定轮廓及白背景。
"""

import json
from pathlib import Path

import numpy as np
from PIL import Image


OUTPUT_DIR = Path(__file__).resolve().parents[2] / "Saved" / "WhitePreview"


def pixels(name):
    """统一读取 RGB，防止 Alpha 和调色板使跨截图的比较失真。"""
    with Image.open(OUTPUT_DIR / name) as picture:
        return np.asarray(picture.convert("RGB"), dtype=np.int16)


def main():
    """以完整效果与调试截图相互验证，并记录结果与未加工的局部截图。"""
    report_path = OUTPUT_DIR / "hatching_pixel_report.json"
    if report_path.exists():
        report_path.unlink()
    capture = json.loads((OUTPUT_DIR / "hatching_capture_report.json").read_text(encoding="utf-8"))
    if capture["error"] or any(capture["compile_errors"].values()) or any(capture["time_node_counts"].values()):
        raise RuntimeError("UE 材质、截图或无时间动画检查失败")

    full = pixels("WhiteHatchingPreview.png")
    repeat = pixels("WhiteHatchingPreview_Repeat.png")
    plain = pixels("WhiteHatchingPreview_NoHatch.png")
    tone_image = pixels("WhiteHatchingPreview_Tone.png")
    normal_tone = pixels("WhiteHatchingPreview_NormalTone.png")
    strokes = pixels("WhiteHatchingPreview_Strokes.png")
    pie = pixels("WhiteHatchingPreview_PIE.png")
    pie_repeat = pixels("WhiteHatchingPreview_PIE_Repeat.png")
    pie_plain = pixels("WhiteHatchingPreview_PIE_NoHatch.png")
    pie_strokes = pixels("WhiteHatchingPreview_PIE_Strokes.png")
    play = capture["play_preview"]
    if (play.get("active_blendables") != capture["active_blendables"]
            or play.get("hatch_debug_view") != 0.0
            or play.get("anti_aliasing_during_play") != 1
            or play.get("anti_aliasing_before_play") != play.get("anti_aliasing_after_play")):
        raise RuntimeError("PIE 后处理或临时抗锯齿状态未正确启用／恢复")

    # 关闭排线后应少掉大量面内笔划；仅编译成功或出现轮廓线都不能通过这一检查。
    hatch_difference = np.max(np.abs(full - plain), axis=2)
    hatch_changed = int(np.count_nonzero(hatch_difference > 8))
    if hatch_changed < 5000:
        raise RuntimeError("排线没有产生足够的表面贡献，检查材质与体积引用")

    # 时间动画已经移除；允许真实渲染 GI / TSR 的细小收敛差异，但限制可见位移。
    repeat_difference = np.max(np.abs(full - repeat), axis=2)
    stability = {"mean_byte_difference": float(repeat_difference.mean()),
                 "changed_fraction_over_8": float(np.mean(repeat_difference > 8))}
    if stability["mean_byte_difference"] > 0.35 or stability["changed_fraction_over_8"] > 0.005:
        raise RuntimeError("重复截图仍存在较大变化，检查轮廓是否稳定")

    # Tone 视图在色调映射之后直接输出 1-tone，UE 截图保存该显示灰度。
    # 此处直接还原明暗依据；再做 sRGB 解码会错误地把中间调判作接近全黑。
    # Strokes 视图只有排线、没有建筑轮廓，因此可直接测量每段明暗对应的笔划暗度。
    encoded = tone_image[:, :, 0].astype(np.float32) / 255.0
    darkness = 1.0 - encoded
    subject = np.min(plain, axis=2) < 254
    lighting_changed = int(np.count_nonzero(subject & (np.max(np.abs(tone_image - normal_tone), axis=2) > 8)))
    if lighting_changed < 2000:
        raise RuntimeError("真实光照未参与排线明暗，检查 ShadowInfluence 和场景颜色输入")
    graphite = 255.0 - strokes[:, :, 0].astype(np.float32)
    density_by_tone = []
    for low, high in ((0.0, 0.18), (0.18, 0.43), (0.43, 0.72), (0.72, 1.01)):
        selected = subject & (darkness >= low) & (darkness < high)
        count = int(np.count_nonzero(selected))
        density_by_tone.append({"tone_min": low, "tone_max": high, "pixels": count,
                                "mean_graphite_darkness": float(graphite[selected].mean()) if count else None})
    light = subject & (darkness < 0.3)
    dark = subject & (darkness > 0.65)
    if np.count_nonzero(light) < 200 or np.count_nonzero(dark) < 200:
        raise RuntimeError("截图缺少可比较的亮面或暗面")
    if graphite[dark].mean() < graphite[light].mean() + 3.0:
        raise RuntimeError("暗部笔划浓度未高于亮部，检查排线明暗映射")

    # 使用同一运行世界的对照，不把编辑器和 PIE 的抗锯齿差异误当成缺失。
    # 关闭排线仍黑、仅排线近白的位置应是结构线；面内亮处则检查排线贡献。
    outline_only = (pie_plain[:, :, 0] < 100) & (pie_strokes[:, :, 0] > 220)
    outline_match = float(np.mean(pie[:, :, 0][outline_only] < 160))
    pie_interior = (pie_plain[:, :, 0] > 180) & (pie_strokes[:, :, 0] < 240)
    pie_hatch_changed = int(np.count_nonzero(pie_interior & (np.max(np.abs(pie - pie_plain), axis=2) > 8)))
    if np.count_nonzero(outline_only) < 500 or outline_match < 0.85 or pie_hatch_changed < 2000:
        raise RuntimeError("PIE 截图未包含完整描边与阴影排线，检查调试实例恢复")
    pie_repeat_difference = np.max(np.abs(pie - pie_repeat), axis=2)
    pie_stability = {"mean_byte_difference": float(pie_repeat_difference.mean()),
                     "changed_fraction_over_8": float(np.mean(pie_repeat_difference > 8))}
    if pie_stability["mean_byte_difference"] > 0.35 or pie_stability["changed_fraction_over_8"] > 0.005:
        raise RuntimeError("PIE 重复截图不稳定，检查实际抗锯齿模式")

    sizes = set()
    for item in capture["captures"]:
        picture = pixels(Path(item["path"]).name)
        height, width, _ = picture.shape
        if (width, height) != (item["width"], item["height"]):
            raise RuntimeError("截图尺寸与报告不一致")
        if np.min(picture) >= 180:
            raise RuntimeError("截图为空或未显示黑白效果")
        # 原始光照视图仅作阴影输入检查，允许引擎 GI 带来微小色偏；
        # 正式风格与其他调试输出仍必须严格灰度。
        if (item.get("overrides", {}).get("HatchDebugView") != 3.0
                and np.max(np.max(picture, axis=2) - np.min(picture, axis=2)) > 1):
            raise RuntimeError("预览中出现彩色像素")
        border = np.concatenate((picture[:12].reshape(-1, 3), picture[-12:].reshape(-1, 3),
                                 picture[:, :12].reshape(-1, 3), picture[:, -12:].reshape(-1, 3)))
        if np.min(border) != 255:
            raise RuntimeError("白背景边缘出现杂色")
        sizes.add((width, height))

    # 只裁剪 UE 原始像素，不额外添加笔划、纹理、锐化或调色，以便用户判断实际结果。
    detail = OUTPUT_DIR / "WhiteHatchingDetail.png"
    with Image.open(OUTPUT_DIR / "WhiteHatchingPreview.png") as picture:
        picture.crop((690, 140, 1190, 540)).save(detail)
    result = {"passed": True, "hatch_changed_pixels_over_8": hatch_changed,
              "lighting_tone_changed_pixels_over_8": lighting_changed,
              "stable_outline": stability, "graphite_density_by_tone": density_by_tone,
              "pie_full_effect": {"outline_match_fraction": outline_match,
                                  "interior_hatch_changed_pixels_over_8": pie_hatch_changed,
                                  "stable_outline": pie_stability,
                                  "anti_aliasing_restored": True},
              "checked_resolutions": sorted(sizes), "detail": str(detail)}
    report_path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
