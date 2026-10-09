# 手办点击映射到真实世界

## 目标与操作

手办由 `UDreamSceneCapturePresentationComponent` 创建：`SceneCapture2D` 捕获真实关卡，
`RenderTarget` 经透明材质显示在角色身上的 `BasicShapes/Plane`。点击的是显示面，
拾取时从捕获相机向真实世界发射对应射线，命中后复用
`IDreamInteractableInterface::OnInteracted`，旋转和平移组件不需要区分交互来源。

运行时按 **Tab** 进入手办交互模式：主相机以手办显示面为中心推近，显示鼠标并暂停探索输入。
**左键**点击/拖动手办画面中的机关；在显示面上**按住右键拖动**可自由旋转手办展示角度。
再次按 Tab 回到探索，**E** 继续执行原有准星世界交互。手办模式下 E 暂停，避免操作错对象。
居中、右键旋转和调参说明见 [手办居中观察与右键旋转](DreamSpace_手办居中观察与右键旋转.md)。

## 2026-10-06 修复：点击无反应、调试不可见

在 `BP_DreamCharacter` 的实际配置中，显示面为 **80×80**，RT 为 **2200×2500**。
旧实现只允许显示面与 RT 宽高比一致，所有点击都被提前拒绝。旧调试绘制又位于映射成功之后，
所以失败时既没有线，也没有明确原因。用户日志中的左键事件已经到达控制器，问题发生在映射阶段。

现在根据显示面上的纹理坐标及 SceneCapture 的实际投影进行数学反算，支持上述非等比显示。
每条点击退出路径都提供具体诊断；HUD 额外显示屏幕十字和结果文字，解决沿视线的三维线
在当前相机中缩成一点，以及捕获相机远在玩家视野外的问题。

## 数学关系

令 `F` 为手办显示面中心，`P` 为鼠标射线与显示面的交点，`O` 为捕获取景采用的理想观察位置，
`A` 为真实场景锚点，`s` 为手办比例。原有相机位置映射保持为：

```text
C = A + (O - F) / s
```

严格窗口条件成立时（投影、面片张角、宽高比、姿态与缩放均匹配），参考平面上的真实点为：

```text
Q = A + (P - F) / s
normalize(Q - C) = normalize(P - O)
```

所以“将点击方向映射到捕获相机”是成立的。但显示面与 RT 宽高比不一致时，图像会被拉伸，
此时简单复制 `P - O` 的方向不再对应显示的像素。新版将以上简式推广成实际投影反算：

```text
LocalP = inverse(DisplayWorldTransform) * P
U = (LocalP.X - MeshBounds.Min.X) / MeshBounds.Size.X
V = (LocalP.Y - MeshBounds.Min.Y) / MeshBounds.Size.Y
```

这是默认 Engine Plane 的 UV0 布局，自动化测试直接检查资产顶点 UV 与该计算一致。
`bRotateDisplayImage180Degrees` 改变了显示网格的真实姿态，逆变换已经包含它，不能再翻转一次 UV。

随后调用 UE 5.8 的 `UGameplayStatics::DeprojectSceneCaptureComponentToWorld`，
使用 SceneCapture 当前姿态、FOV、RT 宽高比及自定义投影矩阵生成世界方向。
普通透视投影的等价表达为（`r = RT.Width / RT.Height`，`t = tan(FOV / 2)`）：

```text
D = normalize(Forward + Right * (2U - 1) * t + Up * (1 - 2V) * t / r)
Ray = C + D * distance
```

引擎反投影返回近裁剪面上的起点和单位方向；当前仅支持透视捕获，因此控制器使用同线的
捕获光心 `C` 作为射线起点。引擎会把 RT 像素位置取整，存在小于一个像素的取样误差。
整个流程没有读取 RT 颜色/深度，也没有 GPU→CPU 回读。

实际玩家相机可能被 SpringArm 碰撞推近，或者滚轮缩放改变位置。它只负责确定用户点到的
显示面位置；捕获方向使用生成画面的 SceneCapture 自身，因此无需强制两台相机位置一致。

## 代码职责与处理顺序

1. `ADreamPlayerController::InteractWithMiniature` 处理左键，反投影鼠标位置。
2. `InteractWithMiniatureRay` 执行完整拾取路径，便于用已知视线做自动化回归。
3. 表现组件的 `MapViewRayToDisplayUV` 用真实显示变换和原始 Bounds 解析求交。
   只接受正面、矩形范围内的点；显示网格保持 `NoCollision`。
4. `TryMapViewRayToCaptureRay` 验证资源和透视投影，调用引擎反投影并返回失败原因。
5. 控制器检查实际相机到面片之间的遮挡，忽略持有手办的 Pawn。
6. 从 SceneCapture 光心做真实世界碰撞射线，忽略 Pawn、捕获黑名单及其 ChildActor。
7. 首个命中物若没有交互组件，则作为遮挡物结束检测；有组件则调用原有 `DispatchInteraction`。

