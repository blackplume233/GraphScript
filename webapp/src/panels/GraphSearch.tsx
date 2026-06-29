import { type FormEvent, type ReactNode, useEffect, useMemo, useState } from 'react'
import { Search, X } from 'lucide-react'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import type { Annotation, Diagnostic, GSState, SourceRange } from '@/api/types'

type SearchKind = 'graph' | 'node' | 'pin' | 'block' | 'connection' | 'declaration' | 'source_reference' | 'diagnostic'

export interface GraphSearchResult {
  kind: SearchKind
  graphIndex: number
  graphName?: string
  score?: number
  matchMode?: 'exact' | 'typo' | 'fuzzy'
  label: string
  detail: string
  nodeInstance?: string
  pinName?: string
  schemaFieldName?: string
  parameterName?: string
  connectionKind?: string
  connectionId?: string
  persistentId?: string
  sourceNode?: string
  sourcePin?: string
  targetNode?: string
  targetPin?: string
  blockKind?: 'event' | 'function'
  blockName?: string
  declarationKind?: 'type' | 'node' | 'pin' | 'schema' | 'schema_field'
  declarationOwner?: string
  sourceReferenceKind?: 'type' | 'node_type' | 'schema' | 'constructor_type' | 'flow_endpoint' | 'link_endpoint'
  sourceReferenceSymbol?: string
  sourceFile?: string
  sourceRange?: SourceRange
  sourceEndpointRange?: SourceRange
  targetEndpointRange?: SourceRange
  annotations?: Annotation[]
  diagnostic?: Diagnostic
}

interface GraphSearchProps {
  state: GSState | null
  graphIndex: number
  diagnostics: Diagnostic[]
  onNavigate: (result: GraphSearchResult) => Promise<void> | void
}

function normalize(text: string): string {
  return text.toLowerCase()
}

function sourceLocatorLabelNeedle(locator: string): string {
  const variants = [
    locator.match(/^(.+)(\(\d+(?:,\d+)?\))$/),
    locator.match(/^(.+)(#L\d+(?:-L\d+)?)$/i),
    locator.match(/^(.+)(:\d+(?::\d+(?:-\d+:\d+)?)?)$/),
  ]
  const match = variants.find(Boolean)
  if (!match) return ''
  const [, file, suffix] = match
  const cleanFile = file.replace(/^["'`]/, '').replace(/["'`]$/, '')
  const fileLabel = sourceFileLabel(cleanFile)
  return fileLabel === cleanFile && cleanFile === file ? '' : `${fileLabel}${suffix}`
}

function queryNeedles(query: string): string[] {
  const needles = new Set<string>()
  const trimmed = normalize(query.trim())
  if (trimmed) needles.add(trimmed)

  const locatorPatterns = [
    /"[^"]+"\(\d+(?:,\d+)?\)/g,
    /"[^"]+"#L\d+(?:-L\d+)?/gi,
    /"[^"]+":\d+(?::\d+(?:-\d+:\d+)?)?/g,
    /'[^']+'\(\d+(?:,\d+)?\)/g,
    /'[^']+'#L\d+(?:-L\d+)?/gi,
    /'[^']+':\d+(?::\d+(?:-\d+:\d+)?)?/g,
    /\S+\(\d+(?:,\d+)?\)/g,
    /\S+#L\d+(?:-L\d+)?/gi,
    /\S+:\d+(?::\d+(?:-\d+:\d+)?)?/g,
  ]

  for (const pattern of locatorPatterns) {
    for (const match of query.matchAll(pattern)) {
      const locator = normalize(match[0].trim())
      if (locator) needles.add(locator)
      const labelLocator = sourceLocatorLabelNeedle(locator)
      if (labelLocator) needles.add(labelLocator)
    }
  }

  return Array.from(needles)
}

function isDefaultRange(diagnostic: Diagnostic): boolean {
  return diagnostic.range.start.line === 1 &&
    diagnostic.range.start.column === 1 &&
    diagnostic.range.end.line === 1 &&
    diagnostic.range.end.column === 1
}

function formatRange(diagnostic: Diagnostic): string {
  const { start, end } = diagnostic.range
  if (start.line === end.line && start.column === end.column) {
    return `${start.line}:${start.column}`
  }
  return `${start.line}:${start.column}-${end.line}:${end.column}`
}

function diagnosticRangeText(diagnostic?: Diagnostic): string {
  if (!diagnostic || isDefaultRange(diagnostic)) return ''
  return formatRange(diagnostic)
}

function sourceRangeText(range?: SourceRange): string {
  if (!range) return ''
  const { start, end } = range
  if (start.line === end.line && start.column === end.column) {
    return `${start.line}:${start.column}`
  }
  return `${start.line}:${start.column}-${end.line}:${end.column}`
}

function resultSourceRangeText(result: GraphSearchResult): string {
  return [
    sourceRangeText(result.sourceRange),
    sourceRangeText(result.sourceEndpointRange),
    sourceRangeText(result.targetEndpointRange),
  ].filter(Boolean).join(' ')
}

function sourceFilesForLocator(sourceFile: string): string[] {
  const fileLabel = sourceFileLabel(sourceFile)
  return sourceFile === fileLabel ? [sourceFile] : [sourceFile, fileLabel]
}

function sourceLocatorSpanTextsForRange(sourceFile: string, range?: SourceRange): string[] {
  const rangeText = sourceRangeText(range)
  if (!rangeText) return []
  return sourceFilesForLocator(sourceFile).map(file => `${file}:${rangeText}`)
}

function sourceLocatorTextsForRange(sourceFile: string, range?: SourceRange): string[] {
  if (!range) return []
  const { start, end } = range
  const spans = sourceLocatorSpanTextsForRange(sourceFile, range)
  return sourceFilesForLocator(sourceFile).flatMap(file => [
    `${file}:${start.line}`,
    `${file}:${start.line}:${start.column}`,
    `${file}(${start.line})`,
    `${file}(${start.line},${start.column})`,
    `${file}#L${start.line}`,
    `${file}#L${start.line}-L${end.line}`,
  ]).concat(spans)
}

