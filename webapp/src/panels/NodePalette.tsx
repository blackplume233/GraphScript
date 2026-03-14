import { useState, useMemo } from 'react'
import { Search, Boxes } from 'lucide-react'
import { Input } from '@/components/ui/input'
import { Badge } from '@/components/ui/badge'
import { ScrollArea } from '@/components/ui/scroll-area'
import type { NodeTypeDef } from '@/api/types'

interface NodePaletteProps {
  types: NodeTypeDef[]
  onAddNode: (typeName: string) => void
}

function groupByCategory(types: NodeTypeDef[]): Record<string, NodeTypeDef[]> {
  const groups: Record<string, NodeTypeDef[]> = {}
  for (const t of types) {
    const hasExec = t.pins.some(p => p.kind === 'exec')
    const cat = t.tags.length > 0 ? t.tags[0] : hasExec ? 'Flow' : 'Pure'
    if (!groups[cat]) groups[cat] = []
    groups[cat].push(t)
  }
  const sorted: Record<string, NodeTypeDef[]> = {}
  for (const key of Object.keys(groups).sort()) {
    sorted[key] = groups[key].sort((a, b) => a.type_name.localeCompare(b.type_name))
  }
  return sorted
}

function PinPreview({ kind, type }: { kind: string; type: string }) {
  const isExec = kind === 'exec'
  return (
    <div
      className="w-1.5 h-1.5 rounded-full"
      title={`${kind}: ${type}`}
      style={{
        background: isExec ? 'var(--color-exec)' : 'var(--color-data-object)',
      }}
    />
  )
}

export default function NodePalette({ types, onAddNode }: NodePaletteProps) {
  const [query, setQuery] = useState('')

  const filtered = useMemo(() => {
    if (!query) return types
    const q = query.toLowerCase()
    return types.filter(t => t.type_name.toLowerCase().includes(q))
  }, [types, query])

  const groups = useMemo(() => groupByCategory(filtered), [filtered])

  return (
    <div className="flex flex-col h-full">
      {/* Header */}
      <div className="flex items-center gap-2 px-3 py-2 border-b">
        <Boxes className="h-3.5 w-3.5 text-primary/70" />
        <span className="text-[11px] font-semibold text-muted-foreground uppercase tracking-wider">
          Nodes
        </span>
        <Badge variant="secondary" className="text-[9px] px-1 py-0 ml-auto h-4">
          {types.length}
        </Badge>
      </div>

      {/* Search */}
      <div className="p-2 border-b">
        <div className="relative">
          <Search className="absolute left-2 top-1/2 -translate-y-1/2 h-3 w-3 text-muted-foreground/50" />
          <Input
            placeholder="Search..."
            value={query}
            onChange={e => setQuery(e.target.value)}
            className="pl-7 h-7 text-[11px] bg-secondary/30 border-border/50 placeholder:text-muted-foreground/40"
          />
        </div>
      </div>

      {/* Node list */}
      <ScrollArea className="flex-1">
        <div className="p-2 space-y-3 panel-enter">
          {Object.entries(groups).map(([cat, items]) => (
            <div key={cat}>
              <div className="text-[9px] uppercase text-muted-foreground/60 font-bold mb-1.5 tracking-[0.1em] px-1">
                {cat}
              </div>
              <div className="space-y-px">
                {items.map(t => (
                  <button
                    key={t.type_name}
                    className="flex items-center gap-2 w-full px-2 py-1.5 rounded-md text-[11px]
                      hover:bg-accent/60 text-left transition-all duration-100
                      text-foreground/75 hover:text-foreground active:scale-[0.98]"
                    onClick={() => onAddNode(t.type_name)}
                    title={`Add ${t.type_name}`}
                  >
                    {/* Mini pin preview */}
                    <div className="flex gap-[2px] shrink-0">
                      {t.pins.slice(0, 4).map((p, i) => (
                        <PinPreview key={i} kind={p.kind} type={p.type} />
                      ))}
                    </div>
                    <span className="truncate flex-1 font-medium">{t.type_name}</span>
                    <span className="text-[9px] text-muted-foreground/40 tabular-nums">{t.pins.length}</span>
                  </button>
                ))}
              </div>
            </div>
          ))}

          {filtered.length === 0 && (
            <div className="text-[11px] text-muted-foreground/50 text-center py-8">
              No matching nodes
            </div>
          )}
        </div>
      </ScrollArea>
    </div>
  )
}
