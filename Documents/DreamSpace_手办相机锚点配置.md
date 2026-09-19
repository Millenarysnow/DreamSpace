# 手办相机锚点配置

`ADreamSceneCaptureAnchor` 是独立的表现层空 Actor，用来指定手办内部的固定取景中心。
角色由 GameMode 在运行时生成也可以使用，不需要在角色蓝图中引用关卡 Actor。

## 关卡配置

1. 编译后，在编辑器的 C++ Classes 中搜索 `DreamSceneCaptureAnchor`，将它拖入关卡。
2. 场景只放一个，将它的位置设为希望显示在手办中心的位置。保持旋转为零即可沿用世界轴；需要调整手办朝向时再旋转锚点。锚点缩放不影响捕获映射。
3. Actor 的 `Tags` 默认已有 `DreamSceneCaptureAnchor`，无需手动添加或修改。这是普通 Actor Tag，不是 GameplayTag。
4. 启动游戏。角色的 `SceneMiniature` 组件会自动绑定它；可在该组件的“场景缩略图 | 相机锚点”下查看只读的 `CameraOrbitAnchorActor`。

锚点只有一个承载变换的 SceneComponent，默认在游戏中隐藏、无碰撞、无 Tick，也没有可渲染模型。
组件的 `bAimCaptureCameraAtOrbitAnchor` 默认开启，使捕获相机始终朝向锚点。

## 自动绑定时机

每个表现组件在自己的 `BeginPlay` 中，只查找一次**当前世界**里第一个属于
`ADreamSceneCaptureAnchor`（包括派生类）、且带默认 Tag 的实例，然后才创建和启用 SceneCapture。
Tag 在锚点构造时就添加，不依赖两个 Actor 的 `BeginPlay` 执行顺序。玩家重新生成时，新组件会重新查找。

锚点应放在游戏开始时已加载的关卡中。当前不会每帧查找，也不会自动等待后来加载的流送关卡。
未找到锚点时，输出一条日志提示，并继续使用原有的 `CapturedSceneReferenceActor` / `CapturedSceneReferenceTransform`。
