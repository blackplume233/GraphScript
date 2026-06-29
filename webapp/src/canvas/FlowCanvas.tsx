import { useCallback, useEffect, useMemo, useRef, useState, type MouseEvent as ReactMouseEvent } from 'react'
import {
  applyEdgeChanges,
  applyNodeChanges,
  Background,
  ConnectionMode,
  Controls,
  MarkerType,
  MiniMap,
  ReactFlow,
  ReactFlowProvider,
  SelectionMode,
  useReactFlow,
  type Connection,
  type Edge,
  type EdgeChange,
  type NodeChange,
  type NodeMouseHandler,
  type OnNodesDelete,
  type OnSelectionChangeFunc,
  type OnConnect,
  type OnConnectEnd,
  type OnConnectStart,
  type OnReconnect,
  type XYPosition,
} from '@xyflow/react'
import '@xyflow/react/dist/style.css'
import type { Annotation, Diagnostic, GraphDef, GSState, LogicBlock, NodeFieldDef, NodeInst, NodeTypeDef, PinDef, SourceRange } from '@/api/types'
import BlueprintNode, { type BlueprintFlowNode, type BlueprintIntrinsicProperty, type BlueprintNodeData } from './BlueprintNode'
import { buildDiagnosticHighlightIndex, edgeKey } from './diagnostic-highlights'

interface FlowCanvasProps {
  state: GSState | null
  graphIndex: number
  selectedNode?: string | null
  onNodeCreate?: (typeName: string, x: number, y: number) => Promise<string | null | void> | string | null | void
  onNodeSelect?: (instanceName: string | null) => void
  onNodeMove?: (instanceName: string, x: number, y: number) => Promise<void> | void
  onNodeDelete?: (instanceName: string) => Promise<void> | void
  onRefresh?: () => Promise<void> | void
  activeLogicBlock?: LogicBlockRef | null
  onEdgeCreate?: (edge: EdgeEditPayload) => Promise<void> | void
  onEdgeDelete?: (edge: EdgeEditPayload) => Promise<void> | void
  onEdgeReconnect?: (previous: EdgeEditPayload, next: EdgeEditPayload) => Promise<void> | void
  onEdgeSelect?: (edge: EdgeEditPayload | null) => void
  diagnostics?: Diagnostic[]
  focusedDiagnostic?: Diagnostic | null
}

interface FlowPosition {
  x: number
  y: number
}

interface CanvasContextMenu {
  localX: number
  localY: number
  graphX: number
  graphY: number
  zoom: number
  query: string
  pendingConnection?: PendingConnection | null
}

interface PointerDragState {
  button: number
  startX: number
  startY: number
  dragged: boolean
}

interface ContextNodeGroup {
  category: string
  items: NodeTypeDef[]
}

interface LogicBlockRef {
  kind: 'event' | 'function'
  name: string
}

interface PortRef {
  kind: 'exec' | 'data'
  direction: 'in' | 'out'
  pin: string
}

interface PendingConnection {
  nodeId: string
  port: PortRef
  type: string
}

type GraphScriptEdge = Edge<EdgeEditPayload, 'smoothstep'>

export interface EdgeEditPayload extends Record<string, unknown> {
  id?: string
  persistentId?: string
  kind: 'exec' | 'data'
  blockKind: 'event' | 'function'
  blockName: string
  sourceNode: string
  sourcePin: string
  targetNode: string
  targetPin: string
  sourceRange?: SourceRange
  sourceEndpointRange?: SourceRange
  targetEndpointRange?: SourceRange
  annotations: Annotation[]
}

const nodeTypes = {
  blueprint: BlueprintNode,
}

function findAnnotationArg(node: NodeInst, annotationName: string, argName: string): string | undefined {
  const annotation = node.annotations.find(item => item.name === annotationName)
  return annotation?.args.find(arg => arg.name === argName)?.value
}

function nodePosition(node: NodeInst, index: number): FlowPosition {
  const x = Number(findAnnotationArg(node, 'Position', 'X'))
  const y = Number(findAnnotationArg(node, 'Position', 'Y'))
  if (Number.isFinite(x) && Number.isFinite(y)) return { x, y }
  return {
    x: 200 + (index % 4) * 300,
    y: 120 + Math.floor(index / 4) * 200,
  }
}

function getPinsForType(typeName: string, types: NodeTypeDef[]): PinDef[] {
  return types.find(type => type.type_name === typeName)?.pins ?? []
}

