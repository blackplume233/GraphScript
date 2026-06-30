#include "graphscript/asset/language.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <unordered_set>
#include <utility>

#include "tree-sitter-graphscript_asset.h"
#include "tree_sitter/api.h"

namespace gs::asset {

namespace {

SourceRange range_from_points(TSPoint start, TSPoint end) {
    SourceRange range;
    range.start.line = static_cast<int>(start.row + 1);
    range.start.column = static_cast<int>(start.column + 1);
    range.end.line = static_cast<int>(end.row + 1);
    range.end.column = static_cast<int>(end.column + 1);
    return range;
}

TextSpan span_of(TSNode node) {
    TextSpan span;
    if (ts_node_is_null(node)) return span;
    span.offset = ts_node_start_byte(node);
    const size_t end = ts_node_end_byte(node);
    span.length = end >= span.offset ? end - span.offset : 0;
    span.range = range_from_points(ts_node_start_point(node), ts_node_end_point(node));
    return span;
}

TextSpan span_from_start_to_end(TSNode start_node, TSNode end_node) {
    TextSpan span;
    if (ts_node_is_null(start_node) || ts_node_is_null(end_node)) return span;
    span.offset = ts_node_start_byte(start_node);
    const size_t end = ts_node_end_byte(end_node);
    span.length = end > span.offset ? end - span.offset : 0;
    span.range = range_from_points(ts_node_start_point(start_node), ts_node_end_point(end_node));
    return span;
}

bool null_node(TSNode node) {
    return ts_node_is_null(node);
}

std::string node_type(TSNode node) {
    return null_node(node) ? "" : ts_node_type(node);
}

bool is_type(TSNode node, const char* type) {
    return !null_node(node) && std::strcmp(ts_node_type(node), type) == 0;
}

TSNode child_by_field(TSNode node, const char* field) {
    if (null_node(node)) return {};
    return ts_node_child_by_field_name(node, field, static_cast<uint32_t>(std::strlen(field)));
}

std::string slice(const std::string& source, TSNode node) {
    if (null_node(node)) return "";
    const size_t start = ts_node_start_byte(node);
    const size_t end = ts_node_end_byte(node);
    if (start > source.size() || end > source.size() || end < start) return "";
    return source.substr(start, end - start);
}

std::string slice_span(const std::string& source, const TextSpan& span) {
    if (span.offset > source.size() || span.offset + span.length > source.size()) return "";
    return source.substr(span.offset, span.length);
}

std::string unquote(const std::string& text) {
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

std::vector<TSNode> named_children(TSNode node) {
    std::vector<TSNode> children;
    if (null_node(node)) return children;
    const uint32_t count = ts_node_named_child_count(node);
    children.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        children.push_back(ts_node_named_child(node, i));
    }
    return children;
}

TSNode first_named_child_of_type(TSNode node, const char* type) {
    for (TSNode child : named_children(node)) {
        if (is_type(child, type)) return child;
    }
    return {};
}

std::vector<Attribute> parse_attributes(const std::string& source, TSNode node);
Expression parse_expression(const std::string& source, TSNode node);
Property parse_property(const std::string& source, TSNode node);
CommandCall parse_call(const std::string& source, TSNode node);
Assignment parse_assignment(const std::string& source, TSNode node);
Directive parse_directive(const std::string& source, TSNode node);
void parse_item_into(const std::string& source, TSNode node, ItemContainer& items);

Diagnostic make_diag(Severity severity,
                     const std::string& code,
                     const std::string& message,
                     SourceRange range,
                     const std::string& context = "",
                     const std::string& hint = "") {
    Diagnostic diag;
    diag.severity = severity;
    diag.code = code;
    diag.message = message;
    diag.context = context;
    diag.range = range;
    diag.hint = hint;
    return diag;
}

std::vector<std::string> split_qualified(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == '.') {
            if (!current.empty()) parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

bool is_identifier_text(const std::string& text) {
    if (text.empty()) return false;
    const unsigned char first = static_cast<unsigned char>(text.front());
    if (!std::isalpha(first) && text.front() != '_') return false;
    for (char c : text) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '_') return false;
    }
    return true;
}

bool identifier_boundary(const std::string& source, size_t offset, size_t length) {
    const auto ident = [](char c) {
        const unsigned char uc = static_cast<unsigned char>(c);
        return std::isalnum(uc) || c == '_';
    };
    const bool left = offset == 0 || !ident(source[offset - 1]);
    const size_t right_index = offset + length;
    const bool right = right_index >= source.size() || !ident(source[right_index]);
    return left && right;
}

void collect_tree_errors(TSNode node, std::vector<Diagnostic>& diagnostics) {
    if (ts_node_is_error(node) || ts_node_is_missing(node)) {
        diagnostics.push_back(make_diag(Severity::Error,
                                        "GS-SYN-001",
                                        ts_node_is_missing(node) ? "Missing syntax node" : "Syntax error",
                                        span_of(node).range,
                                        node_type(node)));
    }
    const uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        collect_tree_errors(ts_node_child(node, i), diagnostics);
    }
}

std::vector<ParameterDecl> parse_parameters(const std::string& source, TSNode list_node) {
    std::vector<ParameterDecl> parameters;
    if (null_node(list_node)) return parameters;
    for (TSNode child : named_children(list_node)) {
        if (!is_type(child, "parameter_declaration")) continue;
        ParameterDecl param;
        param.span = span_of(child);
        TSNode name = child_by_field(child, "name");
        TSNode type = child_by_field(child, "type");
        TSNode def = child_by_field(child, "default_value");
        param.name = slice(source, name);
        param.name_span = span_of(name);
        param.type = slice(source, type);
        param.type_span = span_of(type);
        if (!null_node(def)) {
            param.default_value = parse_expression(source, def);
            param.has_default = true;
        }
        parameters.push_back(std::move(param));
    }
    return parameters;
}

