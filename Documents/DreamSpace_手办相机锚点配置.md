# 手办相机锚点配置

`ADreamSceneCaptureAnchor` 是独立的表现层空 Actor，用来指定手办内部的固定取景中心。
角色由 GameMode 在运行时生成也可以使用，不需要在角色蓝图中引用关卡 Actor。

## 关卡配置

1. 编译后，在编辑器的 C++ Classes 中搜索 `DreamSceneCaptureAnchor`，将它拖入关卡。
2. 场景只放一个，将它的位置设为希望显示在手办中心的位置。锚点的**旋转和缩放都不参与映射**：捕获相机始终与玩家相机处于同一侧，旋转锚点不会翻转取景方向。
3. Actor 的 `Tags` 默认已有 `DreamSceneCaptureAnchor`，无需手动添加或修改。这是普通 Actor Tag，不是 GameplayTag。
4. 启动游戏。角色的 `SceneMiniature` 组件会自动绑定它；可在该组件的“场景缩略图 | 相机锚点”下查看只读的 `CameraOrbitAnchorActor`。

锚点只有一个承载变换的 SceneComponent，默认在游戏中隐藏、无碰撞、无 Tick，也没有可渲染模型。
组件的 `bAimCaptureCameraAtOrbitAnchor` 默认开启；它只影响不跟随玩家相机的固定取景模式。

## 跟随玩家相机时的映射

锚点对应手办面片的中心。默认的固定半径轨道模式（`bUseFixedCaptureOrbit` 开启）下：

- 捕获相机沿“玩家相机 → 面片中心”的视线看向锚点，距离固定为 `CaptureDistance`。
  玩家绕着角色转到哪一侧、俯到什么角度，就从同一侧、同一俯角看建筑；
  捕获相机始终在玩家相机所在的这一侧；
- 关闭 `bOrbitAlongLineOfSightToDisplay` 会改用玩家相机的前方向：转动与鼠标 1:1 对应，
  但面片偏在角色一侧时，视线与前方向相差一个随臂长变化的夹角；
- 视线与上方向都在**世界空间**计算，不经过手办组件或锚点的旋转。手办组件的旋转属于
  显示层（原型角色为了让面片朝外设了 180°，见 `DreamCharacter` 的 `SetRelativeRotation`），
  锚点旋转属于取景参考系；两者一旦乘进捕获方向，方位角会整体翻转 180°，
  捕获相机就跑到玩家相机的另一侧；
- 画面上方向取玩家相机的上方向，与 `FaceCamera` 面片一致；视场角使用 `CaptureFOV`；
- `bIgnoreSpringArmCollision` 开启时，使用 SpringArm 未经碰撞缩短的理想镜头位置，
  镜头被墙推近时手办画面不会跟着推近。

需要调整手办内的景别时改 `CaptureDistance` 或 `CaptureFOV`。
关闭 `bUseFixedCaptureOrbit` 会退回等比映射：`锚点位置 + (玩家相机 − 面片) / MiniatureSceneScale`，
旋转沿用玩家相机，景别随玩家相机到面片的距离变化。

固定轨道模式下，`FaceCamera` 面片的法线与捕获视线使用同一方向（默认是“面片 → 玩家相机”），
而不是“面片 → 真实相机位置”。否则面片偏在角色一侧时，画面按相机俯仰、面片按真实相机位置转向，
两者之间会出现随臂长变化的额外倾斜，镜头被障碍挤近时尤其明显。

## 调试绘制

控制台输入 `dream.DebugSceneCapture 1` 开启，`dream.DebugSceneCapture 0` 关闭。调试线不会被拍进手办画面。

| 图形 | 含义 |
|---|---|
| 黄色球 + 坐标轴 | 锚点位置与朝向 |
| 品红相机 + 品红线 | SceneCapture 的位置、朝向、FOV，以及它到锚点的连线 |
| 面片中心绿色箭头 | 映射使用的观察视线；视线模式下是“理想观察位置 → 面片中心”，否则是玩家相机前方向 |
| 面片中心红色箭头 | 捕获相机的世界前方向；映射正确时与绿色重合 |
| 面片中心蓝色箭头 | 面片实际法线；固定轨道模式下应与绿色正好反向 |
| 橙色 / 白色小球 | 真实相机位置 / SpringArm 未经碰撞修正的理想位置，镜头被挤近时两者分开 |

屏幕左上角同时显示玩家相机与捕获相机的 Pitch/Yaw、真实与理想臂长、捕获距离，
以及绿-红、蓝-(-绿) 两个夹角；映射正确时两个夹角都应接近 0°。

## 自动绑定时机

每个表现组件在自己的 `BeginPlay` 中，只查找一次**当前世界**里第一个属于
`ADreamSceneCaptureAnchor`（包括派生类）、且带默认 Tag 的实例，然后才创建和启用 SceneCapture。
Tag 在锚点构造时就添加，不依赖两个 Actor 的 `BeginPlay` 执行顺序。玩家重新生成时，新组件会重新查找。

锚点应放在游戏开始时已加载的关卡中。当前不会每帧查找，也不会自动等待后来加载的流送关卡。
未找到锚点时，输出一条日志提示，并继续使用原有的 `CapturedSceneReferenceActor` / `CapturedSceneReferenceTransform`。
