# 手办相机锚点配置

`ADreamSceneCaptureAnchor` 是独立的表现层空 Actor，用来指定手办内部的固定取景中心。
角色由 GameMode 在运行时生成也可以使用，不需要在角色蓝图中引用关卡 Actor。

## 关卡配置

1. 编译后，在编辑器的 C++ Classes 中搜索 `DreamSceneCaptureAnchor`，将它拖入关卡。
2. 场景只放一个，将它的位置设为希望显示在手办中心的位置。保持旋转为零即可沿用世界轴；需要调整手办朝向时再旋转锚点。锚点缩放不影响捕获映射。
3. Actor 的 `Tags` 默认已有 `DreamSceneCaptureAnchor`，无需手动添加或修改。这是普通 Actor Tag，不是 GameplayTag。
4. 启动游戏。角色的 `SceneMiniature` 组件会自动绑定它；可在该组件的“场景缩略图 | 相机锚点”下查看只读的 `CameraOrbitAnchorActor`。

锚点只有一个承载变换的 SceneComponent，默认在游戏中隐藏、无碰撞、无 Tick，也没有可渲染模型。
组件的 `bAimCaptureCameraAtOrbitAnchor` 默认开启；它只影响不跟随玩家相机的固定取景模式。

## 跟随玩家相机时的映射

锚点对应手办面片的中心。默认的固定半径轨道模式（`bUseFixedCaptureOrbit` 开启）下：

- 捕获相机沿“玩家相机 → 面片中心”的视线方向看向锚点，距离固定为 `CaptureDistance`。
  从哪一侧、以多大俯角看手办，就从同一侧、同一俯角看建筑；
- 视线先换算到手办坐标系（跟随手部/角色朝向），再换算到锚点坐标系，所以角色转身时手办里的建筑随之转动；
- 画面上方向取玩家相机的上方向，与 `FaceCamera` 面片一致；视场角使用 `CaptureFOV`；
- `bIgnoreSpringArmCollision` 开启时，使用 SpringArm 未经碰撞缩短的理想镜头位置，
  镜头被墙推近时手办画面不会跟着推近。

需要调整手办内的景别时改 `CaptureDistance` 或 `CaptureFOV`。
关闭 `bUseFixedCaptureOrbit` 会退回等比映射：`(玩家相机 − 面片) / MiniatureSceneScale`，
沿用玩家相机的旋转和 FOV，景别随玩家相机到面片的距离变化。

## 自动绑定时机

每个表现组件在自己的 `BeginPlay` 中，只查找一次**当前世界**里第一个属于
`ADreamSceneCaptureAnchor`（包括派生类）、且带默认 Tag 的实例，然后才创建和启用 SceneCapture。
Tag 在锚点构造时就添加，不依赖两个 Actor 的 `BeginPlay` 执行顺序。玩家重新生成时，新组件会重新查找。

锚点应放在游戏开始时已加载的关卡中。当前不会每帧查找，也不会自动等待后来加载的流送关卡。
未找到锚点时，输出一条日志提示，并继续使用原有的 `CapturedSceneReferenceActor` / `CapturedSceneReferenceTransform`。
