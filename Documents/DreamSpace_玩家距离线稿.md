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

深度和法线缓冲仍使用渲染分辨率和时间抖动。HLSL 显式补偿当前投影抖动，以四点插值重建用于排线和常规结构线的倒数深度与法线。细几何另在原始 GBuffer texel 上检测双侧深度 / 法线转折，再插值墨线覆盖率；广告文字等细节则从已完成 TSR 的输出画面提取局部色彩二阶差分，补充纯几何检测缺失的笔画。排线继续使用固定世界空间的三平面投影。

距离遮罩由 `ProximitySketchMask.hlsl` 提供，生成器将同一份实现内嵌到两个材质。每个真实深度 texel 独立逆投影并计算玩家距离，再插值范围权重，避免把栏杆与背后墙面平均成不存在的表面。第二阶段只在同一距离遮罩内平滑新增线条，不修改全局 `r.AntiAliasingMethod`。

外圈明暗结合主方向光法线受光与色调映射前的真实场景亮度。后者使用基础色估计光照，降低深色贴图直接产生浓重排线的问题；金属与极黑表面则逐渐退回法线依据。这是风格化的光照估计，不是对独立阴影缓冲的物理分离。

手办的 SceneCapture 不读取玩家相机缓存，保持其自己的原始渲染。手办透明显示面不写普通场景深度，因此新增专用材质 `M_ProximityMiniatureDisplay`：保留原显示图，只允许可见透明像素写入 CustomDepth，以 Stencil **241** 标记。距离遮罩选择它比不透明建筑更近的深度，使玩家旁边的手办继续正常显示；为此项目启用 `r.CustomDepth=3`。241 是本效果的预留值，不应分配给其它物体。现有自定义显示材质若也要支持此效果，应开启 Allow Custom Depth Writes。

普通透明玻璃、粒子和不写深度的其它表面暂时按背后不透明表面的距离处理。新的细节检测可以利用 TSR 已保留的笔画，但无法恢复原始画面中也完全不可见的几何。低渲染比例下的运动稳定性、密集栏杆的线宽和重力切换仍需要在目标画质中做主观验收。

## 2026-10-11：第一次细节检测修复

此项修复用于保留原始画面中已有的细笔画。用户随后反馈的整段扶手缺失、尖头立杆和残缺广告字，还涉及房间 Nanite 回退网格的几何简化，见下一节；第一次材质修复不能单独解决这类缺失。

在当前 `E:\UE5_program\DreamSpace` 工程的 TEST 地图中复现：保持同一外观相机，将 TSR 渲染比例设为 50%，原始画面仍能看见广告文字，但旧线稿里的“南”“珍”等笔画几乎消失；关闭末端平滑仍有问题。原因是主通道先混合法线 / 深度，再用输出像素大小的步长做非线性边缘判断，低分辨率中的细小特征会在判断前被削弱。只有颜色或发光差异的细节也缺少对应检测。

修复包含原始 texel 的细几何检测、最终画面的文字细节检测，以及两个通道共用的逐 texel 距离遮罩。对应 HLSL、生成器和验收脚本均有中文注释；实际材质 `.uasset` 已更新，打开 TEST 运行即可使用。

以下参数位于 `MI_ProximitySketch`，可在需要时调节：

| 参数 | 默认值 | 用途 |
| --- | --- | --- |
| `ThinGeometryStrength` | 0.85 | 细栏杆、浅浮雕的补线浓度；0 关闭该补线 |
| `DetailStrength` | 0.8 | TSR 输出中的文字 / 色彩细节补线浓度；0 关闭 |
| `DetailThreshold` | 0.12 | 局部二阶色彩对比阈值；增大可减少贴图纹理点，降低可保留更浅笔画 |

真实 GPU 对照由 `Source/ProximitySketch/capture_sketch_details.py` 生成，在独立编辑器中运行：

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' `
  'E:\UE5_program\DreamSpace\DreamSpace.uproject' `
  '-ExecutePythonScript=E:/UE5_program/DreamSpace/Source/ProximitySketch/capture_sketch_details.py' `
  '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' `
  -SketchDetailLabel=After -unattended -nosound -NoSplash -Multiprocess
```

脚本只读 TEST，创建临时相机，使用真实玩家相机的动态实例，以 100% / 50% 渲染比例分别采集玩家视角、外观视角、原图、线稿、关闭平滑、重复帧和距离遮罩；结束后退出专用编辑器。输出位于 `Saved/ProximitySketch/DetailFix/After`，修复前对照保留在 `Before`。

