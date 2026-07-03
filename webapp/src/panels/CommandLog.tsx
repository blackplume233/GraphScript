import { useState, useRef, useEffect } from 'react'
import { Terminal, Send, ChevronRight } from 'lucide-react'
import { Input } from '@/components/ui/input'
import { ScrollArea } from '@/components/ui/scroll-area'
import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import SourceDiagnosticsOutput from './SourceDiagnosticsOutput'
import type { SourceDiagnosticsEnvironment } from '@/api/types'

interface CommandLogProps {
  log: string[]
  onExec: (command: string) => void
  sourceEnvironmentNotice: string
  sourceResolverEnvironment: SourceDiagnosticsEnvironment | null
  sessionImports: string[]
  sourceBusy: boolean
  onImportPlan: (commands: string[]) => void
  onOpenDeclarationSource: (path: string, contentHash: string) => void
}

export default function CommandLog({
  log,
  onExec,
  sourceEnvironmentNotice,
  sourceResolverEnvironment,
  sessionImports,
  sourceBusy,
  onImportPlan,
  onOpenDeclarationSource,
}: CommandLogProps) {
  const [input, setInput] = useState('')
  const [history, setHistory] = useState<string[]>([])
  const [historyIndex, setHistoryIndex] = useState(-1)
  const bottomRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [log.length])

  function handleSubmit() {
    if (!input.trim()) return
    const cmd = input.trim()
    onExec(cmd)
    setHistory(prev => [cmd, ...prev])
    setHistoryIndex(-1)
    setInput('')
  }

  function handleKeyDown(e: React.KeyboardEvent) {
    if (e.key === 'Enter') {
      e.preventDefault()
      handleSubmit()
    } else if (e.key === 'ArrowUp') {
      e.preventDefault()
      if (historyIndex < history.length - 1) {
        const newIdx = historyIndex + 1
        setHistoryIndex(newIdx)
        setInput(history[newIdx])
      }
    } else if (e.key === 'ArrowDown') {
      e.preventDefault()
      if (historyIndex > 0) {
        const newIdx = historyIndex - 1
        setHistoryIndex(newIdx)
        setInput(history[newIdx])
      } else {
        setHistoryIndex(-1)
        setInput('')
      }
    }
  }

  return (
    <div className="flex flex-col h-full">
      {/* Header */}
      <div className="flex items-center gap-2 px-3 py-1.5 border-b shrink-0">
        <Terminal className="h-3.5 w-3.5 text-primary/70" />
        <span className="text-[11px] font-semibold text-muted-foreground uppercase tracking-wider">
          Console
        </span>
        <Badge variant="secondary" className="text-[9px] px-1 py-0 ml-auto h-4 tabular-nums">
          {log.length}
        </Badge>
      </div>

      <SourceDiagnosticsOutput
        environmentNotice={sourceEnvironmentNotice}
        resolverEnvironment={sourceResolverEnvironment}
        sessionImports={sessionImports}
        busy={sourceBusy}
        onImportCommand={onExec}
        onImportPlan={onImportPlan}
        onOpenDeclarationSource={onOpenDeclarationSource}
      />

      {/* Log area */}
      <ScrollArea className="flex-1 min-h-0">
        <div className="p-2 space-y-px font-mono text-[11px]">
          {log.map((cmd, i) => (
            <div
              key={`${i}-${cmd.slice(0, 20)}`}
              className="flex gap-1.5 py-[2px] hover:bg-accent/30 rounded px-1 transition-colors"
            >
              <ChevronRight className="h-3.5 w-3.5 text-primary/40 shrink-0 mt-px" />
              <span className="text-foreground/70 break-all leading-relaxed">{cmd}</span>
            </div>
          ))}
          {log.length === 0 && (
            <div className="text-muted-foreground/30 text-center py-6 text-[11px]">
              Type a CLI command below...
            </div>
          )}
          <div ref={bottomRef} />
        </div>
      </ScrollArea>

      {/* Input area */}
      <div className="p-2 border-t shrink-0">
        <div className="flex items-center gap-1.5">
          <ChevronRight className="h-3.5 w-3.5 text-primary/50 shrink-0" />
          <Input
            placeholder="add_node PrintString ps1"
            value={input}
            onChange={e => setInput(e.target.value)}
            onKeyDown={handleKeyDown}
            className="h-7 text-[11px] font-mono bg-secondary/30 border-border/40
              placeholder:text-muted-foreground/25 focus-visible:ring-primary/30"
          />
          <Button
            variant="ghost"
            size="icon"
            className="h-7 w-7 shrink-0 text-muted-foreground hover:text-primary"
            onClick={handleSubmit}
            disabled={!input.trim()}
          >
            <Send className="h-3 w-3" />
          </Button>
        </div>
      </div>
    </div>
  )
}
