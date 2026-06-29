import type { Diagnostic, GraphDef } from '@/api/types'

export type DiagnosticHighlightSeverity = 'warning' | 'error'

export interface DiagnosticPinHighlight {
  severity: DiagnosticHighlightSeverity
  focused: boolean
  connection: boolean
}

export interface DiagnosticNodeHighlight {
  severity: DiagnosticHighlightSeverity
  focused: boolean
  pins: Record<string, DiagnosticPinHighlight>
}

export interface DiagnosticEdgeHighlight {
  severity: DiagnosticHighlightSeverity
  focused: boolean
}

export interface DiagnosticHighlightIndex {
  nodes: Record<string, DiagnosticNodeHighlight>
  edges: Record<string, DiagnosticEdgeHighlight>
}

export interface DiagnosticHighlightData {
  severity: DiagnosticHighlightSeverity
  focused: boolean
  pins: Record<string, DiagnosticPinHighlight>
}

export function emptyDiagnosticHighlightIndex(): DiagnosticHighlightIndex {
  return { nodes: {}, edges: {} }
}

export function portKey(kind: 'exec' | 'data', direction: 'in' | 'out', pin: string): string {
  return `${kind}-${direction}-${pin}`
}

export function edgeKey(
  kind: 'exec' | 'data',
  blockKind: 'event' | 'function',
  blockName: string,
  sourceNode: string,
  sourcePin: string,
  targetNode: string,
  targetPin: string,
): string {
  return [
    kind,
    blockKind,
    blockName,
    sourceNode,
    sourcePin,
    targetNode,
    targetPin,
  ].join('|')
}

export function strongerSeverity(
  current: DiagnosticHighlightSeverity | undefined,
  next: DiagnosticHighlightSeverity,
): DiagnosticHighlightSeverity {
  return current === 'error' || next === 'error' ? 'error' : 'warning'
}

function addNodeHighlight(
  index: DiagnosticHighlightIndex,
  nodeInstance: string,
  severity: DiagnosticHighlightSeverity,
  focused: boolean,
): DiagnosticNodeHighlight {
  const existing = index.nodes[nodeInstance]
  const next: DiagnosticNodeHighlight = existing ?? {
    severity,
    focused: false,
    pins: {},
  }
  next.severity = strongerSeverity(next.severity, severity)
  next.focused = next.focused || focused
  index.nodes[nodeInstance] = next
  return next
}

function addPinHighlight(
  node: DiagnosticNodeHighlight,
  key: string,
  severity: DiagnosticHighlightSeverity,
  focused: boolean,
  connection: boolean,
) {
  const existing = node.pins[key]
  node.pins[key] = {
    severity: strongerSeverity(existing?.severity, severity),
    focused: (existing?.focused ?? false) || focused,
    connection: (existing?.connection ?? false) || connection,
  }
}

function addEdgeHighlight(
  index: DiagnosticHighlightIndex,
  key: string,
  severity: DiagnosticHighlightSeverity,
  focused: boolean,
) {
  const existing = index.edges[key]
  index.edges[key] = {
    severity: strongerSeverity(existing?.severity, severity),
    focused: (existing?.focused ?? false) || focused,
  }
}

function sameDiagnostic(a: Diagnostic | null, b: Diagnostic): boolean {
  if (!a) return false
  if (a.id || b.id) return a.id === b.id
  return a.code === b.code &&
    a.message === b.message &&
    a.context === b.context &&
    a.range?.start.line === b.range?.start.line &&
    a.range?.start.column === b.range?.start.column &&
    a.range?.end.line === b.range?.end.line &&
    a.range?.end.column === b.range?.end.column &&
    a.target?.graph === b.target?.graph &&
    a.target?.block_kind === b.target?.block_kind &&
    a.target?.block_name === b.target?.block_name &&
    a.target?.node_instance === b.target?.node_instance &&
    a.target?.pin_name === b.target?.pin_name &&
    a.target?.parameter_name === b.target?.parameter_name &&
    a.target?.reference === b.target?.reference &&
    a.target?.connection_kind === b.target?.connection_kind
}

function diagnosticMatchesGraph(graph: GraphDef, diagnostic: Diagnostic): boolean {
  const targetGraph = diagnostic.target?.graph
  return !targetGraph || targetGraph === graph.name
}

function diagnosticNodeCandidate(graph: GraphDef, diagnostic: Diagnostic): string {
  const target = diagnostic.target
  const candidates = [
    target?.node_instance,
    target?.reference,
    diagnostic.context,
  ]
  return candidates.find(candidate =>
    !!candidate && graph.nodes.some(node => node.instance === candidate)) ?? ''
}

