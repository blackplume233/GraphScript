#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>

#include "graphscript/parse/token.h"
#include "graphscript/core/pin.h"
#include "graphscript/core/annotation.h"
#include "graphscript/core/initializer_field.h"

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
    SourceRange name_range;
    std::vector<Annotation> annotations;  ///< C# 风格前缀标注
};

/// Pin declaration (kind, direction, name, type).
struct PinDeclNode : ASTNode {
    PinKind      kind;
    PinDirection direction;
    std::string  name;
    SourceRange  name_range;
    std::string  type_name;
    SourceRange  type_name_range;
    std::vector<Annotation> annotations;  ///< C# 风格前缀标注
};

/// Intrinsic node field declaration.
struct NodeFieldDeclNode : ASTNode {
    std::string name;
    SourceRange name_range;
    std::string type_name;
    SourceRange type_name_range;
    std::string default_value;
    SourceRange default_value_range;
    SourceRange default_constructor_range;
    SourceRange default_constructor_type_range;
    SourceRange default_constructor_arg_range;
    std::vector<Annotation> annotations;  ///< C# 风格前缀标注
};

/// Declares a node type with its pins.
struct DeclareNodeNode : ASTNode {
    std::string                             name;
    SourceRange                             name_range;
    std::vector<std::unique_ptr<PinDeclNode>> pins;
    std::vector<std::unique_ptr<NodeFieldDeclNode>> fields;
    std::vector<Annotation>                 annotations;  ///< C# 风格前缀标注
};

/// Schema field declaration (name and parsed value text).
struct SchemaFieldNode : ASTNode {
    std::string name;
    SourceRange name_range;
    std::string value;
    SourceRange value_range;
    SourceRange value_constructor_range;
    SourceRange value_constructor_type_range;
    SourceRange value_constructor_arg_range;
    std::vector<Annotation> annotations;  ///< C# 风格前缀标注
};

/// Declares a schema with field name-type pairs.
struct DeclareSchemaNode : ASTNode {
    std::string                                      name;
    SourceRange                                      name_range;
    std::vector<std::unique_ptr<SchemaFieldNode>>    fields;
    std::vector<Annotation>                          annotations;  ///< C# 风格前缀标注
};

// ─── Script file (.gs) nodes ───────────────────────────────────────

/// Import path (module or native).
struct ImportNode : ASTNode {
    std::string path;
    SourceRange path_range;
    std::vector<Annotation> annotations;  ///< C# 风格前缀标注
};

/// Top-level let binding (name, type, constructor arg).
struct LetDeclNode : ASTNode {
    std::string name;
    SourceRange name_range;
    std::string type_name;
    SourceRange type_name_range;
    std::string constructor_arg;
    SourceRange constructor_range;
    SourceRange constructor_arg_range;
    std::vector<Annotation> annotations;  ///< C# 风格前缀标注
};

/// Graph parameter (name, type, direction, default, annotations).
struct ParamDeclNode : ASTNode {
    std::string  name;
    SourceRange  name_range;
    std::string  type_name;
    SourceRange  type_name_range;
    std::string  direction;  // "in", "out", "var"
    std::string  default_value;
    SourceRange  default_value_range;
    SourceRange  default_constructor_range;
    SourceRange  default_constructor_type_range;
    SourceRange  default_constructor_arg_range;
    std::vector<Annotation> annotations;  ///< C# 风格前缀标注
};

/// Node instance in a graph (type, instance name, initializer, annotations).
struct NodeInstanceNode : ASTNode {
    std::string type_name;
    SourceRange type_name_range;
    std::string instance_name;
    SourceRange instance_name_range;
    std::string initializer;
    SourceRange initializer_range;
    SourceRange initializer_constructor_range;
    SourceRange initializer_constructor_type_range;
    SourceRange initializer_constructor_arg_range;
    std::vector<InitializerField> initializer_fields;
    std::vector<Annotation> annotations;  ///< C# 风格前缀标注
};

/// Exec flow statement (from node/pin to node/pin).
struct FlowStmtNode : ASTNode {
    std::string from_node;
    std::string from_pin;
    std::string to_node;
    std::string to_pin;
    SourceRange from_expr_range;
    SourceRange to_expr_range;
    SourceRange from_node_range;
    SourceRange from_pin_range;
    SourceRange to_node_range;
    SourceRange to_pin_range;
    std::vector<Annotation> annotations;  ///< C# 风格前缀标注
};

/// Data link statement (target node/pin <- source node/pin).
struct LinkStmtNode : ASTNode {
    std::string target_node;
    std::string target_pin;
    std::string source_node;
    std::string source_pin;
    SourceRange target_expr_range;
    SourceRange source_expr_range;
    SourceRange target_node_range;
    SourceRange target_pin_range;
    SourceRange source_node_range;
    SourceRange source_pin_range;
    std::vector<Annotation> annotations;  ///< C# 风格前缀标注
};

/// Event handler (name, flow and link statements).
struct EventNode : ASTNode {
    std::string                               name;
    SourceRange                               name_range;
    std::vector<Annotation>                   annotations;
    std::vector<std::unique_ptr<FlowStmtNode>> flow_stmts;
    std::vector<std::unique_ptr<LinkStmtNode>>  link_stmts;
};

/// Function definition (name, flow and link statements).
struct FunctionNode : ASTNode {
    std::string                               name;
    SourceRange                               name_range;
    std::vector<Annotation>                   annotations;
    std::vector<std::unique_ptr<FlowStmtNode>> flow_stmts;
    std::vector<std::unique_ptr<LinkStmtNode>>  link_stmts;
};

/// Comment node (instance name, text).
struct CommentNode : ASTNode {
    std::string instance_name;
    std::string text;
    SourceRange instance_name_range;
    SourceRange text_range;
    std::vector<Annotation> annotations;  ///< C# 风格前缀标注
};

/// Metadata entry (scope, node, property, value).
struct MetadataNode : ASTNode {
    std::string scope;
    std::string node;
    std::string property;
    std::string value;
    SourceRange scope_range;
    SourceRange node_range;
    SourceRange property_range;
    SourceRange value_range;
    SourceRange value_constructor_range;
    SourceRange value_constructor_type_range;
    SourceRange value_constructor_arg_range;
    std::vector<Annotation> annotations;  ///< C# 风格前缀标注
};

/// Generate block (comments and metadata for codegen).
struct GenerateNode : ASTNode {
    std::vector<std::unique_ptr<CommentNode>>  comments;
    std::vector<std::unique_ptr<MetadataNode>> metadata;
};

/// Graph definition (name, base, annotations, params, instances, events, functions, generate).
struct GraphNode : ASTNode {
    std::string                                  name;
    SourceRange                                  name_range;
    std::optional<std::string>                   base_type;
    SourceRange                                  base_type_range;
    std::vector<Annotation>                      annotations;  ///< C# 风格前缀标注
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
