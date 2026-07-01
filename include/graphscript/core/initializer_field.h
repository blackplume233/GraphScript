#pragma once

#include <string>

#include "graphscript/core/source_range.h"

namespace gs {

/// Source-trace metadata for a single `name = value` entry inside a node initializer.
struct InitializerField {
    std::string name;        ///< Initializer field name.
    std::string value;       ///< Initializer field value text as parsed from tokens.
    SourceRange source_range;      ///< Source span of the full field assignment.
    SourceRange name_range;        ///< Source span of the field name.
    SourceRange value_range;       ///< Source span of the field value expression.
    SourceRange value_constructor_range;      ///< Source span of the field value constructor call expression.
    SourceRange value_constructor_type_range; ///< Source span of the field value constructor type name.
    SourceRange value_constructor_arg_range;  ///< Source span of the field value constructor argument expression.
};

} // namespace gs
