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
  event: { from: 'oklch(0.25 0.08 15)', to: 'oklch(0.18 0.06 15)' },
  function: { from: 'oklch(0.22 0.06 270)', to: 'oklch(0.16 0.04 270)' },
}
