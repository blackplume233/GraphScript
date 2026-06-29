import { FormEvent, useState } from 'react'
import { Undo2, Redo2, Download, RefreshCw, Zap, Circle, Plus, Trash2 } from 'lucide-react'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Separator } from '@/components/ui/separator'
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/ui/tooltip'
import type { Diagnostic, GSState } from '@/api/types'
import GraphSearch, { type GraphSearchResult } from './GraphSearch'

interface ToolbarProps {
  state: GSState | null
  graphIndex: number
  selectedNode: string | null
  activeLogicBlock: { kind: 'event' | 'function'; name: string } | null
  diagnostics: Diagnostic[]
  onGraphChange: (index: number) => void
  onGraphCreate: (name: string) => Promise<void> | void
  onGraphDelete: (name: string) => Promise<void> | void
  onLogicBlockChange: (block: { kind: 'event' | 'function'; name: string } | null) => void
  onSearchNavigate: (result: GraphSearchResult) => Promise<void> | void
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
        <Button
          variant="ghost"
          size="icon"
          onClick={onClick}
          disabled={disabled}
          aria-label={label}
          className="h-7 w-7 text-muted-foreground hover:text-foreground"
        >
          <Icon className="h-3.5 w-3.5" />
        </Button>
      </TooltipTrigger>
      <TooltipContent side="bottom" className="text-[11px]">{label}</TooltipContent>
    </Tooltip>
  )
}

function ToolbarBreadcrumbs({
  graphName,
  activeLogicBlock,
  selectedNode,
}: {
  graphName?: string
  activeLogicBlock: { kind: 'event' | 'function'; name: string } | null
  selectedNode: string | null
}) {
  if (!graphName) return null

  const blockLabel = activeLogicBlock
    ? `${activeLogicBlock.kind === 'event' ? 'event' : 'fn'} ${activeLogicBlock.name}`
    : ''

  return (
    <div
      data-graph-breadcrumb
      className="flex w-[320px] shrink-0 items-center gap-1 text-[11px] text-muted-foreground"
    >
      <span
        data-breadcrumb-graph={graphName}
        className="truncate font-medium text-foreground/85"
      >
        {graphName}
      </span>
      {blockLabel && (
        <>
          <span className="text-muted-foreground/40">/</span>
          <span
            data-breadcrumb-block={blockLabel}
            className="truncate text-foreground/70"
          >
            {blockLabel}
          </span>
        </>
      )}
      {selectedNode && (
        <>
          <span className="text-muted-foreground/40">/</span>
          <span
            data-breadcrumb-node={selectedNode}
            className="truncate text-primary"
          >
            {selectedNode}
          </span>
        </>
      )}
    </div>
  )
}

export default function Toolbar({
  state,
  graphIndex,
  selectedNode,
  activeLogicBlock,
  diagnostics,
  onGraphChange,
  onGraphCreate,
  onGraphDelete,
  onLogicBlockChange,
  onSearchNavigate,
  onUndo,
  onRedo,
  onRefresh,
  onEmit,
}: ToolbarProps) {
  const graphs = state?.module.graphs ?? []
  const graph = graphs[graphIndex]
  const logicBlocks = graph ? [...graph.events, ...graph.functions] : []
  const connected = state !== null
  const activeBlockValue = activeLogicBlock ? `${activeLogicBlock.kind}:${activeLogicBlock.name}` : ''
  const [newGraphName, setNewGraphName] = useState('')

  const createGraph = async (e: FormEvent) => {
    e.preventDefault()
    const name = newGraphName.trim()
    if (!name) return
    await onGraphCreate(name)
    setNewGraphName('')
  }

  return (
    <div className="relative z-[1000] flex items-center gap-1.5 px-3 h-11 border-b bg-card/80 backdrop-blur shrink-0">
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
      <div className="flex items-center gap-1">
        <select
          className="h-7 rounded-md border border-input bg-secondary/50 px-2 text-[11px] font-medium
            focus:outline-none focus:ring-1 focus:ring-ring transition-colors
            text-foreground/80 hover:bg-secondary max-w-[140px]"
          value={graphs.length === 0 ? -1 : graphIndex}
          onChange={e => onGraphChange(Number(e.target.value))}
          disabled={graphs.length === 0}
        >
          {graphs.length === 0 && <option value={-1}>No graphs</option>}
          {graphs.map((g, i) => (
            <option key={i} value={i}>{g.name}</option>
          ))}
        </select>

        <Tooltip>
          <TooltipTrigger asChild>
            <Button
              variant="ghost"
              size="icon"
              onClick={() => graph && onGraphDelete(graph.name)}
              disabled={!graph}
              className="h-7 w-7 text-muted-foreground hover:text-destructive"
            >
              <Trash2 className="h-3.5 w-3.5" />
            </Button>
          </TooltipTrigger>
          <TooltipContent side="bottom" className="text-[11px]">Delete graph</TooltipContent>
        </Tooltip>
      </div>

      <form className="flex items-center gap-1" onSubmit={createGraph}>
        <Input
          value={newGraphName}
          onChange={e => setNewGraphName(e.target.value)}
          placeholder="graph"
          disabled={!connected}
          className="h-7 w-24 px-2 text-[11px] font-mono"
        />
        <Tooltip>
          <TooltipTrigger asChild>
            <Button
              type="submit"
              variant="secondary"
              size="icon"
              aria-label="Create graph"
              disabled={!connected || !newGraphName.trim()}
              className="h-7 w-7"
            >
              <Plus className="h-3.5 w-3.5" />
            </Button>
          </TooltipTrigger>
          <TooltipContent side="bottom" className="text-[11px]">Create graph</TooltipContent>
        </Tooltip>
      </form>

      <Separator orientation="vertical" className="h-4" />

      {/* Logic block selector */}
      <select
        className="h-7 rounded-md border border-input bg-secondary/50 px-2 text-[11px] font-medium
          focus:outline-none focus:ring-1 focus:ring-ring transition-colors
          text-foreground/80 hover:bg-secondary max-w-[150px]"
        value={activeBlockValue}
        onChange={e => {
          const [kind, ...nameParts] = e.target.value.split(':')
          const name = nameParts.join(':')
          if ((kind === 'event' || kind === 'function') && name) {
            onLogicBlockChange({ kind, name })
          } else {
            onLogicBlockChange(null)
          }
        }}
        disabled={logicBlocks.length === 0}
        title="Active event/function for visual edge edits"
      >
        {logicBlocks.length === 0 && <option value="">No block</option>}
        {logicBlocks.map(block => (
          <option key={`${block.kind}:${block.name}`} value={`${block.kind}:${block.name}`}>
            {block.kind === 'event' ? 'event' : 'fn'} {block.name}
          </option>
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

      <Separator orientation="vertical" className="h-4" />

      <GraphSearch
        state={state}
        graphIndex={graphIndex}
        diagnostics={diagnostics}
        onNavigate={onSearchNavigate}
      />

      <ToolbarBreadcrumbs
        graphName={graph?.name}
        activeLogicBlock={activeLogicBlock}
        selectedNode={selectedNode}
      />

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
