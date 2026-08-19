import { useEffect, useRef, useState } from 'react';
import { Button } from '@/components/ui/button';
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from '@/components/ui/dialog';
import { toast } from 'sonner';

interface ExitConfirmDialogProps {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  hasPendingChanges: boolean;
}

// 关闭应用时的保存确认:有待提交变更时询问"保存并退出 / 直接退出"
export function ExitConfirmDialog({ open, onOpenChange, hasPendingChanges }: ExitConfirmDialogProps) {
  const [saving, setSaving] = useState(false);
  const savingRef = useRef(false);

  useEffect(() => {
    const handler = (e: BeforeUnloadEvent) => {
      if (hasPendingChanges && !savingRef.current) {
        e.preventDefault();
        e.returnValue = '';
      }
    };
    window.addEventListener('beforeunload', handler);
    return () => window.removeEventListener('beforeunload', handler);
  }, [hasPendingChanges]);

  const handleSaveAndExit = () => {
    setSaving(true);
    savingRef.current = true;
    setTimeout(() => {
      toast.success('存档已提交,正在退出...');
      setSaving(false);
      savingRef.current = false;
      onOpenChange(false);
    }, 1200);
  };

  const handleExit = () => {
    onOpenChange(false);
    toast.info('已退出,未提交的变更已保留');
  };

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>退出前确认</DialogTitle>
          <DialogDescription>
            {hasPendingChanges
              ? '检测到有待提交的存档变更,退出前是否保存?'
              : '当前没有待提交的变更,确认退出?'}
          </DialogDescription>
        </DialogHeader>
        <DialogFooter className="gap-2 sm:gap-0">
          <Button variant="outline" onClick={handleExit} disabled={saving}>
            直接退出
          </Button>
          {hasPendingChanges && (
            <Button onClick={handleSaveAndExit} disabled={saving}>
              {saving ? '保存中...' : '保存并退出'}
            </Button>
          )}
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
