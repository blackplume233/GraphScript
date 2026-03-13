#pragma once

#include <string>
#include <vector>

namespace gs {

/// Severity level for validation diagnostics.
enum class Severity : uint8_t {
    Warning,
    Error
};

/// A single validation issue (warning or error) with optional context.
struct Diagnostic {
    Severity    severity;
    std::string message;
    std::string context;
};

class EditGraph;
struct GraphSchema;

/// Validates graph structure (orphans, cycles, pin existence) without schema rules.
std::vector<Diagnostic> validate_common(const EditGraph& graph);
/// Validates graph against schema (connection policy, tags, required events).
std::vector<Diagnostic> validate_schema(const EditGraph& graph, const GraphSchema& schema);

} // namespace gs
