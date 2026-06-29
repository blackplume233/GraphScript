#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "graphscript/parse/token.h"

namespace gs {

/// Severity level for diagnostics.
enum class Severity : uint8_t {
    Warning,
    Error
};

/// Structured target metadata for graph/source diagnostics.
struct DiagnosticTarget {
    std::string graph;
    std::string block_kind;
    std::string block_name;
    std::string node_instance;
    std::string pin_name;
    std::string parameter_name;
    std::string reference;
    std::string connection_kind;
};

/// A tool-facing action suggested by a diagnostic.
struct DiagnosticAction {
    std::string title;
    std::string kind;
    std::string command;
    SourceRange edit_range;
    std::string replacement;
};

/// A single diagnostic issue with stable metadata for tools and UI.
struct Diagnostic {
    Severity severity;
    std::string message;
    std::string context;
    std::string code;
    SourceRange range;
    std::string hint;
    DiagnosticTarget target;
    std::vector<DiagnosticAction> actions;
};

} // namespace gs
