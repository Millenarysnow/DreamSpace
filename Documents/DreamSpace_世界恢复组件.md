# 世界恢复交互组件

新增 **Dream World Recovery**（`UDreamWorldRecoveryComponent`），可以直接添加到机关 Actor。
玩家与所属 Actor 交互后，正常渲染的球形范围会从当前半径逐渐扩大，呈现线稿世界恢复原始材质、颜色和光照的效果。

默认从项目原有的 600 cm 正常半径开始，在 **5 秒**内扩大到 **10000 cm（100 米）**。
前 75% 时段主要通过半径扩张恢复场景；最后 25% 时段同时平滑淡出剩余线稿，让天空和目标半径以外的远景一起恢复。
完成后不再向玩家相机提交线稿和平滑两个通道。

## 在关卡中配置

1. 编译 `DreamSpaceEditor` 并打开工程，在目标 Actor 的 **添加组件**中搜索 `Dream World Recovery`。
2. 在组件的 **世界恢复 → 范围**中设置目标正常渲染半径和恢复时长。距离单位为厘米，时间单位为游戏秒。
3. 所属 Actor 的网格或碰撞体需要阻挡控制器使用的交互通道，项目默认是 **Visibility**。
4. 使用现有 `DreamPlayerController` 运行关卡，瞄准 Actor 按 **E**。手办内可沿用 **Tab → 左键**交互。

组件不需要枢轴点、特殊 Actor 父类、Timeline、新输入资源或新的后处理体积。
纯交互机关也不需要设为 Movable，因为本组件只修改本地玩家相机的瞬时参数，不移动 Actor。
此次没有把组件自动加入 TEST 的任何现有机关，需要按关卡设计选择实际触发物体。

| 详情面板属性 | 默认值 | 行为 |
| --- | --- | --- |
| 目标正常渲染半径 `TargetNormalRadius` | 10000 cm | 正常球形区域的目标大小，从交互时的实际半径开始扩张 |
| 恢复时长 `RecoveryDuration` | 5 秒 | 起止平滑的固定时长插值；0 立即完成 |
| 恢复整个世界 `bRestoreEntireWorld` | 开启 | 动画末段额外淡出剩余线稿，完成后包含天空、远景在内的场景正常渲染 |
| 仅触发一次 `bTriggerOnce` | 开启 | 每个组件实例只接受一次成功请求；失败请求不消耗次数 |

正常区域始终以**当前被控制的 Pawn 的世界 XYZ 位置**为球心。
玩家移动、上下楼或重力切换时，球心继续跟随角色；机关位置、第三人称镜头位置和手办观察相机不会替代球心。

## 分阶段恢复

若只想逐步解锁正常渲染范围，关闭组件的 **恢复整个世界**，为多个机关依次设置例如 1500、3000、6000 cm 的目标。
每段结束后保持对应半径，目标之外继续显示线稿。最后一个机关可以开启 **恢复整个世界**，完成最终恢复。

相机统一管理扩张，多个组件不会各自 Tick 争用同一半径：

- 新目标比正在执行的目标更大时，从当前半径连续开始新一段动画，采用新机关的时长。
- 相同或更小的阶段目标不会回缩范围，也不会重置进度。
- 从阶段扩张升级为全世界恢复时，允许目标半径相同或更小，保留已有较大目标并执行最终淡出。
- 全世界恢复开始后，后续阶段机关不能撤销它；全部恢复完成后不再接受恢复请求。

请求被接受后，目标、时长和恢复方式采用当时的参数快照。运行中修改组件属性不会改变已开始的动画，需要新的有效请求。
即使机关在触发后隐藏、销毁或随流送关卡卸载，已提交给相机的恢复仍然继续。

## 蓝图入口和交互优先级

`TriggerWorldRecovery(Interactor)` 可以从蓝图直接调用，返回是否成功提交请求。
`Interactor` 应传入同一世界内、由本地玩家控制器当前占有的 Pawn。
组件停用、空引用、未占有的 Pawn、线稿已经关闭或没有新的恢复目标时返回 false，保持组件未使用。

`HasTriggeredWorldRecovery()` 查询该组件是否成功触发过。
`OnWorldRecoveryTriggered(Interactor)` 是成功提交后的蓝图事件，可绑定机关音效、灯光或隐藏 / 销毁机关；它表示**请求已提交**。
动画完成可从玩家的 `DreamProximitySketchCameraManager` 查询 `HasWorldRecoveryFinished()`；动画中可查询 `IsWorldRecoveryActive()` 和 `GetEffectiveNormalRadius()`。

控制器保持原有分发规则：Actor 自己实现 `DreamInteractableInterface` 时优先交给 Actor；自由拖动组件也先于普通单次组件。
这类 Actor 需要在自己的交互 / 机关完成逻辑中显式调用 `TriggerWorldRecovery`，普通 Actor 则自动收到 E / 手办左键分发。

