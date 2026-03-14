import { useRef, useCallback, useEffect, useMemo, useState } from 'react'
import {
  FreeLayoutEditor,
  type FreeLayoutPluginContext,
  type FreeLayoutProps,
  type WorkflowNodeRegistry,
} from '@flowgram.ai/free-layout-editor'
import '@flowgram.ai/free-layout-editor/index.css'
import type { GSState, GraphDef } from '@/api/types'
import { graphToFlowData, type FlowGraphData } from './gs-to-flowgram'
import { buildNodeRegistries } from './node-registries'
import BlueprintNode from './BlueprintNode'

interface FlowCanvasProps {
  state: GSState | null
  graphIndex: number
  onNodeSelect?: (instanceName: string | null) => void
}

export default function FlowCanvas({ state, graphIndex, onNodeSelect }: FlowCanvasProps) {
  const editorRef = useRef<FreeLayoutPluginContext>(null)
  const [ready, setReady] = useState(false)

  const graph: GraphDef | undefined = state?.module.graphs[graphIndex]

  const flowData: FlowGraphData | null = useMemo(() => {
    if (!graph || !state) return null
    return graphToFlowData(graph, state)
  }, [graph, state])

  const nodeRegistries: WorkflowNodeRegistry[] = useMemo(() => {
    if (!state) return []
    return buildNodeRegistries(state.types)
  }, [state?.types])

  const initialData = useMemo(() => {
    if (!flowData) return { nodes: [], edges: [] }
    return flowData
  }, [flowData])

  const editorProps: FreeLayoutProps = useMemo(() => ({
    nodeRegistries,
    materials: {
      renderDefaultNode: () => <BlueprintNode />,
    },
    lineColor: {
      default: '#4a5a7a',
      drawing: '#7aafff',
      hovered: '#7aafff',
      selected: '#7aafff',
      hidden: 'transparent',
      error: '#e64a19',
      flowing: '#ff6b35',
    },
    history: {
      disableShortcuts: true,
    },
    onContentChange: () => {
      // future: sync canvas changes back to backend
    },
    initialData,
  }), [nodeRegistries, initialData, onNodeSelect])

  const handleReady = useCallback((ctx: FreeLayoutPluginContext) => {
    setReady(true)

    // Wire up node selection
    try {
      const sel = ctx.selection as unknown as Record<string, unknown>
      const method = (sel?.onSelectionChanged ?? sel?.onChange) as
        ((cb: () => void) => { dispose: () => void }) | undefined
      if (method) {
        method(() => {
          const getNodes = (sel?.getSelectedNodes ?? sel?.getNodes) as
            (() => Array<{ id: string }>) | undefined
          const nodes = getNodes?.() ?? []
          if (nodes.length > 0 && onNodeSelect) {
            onNodeSelect(nodes[0].id)
          } else if (onNodeSelect) {
            onNodeSelect(null)
          }
        })
      }
    } catch {
      // selection service may have different API shape
    }
  }, [onNodeSelect])

  useEffect(() => {
    if (!ready || !editorRef.current || !flowData) return
    try {
      editorRef.current.document.fromJSON(flowData)
    } catch {
      // ignore when editor not fully initialized
    }
  }, [flowData, ready])

  if (!state || !graph) {
    return (
      <div className="flex flex-col items-center justify-center h-full gap-3 blueprint-grid">
        <div className="flex flex-col items-center gap-2 p-8 rounded-xl bg-card/80 border border-border/50 backdrop-blur">
          <svg width="48" height="48" viewBox="0 0 48 48" fill="none" className="opacity-30 mb-2">
            <rect x="4" y="8" width="18" height="14" rx="2" stroke="currentColor" strokeWidth="1.5" />
            <rect x="26" y="26" width="18" height="14" rx="2" stroke="currentColor" strokeWidth="1.5" />
            <path d="M22 15 H30 V33" stroke="currentColor" strokeWidth="1.5" strokeDasharray="3 2" />
          </svg>
          <span className="text-sm text-muted-foreground">No graph loaded</span>
          <span className="text-xs text-muted-foreground/60">
            Start the server: <code className="px-1.5 py-0.5 bg-secondary rounded text-[11px] font-mono">gs serve</code>
          </span>
        </div>
      </div>
    )
  }

  return (
    <div className="w-full h-full relative blueprint-grid">
      <FreeLayoutEditor
        {...editorProps}
        ref={editorRef}
        onReady={handleReady}
      />
    </div>
  )
}
