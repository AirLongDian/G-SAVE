import { useState } from 'react';
import { createFileRoute, useParams, useNavigate, useSearch } from '@tanstack/react-router';
import { z } from 'zod';
import { Tabs, TabsContent, TabsList, TabsTrigger } from '@/components/ui/tabs';
import { Button } from '@/components/ui/button';
import { AppHeader } from '@/components/AppHeader';
import { ExitConfirmDialog } from '@/components/ExitConfirmDialog';
import { SupportConfigTab } from '@/components/SupportConfigTab';
import { SavesManagementTab } from '@/components/SavesManagementTab';
import { mockGameConfigs, mockInstalledGames, mockCoreStatus } from '@/lib/mock-data';
import type { GameConfig } from '@/types/game';
import { toast } from 'sonner';

const configSearchSchema = z.object({
  tab: z.enum(['support', 'saves']).optional().default('support'),
});

export const Route = createFileRoute('/games_/$gameId/config')({
  validateSearch: configSearchSchema,
  component: GameConfigPage,
});

function GameConfigPage() {
  const [core, setCore] = useState(mockCoreStatus);

  // 状态灯即开关。过渡期间禁用点击，避免连点同时启停。
  const handleToggleCore = () => {
    setCore((prev) => ({ ...prev, coreBusy: true }));
    setTimeout(() => {
      setCore((prev) => ({ ...prev, coreBusy: false, coreRunning: !prev.coreRunning }));
    }, 900);
  };

  const { gameId } = useParams({ from: '/games_/$gameId/config' });
  const navigate = useNavigate();
  const [exitDialogOpen, setExitDialogOpen] = useState(false);
  const { tab } = useSearch({ from: '/games_/$gameId/config' });
  const activeTab = tab;
  const setActiveTab = (value: string) => {
    navigate({ to: '/games/$gameId/config', params: { gameId }, search: { tab: value as 'support' | 'saves' } });
  };

  // 查找游戏信息
  const game = mockInstalledGames.find(g => g.id === gameId);
  const config = mockGameConfigs[gameId];

  const handleSaveConfig = (newConfig: GameConfig) => {
    // 实际项目中这里会调用API保存配置
    console.log('Saving config:', newConfig);
    toast.success('配置已保存');
  };

  if (!game) {
    return (
      <div className="min-h-screen flex items-center justify-center">
        <div className="text-center">
          <h2 className="text-xl font-semibold mb-2">游戏未找到</h2>
          <Button onClick={() => navigate({ to: '/' })}>返回首页</Button>
        </div>
      </div>
    );
  }

  return (
    <div className="min-h-screen bg-background">
      {/* Header */}
      <AppHeader
        title={game.name}
        subtitle="配置管理"
        backTo="/"
        coreStatus={core}
        onToggleCore={handleToggleCore}
      />
      <ExitConfirmDialog
        open={exitDialogOpen}
        onOpenChange={setExitDialogOpen}
        hasPendingChanges={core.hasPendingChanges}
      />

      {/* Main Content */}
      <main className="container mx-auto px-4 py-6 max-w-4xl">
        <Tabs value={activeTab} onValueChange={setActiveTab} className="w-full">
          <TabsList className="grid w-full grid-cols-2 mb-6">
            <TabsTrigger value="support">支持配置</TabsTrigger>
            <TabsTrigger value="saves">存档管理</TabsTrigger>
          </TabsList>

          <TabsContent value="support" className="mt-0">
            <SupportConfigTab
              gameId={gameId}
              config={config}
              onSave={handleSaveConfig}
            />
          </TabsContent>

          <TabsContent value="saves" className="mt-0">
            <SavesManagementTab gameId={gameId} />
          </TabsContent>
        </Tabs>
      </main>
    </div>
  );
}
