export interface PinDef {
  persistent_id?: string
  source_file?: string
  source_range?: SourceRange
  name_source_range?: SourceRange
  type_source_range?: SourceRange
  name: string
  kind: 'exec' | 'data'
  direction: 'in' | 'out'
  type: string
  annotations: Annotation[]
}

export interface DeclaredTypeDef {
  persistent_id?: string
  source_file?: string
  source_range?: SourceRange
  name_source_range?: SourceRange
  name: string
  constructible: boolean
  annotations: Annotation[]
}

export interface AnnotationArg {
  id?: string
  name: string
  value: string
  source_range?: SourceRange
  name_source_range?: SourceRange
  value_source_range?: SourceRange
  value_constructor_source_range?: SourceRange
  value_constructor_type_source_range?: SourceRange
  value_constructor_arg_source_range?: SourceRange
}

export interface Annotation {
  id?: string
  name: string
  source_range?: SourceRange
  name_source_range?: SourceRange
  args: AnnotationArg[]
}

export interface NodeTypeDef {
  persistent_id?: string
  source_file?: string
  source_range?: SourceRange
  name_source_range?: SourceRange
  type_name: string
  is_native: boolean
  source_graph: string
  tags: string[]
  annotations: Annotation[]
  pins: PinDef[]
  fields?: NodeFieldDef[]
}

export interface NodeFieldDef {
  persistent_id?: string
  source_file?: string
  source_range?: SourceRange
  name_source_range?: SourceRange
  type_source_range?: SourceRange
  default_source_range?: SourceRange
  default_constructor_source_range?: SourceRange
  default_constructor_type_source_range?: SourceRange
  default_constructor_arg_source_range?: SourceRange
  name: string
  type: string
  default: string
  annotations: Annotation[]
}

export interface GraphParam {
  id?: string
  persistent_id?: string
  source_range?: SourceRange
  name_source_range?: SourceRange
  default_source_range?: SourceRange
  default_constructor_source_range?: SourceRange
  default_constructor_type_source_range?: SourceRange
  default_constructor_arg_source_range?: SourceRange
  type_source_range?: SourceRange
  name: string
  type: string
  direction: 'in' | 'out' | 'var'
  default: string
  annotations: Annotation[]
}

export interface NodeInst {
  id?: string
  persistent_id?: string
  source_range?: SourceRange
  init_source_range?: SourceRange
  init_constructor_source_range?: SourceRange
  init_constructor_type_source_range?: SourceRange
  init_constructor_arg_source_range?: SourceRange
  initializer_fields?: InitializerField[]
  type_source_range?: SourceRange
  instance_source_range?: SourceRange
  type: string
  instance: string
  init: string
  annotations: Annotation[]
}

export interface InitializerField {
  id?: string
  name: string
  value: string
  source_range?: SourceRange
  name_source_range?: SourceRange
  value_source_range?: SourceRange
  value_constructor_source_range?: SourceRange
  value_constructor_type_source_range?: SourceRange
  value_constructor_arg_source_range?: SourceRange
}

export interface FlowConn {
  id?: string
  persistent_id?: string
  source_range?: SourceRange
  from_endpoint_source_range?: SourceRange
  to_endpoint_source_range?: SourceRange
  from_node_source_range?: SourceRange
  from_pin_source_range?: SourceRange
  to_node_source_range?: SourceRange
  to_pin_source_range?: SourceRange
  from_node: string
  from_pin: string
  to_node: string
  to_pin: string
  annotations: Annotation[]
}

export interface DataLink {
  id?: string
  persistent_id?: string
  source_range?: SourceRange
  target_endpoint_source_range?: SourceRange
  source_endpoint_source_range?: SourceRange
  target_node_source_range?: SourceRange
  target_pin_source_range?: SourceRange
  source_node_source_range?: SourceRange
  source_pin_source_range?: SourceRange
  target_node: string
  target_pin: string
  source_node: string
  source_pin: string
  annotations: Annotation[]
}

export interface LogicBlock {
  id?: string
  persistent_id?: string
  source_range?: SourceRange
  name_source_range?: SourceRange
  name: string
  kind: 'event' | 'function'
  annotations: Annotation[]
  flows: FlowConn[]
  links: DataLink[]
}

export interface GraphDef {
  id?: string
  persistent_id?: string
  source_range?: SourceRange
  name_source_range?: SourceRange
  name: string
  base_type: string | null
  base_type_source_range?: SourceRange
  annotations: Annotation[]
  parameters: GraphParam[]
  nodes: NodeInst[]
  events: LogicBlock[]
  functions: LogicBlock[]
  generate?: GenerateBlockDef | null
}

