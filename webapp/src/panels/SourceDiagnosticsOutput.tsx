import { useMemo, useState } from 'react'
import { AlertTriangle, ChevronDown, ChevronRight, FileSearch, FileText, Play } from 'lucide-react'
import type { SourceDiagnosticsEnvironment } from '@/api/types'

interface SourceDiagnosticsOutputProps {
  environmentNotice: string
  resolverEnvironment: SourceDiagnosticsEnvironment | null
  sessionImports: string[]
  busy: boolean
  onImportCommand: (command: string) => void
  onImportPlan: (commands: string[]) => void
  onOpenDeclarationSource: (path: string, contentHash: string) => void
}

type SourceDeclaration = SourceDiagnosticsEnvironment['declarations'][number]

interface ImportTreeNode {
  key: string
  pathKey: string
  declaration: SourceDeclaration
  children: ImportTreeNode[]
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
    case 'semantic_error':
      return 'border-destructive/30 bg-destructive/10 text-destructive'
    default:
      return 'border-warning/30 bg-warning/10 text-warning'
  }
}

export default function SourceDiagnosticsOutput({
  environmentNotice,
  resolverEnvironment,
  sessionImports,
  busy,
  onImportCommand,
  onImportPlan,
  onOpenDeclarationSource,
}: SourceDiagnosticsOutputProps) {
  const environmentHash = resolverEnvironment?.environment_hash ?? ''
  const [collapsedImportState, setCollapsedImportState] = useState<{ environmentHash: string; nodes: Set<string> }>(() => ({
    environmentHash: '',
    nodes: new Set(),
  }))
  const collapsedImportNodes = collapsedImportState.environmentHash === environmentHash
    ? collapsedImportState.nodes
    : new Set<string>()
  const sessionImportSet = useMemo(() => new Set(sessionImports), [sessionImports])
  const importTree = useMemo(
    () => buildImportTree(resolverEnvironment?.declarations ?? []),
    [resolverEnvironment],
  )
  const importReplayCommands = useMemo(
    () => buildImportReplayPlan(resolverEnvironment?.declarations ?? [], sessionImportSet),
    [resolverEnvironment, sessionImportSet],
  )

  if (!environmentNotice && !resolverEnvironment) return null

  const toggleImportNode = (key: string) => {
    setCollapsedImportState(previous => {
      const previousNodes = previous.environmentHash === environmentHash ? previous.nodes : new Set<string>()
      const next = new Set(previousNodes)
      if (next.has(key)) {
        next.delete(key)
      } else {
        next.add(key)
      }
      return { environmentHash, nodes: next }
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
            ? `${declaration.import_chain || declaration.path}: 已加载到当前会话`
            : `${declaration.import_chain || declaration.path}: ${declaration.message || declaration.status}`}
        >
          <button
            type="button"
            data-source-import-toggle={node.key}
            className="inline-flex h-3.5 w-3.5 shrink-0 items-center justify-center rounded text-current hover:bg-current/10 disabled:opacity-30"
            title={hasChildren ? (collapsed ? '展开导入' : '折叠导入') : '没有嵌套导入'}
            disabled={!hasChildren}
            aria-expanded={hasChildren ? !collapsed : undefined}
            onClick={() => toggleImportNode(node.key)}
          >
            {hasChildren ? (
              collapsed ? <ChevronRight className="h-2.5 w-2.5" /> : <ChevronDown className="h-2.5 w-2.5" />
            ) : (
              <span className="h-1 w-1 rounded-full bg-current/50" />
            )}
            <span className="sr-only">{hasChildren ? (collapsed ? '展开导入' : '折叠导入') : '没有嵌套导入'}</span>
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
              title={loadedInSession ? '导入已加载到当前会话' : declaration.command}
              disabled={busy || loadedInSession}
              onClick={() => onImportCommand(declaration.command)}
            >
              <Play className="h-2.5 w-2.5" />
              <span className="sr-only">运行导入命令</span>
            </button>
          )}
          {canOpenSource && (
            <button
              type="button"
              data-source-declaration-open={declarationPathKey(declaration)}
              className="inline-flex h-3.5 w-3.5 shrink-0 items-center justify-center rounded text-current hover:bg-current/10 disabled:opacity-40"
              title={`打开声明源码 ${declaration.path}`}
              disabled={busy}
              onClick={() => onOpenDeclarationSource(declarationPathKey(declaration), declaration.content_hash)}
            >
              <FileText className="h-2.5 w-2.5" />
              <span className="sr-only">打开声明源码</span>
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
    <div
      data-source-diagnostics-output="true"
      className="mx-2 mt-2 max-h-52 shrink-0 overflow-auto rounded border border-primary/20 bg-primary/5 p-2 text-[10px] text-primary/85"
    >
      <div className="mb-1 flex items-center gap-1.5 text-[10px] font-semibold uppercase tracking-wider text-primary/80">
        <FileSearch className="h-3 w-3" />
        <span>Source Diagnostics Output</span>
      </div>
      {environmentNotice && (
        <div
          data-source-env-notice="true"
          className="font-mono leading-relaxed text-warning/90"
          title={environmentNotice}
        >
          {environmentNotice}
        </div>
      )}
      {resolverEnvironment && (
        <div
          data-source-resolver-metadata="true"
          className="mt-1 font-mono leading-relaxed"
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
                title={`运行 ${importReplayCommands.length} 条导入命令`}
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
    </div>
  )
}
