export type PinKind = 'exec' | 'data'

export const PIN_COLORS: Record<string, string> = {
  exec:             'var(--color-exec)',
  float:            'var(--color-data-float)',
  int:              'var(--color-data-int)',
  bool:             'var(--color-data-bool)',
  string:           'var(--color-data-string)',
  FVector:          '#d6a64a',
  FRotator:         '#8d77bd',
  FTransform:       '#c58f42',
  FName:            '#a883c4',
  FString:          'var(--color-data-string)',
  FText:            'var(--color-data-string)',
  UClass:           '#7391c7',
  AActor:           '#5f9a73',
  UUserWidget:      '#997a51',
  APlayerController:'#5f9a73',
  default:          '#8a929b',
}

export function pinColor(type: string, kind: PinKind): string {
  if (kind === 'exec') return PIN_COLORS.exec
  return PIN_COLORS[type] ?? PIN_COLORS.default
}

export const NODE_HEADER_COLORS: Record<string, { from: string; to: string }> = {
  exec: { from: 'var(--color-node-exec-start)', to: 'var(--color-node-exec-end)' },
  pure: { from: 'var(--color-node-pure-start)', to: 'var(--color-node-pure-end)' },
  flow: { from: 'oklch(0.34 0.07 185)', to: 'oklch(0.19 0.045 185)' },
  math: { from: 'oklch(0.31 0.075 145)', to: 'oklch(0.18 0.04 145)' },
  logic: { from: 'oklch(0.36 0.075 65)', to: 'oklch(0.20 0.045 65)' },
  io: { from: 'oklch(0.31 0.06 205)', to: 'oklch(0.18 0.035 205)' },
  field: { from: 'oklch(0.28 0.055 310)', to: 'oklch(0.17 0.035 310)' },
  native: { from: 'oklch(0.28 0.025 250)', to: 'oklch(0.17 0.018 250)' },
  graph: { from: 'oklch(0.32 0.06 180)', to: 'oklch(0.18 0.035 180)' },
  event: { from: 'oklch(0.33 0.09 25)', to: 'oklch(0.18 0.055 25)' },
  function: { from: 'oklch(0.27 0.035 265)', to: 'oklch(0.17 0.025 265)' },
}
