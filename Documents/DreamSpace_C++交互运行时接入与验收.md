# DreamSpace C++ 交互运行时接入与验收

本次按技术方案第 18 节落地首个完整垂直切片：分层魔方、缩放拾取钥匙、钥匙门、旋转重力房间，以及可以预览、确认、取消、撤销、重做和保存的交互事务。目标版本为 UE **5.6**。运行入口、输入、角色移动、相机、对象行为和开发 HUD 均由 C++ 实现。

## 直接运行

打开 `DreamSpace.uproject`，编译 `DreamSpaceEditor / Win64 / Development` 后运行默认地图：

`/Game/DreamInteraction/Maps/InteractionDemo`

项目的默认地图和 GameMode 已配置为新验收场景及 `/Script/DreamSpace.MainGameMode`。角色出生在重力房间内。四个可交互对象都使用 `AInteractiveAssemblyActor`，差异来自 `Content/DreamInteraction/Definitions` 中的定义资产。

| 操作 | 按键 |
| --- | --- |
| 第三人称 / 全局视角切换 | Tab |
| 第三人称行走 / 全局镜头平移 | WASD |
| 第三人称观察 / 全局镜头旋转 | 鼠标 / 按住右键拖动 |
| 跳跃 | Space |
| 全局镜头拉近、拉远 | 滚轮 |
| 选择部件 | 左键；第三人称使用屏幕中央射线 |
| 选择父级 / 第一个子级 | P / O |
| 选择旋转轴 | X / Y / Z；能力可以固定轴并忽略此选择 |
| 旋转预览 | R：+90°；Shift+R：-90° |
| 缩放预览 | `-`：乘 0.8；`=`：乘 1.25 |
| 按装配体坐标移动预览 | 方向键，每次 100 cm |
| 提交 / 取消预览 | Enter / Esc |
| 当前装配体撤销 / 重做 | U / J |
| 第三人称拾取钥匙、开门 | E，先用屏幕中央对准对象 |
| 放下携带的物体 | G |
| 破坏 / 拆分 | B，取决于目标定义的 Destruction 配置 |
| 保存 / 读取世界状态 | F5 / F9 |

预览使用青色线框；真实模型、碰撞和玩家仍保留已提交姿态。切换视角、改变选择或取消时释放会话锁。失败原因显示在 HUD 上。

## 推荐验收顺序

1. **重力房间**：出生后按 Tab，选择房间，按 X、R。预览时玩家不移动。Enter 后房间与玩家一起转动，方向、速度、胶囊朝向和相机随局部重力改变。U 恢复房间和玩家，J 可再次重做。
2. **分层魔方**：选择一个方块，R 使用所属层的 Transform 配置；同一层的九个方块一起旋转。P 可向上选择层，再按 P 选择装配体根。不同层及层内亮度用于辨识旋转位置。
3. **独立部件**：在魔方上选择单个方块并按 B。节点转为 Released，保持世界姿态，随后可以用通用 Transform 独立平移或旋转。移动原装配体根不会再带动已释放部件。
4. **钥匙与门**：全局视角选择钥匙，连续按五次 `-` 后 Enter，尺寸约为原来的 0.32768。切回第三人称，靠近并对准钥匙按 E，随后靠近紫色门按 E。门只检查钥匙的逻辑标签和携带状态。G 放下物体时检查目标净空。
5. **存档与回滚**：F5 保存，操作任意对象后 F9 恢复。预览不会进入存档。恢复会同时检查全部对象与玩家，任一条目失效时整份快照都不应用。

## 源码职责

| 目录 | 职责 |
| --- | --- |
| `Source/DreamSpace/Public/Interaction`、`Private/Interaction` | 稳定 ID、定义资产、状态、层级数学、能力、选择、事务、重力、占用者、存档 |
| `Source/DreamSpace/Public/Gameplay`、`Private/Gameplay` | 原生角色、控制器、GameMode、钥匙门条件 |
| `Source/DreamSpace/Public/Presentation`、`Private/Presentation` | 开发 HUD |
| `Source/DreamSpace/Public/Examples`、`Private/Examples` | 示例定义生成函数；不提供专用魔方或钥匙 Actor |
| `Source/DreamInteractionEditor` | 示例内容生成 Commandlet、真实 PIE 输入验收 |
| `Source/DreamSpace/Tests` | 交互状态和事务行为的自动化测试 |