function intrinsicPropertiesForNode(node: NodeInst, nodeFields: NodeFieldDef[], pins: PinDef[]): BlueprintIntrinsicProperty[] {
  const initializerFields = new Map((node.initializer_fields ?? []).map(field => [field.name, field.value]))
  const declaredFields = nodeFields.length > 0
    ? nodeFields
    : pins
      .filter(pin => pin.kind === 'data' && pin.direction === 'in')
      .map(pin => ({ name: pin.name, type: pin.type, default: '' }))
  return declaredFields
    .map(field => {
      const value = initializerFields.get(field.name) ?? ''
      return {
        name: field.name,
        type: field.type,
        value,
        defaultValue: field.default ?? '',
        overridden: initializerFields.has(field.name),
      }
    })
}

function roundPosition(pos: XYPosition): FlowPosition {
  return {
    x: Math.round(pos.x),
    y: Math.round(pos.y),
  }
}

function parsePortId(portId: string | null | undefined): PortRef | null {
  if (!portId) return null
  const parts = String(portId).split('-')
  if (parts.length < 3) return null
  const [kind, direction, ...pinParts] = parts
  if ((kind !== 'exec' && kind !== 'data') || (direction !== 'in' && direction !== 'out')) return null
  const pin = pinParts.join('-')
  if (!pin) return null
  return { kind, direction, pin }
}

function dataTypesCompatible(left: string, right: string): boolean {
  if (!left || !right) return true
  return left === right
}

function findNodeTypeForInstance(graph: GraphDef | undefined, types: NodeTypeDef[], instanceName: string): NodeTypeDef | undefined {
  const node = graph?.nodes.find(item => item.instance === instanceName)
  if (!node) return undefined
  return types.find(type => type.type_name === node.type)
}

function compatiblePinsForPending(type: NodeTypeDef, pending: PendingConnection | null | undefined): PinDef[] {
  if (!pending) return []
  return type.pins.filter(pin => {
    if (pin.kind !== pending.port.kind) return false
    if (pin.direction === pending.port.direction) return false
    if (pin.kind === 'exec') return true
    return dataTypesCompatible(pin.type, pending.type)
  })
}

function edgePayloadForPendingConnection(
  pending: PendingConnection,
  newNodeId: string,
  newNodePin: PinDef,
  block: LogicBlockRef,
): EdgeEditPayload {
  if (pending.port.direction === 'out') {
    return {
      kind: pending.port.kind,
      blockKind: block.kind,
      blockName: block.name,
      sourceNode: pending.nodeId,
      sourcePin: pending.port.pin,
      targetNode: newNodeId,
      targetPin: newNodePin.name,
      annotations: [],
    }
  }

  return {
    kind: pending.port.kind,
    blockKind: block.kind,
    blockName: block.name,
    sourceNode: newNodeId,
    sourcePin: newNodePin.name,
    targetNode: pending.nodeId,
    targetPin: pending.port.pin,
    annotations: [],
  }
}

function eventClientPosition(event: globalThis.MouseEvent | TouchEvent): { x: number; y: number } | null {
  if ('clientX' in event) return { x: event.clientX, y: event.clientY }
  const touch = event.changedTouches[0] ?? event.touches[0]
  return touch ? { x: touch.clientX, y: touch.clientY } : null
}

function isEditableKeyboardTarget(target: EventTarget | null): boolean {
  if (!(target instanceof HTMLElement)) return false
  const tagName = target.tagName.toLowerCase()
  return target.isContentEditable || tagName === 'input' || tagName === 'textarea' || tagName === 'select'
}

function activeBlockForGraph(graph: GraphDef | undefined, block: LogicBlockRef | null | undefined): LogicBlock | undefined {
  if (!graph || !block) return undefined
  const blocks = block.kind === 'event' ? graph.events : graph.functions
  return blocks.find(item => item.name === block.name)
}

function hasIncomingConnection(
  graph: GraphDef | undefined,
  block: LogicBlockRef | null | undefined,
  targetNode: string,
  targetPin: string,
  kind: 'exec' | 'data',
  except?: EdgeEditPayload,
): boolean {
  const logicBlock = activeBlockForGraph(graph, block)
  if (!logicBlock) return false
  if (kind === 'exec') {
    return logicBlock.flows.some(flow => {
      if (except?.kind === 'exec' &&
        flow.from_node === except.sourceNode &&
        flow.from_pin === except.sourcePin &&
        flow.to_node === except.targetNode &&
        flow.to_pin === except.targetPin) {
        return false
      }
      return flow.to_node === targetNode && flow.to_pin === targetPin
    })
  }
  return logicBlock.links.some(link => {
    if (except?.kind === 'data' &&
      link.source_node === except.sourceNode &&
      link.source_pin === except.sourcePin &&
      link.target_node === except.targetNode &&
      link.target_pin === except.targetPin) {
      return false
    }
    return link.target_node === targetNode && link.target_pin === targetPin
  })
}

