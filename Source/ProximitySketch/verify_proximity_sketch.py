# -*- coding: utf-8 -*-
"""比较真实 PIE 的 GPU 截图，验证内圈原画面、外圈线稿及玩家距离遮罩。

在普通 Python 中运行，需要 Pillow / NumPy。不复写着色器距离公式做自证，
而是检查关掉效果、扩大半径、固定镜头移动玩家、关闭排线的实际像素变化。
"""

import json
from pathlib import Path

import numpy as np
from PIL import Image

PROJECT = Path(__file__).resolve().parents[2]
OUTPUT = PROJECT / "Saved/ProximitySketch"


def main():
    report = json.loads((OUTPUT / "capture_report.json").read_text(encoding="utf-8"))
    if report["error"] or report["compile_errors"]:
        raise RuntimeError("实际渲染验收失败：" + str(report))
    names = ("Default", "Repeat", "NoHatching", "Mask", "Original", "Disabled", "LargeMask", "MovedMask")
    pictures = {name: np.asarray(Image.open(OUTPUT / ("ProximitySketch_" + name + ".png")).convert("RGB"),
                                 dtype=np.float32) for name in names}
    shapes = {image.shape for image in pictures.values()}
    if len(shapes) != 1:
        raise RuntimeError("对照视口尺寸不一致，不能进行像素比较")
    mask = pictures["Mask"].mean(axis=2)
    valid = np.ones(mask.shape, dtype=bool)
    valid[:65, :] = False  # 项目开发 HUD 是 Slate / Canvas，排除其覆盖区域。
    near = valid & (mask < 1)
    far = valid & (mask > 254)
    transition = valid & (mask >= 5) & (mask <= 250)

    def difference(first, second):
        return np.abs(pictures[first] - pictures[second]).mean(axis=2)

    inner_delta = difference("Default", "Original")
    repeated_delta = difference("Default", "Repeat")
    hatch_delta = difference("Default", "NoHatching")
    moved_delta = difference("Mask", "MovedMask")
    # 连续帧的 GI / 天空可以改变原图，内圈允许少量真实渲染噪声。
    # 远处白纸与原图必须明显不同，关闭排线则必须保留轮廓并改变面内像素。
    metrics = {
        "viewport": [mask.shape[1], mask.shape[0]],
        "near_pixels": int(near.sum()), "far_pixels": int(far.sum()),
        "transition_pixels": int(transition.sum()),
        "near_original_mean_difference": float(inner_delta[near].mean()),
        "near_original_p95_difference": float(np.percentile(inner_delta[near], 95)),
        "disabled_original_near_mean_difference": float(difference("Original", "Disabled")[near].mean()),
        "outside_original_mean_difference": float(difference("Default", "Original")[far].mean()),
        "repeated_frame_mean_difference": float(repeated_delta[valid].mean()),
        "hatching_changed_pixels_over_8": int(((hatch_delta > 8) & far).sum()),
        "larger_radius_near_pixels": int((valid & (pictures["LargeMask"].mean(axis=2) < 1)).sum()),
        "moving_player_changed_mask_pixels_over_8": int(((moved_delta > 8) & valid).sum()),
        "aa_unchanged": report["aa_before"] == report["play"]["aa_runtime"] == report["play"]["aa_after_captures"],
        "fixed_camera_error_cm": float(np.linalg.norm(np.array(report["play"]["camera_position"])
                                                       - report["play"]["moved_camera_position"])),
        "shader_center_error_cm": float(np.linalg.norm(np.array(report["play"]["moved_pawn_position"])
                                                        - report["play"]["moved_shader_position"])),
    }
    checks = {
        "normal_inner_region": metrics["near_pixels"] > 10000 and metrics["near_original_mean_difference"] < 2.0,
        "original_passthrough": metrics["disabled_original_near_mean_difference"] < 2.0,
        "styled_outer_region": metrics["far_pixels"] > 10000 and metrics["outside_original_mean_difference"] > 15,
        "soft_transition": metrics["transition_pixels"] > 1000,
        "visible_hatching": metrics["hatching_changed_pixels_over_8"] > 1000,
        "larger_radius_reveals_more": metrics["larger_radius_near_pixels"] > metrics["near_pixels"] + 10000,
        "center_follows_pawn": metrics["shader_center_error_cm"] < 0.01,
        "moving_center_changes_mask": metrics["moving_player_changed_mask_pixels_over_8"] > 10000
                                      and metrics["fixed_camera_error_cm"] < 0.01,
        "original_antialiasing_preserved": metrics["aa_unchanged"],
    }
    (OUTPUT / "pixel_report.json").write_text(json.dumps({"metrics": metrics, "checks": checks},
                                                        ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"metrics": metrics, "checks": checks}, ensure_ascii=False, indent=2))
    if not all(checks.values()):
        raise RuntimeError("距离线稿像素验收未全部通过")


if __name__ == "__main__":
    main()
