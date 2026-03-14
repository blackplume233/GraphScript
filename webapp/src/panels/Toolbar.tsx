import { Undo2, Redo2, Download, RefreshCw, Zap, Circle } from 'lucide-react'
import { Button } from '@/components/ui/button'
import { Separator } from '@/components/ui/separator'
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/ui/tooltip'
import type { GSState } from '@/api/types'

interface ToolbarProps {
  state: GSState | null
  graphIndex: number
  onGraphChange: (index: number) => void
  onUndo: () => void
  onRedo: () => void
  onRefresh: () => void
  onEmit: () => void
}

function ToolbarButton({
  icon: Icon,
  label,
  onClick,
  disabled,
}: {
  icon: React.ComponentType<{ className?: string }>
  label: string
  onClick: () => void
  disabled?: boolean
}) {
  return (
    <Tooltip>
      <TooltipTrigger asChild>
        <Button variant="ghost" size="icon" onClick={onClick} disabled={disabled}
          className="h-7 w-7 text-muted-foreground hover:text-foreground"
        >
          <Icon className="h-3.5 w-3.5" />
        </Button>
      </TooltipTrigger>
      <TooltipContent side="bottom" className="text-[11px]">{label}</TooltipContent>
    </Tooltip>
  )
}

export default function Toolbar({
  state,
  graphIndex,
  onGraphChange,
  onUndo,
  onRedo,
  onRefresh,
  onEmit,
}: ToolbarProps) {
  const graphs = state?.module.graphs ?? []
  const connected = state !== null

  return (
    <div className="flex items-center gap-1.5 px-3 h-11 border-b bg-card/80 backdrop-blur shrink-0">
      {/* Logo + Title */}
      <div className="flex items-center gap-2 mr-2">
        <Zap className="h-4 w-4 text-primary" />
        <span className="text-[13px] font-bold tracking-tight text-foreground/90">
          GraphScript
        </span>
      </div>

      <Separator orientation="vertical" className="h-4" />

      {/* Connection indicator */}
      <Tooltip>
        <TooltipTrigger asChild>
          <div className="flex items-center gap-1.5 px-1.5">
            <Circle
              className="h-2 w-2"
              fill={connected ? 'var(--color-success)' : 'var(--color-destructive)'}
              stroke="none"
            />
            <span className="text-[10px] text-muted-foreground uppercase tracking-wider font-medium">
              {connected ? 'Live' : 'Offline'}
            </span>
          </div>
        </TooltipTrigger>
        <TooltipContent side="bottom" className="text-[11px]">
          {connected ? 'Connected to backend' : 'Backend not responding'}
        </TooltipContent>
      </Tooltip>

      <Separator orientation="vertical" className="h-4" />

      {/* Graph selector */}
      <select
        className="h-7 rounded-md border border-input bg-secondary/50 px-2 text-[11px] font-medium
          focus:outline-none focus:ring-1 focus:ring-ring transition-colors
          text-foreground/80 hover:bg-secondary"
        value={graphIndex}
        onChange={e => onGraphChange(Number(e.target.value))}
        disabled={graphs.length === 0}
      >
        {graphs.length === 0 && <option value={-1}>No graphs</option>}
        {graphs.map((g, i) => (
          <option key={i} value={i}>{g.name}</option>
        ))}
      </select>

      <Separator orientation="vertical" className="h-4" />

      {/* Undo / Redo */}
      <div className="flex items-center gap-0.5">
        <ToolbarButton icon={Undo2} label="Undo (Ctrl+Z)" onClick={onUndo} disabled={!state?.can_undo} />
        <ToolbarButton icon={Redo2} label="Redo (Ctrl+Y)" onClick={onRedo} disabled={!state?.can_redo} />
      </div>

      <Separator orientation="vertical" className="h-4" />

      <ToolbarButton icon={RefreshCw} label="Refresh" onClick={onRefresh} />
      <ToolbarButton icon={Download} label="Export .gs" onClick={onEmit} />

      {/* Right side: status */}
      <div className="flex-1" />

      {state?.dirty && (
        <div className="flex items-center gap-1 px-2 py-0.5 rounded bg-warning/10 border border-warning/20">
          <div className="w-1.5 h-1.5 rounded-full bg-warning" />
          <span className="text-[10px] text-warning font-medium">Modified</span>
        </div>
      )}

      {state?.file_path && (
        <span className="text-[10px] text-muted-foreground/60 truncate max-w-[180px] font-mono">
          {state.file_path.split(/[\\/]/).pop()}
        </span>
      )}
    </div>
  )
}
