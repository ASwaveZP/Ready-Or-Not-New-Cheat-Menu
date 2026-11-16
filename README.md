<a id="top"></a>
# RON Cheat Menu

**Ready or Not · Windows x64 · C++ / WPF**

[简体中文](#zh-cn) | [English](#en)

<a id="zh-cn"></a>
## 简体中文

[Switch to English](#en)

这是我为 Ready or Not 整理和改进的功能菜单。我把设置界面放到了 WPF 中，游戏内的透视和 HUD 仍由原生模块绘制。菜单、模型和功能模块打包在一个 `OHook.dll` 里，支持中英文切换、主题和按键设置。

[功能](#zh-features) · [构建](#zh-build) · [使用与配置](#zh-use) · [项目结构](#zh-structure) · [限制与排查](#zh-limitations) · [许可证](#zh-license)

<a id="zh-features"></a>
### 功能

| 模块 | 功能 |
| --- | --- |
| 菜单 | 中英文界面、主题、按键绑定、配置预设、地图与连接状态、模型预览 |
| 角色透视 | 玩家、嫌疑人、平民、AI 队友、小队及伤员分类；方框、名字、生命值、骨骼、射线、距离与颜色设置，部分分类支持尸体标记 |
| 证物与武器 | 独立证物和已收取证物袋分别控制；“显示武器”只显示掉落后可收取的证物枪械；“手持物品”单独显示 AI 和玩家当前手持的物品 |
| 场景显示 | 陷阱与任务目标 |
| 自瞄 | 目标骨骼、视线检查、范围、平滑与按键设置；随距离和姿态变化跟踪所选骨骼部位 |
| 武器 | 无限弹药、弹匣补充、自定义射速、强制连发、准星自动射击、一击必杀、准星射击方向及武器效果调整 |
| 玩家 | 无敌、移动速度、玩家穿墙、视野调整 |
| 世界交互 | 嫌疑人行为控制、解除武装、投降/服从、陷阱禁用、证物收集及传送相关操作 |

**子弹穿墙仍在调试。** 目前只支持单人或主机，以及使用射线判定的武器。开启后沿射击方向直接检测嫌疑人，忽略中间墙体；需要瞄准目标，目标也必须已经加载。实体弹丸暂不支持，护甲、命中反馈和联机效果还需要继续测试。

**尚未完成：** 静默瞄准、瞬间换弹、防闪光、无重量、无硬直、解锁全部物品。这些选项暂时保留在菜单中，功能还没有接通。

**强制连发与自动射击：** 强制连发把武器切到全自动模式，需要按住开火键。自动射击是独立开关，准星对准未投降且仍有行动能力的嫌疑人时自动扣动扳机，移开准星后停止；不会穿过遮挡物寻找目标，打开菜单、切出游戏或换弹时暂停。两项可以同时开启。

针对游戏卡顿，我减少了事件记录和重复对象扫描，给骨骼查找和命中检测加了缓存，菜单隐藏后也会停止连接轮询。具体效果还需要在不同场景下测试，暂时没有帧率对比数据。

<a id="zh-build"></a>
### 构建

需要 Windows x64、Visual Studio C++ 工具链与 Windows SDK、MSBuild、支持所选生成器的 CMake，以及 .NET Framework 4.8 开发/目标包。C++ 工程设置为 C++23，菜单目标为 `net48`。

源码包含 `Native/ReadyornotSDK/`、本地依赖和 `Model.glb`。模型约 76 MiB，是必需的嵌入资源。构建缓存、个人配置和生成的 DLL 不纳入源码版本管理。

在 **Visual Studio x64 开发者命令提示符** 中执行：

```bat
git clone https://github.com/2786256218/Ready-Or-Not-New-Cheat-Menu.git
cd Ready-Or-Not-New-Cheat-Menu
cmake -S Native -B Native/build-vs -G "Visual Studio 18 2026" -A x64
cmake --build Native/build-vs --config Release --target OHook -- /m
```

我使用 Visual Studio 2026 构建，Release 已通过编译。使用其他工具链时，请确认 C++23、SDK 和 CMake 生成器兼容。找不到 MSBuild 时，在配置命令中追加 `-DMSBUILD_EXECUTABLE="你的完整路径/MSBuild.exe"`。

也可使用根目录 `build.bat`，但其 Visual Studio 路径固定为 `E:\Data\VS\Common7\Tools\VsDevCmd.bat`，需按实际安装位置调整。该脚本还会清理生成的 EXE、PDB 和部分中间产物；手动 CMake 构建不执行这些额外清理。`Native/` 中旧脚本保留作历史参考，推荐使用上述流程。

构建链：`Menu/RonMenu.csproj` → `RONMenu.UI.dll` → 原生 RCDATA 资源 → **`dist/OHook.dll`**。WPF 菜单不是独立 EXE。

<a id="zh-use"></a>
### 使用与配置

1. 构建匹配当前游戏版本的 x64 DLL。仓库不附带独立注入器，模块需通过兼容的 DLL 加载方式进入游戏进程。
2. 初始化后菜单默认隐藏，按 `Insert` 切换显示；菜单快捷键可修改。
3. 检查连接状态与地图后设置功能。权威端功能需在单人或主机场景检查。
4. 替换 DLL 后重启游戏，避免继续使用已加载的旧模块。

| 配置 | 位置 / 说明 |
| --- | --- |
| 原生功能 | `C:\ProgramData\OriginReadyornot\config\overlay_config.json` |
| 菜单偏好 | `%LOCALAPPDATA%\RON New Cheat Menu\menu-config.json` |
| 界面语言 | `%LOCALAPPDATA%\RON New Cheat Menu\ui-language.txt` |
| 配置预设 | `%LOCALAPPDATA%\RON New Cheat Menu\Profiles\` |
| 本地通信 | Windows 命名管道 `\\.\pipe\RONFentry.Menu.v1`，详见 [IPC.md](IPC.md) |

<a id="zh-structure"></a>
### 项目结构

```text
Menu/                       WPF 界面、设置、主题、模型预览及管道客户端
Native/OHook/Core/          事件拦截、配置、CLR 托管、管道及输入管理
Native/OHook/Features/      自瞄、武器、玩家及世界功能
Native/OHook/Handlers/      场景对象缓存
Native/OHook/Overlays/      游戏内透视与 HUD
Native/ReadyornotSDK/       当前代码使用的生成式游戏 SDK
Native/Libraries/           本地第三方依赖
Model.glb                   菜单模型资源
IPC.md                      菜单与原生模块通信协议
build.bat                   Windows 构建入口
dist/OHook.dll              构建输出，不提交到 Git
```

<a id="zh-limitations"></a>
### 已知限制与排查

- **游戏更新后异常：** SDK、对象布局和事件接口与游戏版本相关，不保证兼容所有更新。
- **菜单无连接：** 检查原生模块初始化、.NET Framework/WPF 运行环境，以及是否仍加载旧 DLL。
- **首次构建较慢：** SDK 源文件多、模型较大；保留构建目录可利用增量构建。
- **新目标未立即参与判定：** 对象缓存每 0.5 秒刷新；自瞄在执行时重新读取骨骼坐标。
- **自瞄未选中嫌疑人：** 当前筛选还受嫌疑人透视开关、AI 最大显示距离、范围和视线检查影响。
- **选项存在但无效果：** 检查上述未接通选项及权威端限制。
- **测试进度：** Windows x64 Release 编译通过，游戏内测试还没有覆盖全部功能和地图。遇到异常可以提交 Issue。

反馈时请注明游戏版本、单人/主机/客户端身份、开启的功能和复现步骤。性能问题请尽量附相同场景下加载前后的帧时间或 FPS。

<a id="zh-license"></a>
### 许可证与依赖

项目采用 [MIT License](LICENSE)。第三方依赖沿用各自的许可证，具体声明见 `Native/Libraries/`。

原生模块基于 RONFentry 整合，使用了 Dear ImGui、Microsoft Detours、GLEW 和 JsonCpp。旧项目说明保存在 [Native/README.md](Native/README.md)，当前功能以本页为准。

[返回顶部](#top) · [English](#en)

---

<a id="en"></a>
## English

[切换到简体中文](#zh-cn)

This is my reworked feature menu for Ready or Not. I moved the settings interface to WPF while keeping ESP and HUD rendering in the native module. The menu, model and native features are bundled into a single `OHook.dll`, with Chinese/English language support, themes and configurable key bindings.

[Features](#en-features) · [Build](#en-build) · [Usage](#en-use) · [Structure](#en-structure) · [Limitations](#en-limitations) · [License](#en-license)

<a id="en-features"></a>
### Features

| Module | Features |
| --- | --- |
| Menu | Chinese/English UI, themes, key bindings, profiles, map/connection status and model preview |
| Character ESP | Player, suspect, civilian, AI teammate, squad and wounded categories; boxes, names, health, skeletons, rays, distance/colors and selected corpse categories |
| Evidence and weapons | Separate controls for evidence and collected bags; Show Weapons displays only dropped evidence firearms, while Held Items displays the active hand item of AI characters and players |
| World visuals | Traps and mission objectives |
| Aim | Bone selection, visibility checks, range, smoothing and key activation; tracking of the selected joint as distance and pose change |
| Weapons | Infinite ammunition, magazine replenishment, custom fire rate, forced full auto, crosshair-triggered shooting, increased damage, crosshair shot direction and weapon-effect adjustments |
| Player | God mode, movement speed, noclip and FOV adjustment |
| World actions | Suspect behavior control, disarming, surrender/compliance, trap disabling, evidence collection and teleport actions |

**Wall hits are still experimental.** They currently support single-player or host sessions and hitscan weapons. The feature checks suspects along the firing direction while ignoring intervening walls. You still need to aim at a loaded target. Physical projectiles are not supported, and armor, hit feedback and network behavior need further testing.

**Not finished yet:** silent aim, instant reload, no flash, no weight, no stagger and unlock-all-items. These controls remain in the menu, but their functionality is not connected yet.

**Force Full Auto and Auto Shoot:** Force Full Auto selects automatic fire mode and still requires holding the fire button. Auto Shoot is a separate switch that pulls the trigger when the crosshair hits an active, non-surrendered suspect and releases it when the target is lost. It does not acquire targets through cover and pauses during reloads, while the menu is open, or when the game loses focus. Both options can be enabled together.

To address stuttering, I reduced event logging and duplicate actor scans, cached bone lookups and reused actor data for hit detection. Connection polling stops when the menu is hidden. Performance still needs testing across different scenes; I do not have frame-rate comparisons yet.

<a id="en-build"></a>
### Build

Requirements: Windows x64, Visual Studio C++ tools and Windows SDK, MSBuild, CMake supporting the chosen generator, and the .NET Framework 4.8 developer/targeting pack. Native code requests C++23; the menu targets `net48`.

The repository includes `Native/ReadyornotSDK/`, local dependencies and `Model.glb`. The model is approximately 76 MiB and is required at build time. Generated build trees, personal settings and DLLs are excluded from source control.

Run in a **Visual Studio x64 Developer Command Prompt**:

```bat
git clone https://github.com/2786256218/Ready-Or-Not-New-Cheat-Menu.git
cd Ready-Or-Not-New-Cheat-Menu
cmake -S Native -B Native/build-vs -G "Visual Studio 18 2026" -A x64
cmake --build Native/build-vs --config Release --target OHook -- /m
```

I build with Visual Studio 2026, and the Release build compiles successfully. If you use another toolchain, check its C++23, SDK and CMake generator compatibility. If MSBuild is not found, append `-DMSBUILD_EXECUTABLE="your/full/path/MSBuild.exe"` to the configure command.

The root `build.bat` is an alternative, but its Visual Studio path is fixed to `E:\Data\VS\Common7\Tools\VsDevCmd.bat`; adapt it to your installation. It also removes generated EXE/PDB files and selected intermediates. Manual CMake commands do not perform that extra cleanup. Legacy scripts under `Native/` are retained for reference; use the root workflow above.

Build flow: `Menu/RonMenu.csproj` → `RONMenu.UI.dll` → native RCDATA resource → **`dist/OHook.dll`**. The WPF menu is not a standalone executable.

<a id="en-use"></a>
### Usage and settings

1. Build an x64 DLL compatible with your game version. No standalone injector is bundled; load the module into the game process using a compatible DLL-loading mechanism.
2. The menu starts hidden. Press `Insert` to toggle it; the hotkey is configurable.
3. Check connection/map status before enabling features. Validate authority-dependent functions in single-player or as host.
4. Restart the game after replacing the DLL to avoid reusing the old loaded module.

| Setting | Location / details |
| --- | --- |
| Native configuration | `C:\ProgramData\OriginReadyornot\config\overlay_config.json` |
| Menu preferences | `%LOCALAPPDATA%\RON New Cheat Menu\menu-config.json` |
| UI language | `%LOCALAPPDATA%\RON New Cheat Menu\ui-language.txt` |
| Profiles | `%LOCALAPPDATA%\RON New Cheat Menu\Profiles\` |
| Local IPC | Windows named pipe `\\.\pipe\RONFentry.Menu.v1`; see [IPC.md](IPC.md) |

<a id="en-structure"></a>
### Structure

| Path | Responsibility |
| --- | --- |
| `Menu/` | WPF UI, settings, themes, model preview and pipe client |
| `Native/OHook/Core/` | Event interception, configuration, CLR hosting, IPC and input ownership |
| `Native/OHook/Features/` | Aim, weapon, player and world features |
| `Native/OHook/Handlers/` | Actor caching |
| `Native/OHook/Overlays/` | In-game ESP/HUD rendering |
| `Native/ReadyornotSDK/` | Generated game SDK |
| `Native/Libraries/` | Bundled third-party dependencies |
| `Model.glb` | Embedded menu model |
| `IPC.md` | Menu/native protocol |
| `build.bat` | Windows build entry point |
| `dist/OHook.dll` | Build output, excluded from Git |

<a id="en-limitations"></a>
### Limitations and troubleshooting

- **Problems after updates:** SDK layouts and event interfaces are game-version-dependent; universal compatibility is not promised.
- **Disconnected menu:** check native initialization, .NET Framework/WPF availability and whether an old DLL remains loaded.
- **Slow first build:** the SDK contains many sources and the model is large. Keep the build directory for incremental builds.
- **New targets not considered immediately:** discovery refreshes every 0.5 seconds; aiming refreshes joint positions at execution time.
- **Aim does not select a suspect:** selection also depends on suspect ESP, maximum AI display distance, targeting range and visibility checks.
- **Visible settings have no effect:** check the unimplemented entries and authority restrictions above.
- **Testing:** Windows x64 Release compiles successfully. In-game testing has not covered every feature and map yet. Please open an issue if you run into problems.

For issue reports, include the game version, single-player/host/client role, enabled features and reproduction steps. For performance reports, include frame times or FPS from the same scene before and after loading the menu where possible.

<a id="en-license"></a>
### License and dependencies

The project uses the [MIT License](LICENSE). Third-party dependencies keep their own licenses; see the notices under `Native/Libraries/`.

The native module builds on RONFentry and uses Dear ImGui, Microsoft Detours, GLEW and JsonCpp. The original project notes are kept in [Native/README.md](Native/README.md); this page describes the current menu.

[Back to top](#top) · [简体中文](#zh-cn)
