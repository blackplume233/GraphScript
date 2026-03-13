#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>

#include "graphscript/parse/token.h"
#include "graphscript/core/pin.h"

namespace gs {

/// Base for all AST nodes; holds source range and leading trivia.
struct ASTNode {
    SourceRange   range;
    std::string   leading_trivia;
    virtual ~ASTNode() = default;
};

// ─── Declaration file (.d.gs) nodes ────────────────────────────────

/// Declares a type (optionally constructible).
struct DeclareTypeNode : ASTNode {
    std::string name;
    bool        constructible = false;
};

/// Pin declaration (kind, direction, name, type).
struct PinDeclNode : ASTNode {
    PinKind      kind;
    PinDirection direction;
    std::string  name;
    std::string  type_name;
};

/// Declares a node type with its pins.
struct DeclareNodeNode : ASTNode {
    std::string                             name;
    std::vector<std::unique_ptr<PinDeclNode>> pins;
};

/// Declares a schema with field name-type pairs.
struct DeclareSchemaNode : ASTNode {
    std::string                                        name;
    std::vector<std::pair<std::string, std::string>>   fields;
};

// ─── Script file (.gs) nodes ───────────────────────────────────────

/// Import path (module or native).
struct ImportNode : ASTNode {
    std::string path;
};

/// Top-level let binding (name, type, constructor arg).
struct LetDeclNode : ASTNode {
    std::string name;
    std::string type_name;
    std::string constructor_arg;
};

/// Graph parameter (name, type, direction, default).
struct ParamDeclNode : ASTNode {
    std::string  name;
    std::string  type_name;
    std::string  direction;  // "in", "out", "var"
    std::string  default_value;
};

/// Node instance in a graph (type, instance name, initializer).
struct NodeInstanceNode : ASTNode {
    std::string type_name;
    std::string instance_name;
    std::string initializer;
};

/// Exec flow statement (from node/pin to node/pin).
struct FlowStmtNode : ASTNode {
    std::string from_node;
    std::string from_pin;
    std::string to_node;
    std::string to_pin;
};

/// Data link statement (target node/pin <- source node/pin).
struct LinkStmtNode : ASTNode {
    std::string target_node;
    std::string target_pin;
    std::string source_node;
    std::string source_pin;
};

/// Event handler (name, flow and link statements).
struct EventNode : ASTNode {
    std::string                               name;
    std::vector<std::unique_ptr<FlowStmtNode>> flow_stmts;
    std::vector<std::unique_ptr<LinkStmtNode>>  link_stmts;
};

/// Function definition (name, flow and link statements).
struct FunctionNode : ASTNode {
    std::string                               name;
    std::vector<std::unique_ptr<FlowStmtNode>> flow_stmts;
    std::vector<std::unique_ptr<LinkStmtNode>>  link_stmts;
};

/// Comment node (instance name, text).
struct CommentNode : ASTNode {
    std::string instance_name;
    std::string text;
};

/// Metadata entry (scope, node, property, value).
struct MetadataNode : ASTNode {
    std::string scope;
    std::string node;
    std::string property;
    std::string value;
};

/// Generate block (comments and metadata for codegen).
struct GenerateNode : ASTNode {
    std::vector<std::unique_ptr<CommentNode>>  comments;
    std::vector<std::unique_ptr<MetadataNode>> metadata;
};

/// Graph definition (name, base, params, instances, events, functions, generate).
struct GraphNode : ASTNode {
    std::string                                  name;
    std::optional<std::string>                   base_type;
    std::vector<std::unique_ptr<ParamDeclNode>>  params;
    std::vector<std::unique_ptr<NodeInstanceNode>> node_instances;
    std::vector<std::unique_ptr<EventNode>>      events;
    std::vector<std::unique_ptr<FunctionNode>>   functions;
    std::unique_ptr<GenerateNode>                generate;
};

/// Base for expression nodes.
struct ExprNode : ASTNode {};

/// Literal value (string, int, float, bool).
struct LiteralNode : ExprNode {
    std::string value;
    TokenType   literal_type;
};

/// Identifier reference.
struct IdentifierNode : ExprNode {
    std::string name;
};

/// Type constructor call (type name, argument).
struct ConstructorNode : ExprNode {
    std::string type_name;
    std::string argument;
};

/// Root AST node: imports, lets, declarations, and graphs.
struct ModuleNode : ASTNode {
    std::vector<std::unique_ptr<ImportNode>>        imports;
    std::vector<std::unique_ptr<LetDeclNode>>       let_decls;
    std::vector<std::unique_ptr<DeclareTypeNode>>   declare_types;
    std::vector<std::unique_ptr<DeclareNodeNode>>   declare_nodes;
    std::vector<std::unique_ptr<DeclareSchemaNode>> declare_schemas;
    std::vector<std::unique_ptr<GraphNode>>         graphs;
};

} // namespace gs