Expression parse_expression(const std::string& source, TSNode node) {
    Expression expr;
    if (null_node(node)) {
        expr.kind = ExprKind::Missing;
        return expr;
    }
    expr.span = span_of(node);
    expr.text = slice(source, node);
    const std::string type = node_type(node);
    if (type == "string_literal") {
        expr.kind = ExprKind::String;
        expr.text = unquote(expr.text);
    } else if (type == "int_literal") {
        expr.kind = ExprKind::Int;
    } else if (type == "float_literal") {
        expr.kind = ExprKind::Float;
    } else if (type == "bool_literal") {
        expr.kind = ExprKind::Bool;
    } else if (type == "null_literal") {
        expr.kind = ExprKind::Null;
    } else if (type == "ref_expression" || type == "qualified_name") {
        expr.kind = ExprKind::Ref;
    } else if (type == "asset_ref_expression") {
        expr.kind = ExprKind::AssetRef;
        TSNode path = child_by_field(node, "path");
        expr.text = unquote(slice(source, path));
    } else if (type == "call_expression") {
        expr.kind = ExprKind::Call;
        TSNode callee = child_by_field(node, "callee");
        expr.callee = slice(source, callee);
        expr.callee_span = span_of(callee);
        TSNode args = child_by_field(node, "arguments");
        for (TSNode child : named_children(args)) {
            expr.elements.push_back(parse_expression(source, child));
        }
    } else if (type == "array_expression") {
        expr.kind = ExprKind::Array;
        for (TSNode child : named_children(node)) {
            if (is_type(child, "line_comment") || is_type(child, "block_comment")) continue;
            expr.elements.push_back(parse_expression(source, child));
        }
    } else if (type == "inline_object_expression") {
        expr.kind = ExprKind::InlineObject;
        for (TSNode child : named_children(node)) {
            if (is_type(child, "property_declaration")) {
                expr.properties.push_back(parse_property(source, child));
            }
        }
    } else {
        expr.kind = ExprKind::Raw;
    }
    return expr;
}

AttributeArg parse_attribute_arg(const std::string& source, TSNode node) {
    AttributeArg arg;
    arg.span = span_of(node);
    TSNode name = child_by_field(node, "name");
    TSNode value = child_by_field(node, "value");
    if (!null_node(name)) arg.name = slice(source, name);
    arg.value = parse_expression(source, value);
    return arg;
}

Attribute parse_attribute(const std::string& source, TSNode node) {
    Attribute attr;
    attr.span = span_of(node);
    attr.name = slice(source, child_by_field(node, "name"));
    TSNode args = child_by_field(node, "arguments");
    for (TSNode child : named_children(args)) {
        if (is_type(child, "attribute_argument")) {
            attr.args.push_back(parse_attribute_arg(source, child));
        }
    }
    return attr;
}

std::vector<Attribute> parse_attributes(const std::string& source, TSNode node) {
    std::vector<Attribute> attrs;
    for (TSNode child : named_children(node)) {
        if (is_type(child, "attribute")) attrs.push_back(parse_attribute(source, child));
    }
    return attrs;
}

Property parse_property(const std::string& source, TSNode node) {
    Property property;
    property.span = span_of(node);
    property.attributes = parse_attributes(source, node);
    TSNode name = child_by_field(node, "name");
    TSNode value = child_by_field(node, "value");
    property.path = slice(source, name);
    property.name_span = span_of(name);
    property.value = parse_expression(source, value);
    property.value_span = property.value.span;
    return property;
}

CommandCall parse_call(const std::string& source, TSNode node) {
    CommandCall call;
    call.span = span_of(node);
    call.attributes = parse_attributes(source, node);
    TSNode expression = child_by_field(node, "expression");
    TSNode callee = child_by_field(expression, "callee");
    call.callee_parts = split_qualified(slice(source, callee));
    TSNode args = child_by_field(expression, "arguments");
    for (TSNode child : named_children(args)) {
        call.args.push_back(parse_expression(source, child));
    }
    return call;
}

Assignment parse_assignment(const std::string& source, TSNode node) {
    Assignment assignment;
    assignment.span = span_of(node);
    assignment.target = slice(source, child_by_field(node, "target"));
    assignment.value = parse_expression(source, child_by_field(node, "value"));
    return assignment;
}

Directive parse_directive(const std::string& source, TSNode node) {
    Directive directive;
    directive.span = span_of(node);
    directive.attributes = parse_attributes(source, node);
    directive.name = slice(source, child_by_field(node, "name"));
    TSNode param = first_named_child_of_type(node, "directive_param_declaration");
    if (!null_node(param)) {
        ParameterDecl p;
        p.span = span_of(param);
        TSNode name = child_by_field(param, "name");
        TSNode type = child_by_field(param, "type");
        TSNode def = child_by_field(param, "default_value");
        p.name = slice(source, name);
        p.name_span = span_of(name);
        p.type = slice(source, type);
        p.type_span = span_of(type);
        if (!null_node(def)) {
            p.default_value = parse_expression(source, def);
            p.has_default = true;
        }
        directive.parameters.push_back(std::move(p));
    }
    TSNode args = first_named_child_of_type(node, "argument_list");
    for (TSNode child : named_children(args)) {
        directive.args.push_back(parse_expression(source, child));
    }
    TSNode directive_args = first_named_child_of_type(node, "directive_argument_list");
    for (TSNode child : named_children(directive_args)) {
        directive.args.push_back(parse_expression(source, child));
    }
    return directive;
}

ConstObject parse_const(const std::string& source, TSNode node) {
    ConstObject object;
    object.span = span_of(node);
    object.attributes = parse_attributes(source, node);
    TSNode alias = child_by_field(node, "name");
    TSNode value = child_by_field(node, "value");
    object.alias = slice(source, alias);
    object.alias_span = span_of(alias);
    TSNode type = child_by_field(value, "type");
    object.type = slice(source, type);
    object.type_span = span_of(type);
    TSNode body = child_by_field(value, "body");
    object.body_start_offset = null_node(body) ? object.span.offset : ts_node_start_byte(body);
    object.body_end_offset = null_node(body) ? object.span.offset + object.span.length : (ts_node_end_byte(body) > 0 ? ts_node_end_byte(body) - 1 : ts_node_end_byte(body));
    for (TSNode child : named_children(body)) {
        if (is_type(child, "property_declaration")) object.properties.push_back(parse_property(source, child));
        else if (is_type(child, "call_statement")) object.calls.push_back(parse_call(source, child));
        else if (is_type(child, "assignment_statement")) object.assignments.push_back(parse_assignment(source, child));
        else if (is_type(child, "directive_statement")) object.directives.push_back(parse_directive(source, child));
    }
    return object;
}