function edgeId(edge: EdgeEditPayload): string {
  return [
    edge.kind,
    edge.blockKind,
    edge.blockName,
    edge.sourceNode,
    edge.sourcePin,
    edge.targetNode,
    edge.targetPin,
    edge.persistentId ?? edge.id ?? '',
  ].join('|')
}

function nodeTypeCategory(type: NodeTypeDef): string {
  const hasExec = type.pins.some(pin => pin.kind === 'exec')
  return type.tags.length > 0 ? type.tags[0] : hasExec ? 'Flow' : 'Pure'
}

function nodeTypeMatches(type: NodeTypeDef, query: string): boolean {
  if (!query) return true
  const haystack = [
    type.type_name,
    type.source_graph,
    ...type.tags,
    ...type.pins.flatMap(pin => [pin.name, pin.type, pin.kind, pin.direction]),
    ...(type.fields ?? []).flatMap(field => [field.name, field.type, field.default]),
  ].join(' ').toLowerCase()
  return haystack.includes(query)
}

function groupContextNodeTypes(types: NodeTypeDef[]): ContextNodeGroup[] {
  const groups = new Map<string, NodeTypeDef[]>()
  for (const type of types) {
    const category = nodeTypeCategory(type)
    groups.set(category, [...(groups.get(category) ?? []), type])
  }
  return Array.from(groups.entries())
    .sort(([left], [right]) => left.localeCompare(right))
    .map(([category, items]) => ({
      category,
      items: items.sort((a, b) => a.type_name.localeCompare(b.type_name)),
    }))
}

function pendingConnectionLabel(pending: PendingConnection): string {
  const verb = pending.port.direction === 'out' ? 'from' : 'to'
  return `${verb} ${pending.nodeId}.${pending.port.pin}`
}

function contextPinSummary(type: NodeTypeDef, pending: PendingConnection | null | undefined): string {
  if (!pending) {
    return type.pins.slice(0, 4).map(pin => `${pin.direction} ${pin.name}`).join(', ') || 'No pins'
  }

  const compatiblePins = compatiblePinsForPending(type, pending)
  const verb = pending.port.direction === 'out' ? 'to' : 'from'
  return compatiblePins
    .slice(0, 4)
    .map(pin => `${verb} ${pin.direction} ${pin.name}${pin.type ? `: ${pin.type}` : ''}`)
    .join(', ') || 'No compatible pins'
}

function edgeStyle(kind: 'exec' | 'data', highlighted: boolean): GraphScriptEdge['style'] {
  return {
    stroke: highlighted ? 'var(--color-warning)' : kind === 'exec' ? 'var(--color-exec)' : 'var(--color-data-object)',
    strokeWidth: highlighted ? 2.5 : 2,
    strokeDasharray: kind === 'data' ? '5 4' : undefined,
  }
}

function payloadFromConnection(connection: Connection, block: LogicBlockRef | null | undefined): EdgeEditPayload | null {
  const source = parsePortId(connection.sourceHandle)
  const target = parsePortId(connection.targetHandle)
  if (!connection.source || !connection.target || !source || !target || !block) return null
  if (source.direction !== 'out' || target.direction !== 'in') return null
  if (source.kind !== target.kind) return null
  return {
    kind: source.kind,
    blockKind: block.kind,
    blockName: block.name,
    sourceNode: connection.source,
    sourcePin: source.pin,
    targetNode: connection.target,
    targetPin: target.pin,
    annotations: [],
  }
}

function toReactFlowNodes(
  graph: GraphDef,
  state: GSState,
  diagnostics: ReturnType<typeof buildDiagnosticHighlightIndex> | null,
  connectionPreview: PendingConnection | null,
): BlueprintFlowNode[] {
  return graph.nodes.map((node, index) => {
    const position = nodePosition(node, index)
    const typeDef = state.types.find(type => type.type_name === node.type)
    const pins = typeDef?.pins ?? []
    const data: BlueprintNodeData = {
      label: node.instance,
      typeName: node.type,
      instanceName: node.instance,
      init: node.init,
      pins,
      intrinsicProperties: intrinsicPropertiesForNode(node, typeDef?.fields ?? [], pins),
      diagnostic: diagnostics?.nodes[node.instance],
      connectionPreview,
    }
    return {
      id: node.instance,
      type: 'blueprint',
      position,
      data,
    }
  })
}

function preserveNodeSelection(nextNodes: BlueprintFlowNode[], currentNodes: BlueprintFlowNode[]): BlueprintFlowNode[] {
  const selectedById = new Map(currentNodes.map(node => [node.id, Boolean(node.selected)]))
  return nextNodes.map(node => ({
    ...node,
    selected: selectedById.get(node.id) ?? false,
  }))
}

