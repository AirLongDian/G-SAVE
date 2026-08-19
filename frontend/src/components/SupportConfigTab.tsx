import { useState } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Button } from '@/components/ui/button';
import { Switch } from '@/components/ui/switch';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { FolderOpen, Package, Lock, RefreshCw } from 'lucide-react';
import type { GameConfig, PushStrategy } from '@/types/game';
import { mockInstalledGames } from '@/lib/mock-data';
import { toast } from 'sonner';

interface SupportConfigTabProps {
  gameId: string;
  config?: GameConfig;
  onSave: (config: GameConfig) => void;
}

export function SupportConfigTab({ gameId, config, onSave }: SupportConfigTabProps) {
  const game = mockInstalledGames.find((g) => g.id === gameId);
  // 专用支持包自己检测路径。手动指定会让 G-SAVE 版本化错误的目录,
  // 所以只有通用支持才允许编辑。
  const isGeneric = game?.generic === true;

  const [formData, setFormData] = useState<GameConfig>(
    config || {
      gameId,
      exePath: '',
      savePath: '',
      pushStrategy: 'immediate',
      commitOnExit: true,
    }
  );

  const handleSelectFolder = (field: 'exePath' | 'savePath') => {
    toast.info('选择文件夹功能需要桌面端支持');
  };

  const handleRedetect = () => {
    toast.info('正在重新检测游戏与存档位置…');
  };

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (!formData.exePath || !formData.savePath) {
      toast.error('缺少游戏程序或存档路径');
      return;
    }
    onSave(formData);
    toast.success('设置已保存');
  };

  return (
    <form onSubmit={handleSubmit}>
      <div className="space-y-6">
        {/* 支持包 */}
        {game?.packagePath && (
          <Card>
            <CardHeader>
              <CardTitle className="flex items-center gap-2">
                <Package className="w-5 h-5" />
                {isGeneric ? '通用支持' : '专用支持包'}
              </CardTitle>
              <CardDescription>
                {isGeneric
                  ? '这个游戏使用通用支持，只提供时间戳和通用文件信息'
                  : '支持包自动检测游戏与存档位置，无需手动指定'}
              </CardDescription>
            </CardHeader>
            <CardContent>
              <div className="flex items-center gap-2 rounded-md border bg-muted/50 px-3 py-2.5">
                <Lock className="w-4 h-4 text-muted-foreground shrink-0" />
                <code className="text-sm text-muted-foreground truncate">{game.packagePath}</code>
              </div>
            </CardContent>
          </Card>
        )}

        {/* 路径 */}
        <Card>
          <CardHeader>
            <div className="flex items-start justify-between gap-3">
              <div>
                <CardTitle>游戏与存档位置</CardTitle>
                <CardDescription>
                  {isGeneric
                    ? '手动选择游戏程序和包含有效存档的文件夹'
                    : '由支持包自动检测。游戏换盘或重装后点击「重新检测」'}
                </CardDescription>
              </div>
              {!isGeneric && (
                <Button type="button" variant="outline" size="sm" className="gap-2 shrink-0" onClick={handleRedetect}>
                  <RefreshCw className="w-4 h-4" />
                  重新检测
                </Button>
              )}
            </div>
          </CardHeader>
          <CardContent className="space-y-4">
            <div className="space-y-2">
              <Label htmlFor="exePath">游戏可执行文件路径</Label>
              {isGeneric ? (
                <div className="flex gap-2">
                  <Input
                    id="exePath"
                    value={formData.exePath}
                    onChange={(e) => setFormData({ ...formData, exePath: e.target.value })}
                    placeholder="例如: C:\Games\MyGame\game.exe"
                  />
                  <Button type="button" variant="outline" size="icon" onClick={() => handleSelectFolder('exePath')}>
                    <FolderOpen className="w-4 h-4" />
                  </Button>
                </div>
              ) : (
                <div className="flex items-center gap-2 rounded-md border bg-muted/50 px-3 py-2.5">
                  <Lock className="w-4 h-4 text-muted-foreground shrink-0" />
                  <code className="text-sm text-muted-foreground truncate">{formData.exePath || '未检测到'}</code>
                </div>
              )}
            </div>

            <div className="space-y-2">
              <Label htmlFor="savePath">游戏存档路径</Label>
              {isGeneric ? (
                <div className="flex gap-2">
                  <Input
                    id="savePath"
                    value={formData.savePath}
                    onChange={(e) => setFormData({ ...formData, savePath: e.target.value })}
                    placeholder="例如: C:\Users\User\AppData\Roaming\MyGame"
                  />
                  <Button type="button" variant="outline" size="icon" onClick={() => handleSelectFolder('savePath')}>
                    <FolderOpen className="w-4 h-4" />
                  </Button>
                </div>
              ) : (
                <div className="flex items-center gap-2 rounded-md border bg-muted/50 px-3 py-2.5">
                  <Lock className="w-4 h-4 text-muted-foreground shrink-0" />
                  <code className="text-sm text-muted-foreground truncate">{formData.savePath || '未检测到'}</code>
                </div>
              )}
            </div>
          </CardContent>
        </Card>

        {/* 提交与推送 */}
        <Card>
          <CardHeader>
            <CardTitle>什么时候保存和上传</CardTitle>
            <CardDescription>存档保护在启动时读取配置，改动需要保存并重启服务后才生效</CardDescription>
          </CardHeader>
          <CardContent className="space-y-4">
            <div className="space-y-2">
              <Label>上传时机</Label>
              <Select
                value={formData.pushStrategy}
                onValueChange={(value: PushStrategy) => setFormData({ ...formData, pushStrategy: value })}
              >
                <SelectTrigger>
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="immediate">立即上传(每次保存后)</SelectItem>
                  <SelectItem value="interval">定时上传(每隔一段时间)</SelectItem>
                  <SelectItem value="onExit">游戏关闭时上传</SelectItem>
                </SelectContent>
              </Select>
            </div>

            {formData.pushStrategy === 'interval' && (
              <div className="space-y-2">
                <Label htmlFor="pushInterval">上传间隔(分钟)</Label>
                <Input
                  id="pushInterval"
                  type="number"
                  min={1}
                  max={60}
                  value={formData.pushInterval || 5}
                  onChange={(e) => setFormData({ ...formData, pushInterval: Number(e.target.value) })}
                />
              </div>
            )}

            <div className="flex items-center justify-between rounded-md border px-3 py-2.5">
              <div className="space-y-0.5">
                <Label htmlFor="commitOnExit" className="text-sm font-medium cursor-pointer">
                  退出游戏时保存
                </Label>
                <p className="text-xs text-muted-foreground">关闭游戏时如有未保存的存档变化，先保存一次</p>
              </div>
              <Switch
                id="commitOnExit"
                checked={formData.commitOnExit ?? false}
                onCheckedChange={(checked) => setFormData({ ...formData, commitOnExit: checked })}
              />
            </div>
          </CardContent>
        </Card>

        <div className="flex justify-end">
          <Button type="submit" size="lg">
            保存并重启服务
          </Button>
        </div>
      </div>
    </form>
  );
}
