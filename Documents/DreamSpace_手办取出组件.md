# 手办取出组件

`Dream Miniature Extractable`（`UDreamMiniatureExtractableComponent`）可以直接挂在房间模型 Actor 上。
它复用现有手办捕获拾取、鼠标模式和持续交互输入锁，把房间中的模型取出为玩家附近的物理掉落物。
新代码的类说明、配置项、生命周期与几何处理均有中文注释。

## 关卡配置

1. 将要取出的物体放在**独立 Actor** 上，添加 `Dream Miniature Extractable` 组件。
   成功后移除的是这个 Actor 整体；不要把组件直接挂在包含整间房屋的 Actor 上。
2. Actor 至少包含一个普通 `StaticMeshComponent`。模型需要在手办 SceneCapture 中可见，
   且阻挡控制器的交互通道（默认 `Visibility`），具有可供复杂射线查询的碰撞。
3. 在 **手办取出 → 模型 → 源静态网格组件** 中选择要复制的静态网格。
   留空时优先使用实际点击命中的静态网格；命中独立碰撞盒时则使用 Actor 的第一个静态网格。
   显式组件引用填错或失效时拒绝开始，不会悄悄取出另一个网格。
4. **取出模型比例** 默认 `0.1`，掉落物尺寸为源网格的世界尺寸乘此比例。
   此值独立于手办的 `MiniatureSceneScale`；需要与手办内模型大小近似时可以设为相同值。
5. **掉落物类** 默认 `DreamDroppedItem`。钥匙选择 `DreamDroppedKey`，掉落后即可用 E 拾取；
   也可以指定掉落物的蓝图子类，扩展声音和特效。
6. **成功后销毁源 Actor** 默认开启。关闭时保留原 Actor 引用，但将它永久隐藏、关闭碰撞并标记已取出。

本组件不移动房间里的源 Actor，因此不需要枢轴，也不要求源网格为 Movable。
源网格应是静置的普通静态网格组件，不能正在模拟物理；不支持骨骼网格、实例化静态网格
或把多个网格、ChildActor 和源蓝图逻辑自动克隆成掉落物。
多个网格的 Actor 会复制选中的一个网格，同时移除源 Actor 整体。

## 操作与状态

- 按 Tab 进入手办模式，在模型上按住左键开始抓取。
- 抓取成功后源 Actor 暂时隐藏、关闭碰撞，玩家附近出现无碰撞的缩小模型预览。
  手办捕获的是同一个世界，因此原模型也会从手办画面消失。
- 保持左键移动鼠标，预览跟随显示面所在平面的交点，移出显示面后仍可以继续移动。
- 鼠标超过显示面任一边缘的阈值后松开左键，移除源 Actor，预览开启物理并成为掉落物。
  **拖出边缘阈值** 默认 `0.02 UV`，约为对应显示尺寸的 2%。
- 在显示面内松开、从面外拖回面内再松开、Tab 切换模式或输入取消，均删除预览并恢复源模型。
- 失去操控角色、停用/销毁组件、预览被销毁、关卡退出以及显示面映射失效，均取消尚未提交的操作。
- 世界 E 交互不会开始取出；手办命中带本组件的 Actor 时，它优先于同 Actor 的自由和步进组件。
  推荐一个实际物体配置一种持续交互，避免其它脚本同时修改源 Actor 的状态。

松开左键时控制器会重新采样当前位置，避免按上一帧的面外位置误提交。
Enhanced Input 的 `Completed` 才尝试提交，`Canceled` 和逐帧丢失按键的兜底检查只取消。
提交前还检查游戏视口焦点和鼠标范围，失焦缓存的坐标不会生成掉落物。
每次取出成对增加/减少自身的视角与移动输入锁，保留其它系统已加的锁。

## 默认掉落物

`ADreamDroppedItem` 复制源网格资源、各材质槽和组件的世界缩放，并保留源网格的世界朝向。
它把偏离模型原点的几何中心对齐到球形碰撞中心；球体根据缩小后的包围盒计算，最小半径为 5 cm。
球形碰撞不依赖美术网格的简单碰撞，因此没有简单碰撞的模型也能正常掉落。

预览和释放点都位于显示面朝向玩家的一侧，距显示面的距离为碰撞半径加 **显示面前方间距**（默认 8 cm）。
释放前检查球体是否与外部阻挡物重叠；落在墙或角色内时取消并恢复源模型。
开启的是 UE 物理世界的普通重力，并不沿用角色的自定义重力方向。

掉落物所有 Primitive 在初始化时关闭碰撞、物理，并隐藏于 SceneCapture；提交后仅默认球体开启碰撞与物理。
球体忽略 Visibility 和 Camera 通道，避免手办中不可见的物体挡住捕获拾取或第三人称相机。
`OnDroppedFromMiniature` 蓝图事件在成功提交、源物体已移除后调用，可以在这里启用自己的拾取碰撞或效果。
成功掉落物不属于原组件的预览会话，后续模式切换或组件清理不会删除它。

## 钥匙拾取

`ADreamDroppedKey` 继承普通掉落物，并默认携带 `Dream Key Pickup`（`UDreamKeyPickupComponent`）。
钥匙身份由 **掉落物类** 显式配置，普通 `DreamDroppedItem` 不会因为网格名称而自动成为钥匙。
当前测试蓝图 `/Game/DreamInteraction/Test/MiniatureExtractableActorTest1` 已配置为 `DreamDroppedKey`，
保留现有长条方块占位模型、缩放和地图布局；更换钥匙静态网格后仍沿用同一拾取行为。

