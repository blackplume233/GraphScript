import { Handle, Position, type Node, type NodeProps } from '@xyflow/react'
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
  pins: PinDef[]
  intrinsicProperties: BlueprintIntrinsicProperty[]
  diagnostic?: DiagnosticHighlightData
  connectionPreview?: BlueprintConnectionPreview | null
}

export type BlueprintFlowNode = Node<BlueprintNodeData, 'blueprint'>

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
  const handleType = pin.direction === 'out' ? 'source' : 'target'
  const position = isLeft ? Position.Left : Position.Right
  const previewGlow = connectionState === 'compatible'
    ? `0 0 0 2px oklch(0.72 0.16 150 / 0.25), 0 0 14px ${color}`
    : connectionState === 'origin'
      ? `0 0 0 2px oklch(0.72 0.16 240 / 0.35), 0 0 14px ${color}`
      : undefined

  return (
    <div
      className="relative flex items-center gap-1.5 py-[3px] group/pin transition-opacity duration-100"
      style={{ flexDirection: isLeft ? 'row' : 'row-reverse' }}
      data-pin-connection-state={connectionState}
    >
      <Handle
        id={pinHandleId(pin)}
        type={handleType}
        position={position}
        className="nodrag"
        style={{
          position: 'relative',
          left: 'auto',
          right: 'auto',
          top: 'auto',
          transform: 'none',
          width: 12,
          height: 12,
          minWidth: 12,
          borderRadius: pin.kind === 'exec' ? 2 : 999,
          border: `2px solid ${color}`,
          background: pin.direction === 'out' ? color : 'var(--color-card)',
          boxShadow: previewGlow ?? diagnosticGlow(highlight),
          opacity: connectionState === 'disabled' ? 0.28 : 1,
          outline: connectionState === 'compatible'
            ? '1px solid oklch(0.72 0.16 150 / 0.95)'
            : highlight ? `1px solid ${diagnosticBorderColor(highlight)}` : undefined,
          outlineOffset: 2,
        }}
        title={`${pin.direction} ${pin.name}: ${pin.type}`}
        data-port-id={pinHandleId(pin)}
        data-port-type={handleType}
        data-testid="sdk.workflow.canvas.node.port"
      />
      <span
        className="text-[11px] leading-none whitespace-nowrap transition-colors duration-100 group-hover/pin:text-foreground"
        style={{
          color: connectionState === 'compatible'
            ? 'oklch(0.78 0.14 150)'
            : highlight ? diagnosticBorderColor(highlight) : 'oklch(0.58 0.01 250)',
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

function PropertyPreviewRow({ property }: { property: BlueprintIntrinsicProperty }) {
  return (
    <div
      className="flex min-w-0 items-center gap-1.5 rounded-[4px] border border-border/25 bg-background/35 px-1.5 py-1"
      data-node-intrinsic-property={property.name}
    >
      <span className="min-w-0 flex-1 truncate font-mono text-[10px] text-muted-foreground/70">
        {property.name}
      </span>
      <span className="max-w-[92px] truncate font-mono text-[10px] text-foreground/80">
        {property.value || property.defaultValue || 'default'}
      </span>
      {property.overridden && (
        <span
          className="h-1.5 w-1.5 shrink-0 rounded-full bg-primary"
          title="Overridden in node initializer"
        />
      )}
    </div>
  )
}

export default function BlueprintNode({ data, selected }: NodeProps<BlueprintFlowNode>) {
  const pins = data.pins ?? []
  const label = data.instanceName || data.label
  const typeName = data.typeName ?? ''
  const intrinsicProperties = data.intrinsicProperties ?? []
  const execIn = pins.filter(pin => pin.kind === 'exec' && pin.direction === 'in')
  const execOut = pins.filter(pin => pin.kind === 'exec' && pin.direction === 'out')
  const dataIn = pins.filter(pin => pin.kind === 'data' && pin.direction === 'in')
  const dataOut = pins.filter(pin => pin.kind === 'data' && pin.direction === 'out')
  const hasExec = execIn.length > 0 || execOut.length > 0
  const headerStyle = hasExec ? NODE_HEADER_COLORS.exec : NODE_HEADER_COLORS.pure
  const diagnostic = data.diagnostic
  const connectionPreview = data.connectionPreview ?? null
  const nodeBorder = diagnostic ? diagnosticBorderColor(diagnostic) : selected ? 'var(--color-primary)' : 'var(--color-border)'
  const nodeShadow = diagnostic
    ? `0 0 18px ${diagnostic.severity === 'error' ? 'oklch(0.58 0.22 25 / 0.42)' : 'oklch(0.72 0.15 80 / 0.36)'}, inset 0 1px 0 oklch(1 0 0 / 0.06)`
    : selected
      ? '0 0 0 1px var(--color-primary), 0 8px 24px oklch(0 0 0 / 0.45), inset 0 1px 0 oklch(1 0 0 / 0.04)'
      : '0 2px 12px oklch(0 0 0 / 0.4), inset 0 1px 0 oklch(1 0 0 / 0.04)'

  return (
    <div
      className="node-card min-w-[190px] rounded-lg overflow-hidden border border-border/60 select-none"
      data-blueprint-node={label}
      onPointerDownCapture={(event) => {
        window.dispatchEvent(new CustomEvent('graphscript:node-pointer-down', {
          detail: {
            nodeId: label,
            additive: event.shiftKey || event.ctrlKey || event.metaKey,
          },
        }))
      }}
      style={{
        background: 'var(--color-card)',
        borderColor: nodeBorder,
        boxShadow: nodeShadow,
      }}
    >
      <div
        className="px-3 py-[7px] flex items-center gap-2"
        style={{
          background: `linear-gradient(135deg, ${headerStyle.from}, ${headerStyle.to})`,
          borderBottom: '1px solid oklch(0.3 0.02 250)',
        }}
      >
        {hasExec && (
          <div
            className="w-2 h-2 rounded-full shrink-0"
            style={{ background: 'var(--color-exec)', boxShadow: '0 0 6px var(--color-exec)' }}
          />
        )}
        <span className="text-[12px] font-semibold truncate text-foreground/90">
          {label}
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

      {intrinsicProperties.length > 0 && (
        <div className="space-y-1 border-b border-border/35 px-2.5 py-2">
          {intrinsicProperties.slice(0, 4).map(property => (
            <PropertyPreviewRow key={property.name} property={property} />
          ))}
          {intrinsicProperties.length > 4 && (
            <div className="px-1.5 text-[9px] text-muted-foreground/45">
              +{intrinsicProperties.length - 4} properties
            </div>
          )}
        </div>
      )}

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
