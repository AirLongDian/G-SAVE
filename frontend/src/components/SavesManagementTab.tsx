import { useState, useRef, useEffect, useCallback } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import { Textarea } from '@/components/ui/textarea';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle, DialogTrigger } from '@/components/ui/dialog';
import {
  GitBranch,
  GitCommit,
  AlertTriangle,
  Upload,
  RotateCcw,
  Clock,
  FileText,
  Gamepad2,
  Trophy,
  Layers,
  Plus,
} from 'lucide-react';
import type { SaveVersion } from '@/types/game';
import { toast } from 'sonner';

interface SavesManagementTabProps {
  gameId: string;
}

// 模拟存档解析数据(由支持包 parse() 提供,对应契约 history[].slots)
interface SaveData {
  slotName: string;
  characterName: string;
  level: number;
  playTime: string;
  location: string;
}

const mockSaveDataMap: Record<string, SaveData> = {
  'a1b2c3d': { slotName: 'Slot 1', characterName: '褪色者·艾尔登', level: 150, playTime: '128小时', location: '艾尔登法环王座' },
  'e5f6g7h': { slotName: 'Slot 2', characterName: '流浪骑士', level: 85, playTime: '56小时', location: '利耶尼亚湖' },
  'g7h8i9j': { slotName: 'Slot 1', characterName: '法师build', level: 120, playTime: '92小时', location: '火山官邸' },
  'b2c3d4e': { slotName: 'Slot 1', characterName: '褪色者·艾尔登', level: 148, playTime: '125小时', location: '史东薇尔城' },
  'f6g7h8i': { slotName: 'Slot 2', characterName: '流浪骑士', level: 83, playTime: '54小时', location: '盖利德' },
  'd4e5f6g': { slotName: 'Slot 1', characterName: '褪色者·艾尔登', level: 140, playTime: '118小时', location: '圆桌厅堂' },
};

// 模拟提交历史(对应契约 history[],按时间倒序)
const mockCommits: (SaveVersion & { branches: string[] })[] = [
  {
    hash: 'a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6q7r8s9t0',
    shortHash: 'a1b2c3d',
    message: '自动保存 - 击败Boss后',
    timestamp: '2024-01-15T14:30:00Z',
    branch: 'main',
    isHead: true,
    branches: ['main'],
  },
  {
    hash: 'e5f6g7h8i9j0k1l2m3n4o5p6q7r8s9t0u1v2w3x4',
    shortHash: 'e5f6g7h',
    message: '自动保存 - 探索新地图',
    timestamp: '2024-01-15T10:00:00Z',
    branch: 'laptop-device',
    isHead: false,
    branches: ['laptop-device'],
  },
  {
    hash: 'g7h8i9j0k1l2m3n4o5p6q7r8s9t0u1v2w3x4y5z6',
    shortHash: 'g7h8i9j',
    message: '自动保存 - 完成支线任务',
    timestamp: '2024-01-15T08:45:00Z',
    branch: 'steam-deck',
    isHead: false,
    branches: ['steam-deck'],
  },
  {
    hash: 'b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6q7r8s9t0u1',
    shortHash: 'b2c3d4e',
    message: '手动保存 - 重要剧情前',
    timestamp: '2024-01-14T20:15:00Z',
    branch: 'main',
    isHead: false,
    branches: ['main', 'laptop-device'],
  },
  {
    hash: 'f6g7h8i9j0k1l2m3n4o5p6q7r8s9t0u1v2w3x4y5',
    shortHash: 'f6g7h8i',
    message: '手动保存 - Boss战前',
    timestamp: '2024-01-14T22:30:00Z',
    branch: 'laptop-device',
    isHead: false,
    branches: ['laptop-device'],
  },
  {
    hash: 'd4e5f6g7h8i9j0k1l2m3n4o5p6q7r8s9t0u1v2w3',
    shortHash: 'd4e5f6g',
    message: '冲突解决 - 从laptop-device同步',
    timestamp: '2024-01-12T16:20:00Z',
    branch: 'main',
    isHead: false,
    hasConflict: true,
    branches: ['main'],
  },
];

// 契约 suggestedBranchName:形如 save/20260818-1a2b3c4d
function suggestedBranchName(shortHash: string): string {
  const d = new Date();
  const ymd = `${d.getFullYear()}${String(d.getMonth() + 1).padStart(2, '0')}${String(d.getDate()).padStart(2, '0')}`;
  return `save/${ymd}-${shortHash}`;
}