function toReactFlowEdges(
  graph: GraphDef,
  block: LogicBlock | undefined,
  diagnostics: ReturnType<typeof buildDiagnosticHighlightIndex> | null,
): GraphScriptEdge[] {
  if (!block) return []
  const graphNodes = new Set(graph.nodes.map(node => node.instance))
  const edges: GraphScriptEdge[] = []

  for (const flow of block.flows) {
    if (!graphNodes.has(flow.from_node) || !graphNodes.has(flow.to_node)) continue
    const payload: EdgeEditPayload = {
      id: flow.id,
      persistentId: flow.persistent_id,
      kind: 'exec',
      blockKind: block.kind,
      blockName: block.name,
      sourceNode: flow.from_node,
      sourcePin: flow.from_pin,
      targetNode: flow.to_node,
      targetPin: flow.to_pin,
      sourceRange: flow.source_range,
      sourceEndpointRange: flow.from_endpoint_source_range,
      targetEndpointRange: flow.to_endpoint_source_range,
      annotations: flow.annotations,
    }
    const highlight = diagnostics?.edges[edgeKey(payload.kind, payload.blockKind, payload.blockName, payload.sourceNode, payload.sourcePin, payload.targetNode, payload.targetPin)]
    edges.push({
      id: edgeId(payload),
      type: 'smoothstep',
      source: payload.sourceNode,
      target: payload.targetNode,
      sourceHandle: `exec-out-${payload.sourcePin}`,
      targetHandle: `exec-in-${payload.targetPin}`,
      data: payload,
      animated: highlight?.severity === 'warning',
      reconnectable: true,
      markerEnd: { type: MarkerType.ArrowClosed, color: 'var(--color-exec)' },
      style: edgeStyle('exec', Boolean(highlight)),
    })
  }

  for (const link of block.links) {
    if (!link.source_pin || !graphNodes.has(link.source_node) || !graphNodes.has(link.target_node)) continue
    const payload: EdgeEditPayload = {
      id: link.id,
      persistentId: link.persistent_id,
      kind: 'data',
      blockKind: block.kind,
      blockName: block.name,
      sourceNode: link.source_node,
      sourcePin: link.source_pin,
      targetNode: link.target_node,
      targetPin: link.target_pin,
      sourceRange: link.source_range,
      sourceEndpointRange: link.source_endpoint_source_range,
      targetEndpointRange: link.target_endpoint_source_range,
      annotations: link.annotations,
    }
    const highlight = diagnostics?.edges[edgeKey(payload.kind, payload.blockKind, payload.blockName, payload.sourceNode, payload.sourcePin, payload.targetNode, payload.targetPin)]
    edges.push({
      id: edgeId(payload),
      type: 'smoothstep',
      source: payload.sourceNode,
      target: payload.targetNode,
      sourceHandle: `data-out-${payload.sourcePin}`,
      targetHandle: `data-in-${payload.targetPin}`,
      data: payload,
      animated: highlight?.severity === 'warning',
      reconnectable: true,
      markerEnd: { type: MarkerType.ArrowClosed, color: 'var(--color-data-object)' },
      style: edgeStyle('data', Boolean(highlight)),
    })
  }

  return edges
}