std::unique_ptr<Block> parse_block(const std::string& source, TSNode node) {
    auto block = std::make_unique<Block>();
    block->span = span_of(node);
    block->attributes = parse_attributes(source, node);
    block->kind = slice(source, child_by_field(node, "kind"));
    TSNode name = child_by_field(node, "name");
    block->name = slice(source, name);
    block->name_span = span_of(name);
    TSNode type = child_by_field(node, "type");
    block->type = slice(source, type);
    block->type_span = span_of(type);
    block->parameters = parse_parameters(source, child_by_field(node, "parameters"));
    TSNode body = child_by_field(node, "body");
    block->body_start_offset = null_node(body) ? block->span.offset : ts_node_start_byte(body);
    block->body_end_offset = null_node(body) ? block->span.offset + block->span.length : (ts_node_end_byte(body) > 0 ? ts_node_end_byte(body) - 1 : ts_node_end_byte(body));
    for (TSNode child : named_children(body)) {
        parse_item_into(source, child, block->items);
    }
    return block;
}

ObjectDecl parse_object_decl(const std::string& source,
                             TSNode node,
                             bool exported,
                             const TextSpan* span_override = nullptr) {
    ObjectDecl object;
    object.span = span_override ? *span_override : span_of(node);
    object.exported = exported;
    object.attributes = parse_attributes(source, node);
    TSNode object_name = child_by_field(node, "name");
    object.name = slice(source, object_name);
    object.name_span = span_of(object_name);
    object.base_type = slice(source, child_by_field(node, "type"));
    TSNode body = child_by_field(node, "body");
    const auto children = null_node(body) ? named_children(node) : named_children(body);
    for (TSNode child : children) {
        if (!is_type(child, "field_declaration")) continue;
        FieldDecl field;
        field.attributes = parse_attributes(source, child);
        TSNode name = child_by_field(child, "name");
        field.span = span_from_start_to_end(name, child);
        field.name = slice(source, name);
        field.name_span = span_of(name);
        TSNode type = child_by_field(child, "type");
        field.type = slice(source, type);
        field.type_span = span_of(type);
        TSNode def = child_by_field(child, "default_value");
        if (!null_node(def)) {
            field.default_value = parse_expression(source, def);
            field.has_default = true;
        }
        object.fields.push_back(std::move(field));
    }
    return object;
}

ModuleDecl parse_module_decl(const std::string& source, TSNode node) {
    ModuleDecl module;
    module.span = span_of(node);
    module.id = unquote(slice(source, child_by_field(node, "id")));
    TSNode body = child_by_field(node, "body");
    for (TSNode child : named_children(body)) {
        if (!is_type(child, "property_declaration")) continue;
        Property property = parse_property(source, child);
        module.metadata[property.path] = property.value;
    }
    return module;
}

EnumDecl parse_enum_decl(const std::string& source, TSNode node, bool exported) {
    EnumDecl decl;
    decl.span = span_of(node);
    decl.exported = exported;
    decl.name = slice(source, child_by_field(node, "name"));
    TSNode body = child_by_field(node, "body");
    const auto children = null_node(body) ? named_children(node) : named_children(body);
    for (TSNode child : children) {
        if (!is_type(child, "enum_member")) continue;
        EnumMemberDecl member;
        member.span = span_of(child);
        member.name = slice(source, child_by_field(child, "name"));
        TSNode value = child_by_field(child, "value");
        if (!null_node(value)) {
            member.value = parse_expression(source, value);
            member.has_value = true;
        }
        decl.members.push_back(std::move(member));
    }
    return decl;
}

BlockKindDecl parse_block_kind_decl(const std::string& source, TSNode node, bool exported) {
    BlockKindDecl decl;
    decl.span = span_of(node);
    decl.exported = exported;
    decl.name = slice(source, child_by_field(node, "name"));
    decl.base_type = slice(source, child_by_field(node, "type"));
    TSNode body = child_by_field(node, "body");
    for (TSNode child : named_children(body)) {
        if (is_type(child, "property_declaration")) decl.properties.push_back(parse_property(source, child));
        else if (is_type(child, "directive_statement")) decl.directives.push_back(slice(source, child));
    }
    return decl;
}

CommandDecl parse_command_decl(const std::string& source, TSNode node, bool exported) {
    CommandDecl decl;
    decl.span = span_of(node);
    decl.exported = exported;
    decl.name = slice(source, child_by_field(node, "name"));
    decl.parameters = parse_parameters(source, child_by_field(node, "parameters"));
    decl.return_type = slice(source, child_by_field(node, "type"));
    return decl;
}

SchemaDecl parse_schema_decl(const std::string& source,
                             TSNode node,
                             bool exported,
                             const TextSpan* span_override = nullptr) {
    SchemaDecl decl;
    decl.span = span_override ? *span_override : span_of(node);
    decl.exported = exported;
    decl.attributes = parse_attributes(source, node);
    TSNode name = child_by_field(node, "name");
    decl.name = slice(source, name);
    decl.name_span = span_of(name);
    decl.base_type = slice(source, child_by_field(node, "type"));
    TSNode body = child_by_field(node, "body");
    for (TSNode child : named_children(body)) {
        if (is_type(child, "property_declaration")) decl.properties.push_back(parse_property(source, child));
        else if (is_type(child, "directive_statement")) decl.directives.push_back(slice(source, child));
    }
    return decl;
}

LintDecl parse_lint_decl(const std::string& source, TSNode node, bool exported) {
    LintDecl decl;
    decl.span = span_of(node);
    decl.exported = exported;
    decl.name = slice(source, child_by_field(node, "name"));
    decl.target_type = slice(source, child_by_field(node, "type"));
    return decl;
}

bool field_is_flow_pin(const FieldDecl& field) {
    for (const auto& attr : field.attributes) {
        if (attr.name == "flow.pin" || attr.name == "flow.input" || attr.name == "flow.output") return true;
    }
    return false;
}

SymbolDecl make_symbol(const std::string& kind,
                       const std::string& name,
                       bool exported,
                       TextSpan span,
                       std::string base_type = {},
                       TextSpan name_span = {},
                       std::vector<Attribute> attributes = {}) {
    SymbolDecl symbol;
    symbol.kind = kind;
    symbol.name = name;
    symbol.base_type = std::move(base_type);
    symbol.attributes = std::move(attributes);
    symbol.exported = exported;
    symbol.span = span;
    symbol.name_span = name_span;
    return symbol;
}

