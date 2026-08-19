import { Link } from '@tanstack/react-router';
import { Settings } from 'lucide-react';
import type { CoreStatus } from '@/types/game';

interface AppHeaderProps {
  title: string;
  subtitle?: string;
  coreStatus?: CoreStatus;
  backTo?: string;
  onToggleCore?: () => void;
  children?: React.ReactNode;
}

// 状态灯只表达存档保护的运行状态,点击即启停。
// 未保存的设置是另一件事,用独立徽标显示,不能混进运行状态里。
function coreLight(status: CoreStatus) {
  if (status.coreBusy) {
    return {
      color: 'bg-amber-500 animate-pulse',
      label: '处理中…',
      hint: '正在启动或停止存档保护',
    };
  }
  if (status.coreRunning) {
    return {
      color: 'bg-emerald-500',
      label: '存档运行中',
      hint: '点击停止存档保护',
    };
  }
  return {
    color: 'bg-muted-foreground/50',
    label: '存档已停止',
    hint: '点击启动存档保护',
  };
}

export function AppHeader({
  title,
  subtitle,
  coreStatus,
  backTo,
  onToggleCore,
  children,
}: AppHeaderProps) {
  const light = coreStatus ? coreLight(coreStatus) : null;

  return (
    <header className="border-b bg-card sticky top-0 z-10">
      <div className="container mx-auto px-4 py-3">
        {/* min-w-0 + shrink 让长标题优先压缩，状态灯永不换行 */}
        <div className="flex items-center gap-3 flex-nowrap">
          <div className="flex items-center gap-2 min-w-0 shrink">
            {backTo && (
              <Link
                to={backTo}
                className="shrink-0 rounded-md p-1.5 text-muted-foreground hover:bg-muted hover:text-foreground transition-colors"
                aria-label="返回"
              >
                <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="m12 19-7-7 7-7" /><path d="M19 12H5" /></svg>
              </Link>
            )}
            <div className="min-w-0">
              <h1 className="text-lg font-bold leading-tight truncate">{title}</h1>
              {subtitle && (
                <p className="text-xs text-muted-foreground truncate">{subtitle}</p>
              )}
            </div>
          </div>

          {/* 搜索框等自定义内容占据中间弹性空间 */}
          {children && (
            <div className="flex-1 min-w-0 flex justify-end">{children}</div>
          )}
          {!children && <div className="flex-1" />}

          <div className="flex items-center gap-2 shrink-0">
            {coreStatus?.hasPendingChanges && (
              <span
                className="hidden md:inline whitespace-nowrap rounded-full border border-amber-500/40 bg-amber-500/10 px-2.5 py-1 text-xs font-medium text-amber-600 dark:text-amber-400"
                title="设置改动需要保存并重启存档保护后才会生效"
              >
                有未保存的设置
              </span>
            )}
            {light && coreStatus && (
              <button
                type="button"
                onClick={onToggleCore}
                disabled={coreStatus.coreBusy}
                title={light.hint}
                aria-label={light.hint}
                className="flex shrink-0 items-center gap-2 whitespace-nowrap rounded-full border bg-background px-3 py-1.5 transition-colors hover:bg-muted disabled:cursor-not-allowed disabled:opacity-60"
              >
                <span className={`w-2.5 h-2.5 shrink-0 rounded-full ${light.color}`} />
                <span className="text-xs text-muted-foreground hidden lg:inline">
                  {light.label}
                </span>
              </button>
            )}
            <Link
              to="/settings"
              className="shrink-0 rounded-md p-2 text-muted-foreground hover:bg-muted hover:text-foreground transition-colors"
              aria-label="设置"
              title="设置"
            >
              <Settings className="w-5 h-5" />
            </Link>
          </div>
        </div>
      </div>
    </header>
  );
}
