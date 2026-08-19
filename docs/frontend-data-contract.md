# G-SAVE 前端数据契约

本文由 `tests/gui_contract_dump.cpp` 从真实运行的后端导出，不是手写推测。样例数据来自
三个带真实 Git 历史的仓库。前端 mock 请直接照这些形状写，接真实数据时无需改结构。

导出方式：

```
build-qt\gsave_gui_library_fixture.exe <临时目录>
build-qt\gsave_gui_contract_dump.exe <临时目录>\config.toml <临时目录>\contract.json
```

## 0. 前端技术约束

- 纯静态 SPA，无后端服务器、无 Supabase、无边缘函数。所有数据来自本地 C++。
- 不要在前端做路径拼接、Git 操作或文件读写；这些只能通过下面的调用交给 C++。
- 海报直接用 `poster` / `banner` 字段里的完整 URL，不要自己拼 AppID。
  这两个字段可能是空字符串，此时回退渐变底 + 游戏名首字母。
- 所有路径字符串是 Windows 反斜杠路径，仅用于显示，不要在前端解析。

## 1. 通信方式

C++ 宿主是 WebView2。已实测在 MinGW 下可编译运行，JS 到 C++ 的消息通路验证通过。

前端发起调用：

```js
window.chrome.webview.postMessage({
  id: 42,              // 前端自增，用于匹配回复
  method: "openCommitAsBranch",
  params: { gameIndex: 0, saveIndex: 0, commitId: "abc...", branch: "save/..." }
})
```

前端接收回复与推送：

```js
window.chrome.webview.addEventListener("message", event => {
  const message = event.data
  // 回复：{ id: 42, ok: true, result: ... } 或 { id: 42, ok: false, error: "..." }
  // 推送：{ event: "gamesChanged" } 等，见第 8 节
})
```

开发阶段没有 WebView2 时用 mock：

```js
const bridge = window.chrome?.webview
  ? realBridge()
  : mockBridge(contractJson)   // 直接喂本文的样例数据
```

## 2. library：游戏库卡片

一次调用拿到整个游戏库。`kind` 区分两个分区，`source` 区分可安装卡片的来源。

- `kind: "installed"` 已配置的游戏，点击进详情页，用 `gameIndex` 定位。
- `kind: "available"` 未配置的支持包，点击开始安装。
  - `source: "local"` 已导入本地，调 `installPackage(packageIndex)`
  - `source: "online"` 只在在线清单里，调 `installFromIndex(id)`
- `updateVersion` 非空表示在线清单版本比已装的高，卡片上显示「可更新」。
- 已安装分区末尾要额外渲染一张「+ 添加无支持包游戏」占位卡，点击调
  `installGenericPackage()`。这是唯一会让玩家手动选路径的入口。

已安装卡片：

```json
{
  "banner": "https://cdn.cloudflare.steamstatic.com/steam/apps/1245620/header.jpg",
  "enabled": true,
  "gameIndex": 0,
  "id": "elden-ring",
  "kind": "installed",
  "name": "ELDEN RING",
  "packageIndex": 2,
  "poster": "https://cdn.cloudflare.steamstatic.com/steam/apps/1245620/library_600x900.jpg",
  "process": "eldenring.exe",
  "saveCount": 1,
  "savePath": "D:\\Games\\SaveDemo\\elden-ring\\save",
  "updateVersion": "",
  "version": "0.1.0"
}
```

可安装卡片（本地已导入）：

```json
{
  "banner": "https://cdn.cloudflare.steamstatic.com/steam/apps/367500/header.jpg",
  "id": "dragons-dogma-dark-arisen",
  "kind": "available",
  "name": "DRAGON'S DOGMA: DARK ARISEN",
  "packageIndex": 1,
  "poster": "https://cdn.cloudflare.steamstatic.com/steam/apps/367500/library_600x900.jpg",
  "process": "DDDA.exe",
  "source": "local",
  "summary": "",
  "version": "0.1.0"
}
```

在线清单卡片形状相同，区别是 `source: "online"`、`packageIndex: -1`、`summary` 有内容。

搜索在前端做，匹配 `name`、`id`、`process` 三个字段即可。

## 3. gameDetail：游戏详情

`gameDetail(gameIndex)`。

