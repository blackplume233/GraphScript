import type { GSState, ExecResponse, UndoRedoResponse, DiagnosticsResponse, ApplySourceResponse, DeclarationSourceResponse, CompletionResponse, SourceRange } from './types'

const BASE = ''

export async function fetchState(): Promise<GSState> {
  const res = await fetch(`${BASE}/api/state`)
  if (!res.ok) throw new Error(`GET /api/state: ${res.status}`)
  return res.json()
}

export async function execCommand(command: string): Promise<ExecResponse> {
  const res = await fetch(`${BASE}/api/exec`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ command }),
  })
  if (!res.ok) throw new Error(`POST /api/exec: ${res.status}`)
  return res.json()
}

export async function undo(): Promise<UndoRedoResponse> {
  const res = await fetch(`${BASE}/api/undo`, { method: 'POST' })
  if (!res.ok) throw new Error(`POST /api/undo: ${res.status}`)
  return res.json()
}

export async function redo(): Promise<UndoRedoResponse> {
  const res = await fetch(`${BASE}/api/redo`, { method: 'POST' })
  if (!res.ok) throw new Error(`POST /api/redo: ${res.status}`)
  return res.json()
}

export async function fetchEmit(): Promise<string> {
  const res = await fetch(`${BASE}/api/emit`)
  if (!res.ok) throw new Error(`GET /api/emit: ${res.status}`)
  return res.text()
}

export async function fetchDeclarationSource(path: string): Promise<DeclarationSourceResponse> {
  const res = await fetch(`${BASE}/api/declaration_source?path=${encodeURIComponent(path)}`)
  if (!res.ok) throw new Error(`GET /api/declaration_source: ${res.status}`)
  return res.json()
}

export interface FetchDiagnosticsOptions {
  resolveImports?: boolean
  sourcePath?: string
  baseDir?: string
}

export async function fetchDiagnostics(
  source?: string,
  options: FetchDiagnosticsOptions = {},
): Promise<DiagnosticsResponse> {
  const useJsonBody = source !== undefined && options.resolveImports === true
  const init: RequestInit = source === undefined
    ? {}
    : useJsonBody
      ? {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            source,
            resolve_imports: options.resolveImports === true,
            ...(options.sourcePath ? { source_path: options.sourcePath } : {}),
            ...(options.baseDir ? { base_dir: options.baseDir } : {}),
          }),
        }
      : {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: source,
      }
  const res = await fetch(`${BASE}/api/diagnostics`, init)
  if (!res.ok) {
    throw new Error(`${source === undefined ? 'GET' : 'POST'} /api/diagnostics: ${res.status}`)
  }
  return res.json()
}

export async function fetchCompletions(
  source: string,
  line: number,
  column: number,
): Promise<CompletionResponse> {
  const res = await fetch(`${BASE}/api/completion`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ source, line, column }),
  })
  if (!res.ok) throw new Error(`POST /api/completion: ${res.status}`)
  return res.json()
}

export interface SourceEnvironmentGuardOptions {
  environmentHash?: string
  resolveImports?: boolean
  sourcePath?: string
  baseDir?: string
}

export async function applySource(
  source: string,
  options: SourceEnvironmentGuardOptions = {},
): Promise<ApplySourceResponse> {
  const guarded = Boolean(options.environmentHash)
  const res = await fetch(`${BASE}/api/source`, guarded
    ? {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          source,
          environment_hash: options.environmentHash,
          resolve_imports: options.resolveImports === true,
          ...(options.sourcePath ? { source_path: options.sourcePath } : {}),
          ...(options.baseDir ? { base_dir: options.baseDir } : {}),
        }),
      }
    : {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: source,
      })
  if (!res.ok) throw new Error(`POST /api/source: ${res.status}`)
  return res.json()
}

function sourceHash(source: string): string {
  let hash = 0xcbf29ce484222325n
  const prime = 0x100000001b3n
  const mask = 0xffffffffffffffffn
  for (const byte of new TextEncoder().encode(source)) {
    hash ^= BigInt(byte)
    hash = (hash * prime) & mask
  }
  return hash.toString(16).padStart(16, '0')
}

export async function applySourcePatch(
  range: SourceRange,
  replacement: string,
  baseSource?: string,
  fallbackSource?: string,
  options: SourceEnvironmentGuardOptions = {},
): Promise<ApplySourceResponse> {
  const payload: Record<string, unknown> = {
    start_line: range.start.line,
    start_column: range.start.column,
    end_line: range.end.line,
    end_column: range.end.column,
    replacement,
  }
  if (baseSource !== undefined) payload.base_hash = sourceHash(baseSource)
  if (fallbackSource !== undefined) payload.fallback_source = fallbackSource
  if (options.environmentHash) {
    payload.environment_hash = options.environmentHash
    payload.resolve_imports = options.resolveImports === true
    if (options.sourcePath) payload.source_path = options.sourcePath
    if (options.baseDir) payload.base_dir = options.baseDir
  }

  const res = await fetch(`${BASE}/api/source_patch`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  })
  if (!res.ok) throw new Error(`POST /api/source_patch: ${res.status}`)
  return res.json()
}
