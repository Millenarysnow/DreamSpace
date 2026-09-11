# DreamSpace 可交互对象配置使用手册

本文面向策划、关卡设计和需要配置交互对象的技术美术，说明如何使用 DreamSpace 当前 C++ 交互运行时中的全部配置项。

本文只讲“如何配置和组合已经提供的能力”。新增一种全新的行为时，仍需要程序先新增 C++ 能力类，然后策划才能在资产中配置它。

当前版本基于 UE5.6，默认验收地图为：

```text
/Game/DreamInteraction/Maps/InteractionDemo
```

默认示例资产位于：

```text
/Game/DreamInteraction/Definitions
```

包括：

| 资产 | 用途 |
| --- | --- |
| `DA_LayeredCube` | 分层魔方，演示父节点、层级旋转和部件释放 |
| `DA_GiantKey` | 可缩放、可拾取、可放下的钥匙 |
| `DA_KeyDoor` | 检查钥匙逻辑标签并切换开门状态的门 |
| `DA_GravityRoom` | 提供局部重力并允许整体旋转的房间 |

## 一、先理解四个对象

配置交互对象时，需要区分四个层次：

| 层次 | 在编辑器中的对象 | 负责什么 |
| --- | --- | --- |
| 对象定义 | `InteractiveAssemblyDefinition` Data Asset | 描述一类对象有哪些节点、能力、表现和默认规则 |
| 对象实例 | 场景中的 `InteractiveAssemblyActor` | 把一个对象定义放进关卡，拥有自己的实例 ID 和运行时状态 |
| 节点 | 定义资产中的 `Nodes` 数组元素 | 描述建筑根、层、房间、门板、方块等局部部件及父子关系 |
| 能力 | 定义资产或节点中的 `Capabilities` 数组元素 | 描述对象可以做什么，以及这个行为的限制和目标 |

同一个对象定义可以放置多次。每个场景实例拥有自己的状态：一个钥匙被拾取不会影响另一个引用同一 DA 的钥匙。

对象定义资产负责“默认结构”；场景 Actor 负责“这个实例放在哪里”。运行时旋转、缩放、破坏、拾取和状态标签都保存在实例的逻辑状态中，Mesh 只是根据状态表现出来的结果。

## 二、创建和放置一个对象

### 2.1 创建对象定义

在内容浏览器中创建 `InteractiveAssemblyDefinition` 类型的 Data Asset。也可以复制一个现有定义作为起点。

建议每个可复用对象建立一个独立 DA，例如：

```text
DA_MagicHouse
DA_RotatingRoom
DA_SmallKey
DA_LiftDoor
```

不要把不同外观、不同部件结构和不同规则的对象全部堆进同一个 DA。一个 DA 应该代表一类可以复用的对象定义。

### 2.2 把定义放进场景

在关卡中放置 `InteractiveAssemblyActor`，在 Details 面板中设置：

| 字段 | 配置方法 | 说明 |
| --- | --- | --- |
| `Definition` | 选择一个对象定义资产 | 决定这个实例有哪些节点、能力和表现 |
| `AssemblyId` | 通常不用手动填写 | 创建实例时自动生成；普通编辑器复制会生成新 ID，PIE 会保留关卡实例 ID |

`AssemblyId` 是场景实例的持久 ID，存档、命令、事件和调试信息都会使用它。不要手动让两个场景 Actor 使用同一个 ID。运行时发现重复 ID 时会拒绝覆盖旧实例。

放置完成后运行 PIE，Actor 会根据定义资产创建节点的碰撞代理和 Mesh 表现。定义资产缺失、节点 ID 重复、父节点不存在或能力配置无效时，初始化会失败并在日志中报错。

## 三、对象定义顶部的配置

选中 `InteractiveAssemblyDefinition` 资产时，顶部是装配体级配置。

### 3.1 装配体基本信息

| 字段 | 作用 | 什么时候配置 |
| --- | --- | --- |
| `DisplayName` | HUD、调试和目标描述中显示的名字 | 所有对象都应填写易读名称 |
| `GameplayTags` | 对象的静态身份标签 | 用于“这是钥匙”“这是机关”“属于某个阵营”等筛选 |
| `DefaultStateTags` | 对象实例启动时的动态状态标签 | 例如初始为 `State.Locked`、`State.Powered` |
| `Capabilities` | 装配体级能力列表 | 能力适用于整个对象，或希望由子节点继承时配置 |
| `Nodes` | 部件树 | 所有有逻辑意义的部件都在这里配置 |
| `Gravity` | 装配体级重力体积 | 整个对象提供局部重力时配置 |
| `DefaultOccupantPolicy` | 建筑变换时内部占用者的默认处理方式 | 建筑会旋转、移动且可能包含玩家时配置 |

### 3.2 `GameplayTags` 和 `DefaultStateTags` 的区别

`GameplayTags` 描述“它是什么”，通常不会因为一次普通操作而变化。例如：

```text
Item.Key
Interactable.Door
Building.MagicHouse
```

`DefaultStateTags` 描述“它当前处于什么状态”，会被状态转移或其他能力改变。例如：

```text
State.Locked
State.Powered
State.Carried
State.Unlocked
```

门的开门条件应该检查钥匙的 `GameplayTags` 和当前携带状态，而不是检查钥匙 Mesh 的名字或大小。

当前代码已经注册的基础标签有：

| 标签 | 含义 | 当前用途 |
| --- | --- | --- |
| `Item.Key` | 对象是钥匙 | `DA_GiantKey` 的身份；钥匙门通过它识别钥匙类型 |
| `State.Carried` | 对象当前被携带 | Pickup 成功后自动添加，Drop 成功后自动移除 |
| `State.Unlocked` | 门或机关已经解锁 | `DA_KeyDoor` 开门成功后添加 |

策划可以在编辑器中填写其他 Gameplay Tag，但如果标签要参与新的 C++ 条件、任务系统或能力逻辑，建议先让程序登记统一的标签名称，避免同一概念出现多个拼写。