export function SavesManagementTab({ gameId }: SavesManagementTabProps) {
  const [selected, setSelected] = useState<(SaveVersion & { branches: string[] }) | null>(null);
  const [commitMessage, setCommitMessage] = useState('');
  const [isSubmitting, setIsSubmitting] = useState(false);
  const [isPushing, setIsPushing] = useState(false);
  const [branchName, setBranchName] = useState('');
  const iframeRef = useRef<HTMLIFrameElement>(null);
  const iframeReady = useRef(false);

  const sendCommitsToIframe = useCallback(() => {
    iframeRef.current?.contentWindow?.postMessage({ type: 'render-commits', commits: mockCommits }, '*');
  }, []);

  // 选中提交(统一入口,同步高亮 iframe 中的节点)
  const handleSelect = useCallback((commit: (SaveVersion & { branches: string[] })) => {
    setSelected(commit);
    iframeRef.current?.contentWindow?.postMessage({ type: 'select-commit', hash: commit.hash }, '*');
  }, []);

  useEffect(() => {
    const handleMessage = (event: MessageEvent) => {
      if (!event.data) return;
      if (event.data.type === 'gitgraph-ready') {
        iframeReady.current = true;
        sendCommitsToIframe();
        // 恢复之前的选中状态
        if (selected) {
          iframeRef.current?.contentWindow?.postMessage({ type: 'select-commit', hash: selected.hash }, '*');
        }
      }
      // iframe 中点击节点回传
      if (event.data.type === 'commit-selected') {
        const commit = mockCommits.find((c) => c.hash === event.data.hash);
        if (commit) setSelected(commit);
      }
    };
    window.addEventListener('message', handleMessage);
    return () => window.removeEventListener('message', handleMessage);
  }, [selected, sendCommitsToIframe]);

  const handleSubmit = () => {
    if (!commitMessage.trim()) {
      toast.error('请填写提交信息');
      return;
    }
    setIsSubmitting(true);
    setTimeout(() => {
      setIsSubmitting(false);
      setCommitMessage('');
      toast.success('存档已提交到本地仓库');
    }, 1500);
  };

  const handlePush = () => {
    setIsPushing(true);
    setTimeout(() => {
      setIsPushing(false);
      toast.success('已推送到远程仓库');
    }, 2000);
  };

  // 契约 openCommitAsBranch:打开旧版本的唯一方式(建分支并切换,禁止裸checkout)
  const handleOpenAsBranch = () => {
    if (!selected) return;
    const name = branchName.trim() || suggestedBranchName(selected.shortHash);
    toast.info(`正在以 ${selected.shortHash} 建立新存档线...`);
    setTimeout(() => {
      toast.success(`已建立并切换到新存档线: ${name}`);
      setBranchName('');
    }, 1000);
  };

  // 契约 restoreVersion:恢复到当前存档线,历史保持线性
  const handleRestore = () => {
    if (!selected) return;
    toast.info(`正在恢复 ${selected.shortHash} 到当前存档线...`);
    setTimeout(() => {
      toast.success(`已恢复 ${selected.shortHash},历史保持线性`);
    }, 1000);
  };

  // 冲突解决
  const handleResolveConflict = () => {
    if (!selected) return;
    toast.info(`正在为冲突commit ${selected.shortHash} 创建解决分支...`);
    setTimeout(() => {
      toast.success(`已创建解决分支: resolve-${selected.shortHash},请在新分支中处理冲突`);
    }, 1200);
  };

  return (
    <div className="space-y-6">
      {/* 操作工具栏 */}
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <GitCommit className="w-5 h-5" />
            存档操作
          </CardTitle>
          <CardDescription>手动提交存档变更或推送到远程仓库</CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          <div className="space-y-2">
            <Label htmlFor="commit-msg">提交信息</Label>
            <Textarea
              id="commit-msg"
              placeholder="描述本次存档变更..."
              value={commitMessage}
              onChange={(e) => setCommitMessage(e.target.value)}
              rows={3}
            />
          </div>
          <div className="flex gap-2 flex-wrap">
            <Dialog>
              <DialogTrigger asChild>
                <Button className="gap-2">
                  <FileText className="w-4 h-4" />
                  提交到本地
                </Button>
              </DialogTrigger>
              <DialogContent>
                <DialogHeader>
                  <DialogTitle>确认提交</DialogTitle>
                  <DialogDescription>将当前存档状态提交到本地Git仓库</DialogDescription>
                </DialogHeader>
                <div className="py-4">
                  <p className="text-sm text-muted-foreground">提交信息: {commitMessage || '(未填写)'}</p>
                </div>
                <DialogFooter>
                  <Button variant="outline" onClick={() => setCommitMessage('')}>取消</Button>
                  <Button onClick={handleSubmit} disabled={isSubmitting}>
                    {isSubmitting ? '提交中...' : '确认提交'}
                  </Button>
                </DialogFooter>
              </DialogContent>
            </Dialog>
            <Button variant="default" onClick={handlePush} disabled={isPushing} className="gap-2">
              {isPushing ? <Upload className="w-4 h-4 animate-spin" /> : <Upload className="w-4 h-4" />}
              {isPushing ? '推送中...' : '推送到远程'}
            </Button>
          </div>
        </CardContent>
      </Card>

      {/* 选中提交的存档数据(位于提交历史上方) */}
      {selected && (
        <Card className="border-primary/30 bg-primary/5">
          <CardHeader className="pb-3">
            <CardTitle className="flex items-center gap-2 text-base">
              <Gamepad2 className="w-4 h-4" />
              存档数据 - {selected.shortHash}
            </CardTitle>
            <CardDescription>由支持包解析的存档信息,可执行版本操作</CardDescription>
          </CardHeader>
          <CardContent className="space-y-4">
            {mockSaveDataMap[selected.shortHash] ? (
              <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
                <div className="space-y-1">
                  <div className="flex items-center gap-1.5 text-xs text-muted-foreground"><Layers className="w-3 h-3" />存档槽位</div>
                  <p className="font-medium text-sm">{mockSaveDataMap[selected.shortHash].slotName}</p>
                </div>
                <div className="space-y-1">
                  <div className="flex items-center gap-1.5 text-xs text-muted-foreground"><Gamepad2 className="w-3 h-3" />角色名</div>
                  <p className="font-medium text-sm">{mockSaveDataMap[selected.shortHash].characterName}</p>
                </div>
                <div className="space-y-1">
                  <div className="flex items-center gap-1.5 text-xs text-muted-foreground"><Trophy className="w-3 h-3" />角色等级</div>
                  <p className="font-medium text-sm">Lv.{mockSaveDataMap[selected.shortHash].level}</p>
                </div>
                <div className="space-y-1">
                  <div className="flex items-center gap-1.5 text-xs text-muted-foreground"><Clock className="w-3 h-3" />游戏时长</div>
                  <p className="font-medium text-sm">{mockSaveDataMap[selected.shortHash].playTime}</p>
                </div>
                <div className="col-span-2 md:col-span-4 space-y-1 pt-2 border-t">
                  <div className="flex items-center gap-1.5 text-xs text-muted-foreground"><Gamepad2 className="w-3 h-3" />当前位置</div>
                  <p className="font-medium text-sm">{mockSaveDataMap[selected.shortHash].location}</p>
                </div>
              </div>
            ) : (
              <p className="text-sm text-muted-foreground">该提交未解析到存档槽位。</p>
            )}

            {/* 操作区 */}
            <div className="flex gap-2 flex-wrap pt-2 border-t">
              <Dialog>
                <DialogTrigger asChild>
                  <Button className="gap-2" onClick={() => setBranchName(suggestedBranchName(selected.shortHash))}>
                    <Plus className="w-4 h-4" />
                    以此版本开新存档线
                  </Button>
                </DialogTrigger>
                <DialogContent>
                  <DialogHeader>
                    <DialogTitle>开新存档线</DialogTitle>
                    <DialogDescription>在 {selected.shortHash} 处建立新分支并切换(打开旧版本的唯一方式)</DialogDescription>
                  </DialogHeader>
                  <div className="space-y-2 py-4">
                    <Label htmlFor="branch-name">新存档线名称</Label>
                    <Input id="branch-name" value={branchName} onChange={(e) => setBranchName(e.target.value)} />
                  </div>
                  <DialogFooter>
                    <Button onClick={handleOpenAsBranch}>建立并切换</Button>
                  </DialogFooter>
                </DialogContent>
              </Dialog>

              <Button variant="outline" onClick={handleRestore} className="gap-2">
                <RotateCcw className="w-4 h-4" />
                恢复到当前存档线
              </Button>

              {selected.hasConflict && (
                <Button variant="destructive" onClick={handleResolveConflict} className="gap-2">
                  <AlertTriangle className="w-4 h-4" />
                  解决冲突
                </Button>
              )}
            </div>
          </CardContent>
        </Card>
      )}

      {/* 提交历史:图与列表合并为统一视图 */}
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <GitBranch className="w-5 h-5" />
            提交历史
          </CardTitle>
          <CardDescription>共 {mockCommits.length} 个提交,直接点击图中节点或提交信息即可选中</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="rounded-lg border bg-background overflow-auto h-[420px]">
            <iframe
              ref={iframeRef}
              src="/git-graph-viewer.html"
              className="w-full h-full border-0"
              title="Git Graph 可视化"
            />
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
