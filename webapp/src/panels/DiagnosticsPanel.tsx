import { AlertTriangle, CheckCircle2, Crosshair, FileSearch, Play, RefreshCw } from 'lucide-react'
import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import { ScrollArea } from '@/components/ui/scroll-area'
import type { Diagnostic, DiagnosticAction } from '@/api/types'

interface DiagnosticsPanelProps {
  diagnostics: Diagnostic[]
  sourceDiagnostics: Diagnostic[]
  checkingSource: boolean
  onCheckSource: () => void
  onLocateDiagnostic: (diagnostic: Diagnostic) => void
  onApplyDiagnosticAction: (action: DiagnosticAction, diagnostic: Diagnostic) => void
}

function isDefaultRange(diag: Diagnostic): boolean {
  return diag.range.start.line === 1 &&
    diag.range.start.column === 1 &&
    diag.range.end.line === 1 &&
    diag.range.end.column === 1
}

function formatRange(diag: Diagnostic): string {
  const { start, end } = diag.range
  if (start.line === end.line && start.column === end.column) {
    return `${start.line}:${start.column}`
  }
  return `${start.line}:${start.column}-${end.line}:${end.column}`
}

function severityClasses(severity: Diagnostic['severity']): string {
  return severity === 'error'
    ? 'border-destructive/30 bg-destructive/10 text-destructive'
    : 'border-warning/30 bg-warning/10 text-warning'
}

function targetLabel(diagnostic: Diagnostic): string {
  const target = diagnostic.target
  if (!target) return diagnostic.context
  if (target.node_instance && target.pin_name) {
    const endpoint = `${target.node_instance}.${target.pin_name}`
    const block = target.block_kind && target.block_name
      ? `${target.block_kind} ${target.block_name}`
      : ''
    const connection = target.connection_kind || ''
    return [block, endpoint, connection].filter(Boolean).join(' / ')
  }
  if (target.node_instance) return target.node_instance
  if (target.parameter_name) return target.parameter_name
  if (target.reference) return target.reference
  if (target.block_kind && target.block_name) return `${target.block_kind} ${target.block_name}`
  if (target.graph) return target.graph
  if (!isDefaultRange(diagnostic)) return formatRange(diagnostic)
  return diagnostic.context
}

