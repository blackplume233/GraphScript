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

// Emits C#-style annotations as a prefix line, e.g. "    [Position(X = 100, Y = 200)]\n".
std::string Emitter::emit_annotations(const std::vector<Annotation>& annots, const std::string& indent) const {
    if (annots.empty()) return "";
    std::string out = indent + "[";
    for (size_t i = 0; i < annots.size(); ++i) {
        if (i > 0) out += ", ";
        out += annots[i].name + "(";
        for (size_t j = 0; j < annots[i].args.size(); ++j) {
            if (j > 0) out += ", ";
            auto& arg = annots[i].args[j];
            if (!arg.name.empty()) out += arg.name + " = ";
            bool is_numeric = !arg.value.empty() &&
                              (arg.value.front() == '-' || std::isdigit(static_cast<unsigned char>(arg.value.front()))) &&
                              arg.value.find(' ') == std::string::npos;
            bool is_bool = (arg.value == "true" || arg.value == "false");
            bool is_string = !arg.value.empty() && !is_numeric && !is_bool;
            if (is_string) out += "\"" + arg.value + "\"";
            else out += arg.value;
        }
        out += ")";
    }
    out += "]\n";
    return out;
}

// Emits single graph: annotations, params, node instances, events, functions, generate.
std::string Emitter::emit_graph(const Graph& graph) const {
    std::string out;
    out += emit_annotations(graph.annotations, "");
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

// Emits graph parameters (in/out/var) with optional C# annotations prefix.
std::string Emitter::emit_params(const std::vector<GraphParameter>& params) const {
    std::string out;
    for (auto& p : params) {
        out += emit_annotations(p.annotations, "    ");
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

// Emits node instance declarations with optional C# annotations prefix.
std::string Emitter::emit_node_instances(const std::vector<NodeInstance>& instances) const {
    std::string out;
    for (auto& ni : instances) {
        out += emit_annotations(ni.annotations, "    ");
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

// ─── Mermaid Diagram Generation ────────────────────────────────────

// Converts a name to a safe Mermaid node ID (no dots, spaces, etc.).
std::string Emitter::mermaid_id(const std::string& name) {
    std::string id;
    for (char c : name) {
        if (c == '.' || c == ' ' || c == '-') id += '_';
        else id += c;
    }
    return id;
}

// Escapes text for Mermaid labels (quotes).
std::string Emitter::mermaid_escape(const std::string& text) {
    std::string r;
    for (char c : text) {
        if (c == '"') r += "#quot;";
        else r += c;
    }
    return r;
}

// Emits all graphs as Mermaid diagrams in one markdown document.
std::string Emitter::emit_diagram(const Module& module) const {
    std::string out;
    for (auto& g : module.graphs) {
        out += emit_graph_diagram(g);
        out += "\n";
    }
    return out;
}

// Emits flow+link connections for one logic block.
std::string Emitter::emit_block_diagram(const LogicBlock& block, const std::string& kind) const {
    std::string out;
    std::string prefix = "    ";

    out += prefix + "subgraph " + mermaid_id(block.name) + "[\"" + kind + " " + mermaid_escape(block.name) + "\"]\n";
    out += prefix + "    direction LR\n";
    out += prefix + "end\n";

    // Flow connections: solid arrows with pin labels
    for (auto& fc : block.flow_connections) {
        std::string from = mermaid_id(fc.from.node_instance);
        std::string to   = mermaid_id(fc.to.node_instance);
        std::string label = fc.from.pin_name + " → " + fc.to.pin_name;
        out += prefix + from + " ==>|\"" + mermaid_escape(label) + "\"| " + to + "\n";
    }

    // Data links: dashed arrows with pin labels
    for (auto& dl : block.data_links) {
        std::string src = dl.source.pin_name.empty()
            ? mermaid_id(dl.source.node_instance)
            : mermaid_id(dl.source.node_instance);
        std::string tgt = mermaid_id(dl.target.node_instance);
        std::string src_label = dl.source.pin_name.empty()
            ? dl.source.node_instance
            : dl.source.node_instance + "." + dl.source.pin_name;
        std::string label = src_label + " → " + dl.target.pin_name;
        out += prefix + src + " -.->|\"" + mermaid_escape(label) + "\"| " + tgt + "\n";
    }

    return out;
}

// Emits a single graph as a complete Mermaid flowchart in markdown.
std::string Emitter::emit_graph_diagram(const Graph& graph) const {
    std::string out;

    // Markdown header
    out += "## " + graph.name;
    if (graph.base_type) out += " : " + *graph.base_type;
    out += "\n\n";
    out += "```mermaid\n";
    out += "flowchart TD\n";

    // Style definitions
    out += "    classDef param fill:#1a3d2a,stroke:#4a9a6a,color:#a6e3a1\n";
    out += "    classDef node fill:#1a2d4a,stroke:#4a7a9a,color:#89b4fa\n";
    out += "    classDef ctx fill:#2a2a3a,stroke:#6c7086,color:#cdd6f4\n";

    // Context node (entry point for events)
    out += "\n    context((\"" + mermaid_escape(graph.name) + "\")):::ctx\n";

    // Parameters
    if (!graph.parameters.empty()) {
        out += "\n    subgraph params[\"Parameters\"]\n";
        out += "        direction TB\n";
        for (auto& p : graph.parameters) {
            std::string id = mermaid_id(p.name);
            std::string dir_icon;
            switch (p.direction) {
                case ParamDirection::In:  dir_icon = "IN"; break;
                case ParamDirection::Out: dir_icon = "OUT"; break;
                case ParamDirection::Var: dir_icon = "VAR"; break;
            }
            std::string label = dir_icon + " " + p.name + " : " + p.type_name;
            if (!p.default_value.empty()) label += " = " + p.default_value;

            if (p.direction == ParamDirection::In || p.direction == ParamDirection::Var) {
                out += "        " + id + "([\"" + mermaid_escape(label) + "\"]):::param\n";
            } else {
                out += "        " + id + "([\"" + mermaid_escape(label) + "\"]):::param\n";
            }
        }
        out += "    end\n";
    }

    // Node instances
    if (!graph.node_instances.empty()) {
        out += "\n    subgraph nodes[\"Nodes\"]\n";
        out += "        direction TB\n";
        for (auto& ni : graph.node_instances) {
            std::string id = mermaid_id(ni.instance_name);
            std::string label = ni.type_name + "\\n" + ni.instance_name;
            out += "        " + id + "[\"" + mermaid_escape(label) + "\"]:::node\n";
        }
        out += "    end\n";
    }

    // Events
    for (auto& ev : graph.events) {
        out += "\n";
        out += emit_block_diagram(ev, "event");
    }

    // Functions
    for (auto& fn : graph.functions) {
        out += "\n";
        out += emit_block_diagram(fn, "function");
    }

    out += "```\n";
    return out;
}

} // namespace gs