当前 Runtime 保留在 DreamSpace 模块中，按目录划分依赖；交互层不包含 Gameplay 或 Presentation 头文件。Editor 模块单独编译，Game 目标不依赖 UnrealEd。

## 配置第二个对象

在内容浏览器创建 `InteractiveAssemblyDefinition` 类型的 Data Asset，或者复制现有定义后修改。用同一个装配体 Actor 引用新定义即可。

- `Nodes` 定义部件；`NodeId` 是资产内稳定 GUID。新增部件后使用 `Assign Missing Node Ids`，不要重建已有 ID。
- `ParentNodeId` 指定父节点，无效 GUID 表示挂在装配体根。节点顺序不会影响计算。
- `DefaultLocalTransform` 定义逻辑坐标系，使用均匀缩放。长、宽、高比例放在 `MeshTransform`。
- `Mesh`、`BrokenMesh`、`Material`、`Tint` 控制表现。原型材质读取 Tint 参数，也可以配置自己的材质。
- `CollisionCenter`、`CollisionExtent` 定义局部盒形碰撞代理，单位为 cm，Extent 是半尺寸。Mesh 自身的碰撞不承担核心交互校验。
- `bCollisionEnabled=false` 可创建逻辑父节点或装饰节点；逻辑参考系仍然存在。
- `Sockets` 提供与 Mesh 名称无关的稳定命名锚点。
- `Capabilities` 为内联能力实例。相同 CapabilityId 的子级配置覆盖祖先；`DisabledInheritedCapabilities` 屏蔽继承行为。
- `ConfiguredNodeIds` 固定能力的目标集合，`bTargetAssemblyRoot` 让命中任意部件都操作装配体根。其余情况使用输入提供的选择。
- `RequiredStateTags`、`BlockedStateTags`、`RequiredNodeTags` 描述条件。运行时通过原生 Gameplay Tags 注册基础钥匙/携带/开门标签。
- `Gravity` 是独立的逻辑盒体积，可放在定义根或具体节点上。局部方向随参考系旋转。

内容校验通过 `Check Definition` 和编辑器标准 **Validate Assets** 入口执行。会检查 ID、父链环、未知引用、节点缩放、碰撞范围、重力体积、Pivot、Socket 和能力参数。

场景实例 GUID 在编辑器创建时生成并随地图保存。普通复制产生新实例 GUID；PIE 保留原 GUID。运行时注册发现重复 ID 会拒绝覆盖。

## 通过 C++ 添加交互

能力应继承 `UDreamInteractionCapability`：

- `CanStart` 只读模式、玩家和状态条件。
- `BuildResult` 只修改传入命令的 `ResultState`。禁止直接修改场景组件、玩家位置或全局状态。
- `ValidateConfiguration` 检查该能力的资产参数。

会话调用顺序：

```cpp
FGuid SessionId;
FText Failure;
auto* Runtime = GetWorld()->GetSubsystem<UDreamInteractionWorldSubsystem>();

if (Runtime->BeginInteraction(AssemblyId, NodeId, TEXT("Transform"),
    EDreamInteractionMode::Overview, RequesterId, SessionId, Failure))
{
    FDreamInteractionIntent Intent;
    Intent.RotationDeltaDegrees = 90.0f;

    FDreamInteractionCommand Command;
    if (Runtime->PrepareCommand(SessionId, Intent, Command, Failure)
        && Runtime->PreviewCommand(Command, Failure))
    {
        // 在玩家确认时提交，继续使用生成时的会话和版本。
        Runtime->ExecuteCommand(Command, nullptr, Failure);
    }

    // 取消或错误路径同样应释放会话；已提交会话再次取消是安全的。
    Runtime->CancelInteraction(SessionId);
}
```

输入是相对 Begin 时逻辑状态的累计量。先选择父级和子级的集合时，能力只变换最上层目标，避免重复施加旋转。提交时根据同一能力重算结果，严格比较版本和结果快照；版本 0 没有特殊豁免。回调只能在全部状态和玩家恢复完毕后观察世界。

