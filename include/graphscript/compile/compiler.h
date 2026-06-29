#pragma once

#include <string>
#include <vector>
#include <memory>

#include "graphscript/core/result.h"
#include "graphscript/core/graph.h"
#include "graphscript/parse/ast.h"
#include "graphscript/registry/environment.h"
#include "graphscript/diagnostic/diagnostic.h"

namespace gs {

/// Resolved import (path, whether native).
struct ImportDecl {
    std::string path;
    bool        is_native = false;
    bool        loaded = false;
    std::vector<Annotation> annotations;  ///< C# 风格标注
    SourceRange source_range;  ///< Source span of this import declaration.
    SourceRange path_range;    ///< Source span of the import path string literal.
};

/// Top-level let binding (name, type, constructor arg).
struct LetDecl {
    std::string name;
    SourceRange name_range;             ///< Source span of the let binding name.
    std::string type_name;
    SourceRange type_name_range;         ///< Source span of the constructible type reference.
    std::string constructor_arg;
    SourceRange source_range;            ///< Source span of this top-level let declaration.
    SourceRange constructor_range;       ///< Source span of the constructor call expression.
    SourceRange constructor_arg_range;   ///< Source span of the constructor argument expression.
    std::vector<Annotation> annotations; ///< C# 风格标注
};

/// Compiled module: file path, imports, lets, and graphs.
struct Module {
    std::string              file_path;
    std::vector<ImportDecl>  imports;
    std::vector<LetDecl>     top_level_lets;
    std::vector<Graph>       graphs;
};

/// Compiles AST into Module (graphs, types, nodes).
class Compiler {
public:
    explicit Compiler(Environment& env);

    /// Compiles module. Returns error string on failure.
    Result<Module, std::string> compile(const ModuleNode& ast, const std::string& file_path = "");
    /// Returns structured diagnostics collected during the last compile attempt.
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

private:
    void process_declare_types(const ModuleNode& ast, const std::string& file_path);
    void process_declare_nodes(const ModuleNode& ast, const std::string& file_path);
    void process_declare_schemas(const ModuleNode& ast, const std::string& file_path);

    Graph compile_graph(const GraphNode& gn);
    NodeDefinition derive_node_from_graph(const Graph& graph);
    Result<void, std::string> validate_graph_scope(const Graph& graph, const GraphNode& graph_ast);
    void record_error(const std::string& message,
                      const std::string& context,
                      const std::string& code,
                      SourceRange range,
                      const std::string& hint,
                      const DiagnosticTarget& target = {});

    GraphParameter compile_param(const ParamDeclNode& pn);
    NodeInstance   compile_node_instance(const NodeInstanceNode& ni);
    Event          compile_event(const EventNode& en);
    Function       compile_function(const FunctionNode& fn);
    GenerateBlock  compile_generate(const GenerateNode& gn);

    void compile_logic_stmts(
        const std::vector<std::unique_ptr<FlowStmtNode>>& flow_stmts,
        const std::vector<std::unique_ptr<LinkStmtNode>>& link_stmts,
        std::vector<FlowConnection>& flows,
        std::vector<DataLink>& links
    );

    Environment& env_;
    std::vector<Diagnostic> diagnostics_;
};

} // namespace gs