本次 1730 × 658 的真实 PIE 像素检查中，按原始画面的字形限制统计，50% 渲染比例的“南”可见墨线像素从 139 增至 741，“珍”从 90 增至 744。近圈与同环境原图的平均通道差为 0.57 / 255（100%）和 0.73 / 255（50%），保留正常渲染。截图、量化结果和放大对照图位于 `Saved/ProximitySketch/DetailFix`，Saved 不进入 Git，需要保留证据时单独复制。

GPU 使用 UE 5.8.2 / D3D12 / SM6，最终两个线稿材质没有编译错误。现有 `verify_proximity_sketch.py` 的 9 项 GPU 像素回归全部通过，覆盖近圈原图、外圈、过渡、排线、扩大半径、固定相机移动玩家和全局抗锯齿保持；`DreamSpace.Presentation.ProximitySketch.PlayerLifecycle` 原生自动化测试通过。

手动验收：靠近和远离二楼走廊栏杆、观察广告牌“南海珍品”，确认近圈、过渡、外圈的细线连续性；再边走边转镜头，并在建筑旋转 / 重力改变后复查。当前静止截图已验证笔画恢复，尚未替代这些运动观感验收。

## 2026-10-11：房间 Nanite 回退几何修复

针对反馈截图中的整段扶手消失、立杆呈尖头 / 虚线，固定相机及玩家位置对照完整 Nanite 与回退路径：原房间在 `r.Nanite 1` 下栏杆完整，`r.Nanite 0` 下出现一致的截断形态，广告“南海”也只剩少量三角形。通过 `dream.Sketch.Enabled 0` 完全关闭两个后处理后，原始画面仍然缺杆、缺字，因此这部分缺失发生在线稿输入之前。现场编辑器为何选择回退路径尚未确认，不能由截图推断用户的实际 CVar 值。

房间将建筑、栏杆和立体文字合并在一个静态网格中，旧的 Auto 回退简化按整栋建筑的相对误差处理，容易删除占比很小的细部。当前已重建并保存 `Content/0_/Meshes/Room/room.uasset`，只调整以下 Nanite 回退设置：

| 设置 | 修复前 | 修复后 |
| --- | --- | --- |
| Generate Fallback | Platform Default | Enabled |
| Fallback Target | Auto | Percent Triangles |
| Fallback Percent Triangles | 100% | 100% |
| Fallback Relative Error | 1.0 | 0.0 |
| 实际回退三角形数 | 4,683 | 54,000 |
| 实际回退顶点数 | 8,767 | 89,260 |

源 MeshDescription 的顶点、三角形关联和 UV 哈希前后一致，源三角形数为 54,000；材质引用、槽位、显式读取的碰撞配置、普通 LOD 构建 / 简化设置以及其余 Nanite 设置核对一致。回退缓冲的顶点会因属性拆分而多于源顶点，不能直接以顶点数是否相同判断源模型是否改变。本次重建会更新派生渲染及碰撞数据；房间采用 Complex As Simple 碰撞，完整回退几何也可能增加复杂碰撞成本，需要一并验收走廊行走和栏杆附近的碰撞。

Nanite 关闭或不能用于该网格时，现在使用完整回退网格。代价是回退数据和该路径的绘制三角形增加，尚未做性能基准；没有修改项目的全局 Nanite 开关。本次也没有新增原生代码或修改后处理材质。

修复工具有详细中文注释，先备份资源、读取源几何与配置，再构建、核对并保存。工具必须通过**完整编辑器**的 `-ExecutePythonScript` 运行，`-run=pythonscript` commandlet 不提供所需的 StaticMeshEditorSubsystem：

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' `
  'E:\UE5_program\DreamSpace\DreamSpace.uproject' `
  '-ExecutePythonScript=E:/UE5_program/DreamSpace/Source/ProximitySketch/repair_room_fallback.py' `
  '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' `
  -unattended -nosound -NoSplash -Multiprocess
```

修复已经保存，正常使用不需要重跑工具。修复报告和首份备份位于 `Saved/ProximitySketch/DetailFix/RoomFallbackRepair`，工具结束后退出专用编辑器。

