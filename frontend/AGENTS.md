# Steam 存档管理器 - 项目上下文

## 项目概述
这是一个 Steam 游戏存档版本管理工具,类似 Vortex 但专注于存档的 Git 版本控制。

## 技术栈
- React 19 + Vite
- TanStack Router (路由)
- shadcn/ui (UI组件库)
- Tailwind CSS v4 (样式)
- TypeScript

## 核心功能

### 1. 首页 (/)
- **已安装游戏卡片**: 显示已配置支持包的游戏,点击可进入配置页
- **可安装支持包**: 从联网获取的支持包清单,点击可自动安装
- **搜索功能**: 按游戏名称搜索
- **添加自定义游戏**: 占位卡片,用于添加无官方支持包的游戏

### 2. 游戏配置页 (/games/:gameId/config)
包含两个Tab:

#### 支持配置 Tab
- 游戏可执行文件路径
- 游戏存档路径
- 提交方式选择(Git/手动)
- Git仓库地址和Token配置
- Git连接测试功能

#### 存档管理 Tab
- 分支切换查看不同存档线
- Git历史树形展示(Commit Hash、消息、作者、时间)
- 版本回退(自动创建新分支避免冲突)
- 冲突检测和解决
- HEAD标记当前版本

## 数据结构

### Game
```typescript
interface Game {
  id: string;
  name: string;
  steamId?: string;
  isInstalled: boolean;
  hasSupportPackage: boolean;
  coverUrl?: string;
  description?: string;
}
```

### GameConfig
```typescript
interface GameConfig {
  gameId: string;
  exePath: string;
  savePath: string;
  submitMethod: 'git' | 'manual';
  gitUrl?: string;
  gitToken?: string;
  branchName?: string;
  lastTestedAt?: string;
  testStatus?: 'success' | 'failed' | 'pending';
}
```

### SaveVersion
```typescript
interface SaveVersion {
  hash: string;
  shortHash: string;
  message: string;
  author: string;
  timestamp: string;
  branch: string;
  isHead: boolean;
  hasConflict?: boolean;
}
```

## 文件结构
```
src/
├── components/
│   ├── GameCard.tsx          # 游戏卡片组件
│   ├── SearchBar.tsx         # 搜索栏组件
│   ├── SupportConfigTab.tsx  # 支持配置Tab
│   └── SavesManagementTab.tsx # 存档管理Tab
├── routes/
│   ├── index.tsx             # 首页
│   └── games_.$gameId.config.tsx # 游戏配置页
├── types/
│   └── game.ts               # 类型定义
└── lib/
    └── mock-data.ts          # 模拟数据
```

## 待完善功能
1. 真实的Git操作封装(当前为模拟)
2. 文件系统访问(需要桌面端Electron/Tauri支持)
3. 联网获取Steam支持包清单API
4. 用户认证和数据持久化
5. 添加自定义游戏的完整流程

## What Didn't Work
- ❌ `@gitgraph/react` v1.6.0：编译通过但运行时报 `templateExtend`/`branch` undefined 错误，React 18/19 均不可用（库自身 bug，2019 年停更）→ 改用 `@gitgraph/js` 纯 JS 版
- ❌ 手写 CSS+SVG 渲染 Git Graph：用户明确拒绝，要求必须用现成组件库

## Lessons
- Git Graph 可视化采用 iframe 嵌入 `public/git-graph-viewer.html`，通过 CDN 引入 `@gitgraph/js@1.4.0/lib/gitgraph.umd.min.js`
- UMD 全局变量为 `window.GitgraphJS`，正确 API 是 `GitgraphJS.createGitgraph(container, options)`（不是 `new Gitgraph()`）；CDN 路径必须是 `/lib/gitgraph.umd.min.js`（不是 `/dist/`）
- 主应用与 iframe 通过 `postMessage` 通信：iframe 发 `gitgraph-ready`，主应用回 `render-commits` 携带提交数据
- 配置页 Tab 状态已改为 URL search 参数 `?tab=support|saves`，便于直接定位存档管理 Tab
- Git Graph iframe 仅做可视化、不可点击；选中提交/查看解析数据/操作必须靠 iframe 之外的可点击提交时间线列表（用户曾反馈换成纯 iframe 后无法选择提交）
- 数据契约（G-SAVE，WebView2 C++ 宿主）：打开旧版本只能用 `openCommitAsBranch`（建分支并切换），禁止裸 checkout；`restoreVersion` 恢复当前存档线保持线性；解析数据在 `history[].slots/slotSummary`；`history[]` 无 author 字段；云端配置（cloudSettings）是全局的，不逐游戏；游戏有 `generic` 标志区分通用/专用支持包，专用包路径只读；`coreBusy`/`hasPendingChanges` 驱动顶栏 Core 状态灯与退出保存确认
