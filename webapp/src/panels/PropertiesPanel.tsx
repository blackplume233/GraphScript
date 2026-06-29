import { FormEvent, useEffect, useState } from 'react'
import { ArrowDown, ArrowUp, Boxes, FileSearch, FunctionSquare, Info, Pencil, Plus, Trash2 } from 'lucide-react'
import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Separator } from '@/components/ui/separator'
import type {
  Annotation,
  DeclaredTypeDef,
  Diagnostic,
  GenerateBlockDef,
  GenerateCommentDef,
  GenerateMetadataDef,
  GSState,
  GraphDef,
  GraphParam,
  ImportDef,
  InitializerField,
  LetDef,
  LogicBlock,
  NodeFieldDef,
  NodeInst,
  PinDef,
  NodeTypeDef,
  SchemaDef,
  SourceRange,
} from '@/api/types'
import type { EdgeEditPayload } from '@/canvas/FlowCanvas'
import { pinColor } from '@/canvas/port-config'

interface PropertiesPanelProps {
  state: GSState | null
  graphIndex: number
  selectedNode: string | null
  selectedEdge?: EdgeEditPayload | null
  diagnostics?: Diagnostic[]
  focusedDiagnostic?: Diagnostic | null
  onSourceRangeFocus?: (range: SourceRange, sourceFile?: string) => Promise<void> | void
  onEdgeRedirected?: (edge: EdgeEditPayload) => void
  onExec: (cmd: string) => Promise<void> | void
}

function PropRow({ label, value, mono }: { label: string; value: string | number; mono?: boolean }) {
  return (
    <div className="flex items-center justify-between py-1">
      <span className="text-[10px] text-muted-foreground/70">{label}</span>
      <span className={`text-[11px] text-foreground/80 truncate max-w-[120px] ${mono ? 'font-mono text-[10px]' : ''}`}>
        {value}
      </span>
    </div>
  )
}

function PanelSection({
  title,
  children,
}: {
  title: string
  children: React.ReactNode
}) {
  return (
    <div>
      <div className="text-[9px] uppercase text-muted-foreground/50 font-bold tracking-[0.1em] mb-1.5">
        {title}
      </div>
      {children}
    </div>
  )
}

function CompactIconButton({
  label,
  disabled,
}: {
  label: string
  disabled?: boolean
}) {
  return (
    <Button type="submit" variant="secondary" size="icon" disabled={disabled} className="h-7 w-7 shrink-0">
      <Plus className="h-3.5 w-3.5" />
      <span className="sr-only">{label}</span>
    </Button>
  )
}

function DeleteIconButton({
  label,
  onClick,
  dataAction,
}: {
  label: string
  onClick: () => void
  dataAction?: string
}) {
  return (
    <Button
      type="button"
      variant="ghost"
      size="icon"
      data-action={dataAction}
      className="ml-auto h-5 w-5 shrink-0 text-muted-foreground hover:text-destructive"
      onClick={onClick}
    >
      <Trash2 className="h-3 w-3" />
      <span className="sr-only">{label}</span>
    </Button>
  )
}

function MoveIconButton({
  label,
  direction,
  disabled,
  onClick,
}: {
  label: string
  direction: 'up' | 'down'
  disabled?: boolean
  onClick: () => void
}) {
  const Icon = direction === 'up' ? ArrowUp : ArrowDown
  return (
    <Button
      type="button"
      variant="ghost"
      size="icon"
      disabled={disabled}
      className="h-5 w-5 shrink-0 text-muted-foreground hover:text-primary"
      onClick={onClick}
      title={label}
    >
      <Icon className="h-3 w-3" />
      <span className="sr-only">{label}</span>
    </Button>
  )
}

function RenameForm({
  label,
  placeholder,
  currentName,
  onRename,
}: {
  label: string
  placeholder: string
  currentName: string
  onRename: (name: string) => Promise<void> | void
}) {
  const [name, setName] = useState('')
  const nextName = name.trim()

  const submit = async (e: FormEvent) => {
    e.preventDefault()
    if (!nextName || nextName === currentName) return
    await onRename(nextName)
    setName('')
  }

  return (
    <form className="flex gap-1.5" onSubmit={submit}>
      <Input
        aria-label={label}
        value={name}
        onChange={e => setName(e.target.value)}
        placeholder={placeholder}
        className="h-7 px-2 text-[11px] font-mono"
      />
      <Button
        type="submit"
        variant="secondary"
        size="icon"
        disabled={!nextName || nextName === currentName}
        className="h-7 w-7 shrink-0"
        title={label}
      >
        <Pencil className="h-3.5 w-3.5" />
        <span className="sr-only">{label}</span>
      </Button>
    </form>
  )
}

function EndpointRedirectForm({
  label,
  placeholder,
  currentEndpoint,
  dataAction,
  onRedirect,
}: {
  label: string
  placeholder: string
  currentEndpoint: string
  dataAction: string
  onRedirect: (endpoint: string) => Promise<void> | void
}) {
  const [endpoint, setEndpoint] = useState('')
  const nextEndpoint = endpoint.trim()

  const submit = async (e: FormEvent) => {
    e.preventDefault()
    if (!nextEndpoint || nextEndpoint === currentEndpoint) return
    await onRedirect(nextEndpoint)
    setEndpoint('')
  }

  return (
    <form className="flex gap-1.5" data-action={dataAction} onSubmit={submit}>
      <Input
        aria-label={label}
        value={endpoint}
        onChange={e => setEndpoint(e.target.value)}
        placeholder={placeholder}
        className="h-7 px-2 text-[11px] font-mono"
      />
      <Button
        type="submit"
        variant="secondary"
        size="icon"
        disabled={!nextEndpoint || nextEndpoint === currentEndpoint}
        className="h-7 w-7 shrink-0"
        title={label}
      >
        <Pencil className="h-3.5 w-3.5" />
        <span className="sr-only">{label}</span>
      </Button>
    </form>
  )
}

function isDefaultSourceRange(range?: SourceRange | null): boolean {
  return !range ||
    (range.start.line === 1 &&
      range.start.column === 1 &&
      range.end.line === 1 &&
      range.end.column === 1)
}

const declarationSourceOpenRange: SourceRange = {
  start: { line: 1, column: 1 },
  end: { line: 1, column: 2 },
}

function SourceRangeButton({
  range,
  sourceFile,
  label,
  sourceKey,
  onSourceRangeFocus,
}: {
  range?: SourceRange
  sourceFile?: string
  label: string
  sourceKey: string
  onSourceRangeFocus?: (range: SourceRange, sourceFile?: string) => Promise<void> | void
}) {
  if (!range || isDefaultSourceRange(range) || !onSourceRangeFocus) return null
  const sourceRange = range

  return (
    <Button
      type="button"
      variant="ghost"
      size="icon"
      data-source-jump={sourceKey}
      className="h-5 w-5 shrink-0 text-muted-foreground hover:text-primary"
      title={label}
      onClick={() => onSourceRangeFocus(sourceRange, sourceFile)}
    >
      <FileSearch className="h-3 w-3" />
      <span className="sr-only">{label}</span>
    </Button>
  )
}

function quoteCommandArg(value: string): string {
  return `"${value.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`
}

function annotationArgCommandPart(arg: Annotation['args'][number]): string {
  const value = quoteCommandArg(arg.value)
  return arg.name ? `${arg.name}=${value}` : value
}

function annotationCommandSuffix(annotation: Annotation): string {
  const args = annotation.args.map(annotationArgCommandPart).join(' ')
  return args ? `${annotation.name} ${args}` : annotation.name
}

function annotationMigrationCommands(prefix: string, annotations: Annotation[]): string[] {
  return annotations.map(annotation => `${prefix} ${annotationCommandSuffix(annotation)}`)
}

