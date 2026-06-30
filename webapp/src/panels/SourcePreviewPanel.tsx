/* eslint-disable react-hooks/set-state-in-effect */
import { useEffect, useMemo, useRef, useState } from 'react'
import { AlertTriangle, ChevronDown, ChevronRight, FileText, Pencil, Play, RefreshCw, RotateCcw, Save } from 'lucide-react'
import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import { ScrollArea } from '@/components/ui/scroll-area'
import type { SourceDiagnosticsEnvironment, SourceRange } from '@/api/types'

export type SourceSyncState =
  | 'empty'
  | 'session'
  | 'checking'
  | 'edited'
  | 'synced_patch'
  | 'synced_snapshot'
  | 'declaration'
  | 'stale'
  | 'error'

export type DeclarationRenameContext =
  | {
      kind: 'type'
      path: string
      contentHash: string
      oldName: string
    }
  | {
      kind: 'node'
      path: string
      contentHash: string
      oldName: string
    }
  | {
      kind: 'node_pin'
      path: string
      contentHash: string
      ownerName: string
      oldName: string
    }
  | {
      kind: 'schema'
      path: string
      contentHash: string
      oldName: string
    }
  | {
      kind: 'schema_field'
      path: string
      contentHash: string
      ownerName: string
      oldName: string
    }

interface SourcePreviewPanelProps {
  source: string
  sourceLabel: string
  sourceEditable: boolean
  focusedRange: SourceRange | null
  checkingSource: boolean
  syncState: SourceSyncState
  syncDetail: string
  applyingSource: boolean
  canApplySource: boolean
  pendingPatchRange: SourceRange | null
  pendingPatchSummary: string
  environmentNotice: string
  resolverEnvironment: SourceDiagnosticsEnvironment | null
  sessionImports: string[]
  declarationRenameContext: DeclarationRenameContext | null
  onCheckSource: () => void
  onSourceChange: (source: string) => void
  onApplySource: () => void
  onRevertSource: () => void
  onImportCommand: (command: string) => void
  onImportPlan: (commands: string[]) => void
  onOpenDeclarationSource: (path: string, contentHash: string) => void
  onRenameDeclaration: (newName: string) => void
}

function isRangeOnLine(range: SourceRange | null, line: number): boolean {
  if (!range) return false
  return line >= range.start.line && line <= range.end.line
}

function clampColumn(column: number, lineLength: number): number {
  return Math.max(1, Math.min(column, lineLength + 1))
}

function offsetForLocation(source: string, location: SourceRange['start']): number | null {
  if (location.line < 1 || location.column < 1) return null

  let line = 1
  let column = 1
  for (let index = 0; index < source.length; index += 1) {
    if (line === location.line && column === location.column) return index

    const char = source[index]
    if (char === '\r') {
      if (source[index + 1] === '\n') index += 1
      line += 1
      column = 1
      continue
    }
    if (char === '\n') {
      line += 1
      column = 1
      continue
    }
    column += 1
  }

  if (line === location.line && column === location.column) return source.length
  return null
}

function renderLineText(text: string, lineNumber: number, focusedRange: SourceRange | null) {
  if (!focusedRange || !isRangeOnLine(focusedRange, lineNumber)) {
    return text || ' '
  }

  const lineLength = text.length
  const startColumn = lineNumber === focusedRange.start.line
    ? clampColumn(focusedRange.start.column, lineLength)
    : 1
  const endColumn = lineNumber === focusedRange.end.line
    ? clampColumn(focusedRange.end.column, lineLength)
    : lineLength + 1

  const startIndex = Math.max(0, startColumn - 1)
  const endIndex = Math.max(startIndex + 1, endColumn - 1)
  const before = text.slice(0, startIndex)
  const active = text.slice(startIndex, endIndex) || ' '
  const after = text.slice(endIndex)

  return (
    <>
      {before}
      <span
        data-source-range="active"
        className="rounded-sm bg-warning/20 text-warning"
      >
        {active}
      </span>
      {after}
    </>
  )
}

