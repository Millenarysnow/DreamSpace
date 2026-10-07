# 越肩 3C 与室内相机

## 接手摘要

- 用户选择近距离越肩构图，普通探索时身体朝移动方向转身：A/D 转身行走，S 转身走向镜头。
- 首轮实现集中在相机稳定性，保留 CharacterMovement、原有跳跃、重力玩法、输入动作和动画资产。
- 新增 `UDreamShoulderCameraComponent : USpringArmComponent`，角色的原生子对象仍叫 `CameraBoom`。
- 主相机理想臂长 210 cm、右肩偏移 45 cm、胶囊局部向上 60 cm、水平 FOV 80°。
- 滚轮目标范围 150～300 cm、每档 25 cm；实际碰撞距离可以小于 150 cm。
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
10. 镜头太近时仅对持有者隐藏人物网格；恢复距离较大，避免阈值闪烁。

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
| `OwnerHideDistance` / `OwnerShowDistance` | 90 / 115 cm | 镜头到胶囊中心的隐藏/恢复阈值 |

初次试调建议先只改变理想距离、肩位和 FOV，再改变恢复半衰期与等待。
不建议同时大幅改变碰撞球、构图和阻尼，否则难以判断是哪一项改善或恶化手感。

## 人物遮挡与表现边界

当前 Quinn 材质没有统一的透明淡出接口，因此第一版使用 `SetOwnerNoSee`，仅影响持有者视口。
胶囊碰撞和动画继续工作，手办显示面保持可见，其他玩家看到的网格不被全局隐藏。
退出播放、组件停用/注销、关闭近距隐藏或失去本地控制时恢复原本的 OwnerNoSee 值。
这不是渐变透明效果；若视觉验收发现切换明显，下一步应为角色材质增加一致的遮挡淡出参数。

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
屏幕显示距离、肩位比例、恢复等待、阻挡 Actor、起点穿透和人物隐藏状态。
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
6. 极窄空间：人物隐藏和恢复不反复闪烁，手办仍可看见；记录过近时的镜头问题。
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
人物隐藏/恢复阈值与停用清理、手办理想观察姿态以及普通 SpringArm 偏移回退。

测试发现并修复了一个恢复等待细节：在障碍旁停留很久后，仅从最后一次收近开始计时会提前耗尽等待。
现在距离和肩位各自检测“开始有更多空间”的时机并重新计时；等待到期后只对本帧剩余时间进行阻尼。
因而一帧无遮挡不会恢复远距离，也不需要在仍被部分遮挡、但空间增大时一直冻结相机。

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
- 近距离人物使用持有者隐藏，有不同的进入/退出阈值；尚未提供材质渐变透明。
- 第一版固定右肩，不自动切换左肩、不自动改变观察旋转、不按房间体积切换 FOV。
- 首轮没有重写角色动画、起停和转身规则；日常探索继续使用模板式自由转向。
  后续移动/动画打磨应基于相机实机验收结果进行，不把“相机更稳定”当成所有 3C 已完成。
- 默认手办仍采用 420 cm 的固定理想观察距离。新近距离构图下，手办的屏幕大小、人物遮挡和 Tab 点击
  是否舒适需要用户确认；不要直接缩短 ObserverArmLength 来修复主相机碰撞，这会改变手办取景。
- 下一轮优先依据上述视觉清单调整 210 cm / 45 cm / 80° 构图、恢复速度和人物隐藏阈值；
  对仍有跳动的位置记录阻挡 Actor、是否起点穿透、实际距离与肩位，再决定算法或资产调整。