### 3.3 `Gravity` 装配体级重力

装配体级重力是一个跟随装配体根坐标系旋转的盒形体积。

| 字段 | 含义 | 配置规则 |
| --- | --- | --- |
| `bEnabled` | 是否启用这组重力配置 | 不需要重力时关闭 |
| `Center` | 体积中心，相对装配体根的局部坐标 | 单位为厘米；例如 `(0,0,0)` 表示根位置 |
| `Extent` | 盒体半尺寸 | `(500,500,200)` 表示完整尺寸约为 `1000×1000×400` cm，必须为正数 |
| `LocalDirection` | 体积内的局部重力方向 | 常规地面使用 `(0,0,-1)`；不能是零向量 |
| `Priority` | 重叠重力源时的优先级 | 数值越大越优先；同优先级时选择体积更小的源 |

运行时会把 `LocalDirection` 通过装配体或节点参考系旋转成世界方向。房间旋转后，房间内的重力方向也会改变。

### 3.4 `DefaultOccupantPolicy`

建筑变换时，玩家或已登记的内部占用者可能受到影响。默认策略有三种：

| 值 | 行为 | 使用建议 |
| --- | --- | --- |
| `FollowAssembly` | 占用者跟随建筑参考系一起移动、旋转，速度和重力方向也同步旋转 | 建筑内部空间、旋转房间、魔方建筑的默认选择 |
| `KeepWorldTransform` | 建筑改变后，占用者保持提交瞬间的世界位置和朝向 | 占用者不属于建筑内部，或建筑变换不应带走玩家时使用 |
| `FailOperation` | 受影响空间中存在占用者时直接拒绝操作 | 变换会造成危险、挤压或规则上不允许时使用 |

某个能力可以通过 `bOverrideOccupantPolicy` 和 `OccupantPolicy` 覆盖对象定义的默认策略。

## 四、节点 `Nodes` 的全部配置

每个 `Nodes` 元素都是一个逻辑节点。节点可以有 Mesh，也可以完全没有 Mesh，只作为层级父节点、Pivot、碰撞代理或重力参考系。

### 4.1 节点身份和父子关系

| 字段 | 作用 | 配置方法 |
| --- | --- | --- |
| `NodeId` | 节点的稳定 ID | 新节点使用 `Assign Missing Node Ids` 生成；已有节点不要重新生成 |
| `ParentNodeId` | 父节点的稳定 ID | 无效 GUID 表示直接挂在装配体根；填写时必须引用同一 DA 中存在的节点 |
| `NodeName` | 编辑器、HUD、日志中的可读名字 | 用功能名或部件名，例如 `Floor`、`DoorPanel`、`Layer_1` |
| `DefaultLocalTransform` | 节点相对父节点的逻辑变换 | 优先使用局部位置和局部旋转；坐标单位为厘米 |
| `GameplayTags` | 节点的静态标签 | 用于按部件类型筛选，例如 `Building.DoorPanel`、`Gravity.Surface` |

节点变换保存的是相对父节点的局部变换。世界变换由装配体根、父节点链和节点局部变换共同计算。

这意味着：

- 旋转一个父节点，子节点的位置和朝向会自然跟随。
- 不要把同一组子节点的世界坐标逐个写死。
- 节点顺序不参与层级计算，存档和命令通过 `NodeId` 查找节点。
- `DefaultLocalTransform` 的缩放应保持均匀；外观长宽高比例放在 `MeshTransform`。

### 4.2 表现字段

| 字段 | 作用 | 配置建议 |
| --- | --- | --- |
| `Mesh` | 完整状态下显示的静态 Mesh | 没有可见表现的逻辑父节点可以留空 |
| `BrokenMesh` | 节点进入 `Broken` 或 `Released` 状态后替换显示的 Mesh | 破坏后仍有独立表现时填写 |
| `Material` | 节点材质 | 当前运行时会为材质创建动态实例 |
| `Tint` | 传给材质参数 `Tint` 的颜色 | 使用原型材质或带同名 Vector 参数的材质时有效 |
| `MeshTransform` | Mesh 相对逻辑节点的表现变换 | 用于调整 Mesh 原点、外观比例和偏移，不改变节点逻辑坐标 |

`MeshTransform` 不应该被用来表达节点父子关系。比如一个长柱子应该保持节点的逻辑缩放为 `(1,1,1)`，再在 `MeshTransform` 中设置细长比例。

如果自定义材质没有名为 `Tint` 的 Vector 参数，`Tint` 不会改变材质颜色，但不会影响交互逻辑。

### 4.3 `Sockets`

`Sockets` 是节点上的命名逻辑锚点，类型为“名字 → 局部变换”。

| 用途 | 示例 |
| --- | --- |
| 旋转铰链 | `Hinge` |
| 携带插槽 | `CarryPoint` |
| 交互提示位置 | `InteractPoint` |
| 重力面中心 | `GravityFace` |
| 破坏特效位置 | `BreakFX` |

Socket 变换相对节点自身坐标系。Pivot 选择 `Socket` 时，还需要在能力中填写 `PivotNodeId` 和 `PivotSocket`。

Socket 是逻辑资产配置，不依赖 Mesh 自带的 Socket 名称。更换 Mesh 后只要保持逻辑锚点位置，存档、命令和玩法规则就不会因为美术改名而失效。

### 4.4 碰撞字段

运行时为每个节点创建盒形碰撞代理。核心交互校验使用这个代理，不依赖 Mesh 自己的碰撞复杂度。

| 字段 | 作用 | 注意事项 |
| --- | --- | --- |
| `bCollisionEnabled` | 是否参与节点碰撞 | 逻辑父节点通常关闭；可见的实体部件通常开启 |
| `bCollisionWhenBroken` | `Broken` 状态下是否继续阻挡 | 破坏后仍是实体墙体时开启，碎裂后应可穿过时关闭 |
| `CollisionCenter` | 盒形代理中心，相对节点局部坐标 | 不要通过移动节点原点代替调整碰撞中心 |
| `CollisionExtent` | 盒形代理半尺寸 | 单位为厘米，三个分量必须为正数 |

