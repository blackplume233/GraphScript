import type { GSState, GraphDef, NodeInst, PinDef, FlowConn, DataLink, NodeTypeDef } from '@/api/types'

export interface FlowNode {
  id: string
  type: string
  meta: { position: { x: number; y: number } }
  data: {
    label: string
    typeName: string
    instanceName: string
    init: string
    pins: PinDef[]
  }
}

export interface FlowEdge {
  sourceNodeID: string
  targetNodeID: string
  sourcePortID: string
  targetPortID: string
}

export interface FlowGraphData {
  nodes: FlowNode[]
  edges: FlowEdge[]
}

function findAnnotationArg(_node: NodeInst, _annoName: string, _argName: string, _state: GSState): string | undefined {
  return undefined
}

/**
 * Parse position from node annotations. The backend doesn't currently include
 * annotations in the JSON state, so we use a simple grid layout as fallback.
 */
function getNodePosition(node: NodeInst, index: number): { x: number; y: number } {
  void findAnnotationArg
  void node
  const col = index % 4
  const row = Math.floor(index / 4)
  return { x: 200 + col * 300, y: 100 + row * 200 }
}

function getPinsForType(typeName: string, types: NodeTypeDef[]): PinDef[] {
  const def = types.find(t => t.type_name === typeName)
  return def?.pins ?? []
}

export function graphToFlowData(
  graph: GraphDef,
  state: GSState,
): FlowGraphData {
  const nodes: FlowNode[] = graph.nodes.map((ni, i) => ({
    id: ni.instance,
    type: ni.type,
    meta: { position: getNodePosition(ni, i) },
    data: {
      label: ni.instance,
      typeName: ni.type,
      instanceName: ni.instance,
      init: ni.init,
      pins: getPinsForType(ni.type, state.types),
    },
  }))

  const edges: FlowEdge[] = []

  for (const block of [...graph.events, ...graph.functions]) {
    for (const fc of block.flows) {
      edges.push({
        sourceNodeID: fc.from_node,
        targetNodeID: fc.to_node,
        sourcePortID: `exec-out-${fc.from_pin}`,
        targetPortID: `exec-in-${fc.to_pin}`,
      })
    }
    for (const dl of block.links) {
      edges.push({
        sourceNodeID: dl.source_node,
        targetNodeID: dl.target_node,
        sourcePortID: `data-out-${dl.source_pin}`,
        targetPortID: `data-in-${dl.target_pin}`,
      })
    }
  }

  return { nodes, edges }
}
