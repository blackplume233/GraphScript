import { useEffect } from 'react'
import { Handle, Position, useUpdateNodeInternals, type Node, type NodeProps } from '@xyflow/react'
import { pinColor, NODE_HEADER_COLORS } from './port-config'
import type { PinDef } from '@/api/types'
import {
  portKey,
  type DiagnosticHighlightData,
  type DiagnosticPinHighlight,
} from './diagnostic-highlights'

export interface BlueprintIntrinsicProperty {
  name: string
  type: string
  value: string
  defaultValue: string
  overridden: boolean
  declared: boolean
  sourceFile?: string
}

export interface BlueprintConnectionPreview {
  nodeId: string
  port: {
    kind: 'exec' | 'data'
    direction: 'in' | 'out'
    pin: string
  }
  type: string
}

export interface BlueprintNodeData extends Record<string, unknown> {
  label: string
  typeName: string
  instanceName: string
  init: string
  category: string
  sourceGraph: string
  isNative: boolean
  isSynthetic?: boolean
  pins: PinDef[]
  intrinsicProperties: BlueprintIntrinsicProperty[]
  diagnostic?: DiagnosticHighlightData
  connectionPreview?: BlueprintConnectionPreview | null
}

export type BlueprintFlowNode = Node<BlueprintNodeData, 'blueprint'>

const EMPTY_PINS: PinDef[] = []

function diagnosticBorderColor(highlight?: DiagnosticPinHighlight | DiagnosticHighlightData): string {
  if (!highlight) return 'transparent'
  return highlight.severity === 'error'
    ? 'var(--color-destructive)'
    : 'var(--color-warning)'
}

function diagnosticGlow(highlight?: DiagnosticPinHighlight | DiagnosticHighlightData): string | undefined {
  if (!highlight) return undefined
  const color = highlight.severity === 'error'
    ? 'oklch(0.58 0.22 25 / 0.45)'
    : 'oklch(0.72 0.15 80 / 0.45)'
  const focus = highlight.focused ? `, 0 0 0 2px ${color}` : ''
  return `0 0 10px ${color}${focus}`
}

function pinHandleId(pin: PinDef): string {
  return `${pin.kind}-${pin.direction}-${pin.name}`
}

function dataTypesCompatible(left: string, right: string): boolean {
  if (!left || !right) return true
  return left === right
}

function connectionStateForPin(
  pin: PinDef,
  nodeId: string,
  preview: BlueprintConnectionPreview | null | undefined,
): 'idle' | 'origin' | 'compatible' | 'disabled' {
  if (!preview) return 'idle'
  if (
    preview.nodeId === nodeId &&
    preview.port.kind === pin.kind &&
    preview.port.direction === pin.direction &&
    preview.port.pin === pin.name
  ) {
    return 'origin'
  }
  if (pin.kind !== preview.port.kind || pin.direction === preview.port.direction) return 'disabled'
  if (pin.kind === 'data' && !dataTypesCompatible(pin.type, preview.type)) return 'disabled'
  return 'compatible'
}

