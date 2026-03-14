export interface PinDef {
  name: string
  kind: 'exec' | 'data'
  direction: 'in' | 'out'
  type: string
}

export interface NodeTypeDef {
  type_name: string
  is_native: boolean
  source_graph: string
  tags: string[]
  pins: PinDef[]
}

export interface GraphParam {
  name: string
  type: string
  direction: 'in' | 'out' | 'var'
  default: string
}

export interface NodeInst {
  type: string
  instance: string
  init: string
}

export interface FlowConn {
  from_node: string
  from_pin: string
  to_node: string
  to_pin: string
}

export interface DataLink {
  target_node: string
  target_pin: string
  source_node: string
  source_pin: string
}

export interface LogicBlock {
  name: string
  kind: 'event' | 'function'
  flows: FlowConn[]
  links: DataLink[]
}

export interface GraphDef {
  name: string
  base_type: string | null
  parameters: GraphParam[]
  nodes: NodeInst[]
  events: LogicBlock[]
  functions: LogicBlock[]
}

export interface ImportDef {
  path: string
  is_native: boolean
}

export interface LetDef {
  name: string
  type: string
  arg: string
}

export interface SchemaDef {
  name: string
  max_exec_fan_out: number
  allow_exec_fan_in: boolean
  strict_type_match: boolean
}

export interface GSState {
  file_path: string
  dirty: boolean
  active_graph: number
  can_undo: boolean
  can_redo: boolean
  module: {
    imports: ImportDef[]
    lets: LetDef[]
    graphs: GraphDef[]
  }
  types: NodeTypeDef[]
  schemas: SchemaDef[]
  command_log: string[]
}

export interface ExecResponse {
  ok: boolean
  command?: string
  output?: string
  error?: string
  state?: GSState
}

export interface UndoRedoResponse {
  ok: boolean
  description: string
  state: GSState
}