```json
{
  "banner": "https://cdn.cloudflare.steamstatic.com/steam/apps/1245620/header.jpg",
  "commit": {
    "commitOnExit": true,
    "maxIntervalSeconds": 300,
    "pending": false,
    "quietSeconds": 5,
    "strategy": 3
  },
  "enabled": true,
  "generic": false,
  "id": "elden-ring",
  "index": 0,
  "name": "ELDEN RING",
  "packageIndex": 2,
  "packageName": "ELDEN RING",
  "packageVersion": "0.1.0",
  "parser": "D:\\G-SAVE\\packages\\elden-ring\\adapter.lua",
  "poster": "https://cdn.cloudflare.steamstatic.com/steam/apps/1245620/library_600x900.jpg",
  "process": "eldenring.exe",
  "processPath": "D:\\Games\\SaveDemo\\elden-ring\\eldenring.exe",
  "saves": [
    {
      "excludeGlobs": [
        ".git/**"
      ],
      "includeGlobs": [
        "*.sav"
      ],
      "index": 0,
      "path": "D:\\Games\\SaveDemo\\elden-ring\\save"
    }
  ],
  "sync": {
    "credentialStored": false,
    "interval": 300,
    "remote": "origin",
    "trigger": 1
  }
}
```

要点：

- `generic: false` 时**不要**渲染任何路径选择器。专用支持包自己检测路径，手动指定会
  让 G-SAVE 版本化错误的目录。只提供「重新检测路径」按钮，调
  `installPackage(packageIndex)`。
- `generic: true` 才是通用支持，允许手动重选。
- `saves[].includeGlobs` / `excludeGlobs` 只读展示。
- `commit.strategy` 是枚举下标：`0` 安静期、`1` 周期、`2` 退出时、`3` 组合。
- `commit.pending: true` 表示这个游戏有暂存未保存的提交策略改动。
- `sync.trigger` 是枚举下标：`0` 每次提交后、`1` 退出游戏后、`2` 周期、`3` 仅手动。

## 4. repositoryState：仓库状态

`repositoryState(gameIndex, saveIndex)`。

```json
{
  "ahead": 0,
  "behind": 0,
  "branch": "main",
  "dirty": false,
  "lastCommit": "0414b8b17a5db5812d5714c5e5b60fb037030bbd",
  "lastCommitAt": "2026-08-18  12:25:39",
  "lastReason": "退出游戏时保存",
  "remoteUrl": ""
}
```

`error` 字段存在时其余字段都没有，直接显示错误文本。
`remoteUrl` 为空串表示未配置云端，此时 `ahead` / `behind` 无意义。

## 5. branches：存档线

`branches(gameIndex, saveIndex)`。每条分支就是一套完整独立的存档。

```json
[
  {
    "current": true,
    "name": "main",
    "shortTip": "0414b8b1",
    "time": "2026-08-18  12:25",
    "tip": "0414b8b17a5db5812d5714c5e5b60fb037030bbd"
  }
]
```

**关键约束**：打开旧版本只能「在该提交建立新分支并切换」这一个原子操作，界面上不得
提供裸 checkout。裸 checkout 会让仓库进入 detached HEAD，此时 C++ 侧的
`push_repository`、`integrate_repository`、`resolve_divergence` 全部直接报错 ——
玩家还能继续玩、后台还能继续提交，但推不上远端、拉不回远端、分歧无法处理，且没有
任何提示。

配套调用：

- `suggestedBranchName(gameIndex, saveIndex, commitId)` 返回建议名，形如
  `save/20260818-1a2b3c4d`。作为输入框默认值，允许玩家改。
- `openCommitAsBranch(gameIndex, saveIndex, commitId, branch)` 建分支并切换。
- `switchToBranch(gameIndex, saveIndex, branch)` 切换到已有分支，只接受具名分支。

三者都会在有未提交存档变化时失败，并要求游戏已退出。

## 6. history：存档时间线

`refreshHistory(gameIndex, saveIndex)` 触发，然后读 `history`。

```json
[
  {
    "id": "0414b8b17a5db5812d5714c5e5b60fb037030bbd",
    "metadata": "{\"accounts\":{},\"changed_files\":{\"1\":\"slot.sav\"},\"game_id\":\"elden-ring\",\"repository\":\"D:/Games/SaveDemo/elden-ring/save\",\"warnings\":{}}",
    "shortId": "0414b8b1",
    "slotSummary": "未解析到存档槽位",
    "slots": [],
    "summary": "G-SAVE elden-ring: game-exit at 2026-08-18T04:25:39Z",
    "time": "2026-08-18  12:25:39",
    "title": "退出游戏时保存"
  },
  {
    "id": "1643433d65f47ccd25771470a2ce569aa2ab783e",
    "metadata": "{\"accounts\":{},\"changed_files\":{\"1\":\"slot.sav\"},\"game_id\":\"elden-ring\",\"repository\":\"D:/Games/SaveDemo/elden-ring/save\",\"warnings\":{}}",
    "shortId": "1643433d",
    "slotSummary": "未解析到存档槽位",
    "slots": [],
    "summary": "G-SAVE elden-ring: quiet-period at 2026-08-18T04:25:39Z",
    "time": "2026-08-18  12:25:39",
    "title": "游戏保存完成"
  }
]
```

