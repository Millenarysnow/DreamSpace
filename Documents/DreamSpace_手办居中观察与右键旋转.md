# 手办居中观察与右键旋转

## 操作

- **Tab**：同一台角色 FollowCamera 改为对准手办显示面的中心，并推近到便于鼠标操作的景别。
- **按住右键拖动**：水平、垂直连续旋转手办展示角度，可翻过顶部/底部；松开后停在当前角度。
- 右键从显示面内开始抓取，之后可以移出手办矩形继续旋转；移出游戏窗口、失焦或输入取消时停止。
- **左键**：沿用原来的机关点击、持续拖动和模型取出；右键旋转与左键交互互斥。
- **再次 Tab**：取消当前拖动，恢复原来的探索视角、输入与跟随取景。

观察期间暂停主动移动、鼠标/手柄转视角、跳跃和探索滚轮缩放。CharacterMovement 仍执行重力、落地和平台搬运。
无需新建输入资产、替换角色蓝图或修改关卡；默认原生角色已包含所需组件。

## 相机与展示职责

`UDreamShoulderCameraComponent` 继续驱动原来的 SpringEndpoint 和 FollowCamera。
聚焦原点使用 `SceneMiniature` 的显示面中心，包含 `DisplayRelativeTransform` 的位置偏移，
不会把组件原点或角色中心当成手办中心。移除聚焦构图的肩位偏移，但保留探索臂长、肩位配置和控制旋转。

聚焦距离使用实际显示网格的世界宽高与 UE 5.8 的投影矩阵计算，兼容视口宽高比、FOV 保持轴和分屏尺寸。
相机仍沿中心视线做 Camera 通道球扫掠，受阻时收近，光轴仍指向手办中心。
墙体过近可能让显示面超过目标占屏比例，安全距离优先。人物渐进局部剔除使用聚焦后的实际镜头。

进入 Tab 时，`UDreamSceneCapturePresentationComponent::BeginInspection` 保存当前捕获角度、
到场景锚点的距离与 FOV，之后围绕 `DreamSceneCaptureAnchor` 独立取景。
主镜头推近不会同步放大手办内建筑；右键使用当前捕获相机的上轴、右轴和归一化四元数连续公转。
输入来自光标像素位移，不重复乘帧时间，没有惯性，也没有探索相机的俯仰限制。

真实建筑、机关、角色和重力不因右键展示旋转而改变。显示面继续朝向玩家主相机，
SceneCapture 单独旋转；左键始终使用当前显示 UV 和 SceneCapture 投影反算射线。
退出 Tab 后恢复原有窗口映射；展示旋转只保留在当前观察会话，再次进入从当时的探索取景开始。

## 调参

| 位置 | 属性 | 默认值 | 含义 |
| --- | --- | --- | --- |
| CameraBoom / 相机 / 手办观察 | `MiniatureScreenFill` | 0.65 | 显示面在视口较紧的一边占据 65%，保留边距供模型取出 |
| SceneMiniature / 场景缩略图 / 居中观察 | `InspectionRotationDegreesPerPixel` | 0.25 | 光标移动 1 像素对应 0.25° 展示旋转 |

默认 `DisplayFacingMode=FaceCamera` 和透视捕获沿用现有点击约定。
观察期间固定面向/绕世界竖轴面向也会临时改为完整朝向主镜头，退出后恢复配置。
RT 为 2200×2500、显示面为 80×80 的现有配置保持不变；宽高比不一致的拉伸也沿用原来的投影拾取规则。

## 生命周期与回归

Tab 会话的输入锁与左键拖动会话独立成对计数，松开左键不会解除观察锁，退出观察不会清掉其它系统的输入锁。
失去 Pawn、切换 ViewTarget、停用显示、相机停用或销毁组件会清理会话；右键松开与 Canceled 都结束旋转。
没有原生角色或显示资源尚未启用时，Tab 不会开启空的观察会话，也不会留下光标或输入锁。

新增自动化入口位于 `Source/DreamSpace/Private/Gameplay/Tests/DreamMiniatureInspectionTests.cpp`：

- `DreamSpace.Camera.Miniature.FocusInputLocksAndLifecycle`：居中、探索姿态恢复、输入锁计数、切镜头与显示停用。
- `DreamSpace.Camera.Miniature.FramingCollisionAndGravity`：实际平面四角投影、横/竖景别、Camera 碰撞与重力翻转。
- `DreamSpace.Presentation.Miniature.FreeRotationAndPicking`：自由翻转、固定景别、停止输入、左右键互斥和真实机关命中。

2026-10-10，UE 5.8.2，Win64 Development：编辑器和游戏双目标编译通过，完整 `DreamSpace` 逻辑套件 46 项通过。
逻辑测试使用 NullRHI，日志为 `Saved/Logs/MiniatureInspectionRegression.log`，不代替主视口的视觉与手感验收。
独立 `UnrealEditor.exe -game -RenderOffscreen -d3d12` 运行 `DreamSpace.Presentation.Miniature.GameplayRender`，
在 `/Game/DreamInteraction/Test/TestMap2` 导出四张 1280×720 主视口 PNG，已查看居中、旋转和恢复构图。
截图位于 `Saved/Screenshots/MiniatureInspection/`，渲染日志为 `Saved/Logs/MiniatureInspectionTestMapRender.log`。
该渲染检查使用展示旋转接口，实际鼠标抓取、失焦行为和主观灵敏度仍需目标关卡实机验收。

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' DreamSpaceEditor Win64 Development '-Project=E:\UE5_program\DreamSpace\DreamSpace.uproject' -WaitMutex -NoHotReloadFromIDE -NoUBA -UBANoDetour -nocache
& 'E:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' DreamSpace Win64 Development '-Project=E:\UE5_program\DreamSpace\DreamSpace.uproject' -WaitMutex -NoHotReloadFromIDE -NoUBA -UBANoDetour -nocache
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'E:\UE5_program\DreamSpace\DreamSpace.uproject' '-ExecCmds=Automation RunTests DreamSpace' '-TestExit=Automation Test Queue Empty' -unattended -nop4 -nosplash -nullrhi '-abslog=E:\UE5_program\DreamSpace\Saved\Logs\MiniatureInspectionRegression.log'
```

`-NoUBA -UBANoDetour -nocache` 用于绕过本机 UBA 进程拦截/缓存导致的构建停滞，不需要更改引擎或全局构建配置。
首次使用完整编译后的 DLL 验收建议重启编辑器，在目标关卡检查 Tab 居中、右键两个方向拖动、
旋转后的左键点击/取出、贴墙取景、平台翻转以及连续进入/退出。
