# 玩家附近正常渲染，远处手绘线稿

游玩时以当前被控制的 Pawn 为球心，6 米内保留原始材质、光照和 TSR 结果；向外经过 1.5 米过渡后，建筑变为白纸、黑色轮廓和按阴影变化的铅笔排线。范围包含 XYZ，适用于上下楼层及重力改变，不随第三人称镜头缩放或转动而移动。

从开始界面进入游戏，或打开 `/Game/0_/Maps/TEST` 点击运行即可体验。效果通过 `DreamPlayerController` 的 `DreamProximitySketchCameraManager` 自动安装，无需给建筑换材质或在地图里放后处理体积。编辑器非运行视口仍显示原始场景。

## 调节范围

运行中按 `~` 打开控制台，可输入：

| 命令 | 作用 |
| --- | --- |
| `dream.Sketch.Radius 600` | 正常渲染半径，厘米；600 = 6 米 |
| `dream.Sketch.Transition 150` | 从原始渲染到完全线稿的过渡宽度，厘米 |
| `dream.Sketch.Enabled 0` | 关闭两个线稿通道，查看原场景 |
| `dream.Sketch.Enabled 1` | 开启效果 |
| `dream.Sketch.Debug 2` | 黑色为内圈、灰色为过渡、白色为外圈的遮罩 |
| `dream.Sketch.Debug 0` | 恢复正式效果 |

各命令设为 `-1` 则恢复相机配置。控制台值是进程内临时覆盖，不保存到地图；它会跨同一编辑器进程的 PIE 会话保留，需要主动恢复。永久默认值在 `Config/DefaultGame.ini` 的 `[/Script/DreamSpace.DreamProximitySketchCameraManager]` 中。

相机类同时公开有中文提示的 `bEnableProximitySketch`、`NormalRadius`、`TransitionWidth`、`EffectStrength` 属性，可通过蓝图或 C++ 动态修改。每位本地玩家使用自己的动态材质实例；换 Pawn 自动更新球心，取消占有或退出关卡后不再提交效果。

排线、轮廓的美术参数位于 `/Game/DreamPresentation/ProximitySketch/Materials/MI_ProximitySketch`，沿用白色建筑预览的 `HatchSpacing`、`HatchStrength`、`HatchWidth`、`HatchToneBias`、`StructureStrength` 等名称。范围参数和玩家位置由相机每帧写入，应通过配置或控制台调节。`MI_ProximitySketchAA` 控制新增线条的 `SmoothingStrength`，默认 1。

## 实现与适用范围

先让原场景完成自动曝光、TSR 和调色，再在 `After Tonemapping` 阶段按世界距离混合原图与线稿。这样外圈白纸不参与自动曝光计量，内圈不会被压暗，完全处于内圈的像素也不经过新增的线稿平滑。

深度和法线缓冲仍使用渲染分辨率和时间抖动。HLSL 显式补偿当前投影抖动，以四点插值重建倒数深度与法线，再用同一深度逆投影得到世界位置。描边使用连续覆盖率及倒数深度曲率，排线使用固定世界空间的三平面投影。第二阶段只在同一距离遮罩内平滑新增线条，不修改全局 `r.AntiAliasingMethod`。

外圈明暗结合主方向光法线受光与色调映射前的真实场景亮度。后者使用基础色估计光照，降低深色贴图直接产生浓重排线的问题；金属与极黑表面则逐渐退回法线依据。这是风格化的光照估计，不是对独立阴影缓冲的物理分离。

手办的 SceneCapture 不读取玩家相机缓存，保持其自己的原始渲染。手办透明显示面不写普通场景深度，因此新增专用材质 `M_ProximityMiniatureDisplay`：保留原显示图，只允许可见透明像素写入 CustomDepth，以 Stencil **241** 标记。距离遮罩选择它比不透明建筑更近的深度，使玩家旁边的手办继续正常显示；为此项目启用 `r.CustomDepth=3`。241 是本效果的预留值，不应分配给其它物体。现有自定义显示材质若也要支持此效果，应开启 Allow Custom Depth Writes。

普通透明玻璃、粒子和不写深度的其它表面暂时按背后不透明表面的距离处理。远处手绘线在几何遮挡交界或极细栏杆处仍可能随 GBuffer 采样轻微变化，尤其当 TSR 渲染比例较低时；需要在目标画质、实际移动和重力切换中做主观验收。

## 更新与验收

材质 HLSL 被完整嵌入 `.uasset`，运行和打包不依赖 Python 插件、外部代码路径或命令行。修改源码后，用 UE 编辑器 Python 增量更新专用资源：

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'F:\Ue5 Project\DreamSpace-UI\DreamSpace.uproject' -run=pythonscript `
  '-script=F:/Ue5 Project/DreamSpace-UI/Source/ProximitySketch/generate_proximity_sketch.py' `
  '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' `
  -unattended -AllowCommandletRendering -nosound -Multiprocess
```

生成器复用现有节点，不重复增加参数，保留实例调参，不保存地图。先生成资源，再编译 `DreamSpaceEditor` 和 `DreamSpace`。资源由原生相机及手办组件引用，能被烘焙发现。

真实渲染截图脚本只在专门启动的验收编辑器中使用：

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'F:\Ue5 Project\DreamSpace-UI\DreamSpace.uproject' `
  '-ExecutePythonScript=F:/Ue5 Project/DreamSpace-UI/Source/ProximitySketch/capture_proximity_sketch.py' `
  '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' `
  -unattended -nosound -NoSplash -Multiprocess
```

输出在 `Saved/ProximitySketch`：默认、重复帧、关闭排线、原始场景、距离遮罩、关闭效果、扩大半径和固定镜头平移玩家的截图，以及读回真实运行参数的 `capture_report.json`。脚本结束 PIE 并恢复原有控制台数值，不保存测试位置、相机或临时 MID。

在含 Pillow / NumPy 的普通 Python 中运行 `Source/ProximitySketch/verify_proximity_sketch.py`，比较 GPU 像素而不是重复距离公式。原生自动化测试 `DreamSpace.Presentation.ProximitySketch.PlayerLifecycle` 验证本地实例隔离、缓存不重复添加、取消占有、换角色、镜头与球心分离、关闭开关及局部平滑资源。

视觉验收重点：默认范围是否合适；近远过渡是否太宽；行走、转镜头或旋转建筑时远处细线是否舒服；手办的可见像素是否保留颜色；上下楼层和重力翻转时球形范围是否符合预期。
