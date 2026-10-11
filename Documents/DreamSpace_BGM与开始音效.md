# BGM 与开始音效

游戏启动地图为 `/Game/DreamPresentation/MainMenu/Maps/L_DreamMainMenu`，确认开始后 Logo 退去，镜头用约 3.8 秒推进天台，再进入 `/Game/0_/Maps/TEST` 接上玩家相机。菜单播放“梦境核心音效”，TEST 播放“午后回声”，两首都循环；鼠标、Enter、空格、触摸和手柄确认共用原有开始入口，第一次确认时播放“请进入游戏”音效。

菜单音乐随天台推进淡出，TEST 音乐在新地图开始运行时淡入。进入音效长约 3.53 秒，由音频设备持有到自然结束；默认镜头推进比短音效更长，因此通常在切图前播放完毕，若调整推进时长，它仍可跨地图继续播放。连续点击不会重新触发音效；停止 PIE 或关闭游戏会主动清理音频组件。

## 音频资源

| 用途 | 原始音频 | 项目资源 | 时长 / 声道 |
| --- | --- | --- | --- |
| 主菜单 BGM | 1 梦境核心音效_胡安霍制作的音效_(1 Dreamcore_爱给网_aigei_com.mp3 | `/Game/Audio/BGM/SW_MenuMusic` | 61.15 秒 / 双声道 |
| TEST BGM | 午后回声  梦核情绪_爱给网_aigei_com.mp3 | `/Game/Audio/BGM/SW_GameplayMusic` | 90 秒 / 单声道 |
| 开始音效 | 请进入游戏(se_enter_game)_爱给网_aigei_com.mp3 | `/Game/Audio/BGM/SW_EnterGame` | 3.53 秒 / 单声道 |

三个来源 MP3 原样保存在 `RawContent/Audio`，`audio_sources.json` 记录原文件名、资源名和 SHA-256。UE 5.8 自带 MP3 导入器生成 SoundWave，保留原始 44100 Hz 采样率和声道数。BGM 资源开启 Looping，使用 Music 分组与 Prime On Load；短音效关闭 Looping，使用 Force Inline。压缩质量为 80。

播放为全局 2D 氛围音，不随相机距离或机关重力旋转衰减。循环衔接以原曲首尾为准，实际听感需要试听。

## 调整音量与地图

`Config/DefaultGame.ini` 中的 `[/Script/DreamSpace.DreamAudioSubsystem]`：

| 配置项 | 默认值 | 用途 |
| --- | --- | --- |
| `MenuMusicVolume` | 0.6 | 菜单音乐线性音量倍率 |
| `GameplayMusicVolume` | 0.5 | 游戏音乐线性音量倍率 |
| `EnterGameVolume` | 1.0 | 确认开始音效线性音量倍率 |
| `MusicFadeInDuration` | 1.0 | 每张地图音乐淡入秒数 |
| `MenuMap` / `GameplayMap` | 两张指定地图的完整资源路径 | 决定哪些地图播放音乐 |
| `MenuMusic` / `GameplayMusic` / `EnterGameSound` | 三个 SoundWave 完整资源路径 | 更换音乐 / 音效 |

修改配置后重新启动编辑器再运行 PIE。菜单音乐淡出时长使用菜单控制器的 `RooftopApproachDuration`（默认 3.8 秒）；`FadeOutDuration`（0.65 秒）只控制 Logo 退场。

如果以后更换资源目录，需要同步更新 Packaging 的 `DirectoriesToAlwaysCook`。当前已经显式烘焙 `/Game/Audio/BGM`，打包运行使用 SoundWave，不依赖外部素材目录或 Python 插件。

## 实现位置

- `Source/DreamSpace/Public/Presentation/DreamAudioSubsystem.h` 和对应 `.cpp`：GameInstance 音频管理器与 World 生命周期桥接器；按照完整地图包路径匹配，兼容 PIE 前缀和直接运行 TEST。
- `DreamMainMenuPlayerController::RequestStartGame`：地图存在性检查通过、设置幂等开始标记后，调用 `BeginMenuTransition`。
- `UDreamMapAudioSubsystem::OnWorldBeginPlay` / `OnWorldEndPlay`：启动、清理当前地图 BGM，独立于角色、控制器和 GameMode。
- `UDreamAudioSubsystem::Deinitialize`：清理跨地图音效和剩余 BGM，覆盖退出 PIE、关闭游戏及中途停止。

## 重新导入

在项目根目录运行：

```powershell
python Source/Audio/prepare_audio.py
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'F:\Ue5 Project\DreamSpace-UI\DreamSpace.uproject' -run=pythonscript `
  '-script=F:/Ue5 Project/DreamSpace-UI/Source/Audio/import_audio.py' `
  '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' `
  -unattended -nop4 -nosplash -nullrhi -nosound -Multiprocess
```

来源目录可通过 `prepare_audio.py --assets <目录>` 指定。导入器创建缺失资源并保留已存在 SoundWave 的手动调整；想替换曲目时，在编辑器对对应 SoundWave 执行重新导入，并核对 Looping 属性。报告写入 `Saved/Audio/import_report.json`。

## 实机检查

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' `
  'F:\Ue5 Project\DreamSpace-UI\DreamSpace.uproject' `
  '-ExecutePythonScript=F:/Ue5 Project/DreamSpace-UI/Source/Audio/verify_audio.py' `
  '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' `
  -unattended -nop4 -NoSplash -Multiprocess -d3d12 `
  -ResX=960 -ResY=540 -AudioMixer '-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0' `
  '-abslog=F:\Ue5 Project\DreamSpace-UI\Saved\Logs\AudioTransitionVerification.log'
```

此检查必须保留声音开启。它使用有窗口的独立编辑器、PIE 和真实混音器，检查菜单 / 游戏 BGM、从曲尾播放后的循环、Slate 留白点击、重复请求、随天台推进淡出、进入音效自然结束、重启 PIE、中途停止以及直接从 TEST 运行。完成后自动退出验收编辑器。

验收期间窗口可能失去焦点，脚本在当前进程临时启用后台音频与第一份 PIE 音频独奏，退出时还原默认对象，不保存编辑器配置。录音还会检查真实 PCM 的能量、曲尾后一秒的声音与峰值，静音或削波会让报告失败。中止场景临时将 `RooftopApproachDuration` 缩短到 0.1 秒，退出时恢复默认 3.8 秒。

录音输出为 `Saved/Audio/MenuLoop.wav`、`MenuToGame.wav`、`GameplayLoop.wav`；状态报告为 `Saved/Audio/verification_report.json`。最终需要在正常游戏窗口试听菜单与游戏音量、进入提示音完整性以及两首原曲的首尾衔接。

## 验证记录

2026-10-11 完成音频资源导入与来源校验：三个 SoundWave 独立进程重新加载通过，时长、声道、循环／非循环和预加载设置符合用途；来源 MP3 副本与原文件 SHA-256 一致。

2026-10-12，UE 5.8.2，Win64 Development，天台过渡完成后的最终验收：

- 编辑器 `DreamSpaceEditor` 和游戏 `DreamSpace` 两个目标编译通过。
- 五项 MainMenu、一项 ProximitySketch、三项 WorldRecovery 自动化测试通过，共 9 项，0 警告、0 错误。报告为 `Saved/MainMenu/TransitionTests/index.json`。
- D3D12 有窗口编辑器中的真实 PIE 音频验收通过，`verification_report.json` 为 `passed=true`、`error=null`；菜单／游戏曲尾后仍有真实声音，Slate 点击与重复确认正确，菜单音乐随天台推进淡出。
- 默认 3.8 秒推进下，从点击到 TEST 实测约 5.45 秒；3.53 秒进入音效在切图前已自然结束。此次默认流程没有实际跨图播放短音效。
- 退出 PIE、重新启动、缩短推进后中途停止、直接从 TEST 运行均通过；旧菜单组件和中止时的设备级音效都完成销毁。
- 三段 48000 Hz 双声道混音录音均有非静音信号，归一化峰值分别约 0.416、0.467、0.089，没有削波。

最终运行日志为 `Saved/Logs/AudioTransitionVerification.log`，状态与录音在 `Saved/Audio`。本轮只同步菜单的天台参数，TEST 音频与地图资源沿用原设置。

本次未制作完整打包产物。音频目录已加入显式烘焙配置；实际扬声器 / 耳机响度、梦核氛围及原曲循环接缝仍需用户试听验收。