function targetMatchesEdge(
  diagnostic: Diagnostic,
  edge: {
    kind: 'exec' | 'data'
    blockKind: 'event' | 'function'
    blockName: string
    sourceNode: string
    sourcePin: string
    targetNode: string
    targetPin: string
  },
): boolean {
  const target = diagnostic.target
  if (!target) return false
  if (target.connection_kind && target.connection_kind !== edge.kind) return false
  if (target.block_kind && target.block_kind !== edge.blockKind) return false
  if (target.block_name && target.block_name !== edge.blockName) return false

  const node = target.node_instance || target.reference || diagnostic.context
  const pin = target.pin_name
  if (!node) return false

  const sourceMatches = edge.sourceNode === node && (!pin || edge.sourcePin === pin)
  const targetMatches = edge.targetNode === node && (!pin || edge.targetPin === pin)
  return sourceMatches || targetMatches
}

function markMatchingNodePins(
  graph: GraphDef,
  index: DiagnosticHighlightIndex,
  nodeInstance: string,
  pinName: string,
  connectionKind: string,
  severity: DiagnosticHighlightSeverity,
  focused: boolean,
) {
  const graphNode = graph.nodes.find(node => node.instance === nodeInstance)
  if (!graphNode) return
  const nodeHighlight = addNodeHighlight(index, nodeInstance, severity, focused)
  const pinKinds = connectionKind === 'exec' || connectionKind === 'data'
    ? [connectionKind]
    : ['exec', 'data']
  for (const kind of pinKinds) {
    addPinHighlight(nodeHighlight, portKey(kind as 'exec' | 'data', 'in', pinName), severity, focused, !!connectionKind)
    addPinHighlight(nodeHighlight, portKey(kind as 'exec' | 'data', 'out', pinName), severity, focused, !!connectionKind)
  }
}

export function buildDiagnosticHighlightIndex(
  graph: GraphDef,
  diagnostics: Diagnostic[],
  focusedDiagnostic: Diagnostic | null,
): DiagnosticHighlightIndex {
  const index = emptyDiagnosticHighlightIndex()

  for (const diagnostic of diagnostics) {
    if (!diagnosticMatchesGraph(graph, diagnostic)) continue
    const target = diagnostic.target
    if (!target) continue

    const severity = diagnostic.severity
    const focused = sameDiagnostic(focusedDiagnostic, diagnostic)
    const nodeInstance = diagnosticNodeCandidate(graph, diagnostic)

    if (nodeInstance) {
      addNodeHighlight(index, nodeInstance, severity, focused)
      if (target.pin_name) {
        markMatchingNodePins(
          graph,
          index,
          nodeInstance,
          target.pin_name,
          target.connection_kind,
          severity,
          focused,
        )
      }
    }

    for (const block of [...graph.events, ...graph.functions]) {
      for (const flow of block.flows) {
        const edge = {
          kind: 'exec' as const,
          blockKind: block.kind,
          blockName: block.name,
          sourceNode: flow.from_node,
          sourcePin: flow.from_pin,
          targetNode: flow.to_node,
          targetPin: flow.to_pin,
        }
        if (!targetMatchesEdge(diagnostic, edge)) continue
        addEdgeHighlight(
          index,
          edgeKey(edge.kind, edge.blockKind, edge.blockName, edge.sourceNode, edge.sourcePin, edge.targetNode, edge.targetPin),
          severity,
          focused,
        )
        addPinHighlight(
          addNodeHighlight(index, edge.sourceNode, severity, focused),
          portKey('exec', 'out', edge.sourcePin),
          severity,
          focused,
          true,
        )
        addPinHighlight(
          addNodeHighlight(index, edge.targetNode, severity, focused),
          portKey('exec', 'in', edge.targetPin),
          severity,
          focused,
          true,
        )
      }

      for (const link of block.links) {
        const edge = {
          kind: 'data' as const,
          blockKind: block.kind,
          blockName: block.name,
          sourceNode: link.source_node,
          sourcePin: link.source_pin,
          targetNode: link.target_node,
          targetPin: link.target_pin,
        }
        if (!targetMatchesEdge(diagnostic, edge)) continue
        addEdgeHighlight(
          index,
          edgeKey(edge.kind, edge.blockKind, edge.blockName, edge.sourceNode, edge.sourcePin, edge.targetNode, edge.targetPin),
          severity,
          focused,
        )
        addPinHighlight(
          addNodeHighlight(index, edge.sourceNode, severity, focused),
          portKey('data', 'out', edge.sourcePin),
          severity,
          focused,
          true,
        )
        addPinHighlight(
          addNodeHighlight(index, edge.targetNode, severity, focused),
          portKey('data', 'in', edge.targetPin),
          severity,
          focused,
          true,
        )
      }
    }
  }

  return index
}
