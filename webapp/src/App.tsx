import { useCallback, useEffect, useMemo, useState, useRef } from 'react'
import { ErrorBoundary } from '@/components/ErrorBoundary'
import { TooltipProvider } from '@/components/ui/tooltip'
import {
  fetchState,
  execCommand,
  undo as apiUndo,
  redo as apiRedo,
  fetchEmit,
  fetchDeclarationSource,
  fetchDiagnostics,
  applySource,
  applySourcePatch,
} from '@/api/client'
import type { Annotation, DataLink, Diagnostic, DiagnosticAction, FlowConn, GraphDef, GSState, LogicBlock, NodeInst, SourceRange } from '@/api/types'
import type { SourceDiagnosticsEnvironment } from '@/api/types'
import FlowCanvas, { type CommentBoxEditPayload, type EdgeEditPayload } from '@/canvas/FlowCanvas'
import Toolbar from '@/panels/Toolbar'
import type { GraphSearchResult } from '@/panels/GraphSearch'
import NodePalette from '@/panels/NodePalette'
import PropertiesPanel from '@/panels/PropertiesPanel'
import CommandLog from '@/panels/CommandLog'
import DiagnosticsPanel from '@/panels/DiagnosticsPanel'
import SourcePreviewPanel, { type DeclarationRenameContext, type SourceSyncState } from '@/panels/SourcePreviewPanel'
import WorkbenchLayout from '@/workbench/WorkbenchLayout'

function isDefaultRange(range: SourceRange): boolean {
  return range.start.line === 1 &&
    range.start.column === 1 &&
    range.end.line === 1 &&
    range.end.column === 1
}

