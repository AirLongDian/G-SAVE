<div align="center">
  <img src="save.png" width="128" height="128" alt="G-SAVE logo">
  <h1>G-SAVE</h1>
  <p>极低空闲占用、基于 Git 的 Windows 游戏存档管理工具<br>Low-idle-overhead, Git-based game save manager for Windows</p>
  <p><a href="#中文">中文</a> · <a href="#english">English</a></p>
</div>

---

## 中文

### 简介

G-SAVE 在游戏原存档目录中建立 Git 历史，并由无窗口、事件驱动的 Core 在游戏运行时监听真实存档变化。GUI 仅在配置、查看历史或恢复版本时启动；游戏未运行时，Core 不打开存档目录、不初始化 libgit2/Lua，也不保留 Git 工作线程。

核心原则是：**不复制影子存档、不要求安装 Git、不让管理工具长期占用游戏资源。**

### 功能

- 游戏进程启动/退出与存档目录变化的事件驱动监听；
- 多个游戏同时运行、后台挂机时分别保护；
- 存档目录原地 Git 仓库，按安静期、周期或游戏退出创建版本；
- 存档时间线、提交元数据、历史恢复和按完整时间线处理分歧；
- Git Remote 云备份，GUI 通过服务地址和 Token 自动识别 GitHub、Gitee、GitLab 或 Gitea；
- Token 存入 Windows 凭据管理器，不写入 TOML 或存档历史；
- 支持包 ZIP 按需导入，路径配置与支持包安装相互独立；
- 无专用包的游戏可使用内置通用支持。

WebDAV 与 OneDrive 同步适配器仍在后续计划中。

### 已有游戏支持包