组件不会锁住移动、视角或滚轮输入。正常探索和手办模式都复用现有交互通道及遮挡规则。

## 生命周期和调试

恢复状态保存在当前本地玩家的相机管理器内，每位本地玩家独立。
游戏暂停或临时取消占有时暂停推进；同一控制器占有新 Pawn 后继续恢复，球心更新为新角色的位置。
结束 PIE 或替换相机管理器后重新使用初始配置；本功能不新增存档、跨关卡继承或网络复制。

恢复前 `dream.Sketch.Radius` 的临时覆盖可以作为动画起点；恢复开始后由动画半径优先，避免残留调试覆盖使交互看起来无效。
组件不会改写共享 CVar，也不会改写相机的 `NormalRadius`、`EffectStrength` 默认配置或磁盘上的材质实例。
线稿开关 / 调试视图仍遵循原有控制台配置；完整恢复后相机不会提交效果，需要重新开始 PIE 才能从初始线稿状态再次体验。

## 自动化验证与手动验收

自动化套件为 `DreamSpace.Puzzle.WorldRecovery`，使用独立瞬时世界，覆盖实际 E 射线及墙体遮挡、组件停用和单次触发、本地玩家隔离、两个后处理通道的参数同步、视角混合时只推进一次、末段全世界淡出、多个目标衔接、机关销毁、换 Pawn、控制台起点和零时长。

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' DreamSpaceEditor Win64 Development '-Project=F:\Ue5 Project\DreamSpace-UI\DreamSpace.uproject' -WaitMutex -NoHotReloadFromIDE -NoUBA -UBANoDetour -nocache
& 'E:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' DreamSpace Win64 Development '-Project=F:\Ue5 Project\DreamSpace-UI\DreamSpace.uproject' -WaitMutex -NoHotReloadFromIDE -NoUBA -UBANoDetour -nocache
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'F:\Ue5 Project\DreamSpace-UI\DreamSpace.uproject' /Engine/Maps/Entry '-ExecCmds=Automation RunTests DreamSpace' '-TestExit=Automation Test Queue Empty' -unattended -nop4 -nosplash -nullrhi '-ReportExportPath=F:\Ue5 Project\DreamSpace-UI\Saved\Automation\WorldRecoveryRegression' '-abslog=F:\Ue5 Project\DreamSpace-UI\Saved\Logs\WorldRecoveryRegression.log'
```

需在正式关卡 PIE 验收：

- 交互后边缘是否以合适的速度扩张，默认 100 米 / 5 秒是否符合关卡节奏。
- 玩家边移动、转镜头、上下楼或切换重力时，范围中心及过渡边缘是否自然。
- 开启全世界恢复时，最后的天空和远景淡出是否顺滑；关闭时，目标范围外是否继续保留线稿。
- 从手办内触发同一机关、以及多个阶段机关连续触发时，是否符合实际关卡设计。

原生测试验证交互和真实相机 MID 参数，不能代替目标画质下的运动观感验收。

真实 GPU 验收工具会在 TEST 的独立 PIE 主视口固定相机和玩家位置，生成原图、初始线稿、恢复中途和最终恢复四帧。它调用组件最终使用的相机恢复入口，按实际游戏帧推进；中途暂停还会核对恢复半径保持不变。E 射线和组件分发由上面的原生测试覆盖。

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' 'F:\Ue5 Project\DreamSpace-UI\DreamSpace.uproject' '-ExecutePythonScript=F:/Ue5 Project/DreamSpace-UI/Source/ProximitySketch/capture_world_recovery.py' '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' -unattended -nosound -NoSplash -Multiprocess -windowed
python Source/ProximitySketch/verify_world_recovery.py
```

第二条命令需在含 Pillow / NumPy 的普通 Python 环境运行。工具只向 `Saved/ProximitySketch/WorldRecovery` 写入截图、真实运行时采样、`verification.json` 和三阶段对照 `comparison.png`，不保存地图或材质。

2026-10-11，UE 5.8.2 / Win64 Development：编辑器和游戏目标编译通过，完整 `DreamSpace` 原生回归 **67 项通过**，包含新增 3 项世界恢复测试。
真实 D3D12 / TSR 主视口的 **6 项 GPU 检查通过**，正常区域由 600 cm 扩大到约 1803 cm 的中途画面已有可见恢复边缘，终点达到 3000 cm 后两个通道权重为 0；恢复结果与同镜头原图的场景平均通道差为 **0.69 / 255**。
GPU 采集使用 3000 cm / 8 秒，以便观察阶段变化；正式组件默认仍是 10000 cm / 5 秒。