1. 在手办内拖出钥匙模型，越过显示面边缘后松开左键，生成物理钥匙掉落物。
2. 按 Tab 回到普通探索模式，对准世界中的掉落钥匙按 E；使用既有的准星射线，
   默认从相机检测 600 cm，墙体等首个阻挡物会遮挡交互。
3. 成功后销毁掉落钥匙，将当前 `ADreamCharacter.bHasKey` 从 `false` 置为 `true`。
   蓝图可读取角色的 **物品 → 钥匙 → 已获得钥匙**，作为后续门锁判断条件。
4. HUD 在屏幕中央显示 **已获得钥匙**，默认持续 3 秒；文字按实际字体宽高居中，
   使用阴影并临时隐藏准星，避免准星和文字重叠。切换手办模式不会清除尚未到期的提示。

只有物理提交成功才启用拾取：拖动预览、取消取出、组件停用以及无效操作者都不会获得钥匙。
已消费的掉落物不能重复授予钥匙或刷新提示。持有状态是布尔值而非数量，提示消失后仍保持为 `true`；
目前保存在当前角色实例中，不涉及存档、跨关卡保留或消耗钥匙逻辑。

钥匙掉落后球形碰撞阻挡 `Visibility`，继续忽略 `Camera`。所有掉落物及其 ChildActor
同时加入手办的捕获和捕获射线排除列表，避免手办中不可见的钥匙挡住其它机关。
默认交互通道为 `Visibility`；若自行修改控制器的交互通道，需同步设置钥匙碰撞响应。

## 蓝图入口

| 入口 | 用途 |
|---|---|
| `BeginExtract(Interactor, HitComponent, InitialDisplayPoint, DisplayFrontNormal)` | 以显示面世界交点开始抓取 |
| `UpdateExtract(DisplayPoint, UnclampedUV, DisplayFrontNormal)` | 更新预览位置和是否越过边缘 |
| `EndExtract(bTryDrop)` | 正常松开传 true，中断传 false；返回是否成功生成掉落物 |
| `IsExtracting()` / `IsExtracted()` | 查询进行中或已经成功取出的状态 |
| `GetPreviewItem()` | 查询尚未提交的预览，结束后为空 |

自定义蓝图控制器需承担按键生命周期，并且在松开前发送当前 UV。
`TryMapViewRayToDisplayPlane` 提供未截断的显示面 UV；已有 `TryMapViewRayToCaptureRay`
继续只允许显示面内拾取。两者共用 `MapViewRayToDisplayUV` 的几何计算，兼容显示图像 180° 修正与非等比显示。

## 验证

新增自动化套件 `DreamSpace.Puzzle.Extraction`，在独立瞬时世界中测试，不保存关卡或蓝图：

- 非均匀缩放、180° 姿态下的面外 UV，以及正面/零射线检查。
- 源网格与材质复制、预览无碰撞、边缘误触取消、原碰撞状态恢复及物理提交。
- 释放位置被墙阻挡、预览销毁、组件停用/移除和保留源引用时禁止重复取出。
- 真实 SceneCapture 拾取及面外拖动、同 Actor 交互优先级、取消、模式切换、失去角色和输入锁清理。

钥匙套件 `DreamSpace.Puzzle.KeyPickup` 覆盖从取出提交到 E 射线分发的流程、墙体遮挡、
手办模式禁止拾取、角色 bool、掉落物销毁、提示有效期与重复按 E，以及预览取消和无效操作者。

可运行完整 `DreamSpace` 套件回归原有旋转、平移、重力和手办交互。
鼠标手感、目标关卡碰撞和掉落物最终尺寸仍需在配置好模型的关卡里通过 PIE 验收。

2026-10-09 取出组件初次验证：UE 5.8.2 的 `DreamSpaceEditor Win64 Development` 与
`DreamSpace Win64 Development` 均编译成功；完整 `DreamSpace` 套件 31 项全部通过，
包含新增 4 项取出测试。最终回归日志位于 `Saved/Logs/MiniatureExtractionRegressionFinal.log`，
结构化报告位于 `Saved/Automation/MiniatureExtractionRegressionFinal/index.json`。
新增取出测试没有警告；完整套件有一条 UE 后台联网探测超时警告，与交互结果无关。
初次取出组件验证未在实际关卡完成可视 PIE 验收，未修改任何关卡或蓝图资产。

2026-10-09 钥匙拾取验证：更新后的编辑器和游戏 Development 目标均编译成功，
完整 `DreamSpace` 套件 33 项全部通过，包含新增 2 项钥匙测试，测试报告没有警告或失败。
日志为 `Saved/Logs/KeyPickupRegressionFinal.log`，报告为
`Saved/Automation/KeyPickupRegressionFinal/index.json`。
实际测试地图的 PIE 副本验证了保存的 `DreamDroppedKey` 配置、物理提交、组件拾取、
角色 `bHasKey = true` 和 HUD 中文居中渲染；E 射线与墙体遮挡由上述自动化覆盖。
提示截图为 `Saved/Screenshots/KeyPickupPIE.png`，到期后的截图为
`Saved/Screenshots/KeyPickupPIEExpired.png`；到期后恢复准星，人物仍持有钥匙。

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'E:\UE5_program\DreamSpace\DreamSpace.uproject' -Unattended -NoSplash -NullRHI `
  '-ExecCmds=Automation RunTests DreamSpace' '-TestExit=Automation Test Queue Empty' `
  '-ReportExportPath=E:\UE5_program\DreamSpace\Saved\Automation\KeyPickupRegressionFinal' `
  '-AbsLog=E:\UE5_program\DreamSpace\Saved\Logs\KeyPickupRegressionFinal.log'
```
