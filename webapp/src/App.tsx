import { useCallback, useEffect, useState, useRef } from 'react'
import { ErrorBoundary } from '@/components/ErrorBoundary'
import { TooltipProvider } from '@/components/ui/tooltip'
import {
  fetchState,
  execCommand,
  undo as apiUndo,
  redo as apiRedo,
  fetchEmit,
} from '@/api/client'
import type { GSState } from '@/api/types'
import FlowCanvas from '@/canvas/FlowCanvas'
import Toolbar from '@/panels/Toolbar'
import NodePalette from '@/panels/NodePalette'
import PropertiesPanel from '@/panels/PropertiesPanel'
import CommandLog from '@/panels/CommandLog'

export default function App() {
  const [state, setState] = useState<GSState | null>(null)
  const [graphIndex, setGraphIndex] = useState(0)
  const [selectedNode, setSelectedNode] = useState<string | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [loading, setLoading] = useState(true)
  const retryRef = useRef<ReturnType<typeof setTimeout>>(undefined)

  const refresh = useCallback(async () => {
    try {
      setLoading(true)
      const s = await fetchState()
      setState(s)
      setError(null)
      if (s.active_graph >= 0) setGraphIndex(s.active_graph)
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Connection failed')
    } finally {
      setLoading(false)
    }
  }, [])

  useEffect(() => {
    refresh()
    return () => { if (retryRef.current) clearTimeout(retryRef.current) }
  }, [refresh])

  // Auto-retry connection every 5s when disconnected
  useEffect(() => {
    if (error && !loading) {
      retryRef.current = setTimeout(refresh, 5000)
      return () => { if (retryRef.current) clearTimeout(retryRef.current) }
    }
  }, [error, loading, refresh])

  const handleExec = useCallback(async (cmd: string) => {
    try {
      const res = await execCommand(cmd)
      if (res.state) setState(res.state)
      else await refresh()
    } catch {
      await refresh()
    }
  }, [refresh])

  const handleUndo = useCallback(async () => {
    try {
      const res = await apiUndo()
      if (res.state) setState(res.state)
      else await refresh()
    } catch {
      await refresh()
    }
  }, [refresh])

  const handleRedo = useCallback(async () => {
    try {
      const res = await apiRedo()
      if (res.state) setState(res.state)
      else await refresh()
    } catch {
      await refresh()
    }
  }, [refresh])

  const handleEmit = useCallback(async () => {
    try {
      const text = await fetchEmit()
      const blob = new Blob([text], { type: 'text/plain' })
      const url = URL.createObjectURL(blob)
      const a = document.createElement('a')
      a.href = url
      a.download = (state?.file_path?.split(/[\\/]/).pop() || 'graph') + '.gs'
      a.click()
      URL.revokeObjectURL(url)
    } catch {
      // ignore
    }
  }, [state?.file_path])

  const handleAddNode = useCallback(async (typeName: string) => {
    const id = `${typeName.toLowerCase()}_${Date.now() % 100000}`
    await handleExec(`add_node ${typeName} ${id}`)
  }, [handleExec])

  const handleGraphChange = useCallback(async (index: number) => {
    setGraphIndex(index)
    setSelectedNode(null)
    await handleExec(`switch_graph ${index}`)
  }, [handleExec])

  // Keyboard shortcuts (Ctrl/Cmd support)
  useEffect(() => {
    function onKeyDown(e: KeyboardEvent) {
      const mod = e.ctrlKey || e.metaKey
      if (mod && e.key === 'z') { e.preventDefault(); handleUndo() }
      if (mod && e.key === 'y') { e.preventDefault(); handleRedo() }
    }
    window.addEventListener('keydown', onKeyDown)
    return () => window.removeEventListener('keydown', onKeyDown)
  }, [handleUndo, handleRedo])

  return (
    <TooltipProvider delayDuration={200}>
      <div className="h-screen w-screen flex flex-col overflow-hidden bg-background">
        {/* Top toolbar */}
        <Toolbar
          state={state}
          graphIndex={graphIndex}
          onGraphChange={handleGraphChange}
          onUndo={handleUndo}
          onRedo={handleRedo}
          onRefresh={refresh}
          onEmit={handleEmit}
        />

        {/* Main content */}
        <div className="flex flex-1 min-h-0">
          {/* Left: Node Palette */}
          <div className="w-52 border-r bg-card/60 flex flex-col shrink-0">
            <NodePalette types={state?.types ?? []} onAddNode={handleAddNode} />
          </div>

          {/* Center: Canvas */}
          <div className="flex-1 relative min-w-0">
            {error ? (
              <div className="flex flex-col items-center justify-center h-full gap-4 blueprint-grid">
                <div className="flex flex-col items-center gap-3 p-8 rounded-xl bg-card/80 border border-border/50 backdrop-blur">
                  <div className="w-10 h-10 rounded-full bg-destructive/10 flex items-center justify-center">
                    <div className="w-3 h-3 rounded-full bg-destructive animate-pulse" />
                  </div>
                  <div className="text-sm text-foreground/70 font-medium">
                    Cannot connect to backend
                  </div>
                  <div className="text-[11px] text-muted-foreground/50 text-center max-w-[240px]">
                    {error}
                  </div>
                  <div className="text-[10px] text-muted-foreground/40">
                    Retrying automatically...
                  </div>
                </div>
              </div>
            ) : (
              <ErrorBoundary>
                <FlowCanvas
                  state={state}
                  graphIndex={graphIndex}
                  onNodeSelect={setSelectedNode}
                />
              </ErrorBoundary>
            )}
          </div>

          {/* Right: Properties */}
          <div className="w-52 border-l bg-card/60 flex flex-col shrink-0">
            <PropertiesPanel
              state={state}
              graphIndex={graphIndex}
              selectedNode={selectedNode}
            />
          </div>
        </div>

        {/* Bottom: Command Log */}
        <div className="h-44 border-t bg-card/60 shrink-0">
          <CommandLog
            log={state?.command_log ?? []}
            onExec={handleExec}
          />
        </div>
      </div>
    </TooltipProvider>
  )
}
