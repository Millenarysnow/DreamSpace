# 白色建筑描边预览

独立预览关卡使用 `TEST` 中当前可见的建筑形体，呈现纯白背景、白色表面、浅灰形体明暗和连续铅笔结构线。笔迹有轻微粗细、深浅与石墨颗粒变化，并在原轮廓附近平滑摆动。当前仅实现视觉效果，不包含标题、开始交互或旋转。

## 查看效果

1. 在 UE 内容浏览器中打开 `Content/DreamPresentation/WhitePreview/Maps/L_WhiteOutlinePreview`。
2. 点击“运行”。预览专用 GameMode 会使用固定相机，不生成角色或调试 HUD。
3. 在编辑器中自由查看时使用“光照”模式，并确认后处理显示开启；按 `G` 可隐藏编辑器图标。查看固定构图可右键 `WhitePreview_Camera` 选择“驾驶”。
4. 编辑器中需要开启视口“实时”才能看到笔迹摆动；运行游戏时会自动更新。`MI_WhitePencil` 的 `AnimationTime` 应保持 `-1`。

这个关卡是静态快照，复用源网格，只覆盖预览 Actor 的材质槽。`TEST`、原网格默认材质、解谜蓝图和项目默认启动地图均不改变。未来制作开始界面时，可在此展示关卡上继续添加文字、动画和开始交互。

## 资源与参数

资源都位于 `/Game/DreamPresentation/WhitePreview`：

| 资源 | 用途 |
| --- | --- |
| `Materials/M_WhiteArchitecture` | 双面、非金属、高粗糙度白色表面，保留几何法线并支持 Nanite |
| `Materials/M_WhiteOutlinePost` | 色调映射后的白背景、浅灰明暗及法线／深度边缘识别 |
| `Materials/M_WhiteOutlineAA` | 在原平滑阶段加入连续亚像素位移、石墨密度与沿边缘方向抗锯齿 |
| `Materials/MI_WhiteOutline` | 关卡实际使用的描边材质实例，可直接调节效果 |
| `Materials/MI_WhitePencil` | 关卡实际使用的铅笔材质实例，可调摆动和石墨笔迹 |
| `Maps/L_WhiteOutlinePreview` | 建筑静态快照、后处理、灯光与固定相机 |

打开 `MI_WhiteOutline`，勾选相应参数的覆盖开关再修改：

| 参数 | 默认值 | 调整效果 |
| --- | --- | --- |
| `SilhouetteWidth` | 1.15 | 外轮廓采样半径，单位为最终输出像素 |
| `StructureWidth` | 0.95 | 墙角、台阶及遮挡边缘的采样半径 |
| `StructureStrength` | 0.9 | 内部线条浓度，0 关闭，1 为完整墨色 |
| `NormalThreshold` | 0.25 | 越高，越只保留明显的法线转折 |
| `DepthThreshold` | 0.006 | 越高，越过滤细小的遮挡边缘 |
| `ShadingStrength` | 0.16 | 浅灰明暗强度，0 为严格纯白表面 |
| `LineOpacity` | 1.0 | 所有线条的整体浓度 |
| `InkColor` | 接近黑色 | 线条颜色 |
| `LightDirection` | 世界空间方向 | 浅灰形体的明暗方向 |
| `MaxSubjectDepth` | 1000000 cm | 超过此深度视为白背景 |
| `StrokeVariation` | 0.22 | 连续的空间线宽变化；0 恢复等宽线 |

打开 `MI_WhitePencil` 调节手绘感：

| 参数 | 默认值 | 调整效果 |
| --- | --- | --- |
| `WobbleAmplitude` | 0.9 | 每轴最大位移，单位为输出像素；建议 0.5～1.2，0 关闭位移 |
| `WobbleSpeed` | 0.65 | 主时间波形的每秒周期数；完整循环约 3.08 秒，0 停止动画 |
| `StrokeScale` | 64 | 主空间波长，单位为输出像素；越大长轮廓越舒缓 |
| `GraphiteSoftness` | 0.12 | 石墨整体减淡量，越大笔迹越偏浅灰 |
| `PressureVariation` | 0.12 | 连续笔压产生的深浅变化 |
| `GraphiteGrain` | 0.16 | 固定细颗粒的浓度变化，不向白纸添加噪点 |
| `SmoothingStrength` | 1.0 | 沿笔迹方向的抗锯齿强度 |
| `AnimationTime` | -1 | 使用实时动画；非负值冻结为指定秒数，供定帧比较 |

优先调整 `WobbleAmplitude` 和 `WobbleSpeed`。如果希望线更硬、更黑，可降低 `GraphiteSoftness`、`PressureVariation` 和 `GraphiteGrain`。把这三项、`WobbleAmplitude` 以及 `MI_WhiteOutline` 的 `StrokeVariation` 都设为 0，可恢复原等宽黑线风格。

时间波形连续，不逐帧随机生成线条；空间位移平滑，石墨浓度有下限，所以不会刻意生成虚线或闪烁缺口。白色背景保持纯白。后处理采样位移也会影响轮廓附近的浅灰像素，但不会移动网格、相机或原始几何缓冲。效果只增加现有平滑阶段的计算，不新增渲染通道。