export interface GenerateBlockDef {
  source_range?: SourceRange
  comments: GenerateCommentDef[]
  metadata: GenerateMetadataDef[]
}

export interface GenerateCommentDef {
  id?: string
  persistent_id?: string
  source_range?: SourceRange
  instance_source_range?: SourceRange
  text_source_range?: SourceRange
  instance: string
  text: string
  annotations: Annotation[]
}

export interface GenerateMetadataDef {
  id?: string
  persistent_id?: string
  source_range?: SourceRange
  scope_source_range?: SourceRange
  node_source_range?: SourceRange
  property_source_range?: SourceRange
  value_source_range?: SourceRange
  value_constructor_source_range?: SourceRange
  value_constructor_type_source_range?: SourceRange
  value_constructor_arg_source_range?: SourceRange
  scope: string
  node: string
  property: string
  value: string
  annotations: Annotation[]
}

export interface ImportDef {
  id?: string
  persistent_id?: string
  source_range?: SourceRange
  path_source_range?: SourceRange
  path: string
  is_native: boolean
  loaded?: boolean
  normalized_path?: string
  annotations: Annotation[]
}

export interface LetDef {
  id?: string
  persistent_id?: string
  source_range?: SourceRange
  name_source_range?: SourceRange
  type_source_range?: SourceRange
  constructor_source_range?: SourceRange
  arg_source_range?: SourceRange
  name: string
  type: string
  arg: string
  annotations: Annotation[]
}

export interface SchemaDef {
  persistent_id?: string
  source_file?: string
  source_range?: SourceRange
  name_source_range?: SourceRange
  name: string
  fields?: SchemaFieldDef[]
  max_exec_fan_out: number
  allow_exec_fan_in: boolean
  strict_type_match: boolean
  annotations: Annotation[]
}

export interface SchemaFieldDef {
  persistent_id?: string
  source_file?: string
  source_range?: SourceRange
  name_source_range?: SourceRange
  value_source_range?: SourceRange
  value_constructor_source_range?: SourceRange
  value_constructor_type_source_range?: SourceRange
  value_constructor_arg_source_range?: SourceRange
  name: string
  value: string
  annotations: Annotation[]
}

export interface SourceLocation {
  line: number
  column: number
}

export interface SourceRange {
  start: SourceLocation
  end: SourceLocation
}

export interface DiagnosticTarget {
  graph: string
  block_kind: string
  block_name: string
  node_instance: string
  pin_name: string
  parameter_name: string
  reference: string
  connection_kind: string
}

export interface DiagnosticAction {
  id?: string
  title: string
  kind: string
  command?: string
  edit_range?: SourceRange
  replacement?: string
}

export interface Diagnostic {
  id?: string
  severity: 'warning' | 'error'
  message: string
  context: string
  code: string
  range: SourceRange
  hint: string
  target: DiagnosticTarget
  actions: DiagnosticAction[]
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
  declared_types?: DeclaredTypeDef[]
  types: NodeTypeDef[]
  schemas: SchemaDef[]
  diagnostics: Diagnostic[]
  module_diagnostics?: Diagnostic[]
  command_log: string[]
}

export interface ExecResponse {
  ok: boolean
  command?: string
  output?: string
  error?: string
  state?: GSState
}

export interface ApplySourceResponse {
  ok: boolean
  error?: string
  command?: string
  fallback?: boolean
  state: GSState
}

export interface UndoRedoResponse {
  ok: boolean
  description: string
  state: GSState
}

export interface DiagnosticsResponse {
  ok: boolean
  stage: 'session' | 'parser' | 'compiler' | 'resolver'
  diagnostics: Diagnostic[]
  environment?: SourceDiagnosticsEnvironment
}

export interface DeclarationSourceResponse {
  ok: boolean
  path: string
  normalized_path: string
  content_hash: string
  source: string
  error?: string
}

export interface SourceDeclarationStatus {
  path: string
  normalized_path: string
  content_hash: string
  status: string
  message: string
  command: string
  parent_path?: string
  parent_normalized_path?: string
  import_chain?: string
  depth?: number
}

export interface SourceDiagnosticsEnvironment {
  mode: 'session' | 'resolved'
  declarations: SourceDeclarationStatus[]
  environment_hash: string
  type_count: number
  node_type_count: number
  schema_count: number
  limits: {
    max_imports: number
    max_import_depth?: number
    max_file_bytes: number
    max_total_bytes: number
  }
}
