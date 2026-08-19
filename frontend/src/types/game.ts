// 游戏相关类型定义(对齐 G-SAVE 数据契约)

export interface Game {
  id: string;
  name: string;
  steamId?: string; // Steam App ID,用于获取海报
  isInstalled: boolean; // 是否已安装支持包
  hasSupportPackage: boolean; // 是否有官方支持包(generic=true 时为通用支持)
  generic?: boolean; // 契约:是否为通用支持包(无专用包)
  packagePath?: string; // 契约:专用支持包路径(只读展示)
  coverUrl?: string; // 游戏封面URL(Steam海报)
  description?: string;
}

export type PushStrategy = 'immediate' | 'interval' | 'onExit';

export interface GameConfig {
  gameId: string;
  exePath: string; // 专用支持包自动检测,只读
  savePath: string; // 专用支持包自动检测,只读
  pushStrategy: PushStrategy; // 推送时机
  pushInterval?: number; // 定时推送间隔(分钟),仅 pushStrategy='interval' 时有效
  commitOnExit?: boolean; // 退出游戏时自动提交
}

// 契约:云端账号为全局配置,对全部已配置游戏生效
export interface CloudSettings {
  serviceAddress: string; // Git 服务地址,自动识别 GitHub/Gitee/GitLab/Gitea
  token: string; // 访问令牌,仅输入用;已保存后留空表示沿用
  credentialStored: boolean; // Token 是否已存入 Windows 凭据管理器
  allRemoteConfigured: boolean; // 是否所有仓库都已配置远端
  gameCount: number; // 受影响的游戏数
  repositoryCount: number; // 受影响的仓库数
  trigger: number; // 上传时机 0 每次提交后 1 退出游戏后 2 周期 3 仅手动
  interval: number; // trigger=2 时的间隔(秒)
  lastTestedAt?: string;
  testStatus?: 'success' | 'failed';
}

// 契约:Core 运行状态(顶栏状态灯)
export interface CoreStatus {
  coreRunning: boolean; // 存档保护是否正在运行
  coreBusy: boolean; // 启停过渡中,此时禁止点击
  hasPendingChanges: boolean; // 是否有未保存的设置(与运行状态无关)
}

// 契约 history[]:不含 author 字段
export interface SaveVersion {
  hash: string; // commit hash
  shortHash: string; // 短 hash(前7位)
  message: string; // 友好文案,直接显示
  timestamp: string;
  branch: string;
  isHead: boolean; // 是否为 HEAD
  hasConflict?: boolean; // 是否存在时间线分歧
}

export interface GitBranch {
  name: string;
  current: boolean;
  commits: SaveVersion[];
}

export interface SupportPackage {
  id: string;
  gameName: string;
  steamId: string;
  version: string;
  downloadUrl: string;
  description: string;
  fileSize: string;
}