void parse_declaration_into(const std::string& source,
                            TSNode node,
                            bool exported,
                            Module& module,
                            const TextSpan* span_override = nullptr) {
    if (is_type(node, "declaration")) {
        for (TSNode child : named_children(node)) {
            parse_declaration_into(source, child, exported, module, span_override);
        }
        return;
    }
    const std::string type = node_type(node);
    if (type == "module_declaration") {
        module.modules.push_back(parse_module_decl(source, node));
    } else if (type == "type_declaration") {
        TSNode name = child_by_field(node, "name");
        module.symbols.push_back(make_symbol(
            "type",
            slice(source, name),
            exported,
            span_override ? *span_override : span_of(node),
            slice(source, child_by_field(node, "type")),
            span_of(name),
            parse_attributes(source, node)));
    } else if (type == "enum_declaration") {
        auto decl = parse_enum_decl(source, node, exported);
        module.symbols.push_back(make_symbol("enum", decl.name, exported, decl.span));
        module.enums.push_back(std::move(decl));
    } else if (type == "kind_declaration") {
        const std::string family = slice(source, child_by_field(node, "family"));
        const std::string name = slice(source, child_by_field(node, "name"));
        module.symbols.push_back(make_symbol("kind " + family, name, exported, span_of(node)));
    } else if (type == "object_declaration") {
        auto decl = parse_object_decl(source, node, exported, span_override);
        module.symbols.push_back(make_symbol("object", decl.name, exported, decl.span));
        module.objects.push_back(std::move(decl));
    } else if (type == "block_kind_declaration") {
        auto decl = parse_block_kind_decl(source, node, exported);
        module.symbols.push_back(make_symbol("block", decl.name, exported, decl.span));
        module.block_kinds.push_back(std::move(decl));
    } else if (type == "command_declaration") {
        auto decl = parse_command_decl(source, node, exported);
        module.symbols.push_back(make_symbol("command", decl.name, exported, decl.span));
        module.commands.push_back(std::move(decl));
    } else if (type == "schema_declaration") {
        auto decl = parse_schema_decl(source, node, exported, span_override);
        module.symbols.push_back(make_symbol("schema", decl.name, exported, decl.span));
        module.schemas.push_back(std::move(decl));
    } else if (type == "lint_declaration") {
        auto decl = parse_lint_decl(source, node, exported);
        module.symbols.push_back(make_symbol("lint", decl.name, exported, decl.span));
        module.lints.push_back(std::move(decl));
    }
}

void parse_export_into(const std::string& source, TSNode node, Module& module) {
    TSNode decl = child_by_field(node, "declaration");
    if (!null_node(decl)) {
        const TextSpan export_span = span_of(node);
        parse_declaration_into(source, decl, true, module, &export_span);
        return;
    }
    TSNode names = child_by_field(node, "names");
    for (TSNode child : named_children(names)) {
        if (is_type(child, "identifier")) {
            module.symbols.push_back(make_symbol("export", slice(source, child), true, span_of(child)));
        }
    }
}

ImportDecl parse_import(const std::string& source, TSNode node) {
    ImportDecl import;
    import.span = span_of(node);
    import.attributes = parse_attributes(source, node);
    TSNode path = child_by_field(node, "path");
    import.path = unquote(slice(source, path));
    import.path_span = span_of(path);
    return import;
}

void parse_item_into(const std::string& source, TSNode node, ItemContainer& items) {
    if (is_type(node, "block_declaration")) items.blocks.push_back(parse_block(source, node));
    else if (is_type(node, "const_declaration")) items.consts.push_back(parse_const(source, node));
    else if (is_type(node, "property_declaration")) items.properties.push_back(parse_property(source, node));
    else if (is_type(node, "call_statement")) items.calls.push_back(parse_call(source, node));
    else if (is_type(node, "assignment_statement")) items.assignments.push_back(parse_assignment(source, node));
    else if (is_type(node, "directive_statement")) items.directives.push_back(parse_directive(source, node));
}

const Block* find_graph_block_in(const ItemContainer& items, const std::string& graph_name) {
    for (const auto& block : items.blocks) {
        if (block->kind == "graph" && (graph_name.empty() || block->name == graph_name)) {
            return block.get();
        }
        if (const auto* nested = find_graph_block_in(block->items, graph_name)) return nested;
    }
    return nullptr;
}

const Block* find_block_by_name_in(const ItemContainer& items, const std::string& name) {
    for (const auto& block : items.blocks) {
        if (block->name == name) return block.get();
        if (const auto* nested = find_block_by_name_in(block->items, name)) return nested;
    }
    return nullptr;
}

const Block* find_block_by_kind_and_name_in(const ItemContainer& items, const std::string& kind, const std::string& name) {
    for (const auto& block : items.blocks) {
        if (block->kind == kind && block->name == name) return block.get();
        if (const auto* nested = find_block_by_kind_and_name_in(block->items, kind, name)) return nested;
    }
    return nullptr;
}

const ConstObject* find_const_object(const ItemContainer& items, const std::string& alias) {
    for (const auto& object : items.consts) {
        if (object.alias == alias) return &object;
    }
    for (const auto& block : items.blocks) {
        if (const auto* found = find_const_object(block->items, alias)) return found;
    }
    return nullptr;
}

void collect_node_aliases(const ItemContainer& items, std::unordered_set<std::string>& aliases, bool& duplicate) {
    for (const auto& object : items.consts) {
        if (!aliases.insert(object.alias).second) duplicate = true;
    }
    for (const auto& block : items.blocks) {
        if (block->kind == "node" && !aliases.insert(block->name).second) duplicate = true;
        collect_node_aliases(block->items, aliases, duplicate);
    }
}

void collect_const_objects(const ItemContainer& items, std::vector<const ConstObject*>& out) {
    for (const auto& object : items.consts) out.push_back(&object);
    for (const auto& block : items.blocks) collect_const_objects(block->items, out);
}

void collect_node_blocks(const ItemContainer& items, std::vector<const Block*>& out) {
    for (const auto& block : items.blocks) {
        if (block->kind == "node") out.push_back(block.get());
        collect_node_blocks(block->items, out);
    }
}