`bSelectable` 和 `bCollisionEnabled` 是两个不同概念：

- `bSelectable=false`：玩家射线不能把该节点作为交互目标。
- `bCollisionEnabled=false`：节点不作为实体阻挡，但仍可以有 Mesh、父子关系或能力。
- 一个逻辑父节点通常配置为 `bSelectable=false`、`bCollisionEnabled=false`，能力仍可以通过子节点选择或 P 键进入。

### 4.5 节点级重力

节点也可以拥有自己的 `Gravity` 配置。它的字段与装配体级重力相同，但参考系是该节点的世界变换。

适合的场景包括：

- 房间中的重力面；
- 只覆盖某一层的重力机关；
- 旋转建筑中某个局部区域的特殊重力；
- 多个重力区域互相嵌套。

重力源选择顺序为：优先级高者优先；优先级相同时选择体积更小者；仍相同时按稳定 ID 决胜。玩家离开全部重力体积后恢复世界向下重力。

## 五、能力的共同配置

所有能力都继承 `UDreamInteractionCapability`。无论具体能力是旋转、破坏、拾取还是组合操作，都会先使用这一组通用规则。

### 5.1 能力身份

| 字段 | 作用 | 配置建议 |
| --- | --- | --- |
| `CapabilityId` | 能力在一个对象层级中的稳定行为 ID | 同一层级不要重复；需要覆盖父级时使用相同 ID |
| `DisplayName` | HUD 和调试中的显示名 | 用玩家能理解的动作名，例如“旋转”“释放部件” |
| `AllowedModes` | 能力可使用的交互模式 | 空数组表示所有模式；否则只允许列出的模式 |
| `bEnabledByDefault` | 是否默认启用能力 | 需要由任务或系统后续控制时可以关闭 |
| `bInheritToChildren` | 父级能力是否允许子节点继承 | 建筑整体能力通常开启；只作用于本节点的能力可以关闭 |

当前模式有：

| 模式 | 主要用途 |
| --- | --- |
| `ThirdPerson` | 走路、观察、拾取、开门、近距离破坏和使用 |
| `Overview` | 拉远镜头、选择部件、显示 Gizmo、旋转、缩放和移动建筑 |

`AllowedModes` 为空不是“禁止所有模式”，而是“所有模式都允许”。

### 5.2 状态条件

| 字段 | 作用 | 示例 |
| --- | --- | --- |
| `RequiredStateTags` | 必须全部拥有的装配体状态标签 | 只有 `State.Powered` 时才能启动 |
| `BlockedStateTags` | 只要拥有任意一个就禁止操作 | `State.Locked`、`State.Destroyed` |
| `RequiredNodeTags` | 目标节点必须拥有的标签 | 只允许操作 `Building.RotatablePart` |

状态标签是逻辑条件。策划不需要也不应该通过 Mesh 名称、材质颜色或当前世界坐标判断任务条件。

### 5.3 目标配置

| 字段 | 作用 | 适用情况 |
| --- | --- | --- |
| `ConfiguredNodeIds` | 固定能力作用的节点集合 | 魔方层、固定机关组、同一扇门的多个部件 |
| `bTargetAssemblyRoot` | 不管点击哪个节点，都把目标解析为装配体根 | 整体缩放钥匙、整体携带、整体开门 |

目标选择优先级如下：

1. `bTargetAssemblyRoot=true` 时，目标是装配体根。
2. 否则，`ConfiguredNodeIds` 不为空时，使用配置的节点集合。
3. 两者都没有时，使用玩家当前选中的节点。

如果同时配置了 `bTargetAssemblyRoot=true` 和 `ConfiguredNodeIds`，以装配体根为准。

选择父节点和子节点同时作为目标时，系统只保留最上层目标，避免同一个旋转被重复应用。

### 5.4 碰撞和占用者覆盖

| 字段 | 作用 |
| --- | --- |
| `bRejectCollisions` | 该能力提交前是否进行碰撞和玩家净空验证 |
| `bOverrideOccupantPolicy` | 是否覆盖对象定义的默认占用者策略 |
| `OccupantPolicy` | 覆盖后的占用者策略 |

一般能力应保持 `bRejectCollisions=true`。只有明确知道自己不需要空间验证的表现型或纯标签型能力才考虑关闭。

## 六、Transform 变换能力

能力类：`UDreamTransformCapability`。

这是当前最重要的通用能力，可以挂在装配体、层节点或其他部件上。它不代表“魔方专用逻辑”，同一能力也可以配置给房间、雕像、门板、机关和独立物件。

### 6.1 变换类型

| 字段 | 作用 |
| --- | --- |
| `bAllowRotation` | 是否允许旋转 |
| `bAllowScaling` | 是否允许均匀缩放 |
| `bAllowTranslation` | 是否允许移动 |

关闭某一种类型后，即使输入命令包含对应增量，也会被拒绝。

### 6.2 旋转轴和吸附

| 字段 | 作用 | 示例 |
| --- | --- | --- |
| `DefaultRotationAxis` | 没有使用输入轴时的默认轴 | `(0,0,1)` 表示局部 Z 轴 |
| `bAllowInputAxis` | 是否使用玩家 X/Y/Z 轴选择 | 开启后，能力允许输入层提供旋转轴 |
| `RotationStepDegrees` | 旋转吸附步长 | `90` 表示每次吸附到 90 度倍数 |

当前引用的坐标系是能力自己的 `ReferenceFrame`。`DefaultRotationAxis` 不是永远的世界轴，它会先转换到指定参考坐标系。

**当前魔方示例的特殊配置：**

