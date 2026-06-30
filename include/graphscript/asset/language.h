#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "graphscript/core/result.h"
#include "graphscript/core/source_range.h"
#include "graphscript/diagnostic/diagnostic.h"

namespace gs::asset {

enum class ExprKind {
    Missing,
    String,
    Int,
    Float,
    Bool,
    Null,
    Ref,
    AssetRef,
    Array,
    InlineObject,
    Raw
};

struct TextSpan {
    size_t offset = 0;
    size_t length = 0;
    SourceRange range;
};

struct Expression;
struct Property;

struct Expression {
    ExprKind kind = ExprKind::Missing;
    std::string text;
    std::vector<Expression> elements;
    std::vector<Property> properties;
    TextSpan span;
};

struct AttributeArg {
    std::string name;
    Expression value;
    TextSpan span;
};

struct Attribute {
    std::string name;
    std::vector<AttributeArg> args;
    TextSpan span;
};

struct ImportDecl {
    std::string path;
    TextSpan span;
    TextSpan path_span;
};

struct ParameterDecl {
    std::string name;
    std::string type;
    Expression default_value;
    bool has_default = false;
    TextSpan span;
    TextSpan name_span;
};

struct ModuleDecl {
    std::string id;
    std::unordered_map<std::string, Expression> metadata;
    TextSpan span;
};

struct EnumMemberDecl {
    std::string name;
    Expression value;
    bool has_value = false;
    TextSpan span;
};

struct EnumDecl {
    std::string name;
    std::vector<EnumMemberDecl> members;
    bool exported = false;
    TextSpan span;
};

struct FieldDecl {
    std::string name;
    std::string type;
    Expression default_value;
    bool has_default = false;
    std::vector<Attribute> attributes;
    TextSpan span;
    TextSpan name_span;
};

struct CommandCall {
    std::vector<std::string> callee_parts;
    std::vector<Expression> args;
    std::vector<Attribute> attributes;
    TextSpan span;
};

struct Assignment {
    std::string target;
    Expression value;
    TextSpan span;
};

struct Directive {
    std::string name;
    std::vector<Expression> args;
    std::vector<ParameterDecl> parameters;
    std::vector<Attribute> attributes;
    TextSpan span;
};

struct ObjectDecl {
    std::string name;
    std::string base_type;
    std::vector<FieldDecl> fields;
    std::vector<Attribute> attributes;
    bool exported = false;
    TextSpan span;
};

struct BlockKindDecl {
    std::string name;
    std::string base_type;
    std::vector<Property> properties;
    std::vector<std::string> directives;
    bool exported = false;
    TextSpan span;
};

struct CommandDecl {
    std::string name;
    std::vector<ParameterDecl> parameters;
    std::string return_type;
    bool exported = false;
    TextSpan span;
};

struct SchemaDecl {
    std::string name;
    std::string base_type;
    std::vector<Property> properties;
    std::vector<std::string> directives;
    bool exported = false;
    TextSpan span;
};

struct LintDecl {
    std::string name;
    std::string target_type;
    bool exported = false;
    TextSpan span;
};

struct SymbolDecl {
    std::string kind;
    std::string name;
    std::string base_type;
    bool exported = false;
    TextSpan span;
    TextSpan name_span;
};

struct Property {
    std::string path;
    Expression value;
    std::vector<Attribute> attributes;
    TextSpan span;
    TextSpan name_span;
    TextSpan value_span;
};

struct ConstObject {
    std::string alias;
    std::string type;
    std::vector<Property> properties;
    std::vector<CommandCall> calls;
    std::vector<Assignment> assignments;
    std::vector<Directive> directives;
    std::vector<Attribute> attributes;
    TextSpan span;
    TextSpan alias_span;
    size_t body_start_offset = 0;
    size_t body_end_offset = 0;
};

struct Block;

struct ItemContainer {
    std::vector<Property> properties;
    std::vector<ConstObject> consts;
    std::vector<CommandCall> calls;
    std::vector<Assignment> assignments;
    std::vector<Directive> directives;
    std::vector<std::unique_ptr<Block>> blocks;
};

struct Block {
    std::string kind;
    std::string name;
    std::string type;
    std::vector<ParameterDecl> parameters;
    std::vector<Attribute> attributes;
    TextSpan span;
    TextSpan name_span;
    size_t body_start_offset = 0;
    size_t body_end_offset = 0;
    ItemContainer items;
};

struct Module {
    std::string source_name;
    std::vector<ImportDecl> imports;
    std::vector<ModuleDecl> modules;
    std::vector<EnumDecl> enums;
    std::vector<ObjectDecl> objects;
    std::vector<BlockKindDecl> block_kinds;
    std::vector<CommandDecl> commands;
    std::vector<SchemaDecl> schemas;
    std::vector<LintDecl> lints;
    std::vector<SymbolDecl> symbols;
    ItemContainer items;
};

struct ParseResult {
    Module module;
    std::vector<Diagnostic> diagnostics;
};

class Parser {
public:
    Parser(std::string source, std::string source_name = "");

