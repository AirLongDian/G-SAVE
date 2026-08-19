// 模拟数据(实际项目中应从后端API获取)
import type { CloudSettings, CoreStatus, Game, GameConfig, SupportPackage } from '@/types/game';

// 模拟已安装的游戏
export const mockInstalledGames: Game[] = [
  {
    id: '1',
    name: 'Elden Ring',
    steamId: '1245620',
    isInstalled: true,
    hasSupportPackage: true,
    generic: false,
    packagePath: 'D:\\G-SAVE\\packages\\elden-ring\\adapter.lua',
    coverUrl: 'https://cdn.cloudflare.steamstatic.com/steam/apps/1245620/header.jpg',
    description: '艾尔登法环',
  },
  {
    id: '2',
    name: 'Baldur\'s Gate 3',
    steamId: '1086940',
    isInstalled: true,
    hasSupportPackage: true,
    generic: false,
    packagePath: 'D:\\G-SAVE\\packages\\bg3\\adapter.lua',
    coverUrl: 'https://cdn.cloudflare.steamstatic.com/steam/apps/1086940/header.jpg',
    description: '博德之门3',
  },
  {
    id: '3',
    name: 'Cyberpunk 2077',
    steamId: '1091500',
    isInstalled: true,
    hasSupportPackage: false, // 无支持包,自定义添加
    generic: true, // 契约:通用支持
    coverUrl: 'https://cdn.cloudflare.steamstatic.com/steam/apps/1091500/header.jpg',
    description: '赛博朋克2077',
  },
];

// 模拟可安装的支持包
export const mockAvailablePackages: SupportPackage[] = [
  {
    id: 'pkg-1',
    gameName: 'Stardew Valley',
    steamId: '413150',
    version: '1.6.0',
    downloadUrl: '#',
    description: '星露谷物语存档扩展支持包',
    fileSize: '2.3 MB',
  },
  {
    id: 'pkg-2',
    gameName: 'Terraria',
    steamId: '105600',
    version: '1.4.4',
    downloadUrl: '#',
    description: '泰拉瑞亚存档扩展支持包',
    fileSize: '1.8 MB',
  },
  {
    id: 'pkg-3',
    gameName: 'Hades',
    steamId: '1145360',
    version: '1.0',
    downloadUrl: '#',
    description: '黑帝斯存档扩展支持包',
    fileSize: '1.5 MB',
  },
];

// 模拟游戏配置(不再含 Git 凭据:云端账号是全局设置)
export const mockGameConfigs: Record<string, GameConfig> = {
  '1': {
    gameId: '1',
    exePath: 'D:\\Steam\\steamapps\\common\\ELDEN RING\\Game\\eldenring.exe',
    savePath: 'C:\\Users\\player\\AppData\\Roaming\\EldenRing',
    pushStrategy: 'onExit',
    commitOnExit: true,
  },
  '2': {
    gameId: '2',
    exePath: 'D:\\Steam\\steamapps\\common\\Baldurs Gate 3\\bin\\bg3.exe',
    savePath: 'C:\\Users\\player\\AppData\\Local\\Larian Studios\\Baldur\'s Gate 3',
    pushStrategy: 'onExit',
    commitOnExit: true,
  },
  '3': {
    gameId: '3',
    exePath: 'D:\\Games\\Cyberpunk 2077\\bin\\x64\\Cyberpunk2077.exe',
    savePath: 'C:\\Users\\player\\Saved Games\\CD Projekt Red\\Cyberpunk 2077',
    pushStrategy: 'interval',
    pushInterval: 10,
    commitOnExit: true,
  },
};

// 契约 cloudSettings:云端账号为全局配置,对全部已配置游戏生效
export const mockCloudSettings: CloudSettings = {
  serviceAddress: 'github.com',
  token: '',
  credentialStored: true,
  allRemoteConfigured: true,
  gameCount: 3,
  repositoryCount: 3,
  trigger: 1,
  interval: 300,
  lastTestedAt: '2026-08-18T10:30:00Z',
  testStatus: 'success',
};

// 契约 Core 运行状态:顶栏状态灯的数据源
export const mockCoreStatus: CoreStatus = {
  coreRunning: true,
  coreBusy: false,
  hasPendingChanges: false,
};

// 契约:设置页展示的文件位置
export const mockPaths = {
  configPath: 'C:\\Users\\player\\AppData\\Local\\G-SAVE\\config.toml',
  packageRoot: 'C:\\Users\\player\\AppData\\Local\\G-SAVE\\packages',
  corePath: 'C:\\Program Files\\G-SAVE\\gsave-core.exe',
};

// 契约:同步方向由每个仓库的实际状态决定(本地领先=push,远端领先=pull,
// 两边都有新提交=分歧,交由玩家在存档管理页整条时间线取舍)
export const mockSyncOutcome = {
  pushed: 2,
  pulled: 1,
  diverged: 0,
};