- 装配体顶部的 Transform 开启了 `bAllowInputAxis=true`。
- 三个 Layer 节点上的 Transform 没有开启 `bAllowInputAxis`，默认轴为 Z 轴。
- 点击方块时，方块继承所属 Layer 的 Transform；Layer 的同名能力覆盖了装配体顶部能力。
- 因此点击方块后，X/Y/Z 只会改变控制器中的“候选输入轴”，但 Layer 能力忽略它，仍固定绕局部 Z 轴旋转。

如果以后要让某一层支持 X/Y/Z 轴，需要修改该层的 Transform 能力，而不是只修改装配体顶部的 Transform。

### 6.3 移动和缩放吸附

| 字段 | 作用 | `0` 的含义 |
| --- | --- | --- |
| `TranslationStep` | 移动吸附步长，单位为厘米 | 不吸附，使用输入的连续值 |
| `ScaleStep` | 缩放增量吸附步长 | 不吸附，使用输入的连续值 |
| `MinimumScale` | 相对默认尺寸的最小比例 | 例如 `0.1` 表示不能小于默认尺寸的 10% |
| `MaximumScale` | 相对默认尺寸的最大比例 | 例如 `4` 表示不能超过默认尺寸的 4 倍 |
| `MaximumRotationPerOperation` | 单次命令允许的最大旋转角度 | 防止一次输入跳转过大 |
| `MaximumTranslationPerOperation` | 单次命令允许的最大移动距离 | 单位为厘米 |

缩放限制是相对定义资产默认尺寸的持久限制，不是只限制一次按键。重复操作不能通过多次提交绕过最大值。

### 6.4 参考坐标系

| 值 | 计算方式 | 适用情况 |
| --- | --- | --- |
| `World` | 使用世界坐标轴 | 需要无论对象朝向如何都沿世界方向操作 |
| `Assembly` | 使用装配体根的局部坐标轴 | 建筑、房间、魔方的默认选择 |
| `Node` | 使用指定节点的局部坐标轴 | 铰链、门板、局部机械部件 |
| `Custom` | 使用 `CustomFrameRotation` 提供的自定义方向 | 有特殊玩法坐标系时使用 |

如果使用 `Node`，应确保 `PivotNodeId` 或目标节点存在。如果使用 `Custom`，`CustomFrameRotation` 必须是有效的归一化旋转。

### 6.5 Pivot 配置

| `PivotMode` | 含义 | 需要额外字段 |
| --- | --- | --- |
| `SelectionCenter` | 使用目标节点世界位置的平均中心 | 无 |
| `NodeOrigin` | 使用 `PivotNodeId` 节点原点 | `PivotNodeId` |
| `Socket` | 使用节点上的命名 Socket | `PivotNodeId`、`PivotSocket` |
| `ConfiguredPoint` | 使用手动填写的局部点 | `PivotPoint` |

Pivot 是旋转和整体缩放的中心，不应默认假设为 Mesh 原点。门的铰链、建筑的支点和魔方层中心都应该通过 Pivot 或 Socket 明确配置。

### 6.6 Transform 配置示例

#### 整体旋转建筑

在 DA 顶部添加一个 Transform：

```text
AllowedModes = Overview
bAllowRotation = true
bAllowScaling = false
bAllowTranslation = false
bAllowInputAxis = true
ReferenceFrame = Assembly
PivotMode = NodeOrigin
PivotNodeId = 建筑根节点或中心节点
RotationStepDegrees = 90
bTargetAssemblyRoot = true
```

#### 只能沿铰链旋转的门板

```text
bAllowRotation = true
bAllowScaling = false
bAllowTranslation = false
bAllowInputAxis = false
DefaultRotationAxis = (0,1,0)
ReferenceFrame = Node
PivotMode = Socket
PivotNodeId = 门板节点 ID
PivotSocket = Hinge
```

#### 只能在全局视角缩放的钥匙

```text
AllowedModes = Overview
bAllowRotation = false
bAllowScaling = true
bAllowTranslation = false
MinimumScale = 0.1
MaximumScale = 2.0
PivotMode = ConfiguredPoint
PivotPoint = (0,0,-210)
bTargetAssemblyRoot = true
```

## 七、State Transition 状态转移能力

能力类：`UDreamStateTransitionCapability`。

它用于在有限状态之间切换，不直接等同于销毁 Actor。常见状态为：

```text
Intact → Damaged → Broken → Released / Destroyed
```

### 7.1 配置字段

| 字段 | 作用 |
| --- | --- |
| `AllowedSourceStates` | 允许从哪些当前状态开始 |
| `TargetState` | 成功后切换到的目标状态 |
| `bExistsAfterTransition` | 转移后节点是否仍存在于逻辑状态 |
| `bLockAfterTransition` | 转移后节点是否锁定，锁定后能力不能再操作 |
| `AddedStateTags` | 转移成功后添加到装配体的状态标签 |
| `RemovedStateTags` | 转移成功后从装配体移除的状态标签 |

状态能力的目标仍然由通用目标字段决定。可以作用于当前节点、固定节点集合或装配体根。

### 7.2 各状态的表现含义

| 状态 | 建议含义 |
| --- | --- |
| `Intact` | 完整、可正常使用 |
| `Damaged` | 已损坏但仍存在，通常可以继续修复或进一步破坏 |
| `Broken` | 破坏表现状态，可以替换 `BrokenMesh`，碰撞由 `bCollisionWhenBroken` 决定 |
| `Released` | 从父层级释放，保持当前世界姿态，之后作为独立节点继续操作 |
| `Destroyed` | 逻辑上销毁，表现隐藏，碰撞关闭，通常不能恢复 |

### 7.3 状态能力的例子

#### 墙体先损坏，再完全破坏

能力 A：

```text
CapabilityId = DamageWall
AllowedSourceStates = Intact
TargetState = Damaged
bExistsAfterTransition = true
```

能力 B：

```text
CapabilityId = BreakWall
AllowedSourceStates = Damaged
TargetState = Broken
bExistsAfterTransition = true
```

