# 越肩 3C 与室内相机

## 接手摘要

- 用户选择近距离越肩构图，普通探索时身体朝移动方向转身：A/D 转身行走，S 转身走向镜头。
- 首轮实现集中在相机稳定性，保留 CharacterMovement、原有跳跃、重力玩法、输入动作和动画资产。
- 新增 `UDreamShoulderCameraComponent : USpringArmComponent`，角色的原生子对象仍叫 `CameraBoom`。
- 主相机理想臂长 210 cm、右肩偏移 45 cm、胶囊局部向上 60 cm、水平 FOV 80°。
- 滚轮目标范围 150～300 cm、每档 25 cm；实际碰撞距离可以小于 150 cm。
- 2026-10-08：整网格 `OwnerNoSee` 隐藏已替换为本地主视角的渐进局部剔除。最近时也保持网格提交渲染。
- 默认角色使用 `/Game/DreamCamera/Materials` 下独立的 Quinn 材质副本；原模板资产未修改。
- 代码、双目标编译和自动化验证已完成；目标关卡中的视觉与舒适度验收待用户执行。

## 原始问题与目标

原相机使用 420 cm 的普通 SpringArm。UE 5.8 默认在命中时直接采用扫掠位置，未命中时直接恢复理想位置。
绕墙角、门框或靠墙移动时，检测状态反复切换容易导致镜头快速推近、拉远。
Camera Lag 在原生实现中发生在碰撞修正之前，不能直接平滑碰撞回弹。

本轮目标：输入旋转直接响应；室内及时收近，空间恢复后短暂停留并缓慢拉远；
肩侧被挡时适度收窄肩位；缩放连续；近距离避免人物填满画面；重力和手办继续正确工作。
具体构图和阻尼数值是第一版试调值，仍需要目标关卡中的实机验收。

## 代码入口与职责

| 文件 | 职责 |
| --- | --- |
| `Source/DreamSpace/Public/Gameplay/DreamShoulderCameraComponent.h` | 调参属性、理想姿态接口、运行时状态定义 |
| `Source/DreamSpace/Private/Gameplay/DreamShoulderCameraComponent.cpp` | 重力相对高度跟随、肩位收窄、碰撞与恢复、网格遮挡、调试 |
| `Source/DreamSpace/Private/Gameplay/DreamCharacter.cpp` | 创建越肩相机，保持自由探索转向，设置 FOV |
| `Source/DreamSpace/Public/Gameplay/DreamPlayerController.h` | 滚轮目标距离范围和步长 |
| `Source/DreamSpace/Private/Presentation/DreamSceneCapturePresentationComponent.cpp` | 共用理想观察姿态，避免碰撞修正污染手办取景 |
| `Source/DreamSpace/Private/Gameplay/Tests/DreamShoulderCameraTests.cpp` | 独立物理世界中的自动化回归 |
| `Documents/Tools/GenerateOwnerClipMaterials.py` | 在 UE 编辑器 Python 环境重建独立材质副本；运行时无需 Python 插件 |
| `Documents/Tools/CheckOwnerClipRendering.py` | 独立 D3D12 编辑器进程输出固定构图的局部剔除/视角隔离对比图，不保存地图或资产 |

`CameraBoom` 的公开属性仍是 `USpringArmComponent*` 接口，不需要其他调用方替换所有引用。
真实类型可用 `Cast<UDreamShoulderCameraComponent>` 获取，参数也会显示在组件详情面板。
不增加自定义 PlayerCameraManager，也不在角色/控制器 Tick 再次设置相机位置。

## 每帧处理顺序

