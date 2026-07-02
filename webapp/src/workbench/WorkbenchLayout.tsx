import { createContext, useCallback, useContext, useEffect, useMemo, useRef } from 'react'
import type { FunctionComponent, ReactNode } from 'react'
import { DockviewReact } from 'dockview-react'
import type { DockviewIDisposable } from 'dockview-react'
import type { DockviewApi, DockviewReadyEvent, IDockviewPanelProps, SerializedDockview } from 'dockview-react'

const LAYOUT_STORAGE_KEY = 'graphscript.workbench.layout.v2'

type WorkbenchPanelId =
  | 'palette'
  | 'canvas'
  | 'properties'
  | 'diagnostics'
  | 'graphText'
  | 'source'
  | 'console'

interface WorkbenchLayoutProps {
  palette: ReactNode
  canvas: ReactNode
  properties: ReactNode
  diagnostics: ReactNode
  graphText: ReactNode
  source: ReactNode
  console: ReactNode
  activePanelRequest?: { panel: WorkbenchPanelId; nonce: number } | null
}

type WorkbenchPanels = Record<WorkbenchPanelId, ReactNode>

const WorkbenchPanelsContext = createContext<WorkbenchPanels | null>(null)
const REQUIRED_PANEL_IDS: WorkbenchPanelId[] = [
  'palette',
  'canvas',
  'properties',
  'diagnostics',
  'graphText',
  'source',
  'console',
]

function PanelHost({ panelId }: { panelId: WorkbenchPanelId }) {
  const panels = useContext(WorkbenchPanelsContext)
  return (
    <div className="h-full min-h-0 w-full overflow-hidden bg-card/60">
      {panels?.[panelId] ?? null}
    </div>
  )
}

function loadStoredLayout(): SerializedDockview | null {
  try {
    const raw = window.localStorage.getItem(LAYOUT_STORAGE_KEY)
    return raw ? JSON.parse(raw) as SerializedDockview : null
  } catch {
    return null
  }
}

function saveStoredLayout(api: DockviewApi) {
  try {
    window.localStorage.setItem(LAYOUT_STORAGE_KEY, JSON.stringify(api.toJSON()))
  } catch {
    // Layout persistence is best-effort UI state; graph/session state stays backend-owned.
  }
}

function createDefaultLayout(api: DockviewApi) {
  api.addPanel({ id: 'canvas', component: 'canvas', title: 'Canvas' })
  api.addPanel({
    id: 'source',
    component: 'source',
    title: 'Source',
    initialWidth: 430,
    position: { referencePanel: 'canvas', direction: 'left' },
  })
  api.addPanel({
    id: 'palette',
    component: 'palette',
    title: 'Palette',
    inactive: true,
    position: { referencePanel: 'source', direction: 'within' },
  })
  api.addPanel({
    id: 'properties',
    component: 'properties',
    title: 'Properties',
    initialWidth: 280,
    position: { referencePanel: 'canvas', direction: 'right' },
  })
  api.addPanel({
    id: 'diagnostics',
    component: 'diagnostics',
    initialHeight: 240,
    title: 'Diagnostics',
    inactive: true,
    position: { referencePanel: 'canvas', direction: 'below' },
  })
  api.addPanel({
    id: 'graphText',
    component: 'graphText',
    title: 'Graph Text',
    inactive: true,
    position: { referencePanel: 'source', direction: 'within' },
  })
  api.addPanel({
    id: 'console',
    component: 'console',
    title: 'Console',
    inactive: true,
    position: { referencePanel: 'source', direction: 'within' },
  })
}

function hasRequiredPanels(api: DockviewApi): boolean {
  return REQUIRED_PANEL_IDS.every(panelId => Boolean(api.getPanel(panelId)))
}

function restoreOrCreateLayout(api: DockviewApi, stored: SerializedDockview | null) {
  if (stored) {
    try {
      api.fromJSON(stored)
      if (hasRequiredPanels(api)) return
    } catch {
      // Invalid or stale layout JSON is recovered below by recreating the default layout.
    }
    api.clear()
  }

  createDefaultLayout(api)
}

export default function WorkbenchLayout({
  palette,
  canvas,
  properties,
  diagnostics,
  graphText,
  source,
  console,
  activePanelRequest,
}: WorkbenchLayoutProps) {
  const apiRef = useRef<DockviewApi | null>(null)
  const layoutDisposableRef = useRef<DockviewIDisposable | null>(null)

  const panels = useMemo<WorkbenchPanels>(() => ({
    palette,
    canvas,
    properties,
    diagnostics,
    graphText,
    source,
    console,
  }), [canvas, console, diagnostics, graphText, palette, properties, source])

  const panelComponents = useMemo<Record<string, FunctionComponent<IDockviewPanelProps>>>(() => ({
    palette: () => <PanelHost panelId="palette" />,
    canvas: () => <PanelHost panelId="canvas" />,
    properties: () => <PanelHost panelId="properties" />,
    diagnostics: () => <PanelHost panelId="diagnostics" />,
    graphText: () => <PanelHost panelId="graphText" />,
    source: () => <PanelHost panelId="source" />,
    console: () => <PanelHost panelId="console" />,
  }), [])

  const handleReady = useCallback((event: DockviewReadyEvent) => {
    apiRef.current = event.api
    layoutDisposableRef.current?.dispose()
    const stored = loadStoredLayout()
    restoreOrCreateLayout(event.api, stored)

    saveStoredLayout(event.api)
    layoutDisposableRef.current = event.api.onDidLayoutChange(() => saveStoredLayout(event.api))
  }, [])

  useEffect(() => () => {
    layoutDisposableRef.current?.dispose()
    layoutDisposableRef.current = null
    apiRef.current = null
  }, [])

  useEffect(() => {
    if (!activePanelRequest) return
    const panel = apiRef.current?.getPanel(activePanelRequest.panel)
    panel?.api.setActive()
  }, [activePanelRequest])

  return (
    <WorkbenchPanelsContext.Provider value={panels}>
      <div className="graphscript-workbench dockview-theme-dark h-full min-h-0 w-full">
        <DockviewReact
          components={panelComponents}
          onReady={handleReady}
        />
      </div>
    </WorkbenchPanelsContext.Provider>
  )
}
