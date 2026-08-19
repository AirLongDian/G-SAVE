import { useState } from 'react';
import { createFileRoute, useNavigate } from '@tanstack/react-router';
import { Tabs, TabsContent, TabsList, TabsTrigger } from '@/components/ui/tabs';
import { GameCard } from '@/components/GameCard';
import { SearchBar } from '@/components/SearchBar';
import { AppHeader } from '@/components/AppHeader';
import { ExitConfirmDialog } from '@/components/ExitConfirmDialog';
import { mockInstalledGames, mockAvailablePackages, mockCoreStatus } from '@/lib/mock-data';
import type { SupportPackage } from '@/types/game';
import { toast } from 'sonner';

export const Route = createFileRoute('/')({
  component: HomePage,
});

function HomePage() {
  const navigate = useNavigate();
  const [searchQuery, setSearchQuery] = useState('');
  const [installedGames, setInstalledGames] = useState(mockInstalledGames);
  const [availablePackages, setAvailablePackages] = useState(mockAvailablePackages);
  const [core, setCore] = useState(mockCoreStatus);

  // 状态灯即开关。过渡期间禁用点击，否则连点会同时触发启动和停止。
  const handleToggleCore = () => {
    setCore((prev) => ({ ...prev, coreBusy: true }));
    setTimeout(() => {
      setCore((prev) => ({ ...prev, coreBusy: false, coreRunning: !prev.coreRunning }));
    }, 900);
  };
  const [exitDialogOpen, setExitDialogOpen] = useState(false);

  // 过滤已安装的游戏
  const filteredInstalled = installedGames.filter(game =>
    game.name.toLowerCase().includes(searchQuery.toLowerCase()) ||
    game.description?.toLowerCase().includes(searchQuery.toLowerCase())
  );

  // 过滤可安装的包
  const filteredAvailable = availablePackages.filter(pkg =>
    pkg.gameName.toLowerCase().includes(searchQuery.toLowerCase()) ||
    pkg.description.toLowerCase().includes(searchQuery.toLowerCase())
  );

  // 处理安装支持包
  const handleInstall = (pkg: SupportPackage) => {
    toast.success(`正在安装 ${pkg.gameName} 支持包...`);
    // 模拟安装过程
    setTimeout(() => {
      const newGame = {
        id: `game-${Date.now()}`,
        name: pkg.gameName,
        steamId: pkg.steamId,
        isInstalled: true,
        hasSupportPackage: true,
        coverUrl: `https://cdn.cloudflare.steamstatic.com/steam/apps/${pkg.steamId}/header.jpg`,
        description: pkg.description,
      };
      setInstalledGames(prev => [...prev, newGame]);
      setAvailablePackages(prev => prev.filter(p => p.id !== pkg.id));
      toast.success(`${pkg.gameName} 支持包安装成功!`);
    }, 1500);
  };

  // 处理配置跳转
  const handleConfigure = (gameId: string) => {
    navigate({ to: '/games/$gameId/config', params: { gameId } });
  };

  // 处理添加自定义游戏
  const handleAddCustom = () => {
    toast.info('添加自定义游戏功能开发中...');
  };

  return (
    <div className="min-h-screen bg-background">
      {/* Header */}
      <AppHeader
        title="G-SAVE 游戏库"
        coreStatus={core}
        onToggleCore={handleToggleCore}
      >
        <SearchBar
          value={searchQuery}
          onChange={setSearchQuery}
          placeholder="搜索游戏名称..."
        />
      </AppHeader>
      <ExitConfirmDialog
        open={exitDialogOpen}
        onOpenChange={setExitDialogOpen}
        hasPendingChanges={core.hasPendingChanges}
      />

      {/* Main Content */}
      <main className="container mx-auto px-4 py-6">
        <Tabs defaultValue="installed" className="w-full">
          <TabsList className="grid w-full max-w-md grid-cols-2 mb-6">
            <TabsTrigger value="installed">
              已安装 ({filteredInstalled.length})
            </TabsTrigger>
            <TabsTrigger value="available">
              可安装 ({filteredAvailable.length})
            </TabsTrigger>
          </TabsList>

          {/* 已安装的游戏 */}
          <TabsContent value="installed" className="mt-0">
            <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 gap-4">
              {filteredInstalled.map(game => (
                <GameCard
                  key={game.id}
                  game={game}
                  type="installed"
                  onConfigure={handleConfigure}
                />
              ))}
              {/* 添加自定义游戏占位卡片 */}
              <GameCard
                type="add-custom"
                onAddCustom={handleAddCustom}
              />
            </div>
            {filteredInstalled.length === 0 && (
              <div className="text-center py-12 text-muted-foreground">
                <p>暂无已安装的游戏</p>
                <p className="text-sm mt-2">切换到"可安装"标签页添加支持包</p>
              </div>
            )}
          </TabsContent>

          {/* 可安装的支持包 */}
          <TabsContent value="available" className="mt-0">
            <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 gap-4">
              {filteredAvailable.map(pkg => (
                <GameCard
                  key={pkg.id}
                  supportPackage={pkg}
                  type="available"
                  onInstall={handleInstall}
                />
              ))}
            </div>
            {filteredAvailable.length === 0 && (
              <div className="text-center py-12 text-muted-foreground">
                <p>没有匹配的支持包</p>
              </div>
            )}
          </TabsContent>
        </Tabs>
      </main>
    </div>
  );
}