    ParseResult parse();

private:
    std::string source_;
    std::string source_name_;
};

struct ExportedSymbol {
    std::string kind;
    std::string name;
    TextSpan span;
};

struct ModuleGraph {
    std::string module_id;
    bool module_id_inferred = true;
    std::vector<std::string> imports;
    std::vector<ExportedSymbol> exports;
};

class Linter {
public:
    static std::vector<Diagnostic> lint(const Module& module, ModuleGraph* graph = nullptr);
};

struct TextEdit {
    size_t offset = 0;
    size_t length = 0;
    std::string replacement;
    SourceRange range;
};

struct TextPatch {
    std::vector<TextEdit> edits;
};

class Patcher {
public:
    static Result<std::string, std::string> apply(const std::string& source, const TextPatch& patch);
    static TextPatch add_import(const std::string& source, const std::string& path);
    static Result<TextPatch, std::string> set_property(const std::string& source,
                                                       const Module& module,
                                                       const std::string& object_alias,
                                                       const std::string& property_path,
                                                       const std::string& value);
    static Result<TextPatch, std::string> add_node(const std::string& source,
                                                   const Module& module,
                                                   const std::string& graph_name,
                                                   const std::string& alias,
                                                   const std::string& type,
                                                   const std::string& body = "");
    static Result<TextPatch, std::string> add_block(const std::string& source,
                                                    const Module& module,
                                                    const std::string& parent_block_name,
                                                    const std::string& kind,
                                                    const std::string& name,
                                                    const std::string& type = "");
    static Result<TextPatch, std::string> add_attribute(const std::string& source,
                                                        const Module& module,
                                                        const std::string& target_name,
                                                        const std::string& attribute_source);
    static Result<TextPatch, std::string> connect(const std::string& source,
                                                  const Module& module,
                                                  const std::string& graph_name,
                                                  const std::string& from,
                                                  const std::string& to);
    static Result<TextPatch, std::string> disconnect(const std::string& source,
                                                     const Module& module,
                                                     const std::string& graph_name,
                                                     const std::string& from,
                                                     const std::string& to);
    static Result<TextPatch, std::string> rename_node(const std::string& source,
                                                      const Module& module,
                                                      const std::string& graph_name,
                                                      const std::string& old_alias,
                                                      const std::string& new_alias);
    static Result<TextPatch, std::string> rename_block(const std::string& source,
                                                       const Module& module,
                                                       const std::string& old_name,
                                                       const std::string& new_name);
};

struct PinDefinition {
    std::string name;
    std::string kind;
    std::string direction;
    std::string type;
    TextSpan span;
};

struct FlowNode {
    std::string alias;
    std::string type;
    std::vector<Property> properties;
    std::vector<PinDefinition> pins;
    TextSpan span;
};

struct GraphParameter {
    std::string name;
    std::string type;
    std::string direction;
    Expression default_value;
    bool has_default = false;
    std::vector<Attribute> attributes;
    TextSpan span;
};

struct FlowEdge {
    std::string from;
    std::string to;
    bool valid = true;
    TextSpan span;
};

struct FlowDataEdge {
    std::string source;
    std::string target;
    bool valid = true;
    TextSpan span;
};

struct FlowBlock {
    std::string kind;
    std::string name;
    std::vector<FlowEdge> edges;
    std::vector<FlowDataEdge> data_edges;
    TextSpan span;
};

struct FlowGraph {
    std::string name;
    std::string schema;
    std::vector<GraphParameter> parameters;
    std::vector<FlowNode> nodes;
    // Canonical flattened edges for whole-graph consumers. FlowBlock keeps the
    // same source edges grouped by event/function/entry block for editor views.
    std::vector<FlowEdge> edges;
    std::vector<FlowDataEdge> data_edges;
    std::vector<FlowBlock> blocks;
    std::vector<Diagnostic> diagnostics;
};

class FlowGraphProjector {
public:
    static Result<FlowGraph, std::string> project(const Module& module, const std::string& graph_name = "");
};

} // namespace gs::asset