1. 从 SpringArm 取得控制器目标旋转，保持直接观察；理想锚点为 `ComponentLocation + TargetOffset`。
2. 平滑滚轮目标臂长；只沿当前重力反方向平滑楼梯/跳跃产生的高度变化，水平移动立即跟随。
3. 机关改变重力时将上一帧锚点搬到新参考系，避免沿旧世界 Z 轴滞后。传送超过阈值直接重建历史。
4. 验证实际锚点到高度跟随枢轴的碰撞路径，防止高度阻尼把枢轴带到障碍内部。
5. 用扩大后的预警球比较完整肩位和中轴的可用后退距离；中轴明显更宽松时收窄肩位。
6. 对插值后的肩位重新执行实际球和预警球扫掠。不能把两条安全路径的端点直接混合当作安全结果。
7. 预警距离变小时快速阻尼收近；任何时候都将结果夹在真实球扫掠的安全距离以内。
8. 可用空间增加后等待 `RecoveryDelay`，再按较慢半衰期恢复；肩位使用独立的恢复历史。
9. 更新继承的 SpringEndpoint Socket、未修正位置和碰撞标记，FollowCamera 自动跟随。
10. 查询镜头到动画物理资产最近表面的距离，平滑局部剔除强度/范围，更新本角色 MID；不隐藏整个网格。

阻尼采用 `1 - exp(-ln(2) * dt / halfLife)`，半衰期单位为秒。
它在固定目标下与帧率无关，不使用固定每帧 Lerp 系数。
急剧碰撞仍可能立即夹紧：安全优先于平滑，不能为了缓慢收近让镜头留在墙内。

## 调参表

| 参数 | 默认值 | 含义 |
| --- | --- | --- |
| `TargetArmLength` | 210 cm | 理想目标距离，碰撞不反写 |
| `SocketOffset` | (0,45,0) cm | 相机旋转空间中的右肩偏移 |
| `ProbeSize` | 18 cm | 实际相机球半径 |
| `ProbeChannel` | Camera | 独立于 Pawn 和 Visibility 的相机阻挡规则 |
| `AnticipationPadding` | 12 cm | 预警球额外半径 |
| `CollisionSafetyMargin` | 2 cm | 与阻挡表面的间隙 |
| `RetractionHalfLife` | 0.045 s | 预警收近半衰期 |
| `RecoveryHalfLife` | 0.16 s | 距离恢复半衰期 |
| `RecoveryDelay` | 0.2 s | 距离和肩位恢复前的等待 |
| `ShoulderNarrowingDistance` | 60 cm | 完全收窄所需的中轴后退空间优势 |
| `ShoulderRetractionHalfLife` | 0.055 s | 肩位收窄半衰期 |
| `ShoulderRecoveryHalfLife` | 0.18 s | 肩位恢复半衰期 |
| `ZoomHalfLife` | 0.08 s | 滚轮缩放半衰期 |
| `HeightFollowHalfLife` / `MaxHeightLag` | 0.06 s / 18 cm | 重力方向高度跟随和最大滞后 |
| `TeleportResetDistance` | 300 cm | 单帧锚点位移超过阈值时重置 |
| `bEnableOwnerLocalClipping` | true | 本地主视角渐进局部剔除开关 |
| `OwnerClipStartDistance` / `OwnerClipFullDistance` | 85 / 12 cm | 镜头到身体表面开始/完全加强的距离 |
| `OwnerClipMinRadius` / `OwnerClipMaxRadius` | 38 / 85 cm | 相机附近局部球的外半径范围 |
| `OwnerClipFeather` | 18 cm | 球外缘柔和过渡宽度 |
| `OwnerClipInHalfLife` / `OwnerClipOutHalfLife` | 0.045 / 0.12 s | 加强/恢复半衰期 |

初次试调建议先只改变理想距离、肩位和 FOV，再改变恢复半衰期与等待。
不建议同时大幅改变碰撞球、构图和阻尼，否则难以判断是哪一项改善或恶化手感。

## 人物遮挡与表现边界

### 从整网格隐藏迁移

首轮使用人物中心 90/115 cm 阈值切换 `SetOwnerNoSee`。用户实测仍会闪烁，因此已删除这组阈值和切换逻辑。
本轮不调用 `SetOwnerNoSee`、`SetHiddenInGame` 或 `SetVisibility` 改变人物可见性，也不把整个身体统一淡出。
极端压近时优先剔除镜头附近表面，保留剔除球外的腿部/外围身体。完整身体是否处在屏幕内仍受构图和近裁剪面限制。

### 连续参数与空间形状

