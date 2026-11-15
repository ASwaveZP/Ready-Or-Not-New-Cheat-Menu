<a id="top"></a>
# RON Cheat Menu

**BY ASWAVE · Ready or Not · Windows x64 · C++ / WPF**

[简体中文](#zh-cn) | [English](#en)

<a id="zh-cn"></a>
## 简体中文

[Switch to English](#en)

Ready or Not 的原生 C++ 功能模块与 C# WPF 菜单项目。菜单程序集和模型资源嵌入 `OHook.dll`，由原生模块托管 .NET Framework CLR 启动。原生 ImGui 负责游戏内透视与 HUD，WPF 负责设置界面；构建产物为一个 DLL。

[功能](#zh-features) · [构建](#zh-build) · [使用与配置](#zh-use) · [项目结构](#zh-structure) · [限制与排查](#zh-limitations) · [许可证](#zh-license)

<a id="zh-features"></a>
### 功能与实现状态

以下内容根据当前源码整理；存在执行路径不代表已在所有游戏版本和联机模式中验证。

| 模块 | 当前实现 |
| --- | --- |
| 菜单 | 中英文界面、主题、按键绑定、配置预设、地图与连接状态、模型预览 |
| 角色透视 | 玩家、嫌疑人、平民、AI 队友、小队及伤员分类；方框、名字、生命值、骨骼、射线、距离与颜色设置，部分分类支持尸体标记 |
| 证物与武器 | 独立证物、可收取的掉落枪械、已收取证物袋；“显示武器”统一控制武器及其他物品，避免重复枪械标记 |
| 场景显示 | 陷阱与任务目标；门透视、可加固物透视入口已移除 |
| 自瞄 | 目标骨骼、视线检查、范围、平滑与按键设置；实时读取骨骼位置，移除随距离抬高头部的固定偏差 |
| 武器 | 无限弹药、弹匣补充、自定义射速、自动射击模式、一击必杀、准星射击方向及武器效果调整 |
| 玩家 | 无敌、移动速度、玩家穿墙、视野调整 |
| 世界交互 | 嫌疑人行为控制、解除武装、投降/服从、陷阱禁用、证物收集及传送相关操作 |

**实验性子弹穿墙：** 仅在本地玩家拥有权威权限的单人/主机场景生效，仅处理 `bHitScan` 武器。代码拦截 `Multicast_PerformHitscan`，沿实际射击方向单独检测缓存中的嫌疑人模型，将最近的命中提交给点伤害接口；墙体不参与这次检测。已处理的射线事件跳过原生射线分支。它不覆盖实体弹丸，也不会在任意方向自动命中；目标必须已加载并进入缓存，伤害仍经过游戏伤害处理。入口触发、护甲、命中反馈及联机表现仍需游戏内验证。

**尚未接通的菜单选项：** 静默瞄准、瞬间换弹、防闪光、无重量、无硬直、解锁全部物品等开关存在于配置与菜单，但本次源码检查未找到相应的功能执行路径，请勿将其视为已完成能力。

**性能调整：** 缓存事件分类，移除全量事件名称记录；在 HUD 帧检查对象缓存，以 0.5 秒间隔扫描；删除嫌疑人/平民重复扫描；缓存自瞄骨骼名称；穿墙判定复用角色缓存；菜单隐藏时停止轮询，防止连接请求重叠。尚无 FPS 或帧时间对比数据。

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

当前工作区使用 Visual Studio 2026 生成器完成 Release 构建。其他工具链需要自行检查 C++23、SDK 和生成器兼容性。找不到 MSBuild 时，在配置命令中追加 `-DMSBUILD_EXECUTABLE="你的完整路径/MSBuild.exe"`。

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
- **验证状态：** 本地 Windows x64 Release 构建通过；尚未完成覆盖全部功能、地图、距离与联机模式的游戏内回归测试。编译通过不等于效果验证通过。

反馈时请注明游戏版本、单人/主机/客户端身份、开启的功能和复现步骤。性能问题请尽量附相同场景下加载前后的帧时间或 FPS。

<a id="zh-license"></a>
### 许可证与来源

项目自身代码按作者确认的授权采用 [MIT License](LICENSE)，版权署名 ASwave。`Native` 项目级许可证同步为 MIT。`Native/Libraries/` 内第三方代码保留原有版权和许可声明，不因根许可证而改变。

原生模块基于保留在仓库中的 RONFentry 代码基础整合。[Native/README.md](Native/README.md) 是历史上游说明，不代表当前版本功能完成度。第三方依赖包括 Dear ImGui、Microsoft Detours、GLEW、JsonCpp；各自源文件中的声明予以保留。

[返回顶部](#top) · [English](#en)

---

<a id="en"></a>
## English

[切换到简体中文](#zh-cn)

A Windows x64 Ready or Not project combining native C++ features with a C# WPF menu. The menu assembly and model are embedded in `OHook.dll`; the native module hosts the .NET Framework CLR to start WPF. Native ImGui renders in-game ESP/HUD, while WPF provides settings. The build produces one deliverable DLL.

[Features](#en-features) · [Build](#en-build) · [Usage](#en-use) · [Structure](#en-structure) · [Limitations](#en-limitations) · [License](#en-license)

<a id="en-features"></a>
### Features and implementation status

This overview reflects the source. An execution path does not establish compatibility with every game version or network mode.

| Module | Current implementation |
| --- | --- |
| Menu | Chinese/English UI, themes, key bindings, profiles, map/connection status and model preview |
| Character ESP | Player, suspect, civilian, AI teammate, squad and wounded categories; boxes, names, health, skeletons, rays, distance/colors and selected corpse categories |
| Evidence and weapons | Evidence actors, collectible dropped firearms and collected evidence bags; one Show Weapons control covers weapons and other items, avoiding duplicate firearm markers |
| World visuals | Traps and mission objectives; door/securable ESP controls removed |
| Aim | Bone selection, visibility checks, range, smoothing and key activation; live joint positions without the former distance-based upward head offset |
| Weapons | Infinite ammunition, magazine replenishment, custom fire rate, automatic fire mode, increased damage, crosshair shot direction and weapon-effect adjustments |
| Player | God mode, movement speed, noclip and FOV adjustment |
| World actions | Suspect behavior control, disarming, surrender/compliance, trap disabling, evidence collection and teleport actions |

**Experimental wall hits:** restricted to the local player's authoritative single-player/host context and `bHitScan` weapons. The `Multicast_PerformHitscan` interception tests cached suspect meshes along the firing direction and submits the nearest hit through point damage. World geometry is excluded from that query. A handled event skips its original hitscan branch. This does not implement physical projectile penetration or arbitrary-direction hits. Targets must be loaded and cached; game damage processing still applies. Hook coverage, armor, hit feedback and network behavior require in-game validation.

**Exposed but not wired up:** silent aim, instant reload, no flash, no weight, no stagger and unlock-all-items have menu/configuration entries, but no corresponding feature execution paths were found in this source review. They should not be treated as completed features.

**Performance work:** cached event classification, removal of full event-name recording, HUD-frame cache checks with a 0.5-second scan interval, removal of duplicate suspect/civilian scans, cached aim bone names, actor-cache reuse for wall hits, and suspended/serialized menu polling. No measured FPS or frame-time gain is claimed.

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

The local Release build used Visual Studio 2026. Other toolchains require their own C++23, SDK and generator checks. If MSBuild is not found, append `-DMSBUILD_EXECUTABLE="your/full/path/MSBuild.exe"` to the configure command.

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
- **Validation:** the local Windows x64 Release build passed. Full in-game regression across features, maps, distances and network modes has not been completed. Build success is not gameplay validation.

For issue reports, include the game version, single-player/host/client role, enabled features and reproduction steps. For performance reports, include frame times or FPS from the same scene before and after loading the menu where possible.

<a id="en-license"></a>
### License and provenance

The project's own code uses the [MIT License](LICENSE), attributed to ASwave, under the author's stated authorization. The project-level license in `Native/` is also MIT. Third-party code under `Native/Libraries/` retains its existing notices and terms.

The native integration builds on the RONFentry code base retained here. [Native/README.md](Native/README.md) is historical upstream documentation, not a statement of current feature completeness. Dependencies include Dear ImGui, Microsoft Detours, GLEW and JsonCpp; their source notices are preserved.

[Back to top](#top) · [简体中文](#zh-cn)