- `title` 已经是中文友好文案，直接显示，不要再翻译 `summary`。
- `slots` 是支持包 `parse()` 解析出的槽位。字段随支持包不同，通用支持包会是空数组。
- `slotSummary` 是给卡片用的一行摘要。
- `metadata` 是原始 JSON 字符串，一般不用，调试时可展示。

同时读两个状态：

- `historyDiverged` 为 true 时本地和云端各有新版本，只能整条时间线取舍：
  调 `resolveTimeline(gameIndex, saveIndex, localAsMain)`，另一端会自动保留为侧分支。
  **不要提供逐文件合并界面**，存档是二进制文件，合并没有意义。
- `historyStatus` 是对应的说明文本。

## 7. service 与设置

顶层服务状态，对应顶栏状态灯和设置页。

```json
{
  "autostartEnabled": true,
  "configPath": "D:\\Games\\SaveDemo\\config.toml",
  "coreBusy": false,
  "corePath": "D:\\Games\\SaveDemo\\gsave-core.exe",
  "coreRunning": false,
  "hasPendingChanges": false,
  "indexLoading": false,
  "indexStatus": "",
  "indexUrl": "https://raw.githubusercontent.com/AirLongDian/G-SAVE/main/packages/index.json",
  "packageRoot": "D:\\Games\\SaveDemo\\packages"
}
```

- `coreRunning` 决定状态灯颜色；`coreBusy` 为 true 时禁用点击并显示过渡动画，
  否则连点会同时触发启动和停止。
- 点击调 `toggleCore()`。
- `hasPendingChanges` 为 true 时顶栏显示「保存并重启服务」。
  后台读配置的时机只有启动，所以提交策略改动是暂存的，需要显式保存并重启。
- 关闭窗口前若 `hasPendingChanges` 为 true，必须弹窗询问
  「保存并重启 / 放弃改动 / 取消关闭」，否则暂存内容会静默丢失。

云端设置：

```json
{
  "allRemoteConfigured": false,
  "credentialStored": false,
  "gameCount": 3,
  "interval": 300,
  "repositories": [
    {
      "configured": false,
      "enabled": true,
      "game": "ELDEN RING",
      "repository": "gsave-elden-ring"
    },
    {
      "configured": false,
      "enabled": true,
      "game": "DarkSoulsIII",
      "repository": "gsave-dark-souls-iii"
    },
    {
      "configured": false,
      "enabled": false,
      "game": "DRAGON'S DOGMA 2",
      "repository": "gsave-dragons-dogma-2"
    }
  ],
  "repositoryCount": 3,
  "serviceAddress": "",
  "trigger": 1
}
```

云端账号对**全部已配置游戏**生效，不提供逐游戏云端开关。是否保护某个游戏只由详情页
的启用、暂停、移除控制。

## 8. 完整调用清单

只读，返回数据：

| 调用 | 返回 |
|---|---|
| `library` | 第 2 节 |
| `games` | 精简游戏列表 |
| `packages` | 已导入支持包列表 |
| `gameDetail(gameIndex)` | 第 3 节 |
| `repositoryState(gameIndex, saveIndex)` | 第 4 节 |
| `branches(gameIndex, saveIndex)` | 第 5 节 |
| `history` | 第 6 节 |
| `repositoriesForGame(gameIndex)` | 存档位置列表 |
| `cloudSettings()` | 第 7 节 |
| `suggestedBranchName(gameIndex, saveIndex, commitId)` | 字符串 |

有副作用，会弹原生确认框：

| 调用 | 说明 |
|---|---|
| `refreshIndex()` | 拉在线清单。失败不是错误，已安装卡片照常显示 |
| `importPackage()` | 打开文件选择器导入 ZIP |
| `importPackageFile(zipPath)` | 直接导入指定 ZIP，用于拖放 |
| `installPackage(packageIndex)` | 安装/重新检测本地支持包 |
| `installFromIndex(id)` | 下载并校验 SHA-256 后安装 |
| `installGenericPackage()` | 通用支持，唯一会打开路径选择器的入口 |
| `toggleGame(gameIndex)` | 暂停/继续保护 |
| `removeGame(gameIndex)` | 移除游戏，保留存档和 `.git` |
| `stageCommitPolicy(gameIndex, policy)` | 暂存提交策略，不写盘 |
| `discardPendingChanges()` | 放弃暂存 |
| `savePendingChanges()` | 写配置并重启服务 |
| `openCommitAsBranch(...)` | 建新存档线并切换 |
| `switchToBranch(...)` | 切换存档线 |
| `restoreVersion(gameIndex, saveIndex, commitId)` | 恢复到当前存档线，历史保持线性 |
| `pushSave` / `integrateSave` | 上传 / 拉取 |
| `resolveTimeline(gameIndex, saveIndex, localAsMain)` | 分歧取舍 |
| `deferTimelineDecision()` | 稍后处理分歧 |
| `saveCloudSettings(form)` / `testCloudConnection(form)` / `deleteCredential()` | 云端 |
| `toggleCore` / `startCore` / `stopCore` / `restartCore` | 服务 |
| `setAutostart(enabled)` | 登录自启 |

