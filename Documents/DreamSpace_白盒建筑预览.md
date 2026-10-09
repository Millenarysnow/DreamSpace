# 白盒建筑预览

这是第二套独立视觉方案，与 `WhiteOutlinePreview` 并存。它把 `TEST` 中当前可见的建筑复制为静态预览，使用白色表面、灰阶形体明暗和柔和的面边缘渐变。没有独立黑色描边；墙角和遮挡边界稍深，向面内连续变浅，保留白盒的三维体积感。背景为纯白，画面只包含黑白灰。

## 查看

在 UE 内容浏览器打开 `Content/DreamPresentation/WhiteBoxPreview/Maps/L_WhiteBoxPreview`，点击“运行”。关卡使用预览专用 GameMode、固定相机，不生成角色和 HUD。

![白盒实际渲染截图](Images/WhiteBoxPreview.png)

本版本只做视觉预览，没有标题、开始提示、旋转或源关卡的解谜逻辑。原建筑的网格和原材质不被修改，材质覆盖只作用于预览关卡内的复制组件。

## 资源

| 资源 | 用途 |
| --- | --- |
| `Materials/M_WhiteBoxSurface` | 双面、粗糙、非金属的白色建筑表面 |
| `Materials/M_WhiteBoxPost` | 边缘深、面中心渐白的灰度后处理 |
| `Materials/MI_WhiteBoxPost` | 白盒参数实例 |
| `Maps/L_WhiteBoxPreview` | 白盒建筑静态快照和固定取景 |

打开 `MI_WhiteBoxPost` 可以调节：

| 参数 | 默认值 | 作用 |
| --- | --- | --- |
| `EdgeWidth` | 22.0 | 1080p 下渐变宽度（像素），随输出高度缩放 |
| `EdgeDarkness` | 0.30 | 柔和边缘压暗程度，设为 0 可完全移除边缘渐变 |
| `EdgeFalloff` | 1.60 | 连续衰减曲线，越小过渡越宽 |
| `NormalThreshold` | 0.32 | 墙角、折面的法线阈值 |
| `DepthThreshold` | 0.018 | 扣除平面透视变化后的深度偏差阈值 |
| `FaceBase` | 0.82 | 背光面的基础亮度，越高整体越白 |
| `FaceContrast` | 0.10 | 真实阴影的灰度强度 |
| `EdgeGray` | 0.12 | 渐变的目标灰度，默认只混合 30% |

`EdgeDarkness = 0` 时仍有法线明暗和真实遮挡阴影，所以不会变成没有立体感的纯白剪影。`LightDirection` 是决定面明暗的世界空间方向，可以调节向量，但不产生彩色灯光。

后处理从八个方向寻找当前可见面的边界，用三次二分细化边界距离，再按连续曲线减淡灰色，避免近、中、远固定权重产生色带。倒数深度梯度会过滤同一倾斜平面的正常透视变化；背景不参与渐变，也不会生成外侧灰色光晕。图中渐变宽度按屏幕计算，不是改动原网格或为每个三角形绘制线框。

## 重新生成

在包含上一版预览代码的项目工作树根目录运行。脚本依赖同目录的 `generate_white_preview.py`，重新生成会重置本版本的默认参数和相机，因此手动调参后先保留自己的实例或关卡副本。

```powershell
$previewProjectRoot = (Get-Location).Path
$previewProject = Join-Path $previewProjectRoot 'DreamSpace.uproject'
$previewGenerator = (Join-Path $previewProjectRoot 'Source/WhitePreview/generate_white_box_preview.py').Replace('\', '/')
& 'E:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' DreamSpaceEditor Win64 Development "-Project=$previewProject" -WaitMutex -NoHotReload
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  $previewProject `
  -run=pythonscript `
  "-script=$previewGenerator" `
  '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' `
  -unattended -AllowCommandletRendering -nosound -stdout
```

生成报告写入 `Saved/WhitePreview/white_box_generation_report.json`。真实渲染截图和 PIE 验收：

```powershell
$previewCapture = (Join-Path $previewProjectRoot 'Source/WhitePreview/capture_white_box_preview.py').Replace('\', '/')
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' `
  $previewProject `
  "-ExecutePythonScript=$previewCapture" `
  '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' `
  -unattended -nosound -NoSplash
```

脚本在单独启动的编辑器中渲染 1920x1080 和 1280x720 截图，再运行 PIE 验证相机、GameMode 及无角色/HUD，结束后自动关闭该编辑器。截图为 `Saved/WhitePreview/WhiteBoxPreview.png` 和 `WhiteBoxPreview_1280x720.png`，验收报告为 `Saved/WhitePreview/white_box_capture_report.json`。报告同时记录材质编译错误、实例参数和启用的后处理。