void collect_flow_blocks(const ItemContainer& items, std::vector<const Block*>& out) {
    for (const auto& block : items.blocks) {
        if (block->kind == "event" || block->kind == "function" || block->kind == "entry") {
            out.push_back(block.get());
        }
        collect_flow_blocks(block->items, out);
    }
}

void collect_generate_blocks(const ItemContainer& items, std::vector<const Block*>& out) {
    for (const auto& block : items.blocks) {
        if (block->kind == "generate") out.push_back(block.get());
        collect_generate_blocks(block->items, out);
    }
}

void collect_command_calls(const ItemContainer& items, std::vector<const CommandCall*>& out) {
    for (const auto& call : items.calls) out.push_back(&call);
    for (const auto& object : items.consts) {
        for (const auto& call : object.calls) out.push_back(&call);
    }
    for (const auto& block : items.blocks) collect_command_calls(block->items, out);
}

void collect_local_command_calls(const ItemContainer& items, std::vector<const CommandCall*>& out) {
    for (const auto& call : items.calls) out.push_back(&call);
    for (const auto& object : items.consts) {
        for (const auto& call : object.calls) out.push_back(&call);
    }
}

const Directive* find_directive(const ItemContainer& items, const std::string& name) {
    for (const auto& directive : items.directives) {
        if (directive.name == name) return &directive;
    }
    return nullptr;
}

std::string first_directive_arg(const ItemContainer& items, const std::string& name) {
    const Directive* directive = find_directive(items, name);
    if (!directive || directive->args.empty()) return "";
    return directive->args.front().text;
}

TextSpan first_directive_arg_span(const ItemContainer& items, const std::string& name) {
    const Directive* directive = find_directive(items, name);
    if (!directive || directive->args.empty()) return {};
    return directive->args.front().span;
}

std::string attr_arg_value(const Attribute& attr, const std::string& name) {
    for (const auto& arg : attr.args) {
        if (arg.name == name) return arg.value.text;
    }
    return "";
}

std::string endpoint_owner(const std::string& endpoint) {
    const size_t dot = endpoint.find('.');
    return dot == std::string::npos ? endpoint : endpoint.substr(0, dot);
}

bool known_endpoint_owner(const std::string& owner,
                          const std::unordered_set<std::string>& node_aliases,
                          const std::unordered_set<std::string>& parameter_names,
                          bool allow_node_aliases) {
    return owner == "context" || parameter_names.find(owner) != parameter_names.end() ||
           (allow_node_aliases && node_aliases.find(owner) != node_aliases.end());
}

std::string graph_parameter_direction(const std::vector<Attribute>& attributes) {
    for (const auto& attr : attributes) {
        if (attr.name == "graph.input") return "in";
        if (attr.name == "graph.output") return "out";
        if (attr.name == "graph.var") return "var";
    }
    return "var";
}

size_t graph_parameter_direction_attribute_count(const std::vector<Attribute>& attributes) {
    size_t count = 0;
    for (const auto& attr : attributes) {
        if (attr.name == "graph.input" || attr.name == "graph.output" || attr.name == "graph.var") ++count;
    }
    return count;
}

void project_command_call(const CommandCall& call,
                          const std::unordered_set<std::string>& aliases,
                          const std::unordered_set<std::string>& parameters,
                          bool allow_node_aliases,
                          std::vector<FlowEdge>& edges,
                          std::vector<FlowDataEdge>& data_edges,
                          std::vector<Diagnostic>& diagnostics) {
    if (call.callee_parts.size() == 1 && call.callee_parts.front() == "connect" && call.args.size() >= 2) {
        FlowEdge edge;
        edge.from = call.args[0].text;
        edge.to = call.args[1].text;
        edge.attributes = call.attributes;
        edge.from_span = call.args[0].span;
        edge.to_span = call.args[1].span;
        edge.span = call.span;
        const std::string from_alias = endpoint_owner(edge.from);
        const std::string to_alias = endpoint_owner(edge.to);
        if (!known_endpoint_owner(from_alias, aliases, parameters, allow_node_aliases)) {
            edge.valid = false;
            diagnostics.push_back(make_diag(Severity::Error, "GS-FLW-002", "Unknown source node in connection", call.span.range, from_alias));
        }
        if (!known_endpoint_owner(to_alias, aliases, parameters, allow_node_aliases)) {
            edge.valid = false;
            diagnostics.push_back(make_diag(Severity::Error, "GS-FLW-003", "Unknown target node in connection", call.span.range, to_alias));
        }
        edges.push_back(std::move(edge));
        return;
    }

    if (call.callee_parts.size() == 1 && call.callee_parts.front() == "bind" && call.args.size() >= 2) {
        FlowDataEdge edge;
        edge.source = call.args[0].text;
        edge.target = call.args[1].text;
        edge.attributes = call.attributes;
        edge.source_span = call.args[0].span;
        edge.target_span = call.args[1].span;
        edge.span = call.span;
        const std::string source_owner = endpoint_owner(edge.source);
        const std::string target_owner = endpoint_owner(edge.target);
        if (!known_endpoint_owner(source_owner, aliases, parameters, allow_node_aliases)) {
            edge.valid = false;
            diagnostics.push_back(make_diag(Severity::Error, "GS-FLW-004", "Unknown source in data link", call.span.range, source_owner));
        }
        if (!known_endpoint_owner(target_owner, aliases, parameters, allow_node_aliases)) {
            edge.valid = false;
            diagnostics.push_back(make_diag(Severity::Error, "GS-FLW-005", "Unknown target in data link", call.span.range, target_owner));
        }
        data_edges.push_back(std::move(edge));
        return;
    }

    diagnostics.push_back(make_diag(Severity::Warning, "GS-FLW-001", "Unsupported command call in graph", call.span.range));
}

} // namespace

Parser::Parser(std::string source, std::string source_name)
    : source_(std::move(source)), source_name_(std::move(source_name)) {}

ParseResult Parser::parse() {
    ParseResult result;
    result.module.source_name = source_name_;

    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_graphscript_asset());
    TSTree* tree = ts_parser_parse_string(parser, nullptr, source_.c_str(), static_cast<uint32_t>(source_.size()));
    TSNode root = ts_tree_root_node(tree);

    collect_tree_errors(root, result.diagnostics);

    for (TSNode child : named_children(root)) {
        if (is_type(child, "import_declaration")) {
            result.module.imports.push_back(parse_import(source_, child));
        } else if (is_type(child, "export_declaration")) {
            parse_export_into(source_, child, result.module);
        } else if (is_type(child, "declaration")) {
            parse_declaration_into(source_, child, false, result.module);
        } else {
            parse_item_into(source_, child, result.module.items);
        }
    }

    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return result;
}

