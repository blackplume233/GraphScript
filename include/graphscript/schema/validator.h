#pragma once

#include <vector>

#include "graphscript/diagnostic/diagnostic.h"

namespace gs {

class EditGraph;
struct GraphSchema;

/// Validates graph structure (orphans, cycles, pin existence) without schema rules.
std::vector<Diagnostic> validate_common(const EditGraph& graph);
/// Validates graph against schema (connection policy, tags, required events).
std::vector<Diagnostic> validate_schema(const EditGraph& graph, const GraphSchema& schema);

} // namespace gs