`MiniatureInteractTraceDistance` 默认 **50,000 cm**，独立于 E 的 600 cm。
捕获相机距锚点常在上万厘米，不能使用普通 E 的短距离。
碰撞通道沿用 `InteractTraceChannel`（默认 **Visibility**），目标需要阻挡该通道，
并且拥有可供复杂射线查询的碰撞。只有画面而没有碰撞的对象无法被拾取。

## 支持范围

- 透视 SceneCapture；支持方形、横向、纵向 RT，无需与面片宽高比相同。
- 支持手动 FOV、固定面片朝向、额外的正缩放和关闭 180° 图像修正。
- 显示网格须为局部 XY 平面上的薄矩形，UV0 与 Engine Plane 一致，材质直接采样 RT。
- 负缩放/镜像、正交捕获和三维外壳不在当前支持范围，诊断会说明不支持的配置。
- 自定义 UV 布局、材质内的 UV 裁切/翻转/扭曲必须同步修改映射规则。

宽高比不匹配会影响显示比例，但不再阻止交互；若要求严格的几何窗口效果，仍应保持
显示面与 RT 的宽高比一致。运行时仅修改 `DisplayWorldSize` 不会重建已有网格尺寸，
拾取以实际显示网格变换为准。

真实世界射线使用碰撞数据，RT 使用渲染数据。透明物体、缺少碰撞的网格、不同的运行时
隐藏规则都可能造成视觉与碰撞不同。当前同一 Actor 上若有多个交互组件，**一次命中会
触发该 Actor 上全部交互组件**；不自动搜索父 Actor。不同部位要绑定不同操作时，需要
另外建立 PrimitiveComponent 到交互组件的显式关系。

## 调试操作

控制台输入 `dream.DebugMiniatureInteraction 1`，关闭用 `dream.DebugMiniatureInteraction 0`。
Tab 进入手办模式后点击，屏幕十字和结果文字保留 **6 秒**，每次点击刷新：

| 颜色 | 含义 |
| --- | --- |
| 红色 | 显示面越界、背面、资源/投影无效，或已映射但世界射线未命中；文字区分具体原因 |
| 橙色 | 玩家相机到手办之间被真实物体遮挡，显示遮挡 Actor 名 |
| 黄色 | 世界射线已命中，但该 Actor 没有交互组件 |
| 绿色 | 已向命中 Actor 的交互组件发送调用 |

三维调试：青线/球是玩家射线和面片交点，世界绿/红线是捕获射线命中/未命中，黄球是真实命中点。
失败的映射也会画一条红色尝试射线。沿视线看直线会接近一个点，可通过屏幕十字确认点击已接收。
捕获射线在远处的真实世界中；现有捕获黑名单会排除世界 LineBatcher，**不会把调试线拍进 RT**。
需要观察捕获相机与世界射线时，可在 PIE 弹出/自由视角查看对应区域。
`dream.DebugSceneCapture 1` 是另一个开关，用于查看捕获相机、锚点和面片朝向。

绿色仅代表已调用接口。组件正在运动、缺少枢轴或已到行程端点时，组件可能忽略触发，
需结合 Output Log 中旋转/平移组件自己的说明判断。点击结果日志统一带 `[手办点击]` 前缀。

## 验证

自动化套件：`DreamSpace.Presentation`。保留原有窗口相机几何测试，新增/更新：

- `MiniatureClickRays`：面片中心/角点、额外缩放、180° 姿态、相机偏移、越界、背面和平行射线。
- `MiniatureConfiguredProjection`：加载实际 `BP_DreamCharacter`，创建真实表现资源并调用运行时入口；
  覆盖真实 RT 尺寸、1024×1024、2500×2200、2200×2500，检查 Engine Plane UV 与世界方向。
  同时走控制器的完整碰撞和分发路径，检查旋转/平移后的 Actor 变换及遮挡、黑名单、诊断反馈。

可视验收仍需在 TestMap2 中检查鼠标交互与渲染画面的对应。此次启动了实际关卡，但电脑窗口
抓取连续返回 `FrameArrived/window capture timed out`，未能完成可视点击验收；不将自动化结果
等同于可视验收。测试世界为瞬时世界，不保存或修改用户关卡/蓝图。

最终验证结果：UE 5.8.2 的 `DreamSpaceEditor Win64 Development` 和 `DreamSpace Win64 Development`
均编译成功；`DreamSpace.Presentation` 的 7 项测试全部通过，包含 Actor 实际旋转 90°、平移 100 cm
及遮挡/黑名单/诊断检查。日志位于 `Saved/Logs/MiniatureFixTests.log`。