1. `GetClosestPointOnPhysicsAsset(..., false)` 求当前动画简单体的最近表面距离；进入简单体内部按 0 cm 处理。
2. 缺少物理资产时退回胶囊表面距离，沿胶囊当前局部 Z 求线段最近点，支持重力翻转。
3. `p = saturate((StartDistance - SurfaceDistance) / (StartDistance - FullDistance))`。
4. 目标强度为 `p*p*(3-2*p)`；目标外半径在 38～85 cm 间随同一强度变化。
5. 强度和外半径分别按进入/恢复半衰期平滑。球心始终是本帧实际相机，不对位置额外阻尼，避免“洞追不上镜头”。
6. 材质按每个像素到镜头的世界厘米距离计算局部球：内核剔除、外缘 18 cm Smoothstep 过渡、球外完全保留。
7. 局部遮罩交给引擎 `DitherTemporalAA`，结果与原材质 `OpacityMask` 相乘；保持 Masked 光照与原模板贴图。
   强度过渡期间内核也通过抖动逐渐减弱；稳定到 1 后内核完全剔除，只有边缘处于柔和状态，球外保持不透明。

`GetOwnerClipAmount / GetOwnerClipRadius / GetOwnerSurfaceDistance` 可用于调试读取。
数值是首轮试调值，实际观感需在目标室内场景验收。物理资产是动画表面的近似，不是逐三角形距离查询。

### 材质资产与视角隔离

资产链为 `M_DreamOwnerClip → MI_QuinnOwnerClip_01 → MI_QuinnOwnerClip_02`，保持原 Quinn 的两层参数继承。
角色构造函数只覆盖本角色两个材质槽，不改 `SKM_Quinn_Simple` 或原始 `M_Mannequin / MI_Quinn`。
相机按材质槽创建自己的 MID，避免共享材质/全局参数集合使所有角色同时受影响。

材质同时要求：

- FollowCamera 的后处理 `UserFlags` 第 6 位（值 64）开启；该位由本功能保留，后续功能应使用其他位。
- 当前渲染镜头转换到网格局部空间后，与此角色发布的位置相差不超过 2 cm。
- 当前渲染镜头世界前方向与此角色发布的方向基本一致（dot ≥ 0.999）。

局部位置核对避免大世界绝对位置参数的精度问题，像素世界位置差先由引擎节点完成再进入 Custom。
手办 SceneCapture 显式覆盖清除第 6 位，默认仍排除持有角色；即使配置显示角色也不会继承主镜头剔除。
`ShadowReplace` 在阴影通道使用 1，随后仍乘原遮罩，让相机附近身体的局部剔除不挖掉常规投影。
其他位置的摄影相机、其他玩家的视角不匹配本角色姿态时也保留原材质。

新增材质参数：`DreamOwnerClipAmount`、`DreamOwnerClipRadius`、`DreamOwnerClipFeather`、
`DreamOwnerClipCameraLocal`、`DreamOwnerClipCameraForward`。未来替换人物材质时需接入相同接口。
不支持此接口的材质记录警告并保持可见，不回退到隐藏整个人物。附件/独立衣物网格尚未自动接入。

### 生命周期

退出播放、组件停用/注销、主动重置、关闭效果、切换 ViewTarget 或失去本地控制时清零旧 MID 并还原材质槽。
只还原仍指向本组件 MID 的槽，不覆盖外部换装；网格/材质槽变化时重新建立绑定。
原有 `OwnerNoSee` 配置由角色自身管理，本组件不覆盖它。
世界原点迁移不改变网格局部位置参数；下一次相机更新会重发真实位置。

### 视觉边界

这是一种带软边的局部剔除，不是物理透明材质。目标是保留身体存在感并露出环境，避免整个人物突然消失。
时间抗锯齿/TSR 能积累柔和边缘；快速运动、低分辨率、FXAA/无抗锯齿下可能看见颗粒或拖影，需要实机检查。
常规阴影通道保留完整身体；Lumen 屏幕追踪和光追反射仍可能受到视角相关 Masked 表面的影响，不能据 NullRHI 测试
认定所有 GI/反射完全不变。强度和半径仍会因急剧相机夹紧而快速变化，但不再切换整个网格可见性。

## 重力与手办约定