function resultSourceLocatorSpanTexts(result: GraphSearchResult): string[] {
  if (!result.sourceFile) return []
  return [
    ...sourceLocatorSpanTextsForRange(result.sourceFile, result.sourceRange),
    ...sourceLocatorSpanTextsForRange(result.sourceFile, result.sourceEndpointRange),
    ...sourceLocatorSpanTextsForRange(result.sourceFile, result.targetEndpointRange),
  ]
}

function resultSourceLocatorTexts(result: GraphSearchResult): string[] {
  if (!result.sourceFile) return []
  return [
    ...sourceLocatorTextsForRange(result.sourceFile, result.sourceRange),
    ...sourceLocatorTextsForRange(result.sourceFile, result.sourceEndpointRange),
    ...sourceLocatorTextsForRange(result.sourceFile, result.targetEndpointRange),
  ]
}

function resultSourceLocatorText(result: GraphSearchResult): string {
  return resultSourceLocatorTexts(result).join(' ')
}

function resultWithSourceRangeDetail(result: GraphSearchResult): GraphSearchResult {
  const rangeText = resultSourceRangeText(result)
  if (!rangeText) return result
  const sourceLocatorText = resultSourceLocatorSpanTexts(result)[0]
  const rangeDetail = `range ${rangeText}`
  const locatorDetail = sourceLocatorText ? `at ${sourceLocatorText}` : ''
  if (result.detail.includes(rangeDetail)) return result
  return {
    ...result,
    detail: [result.detail, rangeDetail, locatorDetail].filter(Boolean).join(' / '),
  }
}

function hasSourceRange(range?: SourceRange): range is SourceRange {
  return Boolean(range)
}

function sourceFileLabel(sourceFile: string): string {
  return sourceFile.split(/[\\/]/).pop() || sourceFile
}