推送事件，收到后重新拉取对应数据：

| 事件 | 影响 |
|---|---|
| `gamesChanged` | `library`、`games`、`gameDetail` |
| `packagesChanged` | `library`、`packages` |
| `historyChanged` | `history` |
| `historyStateChanged` | `historyDiverged`、`historyStatus` |
| `serviceChanged` | `service` |
| `indexChanged` | `indexUrl`、`indexStatus`、`indexLoading` |
| `pendingChanged` | `hasPendingChanges` |
| `message(text, error)` | 显示 toast，`error` 为 true 用错误样式 |

## 9. 可直接使用的 mock 桥

`docs/frontend-mock-data.json` 是上面全部样例的完整导出，可直接作为开发期数据源。

```js
import mock from "./frontend-mock-data.json"

function createBridge() {
  const real = window.chrome?.webview
  if (real) {
    let sequence = 0
    const pending = new Map()
    const listeners = new Set()
    real.addEventListener("message", event => {
      const message = event.data
      if (message.id !== undefined && pending.has(message.id)) {
        const { resolve, reject } = pending.get(message.id)
        pending.delete(message.id)
        message.ok ? resolve(message.result) : reject(new Error(message.error))
        return
      }
      listeners.forEach(listener => listener(message))
    })
    return {
      call(method, params = {}) {
        const id = ++sequence
        return new Promise((resolve, reject) => {
          pending.set(id, { resolve, reject })
          real.postMessage({ id, method, params })
        })
      },
      subscribe(listener) {
        listeners.add(listener)
        return () => listeners.delete(listener)
      }
    }
  }

  // Browser development: same interface, data straight from the contract dump.
  const perGame = mock.perGame
  const table = {
    library: () => mock.library,
    games: () => mock.games,
    packages: () => mock.packages,
    cloudSettings: () => mock.cloudSettings,
    service: () => mock.service,
    gameDetail: ({ gameIndex }) => perGame[gameIndex]?.gameDetail ?? {},
    repositoryState: ({ gameIndex }) => perGame[gameIndex]?.repositoryState ?? {},
    branches: ({ gameIndex }) => perGame[gameIndex]?.branches ?? [],
    history: ({ gameIndex = 0 }) => perGame[gameIndex]?.history ?? [],
    repositoriesForGame: ({ gameIndex }) =>
      perGame[gameIndex]?.repositoriesForGame ?? [],
    suggestedBranchName: ({ commitId }) =>
      "save/20260818-" + String(commitId ?? "").slice(0, 8)
  }
  return {
    call(method, params = {}) {
      const handler = table[method]
      // Side-effecting calls are no-ops in the browser; only layout matters here.
      return Promise.resolve(handler ? handler(params) : null)
    },
    subscribe() {
      return () => {}
    }
  }
}
```

mock 里所有有副作用的调用都是空操作。前端不要依赖它们的返回值刷新界面 —— 真实环境下
界面刷新由第 8 节的推送事件驱动。

## 10. 配色参考

沿用现有深色主题，可改：

```
canvas  #0b1118      面板底  #131f2a      悬停  #1c2d3a
边框    #29404f      正文    #eef3f6      次要  #94a7b3
弱化    #607683      强调    #efb45b      成功  #64c7b4
危险    #ed7d7d      输入底  #0e1821
```

界面文字用 Segoe UI Variable，路径和提交 ID 用等宽字体。

## 11. 页面结构

1. **顶栏** —— 标识、导航（游戏库 / 设置）、待保存提示、Core 状态灯（点击启停）
2. **游戏库** —— 搜索、导入 ZIP、刷新清单、已安装分区（末尾占位卡）、可安装分区
3. **游戏详情** —— 横幅 + 两标签页
   - 支持配置：路径、监听规则、提交策略、仓库状态
   - 存档管理：时间线、选中版本元数据、开新存档线、切换存档线、上传拉取、分歧处理
4. **设置** —— 服务启停与自启、云端账号、在线清单地址、文件位置

窗口 ZIP 拖放导入支持包，方便玩家分享自制包。
