import type { WorkflowNodeRegistry } from '@flowgram.ai/free-layout-editor'
import type { NodeTypeDef, PinDef } from '@/api/types'

/**
 * Build FlowGram port definitions from GraphScript pin definitions.
 * Uses dynamic ports via data-port-id attributes in BlueprintNode,
 * but we still declare defaultPorts for connection logic.
 */
function buildPorts(pins: PinDef[]) {
  const ports: Array<{
    portID: string
    type: 'input' | 'output'
    location: 'left' | 'right'
  }> = []

  for (const pin of pins) {
    const dir = pin.direction === 'in' ? 'input' : 'output'
    ports.push({
      portID: pin.kind === 'exec'
        ? `exec-${pin.direction}-${pin.name}`
        : `data-${pin.direction}-${pin.name}`,
      type: dir,
      location: pin.direction === 'in' ? 'left' : 'right',
    })
  }

  return ports
}

export function buildNodeRegistries(types: NodeTypeDef[]): WorkflowNodeRegistry[] {
  return types.map(t => ({
    type: t.type_name,
    meta: {
      useDynamicPort: true,
      defaultPorts: buildPorts(t.pins),
    },
  }))
}