新增真实 GPU 对照工具 `capture_sketch_fallback.py`，在相同相机和玩家位置采集近处栏杆 / 外观广告、Nanite / fallback、100% / 50% 渲染比例、原图 / 线稿，共 16 帧。原图组完全关闭两个线稿通道；近景使用正常半径 600 厘米和过渡 150 厘米，外观广告强制处于完全线稿区。临时相机、玩家位置与 CVar 不保存到地图，脚本结束后退出专用进程：

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' `
  'E:\UE5_program\DreamSpace\DreamSpace.uproject' `
  '-ExecutePythonScript=E:/UE5_program/DreamSpace/Source/ProximitySketch/capture_sketch_fallback.py' `
  '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' `
  -SketchDetailLabel=FallbackReproAfter -unattended -nosound -NoSplash -Multiprocess -windowed
```

在含 Pillow / NumPy 的普通 Python 中运行：

```powershell
python Source/ProximitySketch/verify_sketch_fallback.py `
  --before Saved/ProximitySketch/DetailFix/FallbackReproBefore
```

`--before` 可省略，新截图目录可用 `--after` 指定。当前像素验证使用此次 2109 × 1146 视口的固定 ROI；其它视口尺寸会明确报错，需要先调整区域，避免无效比较。验证工具先核对 16 帧模式和相机 / 玩家位置，再以同批 Nanite 原图中的近处深色栏杆作为参考，避开角色与 HUD。此次结果如下：

| 渲染比例 / 模式 | 修复前回退栏杆覆盖率 | 修复后 |
| --- | --- | --- |
| 100% 原图 | 32.97% | 99.96% |
| 100% 线稿开启 | 37.08% | 100.00% |
| 50% 原图 | 32.88% | 99.92% |
| 50% 线稿开启 | 37.86% | 100.00% |

覆盖率仅指这个相机下的近处栏杆区域，不代表整个场景无视觉问题。远处“南海”字形恢复由同相机截图人工核对。修复前 / 后 16 帧位于 `Saved/ProximitySketch/DetailFix/FallbackReproBefore` 和 `FallbackReproAfter`；像素报告为 `fallback_verification.json`，裁剪对照为 `fallback_comparison.png`。当前两个截图报告均无错误，现有 `verify_proximity_sketch.py` 的 9 项整体 GPU 像素回归也全部通过。

手动验收时先重启编辑器，确保加载磁盘上重建后的 room 资源，再回到反馈截图的位置检查扶手、立杆和广告字。继续行走、转镜头、跨越过渡范围及改变重力，确认运动观感和走廊碰撞；本次固定相机截图不替代这些验收。

## 更新与验收

材质 HLSL 被完整嵌入 `.uasset`，运行和打包不依赖 Python 插件、外部代码路径或命令行。修改源码后，用 UE 编辑器 Python 增量更新专用资源：

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'E:\UE5_program\DreamSpace\DreamSpace.uproject' -run=pythonscript `
  '-script=E:/UE5_program/DreamSpace/Source/ProximitySketch/generate_proximity_sketch.py' `
  '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' `
  -unattended -AllowCommandletRendering -nosound -Multiprocess
```

生成器复用现有节点，不重复增加参数，保留实例调参，不保存地图。资源由原生相机及手办组件引用，能被烘焙发现。只修改 HLSL / 材质时重新生成资源即可；修改原生相机或组件代码后再编译 `DreamSpaceEditor` 和 `DreamSpace`。

真实渲染截图脚本只在专门启动的验收编辑器中使用：

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'E:\UE5_program\DreamSpace\DreamSpace.uproject' `
  '-ExecutePythonScript=E:/UE5_program/DreamSpace/Source/ProximitySketch/capture_proximity_sketch.py' `
  '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' `
  -unattended -nosound -NoSplash -Multiprocess
```

输出在 `Saved/ProximitySketch`：默认、重复帧、关闭排线、原始场景、距离遮罩、关闭效果、扩大半径和固定镜头平移玩家的截图，以及读回真实运行参数的 `capture_report.json`。脚本结束 PIE 并恢复原有控制台数值，不保存测试位置、相机或临时 MID。

在含 Pillow / NumPy 的普通 Python 中运行 `Source/ProximitySketch/verify_proximity_sketch.py`，比较 GPU 像素而不是重复距离公式。原生自动化测试 `DreamSpace.Presentation.ProximitySketch.PlayerLifecycle` 验证本地实例隔离、缓存不重复添加、取消占有、换角色、镜头与球心分离、关闭开关及局部平滑资源。

视觉验收重点：默认范围是否合适；近远过渡是否太宽；行走、转镜头或旋转建筑时远处细线是否舒服；手办的可见像素是否保留颜色；上下楼层和重力翻转时球形范围是否符合预期。
