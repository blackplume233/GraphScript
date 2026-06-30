export type PinKind = 'exec' | 'data'

export const PIN_COLORS: Record<string, string> = {
  exec:             'var(--color-exec)',
  float:            'var(--color-data-float)',
  int:              'var(--color-data-int)',
  bool:             'var(--color-data-bool)',
  string:           'var(--color-data-string)',
  FVector:          '#dba944',
  FRotator:         '#9977cc',
  FTransform:       '#cc9944',
  FName:            '#aa88cc',
  FString:          'var(--color-data-string)',
  FText:            'var(--color-data-string)',
  UClass:           '#6688cc',
  AActor:           '#448866',
  UUserWidget:      '#886644',
  APlayerController:'#448866',
  default:          '#667788',
}

export function pinColor(type: string, kind: PinKind): string {
  if (kind === 'exec') return PIN_COLORS.exec
  return PIN_COLORS[type] ?? PIN_COLORS.default
}

export const NODE_HEADER_COLORS: Record<string, { from: string; to: string }> = {
  exec: { from: 'var(--color-node-exec-start)', to: 'var(--color-node-exec-end)' },
  pure: { from: 'var(--color-node-pure-start)', to: 'var(--color-node-pure-end)' },
  flow: { from: 'oklch(0.28 0.08 250)', to: 'oklch(0.17 0.05 250)' },
  math: { from: 'oklch(0.28 0.09 145)', to: 'oklch(0.17 0.05 145)' },
  logic: { from: 'oklch(0.29 0.08 40)', to: 'oklch(0.18 0.05 40)' },
  io: { from: 'oklch(0.27 0.08 210)', to: 'oklch(0.16 0.05 210)' },
  field: { from: 'oklch(0.25 0.08 320)', to: 'oklch(0.16 0.05 320)' },
  native: { from: 'oklch(0.26 0.05 235)', to: 'oklch(0.16 0.03 235)' },
  graph: { from: 'oklch(0.26 0.07 185)', to: 'oklch(0.16 0.04 185)' },
  event: { from: 'oklch(0.25 0.08 15)', to: 'oklch(0.18 0.06 15)' },
  function: { from: 'oklch(0.22 0.06 270)', to: 'oklch(0.16 0.04 270)' },
}
