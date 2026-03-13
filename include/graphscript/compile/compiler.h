#pragma once

#include <string>
#include <vector>
#include <memory>

#include "graphscript/core/result.h"
#include "graphscript/core/graph.h"
#include "graphscript/parse/ast.h"
#include "graphscript/registry/environment.h"

namespace gs {

/// Resolved import (path, whether native).
struct ImportDecl {
    std::string path;
    bool        is_native = false;
};

/// Top-level let binding (name, type, constructor arg).
struct LetDecl {
    std::string name;
    std::string type_name;
    std::string constructor_arg;
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

private:
    void process_declare_types(const ModuleNode& ast);
    void process_declare_nodes(const ModuleNode& ast);
    void process_declare_schemas(const ModuleNode& ast);

    Graph compile_graph(const GraphNode& gn);
    NodeDefinition derive_node_from_graph(const Graph& graph);

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
};

} // namespace gs