#### 部件拆分为独立节点

```text
CapabilityId = ReleasePart
AllowedSourceStates = Intact, Damaged
TargetState = Released
bExistsAfterTransition = true
bLockAfterTransition = false
```

释放时节点的世界姿态会被保存到新的根局部变换中；移动原装配体根不会再次带动已经释放的部件。

## 八、Destruction 破坏能力

能力类：`UDreamDestructionCapability`。

它是状态转移能力的专用配置，默认表示“破坏”，但当前魔方示例把它配置成“释放部件”。它继承 State Transition 的状态字段，因此配置方式大体相同。

当前魔方的 Destruction 配置如下：

```text
CapabilityId = Destruction
DisplayName = 释放部件
AllowedSourceStates = Intact, Damaged
TargetState = Released
bExistsAfterTransition = true
bLockAfterTransition = false
```

玩家在全局视角或第三人称按 `B` 时，系统会把当前目标节点切换为 Released。它不会直接销毁 Actor，也不会丢失节点 ID。

如果要实现真正的碎裂墙体，建议配置：

```text
TargetState = Broken
bExistsAfterTransition = true
```

并为节点填写 `BrokenMesh`、`bCollisionWhenBroken` 和破坏表现资源。当前版本的表现替换入口已经存在，Niagara、音效和动画事件可以由表现层继续接入。

## 九、Pickup 拾取能力

能力类：`UDreamPickupCapability`。

它适合钥匙、可携带雕像、小型建筑部件等对象。拾取状态保存在装配体逻辑状态中，同一个对象实例会进入携带参考系。

### 9.1 配置字段

| 字段 | 作用 | 配置建议 |
| --- | --- | --- |
| `MaximumScaleToPickup` | 允许拾取的最大逻辑尺寸比例 | 只有缩小到不大于该值时才允许拾取 |
| `MaximumDistance` | 玩家到对象根的最大拾取距离 | 单位为厘米 |
| `CarryTransform` | 对象相对持有者插槽的携带变换 | 用于确定携带时的位置、旋转和局部比例 |
| `CarriedStateTag` | 携带成功后加入的状态标签 | 通常使用 `State.Carried` |
| `bDropInsteadOfPickup` | 是否把该能力当作放下能力 | 拾取配置为 false，放下配置为 true |
| `bTargetAssemblyRoot` | 是否整体拾取/放下 | 钥匙、雕像等整体对象通常开启 |

### 9.2 拾取条件

当前运行时会检查：

- 对象没有已经携带；
- 对象尺寸不超过 `MaximumScaleToPickup`；
- 玩家与对象距离不超过 `MaximumDistance`；
- 对象没有处于 Released 或不可操作状态；
- 当前玩家没有携带另一个对象；
- 目标对象和玩家都有有效稳定 ID。

### 9.3 携带和放下

携带成功后：

- `bIsCarried` 变为 true；
- `CarrierId` 记录玩家占用者 ID；
- `CarryTransform` 记录携带插槽局部变换；
- `CarriedStateTag` 加入逻辑状态标签；
- 对象根表现跟随玩家携带参考系刷新；
- 对象不会依赖 Mesh 名称判断自己是否被携带。

放下能力应复制同一个对象的 Pickup 能力，再设置：

```text
CapabilityId = Drop
DisplayName = 放下
bDropInsteadOfPickup = true
```

放下时会检查目标位置净空。无法放下时，整个事务失败，物体仍保持携带状态。

## 十、Sequence 组合能力

能力类：`UDreamSequenceCapability`。

它把多个能力按顺序组合。每个步骤先作用于临时状态，所有步骤成功后才进入一次事务提交。

### 10.1 配置字段

| 字段 | 作用 |
| --- | --- |
| `Steps` | 按顺序执行的子能力列表 | 至少一个步骤；当前版本禁止嵌套 Sequence |

步骤可以组合：

1. 添加状态标签；
2. 生成变换结果；
3. 切换节点状态；
4. 根据前一步的临时标签检查下一步条件。

任何一步失败，前面的临时结果都会被丢弃，不会出现“第一步已经提交、第二步失败”的半完成状态。

### 10.2 适合使用 Sequence 的情况

例如“拉杆通电后旋转平台，再打开门”：

```text
Step 1: StateTransition，添加 State.Powered
Step 2: Transform，旋转平台
Step 3: StateTransition，门从 Locked 切换为 Unlocked
```

如果操作需要条件分支、并行、等待动画或网络确认，应先由程序扩展组合能力的执行模型；当前 Sequence 是平坦、有序、单事务组合。

## 十一、Key Door 钥匙门能力

能力类：`UDreamKeyDoorCapability`。

它是 Gameplay 层的专用条件能力，继承状态转移能力。门不读取钥匙 Mesh 的尺寸、名称或材质，只检查钥匙的逻辑标签和携带状态。

### 11.1 配置字段

| 字段 | 作用 |
| --- | --- |
| `RequiredKeyTag` | 需要的钥匙身份标签 | 例如 `Item.Key.Red` |
| `UseDistance` | 玩家与门的最大使用距离 | 单位为厘米 |
| 继承字段 `TargetState` | 开门后门节点的状态 | 当前默认设置为 Destroyed |
| 继承字段 `bExistsAfterTransition` | 开门后门是否仍存在 | 当前示例为 false，表示门碰撞和表现移除 |
| 继承字段 `AddedStateTags` | 开门后添加的状态标签 | 当前示例添加 `State.Unlocked` |

当前示例门的逻辑结果是：门节点切换为 Destroyed、节点不再存在、装配体增加 `State.Unlocked`。

如果未来有“门打开但仍保留门框”的需求，可以把门框和门板拆成不同节点，只让门板进入 Destroyed 或 Broken。

## 十二、能力继承、覆盖和优先级

这是配置中最容易影响实际效果的部分。

### 12.1 查找顺序

当玩家选中一个节点时，系统会按以下路径收集能力：

