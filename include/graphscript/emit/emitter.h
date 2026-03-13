#pragma once

#include <string>
#include "graphscript/core/graph.h"
#include "graphscript/compile/compiler.h"

namespace gs {

/// Emits GraphScript source from compiled Module or Graph.
class Emitter {
public:
    /// Emits full module (imports, lets, graphs).
    std::string emit(const Module& module) const;
    /// Emits a single graph definition.
    std::string emit_graph(const Graph& graph) const;

    /// Emits all graphs in the module as Mermaid markdown diagrams.
    std::string emit_diagram(const Module& module) const;
    /// Emits a single graph as a Mermaid flowchart.
    std::string emit_graph_diagram(const Graph& graph) const;

private:
    std::string emit_annotations(const std::vector<Annotation>& annots, const std::string& indent) const;
    std::string emit_imports(const std::vector<ImportDecl>& imports) const;
    std::string emit_lets(const std::vector<LetDecl>& lets) const;
    std::string emit_params(const std::vector<GraphParameter>& params) const;
    std::string emit_node_instances(const std::vector<NodeInstance>& instances) const;
    std::string emit_event(const Event& ev) const;
    std::string emit_function(const Function& fn) const;
    std::string emit_generate(const GenerateBlock& gen) const;
    std::string emit_logic_stmts(const std::vector<FlowConnection>& flows, const std::vector<DataLink>& links) const;

    // Mermaid helpers
    static std::string mermaid_id(const std::string& name);
    static std::string mermaid_escape(const std::string& text);
    std::string emit_block_diagram(const LogicBlock& block, const std::string& kind) const;
};

} // namespace gs