function DiagnosticItem({
  diagnostic,
  onLocateDiagnostic,
  onApplyDiagnosticAction,
}: {
  diagnostic: Diagnostic
  onLocateDiagnostic: (diagnostic: Diagnostic) => void
  onApplyDiagnosticAction: (action: DiagnosticAction, diagnostic: Diagnostic) => void
}) {
  const hasRange = !isDefaultRange(diagnostic)
  const label = targetLabel(diagnostic)
  const hasTarget = label.trim().length > 0
  const visibleActions = (diagnostic.actions ?? []).filter(action =>
    (action.command ?? '').trim().length > 0 || (action.replacement ?? '').length > 0)

  return (
    <div
      className="rounded border border-border/40 bg-secondary/20 px-2 py-1.5"
      data-diagnostic-id={diagnostic.id ?? diagnostic.code}
    >
      <div className="flex items-start gap-1.5">
        <AlertTriangle
          className={`mt-0.5 h-3.5 w-3.5 shrink-0 ${
            diagnostic.severity === 'error' ? 'text-destructive' : 'text-warning'
          }`}
        />
        <div className="min-w-0 flex-1">
          <div className="flex items-center gap-1.5">
            <Badge variant="outline" className={`h-4 px-1 py-0 text-[8px] ${severityClasses(diagnostic.severity)}`}>
              {diagnostic.severity}
            </Badge>
            {diagnostic.code && (
              <span className="truncate font-mono text-[9px] text-muted-foreground/70">
                {diagnostic.code}
              </span>
            )}
            {hasRange && (
              <span className="ml-auto shrink-0 font-mono text-[9px] text-primary/70">
                {formatRange(diagnostic)}
              </span>
            )}
          </div>
          <div className="mt-1 text-[11px] leading-snug text-foreground/80">
            {diagnostic.message}
          </div>
          {(hasTarget || diagnostic.hint) && (
            <div className="mt-1 flex items-center gap-1.5">
              {hasTarget && (
                <button
                  type="button"
                  data-diagnostic-locate={diagnostic.id ?? diagnostic.code}
                  className="flex min-w-0 items-center gap-1 rounded px-1 py-0.5 font-mono text-[9px]
                    text-primary/75 hover:bg-primary/10 hover:text-primary"
                  onClick={() => onLocateDiagnostic(diagnostic)}
                >
                  <Crosshair className="h-3 w-3 shrink-0" />
                  <span className="truncate">{label}</span>
                </button>
              )}
              {diagnostic.hint && (
                <span className="min-w-0 truncate text-[10px] text-muted-foreground/60">
                  {diagnostic.hint}
                </span>
              )}
            </div>
          )}
          {visibleActions.length > 0 && (
            <div className="mt-1 flex flex-wrap gap-1">
              {visibleActions.map((action, index) => (
                <Button
                  key={action.id ?? `${action.kind}-${action.command}-${index}`}
                  type="button"
                  data-diagnostic-action-id={action.id ?? `${diagnostic.id ?? diagnostic.code}:${action.kind}:${action.command ?? action.title}`}
                  variant="ghost"
                  size="sm"
                  className="h-5 max-w-full gap-1 px-1.5 text-[9px] text-primary/80 hover:bg-primary/10 hover:text-primary"
                  title={action.title}
                  onClick={() => onApplyDiagnosticAction(action, diagnostic)}
                >
                  <Play className="h-2.5 w-2.5 shrink-0" />
                  <span className="truncate">{action.title}</span>
                </Button>
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  )
}

function DiagnosticGroup({
  title,
  diagnostics,
  onLocateDiagnostic,
  onApplyDiagnosticAction,
}: {
  title: string
  diagnostics: Diagnostic[]
  onLocateDiagnostic: (diagnostic: Diagnostic) => void
  onApplyDiagnosticAction: (action: DiagnosticAction, diagnostic: Diagnostic) => void
}) {
  if (diagnostics.length === 0) return null

  return (
    <div className="space-y-1.5">
      <div className="flex items-center gap-1.5 text-[9px] font-bold uppercase tracking-[0.1em] text-muted-foreground/50">
        <span>{title}</span>
        <Badge variant="secondary" className="h-4 px-1 py-0 text-[9px] tabular-nums">
          {diagnostics.length}
        </Badge>
      </div>
      <div className="space-y-1">
        {diagnostics.map((diagnostic, index) => (
          <DiagnosticItem
            key={diagnostic.id ?? `${title}-${index}-${diagnostic.code}-${diagnostic.message}`}
            diagnostic={diagnostic}
            onLocateDiagnostic={onLocateDiagnostic}
            onApplyDiagnosticAction={onApplyDiagnosticAction}
          />
        ))}
      </div>
    </div>
  )
}

export default function DiagnosticsPanel({
  diagnostics,
  sourceDiagnostics,
  checkingSource,
  onCheckSource,
  onLocateDiagnostic,
  onApplyDiagnosticAction,
}: DiagnosticsPanelProps) {
  const total = diagnostics.length + sourceDiagnostics.length

  return (
    <div className="flex h-full flex-col">
      <div className="flex items-center gap-2 border-b px-3 py-1.5 shrink-0">
        <FileSearch className="h-3.5 w-3.5 text-primary/70" />
        <span className="text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">
          Diagnostics
        </span>
        <Badge variant="secondary" className="ml-auto h-4 px-1 py-0 text-[9px] tabular-nums">
          {total}
        </Badge>
        <Button
          type="button"
          variant="ghost"
          size="icon"
          className="h-6 w-6 text-muted-foreground hover:text-primary"
          onClick={onCheckSource}
          disabled={checkingSource}
          title="Refresh source diagnostics"
        >
          <RefreshCw className={`h-3 w-3 ${checkingSource ? 'animate-spin' : ''}`} />
          <span className="sr-only">Refresh source diagnostics</span>
        </Button>
      </div>

      <ScrollArea className="min-h-0 flex-1">
        <div className="space-y-3 p-2 panel-enter">
          {total === 0 && (
            <div className="flex flex-col items-center justify-center py-6 text-center">
              <CheckCircle2 className="mb-2 h-5 w-5 text-success/70" />
              <div className="text-[11px] text-muted-foreground/50">
                No diagnostics
              </div>
            </div>
          )}
          <DiagnosticGroup
            title="Session"
            diagnostics={diagnostics}
            onLocateDiagnostic={onLocateDiagnostic}
            onApplyDiagnosticAction={onApplyDiagnosticAction}
          />
          <DiagnosticGroup
            title="Source"
            diagnostics={sourceDiagnostics}
            onLocateDiagnostic={onLocateDiagnostic}
            onApplyDiagnosticAction={onApplyDiagnosticAction}
          />
        </div>
      </ScrollArea>
    </div>
  )
}
