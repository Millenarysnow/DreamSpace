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

## 跟随玩家相机时的映射：严格窗口

锚点对应手办面片的中心，面片本身是一块**窗口**：真实场景按 `MiniatureSceneScale` 缩放、
把锚点对齐到面片中心之后贴在面片背后。要让“透过窗口看”成立，三条必须同时满足，
它们由 `MapObserverWindowToCaptureWorld` 与 `ComputeWindowFieldOfView` 给出：

| 量 | 取值 | 理由 |
|---|---|---|
| 光轴方向 | `normalize(面片中心 − 观察位置)` | 光轴必须穿过面片中心，RT 的四角才落在面片四角上、锚点落在画面正中 |
| 取景距离 | `观察位置到面片的距离 ÷ MiniatureSceneScale` | 眼睛后退时等效相机同步后退，视差幅度才正确 |
| 视场角 | `2·atan((DisplayWorldSize.X/2) / 距离)` | 整幅画面恰好覆盖顺着面片看过去的那一束角度 |

三者缺一不可：只改方向会让画面相对面片偏转，只改距离会让视差比例错，
只改视场角会让画面相对面片放大或缩小。

注意**光轴方向是视线，不是玩家相机的前方向**。第三人称相机的前方向穿过角色中心，
而面片挂在角色一侧（`SceneMiniature` 的 `SetRelativeLocation`），两者相差一个
`asin(|面片偏移| / 臂长)` 的夹角，臂长 420、偏移 96 时约 13°，而且随绕转角度摆动。
用相机前方向会让画面相对面片偏转，看起来像“朝向角色本体倾斜”。

其他要点：

- 视线与上方向都在**世界空间**计算，不经过手办组件或锚点的旋转。手办组件的旋转属于
  显示层（原型角色为了让面片朝外设了 180°，见 `DreamCharacter` 的 `SetRelativeRotation`），
  锚点旋转属于取景参考系；两者一旦乘进捕获方向，方位角会整体翻转 180°，
  捕获相机就跑到玩家相机的另一侧；
- 画面上方向取玩家相机的上方向并正交化到垂直于光轴，与 `FaceCamera` 面片一致；
- `bIgnoreSpringArmCollision` 开启时，观察位置取 SpringArm 未经碰撞缩短的理想位置
  （`TargetArmLength` 那根臂），镜头贴墙被挤近时手办画面不变；
- `FaceCamera` 面片的法线取自捕获相机的光轴，因此面片严格垂直于光轴，
  画面上下的方向与面片不可能错开。

### 景别怎么调

`MiniatureSceneScale` 是唯一的景别旋钮，它决定窗口里装得下多大的真实场景：

```
窗口覆盖的真实范围 ≈ DisplayWorldSize / MiniatureSceneScale
```

面片 80、比例 0.03 时约覆盖 2667 cm（27 m）的建筑；比例调到 0.06 就只装得下 13 m，
手办看起来像是贴着建筑在看。视场角不随这个值变化（它只由面片张角决定），
所以调比例相当于换镜头焦距，而不是改取景距离。

`CaptureFOV` 只在关闭 `bMatchCaptureFOVToDisplay` 时使用；手填固定角度会让画面
相对面片缩放，破坏窗口对应关系，一般不要动。

## 调试绘制

控制台输入 `dream.DebugSceneCapture 1` 开启，`dream.DebugSceneCapture 0` 关闭。调试线不会被拍进手办画面。

| 图形 | 含义 |
|---|---|
| 黄色球 + 坐标轴 | 锚点位置与朝向 |
| 品红相机 + 品红线 | SceneCapture 的位置、朝向、FOV，以及它到锚点的连线 |
| 面片中心绿色箭头 | 映射使用的观察视线：理想观察位置 → 面片中心 |
| 面片中心红色箭头 | 捕获相机的光轴；映射正确时与绿色重合 |
| 面片中心蓝色箭头 | 面片实际法线；应与绿色正好反向 |
| 橙色 / 白色小球 | 真实相机位置 / SpringArm 未经碰撞修正的理想位置，镜头被挤近时两者分开 |

屏幕左上角显示两台相机的 Pitch/Yaw、真实与理想臂长、捕获 FOV 与面片张角，
以及绿-红、蓝-(-绿) 两个夹角；映射正确时两个夹角都应为 0°。
另外显示按当前比例计算出的窗口覆盖范围和眼睛到面片的距离，方便调景别。

## 自动绑定时机

每个表现组件在自己的 `BeginPlay` 中，只查找一次**当前世界**里第一个属于
`ADreamSceneCaptureAnchor`（包括派生类）、且带默认 Tag 的实例，然后才创建和启用 SceneCapture。
Tag 在锚点构造时就添加，不依赖两个 Actor 的 `BeginPlay` 执行顺序。玩家重新生成时，新组件会重新查找。

锚点应放在游戏开始时已加载的关卡中。当前不会每帧查找，也不会自动等待后来加载的流送关卡。
未找到锚点时，输出一条日志提示，并继续使用原有的 `CapturedSceneReferenceActor` / `CapturedSceneReferenceTransform`。