std::vector<Diagnostic> Linter::lint(const Module& module, ModuleGraph* graph) {
    std::vector<Diagnostic> diagnostics;
    if (graph) {
        graph->module_id = module.source_name;
        graph->module_id_inferred = true;
        graph->imports.clear();
        graph->exports.clear();
        for (const auto& import : module.imports) graph->imports.push_back(import.path);
        if (!module.modules.empty()) {
            graph->module_id = module.modules.front().id;
            graph->module_id_inferred = false;
        }
        for (const auto& symbol : module.symbols) {
            if (symbol.exported) graph->exports.push_back({symbol.kind, symbol.name, symbol.span});
        }
    }

    if (module.modules.size() > 1) {
        diagnostics.push_back(make_diag(Severity::Error, "GS-REF-007", "Only one declare module is allowed", module.modules[1].span.range));
    }

    std::unordered_set<std::string> declared;
    for (const auto& symbol : module.symbols) {
        const std::string key = symbol.kind + ":" + symbol.name;
        if (!declared.insert(key).second) {
            diagnostics.push_back(make_diag(Severity::Error, "GS-LINT-001", "Duplicate declaration", symbol.span.range, symbol.name));
        }
    }

    for (const auto& object : module.objects) {
        std::unordered_set<std::string> field_names;
        for (const auto& field : object.fields) {
            const std::string role = field_is_flow_pin(field) ? "pin:" : "field:";
            if (!field_names.insert(role + field.name).second) {
                diagnostics.push_back(make_diag(Severity::Error, "GS-LINT-002", "Duplicate field declaration", field.span.range, field.name));
            }
        }
    }

    std::unordered_set<std::string> aliases;
    bool duplicate_alias = false;
    collect_node_aliases(module.items, aliases, duplicate_alias);
    if (duplicate_alias) {
        diagnostics.push_back(make_diag(Severity::Error, "GS-LINT-003", "Duplicate node alias", {}));
    }
    return diagnostics;
}

Result<std::string, std::string> Patcher::apply(const std::string& source, const TextPatch& patch) {
    std::string result = source;
    auto edits = patch.edits;
    std::sort(edits.begin(), edits.end(), [](const TextEdit& a, const TextEdit& b) { return a.offset > b.offset; });
    for (const auto& edit : edits) {
        if (edit.offset > result.size() || edit.offset + edit.length > result.size()) {
            return Result<std::string, std::string>::err("Patch edit is outside source range");
        }
        result.replace(edit.offset, edit.length, edit.replacement);
    }
    return Result<std::string, std::string>::ok(result);
}

TextPatch Patcher::add_import(const std::string& source, const std::string& path) {
    TextPatch patch;
    size_t insert = 0;
    while (insert < source.size()) {
        const size_t line_end = source.find('\n', insert);
        const size_t end = line_end == std::string::npos ? source.size() : line_end + 1;
        const std::string line = source.substr(insert, end - insert);
        if (line.find("import ") != 0) break;
        insert = end;
    }
    patch.edits.push_back({insert, 0, "import \"" + path + "\";\n", {}});
    return patch;
}

Result<TextPatch, std::string> Patcher::set_property(const std::string&,
                                                     const Module& module,
                                                     const std::string& object_alias,
                                                     const std::string& property_path,
                                                     const std::string& value) {
    if (const auto* object = find_const_object(module.items, object_alias)) {
        for (const auto& property : object->properties) {
            if (property.path != property_path) continue;
            TextPatch patch;
            patch.edits.push_back({property.value_span.offset, property.value_span.length, value, property.value_span.range});
            return Result<TextPatch, std::string>::ok(std::move(patch));
        }
    }
    if (const auto* node = find_block_by_kind_and_name_in(module.items, "node", object_alias)) {
        for (const auto& property : node->items.properties) {
            if (property.path != property_path) continue;
            TextPatch patch;
            patch.edits.push_back({property.value_span.offset, property.value_span.length, value, property.value_span.range});
            return Result<TextPatch, std::string>::ok(std::move(patch));
        }
    }
    return Result<TextPatch, std::string>::err("Property not found");
}

Result<TextPatch, std::string> Patcher::add_node(const std::string&,
                                                 const Module& module,
                                                 const std::string& graph_name,
                                                 const std::string& alias,
                                                 const std::string& type,
                                                 const std::string& body) {
    const Block* graph = find_graph_block_in(module.items, graph_name);
    if (!graph) return Result<TextPatch, std::string>::err("Graph block not found");
    const std::string property_body = body.empty() ? "" : "\n        " + body + "\n";
    TextPatch patch;
    patch.edits.push_back({graph->body_end_offset, 0, "    node " + alias + " {\n        type " + type + ";" + property_body + "    }\n", graph->span.range});
    return Result<TextPatch, std::string>::ok(std::move(patch));
}

Result<TextPatch, std::string> Patcher::add_block(const std::string& source,
                                                  const Module& module,
                                                  const std::string& parent_block_name,
                                                  const std::string& kind,
                                                  const std::string& name,
                                                  const std::string& type) {
    if (!is_identifier_text(kind)) return Result<TextPatch, std::string>::err("Block kind must be an identifier");
    if (!is_identifier_text(name)) return Result<TextPatch, std::string>::err("Block name must be an identifier");

    TextPatch patch;
    const std::string type_line = type.empty() ? "" : "        type " + type + ";\n";
    if (parent_block_name.empty()) {
        const bool needs_newline = !source.empty() && source.back() != '\n';
        patch.edits.push_back({source.size(), 0, std::string(needs_newline ? "\n" : "") + kind + " " + name + " {\n" + type_line + "}\n", {}});
        return Result<TextPatch, std::string>::ok(std::move(patch));
    }

    const Block* parent = find_block_by_name_in(module.items, parent_block_name);
    if (!parent) return Result<TextPatch, std::string>::err("Parent block not found");
    patch.edits.push_back({parent->body_end_offset, 0, "    " + kind + " " + name + " {\n" + type_line + "    }\n", parent->span.range});
    return Result<TextPatch, std::string>::ok(std::move(patch));
}