```text
当前节点 → 父节点 → 更上层父节点 → 装配体定义顶部
```

收集时以 `CapabilityId` 去重：距离目标最近的配置优先，后面的同 ID 能力被覆盖。

例子：

```text
装配体顶部：Transform
Layer_2：Transform
Block_2_1_1：无本地能力
```

点击 `Block_2_1_1` 后，实际使用的是：

```text
Layer_2 的 Transform
```

因为它比装配体顶部的 Transform 更靠近目标，而且两者的 `CapabilityId` 都是 `Transform`。

### 12.2 节点能力和顶部能力的区别

| 配置位置 | 典型用途 |
| --- | --- |
| DA 顶部 `Capabilities` | 对整个装配体通用的能力，或希望由很多子节点继承的能力 |
| 父节点 `Capabilities` | 对一组子节点通用的能力，例如魔方某一层的旋转 |
| 子节点 `Capabilities` | 只对该部件有效，或覆盖父级同 ID 能力的特殊规则 |

节点自身没有能力时，不代表它不可交互；它可能继承父节点或顶部能力。

### 12.3 `bInheritToChildren`

父级能力只有在 `bInheritToChildren=true` 时才会传给子节点。

建议：

- 建筑整体的通用 Transform：开启；
- 某个只允许点击自身的按钮能力：关闭；
- 层节点的层旋转能力：通常开启，让层内方块可以继承；
- 只允许通过父节点选择的管理能力：可以关闭，并将节点设为不可选择。

### 12.4 `DisabledInheritedCapabilities`

在节点中填写能力 ID，可以屏蔽从父级继承的同名能力。

例如：

```text
装配体顶部：Transform、Pickup
某个装饰部件：DisabledInheritedCapabilities = Pickup
```

该装饰部件仍可以继承 Transform，但不会继承 Pickup。屏蔽的是继承来的能力；如果节点自己配置了同 ID 的本地能力，本地能力仍然是该节点自己的配置。

### 12.5 覆盖是整套配置覆盖

同一个 `CapabilityId` 被子节点覆盖时，使用的是子节点这份能力对象的完整配置，不会把父级和子级字段逐项合并。

如果子节点只想改一个参数，建议复制父级能力配置后再修改；否则没有填写的字段会使用能力类默认值。

## 十三、魔方示例资产逐项说明

当前 `DA_LayeredCube` 不是 27 个平级方块，而是一个三层父子树。

### 13.1 节点数量

实际总数是 **30 个节点**：

```text
3 个层父节点 + 27 个方块节点 = 30 个节点
```

数组索引如下：

| 数组索引 | 节点 |
| --- | --- |
| `0` | `Layer_1` |
| `1～9` | 第一层九个方块 |
| `10` | `Layer_2` |
| `11～19` | 第二层九个方块 |
| `20` | `Layer_3` |
| `21～29` | 第三层九个方块 |

因此，如果 Details 面板最后一个索引显示为 `29`，表示数组有 30 个元素，不是 29 个元素。

### 13.2 三个 Layer 节点

每个 Layer 节点的配置特点：

| 字段 | 当前配置 |
| --- | --- |
| Mesh | 空 |
| `bCollisionEnabled` | false |
| `bSelectable` | false |
| 父节点 | 装配体根 |
| 子节点 | 九个方块 |
| 能力 | 一个 Transform |
| `ConfiguredNodeIds` | 只包含当前 Layer 自己的 ID |
| `bAllowInputAxis` | false |
| 默认旋转轴 | 局部 Z 轴 |

Layer 是逻辑坐标系和能力代理，不是一个需要显示出来的方块。九个子方块通过它的局部坐标形成一层。

### 13.3 27 个方块节点

每个方块节点当前配置：

| 字段 | 当前配置 |
| --- | --- |
| Mesh | 引擎基础 Cube |
| 父节点 | 所属 Layer |
| `bSelectable` | true |
| `bCollisionEnabled` | true |
| 自身 Capabilities | 空 |
| 颜色 | 按所在层使用不同蓝、橙、绿颜色和亮度 |

方块没有自己的 Transform 能力，但会继承 Layer 的 Transform。因此点击方块时，实际变换的是 Layer，九个方块一起动。

### 13.4 魔方顶部的两个能力

#### Transform

顶部 Transform 是装配体级能力，当前主要配置：

```text
CapabilityId = Transform
AllowedModes = Overview
bAllowRotation = true
bAllowScaling = true
bAllowTranslation = true
DefaultRotationAxis = Z
bAllowInputAxis = true
RotationStepDegrees = 90
MinimumScale = 0.1
MaximumScale = 4.0
ReferenceFrame = Assembly
PivotMode = SelectionCenter
```

它适合在选择装配体根时整体变换魔方。由于 Layer 节点存在同 ID 的本地 Transform，点击方块时不会使用这份顶部配置。

#### Destruction

顶部 Destruction 当前被改名为“释放部件”，主要配置：

```text
CapabilityId = Destruction
AllowedSourceStates = Intact, Damaged
TargetState = Released
bExistsAfterTransition = true
bLockAfterTransition = false
```

它没有固定目标集合，所以点击某个方块后会作用于当前节点。方块释放后保持自己的世界姿态，之后可以继续使用通用 Transform 能力独立操作。

## 十四、其他三个示例资产逐项说明

### 14.1 `DA_GiantKey`

对象顶部能力：

| 能力 | 关键配置 | 结果 |
| --- | --- | --- |
| Transform | `bTargetAssemblyRoot=true`、不允许旋转、允许缩放、比例 `0.1～2.0`、Pivot 为配置点 `(0,0,-210)` | 无论点击钥匙哪个部件，都整体缩放 |
| Pickup | `bTargetAssemblyRoot=true`、`MaximumScaleToPickup=0.35` | 缩小到阈值并靠近玩家后可以拾取 |
| Drop | `bDropInsteadOfPickup=true` | 携带状态下把钥匙放回世界 |

