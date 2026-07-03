import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import Editor, { type OnMount } from '@monaco-editor/react'
import type * as Monaco from 'monaco-editor'
import { FileText, Pencil, RefreshCw, RotateCcw, Save } from 'lucide-react'
import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import { ScrollArea } from '@/components/ui/scroll-area'
import { fetchCompletions } from '@/api/client'
import type { Diagnostic, SourceRange } from '@/api/types'

type MonacoEditorInstance = Parameters<OnMount>[0]
type MonacoApi = Parameters<OnMount>[1]

let graphScriptMonacoConfigured = false
const SOURCE_DIAGNOSTIC_MARKER_OWNER = 'graphscript-source-diagnostics'

declare global {
  interface Window {
    __graphScriptSourceEditor?: {
      getSelectedText: () => string
      getValue: () => string
      setValue: (value: string) => void
      focus: () => void
      getLastExternalSyncKind: () => string
      isReadOnly: () => boolean
      getDiagnosticMarkers: () => Array<{ severity: string; message: string; line: number; column: number }>
    }
  }
}

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
  sourceDiagnostics: Diagnostic[]
  declarationRenameContext: DeclarationRenameContext | null
  onCheckSource: () => void
  onSourceChange: (source: string) => void
  onApplySource: () => void
  onRevertSource: () => void
  onRenameDeclaration: (newName: string) => void
}

function isRangeOnLine(range: SourceRange | null, line: number): boolean {
  if (!range) return false
  return line >= range.start.line && line <= range.end.line
}

function clampColumn(column: number, lineLength: number): number {
  return Math.max(1, Math.min(column, lineLength + 1))
}

function sourceLineLength(source: string, lineNumber: number): number {
  if (lineNumber < 1) return 0
  return (source.split(/\r?\n/)[lineNumber - 1] ?? '').length
}

function toMonacoRange(source: string, range: SourceRange | null) {
  if (!range) return null
  const startLineNumber = Math.max(1, range.start.line)
  const endLineNumber = Math.max(startLineNumber, range.end.line)
  const startColumn = clampColumn(range.start.column, sourceLineLength(source, startLineNumber))
  const endColumn = clampColumn(range.end.column, sourceLineLength(source, endLineNumber))
  return {
    startLineNumber,
    startColumn,
    endLineNumber,
    endColumn: endLineNumber === startLineNumber && endColumn <= startColumn ? startColumn + 1 : endColumn,
  }
}

function focusMonacoRange(editor: MonacoEditorInstance | null, source: string, range: SourceRange | null) {
  const monacoRange = toMonacoRange(source, range)
  if (!editor || !monacoRange) return
  editor.setSelection(monacoRange)
  editor.revealRangeInCenter(monacoRange)
  editor.focus()
}

function commonPrefixLength(left: string, right: string): number {
  const limit = Math.min(left.length, right.length)
  let index = 0
  while (index < limit && left[index] === right[index]) index += 1
  return index
}

function commonSuffixLength(left: string, right: string, prefixLength: number): number {
  const limit = Math.min(left.length, right.length) - prefixLength
  let length = 0
  while (
    length < limit &&
    left[left.length - 1 - length] === right[right.length - 1 - length]
  ) {
    length += 1
  }
  return length
}

function applyMinimalMonacoEdit(editor: MonacoEditorInstance, nextValue: string): 'none' | 'patch' | 'replace' {
  const model = editor.getModel()
  if (!model) return 'replace'
  const currentValue = model.getValue()
  if (currentValue === nextValue) return 'none'

  const prefixLength = commonPrefixLength(currentValue, nextValue)
  const suffixLength = commonSuffixLength(currentValue, nextValue, prefixLength)
  const currentEndOffset = currentValue.length - suffixLength
  const nextEndOffset = nextValue.length - suffixLength
  const startPosition = model.getPositionAt(prefixLength)
  const endPosition = model.getPositionAt(currentEndOffset)
  const replacement = nextValue.slice(prefixLength, nextEndOffset)

  model.pushEditOperations([], [{
    range: {
      startLineNumber: startPosition.lineNumber,
      startColumn: startPosition.column,
      endLineNumber: endPosition.lineNumber,
      endColumn: endPosition.column,
    },
    text: replacement,
    forceMoveMarkers: true,
  }], () => null)
  return 'patch'
}

