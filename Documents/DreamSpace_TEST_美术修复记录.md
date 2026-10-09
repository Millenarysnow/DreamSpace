# TEST 地图美术修复记录

完成日期：2026-10-10。目标工程为 `F:\Ue5 Project\DreamSpace-art_fix`，主地图为 `/Game/0_/Maps/TEST`。参考美术工程为 `F:\Ue5 Project\DreamSpace-master`。

修复在现有策划地图和蓝图上完成。通过 UE 的资源加载、组件属性编辑和保存接口补回美术引用，保留策划调整后的布局、组件变换、碰撞、父类和蓝图逻辑。

## 原因

- 部分资源在 `Content/Fab`，而已保存的引用要求 `Content/0_/Fab`；同时缺少贴花、室内道具、门窗等依赖。
- 蓝图和地图中有些网格、贴花材质、材质覆盖、子 Actor 类引用已经保存为空。因此只补资源文件不足以恢复画面。
- `.gitignore` 以 `*` 开头，但没有放行根目录本身，导致新增 Content 等文件被忽略，容易漏提交。具体历史上传过程无法仅凭当前文件确定。

## 实际修复

| 项目 | 结果 |
| --- | --- |
| 缺失依赖 | 从参考工程按原包路径补入 203 个文件，约 702 MiB；逐一核对 SHA256 |
| 蓝图引用 | 在 15 个蓝图中恢复 241 处引用：58 处网格、152 处贴花材质、26 处材质覆盖、5 处子 Actor 类 |
| 地图引用 | 恢复 TEST 内 7 处网格引用 |
| 重名贴花 | Rom 重设父类后引入同名继承网格，原贴花 `w` 被 UE 改名为 `w_0`；按组件类型匹配恢复材质 |
| 透明材质 | Door_01 的门框、Upstair 的楼梯组件设置 `Disallow Nanite`，解决透明材质与 Nanite 的不兼容 |
| Git 忽略规则 | 补齐 Config、Content、Source、Documents、RawContent 根目录例外，排除 Python 缓存 |

现有资源文件没有被参考工程整包替换。共重新保存 17 个已有蓝图和 TEST 地图；新增资源来自缺失依赖清单。参考工程只用于读取。

恢复子 Actor 类后，地图 Actor 数由 43 变为 48。这 5 个实例由原有 ChildActorComponent 自动生成，原有 Actor 上没有新增组件。

## 验证结果

| 检查 | 结果 |
| --- | --- |
| TEST 可追踪的缺失包 | 11 -> 0 |
| 原有 Actor | 43 个全部保留，名称、类、GUID、变换、附着关系等对照通过 |
| 原有实例组件 | 503 个，变换、附着关系、移动性、碰撞通道/响应、BodyInstance 等对照通过 |
| 蓝图组件 | 404 个组件记录对照通过 |
| 原蓝图父类 | 35 个全部保持一致 |
| 蓝图源图表 | 无差异；18 处差异来自带 `bIsIntermediateNode=True` 的编译器临时图表，不属于源图表 |
| 原有静态网格 | 56 个资源的文件哈希全部不变，保留现有几何和网格内碰撞 |
| 补入资源 | 203 个文件哈希全部一致 |
| 原始备份 | 644 个文件哈希全部与修复前清单一致 |
| 其他原文件 | 无清单以外的修改，Source、Config、uproject 保持原样 |
| 可比美术引用 | 无未解决差异，以下材质槽差异单独保留 |
| GPU 验证 | UE 5.8.2 / D3D12 编译 65 个基础材质，无编译错误；三张截图已人工检查，进程退出码 0 |

三处碰撞 BodyInstance 的最大角速度在 UE 保存时从 `3600` 变为 `3599.999756`。验证仅对此字段按 `0.001` 度/秒精度比较，其他受保护数据仍按原值比较。

## 保留差异和验证边界

- `Barrier_09` 当前网格有 4 个材质槽，美术参考有 5 个。共同的槽位及原有第 5 个材质覆盖已恢复正确；保留当前网格文件，避免覆盖策划修改过的模型和碰撞。因此不保证这一网格与美术参考的表面分区完全相同。
- 日志仍有洗衣机子蓝图为 Static、Rom 父级可移动的 AttachTo 警告。按保留策划设置的要求保留移动性；房间实际移动时，洗衣机是否正确跟随需要在玩法中复核。
- 本次验证涵盖资源引用、受保护属性、蓝图源图表和编辑器渲染；没有完成整套玩法回归或打包验证。其他地图不在本次修复范围内。

## 备份和证据

- 原始备份：`F:\Ue5 Project\DreamSpace-art_fix\Saved\ArtRepair\Backup-original`，包含修复前的 Content、Config、Source、uproject 和 .gitignore。
- 原始哈希清单：`Saved/ArtRepair/original-file-hashes.json`。
- 最终验证：`Saved/ArtRepair/verification.json`、`after-final.json` 和 `after-final-exports/`。
- 改动明细：`Saved/ArtRepair/repair-blueprints.json`、`repair-map.json`、`repair-rendering.json`。
- GPU 结果：`Saved/ArtRepair/render-verification.json`、`render-final.log`。
- 截图：`Saved/ArtRepair/Render-final/Overview.png`、`Exterior.png`、`Interior.png`。

审计、修复和验证工具位于 `Documents/Tools/`。保留上述 Saved 数据后，可用 `C:\Python314\python.exe Documents\Tools\VerifyArtRepair.py` 再次核对现有结果。缺失依赖复制已执行，原始备份已存在，无需再次执行复制工具的 `-Apply`。

当前修改未暂存、提交或推送 Git。后续提交应一起包含 `.gitignore`、修改后的地图/蓝图和新增依赖资源。备份及详细审计数据位于忽略的 Saved 目录，需要单独保留。
