import { Info } from 'lucide-react'
import { Badge } from '@/components/ui/badge'
import { Separator } from '@/components/ui/separator'
import type { GSState, GraphDef, NodeInst } from '@/api/types'
import { pinColor } from '@/canvas/port-config'

interface PropertiesPanelProps {
  state: GSState | null
  graphIndex: number
  selectedNode: string | null
}

function PropRow({ label, value, mono }: { label: string; value: string | number; mono?: boolean }) {
  return (
    <div className="flex items-center justify-between py-1">
      <span className="text-[10px] text-muted-foreground/70">{label}</span>
      <span className={`text-[11px] text-foreground/80 truncate max-w-[120px] ${mono ? 'font-mono text-[10px]' : ''}`}>
        {value}
      </span>
    </div>
  )
}

function GraphInfo({ graph }: { graph: GraphDef }) {
  return (
    <div className="space-y-3 panel-enter">
      <div className="space-y-0.5">
        <PropRow label="Name" value={graph.name} mono />
        {graph.base_type && <PropRow label="Base" value={graph.base_type} mono />}
        <PropRow label="Nodes" value={graph.nodes.length} />
        <PropRow label="Events" value={graph.events.length} />
        <PropRow label="Functions" value={graph.functions.length} />
      </div>

      {graph.parameters.length > 0 && (
        <>
          <Separator className="opacity-50" />
          <div>
            <div className="text-[9px] uppercase text-muted-foreground/50 font-bold tracking-[0.1em] mb-1.5">
              Parameters
            </div>
            <div className="space-y-1">
              {graph.parameters.map(p => (
                <div key={p.name} className="flex items-center gap-1.5 py-0.5">
                  <Badge variant="outline"
                    className="text-[8px] px-1 py-0 h-3.5 border-border/50 text-muted-foreground/60"
                  >
                    {p.direction}
                  </Badge>
                  <span className="text-[11px] font-mono text-foreground/80">{p.name}</span>
                  <span className="text-[10px] text-muted-foreground/40 ml-auto font-mono">{p.type}</span>
                </div>
              ))}
            </div>
          </div>
        </>
      )}
    </div>
  )
}

function NodeInfo({ node, state }: { node: NodeInst; state: GSState }) {
  const def = state.types.find(t => t.type_name === node.type)

  return (
    <div className="space-y-3 panel-enter">
      <div className="space-y-0.5">
        <PropRow label="Instance" value={node.instance} mono />
        <PropRow label="Type" value={node.type} mono />
        {node.init && <PropRow label="Init" value={node.init} mono />}
      </div>

      {def && def.pins.length > 0 && (
        <>
          <Separator className="opacity-50" />
          <div>
            <div className="text-[9px] uppercase text-muted-foreground/50 font-bold tracking-[0.1em] mb-1.5">
              Pins
            </div>
            <div className="space-y-0.5">
              {def.pins.map(p => (
                <div key={`${p.direction}-${p.name}`} className="flex items-center gap-1.5 py-0.5">
                  <div
                    className="w-2 h-2 rounded-full shrink-0"
                    style={{
                      backgroundColor: pinColor(p.type, p.kind),
                      boxShadow: `0 0 4px ${pinColor(p.type, p.kind)}40`,
                    }}
                  />
                  <Badge variant="outline"
                    className="text-[8px] px-1 py-0 h-3.5 border-border/50 text-muted-foreground/60"
                  >
                    {p.direction}
                  </Badge>
                  <span className="text-[11px] font-mono text-foreground/80">{p.name}</span>
                  {p.type && (
                    <span className="text-[9px] text-muted-foreground/40 ml-auto font-mono">{p.type}</span>
                  )}
                </div>
              ))}
            </div>
          </div>
        </>
      )}
    </div>
  )
}

export default function PropertiesPanel({ state, graphIndex, selectedNode }: PropertiesPanelProps) {
  const graph = state?.module.graphs[graphIndex]
  const node = graph?.nodes.find(n => n.instance === selectedNode)

  return (
    <div className="h-full overflow-auto">
      {/* Header */}
      <div className="flex items-center gap-2 px-3 py-2 border-b">
        <Info className="h-3.5 w-3.5 text-primary/70" />
        <span className="text-[11px] font-semibold text-muted-foreground uppercase tracking-wider">
          {node ? 'Node' : 'Graph'}
        </span>
      </div>

      {/* Content */}
      <div className="p-3">
        {!state || !graph ? (
          <div className="text-[11px] text-muted-foreground/40 text-center py-8">
            No data
          </div>
        ) : node ? (
          <NodeInfo node={node} state={state} />
        ) : (
          <GraphInfo graph={graph} />
        )}
      </div>
    </div>
  )
}
