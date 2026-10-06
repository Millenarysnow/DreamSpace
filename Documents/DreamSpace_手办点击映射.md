# 手办点击映射到真实世界

## 目标与入口

手办目前由 `UDreamSceneCapturePresentationComponent` 创建：`SceneCapture2D` 捕获真实关卡，
`RenderTarget` 经透明材质显示在角色身上的 `BasicShapes/Plane`。点击的是一块显示面，
不是另一套缩小后的可碰撞场景。本功能把显示面上的点击位置反推为捕获相机在真实世界中的
射线，命中 Actor 后继续使用 `IDreamInteractableInterface::OnInteracted`。

运行时按 `Tab` 进入手办交互模式：显示鼠标、暂停鼠标转视角；左键点击手办画面中的物体。
再次按 `Tab` 回到普通探索，原有的 `E` 准星世界交互继续工作。在手办模式下，`E` 不触发
主视口中心的世界交互，避免同一次操作走错路径。开发期 HUD 会显示当前模式。

## 射线的几何关系

令 `F` 为手办显示面中心，`P` 为玩家鼠标射线与显示面的交点，`O` 为**生成当前捕获视角时**
采用的理想观察位置，`A` 为真实场景中的捕获锚点，`s` 为 `MiniatureSceneScale`。
现有严格窗口映射把捕获相机放在：

```text
C = A + (O - F) / s
```

显示面上的 `P` 对应真实场景里穿过锚点的参考平面上的点：

```text
Q = A + (P - F) / s
```

所以 `Q - C = (P - O) / s`。在正比例缩放下，捕获相机要发射的**单位方向**就是
`normalize(P - O)`；射线起点是 `C`。实现不读取 RT 像素、不对 RT 做 GPU→CPU 回读，
也不把二维坐标武断地当成真实物体的位置。真实物体的深度和所属 Actor 由从 `C` 发出的
碰撞射线确定。

这里的 `O` 不总是人物相机的实际位置。默认 `bIgnoreSpringArmCollision=true` 会使用未被
墙面缩短的 SpringArm 位置；默认 `bFollowCameraZoom=false` 还会用固定的
`ObserverArmLength`。因此鼠标射线只负责找出玩家在显示面上点到的 `P`，不能把这条实际
相机射线的方向直接复制给捕获相机。表现组件在更新捕获视角时保存同一帧的 `O`，拾取时
使用保存值和捕获相机位置。

## 实际处理流程

1. 控制器把鼠标位置反投影成真实第三人称相机射线。
2. 表现组件以当前显示网格的世界变换和原始 Bounds 与射线求交，只接受正面、矩形范围内
   的点。显示网格本身仍保持 `NoCollision`，不会挡住原有世界交互。
3. 根据基础 Plane 的图像朝向计算捕获方向。默认
   `bRotateDisplayImage180Degrees=true` 时直接使用 `P`；关闭该选项时，基础 Plane 的
   画面绕中心翻转 180°，先将 `P` 绕面片中心翻转，再求 `normalize(P - O)`。
4. 控制器先沿实际相机到显示面的线段检查近处遮挡；如果墙或道具挡住手办，不允许穿透
   遮挡物点击。然后从 SceneCapture 位置做第二次真实世界 `Visibility` 射线。
5. 第二次射线忽略玩家 Pawn、捕获黑名单中的 Actor 及其 ChildActor；普通非交互物体
   作为遮挡物保留。
   命中带交互组件的 Actor 后调用原有 `DispatchInteraction`。旋转和平移组件无需知道
   点击来自主视口还是手办。

捕获相机到锚点往往远大于普通 E 射线的 600 cm，例如约 420 cm 观察臂长、0.03 比例
会得到约 14,000 cm 的捕获距离。因此手办射线有独立的
`MiniatureInteractTraceDistance`，默认 50,000 cm。碰撞通道沿用控制器现有的
`InteractTraceChannel`（默认 `Visibility`）：目标网格必须阻挡该通道。

## 严格窗口的适用条件

直接方向映射只在当前默认的严格窗口模式成立：跟随玩家相机、透视投影、FOV 匹配面片
张角、`FaceCamera` 显示面、显示面宽高比与 RT 宽高比一致。实现会在运行时检查这些
条件，以及 FOV 是否因为极端距离被限幅、显示面是否仍垂直于捕获光轴。条件不满足时
本次手办点击不会触发世界内容，避免错误地点击另一处。正交投影、手动 FOV、固定朝向
显示面若需要交互，应另外使用 RT 坐标和相应投影矩阵建立拾取射线。

显示网格实际宽高还必须等于 `DisplayWorldSize`，不能另外通过父节点或
`DisplayRelativeTransform` 施加缩放或镜像，否则 FOV 与物理面片不再对应。改变手办大小
应在启动前配置 `DisplayWorldSize`；运行时仅改数值不会重建已有网格尺寸。

目前拾取面要求原始网格是中心对齐的薄矩形平面。更换三维外壳可以继续表现 RT，但应
提供明确的可点击平面和它到画面坐标的关系，不能直接把外壳包围盒当作整个显示面。
若替换默认 Plane 的 UV 布局或修改材质中的 UV 变换，也应同步调整映射规则。

真实场景的射线使用碰撞数据，而 RT 使用渲染数据。透明物体、只有视觉没有相应碰撞的
网格、或者与渲染黑名单不一致的运行时隐藏规则都可能造成“看见但点不到”或相反的
结果。当前同一 Actor 上若有多个 `IDreamInteractableInterface` 组件，沿用原有分发语义：
**一次命中会触发该 Actor 上全部交互组件**。将来如果同一 Actor 的不同部位要执行不同
动作，需要增加“命中 PrimitiveComponent → 指定交互组件”的显式绑定。

## 调试与验证

控制台输入 `dream.DebugMiniatureInteraction 1`，点击时显示：青色为玩家相机到显示面的
射线和交点，绿色为捕获射线的命中，红色为捕获射线未命中，橙色为显示面前的遮挡，
黄色球为真实世界命中点。输入 `dream.DebugMiniatureInteraction 0` 关闭。
这些调试线与已有 `dream.DebugSceneCapture 1` 的相机、锚点可视化可以一起使用。

建议在 `InteractionDemo` 的 PIE 中验证：点击手办画面里旋转/平移 Actor 的中心和边缘
时，真实 Actor 与直接对其按 E 的行为一致；点击透明背景、平面外、背面或被近处物体
挡住的区域没有动作。转动第三人称相机、滚轮拉近拉远，以及让 SpringArm 贴墙被推近，
都应保持点击位置对应同一个可见物体。自动化测试
`DreamSpace.Presentation.MiniatureClickRays` 覆盖中心/边缘、实际相机与理想观察位置
不同、180° 图像翻转、越界和背面拒绝等纯几何情况。

## 本次验证记录（2026-10-06）

- UE 5.8 的 `DreamSpaceEditor Win64 Development` 与 `DreamSpace Win64 Development` 编译通过。
- 使用 `UnrealEditor-Cmd -nullrhi` 运行 `DreamSpace.Presentation`，6 项测试全部通过。
- 自动化测试验证数学关系；尚未在可视 PIE 中验证鼠标捕获/释放和实际关卡的渲染与碰撞对应。