钥匙由 `Shaft`、`Head`、`Tooth` 三个节点组成。钥匙的身份标签是 `Item.Key`，门通过这个标签识别它。

推荐验收流程：

1. 在 Overview 中选择钥匙；
2. 使用 `-` 缩小，确认缩放限制；
3. 切回 ThirdPerson，靠近钥匙按 `E`；
4. 靠近紫色门按 `E`；
5. 按 `G` 放下。

### 14.2 `DA_KeyDoor`

门顶部配置一个 `OpenDoor` 能力：

```text
RequiredKeyTag = Item.Key
UseDistance = 300
TargetState = Destroyed
bExistsAfterTransition = false
AddedStateTags = State.Unlocked
```

门能否打开取决于：

- 玩家是否有效；
- 玩家是否携带对象；
- 携带对象是否有 `Item.Key` 标签；
- 玩家是否在 `UseDistance` 内。

### 14.3 `DA_GravityRoom`

房间顶部配置 Transform 和重力：

| 配置 | 当前值 |
| --- | --- |
| Transform 的 `bTargetAssemblyRoot` | true |
| Transform 的 `bAllowInputAxis` | true |
| Transform 的 `bAllowScaling` | false |
| Transform 的 Pivot | NodeOrigin |
| Gravity `bEnabled` | true |
| Gravity `Extent` | `(360,360,220)` |
| Gravity `Priority` | `10` |

房间节点包括地板、后墙、左右墙、天花板和一个内部方块。玩家在重力体积内时，旋转房间会同时改变玩家的位置、朝向、速度和重力方向。

当前示例的 `PivotMode=NodeOrigin` 没有填写 `PivotNodeId`，运行时会把空的 Pivot 节点解析为装配体根，因此实际效果仍然是绕房间根坐标系旋转。新对象如果要绕某个具体节点旋转，应同时填写有效的 `PivotNodeId`。

## 十五、节点状态、能力状态和任务条件如何配合

策划可以把逻辑拆成三层：

| 层 | 示例 | 保存在哪里 |
| --- | --- | --- |
| 静态身份 | 这是钥匙、这是门、这是可旋转部件 | `GameplayTags` |
| 动态状态 | 已携带、已通电、已解锁、已损坏 | `DefaultStateTags` 和状态能力结果 |
| 结构状态 | 节点是否存在、是否破坏、是否释放 | `FDreamNodeState`，由状态能力修改 |

例如钥匙门可以使用如下规则：

```text
门的 OpenDoor 能力要求：
1. 玩家距离门不超过 UseDistance；
2. 玩家当前携带一个对象；
3. 携带对象 GameplayTags 包含 Item.Key；
4. 携带对象状态标签包含 State.Carried。
```

这套规则不依赖钥匙当前 Mesh 的大小、资源名或数组索引。

## 十六、编辑器校验和稳定 ID

### 16.1 生成缺失 ID

在对象定义资产 Details 面板执行：

```text
Assign Missing Node Ids
```

该操作只为没有 ID 的新节点生成 ID，不会修改已经存在的 ID。

### 16.2 检查定义

执行：

```text
Check Definition
```

或者使用编辑器的 Validate Assets。系统会检查：

- 节点 ID 是否为空；
- 节点 ID 是否重复；
- 父节点是否存在；
- 父节点链是否成环；
- 能力目标 ID 是否有效；
- Transform、Pivot、Socket 和缩放配置是否有效；
- 碰撞代理尺寸是否为正数；
- 重力方向和重力体积是否有效；
- 能力 ID 是否在同一层级重复；
- Mesh 表现变换是否包含无效数值。

校验失败时不要直接进入关卡调试，先修复 DA 中的错误。

### 16.3 稳定 ID 规则

必须遵守：

- 已经被命令、存档或关卡引用的节点不要重新生成 ID；
- 不要使用数组位置代替节点 ID；
- 不要使用 Mesh 名称代替节点 ID；
- 复制对象定义后，确认复制出的节点 ID 是否需要保持定义级稳定引用；
- 普通复制场景 Actor 时让系统自动生成新的 `AssemblyId`；
- 不要手动复制另一个实例的 `AssemblyId`。

## 十七、交互模式和验收操作

当前开发 HUD 显示的主要快捷键如下：

| 操作 | 按键 |
| --- | --- |
| ThirdPerson / Overview 切换 | `Tab` |
| 选择对象 | 左键；第三人称使用屏幕中央射线 |
| 选择父节点 / 子节点 | `P` / `O` |
| 选择旋转轴 | `X` / `Y` / `Z` |
| 旋转预览 | `R`；Shift+R 反向 |
| 缩放预览 | `-` / `=` |
| 移动预览 | 方向键 |
| 提交 / 取消 | `Enter` / `Esc` |
| 撤销 / 重做 | `U` / `J` |
| 拾取 / 使用 / 开门 | `E` |
| 放下 | `G` |
| 破坏 / 释放 | `B` |
| 保存 / 读取 | `F5` / `F9` |

预览只显示临时状态，不修改真实逻辑状态、碰撞、重力或玩家位置。提交时系统会重新根据能力配置计算结果，并再次检查版本、目标、碰撞和占用者。

## 十八、策划配置时的常见问题

### 点击子部件后为什么操作了父节点？

这是能力继承和 `ConfiguredNodeIds` 的结果。子部件没有本地 Transform 时，会沿父链找到最近的 Transform。如果父节点的 Transform 配置了自己的 `ConfiguredNodeIds`，它会把操作目标改成配置的节点集合。

### 为什么 X/Y/Z 输入没有改变某个部件的旋转轴？

检查实际命中的节点或其最近父节点的 Transform：

- `bAllowInputAxis=false` 时，能力只使用 `DefaultRotationAxis`；
- 子节点或父节点的同 ID Transform 会覆盖 DA 顶部 Transform；
- 当前魔方的 Layer Transform 关闭了输入轴，所以点击方块始终绕 Layer 局部 Z 轴旋转。

