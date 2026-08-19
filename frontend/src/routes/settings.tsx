import { useState } from 'react';
import { createFileRoute } from '@tanstack/react-router';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Switch } from '@/components/ui/switch';
import { Badge } from '@/components/ui/badge';
import {
  Cloud,
  KeyRound,
  CheckCircle2,
  XCircle,
  Power,
  FolderOpen,
  Trash2,
} from 'lucide-react';
import { AppHeader } from '@/components/AppHeader';
import { mockCloudSettings, mockCoreStatus, mockPaths, mockSyncOutcome } from '@/lib/mock-data';
import { toast } from 'sonner';

export const Route = createFileRoute('/settings')({
  component: SettingsPage,
});

function SettingsPage() {
  const [settings, setSettings] = useState(mockCloudSettings);
  const [core, setCore] = useState(mockCoreStatus);
  const [autostart, setAutostart] = useState(true);
  const [testing, setTesting] = useState(false);
  const [saving, setSaving] = useState(false);

  const canSubmit =
    settings.serviceAddress.trim().length > 0 &&
    (settings.token.trim().length > 0 || settings.credentialStored);

  // 只验证登录,不创建仓库。
  const handleTest = () => {
    if (!canSubmit) {
      toast.error('请填写服务地址和 Token；已保存 Token 时可留空');
      return;
    }
    setTesting(true);
    setTimeout(() => {
      setTesting(false);
      setSettings((prev) => ({
        ...prev,
        testStatus: 'success',
        lastTestedAt: new Date().toISOString(),
      }));
      toast.success('已识别为 GitHub，Token 属于账号 player。尚未创建或修改仓库。');
    }, 1200);
  };

  // 同步方向由每个仓库的实际状态决定,不由界面预设:
  //   本地领先        -> push
  //   远端领先        -> pull(快进)
  //   两边都有新提交  -> 分歧,必须由玩家在该游戏的存档管理页整条时间线取舍
  const handleSync = () => {
    if (!canSubmit) {
      toast.error('请填写服务地址和 Token；已保存 Token 时可留空');
      return;
    }
    setSaving(true);
    setTimeout(() => {
      setSaving(false);
      setSettings((prev) => ({
        ...prev,
        token: '',
        credentialStored: true,
        allRemoteConfigured: true,
      }));
      const { pushed, pulled, diverged } = mockSyncOutcome;
      const parts: string[] = [];
      if (pushed > 0) parts.push(`上传 ${pushed} 个`);
      if (pulled > 0) parts.push(`下载 ${pulled} 个`);
      if (parts.length === 0) parts.push('全部已是最新');

      if (diverged > 0) {
        toast.warning(
          `${parts.join('，')}。${diverged} 个存档本地和云端各有新版本，` +
            '需要在该游戏的存档管理页选择保留哪条时间线。'
        );
      } else {
        toast.success(`${settings.repositoryCount} 个仓库同步完成：${parts.join('，')}。`);
      }
    }, 1400);
  };

  const handleDeleteToken = () => {
    setSettings((prev) => ({ ...prev, credentialStored: false, allRemoteConfigured: false }));
    toast.success('Token 已删除；自动上传已暂停，本地版本历史继续保存。');
  };

  const handleToggleCore = () => {
    setCore((prev) => ({ ...prev, coreBusy: true }));
    setTimeout(() => {
      setCore((prev) => ({ ...prev, coreBusy: false, coreRunning: !prev.coreRunning }));
    }, 900);
  };

  return (
    <div className="min-h-screen bg-background">
      <AppHeader
        title="设置"
        subtitle="全局配置"
        backTo="/"
        coreStatus={core}
        onToggleCore={handleToggleCore}
      />

      <main className="container mx-auto px-4 py-6 max-w-3xl space-y-6">
        {/* 存档保护服务 */}
        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2">
              <Power className="w-5 h-5" />
              存档保护服务
            </CardTitle>
            <CardDescription>
              游戏没运行时不会打开存档目录，也不做任何轮询
            </CardDescription>
          </CardHeader>
          <CardContent className="space-y-4">
            <div className="flex items-center justify-between rounded-md border px-3 py-2.5">
              <div className="flex items-center gap-2 min-w-0">
                <span
                  className={`w-2.5 h-2.5 rounded-full shrink-0 ${
                    core.coreBusy
                      ? 'bg-amber-500 animate-pulse'
                      : core.coreRunning
                        ? 'bg-emerald-500'
                        : 'bg-muted-foreground/50'
                  }`}
                />
                <span className="text-sm">
                  {core.coreBusy ? '处理中…' : core.coreRunning ? '正在运行' : '已停止'}
                </span>
              </div>
              <div className="flex items-center gap-2">
                <Button variant="outline" size="sm" onClick={handleToggleCore} disabled={core.coreBusy}>
                  {core.coreRunning ? '停止' : '启动'}
                </Button>
                <Button
                  variant="outline"
                  size="sm"
                  disabled={core.coreBusy || !core.coreRunning}
                  onClick={() => toast.success('存档保护已重新启动')}
                >
                  重启
                </Button>
              </div>
            </div>

            <div className="flex items-center justify-between rounded-md border px-3 py-2.5">
              <div className="space-y-0.5">
                <Label htmlFor="autostart" className="text-sm font-medium cursor-pointer">
                  登录 Windows 后自动启动
                </Label>
                <p className="text-xs text-muted-foreground">
                  推荐开启。无窗口、无托盘、不写常驻日志
                </p>
              </div>
              <Switch id="autostart" checked={autostart} onCheckedChange={setAutostart} />
            </div>
          </CardContent>
        </Card>

        {/* 云端账号:全局,对全部游戏生效 */}
        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2">
              <Cloud className="w-5 h-5" />
              云端账号
            </CardTitle>
            <CardDescription>
              自动识别 GitHub、Gitee、GitLab 和 Gitea，并按游戏创建私有仓库。
              账号对全部已配置游戏生效；上传时机由各游戏的支持包决定，在游戏配置页设置。
            </CardDescription>
          </CardHeader>
          <CardContent className="space-y-4">
            <div className="space-y-2">
              <Label htmlFor="service-address">服务地址</Label>
              <Input
                id="service-address"
                value={settings.serviceAddress}
                onChange={(e) => setSettings({ ...settings, serviceAddress: e.target.value })}
                placeholder="例如 github.com、gitee.com 或你的 Git 服务地址"
              />
              <p className="text-xs text-muted-foreground">
                留空表示只保存本地历史，不上传
              </p>
            </div>

            <div className="space-y-2">
              <div className="flex items-center justify-between">
                <Label htmlFor="git-token">Token</Label>
                <span className="text-xs text-muted-foreground">
                  {settings.credentialStored
                    ? '留空可继续使用已保存的 Token'
                    : '在服务的个人设置中创建访问令牌'}
                </span>
              </div>
              <Input
                id="git-token"
                type="password"
                value={settings.token}
                onChange={(e) => setSettings({ ...settings, token: e.target.value })}
                placeholder={
                  settings.credentialStored
                    ? 'Token 已保存，无需重复输入'
                    : '粘贴 Token；会自动读取账号名'
                }
              />
            </div>

            <div className="flex items-center justify-between rounded-md border px-3 py-2.5">
              <div className="flex items-center gap-2">
                <KeyRound className="w-4 h-4 text-muted-foreground" />
                <span className="text-sm">Windows 凭据管理器</span>
              </div>
              {settings.credentialStored ? (
                <Badge variant="default">Token 已保存</Badge>
              ) : (
                <Badge variant="secondary">尚未连接</Badge>
              )}
            </div>

            <div className="flex items-center justify-between gap-3 pt-1">
              <div className="flex items-center gap-2 min-w-0">
                {settings.testStatus === 'success' && (
                  <Badge variant="default" className="gap-1">
                    <CheckCircle2 className="w-3 h-3" />
                    已连接
                  </Badge>
                )}
                {settings.testStatus === 'failed' && (
                  <Badge variant="destructive" className="gap-1">
                    <XCircle className="w-3 h-3" />
                    连接失败
                  </Badge>
                )}
                {settings.lastTestedAt && (
                  <span className="text-xs text-muted-foreground truncate">
                    上次测试: {new Date(settings.lastTestedAt).toLocaleString('zh-CN')}
                  </span>
                )}
              </div>
              <Button
                type="button"
                variant="outline"
                onClick={handleTest}
                disabled={testing || !canSubmit}
                className="shrink-0"
              >
                {testing ? '测试中…' : '测试登录'}
              </Button>
            </div>

            <p className="text-xs text-muted-foreground">
              测试登录不会创建仓库。保存时才会检测或创建私有仓库、验证上传权限，
              并将 Token 存入 Windows 凭据管理器；Token 不会写入配置或存档历史。
            </p>

            <div className="flex items-center justify-between gap-3">
              <Button
                variant="ghost"
                size="sm"
                className="gap-2 text-destructive hover:text-destructive"
                disabled={!settings.credentialStored}
                onClick={handleDeleteToken}
              >
                <Trash2 className="w-4 h-4" />
                删除已保存的 Token
              </Button>
              <Button onClick={handleSync} disabled={saving || !canSubmit}>
                {saving ? '同步中…' : '同步全部仓库'}
              </Button>
            </div>
          </CardContent>
        </Card>

        {/* 文件位置 */}
        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2">
              <FolderOpen className="w-5 h-5" />
              文件位置
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-3">
            {[
              { label: '配置文件', value: mockPaths.configPath },
              { label: '支持包目录', value: mockPaths.packageRoot },
              { label: '后台程序', value: mockPaths.corePath },
            ].map((row) => (
              <div key={row.label} className="space-y-1">
                <p className="text-xs text-muted-foreground">{row.label}</p>
                <code className="block truncate rounded-md border bg-muted/50 px-3 py-2 text-xs">
                  {row.value}
                </code>
              </div>
            ))}
          </CardContent>
        </Card>
      </main>
    </div>
  );
}
