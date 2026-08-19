# G-SAVE GUI 重构需求（Vortex 风格游戏库）

整理时间：2026-08-17（实现完成 2026-08-18）

本文记录本轮 GUI 重构的确认需求。长期硬约束仍以 `AGENTS.md` 为准；本文只描述界面
结构、交互与为支撑界面所需的 GUI 侧新增能力。

## 0. 本轮边界

- Core（`src/core/**`）冻结，不做任何改动。
- 技术栈保持 Qt 6 Quick/QML，不引入 WebEngine 或网页界面。
- 海报不做本地缓存，搜索与展示时实时 GET Steam CDN。
- 在线清单前期只用 GitHub raw，Gitee 备用镜像延后。
- 历史压缩（squash 旧提交）延后，本轮不实现。
- Repository Engine 只做纯新增（分支列举、在指定提交建分支并切换、切换已有分支），
  不修改 `commit_repository`、`restore_repository`、`push_repository` 等 Core 调用路径，
  Core 运行行为零变化。
- 仓库任何时候都必须停在具名分支上，不允许 GUI 把仓库留在 detached HEAD。

## 1. 顶栏

全局顶栏常驻，与页面无关：

- 左侧：G-SAVE 标识；进入游戏详情页时显示返回按钮与当前游戏名。
- 右侧：Core 状态指示灯 + 文字（运行中 / 已停止），点击直接切换启停。
  运行中点击执行 `stopCore()`，停止时点击执行 `startCore()`；切换期间按钮禁用
  并显示进行中状态，避免重复触发。
- 右侧：设置入口。

顶栏状态灯取代原侧栏底部的状态块。

## 2. 游戏库（首页）

Vortex 风格卡片网格，是应用主页面。

### 2.1 顶部工具行

`搜索框` | `导入支持包 ZIP` | `刷新在线清单`

- 搜索实时过滤两个分区的卡片，按游戏名匹配。
- 支持把 ZIP 拖拽到窗口任意位置完成导入。

### 2.2 分区

**已安装（已配置游戏）**

- 每张卡片背景为 Steam 竖版海报（`library_600x900`），失败时回退渐变底 + 名称。
- 卡片显示：游戏名、保护中/已暂停徽标、存档位置数量。
- 点击进入该游戏的详情页。
- 分区末尾固定一张「+ 添加无支持包游戏」占位卡，点击走通用包手动流程
  （选择 EXE + 存档目录）。

**可安装**

包含三种来源，统一为卡片：

| 来源 | 标记 | 点击行为 |
|---|---|---|
| 在线清单 | 无 | 下载 ZIP → 校验 SHA-256 → 导入 → 自动检测并配置 |
| 本地已导入未配置 | `本地` | 自动检测并配置 |
| 已配置但清单版本更高 | `可更新` | 显示在已安装区，卡片上给出更新入口 |

- 专用包全程不出现路径选择器；检测不完整时汇总 `problems` 报错并停止。
- 在线清单拉取失败不是错误：已安装区照常显示，可安装区显示「无法获取在线清单，
  点击重试」。

## 3. 游戏详情页

顶部为游戏名 + 海报横幅，下方两个标签页。

### 3.1 支持配置

| 分组 | 项 |
|---|---|
| 游戏 | 可执行文件路径、进程名、支持包名与版本、重新检测按钮 |
| 存档 | 全部存档目录列表、include/exclude 规则（只读展示） |
| 提交策略 | 策略（安静期/周期/退出/混合）、安静秒数、最大间隔秒数、退出时提交 |
| 分支 | 当前分支、分支列表、切换分支（多设备/多存档并行时间线） |
| 云端 | 服务地址、Token、连接测试、本游戏各仓库的远端 URL（只读）、推送触发策略 |
| 状态 | 当前分支、是否有未提交变化、领先/落后远端提交数、最后提交时间 |
| 管理 | 暂停/继续保护、移除游戏（保留 `.git` 与存档） |

云端分组明确标注「云端账号设置对全部游戏生效」，与 `AGENTS.md` 的全局云端策略
一致；仓库 URL、remote 名、分支、凭据引用仍不作为玩家输入项。

### 3.2 存档管理

- 顶部：当前选中版本（默认 HEAD）的解析元数据 —— 槽位、角色名、等级、游玩时间等，
  由支持包 `parse()` 返回。
- 中部：历史列表/树，显示提交时间、友好原因、短 ID、分支标记。
- 选中任一节点即在顶部显示该版本的元数据。
- 操作：切换到选中版本、恢复到当前时间线、上传、拉取并整合、时间线分歧处理。
- **切换到旧版本必须同时创建并切换到新分支，二者是同一个原子操作，不提供裸 checkout。**
  裸 checkout 会让仓库进入 detached HEAD，而 `push_repository`、`integrate_repository`
  和 `resolve_divergence` 都会在 detached HEAD 下直接报错 —— 玩家能继续玩、Core 也能继续
  提交，但提交推不上远端、拉不回远端、分歧无法处理，属于必须避免的死状态。
  因此界面上只有「从这个版本开一条新存档线」这一个入口，分支名默认按时间和短 ID 生成，
  允许玩家改名。
- 每条分支就是一套独立存档。多设备、多套存档并行都靠分支承载，切换分支即切换存档。
- 另有「恢复到当前时间线」：沿用现有 `restore_repository()`，先建 `pre-restore-recovery`
  安全提交，再把旧内容作为新提交推进当前分支，历史保持线性。适用于只想回退、
  不想开新存档线的场景。
- 分支切换、创建和恢复都必须先确认游戏已退出且 Core 已停止，完成后再恢复 Core。
- 分歧发生时只允许选择本地或远端整体时间线为主，另一端保留为持久侧分支；
  不提供逐文件合并。

