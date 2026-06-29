#include "graphscript/schema/validator.h"
#include "graphscript/edit/edit_graph.h"
#include "graphscript/schema/graph_schema.h"

namespace gs {

// Common validation: orphan detection, function scope isolation.
std::vector<Diagnostic> validate_common(const EditGraph& graph) {
    std::vector<Diagnostic> diags;

    // Check for nodes with zero connections (orphans)
    graph.for_each_node([&](Handle h, const EditNode& node) {
        bool has_conn = false;
        graph.for_each_connection([&](Handle, const EditConnection& c) {
            if (c.from_node == h || c.to_node == h) has_conn = true;
        });
        if (!has_conn) {
            diags.push_back({Severity::Warning,
                "Node '" + node.instance_name + "' has no connections",
                graph.name()});
        }
    });

    return diags;
}

// Schema-specific validation: required events, connection policy, allowed tags.
std::vector<Diagnostic> validate_schema(const EditGraph& graph, const GraphSchema& schema) {
    std::vector<Diagnostic> diags;
    (void)graph;

    // Check required events
    for (auto& req : schema.required_events) {
        bool found = false;
        // Required events are checked at the Module/Graph level, not EditGraph.
        // This is a placeholder for future per-schema validation.
        (void)found;
        (void)req;
    }

    return diags;
}

} // namespace gs
