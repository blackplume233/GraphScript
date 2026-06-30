#pragma once

#include <string>
#include <vector>

#include "graphscript/core/graph.h"
#include "graphscript/core/initializer_field.h"
#include "graphscript/core/source_range.h"

namespace gs {

/// Resolved import declaration in a GraphScript module.
struct ImportDecl {
    std::string path;
    bool        is_native = false;
    bool        loaded = false;
    std::vector<Annotation> annotations;  ///< C# style annotations.
    SourceRange source_range;             ///< Source span of this import declaration.
    SourceRange path_range;               ///< Source span of the import path string literal.
};

/// Top-level let binding in a GraphScript module.
struct LetDecl {
    std::string name;
    SourceRange name_range;             ///< Source span of the let binding name.
    std::string type_name;
    SourceRange type_name_range;        ///< Source span of the constructible type reference.
    std::string constructor_arg;
    std::vector<InitializerField> initializer_fields; ///< Properties inside an asset `new Type { ... }` const body.
    SourceRange source_range;           ///< Source span of this top-level let declaration.
    SourceRange constructor_range;      ///< Source span of the constructor call expression.
    SourceRange constructor_arg_range;  ///< Source span of the constructor argument expression.
    std::vector<Annotation> annotations; ///< C# style annotations.
};

/// Compiled module: file path, imports, lets, and graphs.
struct Module {
    std::string              file_path;
    std::vector<ImportDecl>  imports;
    std::vector<LetDecl>     top_level_lets;
    std::vector<Graph>       graphs;
};

} // namespace gs