描边识别可见表面的几何转折和深度跳变，不绘制隐藏边，也不显示三角网格。内部深度判定使用倒数深度二阶差分，减少倾斜墙面被误判为黑块的问题。当前后处理用于专用白色展示关卡，关卡中的所有可见网格都会被纳入处理；不需要开启全项目 Custom Depth／Stencil。

## 重新生成

通常只需打开已生成的关卡。仅在 `TEST` 建筑布局改变、或修改着色代码后才需要重跑脚本。完整重跑会覆盖专用预览地图与材质，并恢复脚本里的默认参数；请先保存需要保留的预览调整。

先编译 `DreamSpaceEditor`，随后在 PowerShell 运行：

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'F:\Ue5 Project\DreamSpace-UI\DreamSpace.uproject' `
  -run=pythonscript `
  '-script=F:/Ue5 Project/DreamSpace-UI/Source/WhitePreview/generate_white_preview.py' `
  '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' `
  -unattended -AllowCommandletRendering -nosound -stdout
```

Python 插件只在此命令行运行时启用，不必改 `.uproject`。脚本路径使用正斜杠，避免路径里的 `\U` 被 Python 解释成转义。生成报告位于 `Saved/WhitePreview/generation_report.json`，列出源组件、取景范围和跳过的空引用。

如果仅修改线条着色器，可将上面命令中的脚本参数改为：

```powershell
'-script=F:/Ue5 Project/DreamSpace-UI/Source/WhitePreview/generate_white_preview.py --materials-only'
```

增量更新只重建描边与铅笔材质、恢复对应实例参数，并更新已有预览体积的材质引用。已有网格变换、相机、灯光及白色表面材质保留，不重新读取 `TEST`。报告写入 `Saved/WhitePreview/pencil_generation_report.json`。另一套 `WhiteBoxPreview` 资源不受影响。

## 自动截图与视觉验收

`Source/WhitePreview/capture_white_preview.py` 使用真实 UE 编辑器视口输出 1920×1080 和 1280×720 截图，并采集 960×540 的 24 帧完整循环。通过临时动态材质实例冻结时间，不保存定帧参数。截图后恢复关卡实际使用的材质实例并启动 PIE，检查预览 GameMode、相机标签、角色和 HUD，再采集两张运行截图验证 Time 节点确实驱动了运动。最后结束 PIE 并自动退出本次验收编辑器实例。

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'F:\Ue5 Project\DreamSpace-UI\DreamSpace.uproject' `
  '-ExecutePythonScript=F:/Ue5 Project/DreamSpace-UI/Source/WhitePreview/capture_white_preview.py' `
  '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' `
  -unattended -nosound -NoSplash
```

截图与报告分别写入 `Saved/WhitePreview/WhiteOutlinePreview.png` 和 `Saved/WhitePreview/capture_report.json`。不能使用 `-nullrhi`，它不会渲染真正的画面。

随后在安装 Pillow 和 NumPy 的普通 Python 环境中运行：

```powershell
python Source/WhitePreview/verify_pencil_preview.py
```

脚本检查定帧和实时的像素变化、循环首尾近似一致、运动集中在笔迹附近、各分辨率非空且背景边缘纯白。输出 `Saved/WhitePreview/pencil_pixel_report.json`、`WhitePencilPreview.gif` 和顶部放大的 `WhitePencilDetail.gif`。GIF 使用同一灰度调色板且不抖色，展示原始渲染帧；时间间隔按 GIF 的 10 毫秒精度取整。

视觉验收主要看白色表面是否足够轻、内部线条是否过密、远处小结构是否易读，以及不同窗口尺寸下的描边粗细。屏幕描边会受到分辨率与渲染比例影响，最终可按目标 UI 的显示大小调节参数。

源 `TEST` 中存在空的装饰网格、旧门窗／洗衣机子蓝图加载警告以及一份缺贴图的旧材质。生成脚本跳过空组件，预览表面统一使用新白色材质；报告保留跳过记录，不补造缺失装饰。

## 本次验证

2026-10-09，UE 5.8.2：`DreamSpaceEditor Win64 Development` 和 `DreamSpace Win64 Development` 编译通过。独立重新加载后的三个新材质均无编译错误，1920×1080 真实视口截图完成；PIE 实际确认预览 GameMode 和相机标签正确，角色、HUD 均为空。预览包含 29 个静态网格。原 `TEST.umap` 的 Git 文件哈希与修改前一致，原资源没有保存改动。

2026-10-10，铅笔风格更新：在 UE 5.8.2 中独立重新加载并编译三个材质，均无编译错误。真实渲染输出 1920×1080、1280×720、960×540，像素检查通过。指定时间点的显著变化全部位于笔迹附近；完整循环首尾平均差约 0.05 / 255，仅 0.00034% 的像素差超过 8 灰阶。PIE 中两张相隔约 0.64 秒的实时截图确认 Time 节点产生可见线条运动，预览 GameMode、相机及空 Pawn/HUD 检查通过。

本次仅增量更新线条材质和预览后处理引用，无 C++ 改动。`TEST.umap`、白色表面材质和 `WhiteBoxPreview` 资源的 Git 文件哈希与更新前一致。新增代码和材质节点均有中文说明。动画的轻重与节奏仍需在实际 UE 运行画面中做主观验收；动图用于辅助比较。
