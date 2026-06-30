#pragma once

#include <string>

#include "graphscript/core/module.h"

namespace gs {
namespace asset {
struct FlowGraph;
}
namespace debug {

/// Emits all graphs in the module as Mermaid markdown diagrams.
std::string emit_mermaid_diagram(const Module& module);

/// Emits a single graph as a Mermaid flowchart.
std::string emit_mermaid_graph_diagram(const Graph& graph);

/// Emits a projected asset flow graph as a Mermaid flowchart.
std::string emit_mermaid_flow_graph_diagram(const asset::FlowGraph& graph);

} // namespace debug
} // namespace gs