function configureGraphScriptMonaco(monaco: MonacoApi) {
  if (!graphScriptMonacoConfigured) {
    graphScriptMonacoConfigured = true
    if (!monaco.languages.getLanguages().some((language: { id: string }) => language.id === 'graphscript')) {
      monaco.languages.register({ id: 'graphscript' })
    }
    monaco.languages.setMonarchTokensProvider('graphscript', {
      defaultToken: '',
      tokenPostfix: '.gs',
      keywords: [
        'bind',
        'connect',
        'declare',
        'event',
        'export',
        'graph',
        'import',
        'in',
        'node',
        'out',
        'param',
        'schema',
        'type',
      ],
      tokenizer: {
        root: [
          [/[A-Za-z_][\w]*/, { cases: { '@keywords': 'keyword', '@default': 'identifier' } }],
          [/".*?"/, 'string'],
          [/\b\d+(\.\d+)?\b/, 'number'],
          [/\/\/.*$/, 'comment'],
          [/[{}()[\].,:;]/, 'delimiter'],
        ],
      },
    })
    monaco.languages.setLanguageConfiguration('graphscript', {
      comments: { lineComment: '//' },
      brackets: [['{', '}'], ['[', ']'], ['(', ')']],
      autoClosingPairs: [
        { open: '{', close: '}' },
        { open: '[', close: ']' },
        { open: '(', close: ')' },
        { open: '"', close: '"' },
      ],
    })
    monaco.languages.registerCompletionItemProvider('graphscript', {
      triggerCharacters: ['.', '"', '@', ' ', '('],
      provideCompletionItems: async (model: Monaco.editor.ITextModel, position: Monaco.Position) => {
        const word = model.getWordUntilPosition(position)
        const range = {
          startLineNumber: position.lineNumber,
          endLineNumber: position.lineNumber,
          startColumn: word.startColumn,
          endColumn: word.endColumn,
        }
        try {
          const response = await fetchCompletions(model.getValue(), position.lineNumber, position.column)
          const suggestions = response.items.map(item => ({
            label: item.label,
            kind: completionKind(monaco, item.kind),
            detail: item.detail,
            insertText: item.insertText ?? item.label,
            insertTextRules: (item.insertText ?? '').includes('${')
              ? monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet
              : undefined,
            range,
          }))
          return { suggestions }
        } catch {
          return { suggestions: [] }
        }
      },
    })
  }

  monaco.editor.defineTheme('graphscript-dark', {
    base: 'vs-dark',
    inherit: true,
    rules: [
      { token: 'keyword', foreground: '72a7ff', fontStyle: 'bold' },
      { token: 'string', foreground: 'd98bd0' },
      { token: 'number', foreground: '7ee2a8' },
      { token: 'comment', foreground: '697386' },
      { token: 'delimiter', foreground: '8ba0bb' },
    ],
    colors: {
      'editor.background': '#11141c',
      'editor.foreground': '#d7deea',
      'editorLineNumber.foreground': '#526071',
      'editorLineNumber.activeForeground': '#7aa7ff',
      'editor.selectionBackground': '#2d5b9f66',
      'editor.lineHighlightBackground': '#1a2230',
      'editorCursor.foreground': '#7aa7ff',
      'editorIndentGuide.background1': '#293140',
      'editorIndentGuide.activeBackground1': '#3c4a62',
    },
  })
}