- 枢轴挂胶囊，肩偏移使用相机旋转空间，跟随高度使用当前 `-GravityDirection`，不硬编码世界 Z。
- 机关仍按平台四元数增量搬运角色和控制旋转，相机直接继承该旋转，不再叠加一次重力补偿。
- `GetIdealCameraTransform` 输出未碰撞的完整肩位、稳定枢轴与当前旋转。
- 手办的 `bIgnoreSpringArmCollision=true` 时使用该接口，保持建筑画面不受主相机收近/收肩影响。
- `bFollowCameraZoom=false` 继续使用旧 `ObserverArmLength=420`，保持原有景别；开启后使用平滑臂长。
- 普通 SpringArm 回退公式也已按引擎约定修正：TargetOffset 是世界空间，SocketOffset 是相机旋转空间。
- 点击继续从真实视口求显示面 UV，再按 SceneCapture 投影反算世界射线；不要求主相机与捕获相机重合。

## 关卡碰撞与调试

开启 `dream.DebugShoulderCamera 1`，关闭用 `dream.DebugShoulderCamera 0`。
绿球是实际避障枢轴，白球/白线是未受阻构图，橙球/橙线是实际镜头，红点是实际命中点。
屏幕显示距离、肩位比例、恢复等待、阻挡 Actor、起点穿透、局部剔除强度/半径和身体表面距离。
手办的 `dream.DebugSceneCapture 1` 与点击的 `dream.DebugMiniatureInteraction 1` 仍可使用。

墙、天花板、大型家具一般 Block Camera；小装饰可按遮挡效果 Ignore Camera。
修改 Camera 响应时不必修改 Pawn 或 Visibility，避免角色穿墙或交互射线失效。
检查门框的简单碰撞是否封住门洞、整栋模型是否被一个大凸体覆盖。
本轮不批量重写二进制关卡/网格碰撞资产，真实场景中有问题的物体需依据调试命中逐个调整。

## 视觉验收清单

1. 开放区域静止、起步、停止、A/D 和 S：人物自由转身，镜头不自动转向，正常处于左侧构图。
2. 靠墙行走，同时快速/缓慢转镜头：相机及时收近，不穿墙，无遮挡后缓慢恢复。
3. 往返穿门、绕墙角和柜子：没有快速远近抽动，肩侧受阻时逐渐回到中轴。
4. 上下楼梯、跳跃：高度变化适度平滑，人物不长期落出构图，落地后不持续漂移。
5. 连续滚轮缩放：150～300 cm 内连续变化，靠墙滚轮不覆盖避障结果。
6. 极窄空间：身体不再整块突然消失；肩背/头部局部区域连续扩大，外围身体仍显示，离墙逐渐恢复。
7. 平台旋转到墙面/天花板、跳离：相机上方向正确，输入方向正确，没有第二次重力旋转。
8. Tab 手办模式、中心/边缘点击：画面稳定，点击对象与显示一致，返回探索可继续旋转视角。

## 实现与验证记录

2026-10-07，UE 5.8.2（CL 56702186），Win64 Development：

- `DreamSpaceEditor` 编译成功，新增相机和测试也完成了非 Unity 独立编译检查。
- `DreamSpace` 游戏目标编译成功。
- 完整 `DreamSpace` 自动化套件 26 项通过，其中新增 `DreamSpace.Camera.Shoulder` 8 项。
- 原有手办投影/点击 7 项、机关碰撞和重力 11 项继续通过。
- 默认关卡 `/Game/DreamInteraction/Maps/InteractionDemo` 成功启动：运行时 GameMode 为 `MainGameMode`，
  Pawn 为原生 `DreamCharacter`，CameraBoom 为 `DreamShoulderCameraComponent`，实际目标臂长 210 cm、肩偏移 (0,45,0)。
- 自动化日志：`Saved/Logs/ShoulderCameraRegression.log`；关卡启动日志：`Saved/Logs/ShoulderCameraMapSmoke.log`。
- 本轮测试与关卡启动使用 `-nullrhi`，没有进行画面截图、材质渲染或人工手感验收。以上结果不能代替视觉验收。

新增测试覆盖实际碰撞球的逐帧重叠查询、延迟恢复、反复命中/未命中、侧向收肩和恢复、
运动时转镜头、Camera 通道隔离、30/60/120 FPS 缩放、重力参考系变化、高度跟随、传送重置、
当时的整人物隐藏/恢复阈值与停用清理、手办理想观察姿态以及普通 SpringArm 偏移回退。

