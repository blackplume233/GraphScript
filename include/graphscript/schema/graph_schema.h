#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>

#include "graphscript/core/annotation.h"
#include "graphscript/core/source_range.h"
#include "graphscript/schema/connection_policy.h"

namespace gs {

struct Diagnostic;
class EditGraph;

/// Source-backed schema field declaration.
struct GraphSchemaField {
    std::string name;         ///< Schema property name
    std::string value;        ///< Parsed property value text
    std::vector<Annotation> annotations; ///< C# 风格标注
    SourceRange source_range; ///< Field declaration source span
    SourceRange value_range;  ///< Field value source span
    SourceRange name_range;   ///< Field name token source span
    SourceRange value_constructor_range;      ///< Field value constructor call span when present.
    SourceRange value_constructor_type_range; ///< Field value constructor type token span when present.
    SourceRange value_constructor_arg_range;  ///< Field value constructor argument span when present.
    std::string source_file;   ///< Source file containing this declaration when loaded from disk.
};

/// Schema defining validation rules and constraints for a graph.
struct GraphSchema {
    std::string                   name;
    ConnectionPolicy              connection_policy;
    std::vector<std::string>      allowed_node_tags;
    std::vector<std::string>      required_events;
    std::vector<GraphSchemaField> fields;
    std::vector<Annotation>       annotations;  ///< C# 风格标注
    SourceRange                   source_range; ///< Schema declaration source span when loaded from a declaration file.
    SourceRange                   name_range;   ///< Schema name token source span when loaded from a declaration file.
    std::string                   source_file;  ///< Source file containing this declaration when loaded from disk.
};

} // namespace gs
