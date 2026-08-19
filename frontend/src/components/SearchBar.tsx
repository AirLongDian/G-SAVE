import { Input } from '@/components/ui/input';
import { Search } from 'lucide-react';

interface SearchBarProps {
  value: string;
  onChange: (value: string) => void;
  placeholder?: string;
}

export function SearchBar({ value, onChange, placeholder = '搜索游戏...' }: SearchBarProps) {
  return (
    // Capped and shrinkable so it never pushes the core status light onto a
    // second line in the title bar.
    <div className="relative w-full max-w-[260px] min-w-[120px]">
      <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-muted-foreground" />
      <Input
        type="text"
        placeholder={placeholder}
        value={value}
        onChange={(e) => onChange(e.target.value)}
        className="pl-10 h-9"
      />
    </div>
  );
}
