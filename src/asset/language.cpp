#include "graphscript/asset/language.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <unordered_set>

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

std::string join_parts(const std::vector<std::string>& parts, size_t begin, size_t end) {
    std::string out;
    for (size_t i = begin; i < end && i < parts.size(); ++i) {
        if (!out.empty()) out += ".";
        out += parts[i];
    }
    return out;
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
    object.type = slice(source, child_by_field(value, "type"));
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

std::unique_ptr<Scope> parse_scope(const std::string& source, TSNode node) {
    auto scope = std::make_unique<Scope>();
    scope->span = span_of(node);
    scope->attributes = parse_attributes(source, node);
    scope->kind = slice(source, child_by_field(node, "kind"));
    TSNode name = child_by_field(node, "name");
    scope->name = slice(source, name);
    scope->name_span = span_of(name);
    scope->type = slice(source, child_by_field(node, "type"));
    scope->parameters = parse_parameters(source, child_by_field(node, "parameters"));
    TSNode body = child_by_field(node, "body");
    scope->body_start_offset = null_node(body) ? scope->span.offset : ts_node_start_byte(body);
    scope->body_end_offset = null_node(body) ? scope->span.offset + scope->span.length : (ts_node_end_byte(body) > 0 ? ts_node_end_byte(body) - 1 : ts_node_end_byte(body));
    for (TSNode child : named_children(body)) {
        parse_item_into(source, child, scope->items);
    }
    return scope;
}

ObjectDecl parse_object_decl(const std::string& source, TSNode node, bool exported) {
    ObjectDecl object;
    object.span = span_of(node);
    object.exported = exported;
    object.attributes = parse_attributes(source, node);
    object.name = slice(source, child_by_field(node, "name"));
    object.base_type = slice(source, child_by_field(node, "type"));
    TSNode body = child_by_field(node, "body");
    const auto children = null_node(body) ? named_children(node) : named_children(body);
    for (TSNode child : children) {
        if (!is_type(child, "field_declaration")) continue;
        FieldDecl field;
        field.span = span_of(child);
        field.attributes = parse_attributes(source, child);
        TSNode name = child_by_field(child, "name");
        field.name = slice(source, name);
        field.name_span = span_of(name);
        field.type = slice(source, child_by_field(child, "type"));
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

ScopeKindDecl parse_scope_kind_decl(const std::string& source, TSNode node, bool exported) {
    ScopeKindDecl decl;
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

SchemaDecl parse_schema_decl(const std::string& source, TSNode node, bool exported) {
    SchemaDecl decl;
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

LintDecl parse_lint_decl(const std::string& source, TSNode node, bool exported) {
    LintDecl decl;
    decl.span = span_of(node);
    decl.exported = exported;
    decl.name = slice(source, child_by_field(node, "name"));
    decl.target_type = slice(source, child_by_field(node, "type"));
    return decl;
}

SymbolDecl make_symbol(const std::string& kind, const std::string& name, bool exported, TextSpan span) {
    SymbolDecl symbol;
    symbol.kind = kind;
    symbol.name = name;
    symbol.exported = exported;
    symbol.span = span;
    return symbol;
}

void parse_declaration_into(const std::string& source, TSNode node, bool exported, Module& module) {
    if (is_type(node, "declaration")) {
        for (TSNode child : named_children(node)) {
            parse_declaration_into(source, child, exported, module);
        }
        return;
    }
    const std::string type = node_type(node);
    if (type == "module_declaration") {
        module.modules.push_back(parse_module_decl(source, node));
    } else if (type == "type_declaration") {
        module.symbols.push_back(make_symbol("type", slice(source, child_by_field(node, "name")), exported, span_of(node)));
    } else if (type == "enum_declaration") {
        auto decl = parse_enum_decl(source, node, exported);
        module.symbols.push_back(make_symbol("enum", decl.name, exported, decl.span));
        module.enums.push_back(std::move(decl));
    } else if (type == "object_declaration") {
        auto decl = parse_object_decl(source, node, exported);
        module.symbols.push_back(make_symbol("object", decl.name, exported, decl.span));
        module.objects.push_back(std::move(decl));
    } else if (type == "scope_kind_declaration") {
        auto decl = parse_scope_kind_decl(source, node, exported);
        module.symbols.push_back(make_symbol("scope", decl.name, exported, decl.span));
        module.scope_kinds.push_back(std::move(decl));
    } else if (type == "command_declaration") {
        auto decl = parse_command_decl(source, node, exported);
        module.symbols.push_back(make_symbol("command", decl.name, exported, decl.span));
        module.commands.push_back(std::move(decl));
    } else if (type == "schema_declaration") {
        auto decl = parse_schema_decl(source, node, exported);
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
        parse_declaration_into(source, decl, true, module);
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
    TSNode path = child_by_field(node, "path");
    import.path = unquote(slice(source, path));
    import.path_span = span_of(path);
    return import;
}

void parse_item_into(const std::string& source, TSNode node, ItemContainer& items) {
    if (is_type(node, "scope_declaration")) items.scopes.push_back(parse_scope(source, node));
    else if (is_type(node, "const_declaration")) items.consts.push_back(parse_const(source, node));
    else if (is_type(node, "property_declaration")) items.properties.push_back(parse_property(source, node));
    else if (is_type(node, "call_statement")) items.calls.push_back(parse_call(source, node));
    else if (is_type(node, "assignment_statement")) items.assignments.push_back(parse_assignment(source, node));
    else if (is_type(node, "directive_statement")) items.directives.push_back(parse_directive(source, node));
}

const Scope* find_graph_scope_in(const ItemContainer& items, const std::string& graph_name) {
    for (const auto& scope : items.scopes) {
        if (scope->kind == "graph" && (graph_name.empty() || scope->name == graph_name)) {
            return scope.get();
        }
        if (const auto* nested = find_graph_scope_in(scope->items, graph_name)) return nested;
    }
    return nullptr;
}

const Scope* find_scope_by_name_in(const ItemContainer& items, const std::string& name) {
    for (const auto& scope : items.scopes) {
        if (scope->name == name) return scope.get();
        if (const auto* nested = find_scope_by_name_in(scope->items, name)) return nested;
    }
    return nullptr;
}

const ConstObject* find_const_object(const ItemContainer& items, const std::string& alias) {
    for (const auto& object : items.consts) {
        if (object.alias == alias) return &object;
    }
    for (const auto& scope : items.scopes) {
        if (const auto* found = find_const_object(scope->items, alias)) return found;
    }
    return nullptr;
}

void collect_const_aliases(const ItemContainer& items, std::unordered_set<std::string>& aliases, bool& duplicate) {
    for (const auto& object : items.consts) {
        if (!aliases.insert(object.alias).second) duplicate = true;
    }
    for (const auto& scope : items.scopes) collect_const_aliases(scope->items, aliases, duplicate);
}

void collect_const_objects(const ItemContainer& items, std::vector<const ConstObject*>& out) {
    for (const auto& object : items.consts) out.push_back(&object);
    for (const auto& scope : items.scopes) collect_const_objects(scope->items, out);
}

void collect_command_calls(const ItemContainer& items, std::vector<const CommandCall*>& out) {
    for (const auto& call : items.calls) out.push_back(&call);
    for (const auto& object : items.consts) {
        for (const auto& call : object.calls) out.push_back(&call);
    }
    for (const auto& scope : items.scopes) collect_command_calls(scope->items, out);
}

std::string attr_arg_value(const Attribute& attr, const std::string& name) {
    for (const auto& arg : attr.args) {
        if (arg.name == name) return arg.value.text;
    }
    return "";
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
            if (!field_names.insert(field.name).second) {
                diagnostics.push_back(make_diag(Severity::Error, "GS-LINT-002", "Duplicate field declaration", field.span.range, field.name));
            }
        }
    }

    std::unordered_set<std::string> aliases;
    bool duplicate_alias = false;
    collect_const_aliases(module.items, aliases, duplicate_alias);
    if (duplicate_alias) {
        diagnostics.push_back(make_diag(Severity::Error, "GS-LINT-003", "Duplicate const alias", {}));
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
    return Result<TextPatch, std::string>::err("Property not found");
}

Result<TextPatch, std::string> Patcher::add_node(const std::string&,
                                                 const Module& module,
                                                 const std::string& graph_name,
                                                 const std::string& alias,
                                                 const std::string& type,
                                                 const std::string& body) {
    const Scope* graph = find_graph_scope_in(module.items, graph_name);
    if (!graph) return Result<TextPatch, std::string>::err("Graph scope not found");
    const std::string object_body = body.empty() ? "" : "\n        " + body + "\n    ";
    TextPatch patch;
    patch.edits.push_back({graph->body_end_offset, 0, "    const " + alias + " = new " + type + " {" + object_body + "}\n", graph->span.range});
    return Result<TextPatch, std::string>::ok(std::move(patch));
}

Result<TextPatch, std::string> Patcher::add_scope(const std::string& source,
                                                  const Module& module,
                                                  const std::string& parent_scope_name,
                                                  const std::string& kind,
                                                  const std::string& name,
                                                  const std::string& type) {
    if (!is_identifier_text(kind)) return Result<TextPatch, std::string>::err("Scope kind must be an identifier");
    if (!is_identifier_text(name)) return Result<TextPatch, std::string>::err("Scope name must be an identifier");

    TextPatch patch;
    const std::string type_suffix = type.empty() ? "" : ": " + type;
    if (parent_scope_name.empty()) {
        const bool needs_newline = !source.empty() && source.back() != '\n';
        patch.edits.push_back({source.size(), 0, std::string(needs_newline ? "\n" : "") + "scope " + kind + " " + name + type_suffix + " {\n}\n", {}});
        return Result<TextPatch, std::string>::ok(std::move(patch));
    }

    const Scope* parent = find_scope_by_name_in(module.items, parent_scope_name);
    if (!parent) return Result<TextPatch, std::string>::err("Parent scope not found");
    patch.edits.push_back({parent->body_end_offset, 0, "    scope " + kind + " " + name + type_suffix + " {\n    }\n", parent->span.range});
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
    } else if (const auto* scope = find_scope_by_name_in(module.items, target_name)) {
        target = scope->span;
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
    const Scope* graph = find_graph_scope_in(module.items, graph_name);
    if (!graph) return Result<TextPatch, std::string>::err("Graph scope not found");
    TextPatch patch;
    patch.edits.push_back({graph->body_end_offset, 0, "    " + from + ".connect(" + to + ");\n", graph->span.range});
    return Result<TextPatch, std::string>::ok(std::move(patch));
}

Result<TextPatch, std::string> Patcher::disconnect(const std::string& source,
                                                   const Module& module,
                                                   const std::string& graph_name,
                                                   const std::string& from,
                                                   const std::string& to) {
    const Scope* graph = find_graph_scope_in(module.items, graph_name);
    if (!graph) return Result<TextPatch, std::string>::err("Graph scope not found");
    TextPatch patch;
    std::vector<const CommandCall*> calls;
    collect_command_calls(graph->items, calls);
    for (const auto* call : calls) {
        if (call->callee_parts.size() < 3 || call->callee_parts.back() != "connect" || call->args.empty()) continue;
        const std::string call_from = join_parts(call->callee_parts, 0, call->callee_parts.size() - 1);
        if (call_from != from || call->args.front().text != to) continue;
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
    const Scope* graph = find_graph_scope_in(module.items, graph_name);
    if (!graph) return Result<TextPatch, std::string>::err("Graph scope not found");
    bool found_old = false;
    for (const auto& object : graph->items.consts) {
        if (object.alias == old_alias) found_old = true;
        if (object.alias == new_alias) return Result<TextPatch, std::string>::err("Node alias already exists");
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

Result<TextPatch, std::string> Patcher::rename_scope(const std::string&,
                                                     const Module& module,
                                                     const std::string& old_name,
                                                     const std::string& new_name) {
    if (!is_identifier_text(new_name)) return Result<TextPatch, std::string>::err("New scope name must be an identifier");
    const Scope* scope = find_scope_by_name_in(module.items, old_name);
    if (!scope) return Result<TextPatch, std::string>::err("Scope not found");
    if (find_scope_by_name_in(module.items, new_name)) return Result<TextPatch, std::string>::err("Scope name already exists");

    TextPatch patch;
    patch.edits.push_back({scope->name_span.offset, scope->name_span.length, new_name, scope->name_span.range});
    return Result<TextPatch, std::string>::ok(std::move(patch));
}

Result<FlowGraph, std::string> FlowGraphProjector::project(const Module& module, const std::string& graph_name) {
    const Scope* graph_scope = find_graph_scope_in(module.items, graph_name);
    if (!graph_scope) return Result<FlowGraph, std::string>::err("Graph scope not found");

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
    graph.name = graph_scope->name;
    graph.schema = graph_scope->type;

    std::unordered_set<std::string> aliases;
    std::vector<const ConstObject*> objects;
    collect_const_objects(graph_scope->items, objects);
    for (const auto* object : objects) {
        FlowNode node;
        node.alias = object->alias;
        node.type = object->type;
        node.properties = object->properties;
        node.span = object->span;
        if (object_pins.count(object->type)) node.pins = object_pins[object->type];
        aliases.insert(node.alias);
        graph.nodes.push_back(std::move(node));
    }

    std::vector<const CommandCall*> calls;
    collect_command_calls(graph_scope->items, calls);
    for (const auto* call : calls) {
        if (call->callee_parts.size() < 3 || call->callee_parts.back() != "connect" || call->args.empty()) {
            graph.diagnostics.push_back(make_diag(Severity::Warning, "GS-FLW-001", "Unsupported command call in graph", call->span.range));
            continue;
        }
        FlowEdge edge;
        edge.from = join_parts(call->callee_parts, 0, call->callee_parts.size() - 1);
        edge.to = call->args.front().text;
        edge.span = call->span;
        const std::string from_alias = call->callee_parts.front();
        const std::string to_alias = edge.to.substr(0, edge.to.find('.'));
        if (from_alias != "context" && aliases.find(from_alias) == aliases.end()) {
            edge.valid = false;
            graph.diagnostics.push_back(make_diag(Severity::Error, "GS-FLW-002", "Unknown source node in connection", call->span.range, from_alias));
        }
        if (to_alias != "context" && aliases.find(to_alias) == aliases.end()) {
            edge.valid = false;
            graph.diagnostics.push_back(make_diag(Severity::Error, "GS-FLW-003", "Unknown target node in connection", call->span.range, to_alias));
        }
        graph.edges.push_back(std::move(edge));
    }

    return Result<FlowGraph, std::string>::ok(std::move(graph));
}

} // namespace gs::asset
