#include "graphscript/debug/diagram.h"

#include "graphscript/asset/language.h"

namespace gs {
namespace debug {

static std::string mermaid_id(const std::string& name) {
    std::string id;
    for (char c : name) {
        if (c == '.' || c == ' ' || c == '-') id += '_';
        else id += c;
    }
    return id;
}

static std::string mermaid_escape(const std::string& text) {
    std::string r;
    for (char c : text) {
        if (c == '"') r += "#quot;";
        else r += c;
    }
    return r;
}

static std::string emit_block_diagram(const LogicBlock& block, const std::string& kind) {
    std::string out;
    std::string prefix = "    ";

    out += prefix + "subgraph " + mermaid_id(block.name) + "[\"" + kind + " " + mermaid_escape(block.name) + "\"]\n";
    out += prefix + "    direction LR\n";
    out += prefix + "end\n";

    for (auto& fc : block.flow_connections) {
        std::string from = mermaid_id(fc.from.node_instance);
        std::string to = mermaid_id(fc.to.node_instance);
        std::string label = fc.from.pin_name + " → " + fc.to.pin_name;
        out += prefix + from + " ==>|\"" + mermaid_escape(label) + "\"| " + to + "\n";
    }

    for (auto& dl : block.data_links) {
        std::string src = mermaid_id(dl.source.node_instance);
        std::string tgt = mermaid_id(dl.target.node_instance);
        std::string src_label = dl.source.pin_name.empty()
            ? dl.source.node_instance
            : dl.source.node_instance + "." + dl.source.pin_name;
        std::string label = src_label + " → " + dl.target.pin_name;
        out += prefix + src + " -.->|\"" + mermaid_escape(label) + "\"| " + tgt + "\n";
    }

    return out;
}

std::string emit_mermaid_diagram(const Module& module) {
    std::string out;
    for (auto& graph : module.graphs) {
        out += emit_mermaid_graph_diagram(graph);
        out += "\n";
    }
    return out;
}

std::string emit_mermaid_graph_diagram(const Graph& graph) {
    std::string out;

    out += "## " + graph.name;
    if (graph.base_type) out += " : " + *graph.base_type;
    out += "\n\n";
    out += "```mermaid\n";
    out += "flowchart TD\n";

    out += "    classDef param fill:#1a3d2a,stroke:#4a9a6a,color:#a6e3a1\n";
    out += "    classDef node fill:#1a2d4a,stroke:#4a7a9a,color:#89b4fa\n";
    out += "    classDef ctx fill:#2a2a3a,stroke:#6c7086,color:#cdd6f4\n";

    out += "\n    context((\"" + mermaid_escape(graph.name) + "\")):::ctx\n";

    if (!graph.parameters.empty()) {
        out += "\n    subgraph params[\"Parameters\"]\n";
        out += "        direction TB\n";
        for (auto& param : graph.parameters) {
            std::string id = mermaid_id(param.name);
            std::string dir_icon;
            switch (param.direction) {
                case ParamDirection::In: dir_icon = "IN"; break;
                case ParamDirection::Out: dir_icon = "OUT"; break;
                case ParamDirection::Var: dir_icon = "VAR"; break;
            }
            std::string label = dir_icon + " " + param.name + " : " + param.type_name;
            if (!param.default_value.empty()) label += " = " + param.default_value;
            out += "        " + id + "([\"" + mermaid_escape(label) + "\"]):::param\n";
        }
        out += "    end\n";
    }

    if (!graph.node_instances.empty()) {
        out += "\n    subgraph nodes[\"Nodes\"]\n";
        out += "        direction TB\n";
        for (auto& node : graph.node_instances) {
            std::string id = mermaid_id(node.instance_name);
            std::string label = node.type_name + "\\n" + node.instance_name;
            out += "        " + id + "[\"" + mermaid_escape(label) + "\"]:::node\n";
        }
        out += "    end\n";
    }

    for (auto& event : graph.events) {
        out += "\n";
        out += emit_block_diagram(event, "event");
    }

    for (auto& function : graph.functions) {
        out += "\n";
        out += emit_block_diagram(function, "function");
    }

    out += "```\n";
    return out;
}

std::string emit_mermaid_flow_graph_diagram(const asset::FlowGraph& graph) {
    std::string out;

    out += "## " + graph.name;
    if (!graph.schema.empty()) out += " : " + graph.schema;
    out += "\n\n";
    out += "```mermaid\n";
    out += "flowchart TD\n";

    out += "    classDef param fill:#1a3d2a,stroke:#4a9a6a,color:#a6e3a1\n";
    out += "    classDef node fill:#1a2d4a,stroke:#4a7a9a,color:#89b4fa\n";
    out += "    classDef ctx fill:#2a2a3a,stroke:#6c7086,color:#cdd6f4\n";

    out += "\n    context((\"" + mermaid_escape(graph.name) + "\")):::ctx\n";

    if (!graph.parameters.empty()) {
        out += "\n    subgraph params[\"Parameters\"]\n";
        out += "        direction TB\n";
        for (const auto& param : graph.parameters) {
            std::string label = param.direction;
            if (!label.empty()) label += " ";
            label += param.name + " : " + param.type;
            if (param.has_default) label += " = " + param.default_value.text;
            out += "        " + mermaid_id(param.name) + "([\"" + mermaid_escape(label) + "\"]):::param\n";
        }
        out += "    end\n";
    }

    if (!graph.nodes.empty()) {
        out += "\n    subgraph nodes[\"Nodes\"]\n";
        out += "        direction TB\n";
        for (const auto& node : graph.nodes) {
            std::string label = node.type + "\\n" + node.alias;
            out += "        " + mermaid_id(node.alias) + "[\"" + mermaid_escape(label) + "\"]:::node\n";
        }
        out += "    end\n";
    }

    for (const auto& block : graph.blocks) {
        out += "\n";
        const std::string block_id = mermaid_id(block.name);
        out += "    subgraph " + block_id + "[\"" + block.kind + " " + mermaid_escape(block.name) + "\"]\n";
        out += "        direction LR\n";
        out += "    end\n";

        for (const auto& edge : block.edges) {
            const auto from_dot = edge.from.find('.');
            const auto to_dot = edge.to.find('.');
            const std::string from_node = from_dot == std::string::npos ? edge.from : edge.from.substr(0, from_dot);
            const std::string to_node = to_dot == std::string::npos ? edge.to : edge.to.substr(0, to_dot);
            out += "    " + mermaid_id(from_node) + " ==>|\"" + mermaid_escape(edge.from + " → " + edge.to)
                + "\"| " + mermaid_id(to_node) + "\n";
        }

        for (const auto& edge : block.data_edges) {
            const auto source_dot = edge.source.find('.');
            const auto target_dot = edge.target.find('.');
            const std::string source_node = source_dot == std::string::npos ? edge.source : edge.source.substr(0, source_dot);
            const std::string target_node = target_dot == std::string::npos ? edge.target : edge.target.substr(0, target_dot);
            out += "    " + mermaid_id(source_node) + " -.->|\"" + mermaid_escape(edge.source + " → " + edge.target)
                + "\"| " + mermaid_id(target_node) + "\n";
        }
    }

    out += "```\n";
    return out;
}

} // namespace debug
} // namespace gs