function PinRow({
  pin,
  side,
  highlight,
  connectionState = 'idle',
}: {
  pin: PinDef
  side: 'left' | 'right'
  highlight?: DiagnosticPinHighlight
  connectionState?: 'idle' | 'origin' | 'compatible' | 'disabled'
}) {
  const color = pinColor(pin.type, pin.kind)
  const isLeft = side === 'left'
  const isExec = pin.kind === 'exec'
  const handleType = pin.direction === 'out' ? 'source' : 'target'
  const position = isLeft ? Position.Left : Position.Right
  const handleSize = isExec ? 18 : 16
  const glyphSize = isExec ? 14 : 10
  const previewGlow = connectionState === 'compatible'
    ? `0 0 0 2px oklch(0.72 0.16 150 / 0.22), 0 0 12px ${color}`
    : connectionState === 'origin'
      ? `0 0 0 2px oklch(0.78 0.14 75 / 0.35), 0 0 12px ${color}`
      : undefined

  return (
    <div
      className="relative flex min-h-[19px] items-center gap-1.5 py-[3px] group/pin transition-opacity duration-100"
      style={{ flexDirection: isLeft ? 'row' : 'row-reverse' }}
      data-pin-connection-state={connectionState}
      data-pin-row={pinHandleId(pin)}
    >
      <Handle
        id={pinHandleId(pin)}
        type={handleType}
        position={position}
        className={`nodrag graphscript-pin-handle graphscript-pin-handle-${pin.kind} graphscript-pin-handle-${pin.direction}`}
        style={{
          position: 'absolute',
          left: isLeft ? -18 : 'auto',
          right: isLeft ? 'auto' : -18,
          top: '50%',
          transform: 'translateY(-50%)',
          width: handleSize,
          height: handleSize,
          minWidth: handleSize,
          border: 'none',
          background: 'transparent',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          overflow: 'visible',
          boxShadow: 'none',
          opacity: connectionState === 'disabled' ? 0.28 : 1,
        }}
        title={`${pin.direction} ${pin.name}: ${pin.type}`}
        data-port-id={pinHandleId(pin)}
        data-port-type={handleType}
        data-testid="sdk.workflow.canvas.node.port"
      />
      <span
        aria-hidden="true"
        data-pin-glyph="true"
        className="graphscript-pin-glyph"
        style={{
          position: 'absolute',
          left: isLeft ? -16 : 'auto',
          right: isLeft ? 'auto' : -16,
          top: '50%',
          width: glyphSize,
          height: isExec ? 13 : glyphSize,
          borderRadius: isExec ? 0 : 1,
          border: `2px solid ${color}`,
          background: isExec || pin.direction === 'out' ? color : 'var(--color-flow-node-bg)',
          boxShadow: previewGlow ?? diagnosticGlow(highlight),
          clipPath: isExec ? 'polygon(16% 8%, 88% 50%, 16% 92%)' : undefined,
          transform: `translateY(-50%) ${isExec ? '' : 'rotate(45deg)'}`,
          outline: connectionState === 'compatible'
            ? '1px solid oklch(0.72 0.16 150 / 0.95)'
            : highlight ? `1px solid ${diagnosticBorderColor(highlight)}` : undefined,
          outlineOffset: 2,
        }}
      />
      <span
        className="text-[11px] leading-none whitespace-nowrap transition-colors duration-100 group-hover/pin:text-foreground"
        style={{
          color: connectionState === 'compatible'
            ? 'oklch(0.78 0.14 150)'
            : highlight ? diagnosticBorderColor(highlight) : isExec ? 'oklch(0.78 0.02 85)' : 'oklch(0.62 0.008 250)',
          fontWeight: highlight || connectionState === 'compatible' || connectionState === 'origin' ? 600 : 400,
          opacity: connectionState === 'disabled' ? 0.42 : 1,
        }}
      >
        {pin.name}
      </span>
      {pin.kind === 'data' && pin.type && (
        <span
          className="text-[9px] leading-none opacity-40 whitespace-nowrap"
          style={{ fontFamily: 'var(--font-mono, monospace)' }}
        >
          {pin.type}
        </span>
      )}
    </div>
  )
}

function headerStyleForNode(category: string, hasExec: boolean, isNative: boolean) {
  const normalized = category.trim().toLowerCase()
  if (normalized.includes('event')) return NODE_HEADER_COLORS.event
  if (normalized.includes('function')) return NODE_HEADER_COLORS.function
  if (normalized.includes('math')) return NODE_HEADER_COLORS.math
  if (normalized.includes('logic') || normalized.includes('branch') || normalized.includes('flow')) return NODE_HEADER_COLORS.logic
  if (normalized.includes('io') || normalized.includes('print') || normalized.includes('debug')) return NODE_HEADER_COLORS.io
  if (normalized.includes('field') || normalized.includes('property')) return NODE_HEADER_COLORS.field
  if (!isNative) return NODE_HEADER_COLORS.graph
  return hasExec ? NODE_HEADER_COLORS.flow : NODE_HEADER_COLORS.pure
}