function constructorTypeName(value: string): string {
  return value.trim().match(/^([A-Za-z_][A-Za-z0-9_]*)\s*\(/)?.[1] ?? ''
}

function constructorArgumentValue(value: string): string {
  const trimmed = value.trim()
  const openParen = trimmed.indexOf('(')
  if (openParen < 0 || !trimmed.endsWith(')')) return ''
  return trimmed.slice(openParen + 1, -1)
}

function isConstructorCallValue(value: string): boolean {
  return /^[A-Za-z_][A-Za-z0-9_]*\s*\(.*\)\s*$/.test(value.trim())
}

function annotationArgsText(annotation: Annotation): string {
  if (annotation.args.length === 0) return ''
  return annotation.args
    .map(arg => arg.name ? `${arg.name}=${arg.value}` : arg.value)
    .join(', ')
}

function AnnotationPanel({
  annotations,
  annotateCommand,
  unannotateCommand,
  onExec,
  onSourceRangeFocus,
  sourceKeyPrefix,
  sourceFile,
  declaredTypes = [],
  readOnly = false,
}: {
  annotations: Annotation[]
  annotateCommand?: string
  unannotateCommand?: string
  onExec?: (cmd: string) => Promise<void> | void
  onSourceRangeFocus?: (range: SourceRange, sourceFile?: string) => Promise<void> | void
  sourceKeyPrefix?: string
  sourceFile?: string
  declaredTypes?: DeclaredTypeDef[]
  readOnly?: boolean
}) {
  const [name, setName] = useState('')
  const [argName, setArgName] = useState('')
  const [argValue, setArgValue] = useState('')
  const canEdit = !readOnly && Boolean(annotateCommand && unannotateCommand && onExec)

  const addAnnotation = async (e: FormEvent) => {
    e.preventDefault()
    if (!canEdit || !annotateCommand || !onExec) return
    const annotationName = name.trim()
    if (!annotationName) return

    const key = argName.trim()
    const value = argValue.trim()
    const arg = value
      ? key
        ? ` ${key}=${quoteCommandArg(value)}`
        : ` ${quoteCommandArg(value)}`
      : ''

    await onExec(`${annotateCommand} ${annotationName}${arg}`)
    setName('')
    setArgName('')
    setArgValue('')
  }

  if (!canEdit && annotations.length === 0) return null

  return (
    <div className="space-y-1.5">
      {annotations.length > 0 && (
        <div className="space-y-1">
          {annotations.map(annotation => (
            <div key={annotation.id ?? annotation.name} className="flex items-start gap-1.5 py-0.5">
              <Badge variant="outline"
                className="text-[8px] px-1 py-0 h-3.5 border-border/50 text-muted-foreground/60"
              >
                {annotation.name}
              </Badge>
              {sourceKeyPrefix && (
                <SourceRangeButton
                  range={annotation.name_source_range ?? annotation.source_range}
                  label={`Locate source for annotation ${annotation.name}`}
                  sourceKey={`annotation-${sourceKeyPrefix}-${annotation.name}`}
                  sourceFile={sourceFile}
                  onSourceRangeFocus={onSourceRangeFocus}
                />
              )}
              <div className="min-w-0 flex-1">
                {annotation.args.length > 0 && (
                  <div className="flex flex-wrap gap-1">
                    {annotation.args.map((arg, index) => {
                      const argKey = arg.name || `arg${index}`
                      const argSourceKey = `annotation-arg-${sourceKeyPrefix}-${annotation.name}-${argKey}`
                      const valueKey = `annotation-arg-value-${sourceKeyPrefix}-${annotation.name}-${argKey}`
                      const nameKey = `annotation-arg-name-${sourceKeyPrefix}-${annotation.name}-${argKey}`
                      const constructorKey = `annotation-arg-constructor-${sourceKeyPrefix}-${annotation.name}-${argKey}`
                      const constructorTypeKey = `annotation-arg-constructor-type-${sourceKeyPrefix}-${annotation.name}-${argKey}`
                      const constructorTypeDefKey = `annotation-arg-constructor-type-def-${sourceKeyPrefix}-${annotation.name}-${argKey}`
                      const constructorArgKey = `annotation-arg-constructor-arg-${sourceKeyPrefix}-${annotation.name}-${argKey}`
                      const constructorTypeDef = declaredTypes.find(typeDef => typeDef.name === constructorTypeName(arg.value))
                      return (
                        <span
                          key={arg.id ?? `${arg.name}-${arg.value}-${index}`}
                          className="inline-flex max-w-full flex-wrap items-center gap-0.5"
                        >
                          {arg.name && (
                            <>
                              <span className="max-w-[5rem] truncate font-mono text-[10px] text-muted-foreground/50">
                                {arg.name}
                              </span>
                              {sourceKeyPrefix && (
                                <SourceRangeButton
                                  range={arg.name_source_range}
                                  label={`Locate source for annotation arg ${arg.name}`}
                                  sourceKey={nameKey}
                                  sourceFile={sourceFile}
                                  onSourceRangeFocus={onSourceRangeFocus}
                                />
                              )}
                              <span className="font-mono text-[10px] text-muted-foreground/40">=</span>
                            </>
                          )}
                          <span className="max-w-[6rem] truncate font-mono text-[10px] text-muted-foreground/50">
                            {arg.value}
                          </span>
                          {sourceKeyPrefix && (
                            <SourceRangeButton
                              range={arg.value_source_range ?? arg.source_range}
                              label={`Locate source for annotation arg value ${argKey}`}
                              sourceKey={valueKey}
                              sourceFile={sourceFile}
                              onSourceRangeFocus={onSourceRangeFocus}
                            />
                          )}
                          {sourceKeyPrefix && (
                            <SourceRangeButton
                              range={arg.value_constructor_type_source_range}
                              label={`Locate source for annotation arg constructor type ${argKey}`}
                              sourceKey={constructorTypeKey}
                              sourceFile={sourceFile}
                              onSourceRangeFocus={onSourceRangeFocus}
                            />
                          )}
                          {sourceKeyPrefix && (
                            <SourceRangeButton
                              range={constructorTypeDef?.name_source_range ?? constructorTypeDef?.source_range}
                              sourceFile={constructorTypeDef?.source_file}
                              label={`Locate source for annotation arg constructor type definition ${argKey}`}
                              sourceKey={constructorTypeDefKey}
                              onSourceRangeFocus={onSourceRangeFocus}
                            />
                          )}
                          {sourceKeyPrefix && (
                            <SourceRangeButton
                              range={arg.value_constructor_source_range}
                              label={`Locate source for annotation arg constructor ${argKey}`}
                              sourceKey={constructorKey}
                              sourceFile={sourceFile}
                              onSourceRangeFocus={onSourceRangeFocus}
                            />
                          )}
                          {sourceKeyPrefix && (
                            <SourceRangeButton
                              range={arg.value_constructor_arg_source_range}
                              label={`Locate source for annotation arg constructor argument ${argKey}`}
                              sourceKey={constructorArgKey}
                              sourceFile={sourceFile}
                              onSourceRangeFocus={onSourceRangeFocus}
                            />
                          )}
                          {sourceKeyPrefix && (
                            <SourceRangeButton
                              range={arg.source_range}
                              label={`Locate source for annotation arg ${argKey}`}
                              sourceKey={argSourceKey}
                              sourceFile={sourceFile}
                              onSourceRangeFocus={onSourceRangeFocus}
                            />
                          )}
                        </span>
                      )
                    })}
                  </div>
                )}
                {annotation.args.length === 0 && (
                  <span className="text-[10px] text-muted-foreground/40 font-mono">
                    {annotationArgsText(annotation)}
                  </span>
                )}
              </div>
              {canEdit && unannotateCommand && onExec && (
                <Button
                  type="button"
                  variant="ghost"
                  size="icon"
                  className="ml-auto h-5 w-5 text-muted-foreground hover:text-destructive"
                  title={`Remove annotation ${annotation.name}`}
                  onClick={() => onExec(`${unannotateCommand} ${annotation.name}`)}
                >
                  <Trash2 className="h-3 w-3" />
                  <span className="sr-only">Remove annotation {annotation.name}</span>
                </Button>
              )}
            </div>
          ))}
        </div>
      )}
      {canEdit && (
        <form className="space-y-1.5" onSubmit={addAnnotation}>
          <div className="flex gap-1.5">
            <Input
              value={name}
              onChange={e => setName(e.target.value)}
              placeholder="Annotation"
              className="h-7 px-2 text-[11px] font-mono"
            />
            <CompactIconButton label="Add annotation" disabled={!name.trim()} />
          </div>
          <div className="flex gap-1.5">
            <Input
              value={argName}
              onChange={e => setArgName(e.target.value)}
              placeholder="key"
              className="h-7 px-2 text-[11px] font-mono"
            />
            <Input
              value={argValue}
              onChange={e => setArgValue(e.target.value)}
              placeholder="value"
              className="h-7 px-2 text-[11px] font-mono"
            />
          </div>
        </form>
      )}
    </div>
  )
}

function generateCommentOccurrence(comments: GenerateCommentDef[], index: number): number {
  const target = comments[index]
  let occurrence = 0
  for (let i = 0; i <= index; i += 1) {
    const comment = comments[i]
    if (comment.instance === target.instance && comment.text === target.text) occurrence += 1
  }
  return occurrence
}

function generateMetadataOccurrence(metadataItems: GenerateMetadataDef[], index: number): number {
  const target = metadataItems[index]
  let occurrence = 0
  for (let i = 0; i <= index; i += 1) {
    const metadata = metadataItems[i]
    if (
      metadata.scope === target.scope &&
      metadata.node === target.node &&
      metadata.property === target.property &&
      metadata.value === target.value
    ) {
      occurrence += 1
    }
  }
  return occurrence
}

function GeneratePanel({
  generate,
  nodes,
  nodeTypes,
  declaredTypes,
  onExec,
  onSourceRangeFocus,
}: {
  generate: GenerateBlockDef
  nodes: NodeInst[]
  nodeTypes: NodeTypeDef[]
  declaredTypes: DeclaredTypeDef[]
  onExec: (cmd: string) => Promise<void> | void
  onSourceRangeFocus?: (range: SourceRange, sourceFile?: string) => Promise<void> | void
}) {
  const hasItems = generate.comments.length > 0 || generate.metadata.length > 0
  if (!hasItems) return null

  return (
    <div className="space-y-1.5">
      {generate.comments.map((comment, index) => {
        const occurrence = generateCommentOccurrence(generate.comments, index)
        const commandTarget = `generate-comment ${comment.instance} ${quoteCommandArg(comment.text)} #${occurrence}`
        const commentNodeDef = nodes.find(node => node.instance === comment.instance)
        const commentNodeTypeDef = nodeTypes.find(typeDef => typeDef.type_name === commentNodeDef?.type)
        return (
          <div
            key={`${comment.instance}-${comment.text}-${index}`}
            data-generate-comment-index={index}
            className="space-y-1 rounded-md border border-border/40 p-1.5"
          >
            <div className="flex min-w-0 items-center gap-1.5">
              <Badge variant="outline" className="text-[8px] px-1 py-0 h-3.5 border-border/50 text-muted-foreground/60">
                comment
              </Badge>
              <span className="min-w-0 truncate font-mono text-[10px] text-muted-foreground/70">
                {comment.instance}
              </span>
              <SourceRangeButton
                range={comment.instance_source_range}
                label={`Locate source for generate comment instance ${comment.instance}`}
                sourceKey={`generate-comment-${index}-instance`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={commentNodeDef?.instance_source_range ?? commentNodeDef?.source_range}
                label={`Locate definition for generate comment instance ${comment.instance}`}
                sourceKey={`generate-comment-${index}-instance-def`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={commentNodeTypeDef?.name_source_range ?? commentNodeTypeDef?.source_range}
                sourceFile={commentNodeTypeDef?.source_file}
                label={`Locate type definition for generate comment instance ${comment.instance}`}
                sourceKey={`generate-comment-${index}-instance-type-def`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <span className="font-mono text-[10px] text-muted-foreground/40">=</span>
              <span className="min-w-0 flex-1 truncate font-mono text-[10px] text-muted-foreground/70">
                {comment.text}
              </span>
              <SourceRangeButton
                range={comment.text_source_range}
                label={`Locate source for generate comment text ${comment.instance}`}
                sourceKey={`generate-comment-${index}-text`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={comment.source_range}
                label={`Locate source for generate comment ${comment.instance}`}
                sourceKey={`generate-comment-${index}`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <MoveIconButton
                label={`Move generate comment ${comment.instance} up`}
                direction="up"
                disabled={index === 0}
                onClick={() => onExec(`move_comment ${comment.instance} ${quoteCommandArg(comment.text)} #${occurrence} up`)}
              />
              <MoveIconButton
                label={`Move generate comment ${comment.instance} down`}
                direction="down"
                disabled={index + 1 >= generate.comments.length}
                onClick={() => onExec(`move_comment ${comment.instance} ${quoteCommandArg(comment.text)} #${occurrence} down`)}
              />
              <DeleteIconButton
                label={`Remove generate comment ${comment.instance}`}
                onClick={() => onExec(`remove_comment ${comment.instance} ${quoteCommandArg(comment.text)} #${occurrence}`)}
              />
            </div>
            <AnnotationPanel
              annotations={comment.annotations}
              annotateCommand={`annotate ${commandTarget}`}
              unannotateCommand={`unannotate ${commandTarget}`}
              onExec={onExec}
              onSourceRangeFocus={onSourceRangeFocus}
              sourceKeyPrefix={`generate-comment-${index}`}
              declaredTypes={declaredTypes}
            />
            <RenameForm
              label={`Rename generate comment ${comment.instance}`}
              placeholder="new comment text"
              currentName={comment.text}
              onRename={text => onExec(
                `rename_comment ${comment.instance} ${quoteCommandArg(comment.text)} #${occurrence} ${quoteCommandArg(text)}`,
              )}
            />
          </div>
        )
      })}
      {generate.metadata.map((metadata, index) => {
        const occurrence = generateMetadataOccurrence(generate.metadata, index)
        const ref = `${metadata.scope}:${metadata.node}.${metadata.property}`
        const commandTarget = `generate-meta ${ref} ${quoteCommandArg(metadata.value)} #${occurrence}`
        const metadataConstructorTypeDef = declaredTypes.find(typeDef => typeDef.name === constructorTypeName(metadata.value))
        const metadataNodeDef = nodes.find(node => node.instance === metadata.node)
        const metadataNodeTypeDef = nodeTypes.find(typeDef => typeDef.type_name === metadataNodeDef?.type)
        const metadataPropertyPinDef = metadataNodeTypeDef?.pins.find(pin => pin.name === metadata.property)
        const metadataPropertyPinTypeDef = declaredTypes.find(typeDef => typeDef.name === metadataPropertyPinDef?.type)
        return (
          <div
            key={`${metadata.scope}-${metadata.node}-${metadata.property}-${metadata.value}-${index}`}
            data-generate-metadata-index={index}
            className="space-y-1 rounded-md border border-border/40 p-1.5"
          >
            <div className="flex min-w-0 items-center gap-1.5">
              <Badge variant="outline" className="text-[8px] px-1 py-0 h-3.5 border-border/50 text-muted-foreground/60">
                meta
              </Badge>
              <span className="min-w-0 truncate font-mono text-[10px] text-muted-foreground/70">
                {ref}
              </span>
              <SourceRangeButton
                range={metadata.scope_source_range}
                label={`Locate source for generate metadata scope ${ref}`}
                sourceKey={`generate-metadata-${index}-scope`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={metadata.node_source_range}
                label={`Locate source for generate metadata node ${ref}`}
                sourceKey={`generate-metadata-${index}-node`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={metadataNodeDef?.instance_source_range ?? metadataNodeDef?.source_range}
                label={`Locate definition for generate metadata node ${ref}`}
                sourceKey={`generate-metadata-${index}-node-def`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={metadataNodeTypeDef?.name_source_range ?? metadataNodeTypeDef?.source_range}
                sourceFile={metadataNodeTypeDef?.source_file}
                label={`Locate type definition for generate metadata node ${ref}`}
                sourceKey={`generate-metadata-${index}-node-type-def`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={metadata.property_source_range}
                label={`Locate source for generate metadata property ${ref}`}
                sourceKey={`generate-metadata-${index}-property`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={metadataPropertyPinDef?.name_source_range ?? metadataPropertyPinDef?.source_range}
                sourceFile={metadataPropertyPinDef?.source_file}
                label={`Locate definition for generate metadata property ${ref}`}
                sourceKey={`generate-metadata-${index}-property-def`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={metadataPropertyPinTypeDef?.name_source_range ?? metadataPropertyPinTypeDef?.source_range}
                sourceFile={metadataPropertyPinTypeDef?.source_file}
                label={`Locate type definition for generate metadata property ${ref}`}
                sourceKey={`generate-metadata-${index}-property-type-def`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <span className="font-mono text-[10px] text-muted-foreground/40">=</span>
              <span className="min-w-0 flex-1 truncate font-mono text-[10px] text-muted-foreground/70">
                {metadata.value}
              </span>
              <SourceRangeButton
                range={metadata.value_source_range}
                label={`Locate source for generate metadata value ${ref}`}
                sourceKey={`generate-metadata-${index}-value`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={metadata.value_constructor_source_range}
                label={`Locate source for generate metadata constructor value ${ref}`}
                sourceKey={`generate-metadata-${index}-constructor`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={metadata.value_constructor_type_source_range}
                label={`Locate source for generate metadata constructor type ${ref}`}
                sourceKey={`generate-metadata-${index}-constructor-type`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={metadataConstructorTypeDef?.name_source_range ?? metadataConstructorTypeDef?.source_range}
                sourceFile={metadataConstructorTypeDef?.source_file}
                label={`Locate source for generate metadata constructor type definition ${ref}`}
                sourceKey={`generate-metadata-${index}-constructor-type-def`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={metadata.value_constructor_arg_source_range}
                label={`Locate source for generate metadata constructor argument ${ref}`}
                sourceKey={`generate-metadata-${index}-constructor-arg`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={metadata.source_range}
                label={`Locate source for generate metadata ${ref}`}
                sourceKey={`generate-metadata-${index}`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <MoveIconButton
                label={`Move generate metadata ${ref} up`}
                direction="up"
                disabled={index === 0}
                onClick={() => onExec(`move_meta ${ref} ${quoteCommandArg(metadata.value)} #${occurrence} up`)}
              />
              <MoveIconButton
                label={`Move generate metadata ${ref} down`}
                direction="down"
                disabled={index + 1 >= generate.metadata.length}
                onClick={() => onExec(`move_meta ${ref} ${quoteCommandArg(metadata.value)} #${occurrence} down`)}
              />
              <DeleteIconButton
                label={`Remove generate metadata ${ref}`}
                onClick={() => onExec(`remove_meta ${ref} ${quoteCommandArg(metadata.value)} #${occurrence}`)}
              />
            </div>
            <AnnotationPanel
              annotations={metadata.annotations}
              annotateCommand={`annotate ${commandTarget}`}
              unannotateCommand={`unannotate ${commandTarget}`}
              onExec={onExec}
              onSourceRangeFocus={onSourceRangeFocus}
              sourceKeyPrefix={`generate-metadata-${index}`}
              declaredTypes={declaredTypes}
            />
            <RenameForm
              label={`Rename generate metadata ${ref}`}
              placeholder="new metadata value"
              currentName={metadata.value}
              onRename={value => onExec(
                `rename_meta ${ref} ${quoteCommandArg(metadata.value)} #${occurrence} ${quoteCommandArg(value)}`,
              )}
            />
            <RenameForm
              label={`Rename generate metadata reference ${ref}`}
              placeholder="scope:node.prop"
              currentName={ref}
              onRename={nextRef => onExec(
                `rename_meta_ref ${ref} ${quoteCommandArg(metadata.value)} #${occurrence} ${nextRef}`,
              )}
            />
          </div>
        )
      })}
    </div>
  )
}

function parameterDiagnostics(graph: GraphDef, parameter: GraphParam, diagnostics: Diagnostic[]): Diagnostic[] {
  return diagnostics.filter(diagnostic =>
    diagnostic.target?.graph === graph.name &&
    diagnostic.target?.parameter_name === parameter.name)
}

function diagnosticSeverity(diagnostics: Diagnostic[]): Diagnostic['severity'] | null {
  if (diagnostics.some(diagnostic => diagnostic.severity === 'error')) return 'error'
  if (diagnostics.some(diagnostic => diagnostic.severity === 'warning')) return 'warning'
  return null
}

function diagnosticBorderColor(severity: Diagnostic['severity'] | null): string | undefined {
  if (severity === 'error') return 'var(--color-destructive)'
  if (severity === 'warning') return 'var(--color-warning)'
  return undefined
}

function connectionEndpoint(node: string, pin: string): string {
  if (!node) return pin || 'context'
  return pin ? `${node}.${pin}` : node
}

function nodeTypeForInstance(graph: GraphDef, instance: string): string {
  return graph.nodes.find(node => node.instance === instance)?.type ?? ''
}

function pinDefinitionForEndpoint(
  graph: GraphDef,
  nodeTypes: NodeTypeDef[],
  nodeInstance: string,
  pinName: string,
): PinDef | undefined {
  if (!nodeInstance || !pinName) return undefined
  const nodeType = nodeTypeForInstance(graph, nodeInstance)
  if (!nodeType) return undefined
  return nodeTypes.find(typeDef => typeDef.type_name === nodeType)
    ?.pins.find(pin => pin.name === pinName)
}

function nodeDefinitionForEndpoint(graph: GraphDef, nodeInstance: string): NodeInst | undefined {
  if (!nodeInstance) return undefined
  return graph.nodes.find(node => node.instance === nodeInstance)
}

function parameterDefinitionForBareSource(graph: GraphDef, sourceNode: string, sourcePin: string): GraphParam | undefined {
  if (!sourceNode || sourcePin) return undefined
  return graph.parameters.find(parameter => parameter.name === sourceNode)
}

function selectedConnectionByMetadata(
  selectedEdge: EdgeEditPayload | null | undefined,
  kind: 'exec' | 'data',
  block: LogicBlock,
  id: string | undefined,
  persistentId: string | undefined,
  sourceNode: string,
  sourcePin: string,
  targetNode: string,
  targetPin: string,
): boolean {
  if (!selectedEdge) return false
  if (selectedEdge.kind !== kind) return false
  if (selectedEdge.blockKind !== block.kind || selectedEdge.blockName !== block.name) return false
  if (selectedEdge.id && id && selectedEdge.id === id) return true
  if (selectedEdge.persistentId && persistentId) return selectedEdge.persistentId === persistentId
  return selectedEdge.sourceNode === sourceNode &&
    selectedEdge.sourcePin === sourcePin &&
    selectedEdge.targetNode === targetNode &&
    selectedEdge.targetPin === targetPin
}

function sourceRangeData(range: SourceRange | undefined): string | undefined {
  if (!range) return undefined
  return `${range.start.line}:${range.start.column}-${range.end.line}:${range.end.column}`
}

function endpointAddress(endpoint: string): { node: string; pin: string } | null {
  const [node, pin, ...rest] = endpoint.split('.')
  if (!node || !pin || rest.length > 0) return null
  return { node, pin }
}

function dataSourceAddress(endpoint: string): { node: string; pin: string } | null {
  const [node, pin, ...rest] = endpoint.split('.')
  if (!node || rest.length > 0) return null
  return { node, pin: pin ?? '' }
}

async function execInLogicBlock(
  block: LogicBlock,
  command: string | string[],
  onExec: (cmd: string) => Promise<void> | void,
) {
  await onExec(block.kind === 'event' ? `event ${block.name}` : `fn ${block.name}`)
  const commands = Array.isArray(command) ? command : [command]
  for (const nextCommand of commands) {
    await onExec(nextCommand)
  }
}

function LogicBlockConnections({
  block,
  graph,
  nodeTypes,
  declaredTypes,
  selectedEdge,
  onEdgeRedirected,
  onExec,
  onSourceRangeFocus,
}: {
  block: LogicBlock
  graph: GraphDef
  nodeTypes: NodeTypeDef[]
  declaredTypes: DeclaredTypeDef[]
  selectedEdge?: EdgeEditPayload | null
  onEdgeRedirected?: (edge: EdgeEditPayload) => void
  onExec: (cmd: string) => Promise<void> | void
  onSourceRangeFocus?: (range: SourceRange, sourceFile?: string) => Promise<void> | void
}) {
  if (block.flows.length === 0 && block.links.length === 0) return null

  return (
    <div className="ml-5 space-y-0.5">
      {block.flows.map(flow => {
        const fromLabel = connectionEndpoint(flow.from_node, flow.from_pin)
        const toLabel = connectionEndpoint(flow.to_node, flow.to_pin)
        const label = `${fromLabel} -> ${toLabel}`
        const sourceKey = `flow-${block.kind}-${block.name}-${flow.from_node}-${flow.from_pin}-${flow.to_node}-${flow.to_pin}`
        const fromNodeDef = nodeDefinitionForEndpoint(graph, flow.from_node)
        const toNodeDef = nodeDefinitionForEndpoint(graph, flow.to_node)
        const fromNodeTypeDef = nodeTypes.find(typeDef => typeDef.type_name === fromNodeDef?.type)
        const toNodeTypeDef = nodeTypes.find(typeDef => typeDef.type_name === toNodeDef?.type)
        const fromPinDef = pinDefinitionForEndpoint(graph, nodeTypes, flow.from_node, flow.from_pin)
        const toPinDef = pinDefinitionForEndpoint(graph, nodeTypes, flow.to_node, flow.to_pin)
        const isSelected = selectedConnectionByMetadata(
          selectedEdge,
          'exec',
          block,
          flow.id,
          flow.persistent_id,
          flow.from_node,
          flow.from_pin,
          flow.to_node,
          flow.to_pin,
        )
        return (
          <div
            key={flow.id ?? sourceKey}
            data-logic-flow-source={label}
            data-logic-connection-id={flow.id}
            data-logic-connection-persistent-id={flow.persistent_id}
            data-logic-connection-selected={isSelected ? 'true' : undefined}
            data-selected-edge-id={isSelected ? selectedEdge?.id : undefined}
            data-selected-edge-persistent-id={isSelected ? selectedEdge?.persistentId : undefined}
            data-selected-edge-source-range={isSelected ? sourceRangeData(selectedEdge?.sourceRange) : undefined}
            data-selected-edge-source-endpoint-range={isSelected ? sourceRangeData(selectedEdge?.sourceEndpointRange) : undefined}
            data-selected-edge-target-endpoint-range={isSelected ? sourceRangeData(selectedEdge?.targetEndpointRange) : undefined}
            className={`space-y-1 rounded border px-1.5 py-1 ${
              isSelected ? 'border-primary/70 bg-primary/10 shadow-sm' : 'border-border/30'
            }`}
          >
            <div className="flex min-w-0 items-center gap-1">
              <Badge variant="outline" className="h-3.5 px-1 py-0 text-[8px] border-border/50 text-muted-foreground/60">
                flow
              </Badge>
              <span className="min-w-0 flex-1 truncate font-mono text-[10px] text-muted-foreground/70">
                {fromLabel}
              </span>
              <SourceRangeButton
                range={flow.from_node_source_range}
                label={`Locate source for flow source node ${flow.from_node}`}
                sourceKey={`${sourceKey}-from-node`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={fromNodeDef?.instance_source_range ?? fromNodeDef?.source_range}
                label={`Locate definition for flow source node ${flow.from_node}`}
                sourceKey={`flow-from-node-def-${block.kind}-${block.name}-${flow.from_node}`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={fromNodeTypeDef?.name_source_range ?? fromNodeTypeDef?.source_range}
                sourceFile={fromNodeTypeDef?.source_file}
                label={`Locate type definition for flow source node ${flow.from_node}`}
                sourceKey={`flow-from-node-type-def-${block.kind}-${block.name}-${flow.from_node}`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={flow.from_pin_source_range}
                label={`Locate source for flow source pin ${flow.from_pin}`}
                sourceKey={`${sourceKey}-from-pin`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={fromPinDef?.name_source_range ?? fromPinDef?.source_range}
                sourceFile={fromPinDef?.source_file}
                label={`Locate definition for flow source pin ${flow.from_pin}`}
                sourceKey={`flow-from-pin-def-${block.kind}-${block.name}-${flow.from_node}-${flow.from_pin}`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={flow.from_endpoint_source_range}
                label={`Locate source for flow source ${fromLabel}`}
                sourceKey={`${sourceKey}-from-endpoint`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <span className="font-mono text-[10px] text-muted-foreground/40">-&gt;</span>
              <span className="min-w-0 flex-1 truncate font-mono text-[10px] text-muted-foreground/70">
                {toLabel}
              </span>
              <SourceRangeButton
                range={flow.to_node_source_range}
                label={`Locate source for flow target node ${flow.to_node}`}
                sourceKey={`${sourceKey}-to-node`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={toNodeDef?.instance_source_range ?? toNodeDef?.source_range}
                label={`Locate definition for flow target node ${flow.to_node}`}
                sourceKey={`flow-to-node-def-${block.kind}-${block.name}-${flow.to_node}`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={toNodeTypeDef?.name_source_range ?? toNodeTypeDef?.source_range}
                sourceFile={toNodeTypeDef?.source_file}
                label={`Locate type definition for flow target node ${flow.to_node}`}
                sourceKey={`flow-to-node-type-def-${block.kind}-${block.name}-${flow.to_node}`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={flow.to_pin_source_range}
                label={`Locate source for flow target pin ${flow.to_pin}`}
                sourceKey={`${sourceKey}-to-pin`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={toPinDef?.name_source_range ?? toPinDef?.source_range}
                sourceFile={toPinDef?.source_file}
                label={`Locate definition for flow target pin ${flow.to_pin}`}
                sourceKey={`flow-to-pin-def-${block.kind}-${block.name}-${flow.to_node}-${flow.to_pin}`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={flow.to_endpoint_source_range}
                label={`Locate source for flow target ${toLabel}`}
                sourceKey={`${sourceKey}-to-endpoint`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={flow.source_range}
                label={`Locate source for flow ${label}`}
                sourceKey={sourceKey}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <DeleteIconButton
                label={`Remove flow ${label}`}
                dataAction="delete-flow"
                onClick={() => void execInLogicBlock(
                  block,
                  `unflow ${fromLabel} ${toLabel}`,
                  onExec,
                )}
              />
            </div>
            <AnnotationPanel
              annotations={flow.annotations}
              annotateCommand={`annotate flow ${block.kind} ${block.name} ${fromLabel} ${toLabel}`}
              unannotateCommand={`unannotate flow ${block.kind} ${block.name} ${fromLabel} ${toLabel}`}
              onExec={onExec}
              onSourceRangeFocus={onSourceRangeFocus}
              sourceKeyPrefix={sourceKey}
              declaredTypes={declaredTypes}
            />
            <div className="grid grid-cols-1 gap-1.5 sm:grid-cols-2">
              <EndpointRedirectForm
                label={`Redirect flow source ${label}`}
                placeholder={fromLabel}
                currentEndpoint={fromLabel}
                dataAction="redirect-flow-source"
                onRedirect={async nextEndpoint => {
                  await execInLogicBlock(
                    block,
                    [
                      `unflow ${fromLabel} ${toLabel}`,
                      `flow ${nextEndpoint} ${toLabel}`,
                      ...annotationMigrationCommands(
                        `annotate flow ${block.kind} ${block.name} ${nextEndpoint} ${toLabel}`,
                        flow.annotations,
                      ),
                    ],
                    onExec,
                  )
                  const nextSource = endpointAddress(nextEndpoint)
                  if (!nextSource) return
                  onEdgeRedirected?.({
                    kind: 'exec',
                    blockKind: block.kind,
                    blockName: block.name,
                    sourceNode: nextSource.node,
                    sourcePin: nextSource.pin,
                    targetNode: flow.to_node,
                    targetPin: flow.to_pin,
                    persistentId: flow.persistent_id,
                    annotations: flow.annotations,
                  })
                }}
              />
              <EndpointRedirectForm
                label={`Redirect flow target ${label}`}
                placeholder={toLabel}
                currentEndpoint={toLabel}
                dataAction="redirect-flow-target"
                onRedirect={async nextEndpoint => {
                  await execInLogicBlock(
                    block,
                    [
                      `unflow ${fromLabel} ${toLabel}`,
                      `flow ${fromLabel} ${nextEndpoint}`,
                      ...annotationMigrationCommands(
                        `annotate flow ${block.kind} ${block.name} ${fromLabel} ${nextEndpoint}`,
                        flow.annotations,
                      ),
                    ],
                    onExec,
                  )
                  const nextTarget = endpointAddress(nextEndpoint)
                  if (!nextTarget) return
                  onEdgeRedirected?.({
                    kind: 'exec',
                    blockKind: block.kind,
                    blockName: block.name,
                    sourceNode: flow.from_node,
                    sourcePin: flow.from_pin,
                    targetNode: nextTarget.node,
                    targetPin: nextTarget.pin,
                    persistentId: flow.persistent_id,
                    annotations: flow.annotations,
                  })
                }}
              />
            </div>
          </div>
        )
      })}
      {block.links.map(link => {
        const sourceLabel = connectionEndpoint(link.source_node, link.source_pin)
        const targetLabel = connectionEndpoint(link.target_node, link.target_pin)
        const label = `${sourceLabel} -> ${targetLabel}`
        const sourceKey = `link-${block.kind}-${block.name}-${link.source_node}-${link.source_pin}-${link.target_node}-${link.target_pin}`
        const sourceNodeDef = link.source_pin ? nodeDefinitionForEndpoint(graph, link.source_node) : undefined
        const targetNodeDef = nodeDefinitionForEndpoint(graph, link.target_node)
        const sourceNodeTypeDef = nodeTypes.find(typeDef => typeDef.type_name === sourceNodeDef?.type)
        const targetNodeTypeDef = nodeTypes.find(typeDef => typeDef.type_name === targetNodeDef?.type)
        const sourcePinDef = pinDefinitionForEndpoint(graph, nodeTypes, link.source_node, link.source_pin)
        const targetPinDef = pinDefinitionForEndpoint(graph, nodeTypes, link.target_node, link.target_pin)
        const sourcePinTypeDef = declaredTypes.find(typeDef => typeDef.name === sourcePinDef?.type)
        const targetPinTypeDef = declaredTypes.find(typeDef => typeDef.name === targetPinDef?.type)
        const sourceParameterDef = parameterDefinitionForBareSource(graph, link.source_node, link.source_pin)
        const sourceParameterTypeDef = declaredTypes.find(typeDef => typeDef.name === sourceParameterDef?.type)
        const isSelected = selectedConnectionByMetadata(
          selectedEdge,
          'data',
          block,
          link.id,
          link.persistent_id,
          link.source_node,
          link.source_pin,
          link.target_node,
          link.target_pin,
        )
        return (
          <div
            key={link.id ?? sourceKey}
            data-logic-link-source={label}
            data-logic-connection-id={link.id}
            data-logic-connection-persistent-id={link.persistent_id}
            data-logic-connection-selected={isSelected ? 'true' : undefined}
            data-selected-edge-id={isSelected ? selectedEdge?.id : undefined}
            data-selected-edge-persistent-id={isSelected ? selectedEdge?.persistentId : undefined}
            data-selected-edge-source-range={isSelected ? sourceRangeData(selectedEdge?.sourceRange) : undefined}
            data-selected-edge-source-endpoint-range={isSelected ? sourceRangeData(selectedEdge?.sourceEndpointRange) : undefined}
            data-selected-edge-target-endpoint-range={isSelected ? sourceRangeData(selectedEdge?.targetEndpointRange) : undefined}
            className={`space-y-1 rounded border px-1.5 py-1 ${
              isSelected ? 'border-primary/70 bg-primary/10 shadow-sm' : 'border-border/30'
            }`}
          >
            <div className="flex min-w-0 items-center gap-1">
              <Badge variant="outline" className="h-3.5 px-1 py-0 text-[8px] border-border/50 text-muted-foreground/60">
                link
              </Badge>
              <span className="min-w-0 flex-1 truncate font-mono text-[10px] text-muted-foreground/70">
                {sourceLabel}
              </span>
              <SourceRangeButton
                range={link.source_node_source_range}
                label={`Locate source for link source ${link.source_node || link.source_pin}`}
                sourceKey={`${sourceKey}-source-node`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={sourceNodeDef?.instance_source_range ?? sourceNodeDef?.source_range}
                label={`Locate definition for link source node ${link.source_node}`}
                sourceKey={`${sourceKey}-source-node-def`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={sourceNodeTypeDef?.name_source_range ?? sourceNodeTypeDef?.source_range}
                sourceFile={sourceNodeTypeDef?.source_file}
                label={`Locate type definition for link source node ${link.source_node}`}
                sourceKey={`${sourceKey}-source-node-type-def`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={sourceParameterDef?.name_source_range ?? sourceParameterDef?.source_range}
                label={`Locate definition for link source parameter ${link.source_node}`}
                sourceKey={`${sourceKey}-source-param-def`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={sourceParameterTypeDef?.name_source_range ?? sourceParameterTypeDef?.source_range}
                sourceFile={sourceParameterTypeDef?.source_file}
                label={`Locate type definition for link source parameter ${link.source_node}`}
                sourceKey={`${sourceKey}-source-param-type-def`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              {link.source_pin && (
                <>
                  <SourceRangeButton
                    range={link.source_pin_source_range}
                    label={`Locate source for link source pin ${link.source_pin}`}
                    sourceKey={`${sourceKey}-source-pin`}
                    onSourceRangeFocus={onSourceRangeFocus}
                  />
                  <SourceRangeButton
                    range={sourcePinDef?.name_source_range ?? sourcePinDef?.source_range}
                    sourceFile={sourcePinDef?.source_file}
                    label={`Locate definition for link source pin ${link.source_pin}`}
                    sourceKey={`${sourceKey}-source-pin-def`}
                    onSourceRangeFocus={onSourceRangeFocus}
                  />
                  <SourceRangeButton
                    range={sourcePinTypeDef?.name_source_range ?? sourcePinTypeDef?.source_range}
                    sourceFile={sourcePinTypeDef?.source_file}
                    label={`Locate definition for link source pin ${link.source_pin} type`}
                    sourceKey={`${sourceKey}-source-pin-type-def`}
                    onSourceRangeFocus={onSourceRangeFocus}
                  />
                </>
              )}
              <SourceRangeButton
                range={link.source_endpoint_source_range}
                label={`Locate source for link source ${sourceLabel}`}
                sourceKey={`${sourceKey}-source-endpoint`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <span className="font-mono text-[10px] text-muted-foreground/40">-&gt;</span>
              <span className="min-w-0 flex-1 truncate font-mono text-[10px] text-muted-foreground/70">
                {targetLabel}
              </span>
              <SourceRangeButton
                range={link.target_node_source_range}
                label={`Locate source for link target node ${link.target_node}`}
                sourceKey={`${sourceKey}-target-node`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={targetNodeDef?.instance_source_range ?? targetNodeDef?.source_range}
                label={`Locate definition for link target node ${link.target_node}`}
                sourceKey={`${sourceKey}-target-node-def`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={targetNodeTypeDef?.name_source_range ?? targetNodeTypeDef?.source_range}
                sourceFile={targetNodeTypeDef?.source_file}
                label={`Locate type definition for link target node ${link.target_node}`}
                sourceKey={`${sourceKey}-target-node-type-def`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={link.target_pin_source_range}
                label={`Locate source for link target pin ${link.target_pin}`}
                sourceKey={`${sourceKey}-target-pin`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={targetPinDef?.name_source_range ?? targetPinDef?.source_range}
                sourceFile={targetPinDef?.source_file}
                label={`Locate definition for link target pin ${link.target_pin}`}
                sourceKey={`${sourceKey}-target-pin-def`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={targetPinTypeDef?.name_source_range ?? targetPinTypeDef?.source_range}
                sourceFile={targetPinTypeDef?.source_file}
                label={`Locate definition for link target pin ${link.target_pin} type`}
                sourceKey={`${sourceKey}-target-pin-type-def`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={link.target_endpoint_source_range}
                label={`Locate source for link target ${targetLabel}`}
                sourceKey={`${sourceKey}-target-endpoint`}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <SourceRangeButton
                range={link.source_range}
                label={`Locate source for link ${label}`}
                sourceKey={sourceKey}
                onSourceRangeFocus={onSourceRangeFocus}
              />
              <DeleteIconButton
                label={`Remove link ${label}`}
                dataAction="delete-link"
                onClick={() => void execInLogicBlock(
                  block,
                  `unlink ${targetLabel}`,
                  onExec,
                )}
              />
            </div>
            <AnnotationPanel
              annotations={link.annotations}
              annotateCommand={`annotate link ${block.kind} ${block.name} ${targetLabel} ${sourceLabel}`}
              unannotateCommand={`unannotate link ${block.kind} ${block.name} ${targetLabel} ${sourceLabel}`}
              onExec={onExec}
              onSourceRangeFocus={onSourceRangeFocus}
              sourceKeyPrefix={sourceKey}
              declaredTypes={declaredTypes}
            />
            <div className="grid grid-cols-1 gap-1.5 sm:grid-cols-2">
              <EndpointRedirectForm
                label={`Redirect link source ${label}`}
                placeholder={sourceLabel}
                currentEndpoint={sourceLabel}
                dataAction="redirect-link-source"
                onRedirect={async nextEndpoint => {
                  await execInLogicBlock(
                    block,
                    [
                      `unlink ${targetLabel}`,
                      `link ${targetLabel} ${nextEndpoint}`,
                      ...annotationMigrationCommands(
                        `annotate link ${block.kind} ${block.name} ${targetLabel} ${nextEndpoint}`,
                        link.annotations,
                      ),
                    ],
                    onExec,
                  )
                  const nextSource = dataSourceAddress(nextEndpoint)
                  if (!nextSource) return
                  onEdgeRedirected?.({
                    kind: 'data',
                    blockKind: block.kind,
                    blockName: block.name,
                    sourceNode: nextSource.node,
                    sourcePin: nextSource.pin,
                    targetNode: link.target_node,
                    targetPin: link.target_pin,
                    persistentId: link.persistent_id,
                    annotations: link.annotations,
                  })
                }}
              />
              <EndpointRedirectForm
                label={`Redirect link target ${label}`}
                placeholder={targetLabel}
                currentEndpoint={targetLabel}
                dataAction="redirect-link-target"
                onRedirect={async nextEndpoint => {
                  await execInLogicBlock(
                    block,
                    [
                      `unlink ${targetLabel}`,
                      `link ${nextEndpoint} ${sourceLabel}`,
                      ...annotationMigrationCommands(
                        `annotate link ${block.kind} ${block.name} ${nextEndpoint} ${sourceLabel}`,
                        link.annotations,
                      ),
                    ],
                    onExec,
                  )
                  const nextTarget = endpointAddress(nextEndpoint)
                  if (!nextTarget) return
                  onEdgeRedirected?.({
                    kind: 'data',
                    blockKind: block.kind,
                    blockName: block.name,
                    sourceNode: link.source_node,
                    sourcePin: link.source_pin,
                    targetNode: nextTarget.node,
                    targetPin: nextTarget.pin,
                    persistentId: link.persistent_id,
                    annotations: link.annotations,
                  })
                }}
              />
            </div>
          </div>
        )
      })}
    </div>
  )
}

function GraphInfo({
  graph,
  imports,
  lets,
  nodeTypes,
  declarationNodeTypes,
  declaredTypes,
  schemas,
  selectedEdge,
  diagnostics,
  focusedDiagnostic,
  onSourceRangeFocus,
  onEdgeRedirected,
  onExec,
}: {
  graph: GraphDef
  imports: ImportDef[]
  lets: LetDef[]
  nodeTypes: NodeTypeDef[]
  declarationNodeTypes: NodeTypeDef[]
  declaredTypes: DeclaredTypeDef[]
  schemas: SchemaDef[]
  selectedEdge?: EdgeEditPayload | null
  diagnostics: Diagnostic[]
  focusedDiagnostic: Diagnostic | null
  onSourceRangeFocus?: (range: SourceRange, sourceFile?: string) => Promise<void> | void
  onEdgeRedirected?: (edge: EdgeEditPayload) => void
  onExec: (cmd: string) => Promise<void> | void
}) {
  const [paramDirection, setParamDirection] = useState<'in' | 'out' | 'var'>('in')
  const [paramName, setParamName] = useState('')
  const [paramType, setParamType] = useState('FString')
  const [paramDefault, setParamDefault] = useState('')
  const [paramDefaultName, setParamDefaultName] = useState('')
  const [paramDefaultValue, setParamDefaultValue] = useState('')
  const [paramDefaultCtorName, setParamDefaultCtorName] = useState('')
  const [paramDefaultCtorType, setParamDefaultCtorType] = useState('')
  const [paramDefaultCtorArg, setParamDefaultCtorArg] = useState('')
  const [paramDefaultCtorArgName, setParamDefaultCtorArgName] = useState('')
  const [paramDefaultCtorArgValue, setParamDefaultCtorArgValue] = useState('')
  const [paramDefaultCtorTypeName, setParamDefaultCtorTypeName] = useState('')
  const [paramDefaultCtorTypeValue, setParamDefaultCtorTypeValue] = useState('')
  const [eventName, setEventName] = useState('')
  const [functionName, setFunctionName] = useState('')

  const addParam = async (e: FormEvent) => {
    e.preventDefault()
    const name = paramName.trim()
    const type = paramType.trim()
    if (!name || !type) return
    const fallback = paramDefault.trim()
    await onExec(`param ${paramDirection} ${name} ${type}${fallback ? ` = ${quoteCommandArg(fallback)}` : ''}`)
    setParamName('')
    setParamDefault('')
  }

  const setParamDefaultValueForGraph = async (e: FormEvent) => {
    e.preventDefault()
    const name = paramDefaultName.trim()
    const value = paramDefaultValue.trim()
    if (!name) return
    await onExec(`set_param_default ${name}${value ? ` ${quoteCommandArg(value)}` : ''}`)
    setParamDefaultName('')
    setParamDefaultValue('')
  }

  const setParamDefaultConstructorForGraph = async (e: FormEvent) => {
    e.preventDefault()
    const name = paramDefaultCtorName.trim()
    const typeName = paramDefaultCtorType.trim()
    const argument = paramDefaultCtorArg.trim()
    if (!name || !typeName) return
    await onExec(`set_param_default_ctor ${name} ${typeName}${argument ? ` ${quoteCommandArg(argument)}` : ''}`)
    setParamDefaultCtorName('')
    setParamDefaultCtorType('')
    setParamDefaultCtorArg('')
  }

  const setParamDefaultConstructorArgumentForGraph = async (e: FormEvent) => {
    e.preventDefault()
    const name = paramDefaultCtorArgName.trim()
    const argument = paramDefaultCtorArgValue.trim()
    if (!name) return
    await onExec(`set_param_default_ctor_arg ${name}${argument ? ` ${quoteCommandArg(argument)}` : ''}`)
    setParamDefaultCtorArgName('')
    setParamDefaultCtorArgValue('')
  }

  const setParamDefaultConstructorTypeForGraph = async (e: FormEvent) => {
    e.preventDefault()
    const name = paramDefaultCtorTypeName.trim()
    const typeName = paramDefaultCtorTypeValue.trim()
    if (!name || !typeName) return
    await onExec(`set_param_default_ctor_type ${name} ${typeName}`)
    setParamDefaultCtorTypeName('')
    setParamDefaultCtorTypeValue('')
  }

  const addEvent = async (e: FormEvent) => {
    e.preventDefault()
    const name = eventName.trim()
    if (!name) return
    await onExec(`event ${name}`)
    setEventName('')
  }

  const addFunction = async (e: FormEvent) => {
    e.preventDefault()
    const name = functionName.trim()
    if (!name) return
    await onExec(`fn ${name}`)
    setFunctionName('')
  }
  const baseSchema = graph.base_type ? schemas.find(schema => schema.name === graph.base_type) : undefined

  return (
    <div className="space-y-3 panel-enter">
      <div className="flex items-start gap-2">
        <div className="min-w-0 flex-1 space-y-0.5">
          <PropRow label="Name" value={graph.name} mono />
          {graph.base_type && <PropRow label="Base" value={graph.base_type} mono />}
          <PropRow label="Nodes" value={graph.nodes.length} />
          <PropRow label="Events" value={graph.events.length} />
          <PropRow label="Functions" value={graph.functions.length} />
        </div>
        <SourceRangeButton
          range={graph.source_range}
          label={`Locate source for graph ${graph.name}`}
          sourceKey={`graph-${graph.name}`}
          onSourceRangeFocus={onSourceRangeFocus}
        />
        <SourceRangeButton
          range={graph.name_source_range}
          label={`Locate source for graph name ${graph.name}`}
          sourceKey={`graph-name-${graph.name}`}
          onSourceRangeFocus={onSourceRangeFocus}
        />
        <SourceRangeButton
          range={graph.base_type_source_range}
          label={`Locate source for graph ${graph.name} base type`}
          sourceKey={`graph-base-type-${graph.name}`}
          onSourceRangeFocus={onSourceRangeFocus}
        />
        <SourceRangeButton
          range={baseSchema?.name_source_range ?? baseSchema?.source_range}
          sourceFile={baseSchema?.source_file}
          label={`Locate source for graph ${graph.name} base schema definition`}
          sourceKey={`graph-base-type-def-${graph.name}`}
          onSourceRangeFocus={onSourceRangeFocus}
        />
      </div>

      <Separator className="opacity-50" />
      <PanelSection title="Rename Graph">
        <RenameForm
          label="Rename graph"
          placeholder="new graph name"
          currentName={graph.name}
          onRename={name => onExec(`rename_graph ${graph.name} ${name}`)}
        />
      </PanelSection>

      <Separator className="opacity-50" />
      <PanelSection title="Graph Annotations">
        <AnnotationPanel
          annotations={graph.annotations}
          annotateCommand="annotate graph"
          unannotateCommand="unannotate graph"
          onExec={onExec}
          onSourceRangeFocus={onSourceRangeFocus}
          sourceKeyPrefix={`graph-${graph.name}`}
          declaredTypes={declaredTypes}
        />
      </PanelSection>

      {imports.length > 0 && (
        <>
          <Separator className="opacity-50" />
          <PanelSection title="Module Imports">
            <div className="space-y-1">
              {imports.map((importDef, index) => (
                <div
                  key={importDef.id ?? `${importDef.path}-${index}`}
                  data-module-import={importDef.path}
                  className="space-y-1 rounded border border-border/30 px-1.5 py-0.5"
                >
                  <div className="flex min-w-0 items-center gap-1">
                    <Badge variant="outline" className="h-3.5 px-1 py-0 text-[8px] border-border/50 text-muted-foreground/60">
                      import
                    </Badge>
                    <span className="min-w-0 flex-1 truncate font-mono text-[10px] text-muted-foreground/70">
                      {importDef.path}
                    </span>
                    <SourceRangeButton
                      range={importDef.path_source_range}
                      label={`Locate source for import path ${importDef.path}`}
                      sourceKey={`import-path-${index}`}
                      onSourceRangeFocus={onSourceRangeFocus}
                    />
                    <SourceRangeButton
                      range={importDef.source_range}
                      label={`Locate source for import ${importDef.path}`}
                      sourceKey={`import-${index}`}
                      onSourceRangeFocus={onSourceRangeFocus}
                    />
                    {importDef.loaded && (importDef.normalized_path || importDef.path) && (
                      <SourceRangeButton
                        range={declarationSourceOpenRange}
                        sourceFile={importDef.normalized_path ?? importDef.path}
                        label={`Open declaration source for import ${importDef.path}`}
                        sourceKey={`import-declaration-${index}`}
                        onSourceRangeFocus={onSourceRangeFocus}
                      />
                    )}
                  </div>
                  <AnnotationPanel
                    annotations={importDef.annotations}
                    annotateCommand={`annotate import ${quoteCommandArg(importDef.path)}`}
                    unannotateCommand={`unannotate import ${quoteCommandArg(importDef.path)}`}
                    onExec={onExec}
                    onSourceRangeFocus={onSourceRangeFocus}
                    sourceKeyPrefix={`import-${index}`}
                    declaredTypes={declaredTypes}
                  />
                </div>
              ))}
            </div>
          </PanelSection>
        </>
      )}

      {lets.length > 0 && (
        <>
          <Separator className="opacity-50" />
          <PanelSection title="Module Lets">
            <div className="space-y-1">
              {lets.map(letDecl => {
                const letTypeDef = declaredTypes.find(typeDef => typeDef.name === letDecl.type)
                return (
                  <div
                    key={letDecl.id ?? letDecl.name}
                    data-module-let={letDecl.name}
                    className="space-y-1 rounded border border-border/30 px-1.5 py-0.5"
                  >
                    <div className="flex min-w-0 items-center gap-1">
                      <Badge variant="outline" className="h-3.5 px-1 py-0 text-[8px] border-border/50 text-muted-foreground/60">
                        let
                      </Badge>
                      <span className="min-w-0 truncate font-mono text-[10px] text-muted-foreground/70">
                        {letDecl.name}
                      </span>
                      <SourceRangeButton
                        range={letDecl.name_source_range}
                        label={`Locate source for let ${letDecl.name}`}
                        sourceKey={`let-name-${letDecl.name}`}
                        onSourceRangeFocus={onSourceRangeFocus}
                      />
                      <span className="font-mono text-[10px] text-muted-foreground/40">:</span>
                      <span className="min-w-0 truncate font-mono text-[10px] text-muted-foreground/70">
                        {letDecl.type}
                      </span>
                      <SourceRangeButton
                        range={letDecl.type_source_range}
                        label={`Locate source for let ${letDecl.name} type`}
                        sourceKey={`let-type-${letDecl.name}`}
                        onSourceRangeFocus={onSourceRangeFocus}
                      />
                      <SourceRangeButton
                        range={letTypeDef?.name_source_range ?? letTypeDef?.source_range}
                        sourceFile={letTypeDef?.source_file}
                        label={`Locate source for let ${letDecl.name} type definition`}
                        sourceKey={`let-type-def-${letDecl.name}`}
                        onSourceRangeFocus={onSourceRangeFocus}
                      />
                      {letDecl.arg && (
                        <>
                          <span className="font-mono text-[10px] text-muted-foreground/40">=</span>
                          <span className="min-w-0 flex-1 truncate font-mono text-[10px] text-muted-foreground/70">
                            {letDecl.type}({letDecl.arg})
                          </span>
                          <SourceRangeButton
                            range={letDecl.constructor_source_range}
                            label={`Locate source for let ${letDecl.name} constructor`}
                            sourceKey={`let-constructor-${letDecl.name}`}
                            onSourceRangeFocus={onSourceRangeFocus}
                          />
                          <SourceRangeButton
                            range={letDecl.type_source_range}
                            label={`Locate source for let ${letDecl.name} constructor type`}
                            sourceKey={`let-constructor-type-${letDecl.name}`}
                            onSourceRangeFocus={onSourceRangeFocus}
                          />
                          <SourceRangeButton
                            range={letTypeDef?.name_source_range ?? letTypeDef?.source_range}
                            sourceFile={letTypeDef?.source_file}
                            label={`Locate source for let ${letDecl.name} constructor type definition`}
                            sourceKey={`let-constructor-type-def-${letDecl.name}`}
                            onSourceRangeFocus={onSourceRangeFocus}
                          />
                          <SourceRangeButton
                            range={letDecl.arg_source_range}
                            label={`Locate source for let ${letDecl.name} constructor argument`}
                            sourceKey={`let-constructor-arg-${letDecl.name}`}
                            onSourceRangeFocus={onSourceRangeFocus}
                          />
                        </>
                      )}
                      <SourceRangeButton
                        range={letDecl.source_range}
                        label={`Locate source for let declaration ${letDecl.name}`}
                        sourceKey={`let-${letDecl.name}`}
                        onSourceRangeFocus={onSourceRangeFocus}
                      />
                    </div>
                    <AnnotationPanel
                      annotations={letDecl.annotations}
                      annotateCommand={`annotate let ${letDecl.name}`}
                      unannotateCommand={`unannotate let ${letDecl.name}`}
                      onExec={onExec}
                      onSourceRangeFocus={onSourceRangeFocus}
                      sourceKeyPrefix={`let-${letDecl.name}`}
                      declaredTypes={declaredTypes}
                    />
                  </div>
                )
              })}
            </div>
          </PanelSection>
        </>
      )}

      {(declarationNodeTypes.length > 0 || declaredTypes.length > 0 || schemas.length > 0) && (
        <>
          <Separator className="opacity-50" />
          <PanelSection title="Declarations">
            <div className="space-y-1">
              {declarationNodeTypes.map(typeDef => (
                <div
                  key={typeDef.persistent_id ?? typeDef.type_name}
                  data-declared-node-type={typeDef.type_name}
                  className="space-y-1 rounded border border-border/30 px-1.5 py-1"
                >
                  <div className="flex min-w-0 items-center gap-1">
                    <Badge variant="outline" className="h-3.5 px-1 py-0 text-[8px] border-border/50 text-muted-foreground/60">
                      node
                    </Badge>
                    <span className="min-w-0 flex-1 truncate font-mono text-[10px] text-muted-foreground/70">
                      {typeDef.type_name}
                    </span>
                    <SourceRangeButton
                      range={typeDef.name_source_range}
                      sourceFile={typeDef.source_file}
                      label={`Locate source for declaration node type name ${typeDef.type_name}`}
                      sourceKey={`decl-node-type-name-${typeDef.type_name}`}
                      onSourceRangeFocus={onSourceRangeFocus}
                    />
                    <SourceRangeButton
                      range={typeDef.source_range}
                      sourceFile={typeDef.source_file}
                      label={`Locate source for declaration node type ${typeDef.type_name}`}
                      sourceKey={`decl-node-type-${typeDef.type_name}`}
                      onSourceRangeFocus={onSourceRangeFocus}
                    />
                  </div>
                  <AnnotationPanel
                    annotations={typeDef.annotations}
                    readOnly
                    onSourceRangeFocus={onSourceRangeFocus}
                    sourceKeyPrefix={`decl-node-type-${typeDef.type_name}`}
                    sourceFile={typeDef.source_file}
                    declaredTypes={declaredTypes}
                  />
                  {typeDef.pins.length > 0 && (
                    <div className="space-y-0.5">
                      {typeDef.pins.map(pin => {
                        const pinTypeDef = declaredTypes.find(declaredType => declaredType.name === pin.type)
                        return (
                          <div
                            key={`${typeDef.type_name}-${pin.direction}-${pin.name}`}
                            data-declared-node-pin={`${typeDef.type_name}.${pin.name}`}
                            className="space-y-1 rounded border border-border/20 px-1 py-0.5"
                          >
                            <div className="flex min-w-0 items-center gap-1">
                              <Badge variant="outline" className="h-3.5 px-1 py-0 text-[8px] border-border/40 text-muted-foreground/50">
                                {pin.direction}
                              </Badge>
                              <span className="min-w-0 truncate font-mono text-[10px] text-muted-foreground/60">
                                {pin.name}
                              </span>
                              <SourceRangeButton
                                range={pin.name_source_range}
                                sourceFile={pin.source_file}
                                label={`Locate source for declaration node pin ${pin.name} name`}
                                sourceKey={`decl-node-pin-name-${typeDef.type_name}-${pin.name}`}
                                onSourceRangeFocus={onSourceRangeFocus}
                              />
                              {pin.type && (
                                <>
                                  <span className="font-mono text-[10px] text-muted-foreground/40">:</span>
                                  <span className="min-w-0 flex-1 truncate font-mono text-[10px] text-muted-foreground/60">
                                    {pin.type}
                                  </span>
                                  <SourceRangeButton
                                    range={pin.type_source_range}
                                    sourceFile={pin.source_file}
                                    label={`Locate source for declaration node pin ${pin.name} type`}
                                    sourceKey={`decl-node-pin-type-${typeDef.type_name}-${pin.name}`}
                                    onSourceRangeFocus={onSourceRangeFocus}
                                  />
                                  <SourceRangeButton
                                    range={pinTypeDef?.name_source_range ?? pinTypeDef?.source_range}
                                    sourceFile={pinTypeDef?.source_file}
                                    label={`Locate source for declaration node pin ${pin.name} type definition`}
                                    sourceKey={`decl-node-pin-type-def-${typeDef.type_name}-${pin.name}`}
                                    onSourceRangeFocus={onSourceRangeFocus}
                                  />
                                </>
                              )}
                              {!pin.type && <span className="min-w-0 flex-1" />}
                              <SourceRangeButton
                                range={pin.source_range}
                                sourceFile={pin.source_file}
                                label={`Locate source for declaration node pin ${pin.name}`}
                                sourceKey={`decl-node-pin-${typeDef.type_name}-${pin.name}`}
                                onSourceRangeFocus={onSourceRangeFocus}
                              />
                            </div>
                            <AnnotationPanel
                              annotations={pin.annotations}
                              readOnly
                              onSourceRangeFocus={onSourceRangeFocus}
                              sourceKeyPrefix={`decl-node-pin-${typeDef.type_name}-${pin.name}`}
                              sourceFile={pin.source_file}
                              declaredTypes={declaredTypes}
                            />
                          </div>
                        )
                      })}
                    </div>
                  )}
                </div>
              ))}
              {declaredTypes.map(typeDef => (
                <div
                  key={typeDef.persistent_id ?? typeDef.name}
                  data-declared-type={typeDef.name}
                  className="space-y-1 rounded border border-border/30 px-1.5 py-0.5"
                >
                  <div className="flex min-w-0 items-center gap-1">
                    <Badge variant="outline" className="h-3.5 px-1 py-0 text-[8px] border-border/50 text-muted-foreground/60">
                      type
                    </Badge>
                    <span className="min-w-0 flex-1 truncate font-mono text-[10px] text-muted-foreground/70">
                      {typeDef.name}
                    </span>
                    <SourceRangeButton
                      range={typeDef.name_source_range}
                      sourceFile={typeDef.source_file}
                      label={`Locate source for declaration type name ${typeDef.name}`}
                      sourceKey={`decl-type-name-${typeDef.name}`}
                      onSourceRangeFocus={onSourceRangeFocus}
                    />
                    <SourceRangeButton
                      range={typeDef.source_range}
                      sourceFile={typeDef.source_file}
                      label={`Locate source for declaration type ${typeDef.name}`}
                      sourceKey={`decl-type-${typeDef.name}`}
                      onSourceRangeFocus={onSourceRangeFocus}
                    />
                  </div>
                  <AnnotationPanel
                    annotations={typeDef.annotations}
                    readOnly
                    onSourceRangeFocus={onSourceRangeFocus}
                    sourceKeyPrefix={`decl-type-${typeDef.name}`}
                    sourceFile={typeDef.source_file}
                    declaredTypes={declaredTypes}
                  />
                </div>
              ))}
              {schemas.map(schema => (
                <div
                  key={schema.persistent_id ?? schema.name}
                  data-declared-schema={schema.name}
                  className="space-y-1 rounded border border-border/30 px-1.5 py-1"
                >
                  <div className="flex min-w-0 items-center gap-1">
                    <Badge variant="outline" className="h-3.5 px-1 py-0 text-[8px] border-border/50 text-muted-foreground/60">
                      schema
                    </Badge>
                    <span className="min-w-0 flex-1 truncate font-mono text-[10px] text-muted-foreground/70">
                      {schema.name}
                    </span>
                    <SourceRangeButton
                      range={schema.name_source_range}
                      sourceFile={schema.source_file}
                      label={`Locate source for schema name ${schema.name}`}
                      sourceKey={`decl-schema-name-${schema.name}`}
                      onSourceRangeFocus={onSourceRangeFocus}
                    />
                    <SourceRangeButton
                      range={schema.source_range}
                      sourceFile={schema.source_file}
                      label={`Locate source for schema ${schema.name}`}
                      sourceKey={`decl-schema-${schema.name}`}
                      onSourceRangeFocus={onSourceRangeFocus}
                    />
                  </div>
                  <AnnotationPanel
                    annotations={schema.annotations}
                    readOnly
                    onSourceRangeFocus={onSourceRangeFocus}
                    sourceKeyPrefix={`decl-schema-${schema.name}`}
                    sourceFile={schema.source_file}
                    declaredTypes={declaredTypes}
                  />
                  {(schema.fields?.length ?? 0) > 0 && (
                    <div className="space-y-0.5">
                      {schema.fields?.map(field => {
                        const fieldConstructorTypeDef = declaredTypes.find(typeDef => typeDef.name === constructorTypeName(field.value))
                        return (
                          <div
                            key={field.persistent_id ?? `${schema.name}-${field.name}`}
                            data-declared-schema-field={`${schema.name}.${field.name}`}
                            className="space-y-1 rounded border border-border/20 px-1 py-0.5"
                          >
                            <div className="flex min-w-0 items-center gap-1">
                              <span className="min-w-0 truncate font-mono text-[10px] text-muted-foreground/60">
                                {field.name}
                              </span>
                              <SourceRangeButton
                                range={field.name_source_range}
                                sourceFile={field.source_file}
                                label={`Locate source for schema field ${field.name} name`}
                                sourceKey={`decl-schema-field-name-${schema.name}-${field.name}`}
                                onSourceRangeFocus={onSourceRangeFocus}
                              />
                              <span className="font-mono text-[10px] text-muted-foreground/40">=</span>
                              <span className="min-w-0 flex-1 truncate font-mono text-[10px] text-muted-foreground/60">
                                {field.value}
                              </span>
                              <SourceRangeButton
                                range={field.value_source_range}
                                sourceFile={field.source_file}
                                label={`Locate source for schema field ${field.name} value`}
                                sourceKey={`decl-schema-field-value-${schema.name}-${field.name}`}
                                onSourceRangeFocus={onSourceRangeFocus}
                              />
                              <SourceRangeButton
                                range={field.value_constructor_source_range}
                                sourceFile={field.source_file}
                                label={`Locate source for schema field ${field.name} constructor value`}
                                sourceKey={`decl-schema-field-constructor-${schema.name}-${field.name}`}
                                onSourceRangeFocus={onSourceRangeFocus}
                              />
                              <SourceRangeButton
                                range={field.value_constructor_type_source_range}
                                sourceFile={field.source_file}
                                label={`Locate source for schema field ${field.name} constructor type`}
                                sourceKey={`decl-schema-field-constructor-type-${schema.name}-${field.name}`}
                                onSourceRangeFocus={onSourceRangeFocus}
                              />
                              <SourceRangeButton
                                range={fieldConstructorTypeDef?.name_source_range ?? fieldConstructorTypeDef?.source_range}
                                sourceFile={fieldConstructorTypeDef?.source_file}
                                label={`Locate source for schema field ${field.name} constructor type definition`}
                                sourceKey={`decl-schema-field-constructor-type-def-${schema.name}-${field.name}`}
                                onSourceRangeFocus={onSourceRangeFocus}
                              />
                              <SourceRangeButton
                                range={field.value_constructor_arg_source_range}
                                sourceFile={field.source_file}
                                label={`Locate source for schema field ${field.name} constructor argument`}
                                sourceKey={`decl-schema-field-constructor-arg-${schema.name}-${field.name}`}
                                onSourceRangeFocus={onSourceRangeFocus}
                              />
                              <SourceRangeButton
                                range={field.source_range}
                                sourceFile={field.source_file}
                                label={`Locate source for schema field ${field.name}`}
                                sourceKey={`decl-schema-field-${schema.name}-${field.name}`}
                                onSourceRangeFocus={onSourceRangeFocus}
                              />
                            </div>
                            <AnnotationPanel
                              annotations={field.annotations}
                              readOnly
                              onSourceRangeFocus={onSourceRangeFocus}
                              sourceKeyPrefix={`decl-schema-field-${schema.name}-${field.name}`}
                              sourceFile={field.source_file}
                              declaredTypes={declaredTypes}
                            />
                          </div>
                        )
                      })}
                    </div>
                  )}
                </div>
              ))}
            </div>
          </PanelSection>
        </>
      )}

      {graph.generate && (graph.generate.comments.length > 0 || graph.generate.metadata.length > 0) && (
        <>
          <Separator className="opacity-50" />
          <PanelSection title="Generate">
            <div className="mb-1 flex items-center gap-1.5">
              <Badge variant="outline" className="text-[8px] px-1 py-0 h-3.5 border-border/50 text-muted-foreground/60">
                block
              </Badge>
              <SourceRangeButton
                range={graph.generate.source_range}
                label="Locate source for generate block"
                sourceKey="generate-block"
                onSourceRangeFocus={onSourceRangeFocus}
              />
            </div>
            <GeneratePanel
              generate={graph.generate}
              nodes={graph.nodes}
              nodeTypes={nodeTypes}
              declaredTypes={declaredTypes}
              onExec={onExec}
              onSourceRangeFocus={onSourceRangeFocus}
            />
          </PanelSection>
        </>
      )}

      {graph.parameters.length > 0 && (
        <>
          <Separator className="opacity-50" />
          <PanelSection title="Parameters">
            <div className="space-y-1">
              {graph.parameters.map(p => {
                const paramDiagnostics = parameterDiagnostics(graph, p, diagnostics)
                const severity = diagnosticSeverity(paramDiagnostics)
                const focused = !!focusedDiagnostic && paramDiagnostics.includes(focusedDiagnostic)
                const borderColor = diagnosticBorderColor(severity)
                const paramTypeDef = declaredTypes.find(typeDef => typeDef.name === p.type)
                const hasDefaultConstructorValue = isConstructorCallValue(p.default)
                const paramDefaultConstructorTypeDef = declaredTypes.find(typeDef => typeDef.name === constructorTypeName(p.default))
                return (
                <div
                  key={p.name}
                  data-graph-param={p.name}
                  data-param-diagnostic-severity={severity ?? undefined}
                  data-param-diagnostic-focused={focused ? 'true' : 'false'}
                  className="space-y-1 rounded-md border border-border/40 p-1.5"
                  style={{
                    borderColor,
                    boxShadow: focused && borderColor ? `0 0 0 1px ${borderColor}` : undefined,
                  }}
                >
                  <div className="flex items-center gap-1.5">
                    <Badge variant="outline"
                      className="text-[8px] px-1 py-0 h-3.5 border-border/50 text-muted-foreground/60"
                    >
                      {p.direction}
                    </Badge>
                    <span className="text-[11px] font-mono text-foreground/80">{p.name}</span>
                    <form
                      className="flex min-w-[100px] max-w-[150px] shrink-0 gap-1"
                      onSubmit={async e => {
                        e.preventDefault()
                        const name = new FormData(e.currentTarget).get('name')?.toString().trim() ?? ''
                        if (!name || name === p.name) return
                        await onExec(`rename_param ${p.name} ${name}`)
                      }}
                    >
                      <Input
                        key={`param-row-name-${p.name}`}
                        name="name"
                        aria-label={`Parameter row name ${p.name}`}
                        defaultValue={p.name}
                        className="h-6 min-w-0 px-1.5 text-[10px] font-mono"
                      />
                      <Button
                        type="submit"
                        variant="secondary"
                        size="icon"
                        className="h-6 w-6 shrink-0"
                        title={`Rename parameter row ${p.name}`}
                      >
                        <Pencil className="h-3 w-3" />
                        <span className="sr-only">Rename parameter row {p.name}</span>
                      </Button>
                    </form>
                    <SourceRangeButton
                      range={p.name_source_range}
                      label={`Locate source for parameter ${p.name} name`}
                      sourceKey={`param-name-${p.name}`}
                      onSourceRangeFocus={onSourceRangeFocus}
                    />
                    <form
                      className="ml-auto flex min-w-[84px] max-w-[130px] shrink-0 gap-1"
                      onSubmit={async e => {
                        e.preventDefault()
                        const typeName = new FormData(e.currentTarget).get('type')?.toString().trim() ?? ''
                        if (!typeName || typeName === p.type) return
                        await onExec(`set_param_type ${p.name} ${typeName}`)
                      }}
                    >
                      <Input
                        key={`param-row-type-${p.name}-${p.type}`}
                        name="type"
                        aria-label={`Parameter row type ${p.name}`}
                        defaultValue={p.type}
                        className="h-6 min-w-0 px-1.5 text-[10px] font-mono text-muted-foreground/80"
                      />
                      <Button
                        type="submit"
                        variant="secondary"
                        size="icon"
                        className="h-6 w-6 shrink-0"
                        title={`Set parameter row type ${p.name}`}
                      >
                        <Pencil className="h-3 w-3" />
                        <span className="sr-only">Set parameter row type {p.name}</span>
                      </Button>
                    </form>
                    <SourceRangeButton
                      range={p.type_source_range}
                      label={`Locate source for parameter ${p.name} type`}
                      sourceKey={`param-type-${p.name}`}
                      onSourceRangeFocus={onSourceRangeFocus}
                    />
                    <SourceRangeButton
                      range={paramTypeDef?.name_source_range ?? paramTypeDef?.source_range}
                      sourceFile={paramTypeDef?.source_file}
                      label={`Locate source for parameter ${p.name} type definition`}
                      sourceKey={`param-type-def-${p.name}`}
                      onSourceRangeFocus={onSourceRangeFocus}
                    />
                  <SourceRangeButton
                    range={p.source_range}
                    label={`Locate source for parameter ${p.name}`}
                    sourceKey={`param-${p.name}`}
                    onSourceRangeFocus={onSourceRangeFocus}
                  />
                  <DeleteIconButton label={`Remove parameter ${p.name}`} onClick={() => onExec(`param rm ${p.name}`)} />
                </div>
                  <RenameForm
                    label={`Rename parameter ${p.name}`}
                    placeholder="new parameter name"
                    currentName={p.name}
                    onRename={name => onExec(`rename_param ${p.name} ${name}`)}
                  />
                  <AnnotationPanel
                    annotations={p.annotations}
                    annotateCommand={`annotate param ${p.name}`}
                    unannotateCommand={`unannotate param ${p.name}`}
                    onExec={onExec}
                    onSourceRangeFocus={onSourceRangeFocus}
                    sourceKeyPrefix={`param-${p.name}`}
                    declaredTypes={declaredTypes}
                  />
                  {p.default && (
                    <div
                      data-graph-param-default={p.name}
                      className="flex min-w-0 items-center gap-1 rounded border border-border/30 px-1.5 py-0.5"
                    >
                      <Badge variant="outline" className="h-3.5 px-1 py-0 text-[8px] border-border/50 text-muted-foreground/60">
                        default
                      </Badge>
                      <span className="min-w-0 flex-1 truncate font-mono text-[10px] text-muted-foreground/70">
                        {p.default}
                      </span>
                      <form
                        className="flex min-w-[120px] max-w-[180px] shrink-0 gap-1"
                        onSubmit={async e => {
                          e.preventDefault()
                          const value = new FormData(e.currentTarget).get('value')?.toString().trim() ?? ''
                          if (!value) return
                          await onExec(`set_param_default ${p.name} ${quoteCommandArg(value)}`)
                        }}
                      >
                        <Input
                          key={`param-default-value-${p.default}`}
                          name="value"
                          aria-label={`Parameter default row value ${p.name}`}
                          defaultValue={p.default}
                          className="h-6 min-w-0 px-1.5 text-[10px] font-mono"
                        />
                        <Button
                          type="submit"
                          variant="secondary"
                          size="icon"
                          className="h-6 w-6 shrink-0"
                          title={`Set parameter default row value ${p.name}`}
                        >
                          <Pencil className="h-3 w-3" />
                          <span className="sr-only">Set parameter default row value {p.name}</span>
                        </Button>
                      </form>
                      <SourceRangeButton
                        range={p.default_source_range}
                        label={`Locate source for parameter ${p.name} default value`}
                        sourceKey={`param-default-${p.name}`}
                        onSourceRangeFocus={onSourceRangeFocus}
                      />
                      <SourceRangeButton
                        range={p.default_constructor_source_range}
                        label={`Locate source for parameter ${p.name} default constructor`}
                        sourceKey={`param-default-constructor-${p.name}`}
                        onSourceRangeFocus={onSourceRangeFocus}
                      />
                      <SourceRangeButton
                        range={p.default_constructor_type_source_range}
                        label={`Locate source for parameter ${p.name} default constructor type`}
                        sourceKey={`param-default-constructor-type-${p.name}`}
                        onSourceRangeFocus={onSourceRangeFocus}
                      />
                      <SourceRangeButton
                        range={paramDefaultConstructorTypeDef?.name_source_range ?? paramDefaultConstructorTypeDef?.source_range}
                        sourceFile={paramDefaultConstructorTypeDef?.source_file}
                        label={`Locate source for parameter ${p.name} default constructor type definition`}
                        sourceKey={`param-default-constructor-type-def-${p.name}`}
                        onSourceRangeFocus={onSourceRangeFocus}
                      />
                      <SourceRangeButton
                        range={p.default_constructor_arg_source_range}
                        label={`Locate source for parameter ${p.name} default constructor argument`}
                        sourceKey={`param-default-constructor-arg-${p.name}`}
                        onSourceRangeFocus={onSourceRangeFocus}
                      />
                      {hasDefaultConstructorValue && (
                        <>
                          <form
                            className="flex min-w-[100px] max-w-[140px] shrink-0 gap-1"
                            onSubmit={async e => {
                              e.preventDefault()
                              const typeName = new FormData(e.currentTarget).get('typeName')?.toString().trim() ?? ''
                              if (!typeName) return
                              await onExec(`set_param_default_ctor_type ${p.name} ${typeName}`)
                            }}
                          >
                            <Input
                              key={`param-default-type-${constructorTypeName(p.default)}`}
                              name="typeName"
                              aria-label={`Parameter default constructor type row value ${p.name}`}
                              defaultValue={constructorTypeName(p.default)}
                              className="h-6 min-w-0 px-1.5 text-[10px] font-mono"
                            />
                            <Button
                              type="submit"
                              variant="secondary"
                              size="icon"
                              className="h-6 w-6 shrink-0"
                              title={`Set parameter default constructor type row value ${p.name}`}
                            >
                              <Pencil className="h-3 w-3" />
                              <span className="sr-only">Set parameter default constructor type row value {p.name}</span>
                            </Button>
                          </form>
                          <form
                            className="flex min-w-[110px] max-w-[160px] shrink-0 gap-1"
                            onSubmit={async e => {
                              e.preventDefault()
                              const argument = new FormData(e.currentTarget).get('argument')?.toString().trim() ?? ''
                              if (!argument) return
                              await onExec(`set_param_default_ctor_arg ${p.name} ${quoteCommandArg(argument)}`)
                            }}
                          >
                            <Input
                              key={`param-default-arg-${constructorArgumentValue(p.default)}`}
                              name="argument"
                              aria-label={`Parameter default constructor argument row value ${p.name}`}
                              defaultValue={constructorArgumentValue(p.default)}
                              className="h-6 min-w-0 px-1.5 text-[10px] font-mono"
                            />
                            <Button
                              type="submit"
                              variant="secondary"
                              size="icon"
                              className="h-6 w-6 shrink-0"
                              title={`Set parameter default constructor argument row value ${p.name}`}
                            >
                              <Pencil className="h-3 w-3" />
                              <span className="sr-only">Set parameter default constructor argument row value {p.name}</span>
                            </Button>
                          </form>
                          <DeleteIconButton
                            label={`Clear parameter default constructor argument ${p.name}`}
                            onClick={() => onExec(`set_param_default_ctor_arg ${p.name}`)}
                          />
                        </>
                      )}
                      <DeleteIconButton
                        label={`Clear parameter default ${p.name}`}
                        onClick={() => onExec(`set_param_default ${p.name}`)}
                      />
                    </div>
                  )}
                </div>
                )
              })}
            </div>
          </PanelSection>
        </>
      )}

      <Separator className="opacity-50" />
      <PanelSection title="Add Parameter">
        <form className="space-y-1.5" onSubmit={addParam}>
          <div className="flex gap-1.5">
            <select
              className="h-7 rounded-md border border-input bg-secondary/50 px-1.5 text-[10px] font-medium
                focus:outline-none focus:ring-1 focus:ring-ring text-foreground/80"
              value={paramDirection}
              onChange={e => setParamDirection(e.target.value as 'in' | 'out' | 'var')}
            >
              <option value="in">in</option>
              <option value="out">out</option>
              <option value="var">var</option>
            </select>
            <Input
              value={paramName}
              onChange={e => setParamName(e.target.value)}
              placeholder="name"
              className="h-7 px-2 text-[11px] font-mono"
            />
          </div>
          <div className="flex gap-1.5">
            <Input
              value={paramType}
              onChange={e => setParamType(e.target.value)}
              placeholder="type"
              className="h-7 px-2 text-[11px] font-mono"
            />
            <CompactIconButton label="Add parameter" disabled={!paramName.trim() || !paramType.trim()} />
          </div>
          <Input
            value={paramDefault}
            onChange={e => setParamDefault(e.target.value)}
            placeholder="default"
            className="h-7 px-2 text-[11px] font-mono"
          />
        </form>
      </PanelSection>

      <Separator className="opacity-50" />
      <PanelSection title="Set Parameter Default">
        <form className="space-y-1.5" data-param-default-form={graph.name} onSubmit={setParamDefaultValueForGraph}>
          <div className="flex gap-1.5">
            <Input
              aria-label={`Parameter default name for ${graph.name}`}
              value={paramDefaultName}
              onChange={e => setParamDefaultName(e.target.value)}
              placeholder="param"
              className="h-7 px-2 text-[11px] font-mono"
            />
            <Button
              type="submit"
              variant="secondary"
              size="icon"
              disabled={!paramDefaultName.trim()}
              className="h-7 w-7 shrink-0"
              title={`Set parameter default for ${graph.name}`}
            >
              <Pencil className="h-3.5 w-3.5" />
              <span className="sr-only">Set parameter default</span>
            </Button>
          </div>
          <Input
            aria-label={`Parameter default value for ${graph.name}`}
            value={paramDefaultValue}
            onChange={e => setParamDefaultValue(e.target.value)}
            placeholder="default"
            className="h-7 px-2 text-[11px] font-mono"
          />
        </form>
      </PanelSection>

      <Separator className="opacity-50" />
      <PanelSection title="Set Parameter Constructor">
        <form className="space-y-1.5" data-param-default-constructor-form={graph.name} onSubmit={setParamDefaultConstructorForGraph}>
          <div className="flex gap-1.5">
            <Input
              aria-label={`Parameter default constructor name for ${graph.name}`}
              value={paramDefaultCtorName}
              onChange={e => setParamDefaultCtorName(e.target.value)}
              placeholder="param"
              className="h-7 px-2 text-[11px] font-mono"
            />
            <Button
              type="submit"
              variant="secondary"
              size="icon"
              disabled={!paramDefaultCtorName.trim() || !paramDefaultCtorType.trim()}
              className="h-7 w-7 shrink-0"
              title={`Set parameter default constructor for ${graph.name}`}
            >
              <Pencil className="h-3.5 w-3.5" />
              <span className="sr-only">Set parameter default constructor</span>
            </Button>
          </div>
          <Input
            aria-label={`Parameter default constructor type for ${graph.name}`}
            value={paramDefaultCtorType}
            onChange={e => setParamDefaultCtorType(e.target.value)}
            placeholder="Type"
            className="h-7 px-2 text-[11px] font-mono"
          />
          <Input
            aria-label={`Parameter default constructor argument for ${graph.name}`}
            value={paramDefaultCtorArg}
            onChange={e => setParamDefaultCtorArg(e.target.value)}
            placeholder="argument"
            className="h-7 px-2 text-[11px] font-mono"
          />
        </form>
      </PanelSection>

      <Separator className="opacity-50" />
      <PanelSection title="Set Parameter Constructor Argument">
        <form className="space-y-1.5" data-param-default-constructor-argument-form={graph.name} onSubmit={setParamDefaultConstructorArgumentForGraph}>
          <div className="flex gap-1.5">
            <Input
              aria-label={`Parameter default constructor argument name for ${graph.name}`}
              value={paramDefaultCtorArgName}
              onChange={e => setParamDefaultCtorArgName(e.target.value)}
              placeholder="param"
              className="h-7 px-2 text-[11px] font-mono"
            />
            <Button
              type="submit"
              variant="secondary"
              size="icon"
              disabled={!paramDefaultCtorArgName.trim()}
              className="h-7 w-7 shrink-0"
              title={`Set parameter default constructor argument for ${graph.name}`}
            >
              <Pencil className="h-3.5 w-3.5" />
              <span className="sr-only">Set parameter default constructor argument</span>
            </Button>
          </div>
          <Input
            aria-label={`Parameter default constructor argument value for ${graph.name}`}
            value={paramDefaultCtorArgValue}
            onChange={e => setParamDefaultCtorArgValue(e.target.value)}
            placeholder="argument"
            className="h-7 px-2 text-[11px] font-mono"
          />
        </form>
      </PanelSection>

      <Separator className="opacity-50" />
      <PanelSection title="Set Parameter Constructor Type">
        <form className="space-y-1.5" data-param-default-constructor-type-form={graph.name} onSubmit={setParamDefaultConstructorTypeForGraph}>
          <div className="flex gap-1.5">
            <Input
              aria-label={`Parameter default constructor type name for ${graph.name}`}
              value={paramDefaultCtorTypeName}
              onChange={e => setParamDefaultCtorTypeName(e.target.value)}
              placeholder="param"
              className="h-7 px-2 text-[11px] font-mono"
            />
            <Button
              type="submit"
              variant="secondary"
              size="icon"
              disabled={!paramDefaultCtorTypeName.trim() || !paramDefaultCtorTypeValue.trim()}
              className="h-7 w-7 shrink-0"
              title={`Set parameter default constructor type for ${graph.name}`}
            >
              <Pencil className="h-3.5 w-3.5" />
              <span className="sr-only">Set parameter default constructor type</span>
            </Button>
          </div>
          <Input
            aria-label={`Parameter default constructor type value for ${graph.name}`}
            value={paramDefaultCtorTypeValue}
            onChange={e => setParamDefaultCtorTypeValue(e.target.value)}
            placeholder="Type"
            className="h-7 px-2 text-[11px] font-mono"
          />
        </form>
      </PanelSection>

      <Separator className="opacity-50" />
      <PanelSection title="Logic Blocks">
        <div className="space-y-1.5">
          {graph.events.map(ev => (
            <div key={`event-${ev.name}`} data-graph-event={ev.name} className="space-y-1">
              <div className="flex items-center gap-1.5 py-0.5">
                <Boxes className="h-3 w-3 text-primary/60" />
                <Badge variant="outline" className="text-[8px] px-1 py-0 h-3.5 border-border/50 text-muted-foreground/60">
                  event
                </Badge>
                <span className="text-[11px] font-mono text-foreground/80 truncate">{ev.name}</span>
                <SourceRangeButton
                  range={ev.name_source_range}
                  label={`Locate source for event name ${ev.name}`}
                  sourceKey={`event-name-${ev.name}`}
                  onSourceRangeFocus={onSourceRangeFocus}
                />
                <SourceRangeButton
                  range={ev.source_range}
                  label={`Locate source for event ${ev.name}`}
                  sourceKey={`event-${ev.name}`}
                  onSourceRangeFocus={onSourceRangeFocus}
                />
                <DeleteIconButton label={`Remove event ${ev.name}`} onClick={() => onExec(`delete_event ${ev.name}`)} />
              </div>
              <RenameForm
                label={`Rename event ${ev.name}`}
                placeholder="new event name"
                currentName={ev.name}
                onRename={name => onExec(`rename_event ${ev.name} ${name}`)}
              />
              <div className="ml-5">
                <AnnotationPanel
                  annotations={ev.annotations}
                  annotateCommand={`annotate event ${ev.name}`}
                  unannotateCommand={`unannotate event ${ev.name}`}
                  onExec={onExec}
                  onSourceRangeFocus={onSourceRangeFocus}
                  sourceKeyPrefix={`event-${ev.name}`}
                  declaredTypes={declaredTypes}
                />
              </div>
              <LogicBlockConnections
                block={ev}
                graph={graph}
                nodeTypes={nodeTypes}
                declaredTypes={declaredTypes}
                selectedEdge={selectedEdge}
                onEdgeRedirected={onEdgeRedirected}
                onExec={onExec}
                onSourceRangeFocus={onSourceRangeFocus}
              />
            </div>
          ))}
          {graph.functions.map(fn => (
            <div key={`function-${fn.name}`} data-graph-function={fn.name} className="space-y-1">
              <div className="flex items-center gap-1.5 py-0.5">
                <FunctionSquare className="h-3 w-3 text-primary/60" />
                <Badge variant="outline" className="text-[8px] px-1 py-0 h-3.5 border-border/50 text-muted-foreground/60">
                  fn
                </Badge>
                <span className="text-[11px] font-mono text-foreground/80 truncate">{fn.name}</span>
                <SourceRangeButton
                  range={fn.name_source_range}
                  label={`Locate source for function name ${fn.name}`}
                  sourceKey={`function-name-${fn.name}`}
                  onSourceRangeFocus={onSourceRangeFocus}
                />
                <SourceRangeButton
                  range={fn.source_range}
                  label={`Locate source for function ${fn.name}`}
                  sourceKey={`function-${fn.name}`}
                  onSourceRangeFocus={onSourceRangeFocus}
                />
                <DeleteIconButton label={`Remove function ${fn.name}`} onClick={() => onExec(`delete_function ${fn.name}`)} />
              </div>
              <RenameForm
                label={`Rename function ${fn.name}`}
                placeholder="new function name"
                currentName={fn.name}
                onRename={name => onExec(`rename_function ${fn.name} ${name}`)}
              />
              <div className="ml-5">
                <AnnotationPanel
                  annotations={fn.annotations}
                  annotateCommand={`annotate function ${fn.name}`}
                  unannotateCommand={`unannotate function ${fn.name}`}
                  onExec={onExec}
                  onSourceRangeFocus={onSourceRangeFocus}
                  sourceKeyPrefix={`function-${fn.name}`}
                  declaredTypes={declaredTypes}
                />
              </div>
              <LogicBlockConnections
                block={fn}
                graph={graph}
                nodeTypes={nodeTypes}
                declaredTypes={declaredTypes}
                selectedEdge={selectedEdge}
                onEdgeRedirected={onEdgeRedirected}
                onExec={onExec}
                onSourceRangeFocus={onSourceRangeFocus}
              />
            </div>
          ))}
        </div>
      </PanelSection>

      <PanelSection title="Add Block">
        <div className="space-y-1.5">
          <form className="flex gap-1.5" onSubmit={addEvent}>
            <Input
              value={eventName}
              onChange={e => setEventName(e.target.value)}
              placeholder="event"
              className="h-7 px-2 text-[11px] font-mono"
            />
            <CompactIconButton label="Add event" disabled={!eventName.trim()} />
          </form>
          <form className="flex gap-1.5" onSubmit={addFunction}>
            <Input
              value={functionName}
              onChange={e => setFunctionName(e.target.value)}
              placeholder="function"
              className="h-7 px-2 text-[11px] font-mono"
            />
            <CompactIconButton label="Add function" disabled={!functionName.trim()} />
          </form>
        </div>
      </PanelSection>
    </div>
  )
}

function InitializerFieldRow({
  field,
  nodeInstance,
  fieldPinDef,
  declaredTypes,
  onSourceRangeFocus,
  onExec,
}: {
  field: InitializerField
  nodeInstance: string
  fieldPinDef?: PinDef
  declaredTypes: DeclaredTypeDef[]
  onSourceRangeFocus?: (range: SourceRange, sourceFile?: string) => Promise<void> | void
  onExec: (cmd: string) => Promise<void> | void
}) {
  const isConstructorValue = isConstructorCallValue(field.value)
  const fieldConstructorTypeDef = declaredTypes.find(typeDef => typeDef.name === constructorTypeName(field.value))
  const fieldPinTypeDef = declaredTypes.find(typeDef => typeDef.name === fieldPinDef?.type)
  const [fieldNameDraft, setFieldNameDraft] = useState(field.name)
  const [fieldValueDraft, setFieldValueDraft] = useState(field.value)

  useEffect(() => {
    setFieldNameDraft(field.name)
  }, [field.name])

  useEffect(() => {
    setFieldValueDraft(field.value)
  }, [field.value])

  const submitFieldName = async (e: FormEvent) => {
    e.preventDefault()
    const name = fieldNameDraft.trim()
    if (!name || name === field.name) return
    await onExec(`rename_init ${nodeInstance} ${field.name} ${name}`)
  }

  const submitFieldValue = async (e: FormEvent) => {
    e.preventDefault()
    const value = fieldValueDraft.trim()
    if (!value) return
    await onExec(`set_init ${nodeInstance} ${field.name} ${quoteCommandArg(value)}`)
  }

  const submitConstructorArgument = async (e: FormEvent<HTMLFormElement>) => {
    e.preventDefault()
    const argument = new FormData(e.currentTarget).get('argument')?.toString().trim() ?? ''
    if (!argument) return
    await onExec(`set_init_ctor_arg ${nodeInstance} ${field.name} ${quoteCommandArg(argument)}`)
  }

  const submitConstructorType = async (e: FormEvent<HTMLFormElement>) => {
    e.preventDefault()
    const typeName = new FormData(e.currentTarget).get('typeName')?.toString().trim() ?? ''
    if (!typeName) return
    await onExec(`set_init_ctor_type ${nodeInstance} ${field.name} ${typeName}`)
  }

  return (
    <div
      key={field.id ?? `${field.name}-${field.value}`}
      data-node-init-field={field.name}
      className="flex min-w-0 items-center gap-1 rounded border border-border/30 px-1.5 py-0.5"
    >
      <span className="min-w-0 truncate font-mono text-[10px] text-muted-foreground/70">
        {field.name}
      </span>
      <form className="flex min-w-[100px] max-w-[150px] shrink-0 gap-1" onSubmit={submitFieldName}>
        <Input
          aria-label={`Initializer row name ${field.name}`}
          value={fieldNameDraft}
          onChange={e => setFieldNameDraft(e.target.value)}
          className="h-6 min-w-0 px-1.5 text-[10px] font-mono"
        />
        <Button
          type="submit"
          variant="secondary"
          size="icon"
          disabled={!fieldNameDraft.trim() || fieldNameDraft.trim() === field.name}
          className="h-6 w-6 shrink-0"
          title={`Rename initializer row field ${field.name}`}
        >
          <Pencil className="h-3 w-3" />
          <span className="sr-only">Rename initializer row field {field.name}</span>
        </Button>
      </form>
      <SourceRangeButton
        range={field.name_source_range}
        label={`Locate source for initializer field ${field.name}`}
        sourceKey={`init-field-name-node-${nodeInstance}-${field.name}`}
        onSourceRangeFocus={onSourceRangeFocus}
      />
      <SourceRangeButton
        range={fieldPinDef?.name_source_range ?? fieldPinDef?.source_range}
        sourceFile={fieldPinDef?.source_file}
        label={`Locate source for initializer field ${field.name} definition`}
        sourceKey={`init-field-name-def-node-${nodeInstance}-${field.name}`}
        onSourceRangeFocus={onSourceRangeFocus}
      />
      <SourceRangeButton
        range={fieldPinTypeDef?.name_source_range ?? fieldPinTypeDef?.source_range}
        sourceFile={fieldPinTypeDef?.source_file}
        label={`Locate source for initializer field ${field.name} type definition`}
        sourceKey={`init-field-name-type-def-node-${nodeInstance}-${field.name}`}
        onSourceRangeFocus={onSourceRangeFocus}
      />
      <span className="font-mono text-[10px] text-muted-foreground/40">=</span>
      <span className="min-w-0 flex-1 truncate font-mono text-[10px] text-muted-foreground/70">
        {field.value}
      </span>
      <form className="flex min-w-[120px] max-w-[180px] shrink-0 gap-1" onSubmit={submitFieldValue}>
        <Input
          aria-label={`Initializer row value ${field.name}`}
          value={fieldValueDraft}
          onChange={e => setFieldValueDraft(e.target.value)}
          className="h-6 min-w-0 px-1.5 text-[10px] font-mono"
        />
        <Button
          type="submit"
          variant="secondary"
          size="icon"
          disabled={!fieldValueDraft.trim()}
          className="h-6 w-6 shrink-0"
          title={`Set initializer row value ${field.name}`}
        >
          <Pencil className="h-3 w-3" />
          <span className="sr-only">Set initializer row value {field.name}</span>
        </Button>
      </form>
      <SourceRangeButton
        range={field.value_source_range ?? field.source_range}
        label={`Locate source for initializer field value ${field.name}`}
        sourceKey={`init-field-value-node-${nodeInstance}-${field.name}`}
        onSourceRangeFocus={onSourceRangeFocus}
      />
      <SourceRangeButton
        range={field.source_range}
        label={`Locate source for initializer field assignment ${field.name}`}
        sourceKey={`init-field-node-${nodeInstance}-${field.name}`}
        onSourceRangeFocus={onSourceRangeFocus}
      />
      <SourceRangeButton
        range={field.value_constructor_source_range}
        label={`Locate source for initializer field constructor ${field.name}`}
        sourceKey={`init-field-constructor-node-${nodeInstance}-${field.name}`}
        onSourceRangeFocus={onSourceRangeFocus}
      />
      <SourceRangeButton
        range={field.value_constructor_type_source_range}
        label={`Locate source for initializer field constructor type ${field.name}`}
        sourceKey={`init-field-constructor-type-node-${nodeInstance}-${field.name}`}
        onSourceRangeFocus={onSourceRangeFocus}
      />
      <SourceRangeButton
        range={fieldConstructorTypeDef?.name_source_range ?? fieldConstructorTypeDef?.source_range}
        sourceFile={fieldConstructorTypeDef?.source_file}
        label={`Locate source for initializer field constructor type definition ${field.name}`}
        sourceKey={`init-field-constructor-type-def-node-${nodeInstance}-${field.name}`}
        onSourceRangeFocus={onSourceRangeFocus}
      />
      <SourceRangeButton
        range={field.value_constructor_arg_source_range}
        label={`Locate source for initializer field constructor argument ${field.name}`}
        sourceKey={`init-field-constructor-arg-node-${nodeInstance}-${field.name}`}
        onSourceRangeFocus={onSourceRangeFocus}
      />
      {isConstructorValue && (
        <>
          <form className="flex min-w-[100px] max-w-[140px] shrink-0 gap-1" onSubmit={submitConstructorType}>
            <Input
              key={`init-type-${constructorTypeName(field.value)}`}
              name="typeName"
              aria-label={`Initializer constructor type row value ${field.name}`}
              defaultValue={constructorTypeName(field.value)}
              className="h-6 min-w-0 px-1.5 text-[10px] font-mono"
            />
            <Button
              type="submit"
              variant="secondary"
              size="icon"
              className="h-6 w-6 shrink-0"
              title={`Set initializer constructor type row value ${field.name}`}
            >
              <Pencil className="h-3 w-3" />
              <span className="sr-only">Set initializer constructor type row value {field.name}</span>
            </Button>
          </form>
          <form className="flex min-w-[110px] max-w-[160px] shrink-0 gap-1" onSubmit={submitConstructorArgument}>
            <Input
              key={`init-arg-${constructorArgumentValue(field.value)}`}
              name="argument"
              aria-label={`Initializer constructor argument row value ${field.name}`}
              defaultValue={constructorArgumentValue(field.value)}
              className="h-6 min-w-0 px-1.5 text-[10px] font-mono"
            />
            <Button
              type="submit"
              variant="secondary"
              size="icon"
              className="h-6 w-6 shrink-0"
              title={`Set initializer constructor argument row value ${field.name}`}
            >
              <Pencil className="h-3 w-3" />
              <span className="sr-only">Set initializer constructor argument row value {field.name}</span>
            </Button>
          </form>
          <DeleteIconButton
            label={`Clear initializer constructor argument ${field.name}`}
            onClick={() => onExec(`set_init_ctor_arg ${nodeInstance} ${field.name}`)}
          />
        </>
      )}
      <DeleteIconButton
        label={`Remove initializer field ${field.name}`}
        onClick={() => onExec(`unset_init ${nodeInstance} ${field.name}`)}
      />
    </div>
  )
}

function IntrinsicPropertyRow({
  property,
  field,
  nodeInstance,
  declaredTypes,
  onSourceRangeFocus,
  onExec,
}: {
  property: NodeFieldDef
  field?: InitializerField
  nodeInstance: string
  declaredTypes: DeclaredTypeDef[]
  onSourceRangeFocus?: (range: SourceRange, sourceFile?: string) => Promise<void> | void
  onExec: (cmd: string) => Promise<void> | void
}) {
  const overridden = Boolean(field)
  const propertyTypeDef = declaredTypes.find(typeDef => typeDef.name === property.type)
  const [valueDraft, setValueDraft] = useState(field?.value ?? '')

  useEffect(() => {
    setValueDraft(field?.value ?? '')
  }, [field?.value])

  const submitValue = async (e: FormEvent) => {
    e.preventDefault()
    const value = valueDraft.trim()
    if (!value || value === (field?.value ?? '')) return
    await onExec(`set_init ${nodeInstance} ${property.name} ${quoteCommandArg(value)}`)
  }

  return (
    <div
      className="space-y-1 rounded-md border border-border/35 bg-background/25 p-1.5"
      data-node-intrinsic-property-row={property.name}
    >
      <div className="flex min-w-0 items-center gap-1.5">
        <div
          className="h-2 w-2 shrink-0 rounded-full"
          style={{
            backgroundColor: pinColor(property.type, 'data'),
            boxShadow: `0 0 4px ${pinColor(property.type, 'data')}40`,
          }}
        />
        <span className="min-w-0 flex-1 truncate font-mono text-[11px] text-foreground/85">
          {property.name}
        </span>
        <Badge
          variant="outline"
          className="h-4 shrink-0 border-border/40 px-1 text-[8px] text-muted-foreground/60"
        >
          {overridden ? 'override' : 'default'}
        </Badge>
        {property.type && (
          <span className="max-w-[4.5rem] truncate font-mono text-[9px] text-muted-foreground/45">
            {property.type}
          </span>
        )}
        <SourceRangeButton
          range={property.name_source_range ?? property.source_range}
          sourceFile={property.source_file}
          label={`Locate source for intrinsic field ${property.name}`}
          sourceKey={`intrinsic-field-def-${nodeInstance}-${property.name}`}
          onSourceRangeFocus={onSourceRangeFocus}
        />
        <SourceRangeButton
          range={propertyTypeDef?.name_source_range ?? propertyTypeDef?.source_range}
          sourceFile={propertyTypeDef?.source_file}
          label={`Locate source for intrinsic field type ${property.name}`}
          sourceKey={`intrinsic-field-type-def-${nodeInstance}-${property.name}`}
          onSourceRangeFocus={onSourceRangeFocus}
        />
        {field && (
          <SourceRangeButton
            range={field.source_range}
            label={`Locate source for intrinsic field override ${property.name}`}
            sourceKey={`intrinsic-field-override-${nodeInstance}-${property.name}`}
            onSourceRangeFocus={onSourceRangeFocus}
          />
        )}
        {field && (
          <DeleteIconButton
            label={`Reset intrinsic field ${property.name}`}
            onClick={() => onExec(`unset_init ${nodeInstance} ${property.name}`)}
          />
        )}
      </div>
      <form className="flex gap-1.5" onSubmit={submitValue}>
        <Input
          aria-label={`Intrinsic field ${property.name} value for ${nodeInstance}`}
          value={valueDraft}
          onChange={e => setValueDraft(e.target.value)}
          placeholder={property.default || 'inherited'}
          className="h-7 min-w-0 px-2 text-[11px] font-mono"
          data-node-intrinsic-property-input={property.name}
        />
        <Button
          type="submit"
          variant="secondary"
          size="icon"
          disabled={!valueDraft.trim() || valueDraft.trim() === (field?.value ?? '')}
          className="h-7 w-7 shrink-0"
          title={`Set intrinsic field ${property.name}`}
        >
          <Pencil className="h-3.5 w-3.5" />
          <span className="sr-only">Set intrinsic field {property.name}</span>
        </Button>
      </form>
    </div>
  )
}

function NodeInfo({
  node,
  state,
  onSourceRangeFocus,
  onExec,
}: {
  node: NodeInst
  state: GSState
  onSourceRangeFocus?: (range: SourceRange, sourceFile?: string) => Promise<void> | void
  onExec: (cmd: string) => Promise<void> | void
}) {
  const def = state.types.find(t => t.type_name === node.type)
  const [initExpression, setInitExpression] = useState(node.init ?? '')
  const [initFieldName, setInitFieldName] = useState('')
  const [initFieldValue, setInitFieldValue] = useState('')
  const [initCtorFieldName, setInitCtorFieldName] = useState('')
  const [initCtorType, setInitCtorType] = useState('')
  const [initCtorArg, setInitCtorArg] = useState('')
  const [initCtorArgFieldName, setInitCtorArgFieldName] = useState('')
  const [initCtorArgValue, setInitCtorArgValue] = useState('')
  const [initCtorTypeFieldName, setInitCtorTypeFieldName] = useState('')
  const [initCtorTypeValue, setInitCtorTypeValue] = useState('')
  const [renameInitOldName, setRenameInitOldName] = useState('')
  const [renameInitNewName, setRenameInitNewName] = useState('')

  useEffect(() => {
    setInitExpression(node.init ?? '')
  }, [node.init])

  const submitInitializerExpression = async (e: FormEvent) => {
    e.preventDefault()
    const expression = initExpression.trim()
    if (expression === (node.init ?? '')) return
    await onExec(`set_init_expr ${node.instance}${expression ? ` ${quoteCommandArg(expression)}` : ''}`)
  }

  const submitInitializerField = async (e: FormEvent) => {
    e.preventDefault()
    const fieldName = initFieldName.trim()
    const fieldValue = initFieldValue.trim()
    if (!fieldName || !fieldValue) return
    await onExec(`set_init ${node.instance} ${fieldName} ${quoteCommandArg(fieldValue)}`)
    setInitFieldName('')
    setInitFieldValue('')
  }

  const submitInitializerConstructor = async (e: FormEvent) => {
    e.preventDefault()
    const fieldName = initCtorFieldName.trim()
    const ctorType = initCtorType.trim()
    const ctorArg = initCtorArg.trim()
    if (!fieldName || !ctorType) return
    await onExec(`set_init_ctor ${node.instance} ${fieldName} ${ctorType}${ctorArg ? ` ${quoteCommandArg(ctorArg)}` : ''}`)
    setInitCtorFieldName('')
    setInitCtorType('')
    setInitCtorArg('')
  }

  const submitInitializerConstructorArgument = async (e: FormEvent) => {
    e.preventDefault()
    const fieldName = initCtorArgFieldName.trim()
    const argument = initCtorArgValue.trim()
    if (!fieldName) return
    await onExec(`set_init_ctor_arg ${node.instance} ${fieldName}${argument ? ` ${quoteCommandArg(argument)}` : ''}`)
    setInitCtorArgFieldName('')
    setInitCtorArgValue('')
  }

  const submitInitializerConstructorType = async (e: FormEvent) => {
    e.preventDefault()
    const fieldName = initCtorTypeFieldName.trim()
    const typeName = initCtorTypeValue.trim()
    if (!fieldName || !typeName) return
    await onExec(`set_init_ctor_type ${node.instance} ${fieldName} ${typeName}`)
    setInitCtorTypeFieldName('')
    setInitCtorTypeValue('')
  }

  const submitRenameInitializerField = async (e: FormEvent) => {
    e.preventDefault()
    const oldName = renameInitOldName.trim()
    const newName = renameInitNewName.trim()
    if (!oldName || !newName) return
    await onExec(`rename_init ${node.instance} ${oldName} ${newName}`)
    setRenameInitOldName('')
    setRenameInitNewName('')
  }

  const nodeInitializerConstructorTypeDef = (state.declared_types ?? [])
    .find(typeDef => typeDef.name === constructorTypeName(node.init))
  const intrinsicFields = def?.fields && def.fields.length > 0
    ? def.fields
    : (def?.pins ?? [])
      .filter(pin => pin.kind === 'data' && pin.direction === 'in')
      .map(pin => ({
        name: pin.name,
        type: pin.type,
        default: '',
        annotations: pin.annotations,
        source_file: pin.source_file,
        source_range: pin.source_range,
        name_source_range: pin.name_source_range,
        type_source_range: pin.type_source_range,
      }))
  const initializerFieldByName = new Map((node.initializer_fields ?? []).map(field => [field.name, field]))

  return (
    <div className="space-y-3 panel-enter" data-node-instance={node.instance}>
      <div className="flex items-start gap-2">
        <div className="min-w-0 flex-1 space-y-0.5">
          <PropRow label="Instance" value={node.instance} mono />
          <PropRow label="Type" value={node.type} mono />
          {node.init && <PropRow label="Init" value={node.init} mono />}
        </div>
        <SourceRangeButton
          range={node.source_range}
          label={`Locate source for node ${node.instance}`}
          sourceKey={`node-${node.instance}`}
          onSourceRangeFocus={onSourceRangeFocus}
        />
        <SourceRangeButton
          range={node.instance_source_range}
          label={`Locate source for node instance ${node.instance}`}
          sourceKey={`node-instance-${node.instance}`}
          onSourceRangeFocus={onSourceRangeFocus}
        />
        <SourceRangeButton
          range={node.type_source_range}
          label={`Locate source for node ${node.instance} type`}
          sourceKey={`node-type-${node.instance}`}
          onSourceRangeFocus={onSourceRangeFocus}
        />
        <SourceRangeButton
          range={def?.name_source_range ?? def?.source_range}
          sourceFile={def?.source_file}
          label={`Locate source for node type definition ${node.type}`}
          sourceKey={`node-type-def-${node.instance}`}
          onSourceRangeFocus={onSourceRangeFocus}
        />
        <SourceRangeButton
          range={node.init_source_range}
          label={`Locate source for node ${node.instance} initializer`}
          sourceKey={`node-init-${node.instance}`}
          onSourceRangeFocus={onSourceRangeFocus}
        />
        <SourceRangeButton
          range={node.init_constructor_source_range}
          label={`Locate source for node ${node.instance} initializer constructor`}
          sourceKey={`node-init-constructor-${node.instance}`}
          onSourceRangeFocus={onSourceRangeFocus}
        />
        <SourceRangeButton
          range={node.init_constructor_type_source_range}
          label={`Locate source for node ${node.instance} initializer constructor type`}
          sourceKey={`node-init-constructor-type-${node.instance}`}
          onSourceRangeFocus={onSourceRangeFocus}
        />
        <SourceRangeButton
          range={nodeInitializerConstructorTypeDef?.name_source_range ?? nodeInitializerConstructorTypeDef?.source_range}
          sourceFile={nodeInitializerConstructorTypeDef?.source_file}
          label={`Locate source for node ${node.instance} initializer constructor type definition`}
          sourceKey={`node-init-constructor-type-def-${node.instance}`}
          onSourceRangeFocus={onSourceRangeFocus}
        />
        <SourceRangeButton
          range={node.init_constructor_arg_source_range}
          label={`Locate source for node ${node.instance} initializer constructor argument`}
          sourceKey={`node-init-constructor-arg-${node.instance}`}
          onSourceRangeFocus={onSourceRangeFocus}
        />
      </div>

      <Separator className="opacity-50" />
      <PanelSection title="Rename Node">
        <RenameForm
          label="Rename node"
          placeholder="new node name"
          currentName={node.instance}
          onRename={name => onExec(`rename_node ${node.instance} ${name}`)}
        />
      </PanelSection>

      <Separator className="opacity-50" />
      <PanelSection title="Set Initializer Expression">
        <form className="space-y-1.5" data-node-initializer-expression-form={node.instance} onSubmit={submitInitializerExpression}>
          <div className="flex gap-1.5">
            <Input
              aria-label={`Initializer expression for ${node.instance}`}
              value={initExpression}
              onChange={e => setInitExpression(e.target.value)}
              placeholder="initializer"
              className="h-7 px-2 text-[11px] font-mono"
            />
            <Button
              type="submit"
              variant="secondary"
              size="icon"
              disabled={initExpression.trim() === (node.init ?? '')}
              className="h-7 w-7 shrink-0"
              title={`Set initializer expression for ${node.instance}`}
            >
              <Pencil className="h-3.5 w-3.5" />
              <span className="sr-only">Set initializer expression</span>
            </Button>
          </div>
        </form>
      </PanelSection>

      {intrinsicFields.length > 0 && (
        <>
          <Separator className="opacity-50" />
          <PanelSection title="Intrinsic Fields">
            <div className="space-y-1.5">
              {intrinsicFields.map(property => (
                <IntrinsicPropertyRow
                  key={property.name}
                  property={property}
                  field={initializerFieldByName.get(property.name)}
                  nodeInstance={node.instance}
                  declaredTypes={state.declared_types ?? []}
                  onSourceRangeFocus={onSourceRangeFocus}
                  onExec={onExec}
                />
              ))}
            </div>
          </PanelSection>
        </>
      )}

      <Separator className="opacity-50" />
      <PanelSection title="Set Initializer Field">
        <form className="space-y-1.5" data-node-initializer-form={node.instance} onSubmit={submitInitializerField}>
          <div className="flex gap-1.5">
            <Input
              aria-label={`Initializer field for ${node.instance}`}
              value={initFieldName}
              onChange={e => setInitFieldName(e.target.value)}
              placeholder="field"
              className="h-7 px-2 text-[11px] font-mono"
            />
            <Button
              type="submit"
              variant="secondary"
              size="icon"
              disabled={!initFieldName.trim() || !initFieldValue.trim()}
              className="h-7 w-7 shrink-0"
              title={`Set initializer field for ${node.instance}`}
            >
              <Pencil className="h-3.5 w-3.5" />
              <span className="sr-only">Set initializer field</span>
            </Button>
          </div>
          <Input
            aria-label={`Initializer value for ${node.instance}`}
            value={initFieldValue}
            onChange={e => setInitFieldValue(e.target.value)}
            placeholder="value"
            className="h-7 px-2 text-[11px] font-mono"
          />
        </form>
      </PanelSection>

      <Separator className="opacity-50" />
      <PanelSection title="Set Initializer Constructor">
        <form className="space-y-1.5" data-node-initializer-constructor-form={node.instance} onSubmit={submitInitializerConstructor}>
          <div className="flex gap-1.5">
            <Input
              aria-label={`Initializer constructor field for ${node.instance}`}
              value={initCtorFieldName}
              onChange={e => setInitCtorFieldName(e.target.value)}
              placeholder="field"
              className="h-7 px-2 text-[11px] font-mono"
            />
            <Button
              type="submit"
              variant="secondary"
              size="icon"
              disabled={!initCtorFieldName.trim() || !initCtorType.trim()}
              className="h-7 w-7 shrink-0"
              title={`Set initializer constructor for ${node.instance}`}
            >
              <Pencil className="h-3.5 w-3.5" />
              <span className="sr-only">Set initializer constructor</span>
            </Button>
          </div>
          <Input
            aria-label={`Initializer constructor type for ${node.instance}`}
            value={initCtorType}
            onChange={e => setInitCtorType(e.target.value)}
            placeholder="Type"
            className="h-7 px-2 text-[11px] font-mono"
          />
          <Input
            aria-label={`Initializer constructor argument for ${node.instance}`}
            value={initCtorArg}
            onChange={e => setInitCtorArg(e.target.value)}
            placeholder="argument"
            className="h-7 px-2 text-[11px] font-mono"
          />
        </form>
      </PanelSection>

      <Separator className="opacity-50" />
      <PanelSection title="Set Constructor Argument">
        <form className="space-y-1.5" data-node-initializer-constructor-argument-form={node.instance} onSubmit={submitInitializerConstructorArgument}>
          <div className="flex gap-1.5">
            <Input
              aria-label={`Initializer constructor argument field for ${node.instance}`}
              value={initCtorArgFieldName}
              onChange={e => setInitCtorArgFieldName(e.target.value)}
              placeholder="field"
              className="h-7 px-2 text-[11px] font-mono"
            />
            <Button
              type="submit"
              variant="secondary"
              size="icon"
              disabled={!initCtorArgFieldName.trim()}
              className="h-7 w-7 shrink-0"
              title={`Set initializer constructor argument for ${node.instance}`}
            >
              <Pencil className="h-3.5 w-3.5" />
              <span className="sr-only">Set initializer constructor argument</span>
            </Button>
          </div>
          <Input
            aria-label={`Initializer constructor argument value for ${node.instance}`}
            value={initCtorArgValue}
            onChange={e => setInitCtorArgValue(e.target.value)}
            placeholder="argument"
            className="h-7 px-2 text-[11px] font-mono"
          />
        </form>
      </PanelSection>

      <Separator className="opacity-50" />
      <PanelSection title="Set Constructor Type">
        <form className="space-y-1.5" data-node-initializer-constructor-type-form={node.instance} onSubmit={submitInitializerConstructorType}>
          <div className="flex gap-1.5">
            <Input
              aria-label={`Initializer constructor type field for ${node.instance}`}
              value={initCtorTypeFieldName}
              onChange={e => setInitCtorTypeFieldName(e.target.value)}
              placeholder="field"
              className="h-7 px-2 text-[11px] font-mono"
            />
            <Button
              type="submit"
              variant="secondary"
              size="icon"
              disabled={!initCtorTypeFieldName.trim() || !initCtorTypeValue.trim()}
              className="h-7 w-7 shrink-0"
              title={`Set initializer constructor type for ${node.instance}`}
            >
              <Pencil className="h-3.5 w-3.5" />
              <span className="sr-only">Set initializer constructor type</span>
            </Button>
          </div>
          <Input
            aria-label={`Initializer constructor type value for ${node.instance}`}
            value={initCtorTypeValue}
            onChange={e => setInitCtorTypeValue(e.target.value)}
            placeholder="Type"
            className="h-7 px-2 text-[11px] font-mono"
          />
        </form>
      </PanelSection>

      <Separator className="opacity-50" />
      <PanelSection title="Rename Initializer Field">
        <form className="space-y-1.5" data-node-initializer-rename-form={node.instance} onSubmit={submitRenameInitializerField}>
          <div className="flex gap-1.5">
            <Input
              aria-label={`Initializer old field for ${node.instance}`}
              value={renameInitOldName}
              onChange={e => setRenameInitOldName(e.target.value)}
              placeholder="old field"
              className="h-7 px-2 text-[11px] font-mono"
            />
            <Button
              type="submit"
              variant="secondary"
              size="icon"
              disabled={!renameInitOldName.trim() || !renameInitNewName.trim()}
              className="h-7 w-7 shrink-0"
              title={`Rename initializer field for ${node.instance}`}
            >
              <Pencil className="h-3.5 w-3.5" />
              <span className="sr-only">Rename initializer field</span>
            </Button>
          </div>
          <Input
            aria-label={`Initializer new field for ${node.instance}`}
            value={renameInitNewName}
            onChange={e => setRenameInitNewName(e.target.value)}
            placeholder="new field"
            className="h-7 px-2 text-[11px] font-mono"
          />
        </form>
      </PanelSection>

      <Separator className="opacity-50" />
      <PanelSection title="Node Annotations">
        <AnnotationPanel
          annotations={node.annotations}
          annotateCommand={`annotate node ${node.instance}`}
          unannotateCommand={`unannotate node ${node.instance}`}
          onExec={onExec}
          onSourceRangeFocus={onSourceRangeFocus}
          sourceKeyPrefix={`node-${node.instance}`}
          declaredTypes={state.declared_types ?? []}
        />
      </PanelSection>

      {def && def.annotations.length > 0 && (
        <>
          <Separator className="opacity-50" />
          <PanelSection title="Node Type Annotations">
            <AnnotationPanel
              annotations={def.annotations}
              readOnly
              onSourceRangeFocus={onSourceRangeFocus}
              sourceKeyPrefix={`node-type-${node.instance}`}
              sourceFile={def.source_file}
              declaredTypes={state.declared_types ?? []}
            />
          </PanelSection>
        </>
      )}

      {(node.initializer_fields?.length ?? 0) > 0 && (
        <>
          <Separator className="opacity-50" />
          <PanelSection title="Initializer Fields">
            <div className="space-y-1">
              {node.initializer_fields?.map(field => (
                <InitializerFieldRow
                  key={field.id ?? `${field.name}-${field.value}`}
                  field={field}
                  nodeInstance={node.instance}
                  fieldPinDef={def?.pins.find(pin => pin.name === field.name)}
                  declaredTypes={state.declared_types ?? []}
                  onSourceRangeFocus={onSourceRangeFocus}
                  onExec={onExec}
                />
              ))}
            </div>
          </PanelSection>
        </>
      )}

      {def && def.pins.length > 0 && (
        <>
          <Separator className="opacity-50" />
          <div>
            <div className="text-[9px] uppercase text-muted-foreground/50 font-bold tracking-[0.1em] mb-1.5">
              Pins
            </div>
            <div className="space-y-0.5">
              {def.pins.map(p => {
                const pinTypeDef = (state.declared_types ?? []).find(typeDef => typeDef.name === p.type)
                return (
                  <div key={`${p.direction}-${p.name}`} className="space-y-1 py-0.5">
                    <div className="flex items-center gap-1.5">
                      <div
                        className="w-2 h-2 rounded-full shrink-0"
                        style={{
                          backgroundColor: pinColor(p.type, p.kind),
                          boxShadow: `0 0 4px ${pinColor(p.type, p.kind)}40`,
                        }}
                      />
                      <Badge variant="outline"
                        className="text-[8px] px-1 py-0 h-3.5 border-border/50 text-muted-foreground/60"
                      >
                        {p.direction}
                      </Badge>
                      <span className="text-[11px] font-mono text-foreground/80">{p.name}</span>
                      <SourceRangeButton
                        range={p.name_source_range}
                        sourceFile={p.source_file}
                        label={`Locate source for pin ${p.name} name`}
                        sourceKey={`node-pin-name-${node.instance}-${p.name}`}
                        onSourceRangeFocus={onSourceRangeFocus}
                      />
                      {p.type && (
                        <span className="text-[9px] text-muted-foreground/40 ml-auto font-mono">{p.type}</span>
                      )}
                      <SourceRangeButton
                        range={p.type_source_range}
                        sourceFile={p.source_file}
                        label={`Locate source for pin ${p.name} type`}
                        sourceKey={`node-pin-type-${node.instance}-${p.name}`}
                        onSourceRangeFocus={onSourceRangeFocus}
                      />
                      <SourceRangeButton
                        range={pinTypeDef?.name_source_range ?? pinTypeDef?.source_range}
                        sourceFile={pinTypeDef?.source_file}
                        label={`Locate source for pin ${p.name} type definition`}
                        sourceKey={`node-pin-type-def-${node.instance}-${p.name}`}
                        onSourceRangeFocus={onSourceRangeFocus}
                      />
                      <SourceRangeButton
                        range={p.source_range}
                        sourceFile={p.source_file}
                        label={`Locate source for pin ${p.name}`}
                        sourceKey={`node-pin-${node.instance}-${p.name}`}
                        onSourceRangeFocus={onSourceRangeFocus}
                      />
                    </div>
                    <AnnotationPanel
                      annotations={p.annotations}
                      readOnly
                      onSourceRangeFocus={onSourceRangeFocus}
                      sourceKeyPrefix={`node-pin-${node.instance}-${p.name}`}
                      sourceFile={p.source_file}
                      declaredTypes={state.declared_types ?? []}
                    />
                  </div>
                )
              })}
            </div>
          </div>
        </>
      )}
    </div>
  )
}

export default function PropertiesPanel({
  state,
  graphIndex,
  selectedNode,
  selectedEdge = null,
  diagnostics = [],
  focusedDiagnostic = null,
  onSourceRangeFocus,
  onEdgeRedirected,
  onExec,
}: PropertiesPanelProps) {
  const graph = state?.module.graphs[graphIndex]
  const node = graph?.nodes.find(n => n.instance === selectedNode)

  return (
    <div className="h-full min-h-0 overflow-auto" style={{ contain: 'layout paint' }}>
      {/* Header */}
      <div className="flex items-center gap-2 px-3 py-2 border-b">
        <Info className="h-3.5 w-3.5 text-primary/70" />
        <span className="text-[11px] font-semibold text-muted-foreground uppercase tracking-wider">
          {node ? 'Node' : 'Graph'}
        </span>
      </div>

      {/* Content */}
      <div className="p-3">
        {!state || !graph ? (
          <div className="text-[11px] text-muted-foreground/40 text-center py-8">
            No data
          </div>
        ) : node ? (
          <NodeInfo
            node={node}
            state={state}
            onSourceRangeFocus={onSourceRangeFocus}
            onExec={onExec}
          />
        ) : (
          <GraphInfo
            graph={graph}
            imports={state.module.imports}
            lets={state.module.lets}
            nodeTypes={state.types}
            declarationNodeTypes={state.types.filter(typeDef => Boolean(typeDef.source_file))}
            declaredTypes={state.declared_types ?? []}
            schemas={state.schemas}
            selectedEdge={selectedEdge}
            diagnostics={diagnostics}
            focusedDiagnostic={focusedDiagnostic}
            onSourceRangeFocus={onSourceRangeFocus}
            onEdgeRedirected={onEdgeRedirected}
            onExec={onExec}
          />
        )}
      </div>
    </div>
  )
}
