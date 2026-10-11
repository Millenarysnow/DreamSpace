# WinsomeGirl 玩家角色

2026-10-11，UE 5.8.2。主游玩地图为 `/Game/0_/Maps/TEST`。

## 当前配置

TEST 的 World Settings 使用 `GM_MainGameMode`，由它生成继承原生 `ADreamCharacter` 的 `BP_DreamCharacter`，控制器为 `PC_DreamPlayerController`。角色外观已换成素材示例使用的 `SK_Chara01`（带外套版本）。

原生默认角色和 TEST 的角色蓝图使用同一模型与动画配置：

| 项目 | 资源 / 设置 |
| --- | --- |
| 项目专用网格 | `/Game/DreamCharacters/WinsomeGirl/SK_DreamWinsomeGirl` |
| 来源网格 | `/Game/WinsomeGirl/Models/SK_Chara01` |
| 动画蓝图 | `/Game/WinsomeGirl/Maps/ThirdPersonExampleMap/Mannequin/Animations/ThirdPerson_AnimBP` |
| 共用骨架 | `/Game/WinsomeGirl/Maps/ThirdPersonExampleMap/Mannequin/Character/Mesh/UE4_Mannequin_Skeleton` |
| 网格相对位置 | `(0, 0, -92.368843)` cm |
| 网格相对旋转 / 缩放 | Yaw `-90°` / `(1, 1, 1)` |
| 玩家碰撞胶囊 | 半径 `42` cm，半高 `96` cm |

网格 Z 从素材示例的胶囊半高 `82.833916`、网格 Z `-79.202759` 迁移而来：`-96 + (82.833916 - 79.202759)`。保留素材示例的脚底间距和模型原始比例。

素材动画与模型本来就共用骨架，直接使用其待机、跑步和跳跃状态机。动画通过 `TryGetPawnOwner`、速度长度及 `IsFalling` 读取当前角色状态。输入继续由现有 Enhanced Input 驱动，移动、跳跃、自定义重力、钥匙、越肩相机和手办交互继续由 DreamSpace 的 C++ 角色、控制器及组件负责。

## 相机材质

项目专用网格的七个槽均引用 `/Game/DreamCharacters/WinsomeGirl/Materials` 中的独立材质副本。衣服与外套实例保留原参数和贴图，并继承专用基础材质；皮肤、脸、头发、透明片分别复制自己的源材质。素材原本为空的 `ClothMat` 槽使用衣服材质补齐。

这些材质都提供现有相机要求的 `DreamOwnerClipAmount` 等参数，由 `DreamShoulderCamera` 为当前玩家创建独立 MID。普通探索时强度为零；靠墙时按真实镜头位置逐渐剔除附近表面。不透明表面使用遮罩，头发保留原遮罩，透明片保留透明混合并在原 Opacity 上乘局部覆盖率。已有主视角标记、镜头身份核对和阴影保留逻辑继续使用。

## 代码和维护工具

- `Source/DreamSpace/Private/Gameplay/DreamCharacter.cpp`：模型、动画类和模型相对位置。
- `Documents/Tools/GenerateWinsomeGirlCharacter.py`：生成八个项目专用资产；只读取源素材，可以重复执行。
- `Documents/Tools/GenerateOwnerClipMaterials.py`：共用局部剔除生成函数，支持不透明、遮罩和透明材质。
- `Documents/Tools/SyncWinsomeGirlPlayer.py`：同步 TEST 使用的角色蓝图外观默认值，逐项检查其它玩法设置。
- `Documents/Tools/CheckWinsomeGirlPlayer.py`：在独立 D3D12 编辑器中启动 TEST，核对玩家模型、动画、七槽 MID、移动与跳跃，并保存主视口截图。

独立命令行生成与同步示例（工程根目录执行）：

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'E:\UE5_program\DreamSpace\DreamSpace.uproject' -run=pythonscript '-script=E:\UE5_program\DreamSpace\Documents\Tools\GenerateWinsomeGirlCharacter.py' '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' -unattended -nop4 -nosplash -nullrhi
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'E:\UE5_program\DreamSpace\DreamSpace.uproject' -run=pythonscript '-script=E:\UE5_program\DreamSpace\Documents\Tools\SyncWinsomeGirlPlayer.py' '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' -unattended -nop4 -nosplash -nullrhi
```

运行和打包使用已生成的 `.uasset`，不需要运行时启用 Python 插件。提交时需要一起包含用户导入的 `Content/WinsomeGirl`、项目专用资产、角色蓝图及本次代码和工具修改。

## 验证证据

本次备份和检查文件位于 `Saved/WinsomeGirl`：`Backup/`、`original-hashes.json`、`generation.json`、`blueprint-sync.json`、`file-verification.json`。编译、逻辑回归和 D3D12 检查分别记录在 `BuildEditor.log`、`BuildGame.log`、`Regression.log`、`Play.log`，真实游玩检查报告为 `play-verification.json`。

2026-10-11 验证结果：

| 检查 | 结果 |
| --- | --- |
| Win64 Development 编译 | `DreamSpaceEditor`、`DreamSpace` 均通过 |
| 现有逻辑测试 | 60 项最终全部通过；汇总为 `regression-verification.json` |
| 逻辑测试首次环境冲突 | 58 项直接通过；2 项仅因并行编辑器写同一资源注册表缓存而被判失败，在独立进程重跑均通过，见 `RegressionIsolated.log` |
| D3D12 材质 | 五个角色基础材质编译错误列表均为空，七槽均成功建立独立 MID |
| TEST 实际游玩 | 正确生成 `BP_DreamCharacter` 与配套动画，跑步速度达到 500 cm/s，起跳和落地状态及骨骼姿势更新正确 |
| 主视口图片 | 四张 1280×720 PNG；已查看角色正面、探索和跳跃画面 |
| 原始素材 | 104 个文件 SHA256 全部与替换前一致 |
| TEST 地图文件 | SHA256 与替换前一致，替换通过角色默认值生效 |

图片为 `Character_TEST.png`、`Exploration_TEST.png`、`Running_TEST.png`、`Jumping_TEST.png`，均位于 `Saved/WinsomeGirl`。本次未执行完整打包；逻辑回归覆盖原有相机、重力与手办组件，实际鼠标操作手感仍以用户在目标关卡中的游玩体验为准。

`CheckWinsomeGirlPlayer.py` 会打开 TEST 并在检查结束后退出专门启动的编辑器，应通过独立 `-ExecutePythonScript` 进程运行。它在运行中临时移动玩家和转动镜头，地图不会保存。