export default function BlueprintNode({ id, data, selected }: NodeProps<BlueprintFlowNode>) {
  const updateNodeInternals = useUpdateNodeInternals()
  const pins = data.pins ?? EMPTY_PINS
  const label = data.instanceName || data.label
  const typeName = data.typeName ?? ''
  const category = data.category || (data.isNative ? 'Native' : 'Graph')
  const execIn = pins.filter(pin => pin.kind === 'exec' && pin.direction === 'in')
  const execOut = pins.filter(pin => pin.kind === 'exec' && pin.direction === 'out')
  const dataIn = pins.filter(pin => pin.kind === 'data' && pin.direction === 'in')
  const dataOut = pins.filter(pin => pin.kind === 'data' && pin.direction === 'out')
  const hasExec = execIn.length > 0 || execOut.length > 0
  const headerStyle = headerStyleForNode(category, hasExec, data.isNative)
  const diagnostic = data.diagnostic
  const connectionPreview = data.connectionPreview ?? null
  const nodeBorder = diagnostic ? diagnosticBorderColor(diagnostic) : selected ? 'var(--color-flow-selected)' : 'var(--color-flow-node-border)'
  const nodeShadow = diagnostic
    ? `0 0 18px ${diagnostic.severity === 'error' ? 'oklch(0.58 0.22 25 / 0.42)' : 'oklch(0.72 0.15 80 / 0.36)'}, inset 0 1px 0 oklch(1 0 0 / 0.06)`
    : selected
      ? '0 0 0 1px var(--color-flow-selected), 0 0 0 2px oklch(0 0 0 / 0.65), 0 8px 20px oklch(0 0 0 / 0.5), inset 0 1px 0 oklch(1 0 0 / 0.04)'
      : '0 2px 12px oklch(0 0 0 / 0.44), inset 0 1px 0 oklch(1 0 0 / 0.04)'
  const pinSignature = pins.map(pin => `${pinHandleId(pin)}:${pin.type}`).join('|')

  useEffect(() => {
    updateNodeInternals(id)
  }, [id, pinSignature, updateNodeInternals])

  return (
    <div
      className="node-card relative min-w-[190px] rounded-[4px] overflow-visible border select-none"
      data-blueprint-node={label}
      onPointerDownCapture={(event) => {
        if (data.isSynthetic) return
        window.dispatchEvent(new CustomEvent('graphscript:node-pointer-down', {
          detail: {
            nodeId: label,
            additive: event.shiftKey || event.ctrlKey || event.metaKey,
          },
        }))
      }}
      style={{
        background: 'var(--color-flow-node-bg)',
        borderColor: nodeBorder,
        boxShadow: nodeShadow,
      }}
    >
      <div
        className="rounded-t-[4px] px-3 py-[7px] flex items-center gap-2"
        style={{
          background: `linear-gradient(135deg, ${headerStyle.from}, ${headerStyle.to})`,
          borderBottom: '1px solid var(--color-flow-node-border)',
        }}
      >
        {hasExec && (
          <div
            className="h-2.5 w-2.5 shrink-0"
            style={{
              background: 'var(--color-exec)',
              clipPath: 'polygon(16% 8%, 88% 50%, 16% 92%)',
            }}
          />
        )}
        <span className="text-[12px] font-semibold truncate text-foreground/90">
          {label}
        </span>
        <span
          className="shrink-0 rounded-[3px] border border-white/10 bg-black/20 px-1 text-[8px] font-semibold uppercase tracking-[0.08em] text-white/55"
          data-node-category={category}
          title={data.sourceGraph ? `Graph source: ${data.sourceGraph}` : data.isNative ? 'Native declaration' : 'Graph declaration'}
        >
          {category}
        </span>
        {diagnostic && (
          <span
            className="ml-auto h-2 w-2 rounded-full shrink-0"
            style={{
              background: diagnosticBorderColor(diagnostic),
              boxShadow: diagnosticGlow(diagnostic),
            }}
          />
        )}
        {typeName && typeName !== label && (
          <span className={`text-[10px] opacity-35 truncate font-mono ${diagnostic ? '' : 'ml-auto'}`}>
            {typeName}
          </span>
        )}
      </div>

      <div className="flex gap-3 px-2.5 py-2 min-h-[28px]">
        <div className="flex flex-col gap-0 min-w-0">
          {execIn.map(pin => (
            <PinRow
              key={`ei-${pin.name}`}
              pin={pin}
              side="left"
              highlight={diagnostic?.pins[portKey('exec', 'in', pin.name)]}
              connectionState={connectionStateForPin(pin, label, connectionPreview)}
            />
          ))}
          {dataIn.map(pin => (
            <PinRow
              key={`di-${pin.name}`}
              pin={pin}
              side="left"
              highlight={diagnostic?.pins[portKey('data', 'in', pin.name)]}
              connectionState={connectionStateForPin(pin, label, connectionPreview)}
            />
          ))}
          {execIn.length === 0 && dataIn.length === 0 && <div className="h-4" />}
        </div>

        <div className="flex-1 min-w-[12px]" />

        <div className="flex flex-col gap-0 min-w-0 items-end">
          {execOut.map(pin => (
            <PinRow
              key={`eo-${pin.name}`}
              pin={pin}
              side="right"
              highlight={diagnostic?.pins[portKey('exec', 'out', pin.name)]}
              connectionState={connectionStateForPin(pin, label, connectionPreview)}
            />
          ))}
          {dataOut.map(pin => (
            <PinRow
              key={`do-${pin.name}`}
              pin={pin}
              side="right"
              highlight={diagnostic?.pins[portKey('data', 'out', pin.name)]}
              connectionState={connectionStateForPin(pin, label, connectionPreview)}
            />
          ))}
          {execOut.length === 0 && dataOut.length === 0 && <div className="h-4" />}
        </div>
      </div>
    </div>
  )
}