| 游戏与独立支持包 | 存档根目录层级 | 元数据 |
|---|---|---|
| [DARK SOULS III](https://github.com/AirLongDian/G-SAVE-Package-Dark-Souls-III) · [下载](https://github.com/AirLongDian/G-SAVE-Package-Dark-Souls-III/releases/latest) | `%APPDATA%\DarkSoulsIII` | 多账号、10 个角色槽位、角色名 |
| [DRAGON'S DOGMA: DARK ARISEN](https://github.com/AirLongDian/G-SAVE-Package-Dragons-Dogma-Dark-Arisen) · [下载](https://github.com/AirLongDian/G-SAVE-Package-Dragons-Dogma-Dark-Arisen/releases/latest) | `Steam\userdata\<账号>\367500\remote` | 当前进度、文件信息 |
| [DRAGON'S DOGMA 2](https://github.com/AirLongDian/G-SAVE-Package-Dragons-Dogma-2) · [下载](https://github.com/AirLongDian/G-SAVE-Package-Dragons-Dogma-2/releases/latest) | `Steam\userdata\<账号>\2054970\remote\win64_save` | 最近存档、旅店存档 |
| [ELDEN RING](https://github.com/AirLongDian/G-SAVE-Package-Elden-Ring) · [下载](https://github.com/AirLongDian/G-SAVE-Package-Elden-Ring/releases/latest) | `%APPDATA%\EldenRing` | 原版/无缝联机、10 个槽位、角色名、等级、游玩时间 |

不要选择单个存档文件。完整目录示例见[游戏支持包使用指南](docs/游戏支持包使用指南.md)。

### 快速开始

1. 从 [Releases](https://github.com/AirLongDian/G-SAVE/releases) 下载 Windows ZIP 并完整解压；
2. 运行 `gsave-gui.exe`；
3. 在“游戏支持”中导入发行包内“游戏支持包”目录中的所需 ZIP；
4. 导入后点击游戏卡片，确认或修改游戏 EXE 和存档文件夹；
5. 如需云备份，在“云端备份”填写服务地址与 Token，先测试，再保存。

新配置游戏默认在**退出游戏后上传**。尚未连接云端时仍会正常建立本地版本历史，不会尝试联网。

恢复存档、切换时间线或处理分歧前，请先退出对应游戏，并按 GUI 提示停止 Core。首次公开版本仍建议额外保留一份重要存档副本。

### 架构与资源约束

```text
游戏进程事件 -> 单线程 Core -> IOCP 目录监听 -> 按需临时任务 -> libgit2/Lua/网络
GUI -------------------------------------------------------> 同一 Repository Engine
```

- 空闲 Core 不创建常驻 Worker、线程池、托盘、隐藏窗口、日志服务或 IPC 服务；
- 提交/推送发生时才创建临时线程，结束后立即回收；
- Core 和 GUI 均静态使用 Repository Engine，不分发 `git.exe`；
- 临时任务使用低于正常的 CPU 优先级，但保持正常磁盘 I/O，避免把几十 MiB 的存档提交节流到数十秒。

真实存档 Release 回归（均包含初始基线、两轮变化、两次固定 1 秒安静期）：DDDA 约 2.4 秒、DD2 约 3.2 秒、ELDEN RING 约 4.7 秒。结果仅代表当前测试机器，不作为其他硬件的固定承诺。

### 从源码构建

要求：Windows、CMake 3.24+、支持 C++23 的编译器、Qt 6.8+（Network、Quick、QuickControls2、Widgets）和 Ninja。toml++、libgit2、Lua 与 GoogleTest 由 CMake FetchContent 获取。

```powershell
cmake -S . -B build-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64
cmake --build build-release --target gsave-core gsave-gui -j 1
ctest --test-dir build-release --output-on-failure
```

支持包格式、Lua 沙箱 API 和发布要求见[新游戏支持包制作教程](docs/新游戏支持包制作教程.md)。项目的性能和架构硬约束记录在 [AGENTS.md](AGENTS.md)。

---

## English

### Overview

G-SAVE keeps Git history directly inside each game's real save directory. A windowless, event-driven Core watches the game process and save changes only while needed. The GUI runs only for configuration, history browsing, recovery, and conflict decisions. When no game is running, Core opens no save directory, initializes neither libgit2 nor Lua, and keeps no Git worker thread alive.

The guiding rule is: **no shadow save copy, no external Git installation, and no permanent heavy background runtime.**

### Features

- Event-driven game process and save-directory monitoring;
- Independent protection for multiple games, including background games;
- In-place Git repositories with quiet-period, periodic, and on-exit commits;
- Save timelines, parsed slot metadata, history recovery, and whole-timeline divergence handling;
- Git Remote backup with automatic GitHub, Gitee, GitLab, or Gitea detection from a service address and token;
- Tokens stored in Windows Credential Manager, never in TOML or save history;
- Game support packages imported as ZIP files only when the player needs them;
- Generic support for games without a dedicated parser.

WebDAV and OneDrive adapters are planned for later versions.

### Included support packages

| Game and package repository | Folder to select | Metadata |
|---|---|---|
| [DARK SOULS III](https://github.com/AirLongDian/G-SAVE-Package-Dark-Souls-III) · [Download](https://github.com/AirLongDian/G-SAVE-Package-Dark-Souls-III/releases/latest) | `%APPDATA%\DarkSoulsIII` | Multiple accounts, 10 slots, character names |
| [DRAGON'S DOGMA: DARK ARISEN](https://github.com/AirLongDian/G-SAVE-Package-Dragons-Dogma-Dark-Arisen) · [Download](https://github.com/AirLongDian/G-SAVE-Package-Dragons-Dogma-Dark-Arisen/releases/latest) | `Steam\userdata\<account>\367500\remote` | Current progress and file information |
| [DRAGON'S DOGMA 2](https://github.com/AirLongDian/G-SAVE-Package-Dragons-Dogma-2) · [Download](https://github.com/AirLongDian/G-SAVE-Package-Dragons-Dogma-2/releases/latest) | `Steam\userdata\<account>\2054970\remote\win64_save` | Recent and inn-save slots |
| [ELDEN RING](https://github.com/AirLongDian/G-SAVE-Package-Elden-Ring) · [Download](https://github.com/AirLongDian/G-SAVE-Package-Elden-Ring/releases/latest) | `%APPDATA%\EldenRing` | Original/Seamless Co-op, 10 slots, names, level, playtime |

Select the folder, not an individual save file. See the [support package guide](docs/游戏支持包使用指南.md) for exact directory trees.

### Quick start

1. Download and fully extract the Windows ZIP from [Releases](https://github.com/AirLongDian/G-SAVE/releases);
2. Run `gsave-gui.exe`;
3. Open Game Support and import the desired ZIP from the bundled support-package directory;
4. Click the installed game card and confirm or correct the game EXE and save folder;
5. For cloud backup, enter the service address and token, test the connection, then save.

Newly configured games default to uploading **after the game exits**. Until a cloud service is connected, local history continues normally without network attempts.

Exit the game and follow the GUI prompt to stop Core before restoring a save, switching timelines, or resolving divergence. Keep an additional copy of irreplaceable saves when evaluating the first public release.

### Architecture and performance model

```text
Process events -> single-threaded Core -> shared IOCP watcher -> temporary task -> libgit2/Lua/network
GUI ---------------------------------------------------------------> shared Repository Engine
```

- No resident worker, thread pool, tray, hidden window, logging service, or IPC service;
- Commit and push threads exist only while real work is running;
- Repository Engine is statically linked; G-SAVE does not ship `git.exe`;
- Temporary tasks use below-normal CPU priority while retaining normal disk I/O priority.

Release regression results using real save copies—including a baseline, two changes, and two fixed one-second quiet periods—were approximately 2.4 s for DDDA, 3.2 s for DD2, and 4.7 s for ELDEN RING on the test machine. These measurements are not fixed promises for other hardware.

### Building from source

Requirements: Windows, CMake 3.24+, a C++23 compiler, Qt 6.8+ with Network/Quick/QuickControls2/Widgets, and Ninja. CMake FetchContent downloads toml++, libgit2, Lua, and GoogleTest.

```powershell
cmake -S . -B build-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64
cmake --build build-release --target gsave-core gsave-gui -j 1
ctest --test-dir build-release --output-on-failure
```

See the [package authoring guide](docs/新游戏支持包制作教程.md) for the API 1 manifest, Lua sandbox, tests, and ZIP format. Hard architectural and performance constraints are documented in [AGENTS.md](AGENTS.md).
