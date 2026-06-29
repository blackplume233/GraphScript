import { useCallback, useEffect, useMemo, useRef, useState, type MouseEvent as ReactMouseEvent } from 'react'
import {
  applyEdgeChanges,
  applyNodeChanges,
  Background,
  BaseEdge,
  ConnectionMode,
  Controls,
  getSmoothStepPath,
  MarkerType,
  MiniMap,
  Position,
  ReactFlow,
  ReactFlowProvider,
  SelectionMode,
  useReactFlow,
  type Connection,
  type Edge,
  type EdgeChange,
  type EdgeProps,
  type Node,
  type NodeChange,
  type NodeMouseHandler,
  type NodeProps,
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
  onNodesDuplicate?: (instanceNames: string[]) => Promise<void> | void
  onCommentBoxCreate?: (box: CommentBoxEditPayload) => Promise<void> | void
  onCommentBoxMove?: (box: CommentBoxEditPayload) => Promise<void> | void
  onCommentBoxDelete?: (annotationName: string) => Promise<void> | void
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

interface ConnectionFeedback {
  localX: number
  localY: number
  message: string
  tone: 'warning' | 'error'
}

interface PointerDragState {
  button: number
  startX: number
  startY: number
  dragged: boolean
}

interface SelectionSnapshot {
  nodeId: string
  additive: boolean
  selectedIds: Set<string>
}

interface ManualReconnectState {
  edge: EdgeEditPayload
  endpoint: 'source' | 'target'
}

type NodeAlignment = 'left' | 'right' | 'top' | 'bottom' | 'middle' | 'center'
type NodeDistribution = 'horizontal' | 'vertical'

export interface CommentBoxEditPayload {
  annotationName: string
  text: string
  x: number
  y: number
  width: number
  height: number
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

type GraphScriptEdge = Edge<EdgeEditPayload, 'blueprint'>

interface CommentBoxNodeData extends Record<string, unknown> {
  annotationName: string
  text: string
  width: number
  height: number
}

type CommentBoxFlowNode = Node<CommentBoxNodeData, 'commentBox'>
type GraphScriptNode = BlueprintFlowNode | CommentBoxFlowNode

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

function nodeMeasuredWidth(node: BlueprintFlowNode): number {
  return node.measured?.width ?? node.width ?? 190
}

function nodeMeasuredHeight(node: BlueprintFlowNode): number {
  return node.measured?.height ?? node.height ?? 96
}

function graphNodeMeasuredWidth(node: GraphScriptNode): number {
  if (node.type === 'commentBox') return node.data.width
  return nodeMeasuredWidth(node)
}

function graphNodeMeasuredHeight(node: GraphScriptNode): number {
  if (node.type === 'commentBox') return node.data.height
  return nodeMeasuredHeight(node)
}

function annotationArg(annotation: Annotation, name: string): string | undefined {
  return annotation.args.find(arg => arg.name === name)?.value
}

function annotationNumber(annotation: Annotation, name: string, fallback: number): number {
  const value = Number(annotationArg(annotation, name))
  return Number.isFinite(value) ? value : fallback
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

function connectionFailureMessage(
  graph: GraphDef,
  state: GSState,
  activeLogicBlock: LogicBlockRef | null | undefined,
  pending: PendingConnection,
  target: Element | null,
): string {
  const handle = target?.closest('.react-flow__handle')
  if (!handle) return 'Release on empty canvas to search compatible nodes'

  const port = parsePortId(handle.getAttribute('data-port-id'))
  const nodeEl = handle.closest('.react-flow__node')
  const nodeId = nodeEl?.getAttribute('data-id') ?? ''
  const nodeType = nodeId ? findNodeTypeForInstance(graph, state.types, nodeId) : undefined
  const targetPin = port
    ? nodeType?.pins.find(item =>
      item.kind === port.kind &&
      item.direction === port.direction &&
      item.name === port.pin)
    : undefined

  if (!port) return 'This socket cannot accept a graph connection'
  if (nodeId === pending.nodeId && port.pin === pending.port.pin) return 'Cannot connect a pin to itself'
  if (port.direction === pending.port.direction) return 'Drag from an output pin to an input pin'
  if (port.kind !== pending.port.kind) return 'Exec pins connect to exec pins; data pins connect to matching data pins'
  if (port.kind === 'data' && targetPin && !dataTypesCompatible(targetPin.type, pending.type)) {
    return `Data type mismatch: ${pending.type || 'value'} -> ${targetPin.type || 'value'}`
  }
  if (activeLogicBlock && hasIncomingConnection(graph, activeLogicBlock, nodeId, port.pin, port.kind)) {
    return `${nodeId}.${port.pin} already has an incoming ${port.kind} connection`
  }
  return 'This connection is not valid in the active graph scope'
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

function edgeDomId(edge: EdgeEditPayload): string {
  return `${edge.sourceNode}_${edge.kind}-out-${edge.sourcePin}-${edge.targetNode}_${edge.kind}-in-${edge.targetPin}`
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

function reconnectPoint(x: number, y: number, position: Position, distance: number): { x: number, y: number } {
  switch (position) {
    case Position.Left:
      return { x: x - distance, y }
    case Position.Right:
      return { x: x + distance, y }
    case Position.Top:
      return { x, y: y - distance }
    case Position.Bottom:
      return { x, y: y + distance }
    default:
      return { x, y }
  }
}

function BlueprintEdge({
  id,
  sourceX,
  sourceY,
  targetX,
  targetY,
  sourcePosition,
  targetPosition,
  markerEnd,
  style,
  data,
}: EdgeProps<GraphScriptEdge>) {
  const [edgePath] = getSmoothStepPath({
    sourceX,
    sourceY,
    sourcePosition,
    targetX,
    targetY,
    targetPosition,
  })
  const sourceReconnect = reconnectPoint(sourceX, sourceY, sourcePosition, 24)
  const targetReconnect = reconnectPoint(targetX, targetY, targetPosition, 24)

  return (
    <g
      data-line-id={data ? edgeDomId(data) : id}
      data-edge-id={id}
      data-testid="sdk.workflow.canvas.line"
    >
      <BaseEdge
        id={id}
        path={edgePath}
        markerEnd={markerEnd}
        style={style}
        interactionWidth={22}
      />
      {data && (
        <>
          <circle
            cx={sourceReconnect.x}
            cy={sourceReconnect.y}
            r={9}
            fill="transparent"
            stroke="transparent"
            strokeWidth={2}
            pointerEvents="all"
            data-edge-reconnect-handle="source"
            onPointerDown={(event) => {
              event.stopPropagation()
              window.dispatchEvent(new CustomEvent('graphscript:edge-reconnect-start', {
                detail: { edge: data, endpoint: 'source' },
              }))
            }}
          />
          <circle
            cx={targetReconnect.x}
            cy={targetReconnect.y}
            r={9}
            fill="transparent"
            stroke="transparent"
            strokeWidth={2}
            pointerEvents="all"
            data-edge-reconnect-handle="target"
            onPointerDown={(event) => {
              event.stopPropagation()
              window.dispatchEvent(new CustomEvent('graphscript:edge-reconnect-start', {
                detail: { edge: data, endpoint: 'target' },
              }))
            }}
          />
        </>
      )}
    </g>
  )
}

function CommentBoxNode({ data, selected }: NodeProps<CommentBoxFlowNode>) {
  return (
    <div
      className="rounded-[6px] border px-3 py-2 text-[12px] font-semibold text-foreground/80 shadow-sm"
      data-comment-box={data.annotationName}
      style={{
        width: data.width,
        height: data.height,
        background: 'oklch(0.18 0.04 92 / 0.38)',
        borderColor: selected ? 'oklch(0.78 0.16 82)' : 'oklch(0.64 0.12 82 / 0.55)',
        boxShadow: selected
          ? '0 0 0 1px oklch(0.78 0.16 82), 0 8px 20px oklch(0 0 0 / 0.34)'
          : '0 6px 18px oklch(0 0 0 / 0.24)',
      }}
    >
      <div className="truncate uppercase tracking-[0.08em] text-[10px] text-yellow-200/80">
        {data.text || 'Comment'}
      </div>
    </div>
  )
}

const nodeTypes = {
  blueprint: BlueprintNode,
  commentBox: CommentBoxNode,
}

const edgeTypes = {
  blueprint: BlueprintEdge,
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

function toReactFlowCommentBoxes(graph: GraphDef): CommentBoxFlowNode[] {
  return graph.annotations
    .filter(annotation => /^CommentBox_[A-Za-z0-9_]+$/.test(annotation.name))
    .map(annotation => {
      const x = annotationNumber(annotation, 'X', 120)
      const y = annotationNumber(annotation, 'Y', 120)
      const width = Math.max(160, annotationNumber(annotation, 'W', 360))
      const height = Math.max(100, annotationNumber(annotation, 'H', 220))
      return {
        id: `comment:${annotation.name}`,
        type: 'commentBox',
        position: { x, y },
        data: {
          annotationName: annotation.name,
          text: annotationArg(annotation, 'Text') ?? 'Comment',
          width,
          height,
        },
        zIndex: -1,
      }
    })
}

function preserveNodeSelection(nextNodes: GraphScriptNode[], currentNodes: GraphScriptNode[]): GraphScriptNode[] {
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
      type: 'blueprint',
      source: payload.sourceNode,
      target: payload.targetNode,
      sourceHandle: `exec-out-${payload.sourcePin}`,
      targetHandle: `exec-in-${payload.targetPin}`,
      data: payload,
      animated: highlight?.severity === 'warning',
      reconnectable: false,
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
      type: 'blueprint',
      source: payload.sourceNode,
      target: payload.targetNode,
      sourceHandle: `data-out-${payload.sourcePin}`,
      targetHandle: `data-in-${payload.targetPin}`,
      data: payload,
      animated: highlight?.severity === 'warning',
      reconnectable: false,
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
  onNodesDuplicate,
  onCommentBoxCreate,
  onCommentBoxMove,
  onCommentBoxDelete,
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
  const reactFlow = useReactFlow<GraphScriptNode, GraphScriptEdge>()
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
  const selectionSnapshotRef = useRef<SelectionSnapshot | null>(null)
  const manualReconnectRef = useRef<ManualReconnectState | null>(null)

  const initialNodes = useMemo(() => {
    if (!state || !graph) return []
    return [
      ...toReactFlowCommentBoxes(graph),
      ...toReactFlowNodes(graph, state, diagnosticHighlights, connectionPreview),
    ]
  }, [connectionPreview, diagnosticHighlights, graph, state])

  const initialEdges = useMemo(() => {
    if (!graph) return []
    return toReactFlowEdges(graph, activeBlock, diagnosticHighlights)
  }, [activeBlock, diagnosticHighlights, graph])

  const [nodes, setNodes] = useState<GraphScriptNode[]>(initialNodes)
  const [edges, setEdges] = useState<GraphScriptEdge[]>(initialEdges)
  const [connectionFeedback, setConnectionFeedback] = useState<ConnectionFeedback | null>(null)
  const nodesRef = useRef<GraphScriptNode[]>(nodes)
  const feedbackTimerRef = useRef<number | null>(null)

  useEffect(() => {
    nodesRef.current = nodes
  }, [nodes])

  useEffect(() => {
    function onBlueprintNodePointerDown(event: Event) {
      const detail = (event as CustomEvent<{ nodeId?: string, additive?: boolean }>).detail
      if (!detail?.nodeId) return
      selectionSnapshotRef.current = {
        nodeId: detail.nodeId,
        additive: Boolean(detail.additive),
        selectedIds: new Set(nodesRef.current.filter(item => item.selected).map(item => item.id)),
      }
    }

    window.addEventListener('graphscript:node-pointer-down', onBlueprintNodePointerDown)
    return () => window.removeEventListener('graphscript:node-pointer-down', onBlueprintNodePointerDown)
  }, [])

  useEffect(() => {
    function onManualReconnectStart(event: Event) {
      const detail = (event as CustomEvent<ManualReconnectState>).detail
      if (!detail?.edge || (detail.endpoint !== 'source' && detail.endpoint !== 'target')) return
      manualReconnectRef.current = detail
    }

    window.addEventListener('graphscript:edge-reconnect-start', onManualReconnectStart)
    return () => window.removeEventListener('graphscript:edge-reconnect-start', onManualReconnectStart)
  }, [])

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

  const showConnectionFeedback = useCallback((clientX: number, clientY: number, message: string, tone: ConnectionFeedback['tone'] = 'warning') => {
    if (!wrapperRef.current) return
    const rect = wrapperRef.current.getBoundingClientRect()
    if (feedbackTimerRef.current !== null) {
      window.clearTimeout(feedbackTimerRef.current)
    }
    setConnectionFeedback({
      localX: Math.max(12, Math.min(clientX - rect.left, Math.max(12, rect.width - 320))),
      localY: Math.max(12, Math.min(clientY - rect.top, Math.max(12, rect.height - 72))),
      message,
      tone,
    })
    feedbackTimerRef.current = window.setTimeout(() => {
      setConnectionFeedback(null)
      feedbackTimerRef.current = null
    }, 1800)
  }, [])

  useEffect(() => {
    return () => {
      if (feedbackTimerRef.current !== null) {
        window.clearTimeout(feedbackTimerRef.current)
      }
    }
  }, [])

  const onNodesChange = useCallback((changes: NodeChange<GraphScriptNode>[]) => {
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
    if (!payload || !isValidConnection(connection)) {
      showConnectionFeedback(24, 24, `Cannot connect ${connection.source ?? 'source'} to ${connection.target ?? 'target'}`, 'error')
      return
    }
    void onEdgeCreate?.(payload)
  }, [activeLogicBlock, isValidConnection, onEdgeCreate, showConnectionFeedback])

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

    if (!pending || !graph || !state || !wrapperRef.current) return
    if (connectionState.isValid) return
    const target = event.target instanceof Element ? event.target : null
    const client = eventClientPosition(event)
    if (!client) return
    if (target?.closest('.react-flow__handle')) {
      showConnectionFeedback(
        client.x,
        client.y,
        connectionFailureMessage(graph, state, activeLogicBlock, pending, target),
        'error',
      )
      return
    }

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
  }, [activeLogicBlock, graph, reactFlow, showConnectionFeedback, state])

  const applyReconnect = useCallback((previous: EdgeEditPayload | undefined, newConnection: Connection, client?: { x: number, y: number }) => {
    if (!previous) {
      const point = client ?? { x: 24, y: 24 }
      showConnectionFeedback(point.x, point.y, 'Reconnect failed: the edge has no editable graph payload', 'error')
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
      const point = client ?? { x: 24, y: 24 }
      showConnectionFeedback(point.x, point.y, 'Reconnect failed: incompatible pin or duplicate target input', 'error')
      void onRefresh?.()
      return
    }
    void onEdgeReconnect?.(previous, next)
  }, [graph, onEdgeReconnect, onRefresh, showConnectionFeedback])

  const onReconnect = useCallback<OnReconnect<GraphScriptEdge>>((oldEdge, newConnection) => {
    applyReconnect(oldEdge.data, newConnection)
  }, [applyReconnect])

  useEffect(() => {
    function onManualReconnectEnd(event: PointerEvent) {
      const reconnecting = manualReconnectRef.current
      if (!reconnecting) return
      manualReconnectRef.current = null

      const target = document.elementFromPoint(event.clientX, event.clientY)
      const handle = target?.closest('.react-flow__handle')
      const handleId = handle?.getAttribute('data-port-id') ?? ''
      const port = parsePortId(handleId)
      const nodeEl = handle?.closest('.react-flow__node')
      const nodeId = nodeEl?.getAttribute('data-id') ?? ''
      if (!port || !nodeId) {
        showConnectionFeedback(event.clientX, event.clientY, 'Release on a compatible pin to reconnect this edge', 'warning')
        return
      }
      if (port.kind !== reconnecting.edge.kind) {
        showConnectionFeedback(event.clientX, event.clientY, 'Reconnect within the same pin kind: exec to exec, data to data', 'error')
        return
      }
      if (reconnecting.endpoint === 'source' && port.direction !== 'out') {
        showConnectionFeedback(event.clientX, event.clientY, 'Reconnect the source endpoint to an output pin', 'error')
        return
      }
      if (reconnecting.endpoint === 'target' && port.direction !== 'in') {
        showConnectionFeedback(event.clientX, event.clientY, 'Reconnect the target endpoint to an input pin', 'error')
        return
      }

      const edge = reconnecting.edge
      const connection: Connection = reconnecting.endpoint === 'source'
        ? {
          source: nodeId,
          sourceHandle: handleId,
          target: edge.targetNode,
          targetHandle: `${edge.kind}-in-${edge.targetPin}`,
        }
        : {
          source: edge.sourceNode,
          sourceHandle: `${edge.kind}-out-${edge.sourcePin}`,
          target: nodeId,
          targetHandle: handleId,
        }
      applyReconnect(edge, connection, { x: event.clientX, y: event.clientY })
    }

    window.addEventListener('pointerup', onManualReconnectEnd)
    return () => window.removeEventListener('pointerup', onManualReconnectEnd)
  }, [applyReconnect, showConnectionFeedback])

  const onEdgesDelete = useCallback((deleted: GraphScriptEdge[]) => {
    for (const edge of deleted) {
      if (edge.data) void onEdgeDelete?.(edge.data)
    }
  }, [onEdgeDelete])

  const onNodesDelete = useCallback<OnNodesDelete<GraphScriptNode>>((deleted) => {
    for (const node of deleted) {
      if (node.type === 'commentBox') {
        void onCommentBoxDelete?.(node.data.annotationName)
      } else {
        void onNodeDelete?.(node.id)
      }
    }
    onNodeSelect?.(null)
    onEdgeSelect?.(null)
  }, [onCommentBoxDelete, onEdgeSelect, onNodeDelete, onNodeSelect])

  const onNodeDragStop = useCallback((_event: globalThis.MouseEvent | TouchEvent, node: GraphScriptNode) => {
    if (node.type === 'commentBox') {
      void onCommentBoxMove?.({
        annotationName: node.data.annotationName,
        text: node.data.text,
        x: Math.round(node.position.x),
        y: Math.round(node.position.y),
        width: node.data.width,
        height: node.data.height,
      })
      return
    }

    const selectedNodes = nodes.filter((item): item is BlueprintFlowNode => item.type === 'blueprint' && Boolean(item.selected))
    const nodesToPersist = selectedNodes.length > 1 && selectedNodes.some(item => item.id === node.id)
      ? selectedNodes
      : [node]

    for (const item of nodesToPersist) {
      const position = roundPosition(item.position)
      void onNodeMove?.(item.id, position.x, position.y)
    }
  }, [nodes, onNodeMove])

  const onNodeClick = useCallback<NodeMouseHandler<GraphScriptNode>>((event, node) => {
    if (node.type === 'commentBox') {
      onNodeSelect?.(null)
      onEdgeSelect?.(null)
      return
    }
    const additive = event.shiftKey || event.ctrlKey || event.metaKey
    window.setTimeout(() => {
      const snapshot = selectionSnapshotRef.current
      const snapshotMatches = Boolean(snapshot && snapshot.nodeId === node.id && snapshot.additive === additive)
      const selectedIdsBefore = snapshotMatches && snapshot
        ? snapshot.selectedIds
        : new Set(nodesRef.current.filter(item => item.selected).map(item => item.id))
      const nodeSelected = additive ? !selectedIdsBefore.has(node.id) : true
      onNodeSelect?.(nodeSelected ? node.id : null)
      setNodes(current => {
        return current.map(item => {
          if (item.id !== node.id) {
            return additive ? { ...item, selected: selectedIdsBefore.has(item.id) } : { ...item, selected: false }
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

  const onSelectionChange = useCallback<OnSelectionChangeFunc<GraphScriptNode, GraphScriptEdge>>(({ nodes: selectedNodes, edges: selectedEdges }) => {
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
      onNodeSelect?.(node.type === 'blueprint' ? node.id : null)
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
    if (hasIncomingConnection(graph, activeLogicBlock, payload.targetNode, payload.targetPin, payload.kind)) {
      const rect = wrapperRef.current?.getBoundingClientRect()
      showConnectionFeedback(
        (rect?.left ?? 0) + contextMenu.localX,
        (rect?.top ?? 0) + contextMenu.localY,
        `${payload.targetNode}.${payload.targetPin} already has an incoming connection`,
        'error',
      )
      return
    }
    await onEdgeCreate?.(payload)
  }, [activeLogicBlock, closeContextMenu, contextMenu, graph, onEdgeCreate, onNodeCreate, showConnectionFeedback, state?.types])

  const alignSelectedNodes = useCallback((alignment: NodeAlignment): boolean => {
    const selectedNodes = nodes.filter((node): node is BlueprintFlowNode => node.type === 'blueprint' && Boolean(node.selected))
    if (selectedNodes.length < 2) return false

    const bounds = selectedNodes.reduce((acc, node) => {
      const width = nodeMeasuredWidth(node)
      const height = nodeMeasuredHeight(node)
      return {
        left: Math.min(acc.left, node.position.x),
        right: Math.max(acc.right, node.position.x + width),
        top: Math.min(acc.top, node.position.y),
        bottom: Math.max(acc.bottom, node.position.y + height),
      }
    }, {
      left: Number.POSITIVE_INFINITY,
      right: Number.NEGATIVE_INFINITY,
      top: Number.POSITIVE_INFINITY,
      bottom: Number.NEGATIVE_INFINITY,
    })

    const nextPositions = new Map<string, FlowPosition>()
    for (const node of selectedNodes) {
      const width = nodeMeasuredWidth(node)
      const height = nodeMeasuredHeight(node)
      const next = roundPosition({
        x: alignment === 'left'
          ? bounds.left
          : alignment === 'right'
            ? bounds.right - width
            : alignment === 'center'
              ? (bounds.left + bounds.right) / 2 - width / 2
              : node.position.x,
        y: alignment === 'top'
          ? bounds.top
          : alignment === 'bottom'
            ? bounds.bottom - height
            : alignment === 'middle'
              ? (bounds.top + bounds.bottom) / 2 - height / 2
              : node.position.y,
      })
      const current = roundPosition(node.position)
      if (next.x !== current.x || next.y !== current.y) {
        nextPositions.set(node.id, next)
      }
    }
    if (nextPositions.size === 0) return true

    setNodes(current => current.map(node => {
      const next = nextPositions.get(node.id)
      return next ? { ...node, position: next } : node
    }))
    for (const [nodeId, position] of nextPositions) {
      void onNodeMove?.(nodeId, position.x, position.y)
    }
    return true
  }, [nodes, onNodeMove])

  const straightenSelectedEdges = useCallback((): boolean => {
    const selectedEdges = edges.filter(edge => edge.selected)
    if (selectedEdges.length === 0) return false

    const nodesById = new Map(nodes.filter((node): node is BlueprintFlowNode => node.type === 'blueprint').map(node => [node.id, node]))
    const nextPositions = new Map<string, FlowPosition>()
    for (const edge of selectedEdges) {
      const source = nodesById.get(edge.source)
      const target = nodesById.get(edge.target)
      if (!source || !target) continue
      const sourceCenterY = source.position.y + nodeMeasuredHeight(source) / 2
      const next = roundPosition({
        x: target.position.x,
        y: sourceCenterY - nodeMeasuredHeight(target) / 2,
      })
      const current = roundPosition(target.position)
      if (next.x !== current.x || next.y !== current.y) {
        nextPositions.set(target.id, next)
      }
    }
    if (nextPositions.size === 0) return true

    setNodes(current => current.map(node => {
      const next = nextPositions.get(node.id)
      return next ? { ...node, position: next } : node
    }))
    for (const [nodeId, position] of nextPositions) {
      void onNodeMove?.(nodeId, position.x, position.y)
    }
    return true
  }, [edges, nodes, onNodeMove])

  const distributeSelectedNodes = useCallback((distribution: NodeDistribution): boolean => {
    const selectedNodes = nodes.filter((node): node is BlueprintFlowNode => node.type === 'blueprint' && Boolean(node.selected))
    if (selectedNodes.length < 3) return false

    const sorted = [...selectedNodes].sort((left, right) => {
      return distribution === 'horizontal'
        ? left.position.x - right.position.x
        : left.position.y - right.position.y
    })
    const first = sorted[0]
    const last = sorted[sorted.length - 1]
    const totalSize = sorted.reduce((sum, node) => {
      return sum + (distribution === 'horizontal' ? nodeMeasuredWidth(node) : nodeMeasuredHeight(node))
    }, 0)
    const start = distribution === 'horizontal' ? first.position.x : first.position.y
    const end = distribution === 'horizontal'
      ? last.position.x + nodeMeasuredWidth(last)
      : last.position.y + nodeMeasuredHeight(last)
    const gap = (end - start - totalSize) / (sorted.length - 1)

    let cursor = start
    const nextPositions = new Map<string, FlowPosition>()
    for (const node of sorted) {
      const size = distribution === 'horizontal' ? nodeMeasuredWidth(node) : nodeMeasuredHeight(node)
      const next = roundPosition({
        x: distribution === 'horizontal' ? cursor : node.position.x,
        y: distribution === 'vertical' ? cursor : node.position.y,
      })
      const current = roundPosition(node.position)
      if (next.x !== current.x || next.y !== current.y) {
        nextPositions.set(node.id, next)
      }
      cursor += size + gap
    }
    if (nextPositions.size === 0) return true

    setNodes(current => current.map(node => {
      const next = nextPositions.get(node.id)
      return next ? { ...node, position: next } : node
    }))
    for (const [nodeId, position] of nextPositions) {
      void onNodeMove?.(nodeId, position.x, position.y)
    }
    return true
  }, [nodes, onNodeMove])

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
      const key = event.key.toLowerCase()
      const duplicateRequested = (event.ctrlKey || event.metaKey) && (key === 'd' || key === 'w')
      if (duplicateRequested) {
        const selectedNodes = nodes.filter(node => node.type === 'blueprint' && node.selected)
        if (selectedNodes.length === 0) return
        event.preventDefault()
        void onNodesDuplicate?.(selectedNodes.map(node => node.id))
        return
      }

      if (key === 'c' && !event.ctrlKey && !event.metaKey && !event.altKey && !event.shiftKey) {
        const selectedNodes = nodes.filter(node => node.type === 'blueprint' && node.selected)
        if (selectedNodes.length === 0 || !onCommentBoxCreate) return
        const left = Math.min(...selectedNodes.map(node => node.position.x))
        const top = Math.min(...selectedNodes.map(node => node.position.y))
        const right = Math.max(...selectedNodes.map(node => node.position.x + graphNodeMeasuredWidth(node)))
        const bottom = Math.max(...selectedNodes.map(node => node.position.y + graphNodeMeasuredHeight(node)))
        event.preventDefault()
        void onCommentBoxCreate({
          annotationName: '',
          text: 'Comment',
          x: Math.round(left - 32),
          y: Math.round(top - 56),
          width: Math.round(right - left + 64),
          height: Math.round(bottom - top + 88),
        })
        return
      }

      const alignmentByKey: Record<string, NodeAlignment> = {
        a: 'left',
        d: 'right',
        w: 'top',
        s: 'bottom',
      }
      const alignment = event.shiftKey && !event.ctrlKey && !event.metaKey
        ? event.altKey
          ? key === 'w'
            ? 'middle'
            : key === 's'
              ? 'center'
              : undefined
          : alignmentByKey[key]
        : undefined
      if (alignment && alignSelectedNodes(alignment)) {
        event.preventDefault()
        return
      }

      if (key === 'q' && !event.ctrlKey && !event.metaKey && !event.altKey && !event.shiftKey && straightenSelectedEdges()) {
        event.preventDefault()
        return
      }

      const distribution = event.shiftKey && event.altKey && !event.ctrlKey && !event.metaKey
        ? key === 'h'
          ? 'horizontal'
          : key === 'v'
            ? 'vertical'
            : undefined
        : undefined
      if (distribution && distributeSelectedNodes(distribution)) {
        event.preventDefault()
        return
      }

      if (key !== 'f') return
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
  }, [alignSelectedNodes, distributeSelectedNodes, edges, nodes, onNodesDuplicate, reactFlow, straightenSelectedEdges])

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
        edgeTypes={edgeTypes}
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
        edgesReconnectable={false}
        elevateEdgesOnSelect
        elevateNodesOnSelect
        reconnectRadius={28}
        fitView
        minZoom={0.2}
        maxZoom={1.8}
        nodeDragThreshold={1}
        connectionDragThreshold={2}
        defaultEdgeOptions={{
          type: 'blueprint',
          reconnectable: false,
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

      {connectionFeedback && (
        <div
          className={`pointer-events-none absolute z-50 max-w-[320px] rounded-md border px-3 py-2 text-xs shadow-xl backdrop-blur ${
            connectionFeedback.tone === 'error'
              ? 'border-destructive/60 bg-destructive/15 text-destructive-foreground'
              : 'border-warning/60 bg-warning/15 text-warning-foreground'
          }`}
          style={{
            left: connectionFeedback.localX,
            top: connectionFeedback.localY,
          }}
          data-connection-feedback={connectionFeedback.tone}
        >
          {connectionFeedback.message}
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