测试发现并修复了一个恢复等待细节：在障碍旁停留很久后，仅从最后一次收近开始计时会提前耗尽等待。
现在距离和肩位各自检测“开始有更多空间”的时机并重新计时；等待到期后只对本帧剩余时间进行阻尼。
因而一帧无遮挡不会恢复远距离，也不需要在仍被部分遮挡、但空间增大时一直冻结相机。

2026-10-08 局部剔除改版（下列结果为本轮，前述 26 项为首轮历史记录）：

- `DreamSpaceEditor` 和 `DreamSpace` Win64 Development 双目标编译通过。
- 完整 `DreamSpace` 套件 28 项全部通过，当前相机套件 10 项；原先的二元隐藏测试已替换。
- 新检查包括：极近仍不切换人物可见性，两槽 MID 强度/范围/实际位置同步，网格位置变化影响表面距离，
  连续恢复，30/60/120 FPS 一致性，无物理资产的胶囊回退，关闭/重置/停用/切镜头/失去控制清理，
  其他角色材质隔离与外部换装保护。原有避障、重力和手办回归继续通过。
- 逻辑回归日志：`Saved/Logs/OwnerLocalClipRegression.log`，使用 NullRHI，不验证材质像素。
- 材质生成日志：`Saved/Logs/CameraClipMaterialGenerate.log`，三个独立资产成功生成。
- 使用完整 `UnrealEditor.exe -RenderOffscreen -d3d12` 完成实际 GPU 材质渲染检查，输出五组 960×720 PNG。
  固定镜头位于肩背附近，使用参考姿势和 BaseColor 捕获；已人工查看原图、半强度和完全局部剔除图。
  局部区域随强度/范围扩大，肩背被剔除后仍保留下肢和外围手部；没有切换整个网格的可见性。
- 在 43,200 个 RGB 采样点的对比中：完全局部剔除改变 11,324 点，仍有 3,243 个身体点；
  半强度改变 1,254 点，仍有 13,232 个身体点。两组足部采样变化均为 0。
  清除视角标记、故意发布不匹配镜头位置两组均与原图一致（变化为 0），验证实际渲染中的视角隔离。
- 渲染日志：`Saved/Logs/OwnerLocalClipRenderLive.log`；截图：`Saved/CameraClip/RenderChecks/`。
  对应 `NearOriginal.png`、`NearHalfClip.png`、`NearSoftClip.png`、`NearCaptureNoFlag.png` 和 `NearDifferentView.png`。
- 本 GPU 检查只验证局部材质图与隔离条件；BaseColor 场景捕获不等于实际游戏主视口，
  不覆盖快速移动的 TSR 积累、室内光照/反射、动画构图或主观手感。目标关卡视觉验收仍待用户完成。

## 后续 Agent 的复现步骤

