#include "graphscript/emit/emitter.h"
#include <sstream>

namespace gs {

// Emits entire module as DSL text: imports, lets, graphs.
std::string Emitter::emit(const Module& module) const {
    std::string out;
    out += emit_imports(module.imports);
    out += emit_lets(module.top_level_lets);
    for (auto& g : module.graphs) {
        if (!out.empty() && out.back() != '\n') out += "\n";
        out += emit_graph(g);
    }
    return out;
}

// Emits single graph: params, node instances, events, functions, generate.
std::string Emitter::emit_graph(const Graph& graph) const {
    std::string out;
    out += "Graph " + graph.name;
    if (graph.base_type) out += " : " + *graph.base_type;
    out += " {\n";
    out += emit_params(graph.parameters);
    if (!graph.parameters.empty() && !graph.node_instances.empty()) out += "\n";
    out += emit_node_instances(graph.node_instances);

    for (auto& ev : graph.events) {
        if (!out.empty() && out.back() != '\n') out += "\n";
        out += emit_event(ev);
    }
    for (auto& fn : graph.functions) {
        if (!out.empty() && out.back() != '\n') out += "\n";
        out += emit_function(fn);
    }
    if (graph.generate) {
        if (!out.empty() && out.back() != '\n') out += "\n";
        out += emit_generate(*graph.generate);
    }
    out += "}\n";
    return out;
}

// Emits import statements.
std::string Emitter::emit_imports(const std::vector<ImportDecl>& imports) const {
    std::string out;
    for (auto& imp : imports) {
        out += "import \"" + imp.path + "\";\n";
    }
    if (!imports.empty()) out += "\n";
    return out;
}

// Emits let declarations.
std::string Emitter::emit_lets(const std::vector<LetDecl>& lets) const {
    std::string out;
    for (auto& l : lets) {
        out += "let " + l.name + " = " + l.type_name + "(\"" + l.constructor_arg + "\");\n";
    }
    if (!lets.empty()) out += "\n";
    return out;
}

// Emits graph parameters (in/out/var).
std::string Emitter::emit_params(const std::vector<GraphParameter>& params) const {
    std::string out;
    for (auto& p : params) {
        std::string dir;
        switch (p.direction) {
            case ParamDirection::In:  dir = "in"; break;
            case ParamDirection::Out: dir = "out"; break;
            case ParamDirection::Var: dir = "var"; break;
        }
        out += "    " + dir + " " + p.name + " : " + p.type_name;
        if (!p.default_value.empty()) out += " = " + p.default_value;
        out += ";\n";
    }
    return out;
}

// Emits node instance declarations.
std::string Emitter::emit_node_instances(const std::vector<NodeInstance>& instances) const {
    std::string out;
    for (auto& ni : instances) {
        out += "    " + ni.type_name + " " + ni.instance_name + "{";
        if (!ni.initializer.empty()) out += ni.initializer;
        out += "};\n";
    }
    return out;
}

// Emits event block with flow and link statements.
std::string Emitter::emit_event(const Event& ev) const {
    std::string out;
    out += "    event " + ev.name + " {\n";
    out += emit_logic_stmts(ev.flow_connections, ev.data_links);
    out += "    }\n";
    return out;
}

// Emits function block with flow and link statements.
std::string Emitter::emit_function(const Function& fn) const {
    std::string out;
    out += "    function " + fn.name + " {\n";
    out += emit_logic_stmts(fn.flow_connections, fn.data_links);
    out += "    }\n";
    return out;
}

// Emits generate block (Comment, metadata).
std::string Emitter::emit_generate(const GenerateBlock& gen) const {
    std::string out;
    out += "    generate {\n";
    for (auto& c : gen.comments) {
        out += "        Comment " + c.instance_name + " = \"" + c.text + "\";\n";
    }
    for (auto& m : gen.metadata) {
        out += "        " + m.scope + ":" + m.node + "." + m.property + "(" + m.value + ");\n";
    }
    out += "    }\n";
    return out;
}

// Emits flow statements and link statements; empty source_pin = param reference.
std::string Emitter::emit_logic_stmts(const std::vector<FlowConnection>& flows, const std::vector<DataLink>& links) const {
    std::string out;
    for (auto& fc : flows) {
        out += "        " + fc.from.node_instance + "." + fc.from.pin_name + "(" + fc.to.node_instance + "." + fc.to.pin_name + ");\n";
    }
    for (auto& dl : links) {
        out += "        link " + dl.target.node_instance + "." + dl.target.pin_name + " = ";
        if (dl.source.pin_name.empty()) {
            out += dl.source.node_instance;
        } else {
            out += dl.source.node_instance + "." + dl.source.pin_name;
        }
        out += ";\n";
    }
    return out;
}

} // namespace gs
