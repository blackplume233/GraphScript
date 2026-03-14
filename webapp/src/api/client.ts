import type { GSState, ExecResponse, UndoRedoResponse } from './types'

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