function syncLabel(state: SourceSyncState): string {
  switch (state) {
    case 'session': return 'Session source'
    case 'checking': return 'Checking'
    case 'edited': return 'Edited buffer'
    case 'synced_patch': return 'Synced patch'
    case 'synced_snapshot': return 'Synced snapshot'
    case 'declaration': return 'Declaration source'
    case 'stale': return 'Stale preview'
    case 'error': return 'Sync issue'
    default: return 'No source'
  }
}

function syncClasses(state: SourceSyncState): string {
  switch (state) {
    case 'session':
    case 'synced_patch':
    case 'synced_snapshot':
    case 'declaration':
      return 'border-success/30 bg-success/10 text-success'
    case 'checking':
    case 'edited':
      return 'border-warning/30 bg-warning/10 text-warning'
    case 'stale':
    case 'error':
      return 'border-destructive/30 bg-destructive/10 text-destructive'
    default:
      return 'border-border/50 bg-secondary/40 text-muted-foreground'
  }
}

function importStatusClasses(status: string): string {
  switch (status) {
    case 'loaded':
      return 'border-success/30 bg-success/10 text-success'
    case 'unsupported':
    case 'missing':
    case 'blocked':
    case 'too_large':
    case 'too_deep':
    case 'cycle':
    case 'dependency_error':
    case 'parse_error':
    case 'compile_error':
      return 'border-destructive/30 bg-destructive/10 text-destructive'
    default:
      return 'border-warning/30 bg-warning/10 text-warning'
  }
}

function commandImportPath(command: string): string {
  const trimmed = command.trim()
  if (!trimmed.startsWith('import')) return ''
  const path = trimmed.slice('import'.length).trim()
  if (path.length >= 2 && path.startsWith('"') && path.endsWith('"')) {
    return path.slice(1, -1)
  }
  return path
}

type SourceDeclaration = SourceDiagnosticsEnvironment['declarations'][number]

interface ImportTreeNode {
  key: string
  pathKey: string
  declaration: SourceDeclaration
  children: ImportTreeNode[]
}

function declarationPathKey(declaration: SourceDeclaration): string {
  return declaration.normalized_path || declaration.path
}

function declarationNodeKey(declaration: SourceDeclaration, index: number): string {
  return `${declarationPathKey(declaration)}::${index}`
}

function declarationParentPathKey(declaration: SourceDeclaration): string {
  return declaration.parent_normalized_path || declaration.parent_path || ''
}

function buildImportTree(declarations: SourceDeclaration[]): ImportTreeNode[] {
  const nodes = declarations.map((declaration, index) => ({
    key: declarationNodeKey(declaration, index),
    pathKey: declarationPathKey(declaration),
    declaration,
    children: [],
  }))
  const firstNodeByPath = new Map<string, ImportTreeNode>()
  for (const node of nodes) {
    if (node.pathKey && !firstNodeByPath.has(node.pathKey)) {
      firstNodeByPath.set(node.pathKey, node)
    }
  }

  const roots: ImportTreeNode[] = []
  for (const node of nodes) {
    const parentKey = declarationParentPathKey(node.declaration)
    const parent = parentKey ? firstNodeByPath.get(parentKey) : null
    if (parent && parent !== node) {
      parent.children.push(node)
    } else {
      roots.push(node)
    }
  }
  return roots
}

function declarationLabel(declaration: SourceDeclaration): string {
  const chain = declaration.import_chain || declaration.path
  return `${chain} ${declaration.status}`
}

function declarationIssueMessage(declaration: SourceDeclaration): string {
  if (declaration.status === 'loaded' || declaration.status === 'skipped') return ''
  return declaration.message || declaration.status
}

function declarationLoadedInSession(declaration: SourceDeclaration, sessionImportSet: Set<string>): boolean {
  const commandPath = commandImportPath(declaration.command)
  return sessionImportSet.has(declaration.path) ||
    sessionImportSet.has(declaration.normalized_path) ||
    (commandPath.length > 0 && sessionImportSet.has(commandPath))
}

function buildImportReplayPlan(
  declarations: SourceDeclaration[],
  sessionImportSet: Set<string>,
): string[] {
  const commands: string[] = []
  const planned = new Set<string>()
  for (const declaration of declarations) {
    if (declaration.status !== 'loaded' || declaration.command.length === 0) continue
    if (declarationLoadedInSession(declaration, sessionImportSet)) continue
    const key = declarationPathKey(declaration) || commandImportPath(declaration.command)
    if (!key || planned.has(key)) continue
    planned.add(key)
    commands.push(declaration.command)
  }
  return commands
}