`UDreamSequenceCapability` 将多个步骤应用到临时状态，再统一提交。前一步可改变标签供后一步检查；任一步失败都会丢弃所有临时结果。第一版采用平坦顺序序列。

## 重力、占用者和碰撞边界

`UDreamOccupantComponent` 将角色登记为事务参与者。第一版单机玩家使用固定 ID，因此重启关卡后仍可恢复相同持有者。有效重力源依次按优先级、体积大小和稳定 ID 决胜；离开全部体积后使用世界向下重力。

默认 FollowAssembly 策略把玩家的旧参考系局部位置映射到新参考系，朝向、控制旋转、速度和重力同步旋转；胶囊尺寸不跟随建筑缩放。KeepWorldTransform 保持提交瞬间的世界姿态，FailOperation 则在体积内有占用者时拒绝操作。

内部物件默认是运动学节点。Released 部件保留原稳定 ID 和子树，将根局部变换转为世界变换，继续由通用能力操作。第一版不自动开启 Chaos 自由刚体模拟。

盒形代理使用 OBB 分离轴检测，允许接触而拒绝穿透；玩家另做胶囊净空检查。提交采用瞬时目标姿态，不播放连续碰撞运动动画，也不承诺扫过的整条旋转路径一定可通行。后续添加缓动表现或自由刚体策略时，应在表现过渡和物理策略层明确补足这些规则。

存档按关卡和对象定义路径校验，不包含跨关卡资产迁移规则。当前快照恢复要求同一批装配体与占用者已经注册。历史最多保留 128 条，存档恢复清空 Undo/Redo 历史，版本继续递增。

## 构建和自动化验收

在 PowerShell 运行：

```powershell
.\Documents\Verify-DreamInteraction.ps1
# 同时验证独立 Game 目标：
.\Documents\Verify-DreamInteraction.ps1 -BuildGame
```

脚本优先从项目的 EngineAssociation 与系统注册表查找安装路径，也可显式传 `-EngineRoot`。当前机器 UE5.6 为 `F:\UE_5.6`。

自动化报告在 `Saved/Automation/FinalInteraction`，日志在 `Saved/Logs/FinalInteractionTests.log`。测试覆盖定义校验、节点顺序、Pivot/Socket、父子选择去重、预览与碰撞隔离、会话锁、版本与篡改拒绝、连续 Undo/Redo、碰撞失败、序列失败、玩家重力跟随、拾取开门放下、独立释放、累计尺寸范围、存档序列化和原子恢复，以及真实 PIE 的键盘输入和相机切换。

开发控制台：

```text
Dream.Dump
Dream.Undo <AssemblyGuid>
stat game
```

示例内容已随项目提交。需要从 C++ 生成器重建时：

```powershell
& 'F:\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'E:\UE5_program\DreamSpace\DreamSpace.uproject' `
  -run=DreamCreateExamples -ReplaceExamples -unattended -nop4 -NullRHI
```

`-ReplaceExamples` 会重建固定的 `Content/DreamInteraction` 示例定义、材质与地图；编辑过示例资产后应先保留自己的副本。不传该参数且地图已存在时，生成器拒绝覆盖。

第一版包含资产 Details 配置、GUID 辅助和自动检查。专用图形化部件树编辑器、任意刚体局部物理、跨装配体嵌套实例引用、联网预测及完整动画/特效编排仍属于后续扩展，不作为此次垂直切片已经实现的功能。

## 本次验证记录

2026-09-10，UE5.6 / Win64 / Development：

- DreamSpaceEditor 编译、链接成功。
- DreamSpace 独立 Game 目标编译、链接成功。
- `DreamSpace` 前缀下 10 项自动化测试全部 Success。
- 有渲染的真实 PIE 输入测试再次通过，已检查原型配色、中文 HUD 和旋转后的场景画面。
- 示例生成器首次创建及显式重建均成功，地图检查为 0 错误、0 警告。

Game 目标的编译结果不等同于已经完成发布包的 Cook/Package 验收。本次交付的试玩入口为 UE5.6 编辑器中的 InteractionDemo。