Result<TextPatch, std::string> Patcher::add_attribute(const std::string& source,
                                                      const Module& module,
                                                      const std::string& target_name,
                                                      const std::string& attribute_source) {
    if (attribute_source.empty()) return Result<TextPatch, std::string>::err("Attribute source is required");

    TextSpan target;
    if (const auto* object = find_const_object(module.items, target_name)) {
        target = object->span;
    } else if (const auto* block = find_block_by_name_in(module.items, target_name)) {
        target = block->span;
    } else {
        return Result<TextPatch, std::string>::err("Attribute target not found");
    }

    size_t line_start = source.rfind('\n', target.offset);
    line_start = line_start == std::string::npos ? 0 : line_start + 1;
    size_t indent_end = line_start;
    while (indent_end < source.size() && (source[indent_end] == ' ' || source[indent_end] == '\t')) ++indent_end;
    std::string attr = attribute_source.front() == '@' ? attribute_source : "@" + attribute_source;
    if (attr.back() != '\n') attr.push_back('\n');
    const std::string indent = source.substr(line_start, indent_end - line_start);

    TextPatch patch;
    patch.edits.push_back({line_start, 0, indent + attr, target.range});
    return Result<TextPatch, std::string>::ok(std::move(patch));
}

Result<TextPatch, std::string> Patcher::connect(const std::string&,
                                                const Module& module,
                                                const std::string& graph_name,
                                                const std::string& from,
                                                const std::string& to) {
    const Block* graph = find_graph_block_in(module.items, graph_name);
    if (!graph) return Result<TextPatch, std::string>::err("Graph block not found");
    TextPatch patch;
    patch.edits.push_back({graph->body_end_offset, 0, "    connect(" + from + ", " + to + ");\n", graph->span.range});
    return Result<TextPatch, std::string>::ok(std::move(patch));
}

Result<TextPatch, std::string> Patcher::disconnect(const std::string& source,
                                                   const Module& module,
                                                   const std::string& graph_name,
                                                   const std::string& from,
                                                   const std::string& to) {
    const Block* graph = find_graph_block_in(module.items, graph_name);
    if (!graph) return Result<TextPatch, std::string>::err("Graph block not found");
    TextPatch patch;
    std::vector<const CommandCall*> calls;
    collect_command_calls(graph->items, calls);
    for (const auto* call : calls) {
        std::string call_from;
        std::string call_to;
        if (call->callee_parts.size() == 1 && call->callee_parts.front() == "connect" && call->args.size() >= 2) {
            call_from = call->args[0].text;
            call_to = call->args[1].text;
        } else {
            continue;
        }
        if (call_from != from || call_to != to) continue;
        size_t offset = call->span.offset;
        size_t length = call->span.length;
        const size_t line_start = source.rfind('\n', call->span.offset);
        offset = line_start == std::string::npos ? 0 : line_start + 1;
        const size_t line_end = source.find('\n', call->span.offset);
        length = line_end == std::string::npos ? source.size() - offset : line_end + 1 - offset;
        patch.edits.push_back({offset, length, "", call->span.range});
    }
    if (patch.edits.empty()) return Result<TextPatch, std::string>::err("Connection not found");
    return Result<TextPatch, std::string>::ok(std::move(patch));
}

Result<TextPatch, std::string> Patcher::rename_node(const std::string& source,
                                                    const Module& module,
                                                    const std::string& graph_name,
                                                    const std::string& old_alias,
                                                    const std::string& new_alias) {
    if (!is_identifier_text(new_alias)) return Result<TextPatch, std::string>::err("New alias must be an identifier");
    const Block* graph = find_graph_block_in(module.items, graph_name);
    if (!graph) return Result<TextPatch, std::string>::err("Graph block not found");
    bool found_old = false;
    for (const auto& object : graph->items.consts) {
        if (object.alias == old_alias) found_old = true;
        if (object.alias == new_alias) return Result<TextPatch, std::string>::err("Node alias already exists");
    }
    for (const auto& block : graph->items.blocks) {
        if (block->kind != "node") continue;
        if (block->name == old_alias) found_old = true;
        if (block->name == new_alias) return Result<TextPatch, std::string>::err("Node alias already exists");
    }
    if (!found_old) return Result<TextPatch, std::string>::err("Node alias not found");

    TextPatch patch;
    size_t offset = graph->body_start_offset;
    while (offset < graph->body_end_offset && offset < source.size()) {
        const size_t found = source.find(old_alias, offset);
        if (found == std::string::npos || found >= graph->body_end_offset) break;
        if (identifier_boundary(source, found, old_alias.size())) {
            patch.edits.push_back({found, old_alias.size(), new_alias, {}});
        }
        offset = found + old_alias.size();
    }
    if (patch.edits.empty()) return Result<TextPatch, std::string>::err("Node alias references not found");
    return Result<TextPatch, std::string>::ok(std::move(patch));
}

Result<TextPatch, std::string> Patcher::rename_block(const std::string&,
                                                     const Module& module,
                                                     const std::string& old_name,
                                                     const std::string& new_name) {
    if (!is_identifier_text(new_name)) return Result<TextPatch, std::string>::err("New block name must be an identifier");
    const Block* block = find_block_by_name_in(module.items, old_name);
    if (!block) return Result<TextPatch, std::string>::err("Block not found");
    if (find_block_by_name_in(module.items, new_name)) return Result<TextPatch, std::string>::err("Block name already exists");

    TextPatch patch;
    patch.edits.push_back({block->name_span.offset, block->name_span.length, new_name, block->name_span.range});
    return Result<TextPatch, std::string>::ok(std::move(patch));
}