function FlowCanvasInner({
  state,
  graphIndex,
  onNodeCreate,
  onNodeSelect,
  onNodeMove,
  onNodeDelete,
  onRefresh,
  activeLogicBlock,
  onEdgeCreate,
  onEdgeDelete,
  onEdgeReconnect,
  onEdgeSelect,
  diagnostics = [],
  focusedDiagnostic = null,
}: FlowCanvasProps) {
  const wrapperRef = useRef<HTMLDivElement>(null)
  const reactFlow = useReactFlow<BlueprintFlowNode, GraphScriptEdge>()
  const graph: GraphDef | undefined = state?.module.graphs[graphIndex]
  const activeBlock = useMemo(() => activeBlockForGraph(graph, activeLogicBlock), [activeLogicBlock, graph])
  const diagnosticHighlights = useMemo(() => {
    if (!graph) return null
    return buildDiagnosticHighlightIndex(graph, diagnostics, focusedDiagnostic)
  }, [diagnostics, focusedDiagnostic, graph])
  const [contextMenu, setContextMenu] = useState<CanvasContextMenu | null>(null)
  const [connectionPreview, setConnectionPreview] = useState<PendingConnection | null>(null)
  const pointerDragRef = useRef<PointerDragState | null>(null)
  const pendingConnectionRef = useRef<PendingConnection | null>(null)

  const initialNodes = useMemo(() => {
    if (!state || !graph) return []
    return toReactFlowNodes(graph, state, diagnosticHighlights, connectionPreview)
  }, [connectionPreview, diagnosticHighlights, graph, state])

  const initialEdges = useMemo(() => {
    if (!graph) return []
    return toReactFlowEdges(graph, activeBlock, diagnosticHighlights)
  }, [activeBlock, diagnosticHighlights, graph])

  const [nodes, setNodes] = useState<BlueprintFlowNode[]>(initialNodes)
  const [edges, setEdges] = useState<GraphScriptEdge[]>(initialEdges)

  useEffect(() => {
    setNodes(current => preserveNodeSelection(initialNodes, current))
  }, [initialNodes])

  useEffect(() => {
    setEdges(initialEdges)
  }, [initialEdges])

  const contextNodeTypes = useMemo(() => {
    const query = contextMenu?.query.trim().toLowerCase() ?? ''
    const pending = contextMenu?.pendingConnection ?? null
    return (state?.types ?? [])
      .filter(type => nodeTypeMatches(type, query))
      .filter(type => !pending || compatiblePinsForPending(type, pending).length > 0)
      .sort((a, b) => a.type_name.localeCompare(b.type_name))
      .slice(0, 50)
  }, [contextMenu?.pendingConnection, contextMenu?.query, state?.types])

  const contextNodeGroups = useMemo(() => groupContextNodeTypes(contextNodeTypes), [contextNodeTypes])

  const onNodesChange = useCallback((changes: NodeChange<BlueprintFlowNode>[]) => {
    setNodes(current => applyNodeChanges(changes, current))
  }, [])

  const onEdgesChange = useCallback((changes: EdgeChange<GraphScriptEdge>[]) => {
    setEdges(current => applyEdgeChanges(changes, current))
  }, [])

  const isValidConnection = useCallback((connection: Connection | Edge) => {
    if (!graph || !activeLogicBlock) return false
    const payload = payloadFromConnection(connection as Connection, activeLogicBlock)
    if (!payload) return false
    return !hasIncomingConnection(graph, activeLogicBlock, payload.targetNode, payload.targetPin, payload.kind)
  }, [activeLogicBlock, graph])

  const onConnect = useCallback<OnConnect>((connection) => {
    const payload = payloadFromConnection(connection, activeLogicBlock)
    if (!payload || !isValidConnection(connection)) return
    void onEdgeCreate?.(payload)
  }, [activeLogicBlock, isValidConnection, onEdgeCreate])

  const onConnectStart = useCallback<OnConnectStart>((_event, params) => {
    if (!graph || !state || !params.nodeId) return
    const port = parsePortId(params.handleId)
    if (!port) return
    const nodeType = findNodeTypeForInstance(graph, state.types, params.nodeId)
    const pin = nodeType?.pins.find(item =>
      item.kind === port.kind &&
      item.direction === port.direction &&
      item.name === port.pin)
    const pending: PendingConnection = {
      nodeId: params.nodeId,
      port,
      type: pin?.type ?? '',
    }
    pendingConnectionRef.current = pending
    setConnectionPreview(pending)
    setContextMenu(null)
  }, [graph, state])

  const onConnectEnd = useCallback<OnConnectEnd>((event, connectionState) => {
    const pending = pendingConnectionRef.current
    pendingConnectionRef.current = null
    setConnectionPreview(null)

    if (!pending || !graph || !wrapperRef.current) return
    if (connectionState.isValid) return
    const target = event.target instanceof Element ? event.target : null
    if (target?.closest('.react-flow__handle')) return
    const client = eventClientPosition(event)
    if (!client) return

    const rect = wrapperRef.current.getBoundingClientRect()
    if (client.x < rect.left || client.x > rect.right || client.y < rect.top || client.y > rect.bottom) return
    const localX = client.x - rect.left
    const localY = client.y - rect.top
    const position = reactFlow.screenToFlowPosition({ x: client.x, y: client.y })
    const viewport = reactFlow.getViewport()
    setContextMenu({
      localX,
      localY,
      graphX: Math.round(position.x),
      graphY: Math.round(position.y),
      zoom: viewport.zoom,
      query: '',
      pendingConnection: pending,
    })
  }, [graph, reactFlow])

  const onReconnect = useCallback<OnReconnect<GraphScriptEdge>>((oldEdge, newConnection) => {
    const previous = oldEdge.data
    if (!previous) {
      void onRefresh?.()
      return
    }
    const next = payloadFromConnection(newConnection, {
      kind: previous.blockKind,
      name: previous.blockName,
    })
    if (!next || hasIncomingConnection(graph, {
      kind: previous.blockKind,
      name: previous.blockName,
    }, next.targetNode, next.targetPin, next.kind, previous)) {
      void onRefresh?.()
      return
    }
    void onEdgeReconnect?.(previous, next)
  }, [graph, onEdgeReconnect, onRefresh])

  const onEdgesDelete = useCallback((deleted: GraphScriptEdge[]) => {
    for (const edge of deleted) {
      if (edge.data) void onEdgeDelete?.(edge.data)
    }
  }, [onEdgeDelete])

  const onNodesDelete = useCallback<OnNodesDelete<BlueprintFlowNode>>((deleted) => {
    for (const node of deleted) {
      void onNodeDelete?.(node.id)
    }
    onNodeSelect?.(null)
    onEdgeSelect?.(null)
  }, [onEdgeSelect, onNodeDelete, onNodeSelect])

  const onNodeDragStop = useCallback((_event: globalThis.MouseEvent | TouchEvent, node: BlueprintFlowNode) => {
    const selectedNodes = nodes.filter(item => item.selected)
    const nodesToPersist = selectedNodes.length > 1 && selectedNodes.some(item => item.id === node.id)
      ? selectedNodes
      : [node]

    for (const item of nodesToPersist) {
      const position = roundPosition(item.position)
      void onNodeMove?.(item.id, position.x, position.y)
    }
  }, [nodes, onNodeMove])

  const onNodeClick = useCallback<NodeMouseHandler<BlueprintFlowNode>>((event, node) => {
    const additive = event.shiftKey || event.ctrlKey || event.metaKey
    window.setTimeout(() => {
      setNodes(current => {
        const clicked = current.find(candidate => candidate.id === node.id)
        const nodeSelected = additive ? !Boolean(clicked?.selected) : true
        onNodeSelect?.(nodeSelected ? node.id : null)
        return current.map(item => {
          if (item.id !== node.id) {
            return additive ? item : { ...item, selected: false }
          }
          return { ...item, selected: nodeSelected }
        })
      })
    }, 0)
    onEdgeSelect?.(null)
  }, [onEdgeSelect, onNodeSelect])

  const onEdgeClick = useCallback((_event: ReactMouseEvent, edge: GraphScriptEdge) => {
    if (edge.data) {
      onEdgeSelect?.(edge.data)
      onNodeSelect?.(null)
    }
  }, [onEdgeSelect, onNodeSelect])

  const onSelectionChange = useCallback<OnSelectionChangeFunc<BlueprintFlowNode, GraphScriptEdge>>(({ nodes: selectedNodes, edges: selectedEdges }) => {
    if (selectedEdges.length > 0) {
      const edge = selectedEdges[selectedEdges.length - 1]
      if (edge.data) {
        onEdgeSelect?.(edge.data)
        onNodeSelect?.(null)
      }
      return
    }

    if (selectedNodes.length > 0) {
      const node = selectedNodes[selectedNodes.length - 1]
      onNodeSelect?.(node.id)
      onEdgeSelect?.(null)
      return
    }

    onNodeSelect?.(null)
    onEdgeSelect?.(null)
  }, [onEdgeSelect, onNodeSelect])

  const closeContextMenu = useCallback(() => {
    setContextMenu(null)
  }, [])

  const onPanePointerDown = useCallback((event: ReactMouseEvent<HTMLDivElement>) => {
    pointerDragRef.current = {
      button: event.button,
      startX: event.clientX,
      startY: event.clientY,
      dragged: false,
    }
  }, [])

  const onPanePointerMove = useCallback((event: ReactMouseEvent<HTMLDivElement>) => {
    const drag = pointerDragRef.current
    if (!drag) return
    const distance = Math.hypot(event.clientX - drag.startX, event.clientY - drag.startY)
    if (distance > 4) drag.dragged = true
  }, [])

  const onPanePointerUp = useCallback(() => {
    window.setTimeout(() => {
      pointerDragRef.current = null
    }, 0)
  }, [])

  const onPaneContextMenu = useCallback((event: globalThis.MouseEvent | ReactMouseEvent<Element>) => {
    if (!graph || !wrapperRef.current) return
    event.preventDefault()
    const drag = pointerDragRef.current
    if (drag?.button === 2 && drag.dragged) {
      pointerDragRef.current = null
      return
    }
    const rect = wrapperRef.current.getBoundingClientRect()
    const localX = event.clientX - rect.left
    const localY = event.clientY - rect.top
    const position = reactFlow.screenToFlowPosition({ x: event.clientX, y: event.clientY })
    const viewport = reactFlow.getViewport()
    setContextMenu({
      localX,
      localY,
      graphX: Math.round(position.x),
      graphY: Math.round(position.y),
      zoom: viewport.zoom,
      query: '',
    })
  }, [graph, reactFlow])

  const createNodeFromContext = useCallback(async (typeName: string) => {
    if (!contextMenu || !onNodeCreate) return
    const { graphX, graphY, pendingConnection } = contextMenu
    const typeDef = state?.types.find(type => type.type_name === typeName)
    const compatiblePin = pendingConnection && typeDef
      ? compatiblePinsForPending(typeDef, pendingConnection)[0]
      : null
    closeContextMenu()
    const newNodeId = await onNodeCreate(typeName, graphX, graphY)
    if (!pendingConnection || !compatiblePin || !newNodeId || !activeLogicBlock) return
    const payload = edgePayloadForPendingConnection(pendingConnection, String(newNodeId), compatiblePin, activeLogicBlock)
    if (hasIncomingConnection(graph, activeLogicBlock, payload.targetNode, payload.targetPin, payload.kind)) return
    await onEdgeCreate?.(payload)
  }, [activeLogicBlock, closeContextMenu, contextMenu, graph, onEdgeCreate, onNodeCreate, state?.types])

  useEffect(() => {
    if (!contextMenu) return
    function close() {
      setContextMenu(null)
    }
    function onKeyDown(event: KeyboardEvent) {
      if (event.key === 'Escape') close()
    }
    window.addEventListener('click', close)
    window.addEventListener('keydown', onKeyDown)
    window.addEventListener('blur', close)
    return () => {
      window.removeEventListener('click', close)
      window.removeEventListener('keydown', onKeyDown)
      window.removeEventListener('blur', close)
    }
  }, [contextMenu])

  useEffect(() => {
    function onKeyDown(event: KeyboardEvent) {
      if (event.defaultPrevented || isEditableKeyboardTarget(event.target)) return
      if (event.key !== 'f' && event.key !== 'F') return
      event.preventDefault()

      const selectedNodes = nodes.filter(node => node.selected)
      if (selectedNodes.length > 0) {
        void reactFlow.fitView({
          nodes: selectedNodes.map(node => ({ id: node.id })),
          padding: 0.35,
          duration: 180,
        })
        return
      }

      const selectedEdges = edges.filter(edge => edge.selected)
      if (selectedEdges.length > 0) {
        const nodeIds = new Set<string>()
        for (const edge of selectedEdges) {
          nodeIds.add(edge.source)
          nodeIds.add(edge.target)
        }
        void reactFlow.fitView({
          nodes: Array.from(nodeIds).map(id => ({ id })),
          padding: 0.4,
          duration: 180,
        })
        return
      }

      void reactFlow.fitView({
        padding: 0.25,
        duration: 180,
      })
    }

    window.addEventListener('keydown', onKeyDown)
    return () => window.removeEventListener('keydown', onKeyDown)
  }, [edges, nodes, reactFlow])

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
            Create a graph from the toolbar or load a .gs file.
          </span>
        </div>
      </div>
    )
  }

  return (
    <div
      ref={wrapperRef}
      className="w-full h-full relative blueprint-grid"
      data-canvas-context-root="true"
      onPointerDown={onPanePointerDown}
      onPointerMove={onPanePointerMove}
      onPointerUp={onPanePointerUp}
      onPointerCancel={onPanePointerUp}
    >
      <ReactFlow
        nodes={nodes}
        edges={edges}
        nodeTypes={nodeTypes}
        onNodesChange={onNodesChange}
        onEdgesChange={onEdgesChange}
        onNodeDragStop={onNodeDragStop}
        onNodeClick={onNodeClick}
        onEdgeClick={onEdgeClick}
        onSelectionChange={onSelectionChange}
        onPaneClick={() => {
          setNodes(current => current.map(node => node.selected ? { ...node, selected: false } : node))
          onNodeSelect?.(null)
          onEdgeSelect?.(null)
          closeContextMenu()
        }}
        onPaneContextMenu={onPaneContextMenu}
        onConnectStart={onConnectStart}
        onConnect={onConnect}
        onConnectEnd={onConnectEnd}
        onReconnect={onReconnect}
        onNodesDelete={onNodesDelete}
        onEdgesDelete={onEdgesDelete}
        isValidConnection={isValidConnection}
        connectionMode={ConnectionMode.Strict}
        elementsSelectable
        selectionOnDrag
        selectionMode={SelectionMode.Partial}
        selectNodesOnDrag={false}
        panOnDrag={[1, 2]}
        panOnScroll
        panActivationKeyCode="Space"
        multiSelectionKeyCode={['Shift', 'Control', 'Meta']}
        selectionKeyCode={null}
        autoPanOnSelection
        edgesReconnectable
        elevateEdgesOnSelect
        elevateNodesOnSelect
        reconnectRadius={28}
        fitView
        minZoom={0.2}
        maxZoom={1.8}
        nodeDragThreshold={1}
        connectionDragThreshold={2}
        defaultEdgeOptions={{
          type: 'smoothstep',
          reconnectable: true,
        }}
        deleteKeyCode={['Backspace', 'Delete']}
        proOptions={{ hideAttribution: true }}
      >
        <Background color="oklch(0.65 0.01 250 / 0.35)" gap={18} size={1.2} />
        <Controls showInteractive={false} className="!bg-card/90 !border-border" />
        <MiniMap
          pannable
          zoomable
          nodeStrokeColor="var(--color-primary)"
          nodeColor="oklch(0.2 0.04 250)"
          maskColor="oklch(0 0 0 / 0.45)"
        />
      </ReactFlow>

      {!activeLogicBlock && (
        <div className="pointer-events-none absolute left-3 top-3 z-20 rounded-md border border-warning/25 bg-card/90 px-3 py-2 text-[11px] text-warning shadow-lg">
          Select or create an event/function block before connecting pins.
        </div>
      )}

      {contextMenu && (
        <div
          className="absolute z-50 w-80 rounded-md border border-border bg-card/95 shadow-xl backdrop-blur p-1"
          style={{
            left: Math.min(contextMenu.localX, Math.max(8, wrapperRef.current ? wrapperRef.current.clientWidth - 328 : contextMenu.localX)),
            top: contextMenu.localY,
          }}
          onClick={event => event.stopPropagation()}
          onContextMenu={event => {
            event.preventDefault()
            event.stopPropagation()
          }}
          data-canvas-context-menu="true"
        >
          <div className="flex items-center justify-between px-2 py-1.5 text-[10px] uppercase tracking-wider text-muted-foreground">
            <span>
              {contextMenu.pendingConnection
                ? `Add and connect ${pendingConnectionLabel(contextMenu.pendingConnection)}`
                : `Add node at ${contextMenu.graphX}, ${contextMenu.graphY}`}
            </span>
            <span>{Math.round(contextMenu.zoom * 100)}%</span>
          </div>
          <input
            className="mb-1 h-7 w-full rounded-sm border border-border bg-secondary/40 px-2 text-[11px] outline-none focus:border-primary/70"
            placeholder={contextMenu.pendingConnection ? 'Search compatible node or pin' : 'Search node type, pin, field, or tag'}
            value={contextMenu.query}
            onChange={event => setContextMenu(current => current ? { ...current, query: event.target.value } : current)}
            autoFocus
          />
          <div className="max-h-72 overflow-y-auto">
            {contextNodeGroups.map(group => (
              <div key={group.category} className="pb-1">
                <div className="px-2 py-1 text-[9px] font-bold uppercase tracking-[0.1em] text-muted-foreground/60">
                  {group.category}
                </div>
                {group.items.map(type => (
                  <button
                    key={type.type_name}
                    className="flex w-full items-center justify-between rounded-sm px-2 py-1.5 text-left text-[11px] text-foreground/80 hover:bg-accent/70"
                    onClick={() => void createNodeFromContext(type.type_name)}
                    data-canvas-context-add-node={type.type_name}
                  >
                    <span className="min-w-0">
                      <span className="block truncate font-medium">{type.type_name}</span>
                      <span className="block truncate text-[9px] text-muted-foreground/55">
                        {contextPinSummary(type, contextMenu.pendingConnection)}
                      </span>
                    </span>
                    <span className="ml-2 shrink-0 rounded-sm bg-secondary/70 px-1 text-[10px] text-muted-foreground">
                      {contextMenu.pendingConnection
                        ? compatiblePinsForPending(type, contextMenu.pendingConnection).length
                        : type.pins.length}
                    </span>
                  </button>
                ))}
              </div>
            ))}
            {contextNodeTypes.length === 0 && (
              <div className="px-3 py-5 text-center text-[11px] text-muted-foreground">
                <div>{contextMenu.pendingConnection ? 'No compatible nodes' : 'No matching nodes'}</div>
                <div className="mt-1 text-[10px] text-muted-foreground/55">
                  {contextMenu.pendingConnection
                    ? 'No loaded declaration exposes a compatible opposite-direction pin.'
                    : 'Loaded declarations and graph node types are shown here.'}
                </div>
              </div>
            )}
          </div>
          <div className="px-2 py-1 text-[10px] text-muted-foreground/55">
            {contextMenu.pendingConnection
              ? 'Showing compatible loaded declarations only.'
              : 'Showing loaded declarations and graph node types.'}
          </div>
          <div className="mt-1 border-t border-border/70 pt-1">
            <button
              className="flex w-full items-center rounded-sm px-2 py-1.5 text-left text-[11px] text-foreground/80 hover:bg-accent/70"
              onClick={() => {
                closeContextMenu()
                void onRefresh?.()
              }}
            >
              Refresh
            </button>
          </div>
        </div>
      )}
    </div>
  )
}

export default function FlowCanvas(props: FlowCanvasProps) {
  return (
    <ReactFlowProvider>
      <FlowCanvasInner {...props} />
    </ReactFlowProvider>
  )
}