工程位于 `F:\Ue5 Project\DreamSpace-better_3C`，引擎位于 `E:\Epic Games\UE_5.8`。
正常调试仍通过项目 GameMode 生成原生角色；无需切换到旧 `BP_DreamCharacter`。
包含新原生组件类型，首次验收建议重启编辑器使用完整编译后的 DLL，避免只用 Live Coding 替换默认对象。
仓库现有 `.gitignore` 会隐藏部分目录搜索和新增文件；检索源码时可用 `rg --files --hidden --no-ignore Source Documents`。
本次新增文件已显式纳入 Git，后续新增同类文件需检查是否被忽略，避免代码存在于本机却没有提交。

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' DreamSpaceEditor Win64 Development '-Project=F:\Ue5 Project\DreamSpace-better_3C\DreamSpace.uproject' -WaitMutex -NoHotReloadFromIDE
& 'E:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' DreamSpace Win64 Development '-Project=F:\Ue5 Project\DreamSpace-better_3C\DreamSpace.uproject' -WaitMutex -NoHotReloadFromIDE
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'F:\Ue5 Project\DreamSpace-better_3C\DreamSpace.uproject' '-ExecCmds=Automation RunTests DreamSpace' '-TestExit=Automation Test Queue Empty' -unattended -nop4 -nosplash -nullrhi '-abslog=F:\Ue5 Project\DreamSpace-better_3C\Saved\Logs\ShoulderCameraRegression.log'
```

## 已知边界与下一轮

- 突然出现的障碍仍需要立即夹紧安全距离；预警球减少突变，不承诺消除所有急剧收近。
- 实际查询起点已在障碍内部时回退到枢轴并显示起点穿透诊断，没有实现通用穿透解算器。
  若角色能合法站立而相机起点穿透，应优先检查过大的碰撞体、枢轴位置和 ProbeSize。
- 使用 Camera 通道的简单碰撞；视觉模型与碰撞体不一致时需逐个配置场景资产。
- 近距离人物已改为局部软剔除；仍需检查时间抖动的颗粒、拖影和极近时剩余身体构图。
- 第一版固定右肩，不自动切换左肩、不自动改变观察旋转、不按房间体积切换 FOV。
- 首轮没有重写角色动画、起停和转身规则；日常探索继续使用模板式自由转向。
  后续移动/动画打磨应基于相机实机验收结果进行，不把“相机更稳定”当成所有 3C 已完成。
- 默认手办仍采用 420 cm 的固定理想观察距离。新近距离构图下，手办的屏幕大小、人物遮挡和 Tab 点击
  是否舒适需要用户确认；不要直接缩短 ObserverArmLength 来修复主相机碰撞，这会改变手办取景。
- 下一轮优先依据上述视觉清单调整 210 cm / 45 cm / 80° 构图、恢复速度和局部剔除范围；
  对仍有跳动的位置记录阻挡 Actor、是否起点穿透、实际距离与肩位，再决定算法或资产调整。

## 局部剔除资产复现与验收

材质资产已生成并随源码纳入 Git；正常打开项目和打包不需要启用 Python。
只有需要从原模板重建副本时，在 UE 编辑器的 Python 环境执行下面命令，脚本只保存 `/Game/DreamCamera/Materials`：

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'F:\Ue5 Project\DreamSpace-better_3C\DreamSpace.uproject' -run=pythonscript '-script=F:\Ue5 Project\DreamSpace-better_3C\Documents\Tools\GenerateOwnerClipMaterials.py' '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' -unattended -nop4 -nosplash -nullrhi '-abslog=F:\Ue5 Project\DreamSpace-better_3C\Saved\Logs\CameraClipMaterialGenerate.log'
```

重跑时保留已生成图表与实例参数；若要修改 Custom 图表，请在材质编辑器里编辑已有节点，同时同步生成脚本，
不能仅修改脚本就假定已有资产已经更新。原始资产若改变继承结构，先检查本生成器的模板假设。

实际渲染检查可用以下命令复现。必须启动独立的完整编辑器进程，不能使用 `-nullrhi` 或 Python commandlet。
脚本会创建瞬时空白地图、导出五组图片并退出，因此不要在有未保存工作的已有编辑器中直接运行。
原图应显示正常身体，半强度/完全局部剔除图应逐渐减少肩背遮挡，最后两张应保持原图外观。
图像和日志只写入 `Saved`，不保存地图、材质或项目配置；此脚本提供对比图，不自动判定手感通过。

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' 'F:\Ue5 Project\DreamSpace-better_3C\DreamSpace.uproject' '-ExecutePythonScript=F:\Ue5 Project\DreamSpace-better_3C\Documents\Tools\CheckOwnerClipRendering.py' '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' -RenderOffscreen -d3d12 -unattended -nop4 -nosplash -nosound '-abslog=F:\Ue5 Project\DreamSpace-better_3C\Saved\Logs\OwnerLocalClipRenderLive.log'
```

用户重点验收：

1. 在出现原先闪烁的位置反复靠墙/转镜头：确认整个人物没有突然消失与出现。
2. 对比正常距离和极近距离：肩背/头部遮挡逐渐减小，球外身体没有变成整片半透明。
3. 贴墙走动、转身、跳跃：留意边缘颗粒、拖影、局部洞是否突兀；不应有两块材质接缝。
4. 离开墙面：剔除范围平滑缩小，人物恢复正常外观，开放区域没有残留孔洞。
5. 平台重力翻转、Tab 手办交互、切换视角、停止并重新播放：显示和点击仍正常。