function quoteCommandArg(value: string): string {
  return /[\s"\\]/.test(value)
    ? `"${value.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`
    : value
}

function isEditableKeyboardTarget(target: EventTarget | null): boolean {
  if (!(target instanceof HTMLElement)) return false
  const tagName = target.tagName.toLowerCase()
  return target.isContentEditable ||
    tagName === 'input' ||
    tagName === 'textarea' ||
    tagName === 'select' ||
    target.closest('[contenteditable="true"], [role="textbox"], .monaco-editor, .cm-editor, .native-edit-context') !== null
}

function isGraphKeyboardTarget(target: EventTarget | null): boolean {
  const root = document.querySelector('[data-canvas-context-root="true"]')
  if (!root) return false
  if (target instanceof Node && root.contains(target)) return true
  const active = document.activeElement
  return active instanceof Node && root.contains(active)
}

function annotationArgCommandPart(arg: Annotation['args'][number]): string {
  const value = quoteCommandArg(arg.value)
  return arg.name ? `${arg.name}=${value}` : value
}

function annotationCommandSuffix(annotation: Annotation): string {
  const args = annotation.args.map(annotationArgCommandPart).join(' ')
  return args ? `${annotation.name} ${args}` : annotation.name
}

function sourceValue(value: string): string {
  const trimmed = value.trim()
  if (!trimmed) return '""'
  if (/^".*"$/.test(trimmed) || /^-?\d+(\.\d+)?$/.test(trimmed)) return trimmed
  if (/^(true|false|unlimited|null)$/i.test(trimmed)) return trimmed
  if (/^[A-Za-z_][A-Za-z0-9_]*\(.*\)$/.test(trimmed)) return trimmed
  return `"${trimmed.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`
}

function sourceAnnotationLine(annotation: Annotation): string {
  const args = annotation.args
    .map(arg => arg.name ? `${arg.name} = ${sourceValue(arg.value)}` : sourceValue(arg.value))
    .join(', ')
  return args ? `${annotation.name}(${args})` : annotation.name
}

function sourceAnnotationLines(annotations: Annotation[] | undefined, indent = ''): string[] {
  if (!annotations || annotations.length === 0) return []
  return annotations.map(annotation => `${indent}@${sourceAnnotationLine(annotation)}`)
}

function sourceForFlow(flow: FlowConn, indent: string): string[] {
  return [
    ...sourceAnnotationLines(flow.annotations, indent),
    `${indent}connect(${flow.from_node}.${flow.from_pin}, ${flow.to_node}.${flow.to_pin});`,
  ]
}

function sourceForLink(link: DataLink, indent: string): string[] {
  const source = link.source_pin ? `${link.source_node}.${link.source_pin}` : link.source_node
  return [
    ...sourceAnnotationLines(link.annotations, indent),
    `${indent}bind(${source}, ${link.target_node}.${link.target_pin});`,
  ]
}

function sourceParamDirectionAttribute(direction: 'in' | 'out' | 'var'): string {
  if (direction === 'in') return '@graph.input'
  if (direction === 'out') return '@graph.output'
  return '@graph.var'
}

function currentGraphSource(graph: GraphDef | undefined): string {
  if (!graph) return ''
  const lines: string[] = []
  lines.push(...sourceAnnotationLines(graph.annotations))
  lines.push(`graph ${graph.name} {`)

  if (graph.base_type) {
    lines.push(`    schema ${graph.base_type};`)
  }

  for (const param of graph.parameters) {
    lines.push(...sourceAnnotationLines(param.annotations, '    '))
    lines.push(`    ${sourceParamDirectionAttribute(param.direction)}`)
    const defaultValue = param.default ? ` = ${param.default}` : ''
    lines.push(`    param ${param.name}: ${param.type}${defaultValue};`)
  }

  if (graph.parameters.length > 0 && graph.nodes.length > 0) lines.push('')
  for (const node of graph.nodes) {
    lines.push(...sourceAnnotationLines(node.annotations, '    '))
    lines.push(`    node ${node.instance} {`)
    lines.push(`        type ${node.type};`)
    for (const field of node.initializer_fields ?? []) {
      lines.push(`        ${field.name}: ${sourceValue(field.value)};`)
    }
    if (!node.initializer_fields?.length && node.init?.trim()) {
      lines.push(`        ${node.init.trim()};`)
    }
    lines.push('    }')
  }

  const blocks = [...graph.events, ...graph.functions]
  if ((graph.parameters.length > 0 || graph.nodes.length > 0) && blocks.length > 0) lines.push('')
  for (const [index, block] of blocks.entries()) {
    if (index > 0) lines.push('')
    lines.push(...sourceAnnotationLines(block.annotations, '    '))
    lines.push(`    ${block.kind} ${block.name} {`)
    for (const flow of block.flows) lines.push(...sourceForFlow(flow, '        '))
    for (const link of block.links) lines.push(...sourceForLink(link, '        '))
    lines.push('    }')
  }

  if (graph.generate && (graph.generate.comments.length > 0 || graph.generate.metadata.length > 0)) {
    if (lines[lines.length - 1] !== '') lines.push('')
    lines.push('    generate Layout {')
    for (const comment of graph.generate.comments) {
      lines.push(...sourceAnnotationLines(comment.annotations, '        '))
      lines.push(`        comment(${comment.instance}, ${sourceValue(comment.text)});`)
    }
    for (const metadata of graph.generate.metadata) {
      lines.push(...sourceAnnotationLines(metadata.annotations, '        '))
      lines.push(`        metadata(${metadata.scope}, ${metadata.node}, ${metadata.property}, ${sourceValue(metadata.value)});`)
    }
    lines.push('    }')
  }

  lines.push('}')
  return lines.join('\n')
}

function nodeInstancePrefix(typeName: string): string {
  const sanitized = typeName
    .replace(/[^A-Za-z0-9_]/g, '_')
    .replace(/^_+|_+$/g, '')
    .toLowerCase()
  if (!sanitized) return 'node'
  return /^[A-Za-z_]/.test(sanitized) ? sanitized : `node_${sanitized}`
}

function annotationValue(node: NodeInst, annotationName: string, argName: string): string | undefined {
  const annotation = node.annotations.find(item => item.name === annotationName)
  return annotation?.args.find(arg => arg.name === argName)?.value
}

function nodeCanvasPosition(node: NodeInst, fallbackIndex: number): { x: number, y: number } {
  const x = Number(annotationValue(node, 'Position', 'X'))
  const y = Number(annotationValue(node, 'Position', 'Y'))
  if (Number.isFinite(x) && Number.isFinite(y)) return { x, y }
  return {
    x: 200 + (fallbackIndex % 4) * 300,
    y: 120 + Math.floor(fallbackIndex / 4) * 200,
  }
}

function nextDuplicateNodeName(instance: string, existingNames: Set<string>): string {
  const base = `${instance}_copy`
  if (!existingNames.has(base)) {
    existingNames.add(base)
    return base
  }
  for (let index = 2; index < 10000; index += 1) {
    const candidate = `${base}${index}`
    if (!existingNames.has(candidate)) {
      existingNames.add(candidate)
      return candidate
    }
  }
  const fallback = `${base}_${Date.now() % 100000}`
  existingNames.add(fallback)
  return fallback
}

function nextCommentBoxAnnotationName(graph: GraphDef): string {
  const existing = new Set(graph.annotations.map(annotation => annotation.name))
  for (let index = 1; index < 10000; index += 1) {
    const candidate = `CommentBox_${index}`
    if (!existing.has(candidate)) return candidate
  }
  return `CommentBox_${Date.now() % 100000}`
}

function commentBoxCommand(annotationName: string, box: Omit<CommentBoxEditPayload, 'annotationName'>): string {
  return [
    'annotate graph',
    annotationName,
    `Text=${quoteCommandArg(box.text || 'Comment')}`,
    `X=${Math.round(box.x)}`,
    `Y=${Math.round(box.y)}`,
    `W=${Math.round(box.width)}`,
    `H=${Math.round(box.height)}`,
  ].join(' ')
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

function rangeAfterReplacement(start: SourceRange['start'], replacement: string): SourceRange {
  const lines = replacement.split(/\r?\n/)
  const end = lines.length === 1
    ? { line: start.line, column: start.column + replacement.length }
    : { line: start.line + lines.length - 1, column: lines[lines.length - 1].length + 1 }
  return { start, end }
}

function locationForOffset(source: string, offset: number): SourceRange['start'] | null {
  if (offset < 0 || offset > source.length) return null

  let line = 1
  let column = 1
  for (let index = 0; index < source.length; index += 1) {
    if (index === offset) return { line, column }

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

  return { line, column }
}

function avoidSplitCrlf(source: string, start: number, end: number): { start: number; end: number } {
  let nextStart = start
  let nextEnd = end
  if (nextStart > 0 && source[nextStart - 1] === '\r' && source[nextStart] === '\n') {
    nextStart -= 1
  }
  if (nextEnd > 0 && source[nextEnd - 1] === '\r' && source[nextEnd] === '\n') {
    nextEnd += 1
  }
  return { start: nextStart, end: nextEnd }
}

function sourceDiffEdit(baseSource: string, nextSource: string): { range: SourceRange; replacement: string } | null {
  if (baseSource === nextSource) return null

  const minLength = Math.min(baseSource.length, nextSource.length)
  let start = 0
  while (start < minLength && baseSource[start] === nextSource[start]) {
    start += 1
  }

  let baseEnd = baseSource.length
  let nextEnd = nextSource.length
  while (baseEnd > start && nextEnd > start && baseSource[baseEnd - 1] === nextSource[nextEnd - 1]) {
    baseEnd -= 1
    nextEnd -= 1
  }

  const adjusted = avoidSplitCrlf(baseSource, start, baseEnd)
  if (adjusted.start !== start) {
    const delta = start - adjusted.start
    start = adjusted.start
    nextEnd = Math.min(nextSource.length, nextEnd + delta)
  }
  if (adjusted.end !== baseEnd) {
    const delta = adjusted.end - baseEnd
    baseEnd = adjusted.end
    nextEnd = Math.min(nextSource.length, nextEnd + delta)
  }

  const rangeStart = locationForOffset(baseSource, start)
  const rangeEnd = locationForOffset(baseSource, baseEnd)
  if (!rangeStart || !rangeEnd) return null

  return {
    range: { start: rangeStart, end: rangeEnd },
    replacement: nextSource.slice(start, nextEnd),
  }
}

function formatSourceRange(range: SourceRange): string {
  const { start, end } = range
  if (start.line === end.line && start.column === end.column) {
    return `${start.line}:${start.column}`
  }
  return `${start.line}:${start.column}-${end.line}:${end.column}`
}

function summarizeSourcePatch(baseSource: string, diff: { range: SourceRange; replacement: string }): string {
  const startOffset = offsetForLocation(baseSource, diff.range.start)
  const endOffset = offsetForLocation(baseSource, diff.range.end)
  const removed = startOffset !== null && endOffset !== null && endOffset >= startOffset
    ? endOffset - startOffset
    : 0
  return `Pending patch ${formatSourceRange(diff.range)}; replace ${removed} chars with ${diff.replacement.length} chars`
}

function importDeclarations(source: string): string[] {
  return source
    .split(/\r?\n/)
    .map(line => line.match(/^\s*import\s+"([^"]+)"/)?.[1] ?? '')
    .filter(Boolean)
}

function countImportDeclarations(source: string): number {
  return importDeclarations(source).length
}

function normalizeSourcePath(path: string): string {
  return path.replace(/\\/g, '/').replace(/\/+$/g, '')
}

function dirname(path: string): string {
  const normalized = normalizeSourcePath(path)
  const slash = normalized.lastIndexOf('/')
  if (slash <= 0) return ''
  return normalized.slice(0, slash)
}

function commonPath(paths: string[]): string | undefined {
  const normalizedPaths = paths
    .map(normalizeSourcePath)
    .filter(Boolean)
  if (normalizedPaths.length === 0) return undefined

  const [first, ...rest] = normalizedPaths
  const firstParts = first.split('/')
  let commonLength = firstParts.length
  for (const path of rest) {
    const parts = path.split('/')
    commonLength = Math.min(commonLength, parts.length)
    for (let index = 0; index < commonLength; index += 1) {
      if (parts[index]?.toLowerCase() !== firstParts[index]?.toLowerCase()) {
        commonLength = index
        break
      }
    }
  }

  if (commonLength === 0) return undefined
  return firstParts.slice(0, commonLength).join('/')
}

function isAbsoluteSourcePath(path: string): boolean {
  return path.startsWith('/') || /^[A-Za-z]:\//.test(path)
}

function sourceImportBaseDir(importDef: GSState['module']['imports'][number]): string | undefined {
  const rawPath = normalizeSourcePath(importDef.path)
  const normalizedPath = importDef.normalized_path ? normalizeSourcePath(importDef.normalized_path) : ''
  if (!rawPath) return normalizedPath ? dirname(normalizedPath) : undefined
  if (isAbsoluteSourcePath(rawPath)) return dirname(rawPath)
  if (!normalizedPath) return dirname(rawPath)

  const suffix = `/${rawPath}`.toLowerCase()
  const normalizedLower = normalizedPath.toLowerCase()
  if (normalizedLower.endsWith(suffix)) {
    return normalizedPath.slice(0, normalizedPath.length - suffix.length)
  }
  return dirname(normalizedPath)
}

function sourceFileBaseDirForImport(importPath: string, sourceFile: string): string | undefined {
  const rawPath = normalizeSourcePath(importPath)
  const normalizedPath = normalizeSourcePath(sourceFile)
  if (!rawPath || !normalizedPath || isAbsoluteSourcePath(rawPath)) return undefined

  const suffix = `/${rawPath}`.toLowerCase()
  const normalizedLower = normalizedPath.toLowerCase()
  if (normalizedLower.endsWith(suffix)) {
    return normalizedPath.slice(0, normalizedPath.length - suffix.length)
  }
  return undefined
}

function loadedDeclarationSourceFiles(state: GSState): string[] {
  const sourceFiles = new Set<string>()
  for (const type of state.declared_types ?? []) {
    if (type.source_file) sourceFiles.add(type.source_file)
  }
  for (const nodeType of state.types) {
    if (nodeType.source_file) sourceFiles.add(nodeType.source_file)
    for (const pin of nodeType.pins) {
      if (pin.source_file) sourceFiles.add(pin.source_file)
    }
    for (const field of nodeType.fields ?? []) {
      if (field.source_file) sourceFiles.add(field.source_file)
    }
  }
  for (const schema of state.schemas) {
    if (schema.source_file) sourceFiles.add(schema.source_file)
    for (const field of schema.fields ?? []) {
      if (field.source_file) sourceFiles.add(field.source_file)
    }
  }
  return [...sourceFiles]
}

function loadedDeclarationBaseDir(source: string, state: GSState): string | undefined {
  const imports = importDeclarations(source)
  if (imports.length === 0) return undefined
  const sourceFiles = loadedDeclarationSourceFiles(state)
  return commonPath(
    imports
      .flatMap(importPath => sourceFiles
        .map(sourceFile => sourceFileBaseDirForImport(importPath, sourceFile))
        .filter((path): path is string => Boolean(path))),
  )
}

function sourceDiagnosticsBaseDir(source: string, state: GSState | null): string | undefined {
  if (!state) return undefined
  const declarationBaseDir = loadedDeclarationBaseDir(source, state)
  if (declarationBaseDir) return declarationBaseDir
  const loadedImportBaseDir = commonPath(
    state.module.imports
      .filter(importDef => importDef.loaded)
      .map(sourceImportBaseDir)
      .filter((path): path is string => Boolean(path)),
  )
  if (loadedImportBaseDir) return loadedImportBaseDir
  if (state.file_path) return undefined
  return undefined
}

function sourceDiagnosticsOptions(source: string, state: GSState | null) {
  const resolveImports = countImportDeclarations(source) > 0
  return {
    resolveImports,
    sourcePath: resolveImports ? state?.file_path || undefined : undefined,
    baseDir: resolveImports ? sourceDiagnosticsBaseDir(source, state) : undefined,
  }
}

function isImportCommand(command: string): boolean {
  return /^import(?:\s|$)/.test(command.trim())
}

function sourceEnvironmentNotice(source: string, state: GSState | null, resolverEnvironment: SourceDiagnosticsEnvironment | null): string {
  if (!source) return ''

  const importCount = countImportDeclarations(source)
  if (resolverEnvironment) {
    const loadedCount = resolverEnvironment.declarations.filter(declaration => declaration.status === 'loaded').length
    const blockedCount = resolverEnvironment.declarations.filter(declaration => declaration.status !== 'loaded').length
    return `Source 诊断在 dry-run 环境中解析了 ${loadedCount}/${resolverEnvironment.declarations.length} 个导入（${resolverEnvironment.node_type_count} 个节点类型，${resolverEnvironment.schema_count} 个 Schema）。${blockedCount > 0 ? `${blockedCount} 个导入被阻止或未解析。` : 'Apply 仍使用当前会话环境，除非先通过 CLI import 命令加载这些声明。'}`
  }

  const typeCount = state?.types.length ?? 0
  const schemaCount = state?.schemas.length ?? 0
  const sessionImportCount = state?.module.imports.filter(importDef => importDef.loaded).length ?? 0
  const base = `Source 诊断和 Apply 当前使用会话环境（${typeCount} 个节点类型，${schemaCount} 个 Schema）。`
  if (importCount > 0) {
    return `${base} 当前源码包含 ${importCount} 个 import；会话已加载 ${sessionImportCount} 个 import。`
  }
  return base
}

function applySourceEdit(source: string, range: SourceRange, replacement: string): { source: string; range: SourceRange } | null {
  const startOffset = offsetForLocation(source, range.start)
  const endOffset = offsetForLocation(source, range.end)
  if (startOffset === null || endOffset === null || startOffset > endOffset) return null

  return {
    source: source.slice(0, startOffset) + replacement + source.slice(endOffset),
    range: rangeAfterReplacement(range.start, replacement),
  }
}

function firstLocatedDiagnostic(diagnostics: Diagnostic[]): Diagnostic | null {
  return diagnostics.find(diagnostic => !isDefaultRange(diagnostic.range)) ?? diagnostics[0] ?? null
}

function diagnosticKey(diagnostic: Diagnostic): string {
  if (diagnostic.id) return diagnostic.id
  const target = diagnostic.target
  return JSON.stringify([
    diagnostic.severity,
    diagnostic.code,
    diagnostic.message,
    diagnostic.context,
    diagnostic.range,
    target?.graph,
    target?.block_kind,
    target?.block_name,
    target?.node_instance,
    target?.pin_name,
    target?.parameter_name,
    target?.reference,
    target?.connection_kind,
  ])
}

function diagnosticNodeCandidate(graph: GraphDef, diagnostic: Diagnostic): string {
  const target = diagnostic.target
  const candidates = [
    target?.node_instance,
    target?.reference,
    diagnostic.context,
  ]
  return candidates.find(candidate =>
    !!candidate && graph.nodes.some(node => node.instance === candidate)) ?? ''
}

function collectSessionDiagnostics(state: GSState | null): Diagnostic[] {
  const diagnostics: Diagnostic[] = []
  const seen = new Set<string>()
  for (const diagnostic of [
    ...(state?.diagnostics ?? []),
    ...(state?.module_diagnostics ?? []),
  ]) {
    const key = diagnosticKey(diagnostic)
    if (seen.has(key)) continue
    seen.add(key)
    diagnostics.push(diagnostic)
  }
  return diagnostics
}

function sourceRangeKey(range: SourceRange | undefined): string {
  if (!range) return ''
  return `${range.start.line}:${range.start.column}-${range.end.line}:${range.end.column}`
}

function sourceLineRange(source: string, needles: string[]): SourceRange | null {
  const lines = source.split(/\r?\n/)
  const cleanedNeedles = needles.map(needle => needle.trim()).filter(Boolean)
  if (cleanedNeedles.length === 0) return null
  for (let index = 0; index < lines.length; index += 1) {
    const line = lines[index]
    if (!cleanedNeedles.some(needle => line.includes(needle))) continue
    return {
      start: { line: index + 1, column: 1 },
      end: { line: index + 1, column: Math.max(1, line.length + 1) },
    }
  }
  return null
}

function nodeSourceNeedles(node: NodeInst): string[] {
  return [
    `${node.type} ${node.instance}`,
    `${node.instance}{`,
    `${node.instance} {`,
  ]
}

function edgeSourceNeedles(edge: EdgeEditPayload): string[] {
  if (edge.kind === 'exec') {
    return [
      `${edge.sourceNode}.${edge.sourcePin}(${edge.targetNode}.${edge.targetPin}`,
      `${edge.sourceNode}.${edge.sourcePin} (${edge.targetNode}.${edge.targetPin}`,
    ]
  }
  return [
    `${edge.targetNode}.${edge.targetPin} = ${dataSourceEndpoint(edge)}`,
    `${edge.targetNode}.${edge.targetPin}= ${dataSourceEndpoint(edge)}`,
    `${edge.targetNode}.${edge.targetPin}=${dataSourceEndpoint(edge)}`,
  ]
}

function dataSourceEndpoint(edge: EdgeEditPayload): string {
  return edge.sourcePin ? `${edge.sourceNode}.${edge.sourcePin}` : edge.sourceNode
}

function edgeAnnotationKey(edge: EdgeEditPayload): string {
  return JSON.stringify(edge.annotations)
}

function edgeMetadataEquivalent(a: EdgeEditPayload, b: EdgeEditPayload): boolean {
  return a.id === b.id &&
    a.persistentId === b.persistentId &&
    sourceRangeKey(a.sourceRange) === sourceRangeKey(b.sourceRange) &&
    sourceRangeKey(a.sourceEndpointRange) === sourceRangeKey(b.sourceEndpointRange) &&
    sourceRangeKey(a.targetEndpointRange) === sourceRangeKey(b.targetEndpointRange) &&
    edgeAnnotationKey(a) === edgeAnnotationKey(b)
}

function logicBlockMatchesEdge(block: LogicBlock, edge: EdgeEditPayload): boolean {
  return block.kind === edge.blockKind && block.name === edge.blockName
}

function flowMatchesSelectedEdge(flow: FlowConn, edge: EdgeEditPayload): boolean {
  if (edge.kind !== 'exec') return false
  if (edge.persistentId && flow.persistent_id === edge.persistentId) return true
  return flow.from_node === edge.sourceNode &&
    flow.from_pin === edge.sourcePin &&
    flow.to_node === edge.targetNode &&
    flow.to_pin === edge.targetPin
}

function linkMatchesSelectedEdge(link: DataLink, edge: EdgeEditPayload): boolean {
  if (edge.kind !== 'data') return false
  if (edge.persistentId && link.persistent_id === edge.persistentId) return true
  return link.source_node === edge.sourceNode &&
    link.source_pin === edge.sourcePin &&
    link.target_node === edge.targetNode &&
    link.target_pin === edge.targetPin
}

function selectedEdgeFromFlow(block: LogicBlock, flow: FlowConn): EdgeEditPayload {
  return {
    id: flow.id,
    persistentId: flow.persistent_id,
    kind: 'exec',
    blockKind: block.kind,
    blockName: block.name,
    sourceNode: flow.from_node,
    sourcePin: flow.from_pin,
    targetNode: flow.to_node,
    targetPin: flow.to_pin,
    sourceRange: flow.source_range,
    sourceEndpointRange: flow.from_endpoint_source_range,
    targetEndpointRange: flow.to_endpoint_source_range,
    annotations: flow.annotations,
  }
}

function selectedEdgeFromLink(block: LogicBlock, link: DataLink): EdgeEditPayload {
  return {
    id: link.id,
    persistentId: link.persistent_id,
    kind: 'data',
    blockKind: block.kind,
    blockName: block.name,
    sourceNode: link.source_node,
    sourcePin: link.source_pin,
    targetNode: link.target_node,
    targetPin: link.target_pin,
    sourceRange: link.source_range,
    sourceEndpointRange: link.source_endpoint_source_range,
    targetEndpointRange: link.target_endpoint_source_range,
    annotations: link.annotations,
  }
}

function hydrateSelectedEdgeFromState(
  edge: EdgeEditPayload | null,
  state: GSState | null,
  graphIndex: number,
): EdgeEditPayload | null {
  if (!edge || !state) return edge
  const graph = state.module.graphs[graphIndex]
  if (!graph) return edge
  const blocks = [...graph.events, ...graph.functions]
  for (const block of blocks) {
    if (!logicBlockMatchesEdge(block, edge)) continue
    if (edge.kind === 'exec') {
      const flow = block.flows.find(candidate => flowMatchesSelectedEdge(candidate, edge))
      if (flow) return selectedEdgeFromFlow(block, flow)
    } else {
      const link = block.links.find(candidate => linkMatchesSelectedEdge(candidate, edge))
      if (link) return selectedEdgeFromLink(block, link)
    }
  }
  return edge
}

function CurrentGraphTextPanel({ graph, source }: { graph: GraphDef | undefined; source: string }) {
  return (
    <div className="flex h-full min-h-0 flex-col">
      <div className="flex items-center justify-between border-b px-3 py-2">
        <div className="text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">
          Current Graph
        </div>
        <div className="max-w-[45%] truncate text-[10px] text-muted-foreground/60" title={graph?.name ?? ''}>
          {graph?.name ?? 'None'}
        </div>
      </div>
      <textarea
        className="h-full min-h-0 flex-1 resize-none border-0 bg-background/30 p-3 font-mono text-[11px] leading-5 text-foreground/80 outline-none"
        value={source}
        readOnly
        spellCheck={false}
        data-current-graph-source="true"
      />
    </div>
  )
}

export default function App() {
  const [state, setState] = useState<GSState | null>(null)
  const [graphIndex, setGraphIndex] = useState(0)
  const [selectedNode, setSelectedNode] = useState<string | null>(null)
  const [selectedEdge, setSelectedEdge] = useState<EdgeEditPayload | null>(null)
  const [activeLogicBlock, setActiveLogicBlock] = useState<{ kind: 'event' | 'function'; name: string } | null>(null)
  const [sourceDiagnostics, setSourceDiagnostics] = useState<Diagnostic[]>([])
  const [sourceResolverEnvironment, setSourceResolverEnvironment] = useState<SourceDiagnosticsEnvironment | null>(null)
  const [sourceText, setSourceText] = useState('')
  const sourceTextRef = useRef('')
  const sourceEditableRef = useRef(true)
  const sourceBaseTextRef = useRef('')
  const sourceSessionChangedRef = useRef(false)
  const sourceApplyConfirmationBaseRef = useRef<string | null>(null)
  const sourceResolverConfirmationHashRef = useRef<string | null>(null)
  const sourceResolverSourceRef = useRef('')
  const [sourceSyncState, setSourceSyncState] = useState<SourceSyncState>('empty')
  const [sourceSyncDetail, setSourceSyncDetail] = useState('')
  const sourceSyncStateRef = useRef<SourceSyncState>('empty')
  const sourceAutoSyncKeyRef = useRef('')
  const [sourcePreviewLabel, setSourcePreviewLabel] = useState('Session')
  const [sourceEditable, setSourceEditable] = useState(true)
  const [declarationRenameContext, setDeclarationRenameContext] = useState<DeclarationRenameContext | null>(null)
  const [sourceNeedsBaselineConfirmation, setSourceNeedsBaselineConfirmation] = useState(false)
  const [pendingSourcePatchRange, setPendingSourcePatchRange] = useState<SourceRange | null>(null)
  const [pendingSourcePatchSummary, setPendingSourcePatchSummary] = useState('')
  const [focusedSourceRange, setFocusedSourceRange] = useState<SourceRange | null>(null)
  const [focusedDiagnostic, setFocusedDiagnostic] = useState<Diagnostic | null>(null)
  const [checkingSource, setCheckingSource] = useState(false)
  const [applyingSource, setApplyingSource] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [loading, setLoading] = useState(true)
  const [activeWorkbenchRequest, setActiveWorkbenchRequest] = useState<{ panel: 'source'; nonce: number } | null>(null)
  const retryRef = useRef<ReturnType<typeof setTimeout>>(undefined)
  const autoSaveTimerRef = useRef<number | undefined>(undefined)
  const autoSourceApplyTimerRef = useRef<number | undefined>(undefined)
  const autoSaveInFlightRef = useRef(false)
  const autoSourceApplyAttemptKeyRef = useRef('')
  const sourceApplyInFlightRef = useRef(false)

  const requestSourcePanel = useCallback(() => {
    setActiveWorkbenchRequest(current => ({ panel: 'source', nonce: (current?.nonce ?? 0) + 1 }))
  }, [])

  const scheduleSessionAutoSave = useCallback((nextState: GSState | null) => {
    if (!nextState?.file_path || !nextState.dirty) return
    if (autoSaveTimerRef.current) window.clearTimeout(autoSaveTimerRef.current)
    autoSaveTimerRef.current = window.setTimeout(() => {
      autoSaveTimerRef.current = undefined
      if (autoSaveInFlightRef.current) {
        scheduleSessionAutoSave(nextState)
        return
      }
      autoSaveInFlightRef.current = true
      void execCommand('save')
        .then(result => {
          if (result.state) setState(result.state)
          if (result.ok) {
            setSourceSyncDetail(current => current || `Auto-saved ${nextState.file_path}`)
          }
        })
        .catch(() => {
          setSourceSyncState('error')
          setSourceSyncDetail('Auto-save failed; session changes are still in memory')
        })
        .finally(() => {
          autoSaveInFlightRef.current = false
        })
    }, 900)
  }, [])

  useEffect(() => {
    sourceTextRef.current = sourceText
    if (!sourceText) {
      sourceBaseTextRef.current = ''
      sourceSessionChangedRef.current = false
      sourceApplyConfirmationBaseRef.current = null
      sourceResolverConfirmationHashRef.current = null
      sourceResolverSourceRef.current = ''
      sourceEditableRef.current = true
      setSourceEditable(true)
      setDeclarationRenameContext(null)
      setSourcePreviewLabel('Session')
      setSourceNeedsBaselineConfirmation(false)
      setPendingSourcePatchRange(null)
      setPendingSourcePatchSummary('')
      setSourceResolverEnvironment(null)
      setSourceSyncState('empty')
      setSourceSyncDetail('')
    }
  }, [sourceText])

  useEffect(() => {
    sourceSyncStateRef.current = sourceSyncState
  }, [sourceSyncState])

  const updatePendingSourcePatch = useCallback((text: string) => {
    const baseSource = sourceBaseTextRef.current
    if (!baseSource || !text || baseSource === text) {
      setPendingSourcePatchRange(null)
      setPendingSourcePatchSummary('')
      return
    }

    const diff = sourceDiffEdit(baseSource, text)
    if (!diff) {
      setPendingSourcePatchRange(null)
      setPendingSourcePatchSummary('')
      return
    }

    setPendingSourcePatchRange(diff.range)
    setPendingSourcePatchSummary(summarizeSourcePatch(baseSource, diff))
  }, [])

  const acceptSourceBaseline = useCallback((text: string) => {
    sourceBaseTextRef.current = text
    sourceSessionChangedRef.current = false
    sourceApplyConfirmationBaseRef.current = null
    sourceResolverConfirmationHashRef.current = null
    autoSourceApplyAttemptKeyRef.current = ''
    setSourceNeedsBaselineConfirmation(false)
    setPendingSourcePatchRange(null)
    setPendingSourcePatchSummary('')
  }, [])

  const setSourcePreviewEditable = useCallback((editable: boolean) => {
    sourceEditableRef.current = editable
    setSourceEditable(editable)
  }, [])

  const markSourceStale = useCallback((detail: string) => {
    if (!sourceTextRef.current) return
    if (!sourceEditableRef.current) return
    sourceSessionChangedRef.current = true
    sourceApplyConfirmationBaseRef.current = null
    sourceResolverConfirmationHashRef.current = null
    setSourceNeedsBaselineConfirmation(false)
    setSourceSyncState('stale')
    setSourceSyncDetail(detail)
  }, [])

  const refresh = useCallback(async () => {
    try {
      setLoading(true)
      const s = await fetchState()
      setState(s)
      setError(null)
      if (s.active_graph >= 0) setGraphIndex(s.active_graph)
      markSourceStale('Session state refreshed; source preview needs refresh')
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Connection failed')
    } finally {
      setLoading(false)
    }
  }, [markSourceStale])

  useEffect(() => {
    refresh()
    return () => {
      if (retryRef.current) clearTimeout(retryRef.current)
      if (autoSaveTimerRef.current) clearTimeout(autoSaveTimerRef.current)
      if (autoSourceApplyTimerRef.current) clearTimeout(autoSourceApplyTimerRef.current)
    }
  }, [refresh])

  useEffect(() => {
    setSelectedEdge(current => {
      const hydrated = hydrateSelectedEdgeFromState(current, state, graphIndex)
      if (!current || !hydrated) return current
      if (current.kind !== hydrated.kind ||
        current.blockKind !== hydrated.blockKind ||
        current.blockName !== hydrated.blockName ||
        current.sourceNode !== hydrated.sourceNode ||
        current.sourcePin !== hydrated.sourcePin ||
        current.targetNode !== hydrated.targetNode ||
        current.targetPin !== hydrated.targetPin) {
        return hydrated
      }
      return edgeMetadataEquivalent(current, hydrated) ? current : hydrated
    })
  }, [graphIndex, selectedEdge, state])

  // Auto-retry connection every 5s when disconnected
  useEffect(() => {
    if (error && !loading) {
      retryRef.current = setTimeout(refresh, 5000)
      return () => { if (retryRef.current) clearTimeout(retryRef.current) }
    }
  }, [error, loading, refresh])

  const runCommand = useCallback(async (cmd: string) => {
    const res = await execCommand(cmd)
    let nextState = state
    if (res.state) {
      setState(res.state)
      if (res.state.active_graph >= 0) setGraphIndex(res.state.active_graph)
      if (cmd.trim() !== 'save') scheduleSessionAutoSave(res.state)
      nextState = res.state
    } else {
      await refresh()
    }
    if (!res.ok && isImportCommand(cmd) && sourceTextRef.current) {
      const detail = (res.error || res.output || 'Import command failed').trim()
      setSourceSyncState('error')
      setSourceSyncDetail(`Import command failed at ${cmd}: ${detail}`)
      return res
    }
    if (res.ok && isImportCommand(cmd) && sourceTextRef.current) {
      try {
        const text = sourceTextRef.current
        const result = await fetchDiagnostics(text, sourceDiagnosticsOptions(text, nextState))
        setSourceDiagnostics(result.diagnostics)
        setSourceResolverEnvironment(result.environment ?? null)
        sourceResolverSourceRef.current = result.environment ? text : ''
        sourceSessionChangedRef.current = true
        sourceApplyConfirmationBaseRef.current = null
        sourceResolverConfirmationHashRef.current = null
        setSourceNeedsBaselineConfirmation(false)
        setSourceSyncState(result.ok ? 'stale' : 'error')
        setSourceSyncDetail(result.ok
          ? `Import command changed session Environment; source resolver metadata refreshed for: ${cmd}`
          : 'Import command changed session Environment, but source diagnostics now report issues')
        return res
      } catch {
        setSourceDiagnostics([])
        setSourceResolverEnvironment(null)
        sourceResolverSourceRef.current = ''
      }
    }
    markSourceStale(`Session changed by: ${cmd}`)
    return res
  }, [markSourceStale, refresh, scheduleSessionAutoSave, state])

  const handleExec = useCallback(async (cmd: string) => {
    try {
      await runCommand(cmd)
    } catch {
      await refresh()
    }
  }, [refresh, runCommand])

  const handleImportPlan = useCallback(async (commands: string[]) => {
    if (commands.length === 0) return
    setApplyingSource(true)
    try {
      setSourceSyncState('checking')
      setSourceSyncDetail(`Running ${commands.length} import command${commands.length === 1 ? '' : 's'} before source replay`)
      await new Promise<void>(resolve => window.setTimeout(resolve, 0))
      for (let index = 0; index < commands.length; index += 1) {
        const command = commands[index]
        try {
          const result = await runCommand(command)
          if (!result.ok) {
            const detail = (result.error || result.output || 'Import command failed').trim()
            setSourceSyncState('error')
            setSourceSyncDetail(`Import replay failed at ${command}: ${detail}`)
            return
          }
          setSourceSyncState('checking')
          setSourceSyncDetail(`Imported ${index + 1}/${commands.length}: ${command}`)
        } catch {
          await refresh()
          setSourceSyncState('error')
          setSourceSyncDetail(`Import replay failed at ${command}: connection failed`)
          return
        }
      }
      setSourceSyncState('stale')
      setSourceSyncDetail(`Import replay loaded ${commands.length} import${commands.length === 1 ? '' : 's'}; source resolver metadata refreshed`)
    } finally {
      setApplyingSource(false)
    }
  }, [refresh, runCommand])

  const handleUndo = useCallback(async () => {
    try {
      const res = await apiUndo()
      if (res.state) {
        setState(res.state)
        scheduleSessionAutoSave(res.state)
      }
      else await refresh()
      markSourceStale('Session changed by undo')
    } catch {
      await refresh()
    }
  }, [markSourceStale, refresh, scheduleSessionAutoSave])

  const handleRedo = useCallback(async () => {
    try {
      const res = await apiRedo()
      if (res.state) {
        setState(res.state)
        scheduleSessionAutoSave(res.state)
      }
      else await refresh()
      markSourceStale('Session changed by redo')
    } catch {
      await refresh()
    }
  }, [markSourceStale, refresh, scheduleSessionAutoSave])

  const handleEmit = useCallback(async () => {
    try {
      const text = await fetchEmit()
      const blob = new Blob([text], { type: 'text/plain' })
      const url = URL.createObjectURL(blob)
      const a = document.createElement('a')
      a.href = url
      a.download = (state?.file_path?.split(/[\\/]/).pop() || 'graph') + '.gs'
      a.click()
      URL.revokeObjectURL(url)
    } catch {
      // ignore
    }
  }, [state?.file_path])

  const syncSourceFromSession = useCallback(async (activatePanel: boolean, successDetail: string) => {
    if (activatePanel) requestSourcePanel()
    try {
      setCheckingSource(true)
      setSourceSyncState('checking')
      setSourceSyncDetail('Loading current emitted session source')
      const text = await fetchEmit()
      setSourcePreviewEditable(true)
      setDeclarationRenameContext(null)
      setSourcePreviewLabel('Session')
      setSourceText(text)
      acceptSourceBaseline(text)
      const result = await fetchDiagnostics(text, sourceDiagnosticsOptions(text, state))
      setSourceDiagnostics(result.diagnostics)
      setSourceResolverEnvironment(result.environment ?? null)
      sourceResolverSourceRef.current = result.environment ? text : ''
      setSourceSyncState('session')
      setSourceSyncDetail(successDetail)
    } catch {
      setSourceDiagnostics([])
      setSourceResolverEnvironment(null)
      setSourceSyncState('error')
      setSourceSyncDetail('Could not refresh source diagnostics')
      await refresh()
    } finally {
      setCheckingSource(false)
    }
  }, [acceptSourceBaseline, refresh, requestSourcePanel, setSourcePreviewEditable, state])

  const handleCheckSourceDiagnostics = useCallback(async () => {
    await syncSourceFromSession(true, 'Current source comes from backend /api/emit')
  }, [syncSourceFromSession])

  useEffect(() => {
    if (!state || loading || checkingSource || applyingSource || sourceNeedsBaselineConfirmation) return
    const hasLocalSourceEdit = Boolean(
      sourceTextRef.current &&
      sourceBaseTextRef.current &&
      sourceTextRef.current !== sourceBaseTextRef.current,
    )
    if (hasLocalSourceEdit) return
    const syncState = sourceSyncStateRef.current
    if (syncState !== 'empty' && syncState !== 'session' && syncState !== 'stale') return

    const graph = state.module.graphs[graphIndex]
    const key = [
      state.active_graph,
      state.dirty ? 'dirty' : 'clean',
      state.command_log.length,
      state.module.graphs.length,
      graph?.nodes.length ?? 0,
      graph?.events.length ?? 0,
      graph?.functions.length ?? 0,
      graph?.parameters.length ?? 0,
    ].join(':')
    if (sourceAutoSyncKeyRef.current === key) return
    sourceAutoSyncKeyRef.current = key
    void syncSourceFromSession(false, 'Source auto-synced from backend /api/emit')
  }, [applyingSource, checkingSource, graphIndex, loading, sourceNeedsBaselineConfirmation, state, syncSourceFromSession])

  const handleSourceTextChange = useCallback((text: string) => {
    setSourcePreviewEditable(true)
    setDeclarationRenameContext(null)
    setSourcePreviewLabel('Session')
    setSourceText(text)
    setSourceDiagnostics([])
    setSourceResolverEnvironment(null)
    sourceResolverSourceRef.current = ''
    setFocusedSourceRange(null)
    setFocusedDiagnostic(null)
    sourceApplyConfirmationBaseRef.current = null
    sourceResolverConfirmationHashRef.current = null
    setSourceNeedsBaselineConfirmation(false)
    updatePendingSourcePatch(text)
    setSourceSyncState(text ? 'edited' : 'empty')
    setSourceSyncDetail(text
      ? sourceSessionChangedRef.current
        ? 'Source edit is based on a stale backend source; auto-apply will verify the baseline first'
        : 'Source edit will auto-apply after diagnostics pass'
      : '')
  }, [setSourcePreviewEditable, updatePendingSourcePatch])

  const handleApplySourceText = useCallback(async (mode: 'manual' | 'background' = 'manual') => {
    const sourceAtStart = sourceTextRef.current
    if (!sourceAtStart || sourceApplyInFlightRef.current) return
    const background = mode === 'background'
    const isCurrentSource = () => sourceTextRef.current === sourceAtStart
    sourceApplyInFlightRef.current = true
    try {
      if (!background) setApplyingSource(true)
      setSourceSyncState('checking')
      setSourceSyncDetail(background ? 'Auto-checking source before save' : 'Checking manual source before Apply')
      const previousEnvironmentHash = sourceResolverSourceRef.current === sourceAtStart
        ? sourceResolverEnvironment?.environment_hash ?? ''
        : ''
      const diagnosticsOptions = sourceDiagnosticsOptions(sourceAtStart, state)
      let result: Awaited<ReturnType<typeof fetchDiagnostics>>
      try {
        result = await fetchDiagnostics(sourceAtStart, diagnosticsOptions)
      } catch {
        if (background && !isCurrentSource()) return
        setSourceDiagnostics([])
        setSourceResolverEnvironment(null)
        sourceResolverSourceRef.current = ''
        updatePendingSourcePatch(sourceAtStart)
        setSourceSyncState('error')
        setSourceSyncDetail(diagnosticsOptions.resolveImports
          ? 'Source import resolver diagnostics failed; buffer preserved'
          : background ? 'Auto source diagnostics failed; buffer preserved' : 'Manual source diagnostics failed; buffer preserved')
        return
      }
      if (background && !isCurrentSource()) return
      setSourceDiagnostics(result.diagnostics)
      setSourceResolverEnvironment(result.environment ?? null)
      sourceResolverSourceRef.current = result.environment ? sourceAtStart : ''
      if (!result.ok) {
        const diagnostic = firstLocatedDiagnostic(result.diagnostics)
        setFocusedDiagnostic(diagnostic)
        setFocusedSourceRange(diagnostic?.range ?? null)
        updatePendingSourcePatch(sourceAtStart)
        setSourceSyncState('edited')
        setSourceSyncDetail(background ? 'Auto source check found diagnostics; buffer preserved' : 'Manual source still has diagnostics; buffer preserved')
        return
      }
      if (diagnosticsOptions.resolveImports) {
        if (!result.environment) {
          setSourceSyncState('error')
          setSourceSyncDetail('Source imports were not resolved; refresh diagnostics before Apply')
          return
        }
        if (
          previousEnvironmentHash &&
          previousEnvironmentHash !== result.environment.environment_hash &&
          sourceResolverConfirmationHashRef.current !== result.environment.environment_hash
        ) {
          sourceResolverConfirmationHashRef.current = result.environment.environment_hash
          setSourceNeedsBaselineConfirmation(true)
          setSourceSyncState('stale')
          setSourceSyncDetail('Source imports resolved to a different environment since the last check; Apply again to use the refreshed resolver environment, or Revert to reload backend source')
          return
        }
        sourceResolverConfirmationHashRef.current = null
      }

      const currentSource = await fetchEmit()
      if (background && !isCurrentSource()) return
      const baseSource = sourceBaseTextRef.current
      if (baseSource && currentSource !== baseSource) sourceSessionChangedRef.current = true
      sourceApplyConfirmationBaseRef.current = null
      setSourceNeedsBaselineConfirmation(false)

      if (sourceAtStart === currentSource || (baseSource && sourceAtStart === baseSource && currentSource === baseSource)) {
        acceptSourceBaseline(sourceAtStart)
        setSourceSyncState('session')
        setSourceSyncDetail(background ? 'Auto source already matches backend session' : 'Manual source matches backend session; nothing to apply')
        return
      }

      const diffBaseSource = baseSource && currentSource === baseSource ? baseSource : currentSource
      const diff = sourceDiffEdit(diffBaseSource, sourceAtStart)
      const environmentGuard = result.environment
        ? {
            environmentHash: result.environment.environment_hash,
            ...diagnosticsOptions,
          }
        : {}
      if (diff) {
        const applied = await applySourcePatch(diff.range, diff.replacement, diffBaseSource, sourceAtStart, environmentGuard)
        if (background && !isCurrentSource()) return
        if (applied.state) {
          setState(applied.state)
          scheduleSessionAutoSave(applied.state)
        }
        if (applied.ok) {
          acceptSourceBaseline(sourceAtStart)
          setFocusedSourceRange(diff.range)
          setSourceSyncState(applied.fallback ? 'synced_snapshot' : 'synced_patch')
          setSourceSyncDetail(applied.fallback
            ? background ? 'Backend source changed during auto-save; synced by full source snapshot' : 'Backend source changed during Apply; synced by full source snapshot'
            : background ? 'Source auto-saved by minimal source patch' : 'Manual source applied by minimal source patch')
          return
        }
        updatePendingSourcePatch(sourceAtStart)
      }

      const applied = await applySource(sourceAtStart, environmentGuard)
      if (background && !isCurrentSource()) return
      if (applied.state) {
        setState(applied.state)
        scheduleSessionAutoSave(applied.state)
      }
      if (applied.ok) {
        acceptSourceBaseline(sourceAtStart)
        setSourceSyncState('synced_snapshot')
        setSourceSyncDetail(background ? 'Source auto-saved by full snapshot fallback' : 'Manual source applied by full snapshot fallback')
      } else {
        setSourceSyncState('error')
        setSourceSyncDetail(applied.error ?? (background ? 'Auto source apply failed' : 'Manual source apply failed'))
      }
    } catch {
      if (background && !isCurrentSource()) return
      setSourceSyncState('error')
      setSourceSyncDetail(background ? 'Auto source apply failed' : 'Manual source apply failed')
      updatePendingSourcePatch(sourceAtStart)
      await refresh()
    } finally {
      sourceApplyInFlightRef.current = false
      if (!background) setApplyingSource(false)
    }
  }, [acceptSourceBaseline, refresh, scheduleSessionAutoSave, sourceResolverEnvironment, state, updatePendingSourcePatch])

  const handleRevertSourceText = useCallback(async () => {
    try {
      setApplyingSource(true)
      setSourceSyncState('checking')
      setSourceSyncDetail('Reverting source buffer to backend session')
      const text = await fetchEmit()
      setSourcePreviewEditable(true)
      setDeclarationRenameContext(null)
      setSourcePreviewLabel('Session')
      setSourceText(text)
      acceptSourceBaseline(text)
      setFocusedSourceRange(null)
      setFocusedDiagnostic(null)
      const result = await fetchDiagnostics(text, sourceDiagnosticsOptions(text, state))
      setSourceDiagnostics(result.diagnostics)
      setSourceResolverEnvironment(result.environment ?? null)
      sourceResolverSourceRef.current = result.environment ? text : ''
      setSourceSyncState('session')
      setSourceSyncDetail('Source buffer reverted to current backend /api/emit')
    } catch {
      setSourceSyncState('error')
      setSourceSyncDetail('Could not revert source buffer')
      await refresh()
    } finally {
      setApplyingSource(false)
    }
  }, [acceptSourceBaseline, refresh, setSourcePreviewEditable, state])

  useEffect(() => {
    if (
      !sourceText ||
      checkingSource ||
      applyingSource ||
      sourceNeedsBaselineConfirmation ||
      !sourceEditable ||
      declarationRenameContext
    ) return
    if (!sourceBaseTextRef.current || sourceText === sourceBaseTextRef.current) return
    const attemptKey = `${sourceBaseTextRef.current.length}:${sourceText}`
    if (autoSourceApplyAttemptKeyRef.current === attemptKey) return
    if (autoSourceApplyTimerRef.current) window.clearTimeout(autoSourceApplyTimerRef.current)
    autoSourceApplyTimerRef.current = window.setTimeout(() => {
      autoSourceApplyTimerRef.current = undefined
      autoSourceApplyAttemptKeyRef.current = attemptKey
      void handleApplySourceText('background')
    }, 1400)
    return () => {
      if (autoSourceApplyTimerRef.current) {
        window.clearTimeout(autoSourceApplyTimerRef.current)
        autoSourceApplyTimerRef.current = undefined
      }
    }
  }, [
    applyingSource,
    checkingSource,
    declarationRenameContext,
    handleApplySourceText,
    sourceEditable,
    sourceNeedsBaselineConfirmation,
    sourceText,
  ])

  const handleAddNode = useCallback(async (typeName: string) => {
    const id = `${nodeInstancePrefix(typeName)}_${Date.now() % 100000}`
    await handleExec(`add_node ${typeName} ${id}`)
  }, [handleExec])

  const handleAddNodeAt = useCallback(async (typeName: string, x: number, y: number) => {
    const id = `${nodeInstancePrefix(typeName)}_${Date.now() % 100000}`
    await runCommand(`add_node ${typeName} ${id}`)
    await runCommand(`annotate node ${id} Position X=${Math.round(x)} Y=${Math.round(y)}`)
    return id
  }, [runCommand])

  const handleNodeMove = useCallback(async (instanceName: string, x: number, y: number) => {
    await handleExec(`annotate node ${instanceName} Position X=${x} Y=${y}`)
  }, [handleExec])

  const handleNodeDelete = useCallback(async (instanceName: string) => {
    setSelectedNode(current => current === instanceName ? null : current)
    setSelectedEdge(null)
    await handleExec(`remove_node ${instanceName}`)
  }, [handleExec])

  const handleNodeSelect = useCallback((instanceName: string | null) => {
    setSelectedNode(instanceName)
    if (instanceName) setSelectedEdge(null)
  }, [])

  const handleEdgeSelect = useCallback((edge: EdgeEditPayload | null) => {
    setSelectedEdge(edge)
    if (!edge) return
    setSelectedNode(null)
    setActiveLogicBlock({ kind: edge.blockKind, name: edge.blockName })
  }, [])

  const handleEdgeRedirected = useCallback((edge: EdgeEditPayload) => {
    setSelectedEdge({
      ...edge,
      id: undefined,
      sourceRange: undefined,
      sourceEndpointRange: undefined,
      targetEndpointRange: undefined,
    })
    setSelectedNode(null)
    setActiveLogicBlock({ kind: edge.blockKind, name: edge.blockName })
  }, [])

  const enterLogicBlock = useCallback(async (block: { kind: 'event' | 'function'; name: string }) => {
    const cmd = block.kind === 'event' ? `event ${block.name}` : `fn ${block.name}`
    await runCommand(cmd)
  }, [runCommand])

  const commandForEdge = useCallback((action: 'add' | 'remove', edge: EdgeEditPayload): string => {
    if (edge.kind === 'exec') {
      const cmd = action === 'add' ? 'flow' : 'unflow'
      return `${cmd} ${edge.sourceNode}.${edge.sourcePin} ${edge.targetNode}.${edge.targetPin}`
    }

    if (action === 'add') {
      return `link ${edge.targetNode}.${edge.targetPin} ${dataSourceEndpoint(edge)}`
    }
    return `unlink ${edge.targetNode}.${edge.targetPin}`
  }, [])

  const annotationCommandsForEdge = useCallback((edge: EdgeEditPayload): string[] => {
    if (edge.annotations.length === 0) return []
    if (edge.kind === 'exec') {
      const prefix = `annotate flow ${edge.blockKind} ${edge.blockName} ${edge.sourceNode}.${edge.sourcePin} ${edge.targetNode}.${edge.targetPin}`
      return edge.annotations.map(annotation => `${prefix} ${annotationCommandSuffix(annotation)}`)
    }

    const prefix = `annotate link ${edge.blockKind} ${edge.blockName} ${edge.targetNode}.${edge.targetPin} ${dataSourceEndpoint(edge)}`
    return edge.annotations.map(annotation => `${prefix} ${annotationCommandSuffix(annotation)}`)
  }, [])

  const handleEdgeEdit = useCallback(async (action: 'add' | 'remove', edge: EdgeEditPayload) => {
    try {
      await enterLogicBlock({ kind: edge.blockKind, name: edge.blockName })
      await runCommand(commandForEdge(action, edge))
    } catch {
      await refresh()
    }
  }, [commandForEdge, enterLogicBlock, refresh, runCommand])

  const handleEdgeReconnect = useCallback(async (previous: EdgeEditPayload, next: EdgeEditPayload) => {
    if (previous.kind !== next.kind ||
      previous.blockKind !== next.blockKind ||
      previous.blockName !== next.blockName) {
      await refresh()
      return
    }

    try {
      await enterLogicBlock({ kind: previous.blockKind, name: previous.blockName })
      const removeCommand = commandForEdge('remove', previous)
      const addCommand = commandForEdge('add', next)
      const rollbackCommand = commandForEdge('add', previous)
      const annotationCommands = annotationCommandsForEdge({
        ...next,
        annotations: previous.annotations,
      })
      const rollbackAnnotationCommands = annotationCommandsForEdge(previous)
      const removeResult = await runCommand(removeCommand)
      if (!removeResult.ok) {
        await refresh()
        return
      }

      const addResult = await runCommand(addCommand)
      if (!addResult.ok) {
        const rollbackResult = await runCommand(rollbackCommand)
        if (rollbackResult.ok) {
          for (const command of rollbackAnnotationCommands) {
            const result = await runCommand(command)
            if (!result.ok) {
              await refresh()
              return
            }
          }
        } else {
          await refresh()
        }
        return
      }

      for (const command of annotationCommands) {
        const result = await runCommand(command)
        if (!result.ok) {
          await refresh()
          return
        }
      }
      setSelectedEdge({
        ...next,
        id: undefined,
        persistentId: previous.persistentId,
        sourceRange: undefined,
        sourceEndpointRange: undefined,
        targetEndpointRange: undefined,
        annotations: previous.annotations,
      })
    } catch {
      await refresh()
    }
  }, [annotationCommandsForEdge, commandForEdge, enterLogicBlock, refresh, runCommand])

  const handleLogicBlockChange = useCallback(async (block: { kind: 'event' | 'function'; name: string } | null) => {
    setActiveLogicBlock(block)
    if (!block) return
    try {
      await enterLogicBlock(block)
    } catch {
      await refresh()
    }
  }, [enterLogicBlock, refresh])

  const handleGraphChange = useCallback(async (index: number) => {
    setGraphIndex(index)
    setSelectedNode(null)
    setActiveLogicBlock(null)
    setFocusedDiagnostic(null)
    setFocusedSourceRange(null)
    await handleExec(`switch_graph ${index}`)
  }, [handleExec])

  const handleGraphCreate = useCallback(async (name: string) => {
    setSelectedNode(null)
    setActiveLogicBlock({ kind: 'event', name: 'OnStart' })
    setFocusedDiagnostic(null)
    setFocusedSourceRange(null)
    try {
      const graphResult = await runCommand(`create_graph ${name}`)
      if (!graphResult.ok) {
        setActiveLogicBlock(null)
        return
      }
      const eventResult = await runCommand('event OnStart')
      if (!eventResult.ok) {
        setActiveLogicBlock(null)
      }
    } catch {
      setActiveLogicBlock(null)
      await refresh()
    }
  }, [refresh, runCommand])

  const handleGraphDelete = useCallback(async (name: string) => {
    setSelectedNode(null)
    setActiveLogicBlock(null)
    setFocusedDiagnostic(null)
    setFocusedSourceRange(null)
    await handleExec(`delete_graph ${name}`)
  }, [handleExec])

  const focusDiagnosticTarget = useCallback(async (diagnostic: Diagnostic) => {
    setFocusedDiagnostic(diagnostic)
    if (!isDefaultRange(diagnostic.range)) {
      setFocusedSourceRange(diagnostic.range)
      if (!sourceText || !sourceEditableRef.current) {
        void fetchEmit().then(text => {
          setSourcePreviewEditable(true)
          setSourcePreviewLabel('Session')
          setSourceText(text)
          acceptSourceBaseline(text)
        }).catch(() => undefined)
      }
    } else {
      setFocusedSourceRange(null)
    }
    const targetGraphName = diagnostic.target?.graph
    const targetGraphIndex = targetGraphName
      ? state?.module.graphs.findIndex(graph => graph.name === targetGraphName) ?? -1
      : graphIndex
    const nextGraphIndex = targetGraphIndex >= 0 ? targetGraphIndex : graphIndex
    const graph = state?.module.graphs[nextGraphIndex]
    if (!graph) return
    const target = diagnostic.target
    const nodeInstance = diagnosticNodeCandidate(graph, diagnostic)
    if (nextGraphIndex !== graphIndex) {
      setGraphIndex(nextGraphIndex)
      await runCommand(`switch_graph ${nextGraphIndex}`)
    }
    if (
      (target?.block_kind === 'event' || target?.block_kind === 'function') &&
      target.block_name &&
      [...graph.events, ...graph.functions].some(block => block.kind === target.block_kind && block.name === target.block_name)
    ) {
      setActiveLogicBlock({ kind: target.block_kind, name: target.block_name })
    } else {
      setActiveLogicBlock(null)
    }
    if (nodeInstance && graph.nodes.some(node => node.instance === nodeInstance)) {
      setSelectedNode(nodeInstance)
      return { graph, graphIndex: nextGraphIndex }
    }
    const graphName = target?.graph || diagnostic.context
    if (graph.name === graphName) {
      setSelectedNode(null)
    }
    return { graph, graphIndex: nextGraphIndex }
  }, [acceptSourceBaseline, graphIndex, runCommand, setSourcePreviewEditable, sourceText, state])

  const handleSourceRangeFocus = useCallback(async (range: SourceRange, sourceFile?: string) => {
    requestSourcePanel()
    setFocusedDiagnostic(null)
    setDeclarationRenameContext(null)
    if (isDefaultRange(range)) {
      setFocusedSourceRange(null)
      return
    }

    if (sourceFile) {
      try {
        setCheckingSource(true)
        const result = await fetchDeclarationSource(sourceFile)
        setSourcePreviewEditable(false)
        setSourcePreviewLabel(result.path || sourceFile)
        setSourceText(result.source)
        acceptSourceBaseline(result.source)
        setFocusedSourceRange(range)
        setSourceSyncState('declaration')
        setSourceSyncDetail(`Declaration source ${result.path || sourceFile}; ${result.content_hash}`)
        sourceSessionChangedRef.current = false
        sourceApplyConfirmationBaseRef.current = null
        sourceResolverConfirmationHashRef.current = null
      } catch {
        setSourceSyncState('error')
        setSourceSyncDetail(`Unable to load declaration source ${sourceFile}`)
      } finally {
        setCheckingSource(false)
      }
      return
    }

    setFocusedSourceRange(range)
    if (sourceTextRef.current && sourceEditableRef.current) return

    try {
      setCheckingSource(true)
      const text = await fetchEmit()
      setSourcePreviewEditable(true)
      setDeclarationRenameContext(null)
      setSourcePreviewLabel('Session')
      setSourceText(text)
      acceptSourceBaseline(text)
      setSourceSyncState('session')
      setSourceSyncDetail('Loaded emitted source for selected graph element')
    } catch {
      setSourceSyncState('error')
      setSourceSyncDetail('Unable to load source for selected graph element')
    } finally {
      setCheckingSource(false)
    }
  }, [acceptSourceBaseline, requestSourcePanel, setSourcePreviewEditable])

  const focusSessionSourceReference = useCallback(async (
    range: SourceRange | undefined,
    needles: string[],
    detail: string,
  ) => {
    requestSourcePanel()
    let text = sourceTextRef.current
    if (!text || !sourceEditableRef.current) {
      try {
        setCheckingSource(true)
        text = await fetchEmit()
        setSourcePreviewEditable(true)
        setDeclarationRenameContext(null)
        setSourcePreviewLabel('Session')
        setSourceText(text)
        acceptSourceBaseline(text)
        setSourceSyncState('session')
      } catch {
        setSourceSyncState('error')
        setSourceSyncDetail('Unable to load source for selected graph element')
        return
      } finally {
        setCheckingSource(false)
      }
    }

    const fallbackRange = sourceLineRange(text, needles)
    if (fallbackRange) {
      setFocusedDiagnostic(null)
      setDeclarationRenameContext(null)
      setFocusedSourceRange(fallbackRange)
      if (!sourceSessionChangedRef.current) {
        setSourceSyncDetail(detail)
      }
      return
    }

    if (range && !isDefaultRange(range)) {
      await handleSourceRangeFocus(range)
      return
    }

    setFocusedSourceRange(null)
  }, [acceptSourceBaseline, handleSourceRangeFocus, requestSourcePanel, setSourcePreviewEditable])

  const handleOpenDeclarationSource = useCallback(async (path: string, contentHash: string) => {
    requestSourcePanel()
    try {
      setCheckingSource(true)
      setFocusedDiagnostic(null)
      setFocusedSourceRange(null)
      setDeclarationRenameContext(null)
      const result = await fetchDeclarationSource(path)
      setSourcePreviewEditable(false)
      setSourcePreviewLabel(result.path || path)
      setSourceText(result.source)
      acceptSourceBaseline(result.source)
      setSourceSyncState('declaration')
      setSourceSyncDetail(`Declaration source ${result.path || path}; ${result.content_hash || contentHash}`)
      sourceSessionChangedRef.current = false
      sourceApplyConfirmationBaseRef.current = null
      sourceResolverConfirmationHashRef.current = null
    } catch {
      setSourceSyncState('error')
      setSourceSyncDetail(`Unable to load declaration source ${path}`)
    } finally {
      setCheckingSource(false)
    }
  }, [acceptSourceBaseline, requestSourcePanel, setSourcePreviewEditable])

  const handleRenameDeclaration = useCallback(async (newName: string) => {
    if (!declarationRenameContext) return
    const commandParts = (() => {
      switch (declarationRenameContext.kind) {
        case 'type':
          return [
            'apply_import_type_rename',
            quoteCommandArg(declarationRenameContext.path),
            declarationRenameContext.oldName,
            newName,
            declarationRenameContext.contentHash,
          ]
        case 'node':
          return [
            'apply_import_node_rename',
            quoteCommandArg(declarationRenameContext.path),
            declarationRenameContext.oldName,
            newName,
            declarationRenameContext.contentHash,
          ]
        case 'node_pin':
          return [
            'apply_import_node_pin_rename',
            quoteCommandArg(declarationRenameContext.path),
            declarationRenameContext.ownerName,
            declarationRenameContext.oldName,
            newName,
            declarationRenameContext.contentHash,
          ]
        case 'schema':
          return [
            'apply_import_schema_rename',
            quoteCommandArg(declarationRenameContext.path),
            declarationRenameContext.oldName,
            newName,
            declarationRenameContext.contentHash,
          ]
        case 'schema_field':
          return [
            'apply_import_schema_field_rename',
            quoteCommandArg(declarationRenameContext.path),
            declarationRenameContext.ownerName,
            declarationRenameContext.oldName,
            newName,
            declarationRenameContext.contentHash,
          ]
      }
    })()
    const command = commandParts.join(' ')

    setApplyingSource(true)
    setSourceSyncState('checking')
    setSourceSyncDetail(`Applying declaration rename: ${declarationRenameContext.oldName} -> ${newName}`)
    try {
      const result = await runCommand(command)
      if (!result.ok) {
        setSourceSyncState('error')
        setSourceSyncDetail((result.error || result.output || 'Declaration rename failed').trim())
        return
      }

      const declaration = await fetchDeclarationSource(declarationRenameContext.path)
      setSourcePreviewEditable(false)
      setSourcePreviewLabel(declaration.path || declarationRenameContext.path)
      setSourceText(declaration.source)
      acceptSourceBaseline(declaration.source)
      setFocusedSourceRange(null)
      setFocusedDiagnostic(null)
      setDeclarationRenameContext({
        ...declarationRenameContext,
        contentHash: declaration.content_hash,
        oldName: newName,
      })
      setSourceSyncState('declaration')
      setSourceSyncDetail(`Declaration rename applied by CLI command; ${declaration.content_hash}`)
    } catch {
      setSourceSyncState('error')
      setSourceSyncDetail('Declaration rename failed')
      await refresh()
    } finally {
      setApplyingSource(false)
    }
  }, [acceptSourceBaseline, declarationRenameContext, refresh, runCommand, setSourcePreviewEditable])

  const handleLocateDiagnostic = useCallback((diagnostic: Diagnostic) => {
    void focusDiagnosticTarget(diagnostic)
  }, [focusDiagnosticTarget])

  const handleApplyDiagnosticAction = useCallback(async (action: DiagnosticAction, diagnostic: Diagnostic) => {
    const command = (action.command ?? '').trim()
    const replacement = action.replacement ?? ''
    if (!command) {
      if (replacement.length > 0 || action.edit_range) {
        const editRange = action.edit_range ?? diagnostic.range
        try {
          const baseSource = sourceText || await fetchEmit()
          const edit = applySourceEdit(baseSource, editRange, replacement)
          if (!edit) {
            handleLocateDiagnostic(diagnostic)
            return
          }
          setCheckingSource(true)
          setSourceText(edit.source)
          setSourceSyncState('edited')
          setSourceSyncDetail('Local source edit is rechecked before session sync')
          setFocusedSourceRange(edit.range)
          setFocusedDiagnostic(diagnostic)
          const diagnosticsOptions = sourceDiagnosticsOptions(edit.source, state)
          const result = await fetchDiagnostics(edit.source, diagnosticsOptions)
          setSourceDiagnostics(result.diagnostics)
          setSourceResolverEnvironment(result.environment ?? null)
          sourceResolverSourceRef.current = result.environment ? edit.source : ''
          if (result.ok) {
            const environmentGuard = result.environment
              ? {
                  environmentHash: result.environment.environment_hash,
                  ...diagnosticsOptions,
                }
              : {}
            const applied = await applySourcePatch(editRange, replacement, baseSource, edit.source, environmentGuard)
            if (applied.state) {
              setState(applied.state)
              scheduleSessionAutoSave(applied.state)
            }
            if (applied.ok) {
              acceptSourceBaseline(edit.source)
              setSourceSyncState(applied.fallback ? 'synced_snapshot' : 'synced_patch')
              setSourceSyncDetail(applied.fallback
                ? 'Backend source changed; synced by full source snapshot'
                : 'Synced by source patch against checked base source')
            } else {
              setSourceSyncState('error')
              setSourceSyncDetail(applied.error ?? 'Source patch sync failed')
            }
          } else {
            setSourceSyncState('edited')
            setSourceSyncDetail('Edited source still has diagnostics; session not changed')
          }
        } catch {
          setSourceSyncState('error')
          setSourceSyncDetail('Source action failed before session sync')
          await refresh()
        } finally {
          setCheckingSource(false)
        }
        return
      }
      handleLocateDiagnostic(diagnostic)
      return
    }
    try {
      await focusDiagnosticTarget(diagnostic)
      const target = diagnostic.target
      if (target?.block_kind === 'event' && target.block_name) {
        await runCommand(`event ${target.block_name}`)
      } else if (target?.block_kind === 'function' && target.block_name) {
        await runCommand(`fn ${target.block_name}`)
      }
      await runCommand(command)
    } catch {
      await refresh()
    }
  }, [acceptSourceBaseline, focusDiagnosticTarget, handleLocateDiagnostic, refresh, runCommand, scheduleSessionAutoSave, sourceText, state])

  const sessionDiagnostics = useMemo(
    () => collectSessionDiagnostics(state),
    [state],
  )

  const canvasDiagnostics = useMemo(() => [
    ...sessionDiagnostics,
    ...sourceDiagnostics,
  ], [sessionDiagnostics, sourceDiagnostics])

  const handleSearchNavigate = useCallback(async (result: GraphSearchResult) => {
    if (result.kind === 'diagnostic' && result.diagnostic) {
      handleLocateDiagnostic(result.diagnostic)
      return
    }

    if (result.kind === 'connection' &&
      result.blockKind &&
      result.blockName &&
      result.connectionKind &&
      result.sourceNode !== undefined &&
      result.sourcePin !== undefined &&
      result.targetNode &&
      result.targetPin) {
      setFocusedDiagnostic(null)
      setDeclarationRenameContext(null)
      if (result.graphIndex !== graphIndex) {
        setGraphIndex(result.graphIndex)
        await runCommand(`switch_graph ${result.graphIndex}`)
      }
      setSelectedNode(null)
      setActiveLogicBlock({ kind: result.blockKind, name: result.blockName })
      setSelectedEdge({
        id: result.connectionId,
        persistentId: result.persistentId,
        kind: result.connectionKind === 'flow' ? 'exec' : 'data',
        blockKind: result.blockKind,
        blockName: result.blockName,
        sourceNode: result.sourceNode,
        sourcePin: result.sourcePin,
        targetNode: result.targetNode,
        targetPin: result.targetPin,
        sourceRange: result.sourceRange,
        sourceEndpointRange: result.sourceEndpointRange,
        targetEndpointRange: result.targetEndpointRange,
        annotations: result.annotations ?? [],
      })
      if (result.sourceRange) await handleSourceRangeFocus(result.sourceRange)
      return
    }

    if (result.kind === 'source_reference' && result.sourceRange) {
      setFocusedDiagnostic(null)
      setDeclarationRenameContext(null)
      if (result.graphIndex !== graphIndex) {
        setGraphIndex(result.graphIndex)
        await runCommand(`switch_graph ${result.graphIndex}`)
      }
      setSelectedNode(result.nodeInstance ?? null)
      if (result.blockKind && result.blockName) {
        setActiveLogicBlock({ kind: result.blockKind, name: result.blockName })
      } else {
        setActiveLogicBlock(null)
      }
      await handleSourceRangeFocus(result.sourceRange, result.sourceFile)
      return
    }

    if (result.kind === 'declaration' && result.sourceFile && result.sourceRange) {
      setSelectedNode(null)
      setActiveLogicBlock(null)
      await handleSourceRangeFocus(result.sourceRange, result.sourceFile)
      if (result.declarationKind === 'type' || result.declarationKind === 'node' || result.declarationKind === 'schema') {
        try {
          const declaration = await fetchDeclarationSource(result.sourceFile)
          setDeclarationRenameContext({
            kind: result.declarationKind,
            path: declaration.path || result.sourceFile,
            contentHash: declaration.content_hash,
            oldName: result.label,
          })
        } catch {
          setDeclarationRenameContext(null)
        }
      } else if (result.declarationKind === 'pin' && result.declarationOwner && result.pinName) {
        try {
          const declaration = await fetchDeclarationSource(result.sourceFile)
          setDeclarationRenameContext({
            kind: 'node_pin',
            path: declaration.path || result.sourceFile,
            contentHash: declaration.content_hash,
            ownerName: result.declarationOwner,
            oldName: result.pinName,
          })
        } catch {
          setDeclarationRenameContext(null)
        }
      } else if (result.declarationKind === 'schema_field' && result.declarationOwner && result.schemaFieldName) {
        try {
          const declaration = await fetchDeclarationSource(result.sourceFile)
          setDeclarationRenameContext({
            kind: 'schema_field',
            path: declaration.path || result.sourceFile,
            contentHash: declaration.content_hash,
            ownerName: result.declarationOwner,
            oldName: result.schemaFieldName,
          })
        } catch {
          setDeclarationRenameContext(null)
        }
      }
      return
    }

    setFocusedDiagnostic(null)
    setFocusedSourceRange(null)
    setDeclarationRenameContext(null)
    if (result.graphIndex !== graphIndex) {
      setGraphIndex(result.graphIndex)
      await runCommand(`switch_graph ${result.graphIndex}`)
    }

    if (result.kind === 'graph') {
      setSelectedNode(null)
      setActiveLogicBlock(null)
      if (result.sourceRange) await handleSourceRangeFocus(result.sourceRange)
      return
    }

    if ((result.kind === 'node' || result.kind === 'pin') && result.nodeInstance) {
      setSelectedNode(result.nodeInstance)
      if (result.sourceRange) await handleSourceRangeFocus(result.sourceRange)
      return
    }

    if (result.kind === 'block' && result.blockKind && result.blockName) {
      setSelectedNode(null)
      setActiveLogicBlock({ kind: result.blockKind, name: result.blockName })
      if (result.sourceRange) await handleSourceRangeFocus(result.sourceRange)
    }
  }, [graphIndex, handleLocateDiagnostic, handleSourceRangeFocus, runCommand])

  const currentGraph = state?.module.graphs[graphIndex]
  const currentGraphText = useMemo(() => currentGraphSource(currentGraph), [currentGraph])
  const handleNodesDuplicate = useCallback(async (instanceNames: string[]) => {
    if (!currentGraph || instanceNames.length === 0) return
    const selected = new Set(instanceNames)
    const orderedNodes = currentGraph.nodes.filter(node => selected.has(node.instance))
    if (orderedNodes.length === 0) return

    const existingNames = new Set(currentGraph.nodes.map(node => node.instance))
    const createdNames: string[] = []
    for (const node of orderedNodes) {
      const newName = nextDuplicateNodeName(node.instance, existingNames)
      const position = nodeCanvasPosition(node, currentGraph.nodes.indexOf(node))
      const initializer = node.init?.trim()
      const addResult = await runCommand(`add_node ${node.type} ${newName}${initializer ? ` ${quoteCommandArg(initializer)}` : ''}`)
      if (!addResult.ok) return
      const positionResult = await runCommand(
        `annotate node ${newName} Position X=${Math.round(position.x + 48)} Y=${Math.round(position.y + 48)}`,
      )
      if (!positionResult.ok) return
      createdNames.push(newName)
    }
    if (createdNames.length > 0) {
      setSelectedNode(createdNames[createdNames.length - 1])
      setSelectedEdge(null)
    }
  }, [currentGraph, runCommand])
  const handleCommentBoxCreate = useCallback(async (box: CommentBoxEditPayload) => {
    if (!currentGraph) return
    const annotationName = nextCommentBoxAnnotationName(currentGraph)
    await runCommand(commentBoxCommand(annotationName, box))
  }, [currentGraph, runCommand])
  const handleCommentBoxMove = useCallback(async (box: CommentBoxEditPayload) => {
    await runCommand(commentBoxCommand(box.annotationName, box))
  }, [runCommand])
  const handleCommentBoxDelete = useCallback(async (annotationName: string) => {
    await runCommand(`unannotate graph ${annotationName}`)
  }, [runCommand])
  const sourceEnvNotice = useMemo(
    () => sourceEnvironmentNotice(sourceText, state, sourceResolverEnvironment),
    [sourceText, state, sourceResolverEnvironment],
  )
  const sessionImports = useMemo(
    () => state?.module.imports
      .filter(importDef => importDef.loaded)
      .flatMap(importDef => [importDef.path, importDef.normalized_path ?? ''])
      .filter(Boolean) ?? [],
    [state],
  )
  const sourceBusy = checkingSource || applyingSource || sourceSyncState === 'checking'

  useEffect(() => {
    if (!currentGraph) return
    if (selectedEdge) {
      void focusSessionSourceReference(
        selectedEdge.sourceRange,
        edgeSourceNeedles(selectedEdge),
        `Focused source for ${selectedEdge.kind} edge ${selectedEdge.sourceNode}.${selectedEdge.sourcePin} -> ${selectedEdge.targetNode}.${selectedEdge.targetPin}`,
      )
      return
    }
    if (!selectedNode) return
    const node = currentGraph.nodes.find(candidate => candidate.instance === selectedNode)
    if (!node) return
    void focusSessionSourceReference(
      node.source_range,
      nodeSourceNeedles(node),
      `Focused source for node ${node.instance}`,
    )
  }, [currentGraph, focusSessionSourceReference, selectedEdge, selectedNode])

  const canApplySource = sourceEditable && (sourceSyncState === 'edited' ||
    sourceSyncState === 'stale' ||
    sourceNeedsBaselineConfirmation ||
    (countImportDeclarations(sourceText) > 0 &&
      sourceResolverSourceRef.current === sourceText &&
      Boolean(sourceResolverEnvironment)))

  useEffect(() => {
    const graph = state?.module.graphs[graphIndex]
    const blocks = graph ? [...graph.events, ...graph.functions] : []
    if (blocks.length === 0) {
      if (activeLogicBlock) setActiveLogicBlock(null)
      return
    }

    const stillExists = activeLogicBlock &&
      blocks.some(block => block.kind === activeLogicBlock.kind && block.name === activeLogicBlock.name)
    if (!stillExists) {
      setActiveLogicBlock({ kind: blocks[0].kind, name: blocks[0].name })
    }
  }, [activeLogicBlock, graphIndex, state])

  // Keyboard shortcuts (Ctrl/Cmd support)
  useEffect(() => {
    function onKeyDown(e: KeyboardEvent) {
      if (e.defaultPrevented || isEditableKeyboardTarget(e.target) || !isGraphKeyboardTarget(e.target)) return
      const mod = e.ctrlKey || e.metaKey
      const key = e.key.toLowerCase()
      if (mod && key === 'z') { e.preventDefault(); handleUndo() }
      if (mod && key === 'y') { e.preventDefault(); handleRedo() }
    }
    window.addEventListener('keydown', onKeyDown)
    return () => window.removeEventListener('keydown', onKeyDown)
  }, [handleUndo, handleRedo])

  return (
    <TooltipProvider delayDuration={200}>
      <div className="h-screen w-screen flex flex-col overflow-hidden bg-background">
        {/* Top toolbar */}
        <Toolbar
          state={state}
          graphIndex={graphIndex}
          selectedNode={selectedNode}
          activeLogicBlock={activeLogicBlock}
          diagnostics={canvasDiagnostics}
          onGraphChange={handleGraphChange}
          onGraphCreate={handleGraphCreate}
          onGraphDelete={handleGraphDelete}
          onLogicBlockChange={handleLogicBlockChange}
          onSearchNavigate={handleSearchNavigate}
          onUndo={handleUndo}
          onRedo={handleRedo}
          onRefresh={refresh}
          onEmit={handleEmit}
        />

        <div className="flex-1 min-h-0">
          <WorkbenchLayout
            activePanelRequest={activeWorkbenchRequest}
            palette={<NodePalette types={state?.types ?? []} onAddNode={handleAddNode} />}
            canvas={error ? (
              <div className="flex h-full flex-col items-center justify-center gap-4 blueprint-grid">
                <div className="flex flex-col items-center gap-3 rounded-xl border border-border/50 bg-card/80 p-8 backdrop-blur">
                  <div className="flex h-10 w-10 items-center justify-center rounded-full bg-destructive/10">
                    <div className="h-3 w-3 animate-pulse rounded-full bg-destructive" />
                  </div>
                  <div className="text-sm font-medium text-foreground/70">
                    Cannot connect to backend
                  </div>
                  <div className="max-w-[240px] text-center text-[11px] text-muted-foreground/50">
                    {error}
                  </div>
                  <div className="text-[10px] text-muted-foreground/40">
                    Retrying automatically...
                  </div>
                </div>
              </div>
            ) : (
              <ErrorBoundary>
                <FlowCanvas
                  state={state}
                  graphIndex={graphIndex}
                  selectedNode={selectedNode}
                  onNodeCreate={handleAddNodeAt}
                  onNodeSelect={handleNodeSelect}
                  onNodeMove={handleNodeMove}
                  onNodeDelete={handleNodeDelete}
                  onNodesDuplicate={handleNodesDuplicate}
                  onCommentBoxCreate={handleCommentBoxCreate}
                  onCommentBoxMove={handleCommentBoxMove}
                  onCommentBoxDelete={handleCommentBoxDelete}
                  onRefresh={refresh}
                  activeLogicBlock={activeLogicBlock}
                  onEdgeCreate={(edge) => handleEdgeEdit('add', edge)}
                  onEdgeDelete={(edge) => handleEdgeEdit('remove', edge)}
                  onEdgeReconnect={handleEdgeReconnect}
                  onEdgeSelect={handleEdgeSelect}
                  diagnostics={canvasDiagnostics}
                  focusedDiagnostic={focusedDiagnostic}
                />
              </ErrorBoundary>
            )}
            properties={(
              <PropertiesPanel
                state={state}
                graphIndex={graphIndex}
                selectedNode={selectedNode}
                selectedEdge={selectedEdge}
                diagnostics={canvasDiagnostics}
                focusedDiagnostic={focusedDiagnostic}
                onSourceRangeFocus={handleSourceRangeFocus}
                onEdgeRedirected={handleEdgeRedirected}
                onExec={handleExec}
              />
            )}
            diagnostics={(
              <DiagnosticsPanel
                diagnostics={sessionDiagnostics}
                sourceDiagnostics={sourceDiagnostics}
                checkingSource={checkingSource}
                onCheckSource={handleCheckSourceDiagnostics}
                onLocateDiagnostic={handleLocateDiagnostic}
                onApplyDiagnosticAction={handleApplyDiagnosticAction}
              />
            )}
            graphText={<CurrentGraphTextPanel graph={currentGraph} source={currentGraphText} />}
            source={(
              <SourcePreviewPanel
                source={sourceText}
                sourceLabel={sourcePreviewLabel}
                sourceEditable={sourceEditable}
                focusedRange={focusedSourceRange}
                checkingSource={checkingSource}
                syncState={sourceSyncState}
                syncDetail={sourceSyncDetail}
                applyingSource={applyingSource}
                canApplySource={canApplySource}
                pendingPatchRange={pendingSourcePatchRange}
                pendingPatchSummary={pendingSourcePatchSummary}
                sourceDiagnostics={sourceDiagnostics}
                declarationRenameContext={declarationRenameContext}
                onCheckSource={handleCheckSourceDiagnostics}
                onSourceChange={handleSourceTextChange}
                onApplySource={handleApplySourceText}
                onRevertSource={handleRevertSourceText}
                onRenameDeclaration={handleRenameDeclaration}
              />
            )}
            console={(
              <CommandLog
                log={state?.command_log ?? []}
                onExec={handleExec}
                sourceEnvironmentNotice={sourceEnvNotice}
                sourceResolverEnvironment={sourceResolverEnvironment}
                sessionImports={sessionImports}
                sourceBusy={sourceBusy}
                onImportPlan={handleImportPlan}
                onOpenDeclarationSource={handleOpenDeclarationSource}
              />
            )}
          />
        </div>
      </div>
    </TooltipProvider>
  )
}