function isIdentifier(text: string): boolean {
  return /^[A-Za-z_][A-Za-z0-9_]*$/.test(text)
}

export default function SourcePreviewPanel({
  source,
  sourceLabel,
  sourceEditable,
  focusedRange,
  checkingSource,
  syncState,
  syncDetail,
  applyingSource,
  canApplySource,
  pendingPatchRange,
  pendingPatchSummary,
  environmentNotice,
  resolverEnvironment,
  sessionImports,
  declarationRenameContext,
  onCheckSource,
  onSourceChange,
  onApplySource,
  onRevertSource,
  onImportCommand,
  onImportPlan,
  onOpenDeclarationSource,
  onRenameDeclaration,
}: SourcePreviewPanelProps) {
  const scrollRootRef = useRef<HTMLDivElement>(null)
  const textareaRef = useRef<HTMLTextAreaElement>(null)
  const [editing, setEditing] = useState(false)
  const [declarationRenameValue, setDeclarationRenameValue] = useState('')
  const [collapsedImportNodes, setCollapsedImportNodes] = useState<Set<string>>(() => new Set())
  const lines = useMemo(() => source ? source.split(/\r?\n/) : [], [source])
  const importTree = useMemo(
    () => buildImportTree(resolverEnvironment?.declarations ?? []),
    [resolverEnvironment],
  )
  const sessionImportSet = useMemo(() => new Set(sessionImports), [sessionImports])
  const importReplayCommands = useMemo(
    () => buildImportReplayPlan(resolverEnvironment?.declarations ?? [], sessionImportSet),
    [resolverEnvironment, sessionImportSet],
  )
  const busy = checkingSource || applyingSource || syncState === 'checking'
  const displayRange = focusedRange ?? (editing ? null : pendingPatchRange)
  const canRenameDeclaration = Boolean(
    declarationRenameContext &&
    syncState === 'declaration' &&
    !sourceEditable &&
    declarationRenameContext.contentHash.length > 0 &&
    isIdentifier(declarationRenameValue) &&
    declarationRenameValue !== declarationRenameContext.oldName,
  )

  useEffect(() => {
    if (!sourceEditable) setEditing(false)
  }, [sourceEditable])

  useEffect(() => {
    setDeclarationRenameValue(declarationRenameContext?.oldName ?? '')
  }, [declarationRenameContext?.oldName])

  useEffect(() => {
    setCollapsedImportNodes(new Set())
  }, [resolverEnvironment?.environment_hash])

  useEffect(() => {
    if (editing && focusedRange && textareaRef.current) {
      const start = offsetForLocation(source, focusedRange.start)
      const end = offsetForLocation(source, focusedRange.end)
      if (start !== null && end !== null && start <= end) {
        textareaRef.current.focus()
        textareaRef.current.setSelectionRange(start, Math.max(start + 1, end))
      }
    }
    const scrollRange = focusedRange ?? (!editing ? pendingPatchRange : null)
    if (!scrollRange || !scrollRootRef.current) return
    const line = scrollRootRef.current.querySelector<HTMLElement>(
      `[data-source-line="${scrollRange.start.line}"]`,
    )
    line?.scrollIntoView({ block: 'center' })
  }, [editing, focusedRange, pendingPatchRange, source])

  const toggleImportNode = (key: string) => {
    setCollapsedImportNodes(previous => {
      const next = new Set(previous)
      if (next.has(key)) {
        next.delete(key)
      } else {
        next.add(key)
      }
      return next
    })
  }

  const renderImportNode = (node: ImportTreeNode) => {
    const { declaration } = node
    const loadedInSession = declarationLoadedInSession(declaration, sessionImportSet)
    const canImport = declaration.status === 'loaded' && declaration.command.length > 0 && !loadedInSession
    const canOpenSource = declaration.status === 'loaded' && Boolean(declarationPathKey(declaration))
    const hasChildren = node.children.length > 0
    const collapsed = collapsedImportNodes.has(node.key)
    const issueMessage = declarationIssueMessage(declaration)

    return (
      <div
        key={node.key}
        data-source-import-tree-node="true"
        data-source-import-status={declaration.status}
        data-source-import-loaded={loadedInSession ? 'true' : 'false'}
        data-source-import-problem={issueMessage ? 'true' : 'false'}
        data-source-import-has-command={declaration.command.length > 0 ? 'true' : 'false'}
        data-source-import-depth={declaration.depth ?? 1}
        className="min-w-0 font-mono text-[9px]"
      >
        <div
          className={`flex min-w-0 items-center gap-1 rounded border px-1.5 py-0.5 ${importStatusClasses(declaration.status)}`}
          title={loadedInSession
            ? `${declaration.import_chain || declaration.path}: already loaded in session`
            : `${declaration.import_chain || declaration.path}: ${declaration.message || declaration.status}`}
        >
          <button
            type="button"
            data-source-import-toggle={node.key}
            className="inline-flex h-3.5 w-3.5 shrink-0 items-center justify-center rounded text-current hover:bg-current/10 disabled:opacity-30"
            title={hasChildren ? (collapsed ? 'Expand import' : 'Collapse import') : 'No nested imports'}
            disabled={!hasChildren}
            aria-expanded={hasChildren ? !collapsed : undefined}
            onClick={() => toggleImportNode(node.key)}
          >
            {hasChildren ? (
              collapsed ? <ChevronRight className="h-2.5 w-2.5" /> : <ChevronDown className="h-2.5 w-2.5" />
            ) : (
              <span className="h-1 w-1 rounded-full bg-current/50" />
            )}
            <span className="sr-only">{hasChildren ? (collapsed ? 'Expand import' : 'Collapse import') : 'No nested imports'}</span>
          </button>
          <span
            className="min-w-0 flex-1 truncate"
            data-source-import-chain={declaration.import_chain || declaration.path}
          >
            {declarationLabel(declaration)}
          </span>
          {loadedInSession && (
            <span className="shrink-0 rounded bg-current/10 px-1 text-[8px]">
              session
            </span>
          )}
          {(canImport || loadedInSession) && (
            <button
              type="button"
              data-source-import-command={declaration.command}
              className="inline-flex h-3.5 w-3.5 shrink-0 items-center justify-center rounded text-current hover:bg-current/10 disabled:opacity-40"
              title={loadedInSession ? 'Import already loaded in session' : declaration.command}
              disabled={busy || loadedInSession}
              onClick={() => onImportCommand(declaration.command)}
            >
              <Play className="h-2.5 w-2.5" />
              <span className="sr-only">Run import command</span>
            </button>
          )}
          {canOpenSource && (
            <button
              type="button"
              data-source-declaration-open={declarationPathKey(declaration)}
              className="inline-flex h-3.5 w-3.5 shrink-0 items-center justify-center rounded text-current hover:bg-current/10 disabled:opacity-40"
              title={`Open declaration source ${declaration.path}`}
              disabled={busy}
              onClick={() => onOpenDeclarationSource(declarationPathKey(declaration), declaration.content_hash)}
            >
              <FileText className="h-2.5 w-2.5" />
              <span className="sr-only">Open declaration source</span>
            </button>
          )}
        </div>
        {issueMessage && (
          <div
            data-source-import-message="true"
            data-source-import-message-status={declaration.status}
            className="mt-0.5 flex min-w-0 items-center gap-1 px-5 text-[8px] text-destructive"
            title={issueMessage}
          >
            <AlertTriangle className="h-2.5 w-2.5 shrink-0" />
            <span className="truncate">{issueMessage}</span>
          </div>
        )}
        {hasChildren && !collapsed && (
          <div
            data-source-import-children={node.key}
            className="ml-3 mt-1 space-y-1 border-l border-current/20 pl-2"
          >
            {node.children.map(child => renderImportNode(child))}
          </div>
        )}
      </div>
    )
  }

  return (
    <div className="flex h-full flex-col">
      <div className="flex items-center gap-2 border-b px-3 py-1.5 shrink-0">
        <FileText className="h-3.5 w-3.5 text-primary/70" />
        <span className="text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">
          Source
        </span>
        {sourceLabel && (
          <span
            data-source-preview-label="true"
            className="max-w-[12rem] truncate font-mono text-[9px] text-muted-foreground"
            title={sourceLabel}
          >
            {sourceLabel}
          </span>
        )}
        <Badge
          variant="outline"
          data-source-sync-state={syncState}
          className={`h-4 max-w-[9rem] truncate px-1 py-0 text-[8px] ${syncClasses(syncState)}`}
          title={syncDetail || syncLabel(syncState)}
        >
          {syncLabel(syncState)}
        </Badge>
        {displayRange && (
          <span className="ml-auto font-mono text-[9px] text-primary/70">
            {displayRange.start.line}:{displayRange.start.column}
          </span>
        )}
        {!displayRange && <span className="ml-auto" />}
        <Button
          type="button"
          variant="ghost"
          size="icon"
          className="h-6 w-6 text-muted-foreground hover:text-primary"
          onClick={() => setEditing(value => !value)}
          disabled={!source || busy || !sourceEditable}
          title={editing ? 'Preview source' : 'Edit source'}
        >
          <Pencil className="h-3 w-3" />
          <span className="sr-only">{editing ? 'Preview source' : 'Edit source'}</span>
        </Button>
        {editing && (
          <>
            <Button
              type="button"
              variant="ghost"
              size="icon"
              className="h-6 w-6 text-muted-foreground hover:text-primary"
              onClick={onApplySource}
              disabled={!source || !canApplySource || busy || !sourceEditable}
              title="Apply source"
            >
              <Save className="h-3 w-3" />
              <span className="sr-only">Apply source</span>
            </Button>
            <Button
              type="button"
              variant="ghost"
              size="icon"
              className="h-6 w-6 text-muted-foreground hover:text-primary"
              onClick={onRevertSource}
              disabled={!source || busy || !sourceEditable}
              title="Revert source"
            >
              <RotateCcw className="h-3 w-3" />
              <span className="sr-only">Revert source</span>
            </Button>
          </>
        )}
        <Button
          type="button"
          variant="ghost"
          size="icon"
          className="h-6 w-6 text-muted-foreground hover:text-primary"
          onClick={onCheckSource}
          disabled={busy}
          title="Refresh source preview"
        >
          <RefreshCw className={`h-3 w-3 ${busy ? 'animate-spin' : ''}`} />
          <span className="sr-only">Refresh source preview</span>
        </Button>
      </div>
      {syncDetail && (
        <div
          data-source-sync-detail="true"
          className={`shrink-0 truncate border-b px-3 py-1 text-[10px] ${syncClasses(syncState)}`}
          title={syncDetail}
        >
          {syncDetail}
        </div>
      )}
      {declarationRenameContext && syncState === 'declaration' && !sourceEditable && (
        <form
          data-declaration-rename="true"
          data-declaration-rename-kind={declarationRenameContext.kind}
          data-declaration-rename-old={declarationRenameContext.oldName}
          className="flex shrink-0 items-center gap-1 border-b border-primary/20 bg-primary/5 px-3 py-1"
          onSubmit={event => {
            event.preventDefault()
            if (canRenameDeclaration) onRenameDeclaration(declarationRenameValue)
          }}
        >
          <input
            data-declaration-rename-input="true"
            aria-label={`Rename declaration ${declarationRenameContext.oldName}`}
            className="h-5 min-w-0 flex-1 rounded border border-border/60 bg-background px-1.5 font-mono text-[10px] outline-none focus:border-primary/60"
            value={declarationRenameValue}
            disabled={busy}
            onChange={event => setDeclarationRenameValue(event.target.value)}
          />
          <Button
            type="submit"
            variant="ghost"
            size="icon"
            className="h-5 w-5 text-muted-foreground hover:text-primary"
            disabled={busy || !canRenameDeclaration}
            title={declarationRenameContext.kind === 'node_pin'
              ? `Apply import node pin rename for ${declarationRenameContext.ownerName}.${declarationRenameContext.oldName}`
              : declarationRenameContext.kind === 'schema_field'
                ? `Apply import schema field rename for ${declarationRenameContext.ownerName}.${declarationRenameContext.oldName}`
              : `Apply import ${declarationRenameContext.kind} rename for ${declarationRenameContext.oldName}`}
          >
            <Save className="h-3 w-3" />
            <span className="sr-only">Rename declaration</span>
          </Button>
        </form>
      )}
      {pendingPatchSummary && (
        <div
          data-source-patch-summary="true"
          className="shrink-0 truncate border-b border-primary/20 bg-primary/5 px-3 py-1 font-mono text-[10px] text-primary/80"
          title={pendingPatchSummary}
        >
          {pendingPatchSummary}
        </div>
      )}
      {environmentNotice && (
        <div
          data-source-env-notice="true"
          className="shrink-0 truncate border-b border-warning/20 bg-warning/5 px-3 py-1 text-[10px] text-warning/80"
          title={environmentNotice}
        >
          {environmentNotice}
        </div>
      )}
      {resolverEnvironment && (
        <div
          data-source-resolver-metadata="true"
          className="shrink-0 border-b border-primary/20 bg-primary/5 px-3 py-1 text-[10px] text-primary/80"
          title={`Environment ${resolverEnvironment.environment_hash}`}
        >
          <div className="flex min-w-0 items-center gap-2">
            <span className="min-w-0 flex-1 truncate">
              Resolver {resolverEnvironment.mode}; {resolverEnvironment.declarations.length} import{resolverEnvironment.declarations.length === 1 ? '' : 's'}; {resolverEnvironment.node_type_count} node types; {resolverEnvironment.schema_count} schemas
            </span>
            {importReplayCommands.length > 0 && (
              <button
                type="button"
                data-source-import-plan-run="true"
                data-source-import-plan-count={importReplayCommands.length}
                data-source-import-plan-commands={importReplayCommands.join('\n')}
                className="inline-flex h-4 shrink-0 items-center gap-1 rounded border border-current/20 px-1.5 text-[8px] hover:bg-current/10 disabled:opacity-40"
                title={`Run ${importReplayCommands.length} import command${importReplayCommands.length === 1 ? '' : 's'}`}
                disabled={busy}
                onClick={() => onImportPlan(importReplayCommands)}
              >
                <Play className="h-2.5 w-2.5" />
                <span>Import {importReplayCommands.length}</span>
              </button>
            )}
          </div>
          {importTree.length > 0 && (
            <div data-source-import-tree="true" className="mt-1 space-y-1">
              {importTree.map(node => renderImportNode(node))}
            </div>
          )}
        </div>
      )}

      <ScrollArea className="min-h-0 flex-1" ref={scrollRootRef}>
        {editing ? (
          <div className="h-full min-h-[9rem] p-2">
            <textarea
              ref={textareaRef}
              data-source-editor="true"
              className="h-full min-h-[8rem] w-full resize-none rounded border border-border/50 bg-background/80
                p-2 font-mono text-[10px] leading-5 text-foreground/80 outline-none focus:border-primary/50"
              value={source}
              spellCheck={false}
              onChange={(event) => onSourceChange(event.target.value)}
              disabled={busy}
            />
          </div>
        ) : (
        <div className="min-w-max p-2 font-mono text-[10px] leading-5">
          {lines.length === 0 ? (
            <div className="px-2 py-6 text-center text-[11px] text-muted-foreground/50">
              No source
            </div>
          ) : (
            lines.map((line, index) => {
              const lineNumber = index + 1
              const focused = isRangeOnLine(displayRange, lineNumber)
              return (
                <div
                  key={lineNumber}
                  data-source-line={lineNumber}
                  data-source-focused={focused ? 'true' : 'false'}
                  data-source-pending-patch={pendingPatchRange && isRangeOnLine(pendingPatchRange, lineNumber) ? 'true' : 'false'}
                  className={`grid grid-cols-[3rem_1fr] gap-3 rounded-sm px-2 ${
                    focused ? 'bg-primary/10 ring-1 ring-primary/20' : 'hover:bg-secondary/30'
                  }`}
                >
                  <span className="select-none text-right text-muted-foreground/45">
                    {lineNumber}
                  </span>
                  <span className="whitespace-pre text-foreground/80">
                    {renderLineText(line, lineNumber, displayRange)}
                  </span>
                </div>
              )
            })
          )}
        </div>
        )}
      </ScrollArea>
    </div>
  )
}
