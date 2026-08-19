import { Card, CardContent } from '@/components/ui/card';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Download, Settings, Plus } from 'lucide-react';
import type { Game, SupportPackage } from '@/types/game';

interface GameCardProps {
  game?: Game;
  supportPackage?: SupportPackage;
  type: 'installed' | 'available' | 'add-custom';
  onInstall?: (pkg: SupportPackage) => void;
  onConfigure?: (gameId: string) => void;
  onAddCustom?: () => void;
}

export function GameCard({ game, supportPackage, type, onInstall, onConfigure, onAddCustom }: GameCardProps) {
  // 添加自定义游戏占位卡片
  if (type === 'add-custom') {
    return (
      <Card 
        className="cursor-pointer hover:shadow-lg transition-all duration-200 border-dashed border-2"
        onClick={onAddCustom}
      >
        <CardContent className="flex flex-col items-center justify-center h-64 gap-3">
          <div className="w-16 h-16 rounded-full bg-muted flex items-center justify-center">
            <Plus className="w-8 h-8 text-muted-foreground" />
          </div>
          <p className="text-sm text-muted-foreground">添加无支持包游戏</p>
        </CardContent>
      </Card>
    );
  }

  // 可安装的支持包卡片
  if (type === 'available' && supportPackage) {
    return (
      <Card className="overflow-hidden hover:shadow-lg transition-all duration-200">
        <div 
          className="h-32 bg-cover bg-center relative"
          style={{ 
            backgroundImage: `url(https://cdn.cloudflare.steamstatic.com/steam/apps/${supportPackage.steamId}/header.jpg)` 
          }}
        >
          <div className="absolute inset-0 bg-gradient-to-t from-black/60 to-transparent" />
          <div className="absolute bottom-2 left-3 right-3">
            <h3 className="text-white font-semibold text-sm truncate">{supportPackage.gameName}</h3>
            <p className="text-white/80 text-xs truncate">{supportPackage.description}</p>
          </div>
        </div>
        <CardContent className="pt-3 pb-3">
          <div className="flex items-center justify-between">
            <div className="flex flex-col gap-1">
              <Badge variant="secondary" className="w-fit text-xs">v{supportPackage.version}</Badge>
              <span className="text-xs text-muted-foreground">{supportPackage.fileSize}</span>
            </div>
            <Button 
              size="sm" 
              onClick={() => onInstall?.(supportPackage)}
              className="gap-1"
            >
              <Download className="w-4 h-4" />
              安装
            </Button>
          </div>
        </CardContent>
      </Card>
    );
  }

  // 已安装的游戏卡片
  if (type === 'installed' && game) {
    return (
      <Card className="overflow-hidden cursor-pointer hover:shadow-lg transition-all duration-200 group">
        <div 
          className="h-40 bg-cover bg-center relative"
          style={{ 
            backgroundImage: `url(${game.coverUrl})` 
          }}
        >
          <div className="absolute inset-0 bg-gradient-to-t from-black/70 via-black/30 to-transparent opacity-80 group-hover:opacity-90 transition-opacity" />
          <div className="absolute bottom-3 left-3 right-3">
            <h3 className="text-white font-bold text-base truncate drop-shadow-md">{game.name}</h3>
            {game.description && (
              <p className="text-white/90 text-xs mt-1 truncate drop-shadow">{game.description}</p>
            )}
          </div>
          {!game.hasSupportPackage && (
            <Badge className="absolute top-2 right-2 bg-orange-500/90 text-white text-xs">
              自定义
            </Badge>
          )}
        </div>
        <CardContent className="pt-3 pb-3">
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-2">
              <Badge variant={game.hasSupportPackage ? "default" : "outline"} className="text-xs">
                {game.hasSupportPackage ? '扩展支持' : '手动配置'}
              </Badge>
            </div>
            <Button 
              size="sm" 
              variant="ghost"
              onClick={(e) => {
                e.stopPropagation();
                onConfigure?.(game.id);
              }}
              className="gap-1"
            >
              <Settings className="w-4 h-4" />
              配置
            </Button>
          </div>
        </CardContent>
      </Card>
    );
  }

  return null;
}