Result<FlowGraph, std::string> FlowGraphProjector::project(const Module& module, const std::string& graph_name) {
    const Block* graph_block = find_graph_block_in(module.items, graph_name);
    if (!graph_block) return Result<FlowGraph, std::string>::err("Graph block not found");

    std::unordered_map<std::string, std::vector<PinDefinition>> object_pins;
    for (const auto& object : module.objects) {
        for (const auto& field : object.fields) {
            PinDefinition pin;
            pin.name = field.name;
            pin.type = field.type;
            pin.span = field.span;
            for (const auto& attr : field.attributes) {
                if (attr.name == "flow.pin") {
                    pin.kind = attr_arg_value(attr, "kind");
                    pin.direction = attr_arg_value(attr, "direction");
                } else if (attr.name == "flow.input") {
                    pin.kind = "data";
                    pin.direction = "in";
                } else if (attr.name == "flow.output") {
                    pin.kind = "data";
                    pin.direction = "out";
                }
            }
            if (!pin.kind.empty()) object_pins[object.name].push_back(std::move(pin));
        }
    }

    FlowGraph graph;
    graph.name = graph_block->name;
    graph.schema = graph_block->type;
    graph.span = graph_block->span;
    graph.name_span = graph_block->name_span;
    graph.schema_span = graph_block->type_span;
    graph.attributes = graph_block->attributes;
    if (graph.schema.empty()) {
        graph.schema = first_directive_arg(graph_block->items, "schema");
        graph.schema_span = first_directive_arg_span(graph_block->items, "schema");
    }

    std::unordered_set<std::string> parameter_names;
    for (const auto& directive : graph_block->items.directives) {
        if (directive.name != "param" || directive.parameters.empty()) continue;
        if (graph_parameter_direction_attribute_count(directive.attributes) > 1) {
            graph.diagnostics.push_back(make_diag(Severity::Error, "GS-FLW-006", "Conflicting graph parameter direction attributes", directive.span.range));
        }
        const auto& param = directive.parameters.front();
        GraphParameter projected_param;
        projected_param.name = param.name;
        projected_param.type = param.type;
        projected_param.direction = graph_parameter_direction(directive.attributes);
        projected_param.default_value = param.default_value;
        projected_param.has_default = param.has_default;
        projected_param.attributes = directive.attributes;
        projected_param.span = directive.span;
        projected_param.name_span = param.name_span;
        projected_param.type_span = param.type_span;
        parameter_names.insert(projected_param.name);
        graph.parameters.push_back(std::move(projected_param));
    }

    std::unordered_set<std::string> aliases;
    std::vector<const ConstObject*> objects;
    collect_const_objects(graph_block->items, objects);
    for (const auto* object : objects) {
        FlowNode node;
        node.alias = object->alias;
        node.type = object->type;
        node.properties = object->properties;
        node.attributes = object->attributes;
        node.span = object->span;
        node.alias_span = object->alias_span;
        node.type_span = object->type_span;
        if (object_pins.count(object->type)) node.pins = object_pins[object->type];
        aliases.insert(node.alias);
        graph.nodes.push_back(std::move(node));
    }
    std::vector<const Block*> node_blocks;
    collect_node_blocks(graph_block->items, node_blocks);
    for (const auto* block : node_blocks) {
        FlowNode node;
        node.alias = block->name;
        node.type = first_directive_arg(block->items, "type");
        node.properties = block->items.properties;
        node.attributes = block->attributes;
        node.span = block->span;
        node.alias_span = block->name_span;
        node.type_span = first_directive_arg_span(block->items, "type");
        if (object_pins.count(node.type)) node.pins = object_pins[node.type];
        aliases.insert(node.alias);
        graph.nodes.push_back(std::move(node));
    }

    std::vector<const CommandCall*> calls;
    collect_local_command_calls(graph_block->items, calls);
    for (const auto* call : calls) {
        project_command_call(*call, aliases, parameter_names, true, graph.edges, graph.data_edges, graph.diagnostics);
    }

    std::vector<const Block*> flow_blocks;
    collect_flow_blocks(graph_block->items, flow_blocks);
    for (const auto* block : flow_blocks) {
        FlowBlock projected_block;
        projected_block.kind = block->kind;
        projected_block.name = block->name;
        projected_block.attributes = block->attributes;
        projected_block.span = block->span;
        projected_block.name_span = block->name_span;
        std::vector<const CommandCall*> block_calls;
        collect_local_command_calls(block->items, block_calls);
        for (const auto* call : block_calls) {
            const bool allow_node_aliases = block->kind != "function";
            const size_t before_edges = projected_block.edges.size();
            const size_t before_data_edges = projected_block.data_edges.size();
            project_command_call(*call, aliases, parameter_names, allow_node_aliases, projected_block.edges, projected_block.data_edges, graph.diagnostics);
            for (size_t i = before_edges; i < projected_block.edges.size(); ++i) graph.edges.push_back(projected_block.edges[i]);
            for (size_t i = before_data_edges; i < projected_block.data_edges.size(); ++i) graph.data_edges.push_back(projected_block.data_edges[i]);
        }
        graph.blocks.push_back(std::move(projected_block));
    }

    std::vector<const Block*> generate_blocks;
    collect_generate_blocks(graph_block->items, generate_blocks);
    if (!generate_blocks.empty()) {
        FlowGenerateBlock projected_generate;
        projected_generate.span = generate_blocks.front()->span;
        std::vector<const CommandCall*> generate_calls;
        collect_local_command_calls(generate_blocks.front()->items, generate_calls);
        for (const auto* call : generate_calls) {
            if (call->callee_parts.size() == 1 && call->callee_parts.front() == "comment" && call->args.size() >= 2) {
                FlowGenerateComment comment;
                comment.instance = call->args[0].text;
                comment.text = call->args[1].text;
                comment.attributes = call->attributes;
                comment.span = call->span;
                comment.instance_span = call->args[0].span;
                comment.text_span = call->args[1].span;
                projected_generate.comments.push_back(std::move(comment));
                continue;
            }
            if (call->callee_parts.size() == 1 && call->callee_parts.front() == "metadata" && call->args.size() >= 4) {
                FlowGenerateMetadata metadata;
                metadata.scope = call->args[0].text;
                metadata.node = call->args[1].text;
                metadata.property = call->args[2].text;
                metadata.value = call->args[3];
                metadata.attributes = call->attributes;
                metadata.span = call->span;
                metadata.scope_span = call->args[0].span;
                metadata.node_span = call->args[1].span;
                metadata.property_span = call->args[2].span;
                metadata.value_span = call->args[3].span;
                projected_generate.metadata.push_back(std::move(metadata));
                continue;
            }
            graph.diagnostics.push_back(make_diag(Severity::Warning, "GS-FLW-007", "Unsupported generate command", call->span.range));
        }
        graph.generate = std::move(projected_generate);
    }

    return Result<FlowGraph, std::string>::ok(std::move(graph));
}

} // namespace gs::asset