function completionKind(monaco: MonacoApi, kind: string) {
  const completionKinds = monaco.languages.CompletionItemKind
  switch (kind) {
    case 'Keyword': return completionKinds.Keyword
    case 'Function': return completionKinds.Function
    case 'Class': return completionKinds.Class
    case 'Field': return completionKinds.Field
    case 'Variable': return completionKinds.Variable
    case 'Property': return completionKinds.Property
    case 'Snippet': return completionKinds.Snippet
    default: return completionKinds.Text
  }
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

function isIdentifier(text: string): boolean {
  return /^[A-Za-z_][A-Za-z0-9_]*$/.test(text)
}

function markerSeverity(monaco: MonacoApi, severity: Diagnostic['severity']) {
  return severity === 'error' ? monaco.MarkerSeverity.Error : monaco.MarkerSeverity.Warning
}

function diagnosticLineClass(severity: Diagnostic['severity']): string {
  return severity === 'error' ? 'source-diagnostic-error-line' : 'source-diagnostic-warning-line'
}

function diagnosticGlyphClass(severity: Diagnostic['severity']): string {
  return severity === 'error' ? 'source-diagnostic-error-glyph' : 'source-diagnostic-warning-glyph'
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
  sourceDiagnostics,
  declarationRenameContext,
  onCheckSource,
  onSourceChange,
  onApplySource,
  onRevertSource,
  onRenameDeclaration,
}: SourcePreviewPanelProps) {
  const scrollRootRef = useRef<HTMLDivElement>(null)
  const editorRef = useRef<MonacoEditorInstance | null>(null)
  const monacoRef = useRef<MonacoApi | null>(null)
  const diagnosticDecorationsRef = useRef<Monaco.editor.IEditorDecorationsCollection | null>(null)
  const suppressEditorChangeRef = useRef(false)
  const lastExternalSyncKindRef = useRef('')
  const [editing, setEditing] = useState(true)
  const [declarationRenameValue, setDeclarationRenameValue] = useState('')
  const lines = useMemo(() => source ? source.split(/\r?\n/) : [], [source])
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

  const handleEditorMount = useCallback<OnMount>((editor, monaco) => {
    editorRef.current = editor
    monacoRef.current = monaco
    diagnosticDecorationsRef.current = editor.createDecorationsCollection()
    configureGraphScriptMonaco(monaco)
    monaco.editor.setTheme('graphscript-dark')
    window.__graphScriptSourceEditor = {
      getSelectedText: () => {
        const model = editor.getModel()
        const selection = editor.getSelection()
        return model && selection ? model.getValueInRange(selection) : ''
      },
      getValue: () => editor.getValue(),
      setValue: (value: string) => {
        editor.setValue(value)
        onSourceChange(value)
      },
      focus: () => editor.focus(),
      getLastExternalSyncKind: () => lastExternalSyncKindRef.current,
      isReadOnly: () => editor.getOption(monaco.editor.EditorOption.readOnly),
      getDiagnosticMarkers: () => {
        const model = editor.getModel()
        if (!model) return []
        return monaco.editor.getModelMarkers({ owner: SOURCE_DIAGNOSTIC_MARKER_OWNER, resource: model.uri })
          .map((marker: Monaco.editor.IMarker) => ({
            severity: marker.severity === monaco.MarkerSeverity.Error ? 'error' : 'warning',
            message: marker.message,
            line: marker.startLineNumber,
            column: marker.startColumn,
          }))
      },
    }
    focusMonacoRange(editor, source, focusedRange)
  }, [focusedRange, onSourceChange, source])

  useEffect(() => {
    const editor = editorRef.current
    if (!editing || !editor) return
    const viewState = editor.saveViewState()
    suppressEditorChangeRef.current = true
    try {
      const syncKind = applyMinimalMonacoEdit(editor, source)
      if (syncKind !== 'none') {
        lastExternalSyncKindRef.current = syncKind
      }
      if (viewState) editor.restoreViewState(viewState)
    } finally {
      suppressEditorChangeRef.current = false
    }
  }, [editing, source])

  useEffect(() => {
    const editor = editorRef.current
    const monaco = monacoRef.current
    if (!editor || !monaco) return

    const model = editor.getModel()
    if (!model) return

    const rangedDiagnostics = sourceDiagnostics
      .filter(diagnostic => Boolean(diagnostic.range))
      .map(diagnostic => {
        const monacoRange = toMonacoRange(source, diagnostic.range)
        return monacoRange ? { diagnostic, monacoRange } : null
      })
      .filter((item): item is { diagnostic: Diagnostic; monacoRange: NonNullable<ReturnType<typeof toMonacoRange>> } => Boolean(item))

    monaco.editor.setModelMarkers(model, SOURCE_DIAGNOSTIC_MARKER_OWNER, rangedDiagnostics.map(({ diagnostic, monacoRange }) => ({
      severity: markerSeverity(monaco, diagnostic.severity),
      message: diagnostic.message,
      code: diagnostic.code,
      startLineNumber: monacoRange.startLineNumber,
      startColumn: monacoRange.startColumn,
      endLineNumber: monacoRange.endLineNumber,
      endColumn: monacoRange.endColumn,
    })))

    diagnosticDecorationsRef.current?.set(rangedDiagnostics.map(({ diagnostic, monacoRange }) => ({
      range: new monaco.Range(
        monacoRange.startLineNumber,
        monacoRange.startColumn,
        monacoRange.endLineNumber,
        monacoRange.endColumn,
      ),
      options: {
        isWholeLine: true,
        className: diagnosticLineClass(diagnostic.severity),
        glyphMarginClassName: diagnosticGlyphClass(diagnostic.severity),
        hoverMessage: { value: `**${diagnostic.code || diagnostic.severity}** ${diagnostic.message}` },
        overviewRuler: {
          color: diagnostic.severity === 'error' ? '#dc2626' : '#f59e0b',
          position: monaco.editor.OverviewRulerLane.Right,
        },
      },
    })))
  }, [source, sourceDiagnostics])

  useEffect(() => {
    if (editing) {
      focusMonacoRange(editorRef.current, source, focusedRange)
      return
    }

    const scrollRange = focusedRange ?? pendingPatchRange
    if (!scrollRange || !scrollRootRef.current) return
    const line = scrollRootRef.current.querySelector<HTMLElement>(
      `[data-source-line="${scrollRange.start.line}"]`,
    )
    line?.scrollIntoView({ block: 'center' })
  }, [editing, focusedRange, pendingPatchRange, source])

  useEffect(() => () => {
    const editor = editorRef.current
    const monaco = monacoRef.current
    const model = editor?.getModel()
    if (monaco && model) {
      monaco.editor.setModelMarkers(model, SOURCE_DIAGNOSTIC_MARKER_OWNER, [])
    }
    diagnosticDecorationsRef.current?.clear()
    window.__graphScriptSourceEditor = undefined
    editorRef.current = null
    monacoRef.current = null
  }, [])

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
          title="Refresh source diagnostics"
        >
          <RefreshCw className={`h-3 w-3 ${busy ? 'animate-spin' : ''}`} />
          <span className="sr-only">Refresh source diagnostics</span>
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

      {editing ? (
        <div data-source-editor="true" className="min-h-0 flex-1 p-2">
          <div className="h-full min-h-[8rem] overflow-hidden rounded border border-border/50 bg-background/80">
            <Editor
              height="100%"
              language="graphscript"
              theme="graphscript-dark"
              defaultValue={source}
              onMount={handleEditorMount}
              onChange={(value) => {
                if (suppressEditorChangeRef.current) return
                onSourceChange(value ?? '')
              }}
              options={{
                automaticLayout: true,
                fontFamily: "'JetBrains Mono', 'Cascadia Code', 'Fira Code', monospace",
                fontSize: 12,
                lineHeight: 20,
                glyphMargin: true,
                minimap: { enabled: false },
                padding: { top: 8, bottom: 8 },
                readOnly: applyingSource || !sourceEditable,
                renderLineHighlight: 'line',
                scrollBeyondLastLine: false,
                tabSize: 4,
                wordWrap: 'on',
              }}
            />
          </div>
        </div>
      ) : (
        <ScrollArea className="min-h-0 flex-1" ref={scrollRootRef}>
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
        </ScrollArea>
      )}
    </div>
  )
}