### 为什么一个节点有 Mesh 却不能点击？

检查 `bSelectable`、节点是否存在、是否被锁定、是否处于 Destroyed 状态，以及当前模式是否满足能力的 `AllowedModes`。

### 为什么旋转失败？

常见原因：

- 目标节点被锁定或已销毁；
- 命令版本过期；
- 目标姿态与关卡或其他装配体碰撞；
- 玩家会被挤入目标碰撞体；
- 旋转角度、移动距离或缩放比例超过能力限制；
- 占用者策略为 `FailOperation`，而建筑内部存在玩家。

### 为什么缩小后仍然不能拾取？

同时检查：

- 当前逻辑缩放是否不大于 `MaximumScaleToPickup`；
- 玩家到对象根的距离是否不大于 `MaximumDistance`；
- 对象是否已经携带；
- 对象是否包含 Released 部件；
- 玩家是否已经携带另一个对象；
- 拾取能力是否允许 ThirdPerson；
- 能力的 `RequiredStateTags` 是否满足。

### 为什么破坏后 Mesh 没有改变？

检查节点是否填写了 `BrokenMesh`，以及目标状态是否真的进入 `Broken` 或 `Released`。如果只配置了 `bExistsAfterTransition=false`，节点会隐藏并关闭碰撞，不会自动生成新的破坏 Mesh。

## 十九、推荐的配置工作流

新增一个可交互建筑时，建议按以下顺序完成：

1. 先确定对象定义的 `GameplayTags` 和需要保存的状态标签。
2. 建立部件树，先配置 `NodeId`、`ParentNodeId` 和 `DefaultLocalTransform`。
3. 检查父子关系、节点数量和逻辑根是否正确。
4. 配置 Mesh、`MeshTransform`、材质和 Tint。
5. 配置碰撞代理和可选择性。
6. 添加装配体级能力。
7. 对需要独立规则的父节点或子节点添加本地能力。
8. 检查 `CapabilityId`、继承、覆盖和固定目标集合。
9. 配置 Pivot、Socket、参考坐标系和旋转轴。
10. 配置重力体积和占用者策略。
11. 执行 `Assign Missing Node Ids`，再执行 `Check Definition`。
12. 将 DA 放入关卡，运行 PIE 验证第三人称和 Overview 是否共享同一状态。
13. 至少验证一次预览取消、碰撞失败、提交、撤销、重做和存档恢复。

## 二十、角色占用者组件

`UDreamOccupantComponent` 挂在玩家角色或需要参与建筑变换的对象上。它不是建筑定义资产中的节点能力，而是告诉运行时“这个 Actor 是一个需要被事务一起处理的占用者”。

| 字段 | 作用 | 配置建议 |
| --- | --- | --- |
| `OccupantId` | 占用者稳定 ID | 玩家通常自动生成；不要让两个占用者共用一个 ID |
| `bResolveLocalGravity` | 是否根据所在重力体积自动切换角色重力 | 普通玩家开启；不受建筑重力影响的特殊 Actor 可以关闭 |
| `CarryOffset` | 持有对象相对角色 Actor 的额外偏移 | 一般保持零；需要把携带物放在手前方时再调整 |

第一版玩家的角色、相机和占用者组件由 C++ 原生类创建。策划通常只需要在新的可玩角色或特殊占用者上添加这个组件，不需要修改玩家的速度、移动模式或控制旋转字段。事务系统会在建筑变换时自动保存和恢复这些运行时状态。

如果对象需要跟随建筑旋转，使用建筑定义或具体能力的 `FollowAssembly`；如果对象只是建筑附近的普通 Actor，不要把它登记为占用者。

## 二十一、哪些字段不应该手动配置

以下内容会在运行时自动生成或自动维护，策划不需要在资产中修改：

| 内容 | 原因 |
| --- | --- |
| `FDreamAssemblyState` | 当前实例的运行时逻辑状态，由命令和事务修改 |
| `FDreamNodeState` | 节点当前是否存在、是否锁定、当前状态，由能力结果修改 |
| `StateVersion` | 每次成功提交、Undo、Redo 或恢复后递增，用于拒绝旧命令 |
| `CarrierId`、`CarryTransform` | 拾取成功后由 Pickup 能力写入 |
| `FDreamInteractionCommand` | 输入和能力临时生成的命令，不是策划资产配置 |
| `FDreamInteractionTransaction` | 事务前后快照，用于回滚、Undo/Redo 和调试 |
| `PreviewState` | 只在预览期间存在，取消或提交后清理 |
| `AssemblyId` | 场景实例生成时自动分配；复制 Actor 时由编辑器处理 |

策划要改变“初始状态”，应改 `DefaultStateTags`、节点定义和能力配置；不要直接修改 PIE 中显示的运行时状态。

## 二十二、当前版本边界

当前版本已经支持：

- 多节点稳定 ID 和局部父子变换；
- 装配体、父节点和子节点级能力；
- 能力继承、同 ID 覆盖和屏蔽继承；
- Transform、State Transition、Destruction、Pickup、Sequence 和 Key Door；
- 局部重力体积和占用者跟随；
- 预览、取消、碰撞验证、事务提交、回滚、Undo/Redo；
- 稳定 ID 存档和原子恢复；
- 基于定义资产的示例内容生成和自动校验。

以下内容需要程序继续扩展后才能配置：

- 完全自由模拟的 Chaos 刚体建筑；
- 复杂的多阶段动画表现和等待动画完成后再提交；
- 多对象并行事务；
- 联网服务器验证和客户端预测；
- 图形化部件树编辑器；
- 真正的条件分支、并行和异步 Sequence；
- 自动由破坏节点生成新的装配体实例。

策划可以先用现有能力组合出魔方、旋转建筑、钥匙门、机关房间和可拆分部件。出现完全不同的计算模型时，应先和程序确认是否需要新增能力类，避免在一个能力里堆叠互相冲突的规则。
