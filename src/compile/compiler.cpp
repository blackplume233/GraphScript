#include "graphscript/compile/compiler.h"
#include <unordered_set>

namespace gs {

// Constructs compiler bound to environment for type/node/schema registration.
Compiler::Compiler(Environment& env) : env_(env) {}

// Compiles AST to Module: imports, lets, declare types/nodes/schemas, graphs.
Result<Module, std::string> Compiler::compile(const ModuleNode& ast, const std::string& file_path) {
    diagnostics_.clear();
    Module mod;
    mod.file_path = file_path;

    // 1. Process imports
    for (auto& imp : ast.imports) {
        ImportDecl decl;
        decl.path = imp->path;
        decl.is_native = (imp->path.size() > 5 && imp->path.substr(imp->path.size() - 5) == ".d.gs");
        decl.annotations = imp->annotations;
        decl.source_range = imp->range;
        decl.path_range = imp->path_range;
        mod.imports.push_back(std::move(decl));
    }

    // 2. Process let declarations
    for (auto& let : ast.let_decls) {
        LetDecl decl;
        decl.name = let->name;
        decl.name_range = let->name_range;
        decl.type_name = let->type_name;
        decl.type_name_range = let->type_name_range;
        decl.constructor_arg = let->constructor_arg;
        decl.constructor_range = let->constructor_range;
        decl.constructor_arg_range = let->constructor_arg_range;
        decl.source_range = let->range;
        decl.annotations = let->annotations;
        mod.top_level_lets.push_back(std::move(decl));
    }

    // 3. Process .d.gs declarations (register into environment)
    process_declare_types(ast, file_path);
    process_declare_nodes(ast, file_path);
    process_declare_schemas(ast, file_path);

    // 4. Compile graphs and derive nodes
    for (auto& graph_ast : ast.graphs) {
        Graph graph = compile_graph(*graph_ast);

        auto scope_result = validate_graph_scope(graph, *graph_ast);
        if (scope_result.is_err()) return Result<Module, std::string>::err(scope_result.error());

        NodeDefinition derived = derive_node_from_graph(graph);
        mod.graphs.push_back(std::move(graph));
        env_.nodes().register_graph_node(std::move(derived));
    }

    return Result<Module, std::string>::ok(std::move(mod));
}

// Validates block-level reference scope and records structured diagnostics on failure.
Result<void, std::string> Compiler::validate_graph_scope(const Graph& graph, const GraphNode& graph_ast) {
    std::unordered_set<std::string> param_names;
    std::unordered_set<std::string> node_names;
    param_names.insert("context");
    for (auto& p : graph.parameters) param_names.insert(p.name);
    for (auto& ni : graph.node_instances) node_names.insert(ni.instance_name);

    auto normalize_range = [](SourceRange range) {
        if (range.end.line < range.start.line ||
            (range.end.line == 1 && range.end.column == 1 &&
             (range.start.line != 1 || range.start.column != 1))) {
            range.end = range.start;
        }
        return range;
    };

    auto check_ref = [&](const std::string& block_kind,
                         const std::string& block_name,
                         const std::string& ref,
                         const std::string& pin,
                         bool allow_nodes,
                         SourceRange range) -> std::string {
        if (param_names.count(ref)) return "";
        if (allow_nodes && node_names.count(ref)) return "";
        DiagnosticTarget target;
        target.graph = graph.name;
        target.block_kind = block_kind;
        target.block_name = block_name;
        target.pin_name = pin;
        target.reference = ref;
        if (!allow_nodes && node_names.count(ref)) {
            std::string message = block_kind + " '" + block_name + "': cannot reference graph node '" +
                                  ref + "' (only context and parameters allowed)";
            target.node_instance = ref;
            record_error(message,
                         ref,
                         "GS_SCOPE_FORBIDDEN_REFERENCE",
                         normalize_range(range),
                         "Move this logic to an event block, or pass the value through a graph parameter.",
                         target);
            return message;
        }

        std::string message = block_kind + " '" + block_name + "': unknown reference '" + ref +
                              "' (must be context" + (allow_nodes ? ", a parameter, or a node instance)" : " or a parameter)");
        record_error(message,
                     ref,
                     "GS_SCOPE_UNKNOWN_REFERENCE",
                     normalize_range(range),
                     allow_nodes
                         ? "Use context, a graph parameter, or an existing node instance name."
                         : "Use context or a graph parameter; functions cannot see node instances.",
                     target);
        return message;
    };

    auto validate_flow = [&](const std::string& kind,
                             const std::string& block_name,
                             const FlowStmtNode& stmt,
                             bool allow_nodes) -> std::string {
        auto e = check_ref(kind, block_name, stmt.from_node, stmt.from_pin, allow_nodes, stmt.range);
        if (!e.empty()) return e;
        return check_ref(kind, block_name, stmt.to_node, stmt.to_pin, allow_nodes, stmt.range);
    };

    auto validate_link = [&](const std::string& kind,
                             const std::string& block_name,
                             const LinkStmtNode& stmt,
                             bool allow_nodes) -> std::string {
        auto e = check_ref(kind, block_name, stmt.target_node, stmt.target_pin, allow_nodes, stmt.range);
        if (!e.empty()) return e;
        return check_ref(kind, block_name, stmt.source_node, stmt.source_pin, allow_nodes, stmt.range);
    };

    for (auto& ev : graph_ast.events) {
        for (auto& flow : ev->flow_stmts) {
            auto e = validate_flow("event", ev->name, *flow, true);
            if (!e.empty()) return Result<void, std::string>::err(e);
        }
        for (auto& link : ev->link_stmts) {
            auto e = validate_link("event", ev->name, *link, true);
            if (!e.empty()) return Result<void, std::string>::err(e);
        }
    }

    for (auto& fn : graph_ast.functions) {
        for (auto& flow : fn->flow_stmts) {
            auto e = validate_flow("function", fn->name, *flow, false);
            if (!e.empty()) return Result<void, std::string>::err(e);
        }
        for (auto& link : fn->link_stmts) {
            auto e = validate_link("function", fn->name, *link, false);
            if (!e.empty()) return Result<void, std::string>::err(e);
        }
    }

    return Result<void, std::string>::ok();
}

// Records a compiler diagnostic without changing the Result<T, string> contract.
void Compiler::record_error(const std::string& message,
                            const std::string& context,
                            const std::string& code,
                            SourceRange range,
                            const std::string& hint,
                            const DiagnosticTarget& target) {
    diagnostics_.push_back({
        Severity::Error,
        message,
        context,
        code,
        range,
        hint,
        target
    });
}

// Registers declare type nodes into environment.
void Compiler::process_declare_types(const ModuleNode& ast, const std::string& file_path) {
    for (auto& dt : ast.declare_types) {
        TypeInfo info;
        info.name = dt->name;
        info.constructible = dt->constructible;
        info.annotations = dt->annotations;
        info.source_range = dt->range;
        info.name_range = dt->name_range;
        info.source_file = file_path;
        env_.types().register_type(std::move(info));
    }
}

// Registers declare Node definitions as native nodes.
void Compiler::process_declare_nodes(const ModuleNode& ast, const std::string& file_path) {
    for (auto& dn : ast.declare_nodes) {
        NodeDefinition def;
        def.type_name = dn->name;
        def.is_native = true;
        def.annotations = dn->annotations;
        def.source_range = dn->range;
        def.name_range = dn->name_range;
        def.source_file = file_path;
        for (auto& pin_ast : dn->pins) {
            PinDefinition pin;
            pin.name = pin_ast->name;
            pin.kind = pin_ast->kind;
            pin.direction = pin_ast->direction;
            pin.type_name = pin_ast->type_name;
            pin.annotations = pin_ast->annotations;
            pin.source_range = pin_ast->range;
            pin.type_name_range = pin_ast->type_name_range;
            pin.name_range = pin_ast->name_range;
            pin.source_file = file_path;
            def.pins.push_back(std::move(pin));
        }
        for (auto& field_ast : dn->fields) {
            NodeFieldDefinition field;
            field.name = field_ast->name;
            field.type_name = field_ast->type_name;
            field.default_value = field_ast->default_value;
            field.annotations = field_ast->annotations;
            field.source_range = field_ast->range;
            field.name_range = field_ast->name_range;
            field.type_name_range = field_ast->type_name_range;
            field.default_value_range = field_ast->default_value_range;
            field.default_constructor_range = field_ast->default_constructor_range;
            field.default_constructor_type_range = field_ast->default_constructor_type_range;
            field.default_constructor_arg_range = field_ast->default_constructor_arg_range;
            field.source_file = file_path;
            def.fields.push_back(std::move(field));
        }
        env_.nodes().register_node(std::move(def));
    }
}

// Registers declare Schema definitions; parses connection_policy and allowed_node_tags.
void Compiler::process_declare_schemas(const ModuleNode& ast, const std::string& file_path) {
    for (auto& ds : ast.declare_schemas) {
        GraphSchema schema;
        schema.name = ds->name;
        schema.annotations = ds->annotations;
        schema.source_range = ds->range;
        schema.name_range = ds->name_range;
        schema.source_file = file_path;

        for (auto& field_ast : ds->fields) {
            GraphSchemaField field;
            field.name = field_ast->name;
            field.value = field_ast->value;
            field.annotations = field_ast->annotations;
            field.source_range = field_ast->range;
            field.value_range = field_ast->value_range;
            field.name_range = field_ast->name_range;
            field.value_constructor_range = field_ast->value_constructor_range;
            field.value_constructor_type_range = field_ast->value_constructor_type_range;
            field.value_constructor_arg_range = field_ast->value_constructor_arg_range;
            field.source_file = file_path;
            schema.fields.push_back(field);

            const auto& key = field.name;
            const auto& val = field.value;
            if (key == "max_exec_fan_out") {
                schema.connection_policy.max_exec_fan_out = (val == "unlimited") ? -1 : std::stoi(val);
            } else if (key == "allow_exec_fan_in") {
                schema.connection_policy.allow_exec_fan_in = (val == "true");
            } else if (key == "strict_type_match") {
                schema.connection_policy.strict_type_match = (val == "true");
            } else if (key == "allowed_node_tags") {
                // Parse array: ["tag1", "tag2"]
                std::string s = val;
                size_t pos = 0;
                while ((pos = s.find('"', pos)) != std::string::npos) {
                    size_t end = s.find('"', pos + 1);
                    if (end == std::string::npos) break;
                    schema.allowed_node_tags.push_back(s.substr(pos + 1, end - pos - 1));
                    pos = end + 1;
                }
            } else if (key == "required_events") {
                std::string s = val;
                size_t pos = 0;
                while ((pos = s.find('"', pos)) != std::string::npos) {
                    size_t end = s.find('"', pos + 1);
                    if (end == std::string::npos) break;
                    schema.required_events.push_back(s.substr(pos + 1, end - pos - 1));
                    pos = end + 1;
                }
            }
        }

        env_.schemas().register_schema(std::move(schema));
    }
}

// Compiles graph AST to Graph IR (annotations, params, instances, events, functions, generate).
Graph Compiler::compile_graph(const GraphNode& gn) {
    Graph g;
    g.name = gn.name;
    g.name_range = gn.name_range;
    g.base_type = gn.base_type;
    g.base_type_range = gn.base_type_range;
    g.annotations = gn.annotations;
    g.source_range = gn.range;

    for (auto& p : gn.params) {
        g.parameters.push_back(compile_param(*p));
    }
    for (auto& ni : gn.node_instances) {
        g.node_instances.push_back(compile_node_instance(*ni));
    }
    for (auto& ev : gn.events) {
        g.events.push_back(compile_event(*ev));
    }
    for (auto& fn : gn.functions) {
        g.functions.push_back(compile_function(*fn));
    }

    if (gn.generate) {
        g.generate = compile_generate(*gn.generate);
    }

    return g;
}

// Derives NodeDefinition from graph: in/out params and events become pins.
NodeDefinition Compiler::derive_node_from_graph(const Graph& graph) {
    NodeDefinition def;
    def.type_name = graph.name;
    def.is_native = false;
    def.source_graph = graph.name;
    def.annotations = graph.annotations;
    def.source_range = graph.source_range;
    def.name_range = graph.name_range;

    for (auto& param : graph.parameters) {
        if (param.direction == ParamDirection::Var) continue;

        PinDefinition pin;
        pin.name = param.name;
        pin.type_name = param.type_name;
        pin.kind = PinKind::Data;
        pin.direction = (param.direction == ParamDirection::In)
                        ? PinDirection::Input
                        : PinDirection::Output;
        pin.annotations = param.annotations;
        pin.source_range = param.source_range;
        pin.name_range = param.name_range;
        pin.type_name_range = param.type_name_range;
        def.pins.push_back(std::move(pin));
    }

    for (auto& ev : graph.events) {
        PinDefinition pin;
        pin.name = ev.name;
        pin.kind = PinKind::Exec;
        pin.direction = PinDirection::Input;
        pin.annotations = ev.annotations;
        pin.source_range = ev.source_range;
        pin.name_range = ev.name_range;
        def.pins.push_back(std::move(pin));
    }

    return def;
}

// Compiles param declaration to GraphParameter (with annotations pass-through).
GraphParameter Compiler::compile_param(const ParamDeclNode& pn) {
    GraphParameter p;
    p.name = pn.name;
    p.name_range = pn.name_range;
    p.type_name = pn.type_name;
    p.type_name_range = pn.type_name_range;
    p.default_value = pn.default_value;
    p.default_value_range = pn.default_value_range;
    p.default_constructor_range = pn.default_constructor_range;
    p.default_constructor_type_range = pn.default_constructor_type_range;
    p.default_constructor_arg_range = pn.default_constructor_arg_range;
    p.annotations = pn.annotations;
    p.source_range = pn.range;
    if (pn.direction == "in")       p.direction = ParamDirection::In;
    else if (pn.direction == "out") p.direction = ParamDirection::Out;
    else                            p.direction = ParamDirection::Var;
    return p;
}

// Compiles node instance AST to NodeInstance (with annotations pass-through).
NodeInstance Compiler::compile_node_instance(const NodeInstanceNode& ni) {
    NodeInstance inst;
    inst.type_name = ni.type_name;
    inst.type_name_range = ni.type_name_range;
    inst.instance_name = ni.instance_name;
    inst.instance_name_range = ni.instance_name_range;
    inst.initializer = ni.initializer;
    inst.initializer_range = ni.initializer_range;
    inst.initializer_constructor_range = ni.initializer_constructor_range;
    inst.initializer_constructor_type_range = ni.initializer_constructor_type_range;
    inst.initializer_constructor_arg_range = ni.initializer_constructor_arg_range;
    inst.initializer_fields = ni.initializer_fields;
    inst.annotations = ni.annotations;
    inst.source_range = ni.range;
    return inst;
}

// Compiles event AST; flow and link stmts become flow_connections and data_links.
Event Compiler::compile_event(const EventNode& en) {
    Event ev;
    ev.name = en.name;
    ev.name_range = en.name_range;
    ev.annotations = en.annotations;
    ev.source_range = en.range;
    compile_logic_stmts(en.flow_stmts, en.link_stmts, ev.flow_connections, ev.data_links);
    return ev;
}

// Compiles function AST; flow and link stmts become flow_connections and data_links.
Function Compiler::compile_function(const FunctionNode& fn) {
    Function func;
    func.name = fn.name;
    func.name_range = fn.name_range;
    func.annotations = fn.annotations;
    func.source_range = fn.range;
    compile_logic_stmts(fn.flow_stmts, fn.link_stmts, func.flow_connections, func.data_links);
    return func;
}

// Compiles generate block (comments, metadata).
GenerateBlock Compiler::compile_generate(const GenerateNode& gn) {
    GenerateBlock gen;
    gen.source_range = gn.range;
    for (auto& c : gn.comments) {
        GenerateComment gc;
        gc.instance_name = c->instance_name;
        gc.text = c->text;
        gc.source_range = c->range;
        gc.instance_name_range = c->instance_name_range;
        gc.text_range = c->text_range;
        gc.annotations = c->annotations;
        gen.comments.push_back(std::move(gc));
    }
    for (auto& m : gn.metadata) {
        GenerateMetadata gm;
        gm.scope = m->scope;
        gm.node = m->node;
        gm.property = m->property;
        gm.value = m->value;
        gm.source_range = m->range;
        gm.scope_range = m->scope_range;
        gm.node_range = m->node_range;
        gm.property_range = m->property_range;
        gm.value_range = m->value_range;
        gm.value_constructor_range = m->value_constructor_range;
        gm.value_constructor_type_range = m->value_constructor_type_range;
        gm.value_constructor_arg_range = m->value_constructor_arg_range;
        gm.annotations = m->annotations;
        gen.metadata.push_back(std::move(gm));
    }
    return gen;
}

// Converts flow_stmts and link_stmts to flow_connections and data_links.
void Compiler::compile_logic_stmts(
    const std::vector<std::unique_ptr<FlowStmtNode>>& flow_stmts,
    const std::vector<std::unique_ptr<LinkStmtNode>>& link_stmts,
    std::vector<FlowConnection>& flows,
    std::vector<DataLink>& links)
{
    for (auto& fs : flow_stmts) {
        FlowConnection fc;
        fc.from = {fs->from_node, fs->from_pin};
        fc.to = {fs->to_node, fs->to_pin};
        fc.source_range = fs->range;
        fc.from_endpoint_range = fs->from_expr_range;
        fc.to_endpoint_range = fs->to_expr_range;
        fc.from_node_range = fs->from_node_range;
        fc.from_pin_range = fs->from_pin_range;
        fc.to_node_range = fs->to_node_range;
        fc.to_pin_range = fs->to_pin_range;
        fc.annotations = fs->annotations;
        flows.push_back(std::move(fc));
    }
    for (auto& ls : link_stmts) {
        DataLink dl;
        dl.target = {ls->target_node, ls->target_pin};
        dl.source = {ls->source_node, ls->source_pin};
        dl.source_range = ls->range;
        dl.target_endpoint_range = ls->target_expr_range;
        dl.source_endpoint_range = ls->source_expr_range;
        dl.target_node_range = ls->target_node_range;
        dl.target_pin_range = ls->target_pin_range;
        dl.source_node_range = ls->source_node_range;
        dl.source_pin_range = ls->source_pin_range;
        dl.annotations = ls->annotations;
        links.push_back(std::move(dl));
    }
}

} // namespace gs