function constructorTypeName(value?: string): string {
  return value?.match(/^\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(/)?.[1] ?? ''
}

function resultText(result: GraphSearchResult): string {
  return normalize([
    result.kind,
    result.graphName,
    result.label,
    result.detail,
    result.nodeInstance,
    result.pinName,
    result.schemaFieldName,
    result.parameterName,
    result.connectionKind,
    result.connectionId,
    result.persistentId,
    result.sourceNode,
    result.sourcePin,
    result.targetNode,
    result.targetPin,
    result.blockKind,
    result.blockName,
    result.declarationKind,
    result.declarationOwner,
    result.sourceReferenceKind,
    result.sourceReferenceSymbol,
    result.sourceFile,
    resultSourceRangeText(result),
    resultSourceLocatorText(result),
    result.diagnostic?.code,
    result.diagnostic?.message,
    result.diagnostic?.context,
    result.diagnostic?.target?.reference,
    result.diagnostic?.target?.parameter_name,
    result.diagnostic?.target?.pin_name,
    result.diagnostic?.target?.block_kind,
    result.diagnostic?.target?.block_name,
    result.diagnostic?.target?.connection_kind,
    diagnosticRangeText(result.diagnostic),
  ].filter(Boolean).join(' '))
}

function diagnosticNodeCandidate(state: GSState, graphIndex: number, diagnostic: Diagnostic): string | undefined {
  const graph = state.module.graphs[graphIndex]
  if (!graph) return undefined
  const target = diagnostic.target
  const candidates = [
    target?.node_instance,
    target?.reference,
    diagnostic.context,
  ]
  return candidates.find(candidate =>
    !!candidate && graph.nodes.some(node => node.instance === candidate))
}

function diagnosticTargetDetail(
  diagnostic: Diagnostic,
  nodeInstance?: string,
  parameterName?: string,
): string {
  const target = diagnostic.target
  const parts: string[] = []
  const reference = target?.reference
  const blockKind = target?.block_kind === 'event' || target?.block_kind === 'function'
    ? target.block_kind
    : ''
  const blockName = target?.block_name
  const pinName = target?.pin_name
  const connectionKind = target?.connection_kind

  if (reference) parts.push(`at ${reference}`)
  if (parameterName) parts.push(`param ${parameterName}`)
  if (blockKind && blockName) parts.push(`${blockKind} ${blockName}`)
  if (pinName) parts.push(`pin ${nodeInstance ? `${nodeInstance}.${pinName}` : pinName}`)
  if (connectionKind) parts.push(connectionKind)
  if (parts.length === 0 && diagnostic.context) parts.push(`in ${diagnostic.context}`)
  return parts.join(' / ')
}

function fuzzyMatchDistance(text: string, query: string): number | null {
  const needle = normalize(query.trim())
  if (!needle) return null

  const haystack = normalize(text)
  let queryIndex = 0
  let firstMatch = -1
  let lastMatch = -1

  for (let index = 0; index < haystack.length && queryIndex < needle.length; index += 1) {
    if (haystack[index] !== needle[queryIndex]) continue
    if (firstMatch === -1) firstMatch = index
    lastMatch = index
    queryIndex += 1
  }

  if (queryIndex !== needle.length || firstMatch === -1) return null
  return (lastMatch - firstMatch + 1) - needle.length
}

function fuzzyMatchIndexes(text: string, query: string): number[] {
  const needle = normalize(query.trim())
  if (!needle) return []

  const haystack = normalize(text)
  const indexes: number[] = []
  let queryIndex = 0

  for (let index = 0; index < haystack.length && queryIndex < needle.length; index += 1) {
    if (haystack[index] !== needle[queryIndex]) continue
    indexes.push(index)
    queryIndex += 1
  }

  return queryIndex === needle.length ? indexes : []
}

function typoDistance(text: string, query: string): number | null {
  const source = normalize(text.trim())
  const target = normalize(query.trim())
  if (!source || !target || source === target) return null
  if (Math.abs(source.length - target.length) > 1) return null

  if (source.length === target.length) {
    const mismatches: number[] = []
    for (let index = 0; index < source.length; index += 1) {
      if (source[index] !== target[index]) mismatches.push(index)
      if (mismatches.length > 2) return null
    }
    if (mismatches.length === 1) return 1
    if (
      mismatches.length === 2 &&
      mismatches[1] === mismatches[0] + 1 &&
      source[mismatches[0]] === target[mismatches[1]] &&
      source[mismatches[1]] === target[mismatches[0]]
    ) return 1
    return null
  }

  const shorter = source.length < target.length ? source : target
  const longer = source.length < target.length ? target : source
  let shortIndex = 0
  let longIndex = 0
  let edits = 0
  while (shortIndex < shorter.length && longIndex < longer.length) {
    if (shorter[shortIndex] === longer[longIndex]) {
      shortIndex += 1
      longIndex += 1
      continue
    }
    edits += 1
    if (edits > 1) return null
    longIndex += 1
  }
  return 1
}

function typoMatchIndexes(text: string, query: string): number[] {
  const source = normalize(text.trim())
  const target = normalize(query.trim())
  if (typoDistance(source, target) === null) return []

  if (source.length === target.length) {
    const mismatches: number[] = []
    for (let index = 0; index < source.length; index += 1) {
      if (source[index] !== target[index]) mismatches.push(index)
    }
    return mismatches
  }

  const maxSharedLength = Math.min(source.length, target.length)
  for (let index = 0; index < maxSharedLength; index += 1) {
    if (source[index] === target[index]) continue
    return [Math.min(index, text.length - 1)]
  }

  return source.length > target.length ? [source.length - 1] : [Math.max(0, source.length - 1)]
}

function typoDistanceForResult(result: GraphSearchResult, query: string): number | null {
  const fields = [
    result.label,
    result.graphName,
    result.nodeInstance,
    result.pinName,
    result.schemaFieldName,
    result.parameterName,
    result.connectionKind,
    result.connectionId,
    result.persistentId,
    result.sourceNode,
    result.sourcePin,
    result.targetNode,
    result.targetPin,
    result.blockKind,
    result.blockName,
    result.declarationKind,
    result.declarationOwner,
    result.sourceReferenceKind,
    result.sourceReferenceSymbol,
    result.sourceFile,
  ].filter(Boolean) as string[]

  let best: number | null = null
  for (const field of fields) {
    const distance = typoDistance(field, query)
    if (distance === null) continue
    best = best === null ? distance : Math.min(best, distance)
  }
  return best
}

function matchesResult(result: GraphSearchResult, query: string): boolean {
  const needles = queryNeedles(query)
  if (needles.length === 0) return false
  return needles.some(needle => resultText(result).includes(needle)) ||
    typoDistanceForResult(result, query) !== null ||
    fuzzyMatchDistance(resultText(result), query) !== null
}

function matchModeForResult(result: GraphSearchResult, query: string): 'exact' | 'typo' | 'fuzzy' {
  if (queryNeedles(query).some(needle => resultText(result).includes(needle))) return 'exact'
  return typoDistanceForResult(result, query) !== null ? 'typo' : 'fuzzy'
}

function scoreResult(result: GraphSearchResult, query: string, currentGraphIndex: number): number {
  const needles = queryNeedles(query)
  const needle = needles[0] ?? ''
  if (!needle) return Number.MAX_SAFE_INTEGER

  const label = normalize(result.label)
  const detail = normalize(result.detail)
  const graphName = normalize(result.graphName ?? '')
  const nodeInstance = normalize(result.nodeInstance ?? '')
  const pinName = normalize(result.pinName ?? '')
  const schemaFieldName = normalize(result.schemaFieldName ?? '')
  const parameterName = normalize(result.parameterName ?? '')
  const connectionKind = normalize(result.connectionKind ?? '')
  const connectionId = normalize(result.connectionId ?? '')
  const persistentId = normalize(result.persistentId ?? '')
  const sourceNode = normalize(result.sourceNode ?? '')
  const sourcePin = normalize(result.sourcePin ?? '')
  const targetNode = normalize(result.targetNode ?? '')
  const targetPin = normalize(result.targetPin ?? '')
  const blockKind = normalize(result.blockKind ?? '')
  const blockName = normalize(result.blockName ?? '')
  const declarationKind = normalize(result.declarationKind ?? '')
  const declarationOwner = normalize(result.declarationOwner ?? '')
  const sourceReferenceKind = normalize(result.sourceReferenceKind ?? '')
  const sourceReferenceSymbol = normalize(result.sourceReferenceSymbol ?? '')
  const sourceFile = normalize(result.sourceFile ?? '')
  const sourceRange = normalize(resultSourceRangeText(result))
  const sourceLocator = normalize(resultSourceLocatorText(result))
  const sourceLocatorExact = resultSourceLocatorTexts(result).some(locator => needles.includes(normalize(locator)))
  const diagnosticText = normalize([
    result.diagnostic?.code,
    result.diagnostic?.message,
    result.diagnostic?.context,
    result.diagnostic?.target?.reference,
    result.diagnostic?.target?.parameter_name,
    result.diagnostic?.target?.pin_name,
    result.diagnostic?.target?.block_kind,
    result.diagnostic?.target?.block_name,
    result.diagnostic?.target?.connection_kind,
    diagnosticRangeText(result.diagnostic),
  ].filter(Boolean).join(' '))

  let score = 100
  if (label === needle) score = 0
  else if (sourceRange === needle || sourceLocatorExact) score = 2
  else if (
    (result.kind === 'graph' && graphName === needle) ||
    (result.kind === 'node' && nodeInstance === needle) ||
    (result.kind === 'pin' && pinName === needle) ||
    ((result.kind === 'diagnostic' || result.kind === 'connection') && (
      pinName === needle ||
      parameterName === needle ||
      connectionKind === needle ||
      connectionId === needle ||
      persistentId === needle ||
      sourceNode === needle ||
      sourcePin === needle ||
      targetNode === needle ||
      targetPin === needle ||
      sourceRange === needle ||
      blockKind === needle ||
      blockName === needle
    )) ||
    (result.kind === 'declaration' && label === needle) ||
    (result.kind === 'source_reference' && sourceReferenceSymbol === needle) ||
    (result.kind === 'block' && blockName === needle)
  ) score = 2
  else if (label.startsWith(needle)) score = 10
  else if (graphName.startsWith(needle) || nodeInstance.startsWith(needle) || pinName.startsWith(needle) || schemaFieldName.startsWith(needle) || parameterName.startsWith(needle) || connectionKind.startsWith(needle) || connectionId.startsWith(needle) || persistentId.startsWith(needle) || sourceNode.startsWith(needle) || sourcePin.startsWith(needle) || targetNode.startsWith(needle) || targetPin.startsWith(needle) || sourceRange.startsWith(needle) || sourceLocator.startsWith(needle) || blockKind.startsWith(needle) || blockName.startsWith(needle) || declarationKind.startsWith(needle) || declarationOwner.startsWith(needle) || sourceReferenceKind.startsWith(needle) || sourceReferenceSymbol.startsWith(needle) || sourceFile.startsWith(needle)) score = 12
  else if (detail.startsWith(needle)) score = 20
  else if (label.includes(needle)) score = 30
  else if (detail.includes(needle) || diagnosticText.includes(needle)) score = 40
  else if (typoDistanceForResult(result, query) !== null) score = 50 + (typoDistanceForResult(result, query) ?? 1) / 100
  else {
    const labelFuzzy = fuzzyMatchDistance(result.label, query)
    const primaryFuzzy = Math.min(
      fuzzyMatchDistance(result.graphName ?? '', query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(result.nodeInstance ?? '', query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(result.pinName ?? '', query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(result.schemaFieldName ?? '', query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(result.parameterName ?? '', query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(result.connectionKind ?? '', query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(result.connectionId ?? '', query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(result.persistentId ?? '', query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(result.sourceNode ?? '', query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(result.sourcePin ?? '', query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(result.targetNode ?? '', query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(result.targetPin ?? '', query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(sourceRange, query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(sourceLocator, query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(result.blockKind ?? '', query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(result.blockName ?? '', query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(result.declarationKind ?? '', query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(result.declarationOwner ?? '', query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(result.sourceReferenceKind ?? '', query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(result.sourceReferenceSymbol ?? '', query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(result.sourceFile ?? '', query) ?? Number.MAX_SAFE_INTEGER,
    )
    const detailFuzzy = Math.min(
      fuzzyMatchDistance(result.detail, query) ?? Number.MAX_SAFE_INTEGER,
      fuzzyMatchDistance(diagnosticText, query) ?? Number.MAX_SAFE_INTEGER,
    )
    if (labelFuzzy !== null) score = 60 + labelFuzzy / 100
    else if (primaryFuzzy !== Number.MAX_SAFE_INTEGER) score = 65 + primaryFuzzy / 100
    else if (detailFuzzy !== Number.MAX_SAFE_INTEGER) score = 70 + detailFuzzy / 100
  }

  const kindBias: Record<SearchKind, number> = {
    node: 0,
    graph: 1,
    pin: 2,
    block: 3,
    connection: 4,
    declaration: 5,
    source_reference: 6,
    diagnostic: 7,
  }
  const graphBias = result.graphIndex === currentGraphIndex ? -0.25 : 0
  return score + kindBias[result.kind] / 100 + graphBias
}

function highlightText(text: string, query: string): ReactNode {
  const needle = query.trim()
  if (!needle) return text

  const normalizedText = normalize(text)
  const normalizedNeedle = normalize(needle)
  const pieces: ReactNode[] = []
  let cursor = 0
  let matchIndex = normalizedText.indexOf(normalizedNeedle)
  let hasExactMatch = false

  while (matchIndex !== -1) {
    hasExactMatch = true
    if (matchIndex > cursor) {
      pieces.push(text.slice(cursor, matchIndex))
    }

    const end = matchIndex + normalizedNeedle.length
    pieces.push(
      <mark
        key={`${matchIndex}:${end}`}
        data-graph-search-match
        className="rounded bg-amber-200/80 px-0.5 text-foreground dark:bg-amber-400/30"
      >
        {text.slice(matchIndex, end)}
      </mark>,
    )
    cursor = end
    matchIndex = normalizedText.indexOf(normalizedNeedle, cursor)
  }

  if (hasExactMatch && cursor < text.length) {
    pieces.push(text.slice(cursor))
  }

  if (hasExactMatch) return pieces

  const typoIndexes = new Set(typoMatchIndexes(text, query))
  if (typoIndexes.size > 0) {
    return Array.from(text).map((char, index) => {
      if (!typoIndexes.has(index)) return char
      return (
        <mark
          key={`typo:${index}`}
          data-graph-search-typo-match
          className="rounded bg-rose-200/80 px-0.5 text-foreground dark:bg-rose-400/30"
        >
          {char}
        </mark>
      )
    })
  }

  const fuzzyIndexes = new Set(fuzzyMatchIndexes(text, query))
  if (fuzzyIndexes.size === 0) return text

  return Array.from(text).map((char, index) => {
    if (!fuzzyIndexes.has(index)) return char
    return (
      <mark
        key={`fuzzy:${index}`}
        data-graph-search-fuzzy-match
        className="rounded bg-sky-200/80 px-0.5 text-foreground dark:bg-sky-400/30"
      >
        {char}
      </mark>
    )
  })
}

interface SearchResultGroup {
  graphName: string
  items: Array<{
    result: GraphSearchResult
    index: number
  }>
}

export default function GraphSearch({
  state,
  graphIndex,
  diagnostics,
  onNavigate,
}: GraphSearchProps) {
  const [query, setQuery] = useState('')
  const [open, setOpen] = useState(false)
  const [activeIndex, setActiveIndex] = useState(-1)
  const hasGraphs = Boolean(state?.module.graphs.length)

  const results = useMemo(() => {
    if (!state) return []
    const typeByName = new Map(state.types.map(type => [type.type_name, type]))
    const graphIndexByName = new Map(state.module.graphs.map((item, index) => [item.name, index]))
    const candidates: GraphSearchResult[] = []
    const addCandidate = (result: GraphSearchResult) => {
      candidates.push(resultWithSourceRangeDetail(result))
    }
    const addSourceReference = (result: Omit<GraphSearchResult, 'kind'>) => {
      if (!hasSourceRange(result.sourceRange)) return
      addCandidate({
        kind: 'source_reference',
        ...result,
      })
    }
    const addDeclarationAnnotationReferences = (
      annotations: Annotation[] | undefined,
      sourceFile: string,
      ownerLabel: string,
      extra: Partial<Omit<GraphSearchResult, 'kind' | 'label' | 'detail' | 'sourceRange' | 'sourceReferenceKind' | 'sourceReferenceSymbol'>>,
    ) => {
      if (!sourceFile) return
      for (const annotation of annotations ?? []) {
        for (const arg of annotation.args) {
          const constructorType = constructorTypeName(arg.value)
          if (!constructorType) continue
          addSourceReference({
            graphIndex,
            graphName: 'Declarations',
            ...extra,
            label: `declare annotation ${ownerLabel} ${annotation.name}.${arg.name || 'arg'} ${constructorType}`,
            detail: `${sourceFileLabel(sourceFile)} / declaration annotation constructor type reference`,
            sourceReferenceKind: 'constructor_type',
            sourceReferenceSymbol: constructorType,
            sourceFile,
            sourceRange: arg.value_constructor_type_source_range,
          })
        }
      }
    }

    for (const letDecl of state.module.lets) {
      addSourceReference({
        graphIndex,
        graphName: 'Module',
        label: `let ${letDecl.name} type ${letDecl.type}`,
        detail: `Session / let ${letDecl.name} type reference`,
        sourceReferenceKind: 'type',
        sourceReferenceSymbol: letDecl.type,
        sourceRange: letDecl.type_source_range,
      })
      if (letDecl.constructor_source_range) {
        addSourceReference({
          graphIndex,
          graphName: 'Module',
          label: `let ${letDecl.name} constructor ${letDecl.type}`,
          detail: `Session / let ${letDecl.name} constructor type reference`,
          sourceReferenceKind: 'constructor_type',
          sourceReferenceSymbol: letDecl.type,
          sourceRange: letDecl.constructor_source_range,
        })
      }
    }

    state.module.graphs.forEach((graph, index) => {
      addCandidate({
        kind: 'graph',
        graphIndex: index,
        graphName: graph.name,
        label: graph.name,
        detail: `${graph.nodes.length} nodes, ${graph.events.length + graph.functions.length} blocks`,
        sourceRange: graph.name_source_range ?? graph.source_range,
      })

      if (graph.base_type) {
        addSourceReference({
          graphIndex: index,
          graphName: graph.name,
          label: `graph ${graph.name} base ${graph.base_type}`,
          detail: `${graph.name} / graph base schema reference`,
          sourceReferenceKind: 'schema',
          sourceReferenceSymbol: graph.base_type,
          sourceRange: graph.base_type_source_range,
        })
      }

      for (const param of graph.parameters) {
        addSourceReference({
          graphIndex: index,
          graphName: graph.name,
          label: `param ${param.name} type ${param.type}`,
          detail: `${graph.name} / parameter type reference`,
          parameterName: param.name,
          sourceReferenceKind: 'type',
          sourceReferenceSymbol: param.type,
          sourceRange: param.type_source_range,
        })
        const defaultConstructorType = constructorTypeName(param.default)
        if (defaultConstructorType) {
          addSourceReference({
            graphIndex: index,
            graphName: graph.name,
            label: `param ${param.name} default ${defaultConstructorType}`,
            detail: `${graph.name} / parameter default constructor type reference`,
            parameterName: param.name,
            sourceReferenceKind: 'constructor_type',
            sourceReferenceSymbol: defaultConstructorType,
            sourceRange: param.default_constructor_type_source_range,
          })
        }
      }

      for (const metadata of graph.generate?.metadata ?? []) {
        const metadataConstructorType = constructorTypeName(metadata.value)
        if (!metadataConstructorType) continue
        addSourceReference({
          graphIndex: index,
          graphName: graph.name,
          label: `metadata ${metadata.node}.${metadata.property} ${metadataConstructorType}`,
          detail: `${graph.name} / generate metadata constructor type reference`,
          nodeInstance: metadata.node,
          sourceReferenceKind: 'constructor_type',
          sourceReferenceSymbol: metadataConstructorType,
          sourceRange: metadata.value_constructor_type_source_range,
        })
      }

      for (const node of graph.nodes) {
        addCandidate({
          kind: 'node',
          graphIndex: index,
          graphName: graph.name,
          label: node.instance,
          detail: `${graph.name} / ${node.type} node`,
          nodeInstance: node.instance,
          sourceRange: node.instance_source_range ?? node.source_range,
        })

        addSourceReference({
          graphIndex: index,
          graphName: graph.name,
          label: `node ${node.instance} type ${node.type}`,
          detail: `${graph.name} / node type reference`,
          nodeInstance: node.instance,
          sourceReferenceKind: 'node_type',
          sourceReferenceSymbol: node.type,
          sourceRange: node.type_source_range,
        })
        const nodeInitializerConstructorType = constructorTypeName(node.init)
        if (nodeInitializerConstructorType) {
          addSourceReference({
            graphIndex: index,
            graphName: graph.name,
            label: `node ${node.instance} init ${nodeInitializerConstructorType}`,
            detail: `${graph.name} / node initializer constructor type reference`,
            nodeInstance: node.instance,
            sourceReferenceKind: 'constructor_type',
            sourceReferenceSymbol: nodeInitializerConstructorType,
            sourceRange: node.init_constructor_type_source_range,
          })
        }
        for (const field of node.initializer_fields ?? []) {
          const fieldConstructorType = constructorTypeName(field.value)
          if (!fieldConstructorType) continue
          addSourceReference({
            graphIndex: index,
            graphName: graph.name,
            label: `node ${node.instance}.${field.name} value ${fieldConstructorType}`,
            detail: `${graph.name} / initializer field constructor type reference`,
            nodeInstance: node.instance,
            pinName: field.name,
            sourceReferenceKind: 'constructor_type',
            sourceReferenceSymbol: fieldConstructorType,
            sourceRange: field.value_constructor_type_source_range,
          })
        }

        const nodeType = typeByName.get(node.type)
        for (const pin of nodeType?.pins ?? []) {
          addCandidate({
            kind: 'pin',
            graphIndex: index,
            graphName: graph.name,
            label: `${node.instance}.${pin.name}`,
            detail: `${graph.name} / ${pin.direction} ${pin.kind}${pin.type ? ` ${pin.type}` : ''}`,
            nodeInstance: node.instance,
            pinName: pin.name,
          })
        }
      }

      for (const block of [...graph.events, ...graph.functions]) {
        addCandidate({
          kind: 'block',
          graphIndex: index,
          graphName: graph.name,
          label: `${block.kind === 'event' ? 'event' : 'fn'} ${block.name}`,
          detail: `${graph.name} / ${block.flows.length} flows, ${block.links.length} links`,
          blockKind: block.kind,
          blockName: block.name,
          sourceRange: block.name_source_range ?? block.source_range,
        })

        for (const flow of block.flows) {
          const flowLabel = `flow ${flow.from_node}.${flow.from_pin} -> ${flow.to_node}.${flow.to_pin}`
          const flowRange = sourceRangeText(flow.source_range)
          addCandidate({
            kind: 'connection',
            graphIndex: index,
            graphName: graph.name,
            label: flowLabel,
            detail: [
              graph.name,
              `${block.kind} ${block.name} flow`,
              flow.persistent_id,
              flowRange ? `range ${flowRange}` : '',
            ].filter(Boolean).join(' / '),
            nodeInstance: flow.from_node,
            pinName: flow.from_pin,
            connectionKind: 'flow',
            connectionId: flow.id,
            persistentId: flow.persistent_id,
            sourceNode: flow.from_node,
            sourcePin: flow.from_pin,
            targetNode: flow.to_node,
            targetPin: flow.to_pin,
            blockKind: block.kind,
            blockName: block.name,
            sourceRange: flow.source_range,
            sourceEndpointRange: flow.from_endpoint_source_range,
            targetEndpointRange: flow.to_endpoint_source_range,
            annotations: flow.annotations,
          })
          addSourceReference({
            graphIndex: index,
            graphName: graph.name,
            label: `flow ${block.kind} ${block.name} ${flow.from_node}.${flow.from_pin}`,
            detail: `${graph.name} / ${block.kind} ${block.name} flow source endpoint`,
            nodeInstance: flow.from_node,
            pinName: flow.from_pin,
            connectionKind: 'flow',
            blockKind: block.kind,
            blockName: block.name,
            sourceReferenceKind: 'flow_endpoint',
            sourceReferenceSymbol: `${flow.from_node}.${flow.from_pin}`,
            sourceRange: flow.from_endpoint_source_range ?? flow.from_pin_source_range,
          })
          addSourceReference({
            graphIndex: index,
            graphName: graph.name,
            label: `flow ${block.kind} ${block.name} ${flow.to_node}.${flow.to_pin}`,
            detail: `${graph.name} / ${block.kind} ${block.name} flow target endpoint`,
            nodeInstance: flow.to_node,
            pinName: flow.to_pin,
            connectionKind: 'flow',
            blockKind: block.kind,
            blockName: block.name,
            sourceReferenceKind: 'flow_endpoint',
            sourceReferenceSymbol: `${flow.to_node}.${flow.to_pin}`,
            sourceRange: flow.to_endpoint_source_range ?? flow.to_pin_source_range,
          })
        }

        for (const link of block.links) {
          const sourceEndpoint = link.source_pin ? `${link.source_node}.${link.source_pin}` : link.source_node
          const linkLabel = `link ${sourceEndpoint} -> ${link.target_node}.${link.target_pin}`
          const linkRange = sourceRangeText(link.source_range)
          addCandidate({
            kind: 'connection',
            graphIndex: index,
            graphName: graph.name,
            label: linkLabel,
            detail: [
              graph.name,
              `${block.kind} ${block.name} link`,
              link.persistent_id,
              linkRange ? `range ${linkRange}` : '',
            ].filter(Boolean).join(' / '),
            nodeInstance: link.source_pin ? link.source_node : link.target_node,
            parameterName: link.source_pin ? undefined : link.source_node,
            pinName: link.source_pin || link.target_pin,
            connectionKind: 'link',
            connectionId: link.id,
            persistentId: link.persistent_id,
            sourceNode: link.source_node,
            sourcePin: link.source_pin,
            targetNode: link.target_node,
            targetPin: link.target_pin,
            blockKind: block.kind,
            blockName: block.name,
            sourceRange: link.source_range,
            sourceEndpointRange: link.source_endpoint_source_range,
            targetEndpointRange: link.target_endpoint_source_range,
            annotations: link.annotations,
          })
          addSourceReference({
            graphIndex: index,
            graphName: graph.name,
            label: `link ${block.kind} ${block.name} ${link.target_node}.${link.target_pin} target`,
            detail: `${graph.name} / ${block.kind} ${block.name} link target endpoint`,
            nodeInstance: link.target_node,
            pinName: link.target_pin,
            connectionKind: 'link',
            blockKind: block.kind,
            blockName: block.name,
            sourceReferenceKind: 'link_endpoint',
            sourceReferenceSymbol: `${link.target_node}.${link.target_pin}`,
            sourceRange: link.target_endpoint_source_range ?? link.target_pin_source_range,
          })
          addSourceReference({
            graphIndex: index,
            graphName: graph.name,
            label: `link ${block.kind} ${block.name} ${link.source_node}${link.source_pin ? `.${link.source_pin}` : ''} source`,
            detail: `${graph.name} / ${block.kind} ${block.name} link source endpoint`,
            nodeInstance: link.source_pin ? link.source_node : undefined,
            parameterName: link.source_pin ? undefined : link.source_node,
            pinName: link.source_pin || undefined,
            connectionKind: 'link',
            blockKind: block.kind,
            blockName: block.name,
            sourceReferenceKind: 'link_endpoint',
            sourceReferenceSymbol: link.source_pin ? `${link.source_node}.${link.source_pin}` : link.source_node,
            sourceRange: link.source_endpoint_source_range ?? link.source_pin_source_range ?? link.source_node_source_range,
          })
        }
      }
    })

    for (const typeDef of state.declared_types ?? []) {
      const sourceFile = typeDef.source_file || ''
      const sourceRange = typeDef.name_source_range ?? typeDef.source_range
      if (sourceFile && hasSourceRange(sourceRange)) {
        addCandidate({
          kind: 'declaration',
          graphIndex,
          graphName: 'Declarations',
          label: typeDef.name,
          detail: `${sourceFileLabel(sourceFile)} / declare type`,
          declarationKind: 'type',
          sourceFile,
          sourceRange,
        })
      }
      addDeclarationAnnotationReferences(
        typeDef.annotations,
        sourceFile,
        `type ${typeDef.name}`,
        {
          declarationKind: 'type',
          declarationOwner: typeDef.name,
        },
      )
    }

    for (const typeDef of state.types.filter(item => Boolean(item.source_file))) {
      const sourceFile = typeDef.source_file || ''
      const sourceRange = typeDef.name_source_range ?? typeDef.source_range
      if (sourceFile && hasSourceRange(sourceRange)) {
        addCandidate({
          kind: 'declaration',
          graphIndex,
          graphName: 'Declarations',
          label: typeDef.type_name,
          detail: `${sourceFileLabel(sourceFile)} / declare Node`,
          declarationKind: 'node',
          sourceFile,
          sourceRange,
        })
      }
      addDeclarationAnnotationReferences(
        typeDef.annotations,
        sourceFile,
        `node ${typeDef.type_name}`,
        {
          declarationKind: 'node',
          declarationOwner: typeDef.type_name,
        },
      )

      for (const pin of typeDef.pins) {
        const pinSourceFile = pin.source_file || sourceFile
        const pinSourceRange = pin.name_source_range ?? pin.source_range
        if (pinSourceFile && hasSourceRange(pinSourceRange)) {
          addCandidate({
            kind: 'declaration',
            graphIndex,
            graphName: 'Declarations',
            label: `${typeDef.type_name}.${pin.name}`,
            detail: `${sourceFileLabel(pinSourceFile)} / ${pin.direction} ${pin.kind}${pin.type ? ` ${pin.type}` : ''}`,
            pinName: pin.name,
            declarationKind: 'pin',
            declarationOwner: typeDef.type_name,
            sourceFile: pinSourceFile,
            sourceRange: pinSourceRange,
          })
        }
        if (pinSourceFile && pin.type) {
          addSourceReference({
            graphIndex,
            graphName: 'Declarations',
            label: `declare pin ${typeDef.type_name}.${pin.name} type ${pin.type}`,
            detail: `${sourceFileLabel(pinSourceFile)} / declaration pin type reference`,
            pinName: pin.name,
            declarationOwner: typeDef.type_name,
            sourceReferenceKind: 'type',
            sourceReferenceSymbol: pin.type,
            sourceFile: pinSourceFile,
            sourceRange: pin.type_source_range,
          })
        }
        addDeclarationAnnotationReferences(
          pin.annotations,
          pinSourceFile,
          `pin ${typeDef.type_name}.${pin.name}`,
          {
            pinName: pin.name,
            declarationKind: 'pin',
            declarationOwner: typeDef.type_name,
          },
        )
      }
    }

    for (const schema of state.schemas) {
      const sourceFile = schema.source_file || ''
      const sourceRange = schema.name_source_range ?? schema.source_range
      if (sourceFile && hasSourceRange(sourceRange)) {
        addCandidate({
          kind: 'declaration',
          graphIndex,
          graphName: 'Declarations',
          label: schema.name,
          detail: `${sourceFileLabel(sourceFile)} / declare Schema`,
          declarationKind: 'schema',
          sourceFile,
          sourceRange,
        })
      }
      addDeclarationAnnotationReferences(
        schema.annotations,
        sourceFile,
        `schema ${schema.name}`,
        {
          declarationKind: 'schema',
          declarationOwner: schema.name,
        },
      )

      for (const field of schema.fields ?? []) {
        const fieldSourceFile = field.source_file || sourceFile
        const fieldSourceRange = field.name_source_range ?? field.source_range
        if (fieldSourceFile && hasSourceRange(fieldSourceRange)) {
          addCandidate({
            kind: 'declaration',
            graphIndex,
            graphName: 'Declarations',
            label: `${schema.name}.${field.name}`,
            detail: `${sourceFileLabel(fieldSourceFile)} / schema field ${field.value}`,
            declarationKind: 'schema_field',
            declarationOwner: schema.name,
            schemaFieldName: field.name,
            sourceFile: fieldSourceFile,
            sourceRange: fieldSourceRange,
          })
        }
        const fieldConstructorType = constructorTypeName(field.value)
        if (fieldSourceFile && fieldConstructorType) {
          addSourceReference({
            graphIndex,
            graphName: 'Declarations',
            label: `declare schema ${schema.name}.${field.name} value ${fieldConstructorType}`,
            detail: `${sourceFileLabel(fieldSourceFile)} / declaration schema field constructor type reference`,
            schemaFieldName: field.name,
            declarationOwner: schema.name,
            sourceReferenceKind: 'constructor_type',
            sourceReferenceSymbol: fieldConstructorType,
            sourceFile: fieldSourceFile,
            sourceRange: field.value_constructor_type_source_range,
          })
        }
        addDeclarationAnnotationReferences(
          field.annotations,
          fieldSourceFile,
          `schema ${schema.name}.${field.name}`,
          {
            schemaFieldName: field.name,
            declarationKind: 'schema_field',
            declarationOwner: schema.name,
          },
        )
      }
    }

    for (const diagnostic of diagnostics) {
      const targetGraph = diagnostic.target?.graph
      const diagnosticGraphIndex = targetGraph ? graphIndexByName.get(targetGraph) : graphIndex
      if (diagnosticGraphIndex === undefined) continue
      const nodeInstance = diagnosticNodeCandidate(state, diagnosticGraphIndex, diagnostic)
      const parameterName = diagnostic.target?.parameter_name || undefined
      const blockKind = diagnostic.target?.block_kind === 'event' || diagnostic.target?.block_kind === 'function'
        ? diagnostic.target.block_kind
        : undefined
      const targetDetail = diagnosticTargetDetail(diagnostic, nodeInstance, parameterName)
      const rangeText = diagnosticRangeText(diagnostic)
      const detailParts = [
        targetGraph || 'current graph',
        diagnostic.severity,
        targetDetail,
        rangeText ? `range ${rangeText}` : '',
      ].filter(Boolean)
      addCandidate({
        kind: 'diagnostic',
        graphIndex: diagnosticGraphIndex,
        graphName: targetGraph || state.module.graphs[diagnosticGraphIndex]?.name,
        label: diagnostic.code || diagnostic.context || diagnostic.severity,
        detail: detailParts.join(' / '),
        nodeInstance,
        pinName: diagnostic.target?.pin_name || undefined,
        parameterName,
        connectionKind: diagnostic.target?.connection_kind || undefined,
        blockKind,
        blockName: diagnostic.target?.block_name || undefined,
        diagnostic,
      })
    }

    const needle = normalize(query.trim())
    if (!needle) return []
    return candidates
      .filter(result => matchesResult(result, query))
      .map((result, index) => ({
        result,
        index,
        score: scoreResult(result, query, graphIndex),
        matchMode: matchModeForResult(result, query),
      }))
      .sort((left, right) => left.score - right.score || left.index - right.index)
      .slice(0, 12)
      .map(item => ({ ...item.result, score: item.score, matchMode: item.matchMode }))
  }, [diagnostics, graphIndex, query, state])

  const selectResult = async (result: GraphSearchResult) => {
    await onNavigate(result)
    setQuery(result.label)
    setOpen(false)
  }

  const submitFirst = async (event: FormEvent) => {
    event.preventDefault()
    const result = results[activeIndex >= 0 ? activeIndex : 0]
    if (result) await selectResult(result)
  }

  useEffect(() => {
    setActiveIndex(results.length > 0 ? 0 : -1)
  }, [query, results.length])

  const groupedResults = useMemo(() => {
    const groups: SearchResultGroup[] = []
    const groupByName = new Map<string, SearchResultGroup>()

    results.forEach((result, index) => {
      const fallbackName = state?.module.graphs[result.graphIndex]?.name ?? `Graph ${result.graphIndex}`
      const graphName = result.graphName || fallbackName
      let group = groupByName.get(graphName)
      if (!group) {
        group = { graphName, items: [] }
        groupByName.set(graphName, group)
        groups.push(group)
      }
      group.items.push({ result, index })
    })

    return groups
  }, [results, state])

  return (
    <form className="relative w-[280px]" onSubmit={submitFirst}>
      <Search className="pointer-events-none absolute left-2 top-1/2 h-3.5 w-3.5 -translate-y-1/2 text-muted-foreground/70" />
      <Input
        value={query}
        onChange={event => {
          setQuery(event.target.value)
          setOpen(true)
        }}
        onFocus={() => setOpen(true)}
        onKeyDown={event => {
          if (event.key === 'Escape') {
            setOpen(false)
            return
          }
          if (event.key === 'ArrowDown') {
            event.preventDefault()
            setOpen(true)
            setActiveIndex(index => {
              if (results.length === 0) return -1
              return index < 0 ? 0 : (index + 1) % results.length
            })
            return
          }
          if (event.key === 'ArrowUp') {
            event.preventDefault()
            setOpen(true)
            setActiveIndex(index => {
              if (results.length === 0) return -1
              return index < 0 ? results.length - 1 : (index - 1 + results.length) % results.length
            })
          }
        }}
        aria-label="Search graph"
        aria-controls="graph-search-results"
        aria-activedescendant={open && activeIndex >= 0 ? `graph-search-result-${activeIndex}` : undefined}
        placeholder="Search graph"
        disabled={!hasGraphs}
        className="h-7 pl-7 pr-7 text-[11px]"
      />
      {query && (
        <Button
          type="button"
          variant="ghost"
          size="icon"
          aria-label="Clear graph search"
          onClick={() => {
            setQuery('')
            setOpen(false)
          }}
          className="absolute right-0.5 top-0.5 h-6 w-6 text-muted-foreground hover:text-foreground"
        >
          <X className="h-3.5 w-3.5" />
        </Button>
      )}
      {open && query.trim() && (
        <div
          id="graph-search-results"
          data-graph-search-results
          role="listbox"
          className="absolute left-0 right-0 top-8 z-50 max-h-72 overflow-auto rounded-md border border-border bg-popover shadow-xl"
        >
          {results.length === 0 ? (
            <div className="px-3 py-2 text-[11px] text-muted-foreground">No results</div>
          ) : groupedResults.map(group => (
            <div key={group.graphName} data-graph-search-group={group.graphName}>
              <div
                data-graph-search-group-header={group.graphName}
                className="border-y border-border/60 bg-muted/40 px-3 py-1 text-[9px] font-semibold uppercase text-muted-foreground first:border-t-0"
              >
                {group.graphName}
              </div>
              {group.items.map(({ result, index }) => (
                <button
                  id={`graph-search-result-${index}`}
                  key={`${result.kind}:${result.label}:${index}`}
                  type="button"
                  role="option"
                  aria-selected={index === activeIndex}
                  data-graph-search-result-kind={result.kind}
                  data-graph-search-result-label={result.label}
                  data-graph-search-result-match={result.matchMode}
                  data-graph-search-result-score={String(result.score)}
                  data-graph-search-result-active={index === activeIndex ? 'true' : 'false'}
                  onMouseDown={event => event.preventDefault()}
                  onMouseEnter={() => setActiveIndex(index)}
                  onClick={() => { void selectResult(result) }}
                  className={`flex w-full items-center justify-between gap-3 px-3 py-2 text-left hover:bg-accent focus:bg-accent focus:outline-none ${index === activeIndex ? 'bg-accent' : ''}`}
                >
                  <span className="min-w-0">
                    <span data-graph-search-result-title className="block truncate text-[11px] font-medium text-foreground">{highlightText(result.label, query)}</span>
                    <span data-graph-search-result-detail className="block truncate text-[10px] text-muted-foreground">{highlightText(result.detail, query)}</span>
                  </span>
                  <span className="shrink-0 rounded bg-secondary px-1.5 py-0.5 text-[9px] uppercase tracking-wide text-muted-foreground">
                    {result.kind}
                  </span>
                </button>
              ))}
            </div>
          ))}
        </div>
      )}
    </form>
  )
}
