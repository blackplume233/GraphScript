#include "graphscript/compile/compiler.h"

namespace gs {

// Constructs compiler bound to environment for type/node/schema registration.
Compiler::Compiler(Environment& env) : env_(env) {}

// Compiles AST to Module: imports, lets, declare types/nodes/schemas, graphs.
Result<Module, std::string> Compiler::compile(const ModuleNode& ast, const std::string& file_path) {
    Module mod;
    mod.file_path = file_path;

    // 1. Process imports
    for (auto& imp : ast.imports) {
        ImportDecl decl;
        decl.path = imp->path;
        decl.is_native = (imp->path.size() > 5 && imp->path.substr(imp->path.size() - 5) == ".d.gs");
        mod.imports.push_back(std::move(decl));
    }

    // 2. Process let declarations
    for (auto& let : ast.let_decls) {
        LetDecl decl;
        decl.name = let->name;
        decl.type_name = let->type_name;
        decl.constructor_arg = let->constructor_arg;
        mod.top_level_lets.push_back(std::move(decl));
    }

    // 3. Process .d.gs declarations (register into environment)
    process_declare_types(ast);
    process_declare_nodes(ast);
    process_declare_schemas(ast);

    // 4. Compile graphs and derive nodes
    for (auto& graph_ast : ast.graphs) {
        Graph graph = compile_graph(*graph_ast);
        NodeDefinition derived = derive_node_from_graph(graph);
        mod.graphs.push_back(std::move(graph));
        env_.nodes().register_graph_node(std::move(derived));
    }

    return Result<Module, std::string>::ok(std::move(mod));
}

// Registers declare type nodes into environment.
void Compiler::process_declare_types(const ModuleNode& ast) {
    for (auto& dt : ast.declare_types) {
        TypeInfo info;
        info.name = dt->name;
        info.constructible = dt->constructible;
        env_.types().register_type(std::move(info));
    }
}

// Registers declare Node definitions as native nodes.
void Compiler::process_declare_nodes(const ModuleNode& ast) {
    for (auto& dn : ast.declare_nodes) {
        NodeDefinition def;
        def.type_name = dn->name;
        def.is_native = true;
        for (auto& pin_ast : dn->pins) {
            PinDefinition pin;
            pin.name = pin_ast->name;
            pin.kind = pin_ast->kind;
            pin.direction = pin_ast->direction;
            pin.type_name = pin_ast->type_name;
            def.pins.push_back(std::move(pin));
        }
        env_.nodes().register_node(std::move(def));
    }
}

// Registers declare Schema definitions; parses connection_policy and allowed_node_tags.
void Compiler::process_declare_schemas(const ModuleNode& ast) {
    for (auto& ds : ast.declare_schemas) {
        GraphSchema schema;
        schema.name = ds->name;

        for (auto& [key, val] : ds->fields) {
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

// Compiles graph AST to Graph IR (params, instances, events, functions, generate).
Graph Compiler::compile_graph(const GraphNode& gn) {
    Graph g;
    g.name = gn.name;
    g.base_type = gn.base_type;

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

    for (auto& param : graph.parameters) {
        if (param.direction == ParamDirection::Var) continue;

        PinDefinition pin;
        pin.name = param.name;
        pin.type_name = param.type_name;
        pin.kind = PinKind::Data;
        pin.direction = (param.direction == ParamDirection::In)
                        ? PinDirection::Input
                        : PinDirection::Output;
        def.pins.push_back(std::move(pin));
    }

    for (auto& ev : graph.events) {
        PinDefinition pin;
        pin.name = ev.name;
        pin.kind = PinKind::Exec;
        pin.direction = PinDirection::Input;
        def.pins.push_back(std::move(pin));
    }

    return def;
}

// Compiles param declaration to GraphParameter.
GraphParameter Compiler::compile_param(const ParamDeclNode& pn) {
    GraphParameter p;
    p.name = pn.name;
    p.type_name = pn.type_name;
    p.default_value = pn.default_value;
    if (pn.direction == "in")       p.direction = ParamDirection::In;
    else if (pn.direction == "out") p.direction = ParamDirection::Out;
    else                            p.direction = ParamDirection::Var;
    return p;
}

// Compiles node instance AST to NodeInstance.
NodeInstance Compiler::compile_node_instance(const NodeInstanceNode& ni) {
    NodeInstance inst;
    inst.type_name = ni.type_name;
    inst.instance_name = ni.instance_name;
    inst.initializer = ni.initializer;
    return inst;
}

// Compiles event AST; flow and link stmts become flow_connections and data_links.
Event Compiler::compile_event(const EventNode& en) {
    Event ev;
    ev.name = en.name;
    compile_logic_stmts(en.flow_stmts, en.link_stmts, ev.flow_connections, ev.data_links);
    return ev;
}

// Compiles function AST; flow and link stmts become flow_connections and data_links.
Function Compiler::compile_function(const FunctionNode& fn) {
    Function func;
    func.name = fn.name;
    compile_logic_stmts(fn.flow_stmts, fn.link_stmts, func.flow_connections, func.data_links);
    return func;
}

// Compiles generate block (comments, metadata).
GenerateBlock Compiler::compile_generate(const GenerateNode& gn) {
    GenerateBlock gen;
    for (auto& c : gn.comments) {
        GenerateComment gc;
        gc.instance_name = c->instance_name;
        gc.text = c->text;
        gen.comments.push_back(std::move(gc));
    }
    for (auto& m : gn.metadata) {
        GenerateMetadata gm;
        gm.scope = m->scope;
        gm.node = m->node;
        gm.property = m->property;
        gm.value = m->value;
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
        flows.push_back(std::move(fc));
    }
    for (auto& ls : link_stmts) {
        DataLink dl;
        dl.target = {ls->target_node, ls->target_pin};
        dl.source = {ls->source_node, ls->source_pin};
        links.push_back(std::move(dl));
    }
}

} // namespace gs
