import { useNodeRender } from '@flowgram.ai/free-layout-editor'
import { pinColor, NODE_HEADER_COLORS } from './port-config'
import type { PinDef } from '@/api/types'

function ExecPinIcon({ color }: { color: string }) {
  return (
    <svg viewBox="0 0 14 14" width={14} height={14} className="shrink-0">
      <path
        d="M3 1.5 L11 7 L3 12.5 Z"
        fill="transparent"
        stroke={color}
        strokeWidth={1.8}
        strokeLinejoin="round"
      />
    </svg>
  )
}

function DataPinIcon({ color, connected }: { color: string; connected?: boolean }) {
  return (
    <svg viewBox="0 0 14 14" width={14} height={14} className="shrink-0">
      <circle
        cx={7} cy={7} r={4.5}
        fill={connected ? color : 'transparent'}
        stroke={color}
        strokeWidth={1.8}
      />
    </svg>
  )
}

function PinRow({ pin, side }: { pin: PinDef; side: 'left' | 'right' }) {
  const color = pinColor(pin.type, pin.kind)
  const portId = pin.kind === 'exec'
    ? `exec-${pin.direction}-${pin.name}`
    : `data-${pin.direction}-${pin.name}`
  const portType = pin.direction === 'in' ? 'input' : 'output'
  const isExec = pin.kind === 'exec'
  const isLeft = side === 'left'

  return (
    <div
      className="flex items-center gap-1.5 py-[3px] group/pin"
      style={{ flexDirection: isLeft ? 'row' : 'row-reverse' }}
    >
      <div
        data-port-id={portId}
        data-port-type={portType}
        className="cursor-pointer transition-transform duration-100 group-hover/pin:scale-125"
      >
        {isExec
          ? <ExecPinIcon color={color} />
          : <DataPinIcon color={color} />
        }
      </div>
      <span
        className="text-[11px] leading-none whitespace-nowrap transition-colors duration-100 group-hover/pin:text-foreground"
        style={{ color: 'oklch(0.58 0.01 250)' }}
      >
        {pin.name}
      </span>
      {!isExec && pin.type && (
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

export default function BlueprintNode() {
  const nodeRender = useNodeRender()
  const data = (nodeRender.data ?? {}) as {
    label?: string
    typeName?: string
    instanceName?: string
    pins?: PinDef[]
  }

  const label = data.instanceName || data.label || nodeRender.id || '?'
  const typeName = data.typeName ?? nodeRender.type ?? ''
  const pins = data.pins ?? []

  const execIn = pins.filter(p => p.kind === 'exec' && p.direction === 'in')
  const execOut = pins.filter(p => p.kind === 'exec' && p.direction === 'out')
  const dataIn = pins.filter(p => p.kind === 'data' && p.direction === 'in')
  const dataOut = pins.filter(p => p.kind === 'data' && p.direction === 'out')

  const hasExec = execIn.length > 0 || execOut.length > 0
  const headerStyle = hasExec ? NODE_HEADER_COLORS.exec : NODE_HEADER_COLORS.pure

  return (
    <div className="node-card min-w-[180px] rounded-lg overflow-hidden border border-border/60 select-none"
      style={{
        background: 'var(--color-card)',
        boxShadow: '0 2px 12px oklch(0 0 0 / 0.4), inset 0 1px 0 oklch(1 0 0 / 0.04)',
      }}
    >
      {/* Title bar */}
      <div
        className="px-3 py-[7px] flex items-center gap-2"
        style={{
          background: `linear-gradient(135deg, ${headerStyle.from}, ${headerStyle.to})`,
          borderBottom: '1px solid oklch(0.3 0.02 250)',
        }}
      >
        {hasExec && (
          <div className="w-2 h-2 rounded-full shrink-0"
            style={{ background: 'var(--color-exec)', boxShadow: '0 0 6px var(--color-exec)' }}
          />
        )}
        <span className="text-[12px] font-semibold truncate text-foreground/90">
          {label}
        </span>
        {typeName && typeName !== label && (
          <span className="text-[10px] opacity-35 truncate ml-auto font-mono">
            {typeName}
          </span>
        )}
      </div>

      {/* Pin rows */}
      <div className="flex gap-3 px-2.5 py-2 min-h-[28px]">
        {/* Left column */}
        <div className="flex flex-col gap-0 min-w-0">
          {execIn.map(p => <PinRow key={`ei-${p.name}`} pin={p} side="left" />)}
          {dataIn.map(p => <PinRow key={`di-${p.name}`} pin={p} side="left" />)}
          {execIn.length === 0 && dataIn.length === 0 && (
            <div className="h-4" />
          )}
        </div>

        <div className="flex-1 min-w-[12px]" />

        {/* Right column */}
        <div className="flex flex-col gap-0 min-w-0 items-end">
          {execOut.map(p => <PinRow key={`eo-${p.name}`} pin={p} side="right" />)}
          {dataOut.map(p => <PinRow key={`do-${p.name}`} pin={p} side="right" />)}
          {execOut.length === 0 && dataOut.length === 0 && (
            <div className="h-4" />
          )}
        </div>
      </div>
    </div>
  )
}