## 4. 设置保存与生效

Core 没有配置热重载，所有配置改动都必须重启 Core 才生效。因此：

- 详情页与设置页的改动先暂存在界面，不立即写 TOML。
- 关闭 GUI 时若存在未保存改动，弹窗询问「保存设置并重启服务？」，
  提供 保存并重启 / 放弃改动 / 取消关闭 三个选项。
- 页面上也保留显式的「保存并重启服务」按钮，不强制用户靠关窗触发。
- 保存流程：写 TOML（临时文件 + 原子替换）→ 重启 Core（原本未运行则保持不运行）。

## 5. 设置页

| 分组 | 项 |
|---|---|
| 启动 | 登录 Windows 后自动启动 Core、Core 启停与重启 |
| 云端账号 | 服务地址、Token、连接测试、删除 Token（对全部游戏生效） |
| 支持包 | 在线清单 URL（可改）、支持包目录、打开目录 |
| 路径 | 配置文件路径、Core 可执行文件路径 |

不提供日志级别、诊断开关或运行状态查询：发布版 Core 无常驻日志与诊断 API。

## 6. 在线支持包清单

地址（前期固定，设置页可改）：

```text
https://raw.githubusercontent.com/AirLongDian/G-SAVE/main/packages/index.json
```

使用 `main` 分支而非 tag，新增支持包不需要发版。

```json
{
  "index_version": 1,
  "updated_at": "2026-08-17",
  "packages": [
    {
      "id": "elden-ring",
      "name": "ELDEN RING",
      "version": "0.1.0",
      "package_api": 1,
      "steam_appid": 1245620,
      "process_name": "eldenring.exe",
      "summary": "10 个槽位、角色名、等级、游玩时间",
      "download": "https://github.com/AirLongDian/G-SAVE-Package-Elden-Ring/releases/download/v0.1.0/elden-ring-0.1.0.zip",
      "sha256": "...",
      "size": 12345
    }
  ]
}
```

约束：

- `sha256` 与 `size` 必须存在；下载后先校验再解压，避免装入被篡改或截断的包。
- `package_api` 用于在下载前过滤不兼容的包。
- 清单不存图片地址；海报由 `steam_appid` 拼出。
- 所有网络调用都是 GUI 侧、短生命周期、用户主动触发；不新增常驻网络状态。

## 7. 海报

```text
竖版 https://cdn.cloudflare.steamstatic.com/steam/apps/<appid>/library_600x900.jpg
横版 https://cdn.cloudflare.steamstatic.com/steam/apps/<appid>/header.jpg
```

由 QML `Image` 直接远程加载，不落盘缓存。加载失败或无 `steam_appid` 时回退到
渐变底 + 游戏名首字母。

四个示例包的 AppID 已通过 Steam `appdetails` 接口和实际图片请求核实：

| 游戏 | AppID | 竖版海报 | 横版海报 |
|---|---|---|---|
| DARK SOULS III | 374320 | 61917 B | 43735 B |
| ELDEN RING | 1245620 | 52021 B | 32689 B |
| DRAGON'S DOGMA: DARK ARISEN | 367500 | 94371 B | 32113 B |
| DRAGON'S DOGMA 2 | 2054970 | 66523 B | 49463 B |

新增支持包若 AppID 不确定，用 `https://store.steampowered.com/api/appdetails?appids=<id>`
联网确认后再写入 `manifest.toml`。

## 8. 支持包清单字段扩展

`manifest.toml` 增加可选字段，便于第三方作者自制包也能有海报：

```toml
[game]
process_name = "eldenring.exe"
steam_appid = 1245620
```

缺省时按无海报处理。现有四个示例包一并补上。

## 9. 产品定位

G-SAVE 是框架，内置的四个支持包只是示例。手动导入 ZIP 必须始终可用，方便玩家
自制与分享支持包；在线清单只是额外来源，不是唯一来源。

## 10. 实现状态

除下列两项外，本文的需求均已实现：

- 历史压缩（squash 旧提交）按用户要求延后。
- Gitee 备用镜像延后，前期只用 GitHub raw。

### 落地位置

| 需求 | 实现 |
|---|---|
| 顶栏状态灯与启停 | `qml/Main.qml`，`GuiController::toggleCore()` / `coreBusy` |
| 卡片网格与搜索 | `qml/pages/LibraryPage.qml`、`qml/components/GameCard.qml` |
| 三来源合并 | `GuiController::library()` |
| 占位卡 | `qml/components/AddGameCard.qml` |
| 详情页两标签 | `GameDetailPage` + `GameSupportTab` / `GameSavesTab` |
| 分支即存档线 | `list_branches` / `suggest_branch_name` / `create_branch_from_commit` / `switch_branch` |
| 设置页 | `qml/pages/SettingsPage.qml` |
| 暂存与关闭确认 | `stageCommitPolicy` / `savePendingChanges` / `Main.qml` 的 `onClosing` |
| 在线清单 | `src/gui/package_index.cpp` + `packages/index.json` |
| 海报 | `steam_poster_url` / `steam_header_url`，QML `Image` 直接远程加载 |
| 手动导入 | 既有 `importPackage()`，另加窗口 ZIP 拖放 |

### 与初稿的两处修正

1. `manifest.toml` 不需要新增 `steam_appid` 字段：四个示例包本来就有
   `[game].steam_app_id`，只需在 `load_package_manifest()` 里解析。
2. 海报域名从 `steamcdn-a.akamaihd.net` 改为 `cdn.cloudflare.steamstatic.com`，
   两者实测都可用，后者更稳定。
