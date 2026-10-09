# 白色建筑描边预览

独立预览关卡使用 `TEST` 中当前可见的建筑形体，呈现纯白背景、白色表面、浅灰形体明暗和细黑结构线。当前仅实现视觉效果，不包含标题、开始交互或旋转。

## 查看效果

1. 在 UE 内容浏览器中打开 `Content/DreamPresentation/WhitePreview/Maps/L_WhiteOutlinePreview`。
2. 点击“运行”。预览专用 GameMode 会使用固定相机，不生成角色或调试 HUD。
3. 在编辑器中自由查看时使用“光照”模式，并确认后处理显示开启；按 `G` 可隐藏编辑器图标。查看固定构图可右键 `WhitePreview_Camera` 选择“驾驶”。

这个关卡是静态快照，复用源网格，只覆盖预览 Actor 的材质槽。`TEST`、原网格默认材质、解谜蓝图和项目默认启动地图均不改变。未来制作开始界面时，可在此展示关卡上继续添加文字、动画和开始交互。

## 资源与参数

资源都位于 `/Game/DreamPresentation/WhitePreview`：

| 资源 | 用途 |
| --- | --- |
| `Materials/M_WhiteArchitecture` | 双面、非金属、高粗糙度白色表面，保留几何法线并支持 Nanite |
| `Materials/M_WhiteOutlinePost` | 色调映射后的白背景、浅灰明暗及法线／深度边缘识别 |
| `Materials/M_WhiteOutlineAA` | 描边之后沿边缘方向抗锯齿，保持纯白留白 |
| `Materials/MI_WhiteOutline` | 关卡实际使用的描边材质实例，可直接调节效果 |
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

描边识别可见表面的几何转折和深度跳变，不绘制隐藏边，也不显示三角网格。内部深度判定使用倒数深度二阶差分，减少倾斜墙面被误判为黑块的问题。当前后处理用于专用白色展示关卡，关卡中的所有可见网格都会被纳入处理；不需要开启全项目 Custom Depth／Stencil。

## 重新生成

通常只需打开已生成的关卡。仅在 `TEST` 建筑布局改变、或修改着色代码后才需要重跑脚本。重跑会覆盖专用预览地图与材质，并恢复脚本里的默认参数；请先保存需要保留的预览调整。

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

## 自动截图与视觉验收

`Source/WhitePreview/capture_white_preview.py` 使用真实 UE 编辑器视口输出 1920×1080 截图，等待正常渲染帧和资源加载。截图后实际启动 PIE，检查预览 GameMode、相机标签、角色和 HUD，再结束 PIE 并自动退出本次验收编辑器实例。

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'F:\Ue5 Project\DreamSpace-UI\DreamSpace.uproject' `
  '-ExecutePythonScript=F:/Ue5 Project/DreamSpace-UI/Source/WhitePreview/capture_white_preview.py' `
  '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' `
  -unattended -nosound -NoSplash
```

截图与报告分别写入 `Saved/WhitePreview/WhiteOutlinePreview.png` 和 `Saved/WhitePreview/capture_report.json`。不能使用 `-nullrhi`，它不会渲染真正的画面。

视觉验收主要看白色表面是否足够轻、内部线条是否过密、远处小结构是否易读，以及不同窗口尺寸下的描边粗细。屏幕描边会受到分辨率与渲染比例影响，最终可按目标 UI 的显示大小调节参数。

源 `TEST` 中存在空的装饰网格、旧门窗／洗衣机子蓝图加载警告以及一份缺贴图的旧材质。生成脚本跳过空组件，预览表面统一使用新白色材质；报告保留跳过记录，不补造缺失装饰。

## 本次验证

2026-10-09，UE 5.8.2：`DreamSpaceEditor Win64 Development` 和 `DreamSpace Win64 Development` 编译通过。独立重新加载后的三个新材质均无编译错误，1920×1080 真实视口截图完成；PIE 实际确认预览 GameMode 和相机标签正确，角色、HUD 均为空。预览包含 29 个静态网格。原 `TEST.umap` 的 Git 文件哈希与修改前一致，原资源没有保存改动。
