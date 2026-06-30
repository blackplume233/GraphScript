#include "graphscript/edit/edit_session.h"
#include "graphscript/asset/language.h"
#include "graphscript/compile/compiler.h"
#include "graphscript/emit/emitter.h"
#include "graphscript/parse/lexer.h"
#include "graphscript/parse/parser.h"
#include "graphscript/schema/schema_registry.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <iterator>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace gs {

// ─── JSON helpers (internal) ───────────────────────────────────────

static std::string json_escape(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n";  break;
            case '\r': r += "\\r";  break;
            case '\t': r += "\\t";  break;
            default:   r += c;
        }
    }
    return r;
}

static std::string jstr(const std::string& s) { return "\"" + json_escape(s) + "\""; }
static std::string jbool(bool b) { return b ? "true" : "false"; }
static std::string jint(int n) { return std::to_string(n); }
static std::string juint(uint32_t n) { return std::to_string(n); }

static std::filesystem::path module_base_dir(const Module& module) {
    if (module.file_path.empty()) return {};
    std::filesystem::path path(module.file_path);
    return path.has_parent_path() ? path.parent_path() : std::filesystem::path{};
}

static std::string normalized_import_path(const std::string& path, const std::filesystem::path& base_dir = {}) {
    std::error_code ec;
    std::filesystem::path candidate(path);
    if (!base_dir.empty() && candidate.is_relative()) {
        candidate = base_dir / candidate;
    }
    candidate = std::filesystem::absolute(candidate, ec);
    if (ec) candidate = std::filesystem::path(path);
    auto canonical = std::filesystem::weakly_canonical(candidate, ec);
    if (!ec) candidate = canonical;
    return candidate.lexically_normal().string();
}

static std::string import_key(const std::string& path, const std::filesystem::path& base_dir = {}) {
    std::filesystem::path normalized(normalized_import_path(path, base_dir));
    std::string key = normalized.generic_string();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
#endif
    while (key.size() > 1 && key.back() == '/') key.pop_back();
    return key;
}

static bool contains_key(const std::vector<std::string>& keys, const std::string& key) {
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

struct InitializerAssignmentText {
    std::string name;
    std::string value;
};

struct ConstructorCallText {
    std::string type_name;
    std::string argument;
};

static std::string trim_copy(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

static bool is_identifier_text(const std::string& text) {
    auto tokens = Lexer(text).tokenize();
    return tokens.size() == 2 &&
           tokens[0].type == TokenType::Identifier &&
           tokens[0].text == text &&
           tokens[1].type == TokenType::EndOfFile;
}

static std::vector<std::string> split_top_level_commas(const std::string& text) {
    std::vector<std::string> parts;
    size_t start = 0;
    int nested = 0;
    bool in_string = false;
    bool escaped = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '(' || c == '[' || c == '{') {
            ++nested;
        } else if ((c == ')' || c == ']' || c == '}') && nested > 0) {
            --nested;
        } else if (c == ',' && nested == 0) {
            parts.push_back(trim_copy(text.substr(start, i - start)));
            start = i + 1;
        }
    }
    parts.push_back(trim_copy(text.substr(start)));
    return parts;
}

static std::optional<size_t> find_top_level_assign(const std::string& text) {
    int nested = 0;
    bool in_string = false;
    bool escaped = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '(' || c == '[' || c == '{') {
            ++nested;
        } else if ((c == ')' || c == ']' || c == '}') && nested > 0) {
            --nested;
        } else if (c == '=' && nested == 0) {
            return i;
        }
    }
    return std::nullopt;
}

static Result<std::vector<InitializerAssignmentText>, std::string> parse_initializer_assignment_text(const std::string& initializer) {
    std::vector<InitializerAssignmentText> assignments;
    const std::string text = trim_copy(initializer);
    if (text.empty()) return Result<std::vector<InitializerAssignmentText>, std::string>::ok(assignments);

    for (const auto& part : split_top_level_commas(text)) {
        if (part.empty()) continue;
        const auto assign = find_top_level_assign(part);
        if (!assign) {
            return Result<std::vector<InitializerAssignmentText>, std::string>::err(
                "Node initializer is not a field assignment list");
        }
        InitializerAssignmentText item;
        item.name = trim_copy(part.substr(0, *assign));
        item.value = trim_copy(part.substr(*assign + 1));
        if (!is_identifier_text(item.name) || item.value.empty()) {
            return Result<std::vector<InitializerAssignmentText>, std::string>::err(
                "Node initializer contains an invalid field assignment");
        }
        assignments.push_back(std::move(item));
    }
    return Result<std::vector<InitializerAssignmentText>, std::string>::ok(assignments);
}

static std::string format_initializer_assignment_text(const std::vector<InitializerAssignmentText>& assignments) {
    std::string text;
    for (size_t i = 0; i < assignments.size(); ++i) {
        if (i > 0) text += ", ";
        text += assignments[i].name + " = " + assignments[i].value;
    }
    return text;
}

static std::optional<ConstructorCallText> parse_constructor_call_text(const std::string& value) {
    const auto text = trim_copy(value);
    const auto open = text.find('(');
    if (open == std::string::npos) return std::nullopt;

    ConstructorCallText call;
    call.type_name = trim_copy(text.substr(0, open));
    if (!is_identifier_text(call.type_name)) return std::nullopt;

    int nested = 0;
    bool in_string = false;
    bool escaped = false;
    for (size_t i = open; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '(') {
            ++nested;
        } else if (c == ')') {
            --nested;
            if (nested == 0) {
                if (i != text.size() - 1) return std::nullopt;
                call.argument = trim_copy(text.substr(open + 1, i - open - 1));
                return call;
            }
            if (nested < 0) return std::nullopt;
        }
    }

    return std::nullopt;
}

static std::vector<InitializerField> initializer_fields_from_assignment_text(
    const std::vector<InitializerAssignmentText>& assignments) {
    std::vector<InitializerField> fields;
    fields.reserve(assignments.size());
    for (const auto& assignment : assignments) {
        InitializerField field;
        field.name = assignment.name;
        field.value = assignment.value;
        fields.push_back(std::move(field));
    }
    return fields;
}

// Joins elements with comma separator.
static std::string jarray(const std::vector<std::string>& elems) {
    std::string r = "[";
    for (size_t i = 0; i < elems.size(); i++) {
        if (i > 0) r += ",";
        r += elems[i];
    }
    r += "]";
    return r;
}

static std::string jobj(const std::vector<std::pair<std::string,std::string>>& kv) {
    std::string r = "{";
    for (size_t i = 0; i < kv.size(); i++) {
        if (i > 0) r += ",";
        r += jstr(kv[i].first) + ":" + kv[i].second;
    }
    r += "}";
    return r;
}

static std::string source_range_to_json(const SourceRange& range);
static std::string annotations_to_json(const std::string& owner_id, const std::vector<Annotation>& annotations);
static std::string persistent_id_from_annotations(const std::vector<Annotation>& annotations);

static std::string declaration_type_element_id(const std::string& name) {
    return "decl-type:" + name;
}

static std::string declaration_node_element_id(const std::string& type_name) {
    return "decl-node:" + type_name;
}

static std::string declaration_pin_element_id(const std::string& type_name, const std::string& pin_name) {
    return "decl-pin:" + type_name + "/" + pin_name;
}

static std::string declaration_node_field_element_id(const std::string& type_name, const std::string& field_name) {
    return "decl-node-field:" + type_name + "/" + field_name;
}

static std::string declaration_schema_element_id(const std::string& schema_name) {
    return "decl-schema:" + schema_name;
}

static std::string declaration_schema_field_element_id(const std::string& schema_name, const std::string& field_name) {
    return "decl-schema-field:" + schema_name + "/" + field_name;
}

static std::string pin_def_to_json(const std::string& type_name, const PinDefinition& p) {
    return jobj({
        {"id",        jstr(declaration_pin_element_id(type_name, p.name))},
        {"name",      jstr(p.name)},
        {"source_file", jstr(p.source_file)},
        {"source_range", source_range_to_json(p.source_range)},
        {"name_source_range", source_range_to_json(p.name_range)},
        {"kind",      jstr(p.kind == PinKind::Exec ? "exec" : "data")},
        {"direction", jstr(p.direction == PinDirection::Input ? "in" : "out")},
        {"type",      jstr(p.type_name)},
        {"type_source_range", source_range_to_json(p.type_name_range)},
        {"persistent_id", jstr(persistent_id_from_annotations(p.annotations))},
        {"annotations", annotations_to_json(declaration_pin_element_id(type_name, p.name), p.annotations)}
    });
}

static std::string node_field_def_to_json(const std::string& type_name, const NodeFieldDefinition& field) {
    return jobj({
        {"id",        jstr(declaration_node_field_element_id(type_name, field.name))},
        {"name",      jstr(field.name)},
        {"source_file", jstr(field.source_file)},
        {"source_range", source_range_to_json(field.source_range)},
        {"name_source_range", source_range_to_json(field.name_range)},
        {"type",      jstr(field.type_name)},
        {"type_source_range", source_range_to_json(field.type_name_range)},
        {"default",   jstr(field.default_value)},
        {"default_source_range", source_range_to_json(field.default_value_range)},
        {"default_constructor_source_range", source_range_to_json(field.default_constructor_range)},
        {"default_constructor_type_source_range", source_range_to_json(field.default_constructor_type_range)},
        {"default_constructor_arg_source_range", source_range_to_json(field.default_constructor_arg_range)},
        {"persistent_id", jstr(persistent_id_from_annotations(field.annotations))},
        {"annotations", annotations_to_json(declaration_node_field_element_id(type_name, field.name), field.annotations)}
    });
}

static std::string type_info_to_json(const TypeInfo& t) {
    return jobj({
        {"id",            jstr(declaration_type_element_id(t.name))},
        {"name",          jstr(t.name)},
        {"source_file",   jstr(t.source_file)},
        {"source_range",  source_range_to_json(t.source_range)},
        {"name_source_range", source_range_to_json(t.name_range)},
        {"constructible", jbool(t.constructible)},
        {"persistent_id", jstr(persistent_id_from_annotations(t.annotations))},
        {"annotations", annotations_to_json(declaration_type_element_id(t.name), t.annotations)}
    });
}

static std::string schema_field_to_json(const std::string& schema_name, const GraphSchemaField& field) {
    return jobj({
        {"id",           jstr(declaration_schema_field_element_id(schema_name, field.name))},
        {"name",         jstr(field.name)},
        {"value",        jstr(field.value)},
        {"source_file",  jstr(field.source_file)},
        {"source_range", source_range_to_json(field.source_range)},
        {"name_source_range", source_range_to_json(field.name_range)},
        {"value_source_range", source_range_to_json(field.value_range)},
        {"value_constructor_source_range", source_range_to_json(field.value_constructor_range)},
        {"value_constructor_type_source_range", source_range_to_json(field.value_constructor_type_range)},
        {"value_constructor_arg_source_range", source_range_to_json(field.value_constructor_arg_range)},
        {"persistent_id", jstr(persistent_id_from_annotations(field.annotations))},
        {"annotations", annotations_to_json(declaration_schema_field_element_id(schema_name, field.name), field.annotations)}
    });
}

static std::string append_occurrence_suffix(const std::string& base_id, std::unordered_map<std::string, size_t>& seen_ids) {
    size_t& seen = seen_ids[base_id];
    ++seen;
    if (seen == 1) return base_id;
    return base_id + "#" + std::to_string(seen);
}

static std::string annotation_element_id(const std::string& owner_id, const std::string& annotation_name) {
    return "annotation:" + owner_id + "/" + annotation_name;
}

static std::string annotation_arg_element_id(const std::string& annotation_id, const std::string& arg_key) {
    return "annotation-arg:" + annotation_id + "/" + arg_key;
}

static std::string annotation_arg_key(const AnnotationArg& arg, size_t index) {
    if (!arg.name.empty()) return arg.name;
    return "$" + std::to_string(index + 1);
}

static std::string annotation_arg_to_json(const std::string& annotation_id, const AnnotationArg& arg, const std::string& arg_key) {
    return jobj({
        {"name",  jstr(arg.name)},
        {"value", jstr(arg.value)},
        {"source_range", source_range_to_json(arg.source_range)},
        {"name_source_range", source_range_to_json(arg.name_range)},
        {"value_source_range", source_range_to_json(arg.value_range)},
        {"value_constructor_source_range", source_range_to_json(arg.value_constructor_range)},
        {"value_constructor_type_source_range", source_range_to_json(arg.value_constructor_type_range)},
        {"value_constructor_arg_source_range", source_range_to_json(arg.value_constructor_arg_range)},
        {"id", jstr(annotation_arg_element_id(annotation_id, arg_key))}
    });
}

static std::string annotation_to_json(const std::string& annotation_id, const Annotation& annot) {
    std::vector<std::string> args;
    std::unordered_map<std::string, size_t> seen_arg_ids;
    for (size_t i = 0; i < annot.args.size(); ++i) {
        const auto& arg = annot.args[i];
        const auto arg_key = annotation_arg_key(arg, i);
        const auto unique_arg_key = append_occurrence_suffix(arg_key, seen_arg_ids);
        args.push_back(annotation_arg_to_json(annotation_id, arg, unique_arg_key));
    }
    return jobj({
        {"name", jstr(annot.name)},
        {"source_range", source_range_to_json(annot.source_range)},
        {"name_source_range", source_range_to_json(annot.name_range)},
        {"id", jstr(annotation_id)},
        {"args", jarray(args)}
    });
}

static std::string annotations_to_json(const std::string& owner_id, const std::vector<Annotation>& annotations) {
    std::vector<std::string> elems;
    std::unordered_map<std::string, size_t> seen_annotation_ids;
    for (auto& annot : annotations) {
        const auto base_id = annotation_element_id(owner_id, annot.name);
        const auto annotation_id = append_occurrence_suffix(base_id, seen_annotation_ids);
        elems.push_back(annotation_to_json(annotation_id, annot));
    }
    return jarray(elems);
}

static std::string persistent_id_from_annotations(const std::vector<Annotation>& annotations) {
    for (const auto& annot : annotations) {
        if (annot.name != "Id" && annot.name != "PersistentId") continue;
        for (const auto& arg : annot.args) {
            if (!arg.name.empty() && arg.name != "value" && arg.name != "Value") continue;
            return arg.value;
        }
    }
    return "";
}

static std::string source_location_to_json(const SourceLocation& loc) {
    return jobj({
        {"line",   juint(loc.line)},
        {"column", juint(loc.column)}
    });
}

static std::string source_range_to_json(const SourceRange& range) {
    return jobj({
        {"start", source_location_to_json(range.start)},
        {"end",   source_location_to_json(range.end)}
    });
}

static std::string diagnostic_target_to_json(const DiagnosticTarget& target) {
    return jobj({
        {"graph",           jstr(target.graph)},
        {"block_kind",      jstr(target.block_kind)},
        {"block_name",      jstr(target.block_name)},
        {"node_instance",   jstr(target.node_instance)},
        {"pin_name",        jstr(target.pin_name)},
        {"parameter_name",  jstr(target.parameter_name)},
        {"reference",       jstr(target.reference)},
        {"connection_kind", jstr(target.connection_kind)}
    });
}

static std::string diagnostic_id_base(const Diagnostic& diag) {
    const auto& target = diag.target;
    return "diagnostic:" + diag.code + "/" +
           (diag.severity == Severity::Error ? "error" : "warning") + "/" +
           diag.context + "/" +
           std::to_string(diag.range.start.line) + ":" + std::to_string(diag.range.start.column) + "-" +
           std::to_string(diag.range.end.line) + ":" + std::to_string(diag.range.end.column) + "/" +
           target.graph + "/" + target.block_kind + "/" + target.block_name + "/" +
           target.node_instance + "/" + target.pin_name + "/" + target.parameter_name + "/" +
           target.reference + "/" + target.connection_kind;
}

static std::string diagnostic_action_id_base(const std::string& diagnostic_id, const DiagnosticAction& action) {
    return "diagnostic-action:" + diagnostic_id + "/" + action.kind + "/" + action.command + "/" + action.title;
}

static std::string diagnostic_action_to_json(const std::string& action_id, const DiagnosticAction& action) {
    return jobj({
        {"id",      jstr(action_id)},
        {"title",   jstr(action.title)},
        {"kind",    jstr(action.kind)},
        {"command", jstr(action.command)},
        {"edit_range", source_range_to_json(action.edit_range)},
        {"replacement", jstr(action.replacement)}
    });
}

static std::string diagnostic_actions_to_json(const std::string& diagnostic_id, const std::vector<DiagnosticAction>& actions) {
    std::vector<std::string> elems;
    std::unordered_map<std::string, size_t> seen_action_ids;
    for (auto& action : actions) {
        const auto action_id = append_occurrence_suffix(diagnostic_action_id_base(diagnostic_id, action), seen_action_ids);
        elems.push_back(diagnostic_action_to_json(action_id, action));
    }
    return jarray(elems);
}

static std::string diagnostic_to_json(const std::string& diagnostic_id, const Diagnostic& diag) {
    return jobj({
        {"id",       jstr(diagnostic_id)},
        {"severity", jstr(diag.severity == Severity::Error ? "error" : "warning")},
        {"message",  jstr(diag.message)},
        {"context",  jstr(diag.context)},
        {"code",     jstr(diag.code)},
        {"range",    source_range_to_json(diag.range)},
        {"hint",     jstr(diag.hint)},
        {"target",   diagnostic_target_to_json(diag.target)},
        {"actions",  diagnostic_actions_to_json(diagnostic_id, diag.actions)}
    });
}

static std::string diagnostics_to_json_array(const std::vector<Diagnostic>& diagnostics) {
    std::vector<std::string> elems;
    std::unordered_map<std::string, size_t> seen_diagnostic_ids;
    for (auto& diag : diagnostics) {
        const auto diagnostic_id = append_occurrence_suffix(diagnostic_id_base(diag), seen_diagnostic_ids);
        elems.push_back(diagnostic_to_json(diagnostic_id, diag));
    }
    return jarray(elems);
}

static std::string endpoint_id(const std::string& node, const std::string& pin) {
    return pin.empty() ? node : node + "." + pin;
}

static std::string graph_element_id(const std::string& graph_name) {
    return "graph:" + graph_name;
}

static std::string import_element_id(const std::string& path) {
    return "import:" + path;
}

static std::string let_element_id(const std::string& name) {
    return "let:" + name;
}

static std::string param_element_id(const std::string& graph_name, const std::string& param_name) {
    return "param:" + graph_name + "/" + param_name;
}

static std::string node_element_id(const std::string& graph_name, const std::string& instance_name) {
    return "node:" + graph_name + "/" + instance_name;
}

static std::string initializer_field_element_id(const std::string& graph_name, const std::string& instance_name, const std::string& field_name) {
    return "initializer-field:" + graph_name + "/" + instance_name + "/" + field_name;
}

static std::string block_element_id(const std::string& graph_name, const std::string& kind, const std::string& block_name) {
    return "block:" + graph_name + "/" + kind + "/" + block_name;
}

static std::string flow_element_id(const std::string& graph_name, const std::string& kind, const std::string& block_name, const FlowConnection& fc) {
    return "flow:" + graph_name + "/" + kind + "/" + block_name + "/" +
           endpoint_id(fc.from.node_instance, fc.from.pin_name) + "->" +
           endpoint_id(fc.to.node_instance, fc.to.pin_name);
}

static std::string link_element_id(const std::string& graph_name, const std::string& kind, const std::string& block_name, const DataLink& dl) {
    return "link:" + graph_name + "/" + kind + "/" + block_name + "/" +
           endpoint_id(dl.source.node_instance, dl.source.pin_name) + "->" +
           endpoint_id(dl.target.node_instance, dl.target.pin_name);
}

static std::string generate_comment_element_id(const std::string& graph_name, const GenerateComment& c) {
    return "generate-comment:" + graph_name + "/" + c.instance_name + "/" + c.text;
}

static std::string generate_metadata_element_id(const std::string& graph_name, const GenerateMetadata& m) {
    return "generate-metadata:" + graph_name + "/" + m.scope + "/" + m.node + "/" + m.property + "/" + m.value;
}

static std::string disambiguate_element_id(const std::string& base_id, std::unordered_map<std::string, size_t>& seen_ids) {
    size_t& seen = seen_ids[base_id];
    ++seen;
    if (seen == 1) return base_id;
    return base_id + "#" + std::to_string(seen);
}

static std::string initializer_field_to_json(const std::string& graph_name, const std::string& instance_name, const InitializerField& field) {
    return jobj({
        {"id", jstr(initializer_field_element_id(graph_name, instance_name, field.name))},
        {"name", jstr(field.name)},
        {"value", jstr(field.value)},
        {"source_range", source_range_to_json(field.source_range)},
        {"name_source_range", source_range_to_json(field.name_range)},
        {"value_source_range", source_range_to_json(field.value_range)},
        {"value_constructor_source_range", source_range_to_json(field.value_constructor_range)},
        {"value_constructor_type_source_range", source_range_to_json(field.value_constructor_type_range)},
        {"value_constructor_arg_source_range", source_range_to_json(field.value_constructor_arg_range)}
    });
}

static std::string initializer_fields_to_json(const std::string& graph_name, const std::string& instance_name, const std::vector<InitializerField>& fields) {
    std::vector<std::string> elems;
    for (auto& field : fields) elems.push_back(initializer_field_to_json(graph_name, instance_name, field));
    return jarray(elems);
}

static std::string param_to_json(const std::string& graph_name, const GraphParameter& p) {
    std::string dir = p.direction == ParamDirection::In  ? "in" :
                      p.direction == ParamDirection::Out ? "out" : "var";
    return jobj({
        {"id",        jstr(param_element_id(graph_name, p.name))},
        {"persistent_id", jstr(persistent_id_from_annotations(p.annotations))},
        {"source_range", source_range_to_json(p.source_range)},
        {"name_source_range", source_range_to_json(p.name_range)},
        {"name",      jstr(p.name)},
        {"type",      jstr(p.type_name)},
        {"type_source_range", source_range_to_json(p.type_name_range)},
        {"direction", jstr(dir)},
        {"default",   jstr(p.default_value)},
        {"default_source_range", source_range_to_json(p.default_value_range)},
        {"default_constructor_source_range", source_range_to_json(p.default_constructor_range)},
        {"default_constructor_type_source_range", source_range_to_json(p.default_constructor_type_range)},
        {"default_constructor_arg_source_range", source_range_to_json(p.default_constructor_arg_range)},
        {"annotations", annotations_to_json(param_element_id(graph_name, p.name), p.annotations)}
    });
}

static std::string node_inst_to_json(const std::string& graph_name, const NodeInstance& ni) {
    return jobj({
        {"id",       jstr(node_element_id(graph_name, ni.instance_name))},
        {"persistent_id", jstr(persistent_id_from_annotations(ni.annotations))},
        {"source_range", source_range_to_json(ni.source_range)},
        {"type",     jstr(ni.type_name)},
        {"type_source_range", source_range_to_json(ni.type_name_range)},
        {"instance_source_range", source_range_to_json(ni.instance_name_range)},
        {"instance", jstr(ni.instance_name)},
        {"init",     jstr(ni.initializer)},
        {"init_source_range", source_range_to_json(ni.initializer_range)},
        {"init_constructor_source_range", source_range_to_json(ni.initializer_constructor_range)},
        {"init_constructor_type_source_range", source_range_to_json(ni.initializer_constructor_type_range)},
        {"init_constructor_arg_source_range", source_range_to_json(ni.initializer_constructor_arg_range)},
        {"initializer_fields", initializer_fields_to_json(graph_name, ni.instance_name, ni.initializer_fields)},
        {"annotations", annotations_to_json(node_element_id(graph_name, ni.instance_name), ni.annotations)}
    });
}

static std::string flow_to_json(const std::string& graph_name, const std::string& kind, const std::string& block_name, const FlowConnection& fc) {
    return jobj({
        {"id",        jstr(flow_element_id(graph_name, kind, block_name, fc))},
        {"persistent_id", jstr(persistent_id_from_annotations(fc.annotations))},
        {"source_range", source_range_to_json(fc.source_range)},
        {"from_endpoint_source_range", source_range_to_json(fc.from_endpoint_range)},
        {"to_endpoint_source_range", source_range_to_json(fc.to_endpoint_range)},
        {"from_node_source_range", source_range_to_json(fc.from_node_range)},
        {"from_pin_source_range", source_range_to_json(fc.from_pin_range)},
        {"to_node_source_range", source_range_to_json(fc.to_node_range)},
        {"to_pin_source_range", source_range_to_json(fc.to_pin_range)},
        {"from_node", jstr(fc.from.node_instance)},
        {"from_pin",  jstr(fc.from.pin_name)},
        {"to_node",   jstr(fc.to.node_instance)},
        {"to_pin",    jstr(fc.to.pin_name)},
        {"annotations", annotations_to_json(flow_element_id(graph_name, kind, block_name, fc), fc.annotations)}
    });
}

static std::string link_to_json(const std::string& graph_name, const std::string& kind, const std::string& block_name, const DataLink& dl) {
    return jobj({
        {"id",          jstr(link_element_id(graph_name, kind, block_name, dl))},
        {"persistent_id", jstr(persistent_id_from_annotations(dl.annotations))},
        {"source_range", source_range_to_json(dl.source_range)},
        {"target_endpoint_source_range", source_range_to_json(dl.target_endpoint_range)},
        {"source_endpoint_source_range", source_range_to_json(dl.source_endpoint_range)},
        {"target_node_source_range", source_range_to_json(dl.target_node_range)},
        {"target_pin_source_range", source_range_to_json(dl.target_pin_range)},
        {"source_node_source_range", source_range_to_json(dl.source_node_range)},
        {"source_pin_source_range", source_range_to_json(dl.source_pin_range)},
        {"target_node", jstr(dl.target.node_instance)},
        {"target_pin",  jstr(dl.target.pin_name)},
        {"source_node", jstr(dl.source.node_instance)},
        {"source_pin",  jstr(dl.source.pin_name)},
        {"annotations", annotations_to_json(link_element_id(graph_name, kind, block_name, dl), dl.annotations)}
    });
}

static std::string block_to_json(const std::string& graph_name, const LogicBlock& b, const std::string& kind) {
    std::vector<std::string> flows, links;
    for (auto& fc : b.flow_connections) flows.push_back(flow_to_json(graph_name, kind, b.name, fc));
    for (auto& dl : b.data_links)       links.push_back(link_to_json(graph_name, kind, b.name, dl));
    return jobj({
        {"id",    jstr(block_element_id(graph_name, kind, b.name))},
        {"persistent_id", jstr(persistent_id_from_annotations(b.annotations))},
        {"source_range", source_range_to_json(b.source_range)},
        {"name_source_range", source_range_to_json(b.name_range)},
        {"name",  jstr(b.name)},
        {"kind",  jstr(kind)},
        {"annotations", annotations_to_json(block_element_id(graph_name, kind, b.name), b.annotations)},
        {"flows", jarray(flows)},
        {"links", jarray(links)}
    });
}

static std::string generate_comment_to_json(const std::string& graph_name, const GenerateComment& c, std::unordered_map<std::string, size_t>& seen_ids) {
    const auto id = disambiguate_element_id(generate_comment_element_id(graph_name, c), seen_ids);
    return jobj({
        {"id",           jstr(id)},
        {"persistent_id", jstr(persistent_id_from_annotations(c.annotations))},
        {"instance",     jstr(c.instance_name)},
        {"text",         jstr(c.text)},
        {"source_range", source_range_to_json(c.source_range)},
        {"instance_source_range", source_range_to_json(c.instance_name_range)},
        {"text_source_range", source_range_to_json(c.text_range)},
        {"annotations", annotations_to_json(id, c.annotations)}
    });
}

static std::string generate_metadata_to_json(const std::string& graph_name, const GenerateMetadata& m, std::unordered_map<std::string, size_t>& seen_ids) {
    const auto id = disambiguate_element_id(generate_metadata_element_id(graph_name, m), seen_ids);
    return jobj({
        {"id",           jstr(id)},
        {"persistent_id", jstr(persistent_id_from_annotations(m.annotations))},
        {"scope",        jstr(m.scope)},
        {"node",         jstr(m.node)},
        {"property",     jstr(m.property)},
        {"value",        jstr(m.value)},
        {"source_range", source_range_to_json(m.source_range)},
        {"scope_source_range", source_range_to_json(m.scope_range)},
        {"node_source_range", source_range_to_json(m.node_range)},
        {"property_source_range", source_range_to_json(m.property_range)},
        {"value_source_range", source_range_to_json(m.value_range)},
        {"value_constructor_source_range", source_range_to_json(m.value_constructor_range)},
        {"value_constructor_type_source_range", source_range_to_json(m.value_constructor_type_range)},
        {"value_constructor_arg_source_range", source_range_to_json(m.value_constructor_arg_range)},
        {"annotations", annotations_to_json(id, m.annotations)}
    });
}

static std::string generate_to_json(const std::string& graph_name, const GenerateBlock& gen) {
    std::vector<std::string> comments, metadata;
    std::unordered_map<std::string, size_t> seen_ids;
    for (auto& c : gen.comments) comments.push_back(generate_comment_to_json(graph_name, c, seen_ids));
    for (auto& m : gen.metadata) metadata.push_back(generate_metadata_to_json(graph_name, m, seen_ids));
    return jobj({
        {"source_range", source_range_to_json(gen.source_range)},
        {"comments",     jarray(comments)},
        {"metadata",     jarray(metadata)}
    });
}

static std::string graph_to_json(const Graph& g) {
    std::vector<std::string> params, nodes, events, funcs;
    for (auto& p : g.parameters)      params.push_back(param_to_json(g.name, p));
    for (auto& ni : g.node_instances) nodes.push_back(node_inst_to_json(g.name, ni));
    for (auto& ev : g.events)         events.push_back(block_to_json(g.name, ev, "event"));
    for (auto& fn : g.functions)      funcs.push_back(block_to_json(g.name, fn, "function"));

    return jobj({
        {"id",         jstr(graph_element_id(g.name))},
        {"persistent_id", jstr(persistent_id_from_annotations(g.annotations))},
        {"source_range", source_range_to_json(g.source_range)},
        {"name_source_range", source_range_to_json(g.name_range)},
        {"name",       jstr(g.name)},
        {"base_type",  g.base_type ? jstr(*g.base_type) : "null"},
        {"base_type_source_range", source_range_to_json(g.base_type_range)},
        {"annotations", annotations_to_json(graph_element_id(g.name), g.annotations)},
        {"parameters", jarray(params)},
        {"nodes",      jarray(nodes)},
        {"events",     jarray(events)},
        {"functions",  jarray(funcs)},
        {"generate",   g.generate ? generate_to_json(g.name, *g.generate) : "null"}
    });
}

static NodeDefinition derive_node_definition_from_graph(const Graph& graph) {
    NodeDefinition def;
    def.type_name = graph.name;
    def.is_native = false;
    def.source_graph = graph.name;
    def.annotations = graph.annotations;
    def.source_range = graph.source_range;
    def.name_range = graph.name_range;

    for (auto& param : graph.parameters) {
        if (param.direction == ParamDirection::Var) continue;

        PinDefinition pin;
        pin.name = param.name;
        pin.type_name = param.type_name;
        pin.kind = PinKind::Data;
        pin.direction = (param.direction == ParamDirection::In)
                        ? PinDirection::Input
                        : PinDirection::Output;
        pin.annotations = param.annotations;
        pin.source_range = param.source_range;
        pin.name_range = param.name_range;
        pin.type_name_range = param.type_name_range;
        def.pins.push_back(std::move(pin));
    }

    for (auto& ev : graph.events) {
        PinDefinition pin;
        pin.name = ev.name;
        pin.kind = PinKind::Exec;
        pin.direction = PinDirection::Input;
        pin.annotations = ev.annotations;
        pin.source_range = ev.source_range;
        pin.name_range = ev.name_range;
        def.pins.push_back(std::move(pin));
    }

    return def;
}

static void refresh_graph_node_definition(Environment& env, const Graph& graph) {
    env.nodes().register_graph_node(derive_node_definition_from_graph(graph));
}

static void refresh_module_graph_node_definitions(Environment& env, const Module& module) {
    std::unordered_set<std::string> graph_names;
    graph_names.reserve(module.graphs.size());
    for (auto& graph : module.graphs) {
        graph_names.insert(graph.name);
    }

    std::vector<std::string> stale_graph_nodes;
    for (auto* def : env.nodes().all()) {
        if (!def->is_native && !def->source_graph.empty() &&
            graph_names.find(def->source_graph) == graph_names.end()) {
            stale_graph_nodes.push_back(def->type_name);
        }
    }
    for (auto& type_name : stale_graph_nodes) {
        env.nodes().unregister_graph_node(type_name);
    }
    for (auto& graph : module.graphs) {
        refresh_graph_node_definition(env, graph);
    }
}

static std::string node_def_to_json(const NodeDefinition& nd) {
    std::vector<std::string> pins, fields, tags;
    for (auto& p : nd.pins) pins.push_back(pin_def_to_json(nd.type_name, p));
    for (auto& field : nd.fields) fields.push_back(node_field_def_to_json(nd.type_name, field));
    for (auto& t : nd.tags) tags.push_back(jstr(t));
    return jobj({
        {"id",           jstr(declaration_node_element_id(nd.type_name))},
        {"type_name",    jstr(nd.type_name)},
        {"source_file",  jstr(nd.source_file)},
        {"source_range", source_range_to_json(nd.source_range)},
        {"name_source_range", source_range_to_json(nd.name_range)},
        {"is_native",    jbool(nd.is_native)},
        {"source_graph", jstr(nd.source_graph)},
        {"tags",         jarray(tags)},
        {"pins",         jarray(pins)},
        {"fields",       jarray(fields)},
        {"persistent_id", jstr(persistent_id_from_annotations(nd.annotations))},
        {"annotations",  annotations_to_json(declaration_node_element_id(nd.type_name), nd.annotations)}
    });
}

EditSession::EditSession(Environment& env) : env_(env) {}

static void upsert_annotation(std::vector<Annotation>& annotations, const Annotation& annot);
static bool erase_annotation(std::vector<Annotation>& annotations, const std::string& annot_name);
static bool has_annotation(const std::vector<Annotation>& annotations, const std::string& annot_name);

// ─── Undo helpers ──────────────────────────────────────────────────

// Saves current active graph state before a mutation.
void EditSession::push_undo(const std::string& description) {
    if (active_ < 0 || active_ >= static_cast<int>(module_.graphs.size())) return;
    asset_source_.reset();
    EditSnapshot snapshot;
    snapshot.scope = EditSnapshot::Scope::Graph;
    snapshot.graph = module_.graphs[active_];
    snapshot.active_index = active_;
    snapshot.description = description;
    undo_stack_.push_back(std::move(snapshot));
    if (undo_stack_.size() > kMaxUndoDepth) {
        undo_stack_.erase(undo_stack_.begin());
    }
    redo_stack_.clear();
    dirty_ = true;
}

// Saves the full module state before a source-level replacement.
void EditSession::push_module_undo(const std::string& description) {
    EditSnapshot snapshot;
    snapshot.scope = EditSnapshot::Scope::Module;
    snapshot.module = module_;
    snapshot.active_index = active_;
    snapshot.description = description;
    snapshot.asset_source = asset_source_;
    undo_stack_.push_back(std::move(snapshot));
    if (undo_stack_.size() > kMaxUndoDepth) {
        undo_stack_.erase(undo_stack_.begin());
    }
    redo_stack_.clear();
    asset_source_.reset();
    dirty_ = true;
}

// Finds an event or function block by name in the active graph.
LogicBlock* EditSession::find_block(const std::string& name) {
    auto* g = active_graph();
    if (!g) return nullptr;
    for (auto& ev : g->events)    if (ev.name == name) return &ev;
    for (auto& fn : g->functions) if (fn.name == name) return &fn;
    return nullptr;
}

// ─── Module-level ──────────────────────────────────────────────────

Result<int, std::string> EditSession::new_graph(const std::string& name, const std::string& base_type) {
    for (auto& g : module_.graphs) {
        if (g.name == name) return Result<int, std::string>::err("Graph '" + name + "' already exists");
    }
    Graph g;
    g.name = name;
    if (!base_type.empty()) g.base_type = base_type;
    asset_source_.reset();
    module_.graphs.push_back(std::move(g));
    active_ = static_cast<int>(module_.graphs.size()) - 1;
    refresh_graph_node_definition(env_, module_.graphs.back());
    dirty_ = true;
    return Result<int, std::string>::ok(active_);
}

Result<void, std::string> EditSession::delete_graph(const std::string& name) {
    for (size_t i = 0; i < module_.graphs.size(); i++) {
        if (module_.graphs[i].name == name) {
            asset_source_.reset();
            module_.graphs.erase(module_.graphs.begin() + i);
            if (active_ == static_cast<int>(i)) active_ = -1;
            else if (active_ > static_cast<int>(i)) active_--;
            refresh_module_graph_node_definitions(env_, module_);
            dirty_ = true;
            return Result<void, std::string>::ok();
        }
    }
    return Result<void, std::string>::err("Graph '" + name + "' not found");
}

Result<void, std::string> EditSession::rename_graph(const std::string& old_name, const std::string& new_name) {
    if (old_name.empty() || new_name.empty()) {
        return Result<void, std::string>::err("Graph names cannot be empty");
    }
    if (old_name == new_name) {
        for (auto& g : module_.graphs) {
            if (g.name == old_name) return Result<void, std::string>::ok();
        }
        return Result<void, std::string>::err("Graph '" + old_name + "' not found");
    }

    auto target_it = module_.graphs.end();
    for (auto it = module_.graphs.begin(); it != module_.graphs.end(); ++it) {
        if (it->name == old_name) target_it = it;
        if (it->name == new_name) {
            return Result<void, std::string>::err("Graph '" + new_name + "' already exists");
        }
    }
    if (target_it == module_.graphs.end()) {
        return Result<void, std::string>::err("Graph '" + old_name + "' not found");
    }

    push_module_undo("rename graph '" + old_name + "' to '" + new_name + "'");
    target_it->name = new_name;
    for (auto& g : module_.graphs) {
        for (auto& node : g.node_instances) {
            if (node.type_name == old_name) node.type_name = new_name;
        }
    }
    refresh_module_graph_node_definitions(env_, module_);
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::set_active(const std::string& name) {
    for (size_t i = 0; i < module_.graphs.size(); i++) {
        if (module_.graphs[i].name == name) {
            active_ = static_cast<int>(i);
            undo_stack_.clear();
            redo_stack_.clear();
            return Result<void, std::string>::ok();
        }
    }
    return Result<void, std::string>::err("Graph '" + name + "' not found");
}

Result<void, std::string> EditSession::set_active(int index) {
    if (index < 0 || index >= static_cast<int>(module_.graphs.size()))
        return Result<void, std::string>::err("Index out of range");
    active_ = index;
    undo_stack_.clear();
    redo_stack_.clear();
    return Result<void, std::string>::ok();
}

void EditSession::add_import(const std::string& path) {
    add_import(path, false);
}

void EditSession::add_import(const std::string& path, bool loaded) {
    auto base_dir = module_base_dir(module_);
    auto key = import_key(path);
    for (auto& imp : module_.imports) {
        if (imp.path == path || import_key(imp.path, base_dir) == key) {
            if (loaded) imp.loaded = true;
            return;
        }
    }
    ImportDecl decl;
    decl.path = path;
    decl.is_native = (path.size() > 5 && path.substr(path.size() - 5) == ".d.gs");
    decl.loaded = loaded;
    asset_source_.reset();
    module_.imports.push_back(std::move(decl));
    dirty_ = true;
}

bool EditSession::is_import_loaded(const std::string& path) const {
    return contains_key(loaded_import_keys_, import_key(path));
}

void EditSession::mark_loaded_imports() {
    auto base_dir = module_base_dir(module_);
    for (auto& imp : module_.imports) {
        imp.loaded = contains_key(loaded_import_keys_, import_key(imp.path, base_dir));
    }
}

void EditSession::add_let(const std::string& name, const std::string& type_name, const std::string& ctor_arg) {
    LetDecl decl;
    decl.name = name;
    decl.type_name = type_name;
    decl.constructor_arg = ctor_arg;
    asset_source_.reset();
    module_.top_level_lets.push_back(std::move(decl));
    dirty_ = true;
}

Graph* EditSession::active_graph() {
    if (active_ < 0 || active_ >= static_cast<int>(module_.graphs.size())) return nullptr;
    return &module_.graphs[active_];
}

const Graph* EditSession::active_graph() const {
    if (active_ < 0 || active_ >= static_cast<int>(module_.graphs.size())) return nullptr;
    return &module_.graphs[active_];
}

// ─── Graph-level ───────────────────────────────────────────────────

Result<void, std::string> EditSession::add_param(ParamDirection dir, const std::string& name,
                                                  const std::string& type_name, const std::string& default_val) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    for (auto& p : g->parameters) {
        if (p.name == name) return Result<void, std::string>::err("Parameter '" + name + "' already exists");
    }
    push_undo("add param '" + name + "'");
    GraphParameter p;
    p.name = name;
    p.type_name = type_name;
    p.direction = dir;
    p.default_value = default_val;
    g->parameters.push_back(std::move(p));
    refresh_graph_node_definition(env_, *g);
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::remove_param(const std::string& name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    for (auto it = g->parameters.begin(); it != g->parameters.end(); ++it) {
        if (it->name == name) {
            push_undo("remove param '" + name + "'");
            g->parameters.erase(it);
            refresh_graph_node_definition(env_, *g);
            return Result<void, std::string>::ok();
        }
    }
    return Result<void, std::string>::err("Parameter '" + name + "' not found");
}

Result<void, std::string> EditSession::rename_param(const std::string& old_name, const std::string& new_name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (old_name.empty() || new_name.empty()) {
        return Result<void, std::string>::err("Parameter names cannot be empty");
    }
    if (old_name == new_name) {
        for (auto& p : g->parameters) {
            if (p.name == old_name) return Result<void, std::string>::ok();
        }
        return Result<void, std::string>::err("Parameter '" + old_name + "' not found");
    }

    auto target_it = g->parameters.end();
    for (auto it = g->parameters.begin(); it != g->parameters.end(); ++it) {
        if (it->name == old_name) target_it = it;
        if (it->name == new_name) {
            return Result<void, std::string>::err("Parameter '" + new_name + "' already exists");
        }
    }
    if (target_it == g->parameters.end()) {
        return Result<void, std::string>::err("Parameter '" + old_name + "' not found");
    }

    const std::string graph_name = g->name;
    push_module_undo("rename param '" + old_name + "' to '" + new_name + "'");
    target_it->name = new_name;

    auto rename_refs = [&](LogicBlock& block) {
        for (auto& dl : block.data_links) {
            if (dl.source.pin_name.empty() && dl.source.node_instance == old_name) {
                dl.source.node_instance = new_name;
            }
        }
    };
    for (auto& ev : g->events) rename_refs(ev);
    for (auto& fn : g->functions) rename_refs(fn);

    auto is_graph_node_instance = [&](const Graph& owner, const std::string& instance_name) {
        for (auto& ni : owner.node_instances) {
            if (ni.instance_name == instance_name && ni.type_name == graph_name) return true;
        }
        return false;
    };
    auto rename_graph_node_pins = [&](Graph& owner, LogicBlock& block) {
        for (auto& dl : block.data_links) {
            if (dl.target.pin_name == old_name && is_graph_node_instance(owner, dl.target.node_instance)) {
                dl.target.pin_name = new_name;
            }
            if (dl.source.pin_name == old_name && is_graph_node_instance(owner, dl.source.node_instance)) {
                dl.source.pin_name = new_name;
            }
        }
    };
    for (auto& owner : module_.graphs) {
        for (auto& ev : owner.events) rename_graph_node_pins(owner, ev);
        for (auto& fn : owner.functions) rename_graph_node_pins(owner, fn);
    }

    refresh_graph_node_definition(env_, *g);
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::set_param_type(const std::string& name, const std::string& type_name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    const auto next_type = trim_copy(type_name);
    if (!is_identifier_text(next_type)) {
        return Result<void, std::string>::err("Parameter type must be an identifier");
    }

    for (auto& p : g->parameters) {
        if (p.name == name) {
            if (p.type_name == next_type) return Result<void, std::string>::ok();

            push_undo("set param type '" + name + "'");
            p.type_name = next_type;
            p.type_name_range = {};
            refresh_graph_node_definition(env_, *g);
            return Result<void, std::string>::ok();
        }
    }

    return Result<void, std::string>::err("Parameter '" + name + "' not found");
}

Result<void, std::string> EditSession::set_param_default(const std::string& name, const std::string& default_val) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");

    for (auto& p : g->parameters) {
        if (p.name == name) {
            if (p.default_value == default_val) return Result<void, std::string>::ok();

            push_undo("set param default '" + name + "'");
            p.default_value = default_val;
            p.default_value_range = {};
            p.default_constructor_range = {};
            p.default_constructor_type_range = {};
            p.default_constructor_arg_range = {};
            return Result<void, std::string>::ok();
        }
    }

    return Result<void, std::string>::err("Parameter '" + name + "' not found");
}

Result<void, std::string> EditSession::set_param_default_constructor(
    const std::string& name,
    const std::string& constructor_type,
    const std::string& constructor_argument) {
    const auto type_name = trim_copy(constructor_type);
    if (!is_identifier_text(type_name)) {
        return Result<void, std::string>::err("Parameter default constructor type must be an identifier");
    }

    const auto argument = trim_copy(constructor_argument);
    return set_param_default(name, type_name + "(" + argument + ")");
}

Result<void, std::string> EditSession::set_param_default_constructor_argument(
    const std::string& name,
    const std::string& constructor_argument) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");

    for (const auto& p : g->parameters) {
        if (p.name != name) continue;
        const auto constructor = parse_constructor_call_text(p.default_value);
        if (!constructor) {
            return Result<void, std::string>::err(
                "Parameter '" + name + "' default is not a constructor call");
        }
        return set_param_default(name, constructor->type_name + "(" + trim_copy(constructor_argument) + ")");
    }

    return Result<void, std::string>::err("Parameter '" + name + "' not found");
}

Result<void, std::string> EditSession::set_param_default_constructor_type(
    const std::string& name,
    const std::string& constructor_type) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    const auto type_name = trim_copy(constructor_type);
    if (!is_identifier_text(type_name)) {
        return Result<void, std::string>::err("Parameter default constructor type must be an identifier");
    }

    for (const auto& p : g->parameters) {
        if (p.name != name) continue;
        const auto constructor = parse_constructor_call_text(p.default_value);
        if (!constructor) {
            return Result<void, std::string>::err(
                "Parameter '" + name + "' default is not a constructor call");
        }
        return set_param_default(name, type_name + "(" + constructor->argument + ")");
    }

    return Result<void, std::string>::err("Parameter '" + name + "' not found");
}

Result<void, std::string> EditSession::add_node(const std::string& type_name, const std::string& instance_name,
                                                const std::string& initializer) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    for (auto& ni : g->node_instances) {
        if (ni.instance_name == instance_name)
            return Result<void, std::string>::err("Node '" + instance_name + "' already exists");
    }
    // Verify node type exists
    if (!env_.nodes().find(type_name))
        return Result<void, std::string>::err("Unknown node type '" + type_name + "'");

    push_undo("add node '" + type_name + " " + instance_name + "'");
    NodeInstance ni;
    ni.type_name = type_name;
    ni.instance_name = instance_name;
    ni.initializer = initializer;
    g->node_instances.push_back(std::move(ni));
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::set_node_initializer(
    const std::string& instance_name,
    const std::string& initializer) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    const auto next_initializer = trim_copy(initializer);

    for (auto& ni : g->node_instances) {
        if (ni.instance_name != instance_name) continue;
        if (ni.initializer == next_initializer) return Result<void, std::string>::ok();

        push_undo("set initializer '" + instance_name + "'");
        ni.initializer = next_initializer;
        ni.initializer_range = {};
        ni.initializer_constructor_range = {};
        ni.initializer_constructor_type_range = {};
        ni.initializer_constructor_arg_range = {};
        auto assignments_result = parse_initializer_assignment_text(next_initializer);
        ni.initializer_fields = assignments_result.is_ok()
            ? initializer_fields_from_assignment_text(assignments_result.value())
            : std::vector<InitializerField>{};
        return Result<void, std::string>::ok();
    }

    return Result<void, std::string>::err("Node '" + instance_name + "' not found");
}

Result<void, std::string> EditSession::set_node_initializer_field(
    const std::string& instance_name,
    const std::string& field_name,
    const std::string& value) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (!is_identifier_text(field_name)) {
        return Result<void, std::string>::err("Initializer field name must be an identifier");
    }
    if (trim_copy(value).empty()) {
        return Result<void, std::string>::err("Initializer field value cannot be empty");
    }

    for (auto& ni : g->node_instances) {
        if (ni.instance_name != instance_name) continue;
        auto assignments_result = parse_initializer_assignment_text(ni.initializer);
        if (assignments_result.is_err()) return Result<void, std::string>::err(assignments_result.error());
        auto assignments = std::move(assignments_result).value();

        bool updated = false;
        for (auto& assignment : assignments) {
            if (assignment.name != field_name) continue;
            assignment.value = value;
            updated = true;
            break;
        }
        if (!updated) assignments.push_back({field_name, value});

        push_undo("set initializer field '" + instance_name + "." + field_name + "'");
        ni.initializer = format_initializer_assignment_text(assignments);
        ni.initializer_fields = initializer_fields_from_assignment_text(assignments);
        return Result<void, std::string>::ok();
    }

    return Result<void, std::string>::err("Node '" + instance_name + "' not found");
}

Result<void, std::string> EditSession::set_node_initializer_constructor_field(
    const std::string& instance_name,
    const std::string& field_name,
    const std::string& constructor_type,
    const std::string& constructor_argument) {
    const auto type_name = trim_copy(constructor_type);
    if (!is_identifier_text(type_name)) {
        return Result<void, std::string>::err("Initializer constructor type must be an identifier");
    }

    const auto argument = trim_copy(constructor_argument);
    return set_node_initializer_field(instance_name, field_name, type_name + "(" + argument + ")");
}

Result<void, std::string> EditSession::set_node_initializer_constructor_argument(
    const std::string& instance_name,
    const std::string& field_name,
    const std::string& constructor_argument) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (!is_identifier_text(field_name)) {
        return Result<void, std::string>::err("Initializer field name must be an identifier");
    }

    for (auto& ni : g->node_instances) {
        if (ni.instance_name != instance_name) continue;
        auto assignments_result = parse_initializer_assignment_text(ni.initializer);
        if (assignments_result.is_err()) return Result<void, std::string>::err(assignments_result.error());
        auto assignments = std::move(assignments_result).value();

        for (auto& assignment : assignments) {
            if (assignment.name != field_name) continue;
            const auto constructor = parse_constructor_call_text(assignment.value);
            if (!constructor) {
                return Result<void, std::string>::err(
                    "Initializer field '" + field_name + "' is not a constructor call on node '" + instance_name + "'");
            }

            push_undo("set initializer constructor argument '" + instance_name + "." + field_name + "'");
            assignment.value = constructor->type_name + "(" + trim_copy(constructor_argument) + ")";
            ni.initializer = format_initializer_assignment_text(assignments);
            ni.initializer_fields = initializer_fields_from_assignment_text(assignments);
            return Result<void, std::string>::ok();
        }

        return Result<void, std::string>::err(
            "Initializer field '" + field_name + "' not found on node '" + instance_name + "'");
    }

    return Result<void, std::string>::err("Node '" + instance_name + "' not found");
}

Result<void, std::string> EditSession::set_node_initializer_constructor_type(
    const std::string& instance_name,
    const std::string& field_name,
    const std::string& constructor_type) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (!is_identifier_text(field_name)) {
        return Result<void, std::string>::err("Initializer field name must be an identifier");
    }
    const auto type_name = trim_copy(constructor_type);
    if (!is_identifier_text(type_name)) {
        return Result<void, std::string>::err("Initializer constructor type must be an identifier");
    }

    for (auto& ni : g->node_instances) {
        if (ni.instance_name != instance_name) continue;
        auto assignments_result = parse_initializer_assignment_text(ni.initializer);
        if (assignments_result.is_err()) return Result<void, std::string>::err(assignments_result.error());
        auto assignments = std::move(assignments_result).value();

        for (auto& assignment : assignments) {
            if (assignment.name != field_name) continue;
            const auto constructor = parse_constructor_call_text(assignment.value);
            if (!constructor) {
                return Result<void, std::string>::err(
                    "Initializer field '" + field_name + "' is not a constructor call on node '" + instance_name + "'");
            }

            push_undo("set initializer constructor type '" + instance_name + "." + field_name + "'");
            assignment.value = type_name + "(" + constructor->argument + ")";
            ni.initializer = format_initializer_assignment_text(assignments);
            ni.initializer_fields = initializer_fields_from_assignment_text(assignments);
            return Result<void, std::string>::ok();
        }

        return Result<void, std::string>::err(
            "Initializer field '" + field_name + "' not found on node '" + instance_name + "'");
    }

    return Result<void, std::string>::err("Node '" + instance_name + "' not found");
}

Result<void, std::string> EditSession::remove_node_initializer_field(
    const std::string& instance_name,
    const std::string& field_name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (!is_identifier_text(field_name)) {
        return Result<void, std::string>::err("Initializer field name must be an identifier");
    }

    for (auto& ni : g->node_instances) {
        if (ni.instance_name != instance_name) continue;
        auto assignments_result = parse_initializer_assignment_text(ni.initializer);
        if (assignments_result.is_err()) return Result<void, std::string>::err(assignments_result.error());
        auto assignments = std::move(assignments_result).value();

        const auto old_size = assignments.size();
        assignments.erase(
            std::remove_if(assignments.begin(), assignments.end(),
                [&](const InitializerAssignmentText& assignment) { return assignment.name == field_name; }),
            assignments.end());
        if (assignments.size() == old_size) {
            return Result<void, std::string>::err(
                "Initializer field '" + field_name + "' not found on node '" + instance_name + "'");
        }

        push_undo("remove initializer field '" + instance_name + "." + field_name + "'");
        ni.initializer = format_initializer_assignment_text(assignments);
        ni.initializer_fields = initializer_fields_from_assignment_text(assignments);
        return Result<void, std::string>::ok();
    }

    return Result<void, std::string>::err("Node '" + instance_name + "' not found");
}

Result<void, std::string> EditSession::rename_node_initializer_field(
    const std::string& instance_name,
    const std::string& old_field_name,
    const std::string& new_field_name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (!is_identifier_text(old_field_name) || !is_identifier_text(new_field_name)) {
        return Result<void, std::string>::err("Initializer field name must be an identifier");
    }

    for (auto& ni : g->node_instances) {
        if (ni.instance_name != instance_name) continue;
        auto assignments_result = parse_initializer_assignment_text(ni.initializer);
        if (assignments_result.is_err()) return Result<void, std::string>::err(assignments_result.error());
        auto assignments = std::move(assignments_result).value();

        auto target = assignments.end();
        for (auto it = assignments.begin(); it != assignments.end(); ++it) {
            if (it->name == old_field_name) target = it;
            if (old_field_name != new_field_name && it->name == new_field_name) {
                return Result<void, std::string>::err(
                    "Initializer field '" + new_field_name + "' already exists on node '" + instance_name + "'");
            }
        }
        if (target == assignments.end()) {
            return Result<void, std::string>::err(
                "Initializer field '" + old_field_name + "' not found on node '" + instance_name + "'");
        }
        if (old_field_name == new_field_name) {
            return Result<void, std::string>::ok();
        }

        push_undo("rename initializer field '" + instance_name + "." + old_field_name + "' to '" + new_field_name + "'");
        target->name = new_field_name;
        ni.initializer = format_initializer_assignment_text(assignments);
        ni.initializer_fields = initializer_fields_from_assignment_text(assignments);
        return Result<void, std::string>::ok();
    }

    return Result<void, std::string>::err("Node '" + instance_name + "' not found");
}

Result<void, std::string> EditSession::remove_node(const std::string& instance_name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");

    bool found = false;
    for (auto it = g->node_instances.begin(); it != g->node_instances.end(); ++it) {
        if (it->instance_name == instance_name) {
            push_undo("remove node '" + instance_name + "'");
            g->node_instances.erase(it);
            found = true;
            break;
        }
    }
    if (!found) return Result<void, std::string>::err("Node '" + instance_name + "' not found");

    // Remove connections referencing this node from all logic blocks
    auto remove_refs = [&](LogicBlock& block) {
        block.flow_connections.erase(
            std::remove_if(block.flow_connections.begin(), block.flow_connections.end(),
                [&](const FlowConnection& fc) {
                    return fc.from.node_instance == instance_name || fc.to.node_instance == instance_name;
                }),
            block.flow_connections.end());
        block.data_links.erase(
            std::remove_if(block.data_links.begin(), block.data_links.end(),
                [&](const DataLink& dl) {
                    return dl.target.node_instance == instance_name || dl.source.node_instance == instance_name;
                }),
            block.data_links.end());
    };
    for (auto& ev : g->events) remove_refs(ev);
    for (auto& fn : g->functions) remove_refs(fn);

    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::rename_node_instance(const std::string& old_name, const std::string& new_name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (old_name.empty() || new_name.empty()) {
        return Result<void, std::string>::err("Node names cannot be empty");
    }
    if (old_name == new_name) {
        for (auto& ni : g->node_instances) {
            if (ni.instance_name == old_name) return Result<void, std::string>::ok();
        }
        return Result<void, std::string>::err("Node '" + old_name + "' not found");
    }

    auto target_it = g->node_instances.end();
    for (auto it = g->node_instances.begin(); it != g->node_instances.end(); ++it) {
        if (it->instance_name == old_name) target_it = it;
        if (it->instance_name == new_name) {
            return Result<void, std::string>::err("Node '" + new_name + "' already exists");
        }
    }
    if (target_it == g->node_instances.end()) {
        return Result<void, std::string>::err("Node '" + old_name + "' not found");
    }

    push_undo("rename node '" + old_name + "' to '" + new_name + "'");
    target_it->instance_name = new_name;

    auto rename_pin_address = [&](PinAddress& addr) {
        if (addr.node_instance == old_name) addr.node_instance = new_name;
    };
    auto rename_refs = [&](LogicBlock& block) {
        for (auto& fc : block.flow_connections) {
            rename_pin_address(fc.from);
            rename_pin_address(fc.to);
        }
        for (auto& dl : block.data_links) {
            rename_pin_address(dl.target);
            if (!dl.source.pin_name.empty() && dl.source.node_instance == old_name) {
                dl.source.node_instance = new_name;
            }
        }
    };
    for (auto& ev : g->events) rename_refs(ev);
    for (auto& fn : g->functions) rename_refs(fn);

    if (g->generate) {
        for (auto& comment : g->generate->comments) {
            if (comment.instance_name == old_name) comment.instance_name = new_name;
        }
        for (auto& metadata : g->generate->metadata) {
            if (metadata.node == old_name) metadata.node = new_name;
        }
    }

    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::add_event(const std::string& name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    for (auto& ev : g->events) {
        if (ev.name == name) return Result<void, std::string>::err("Event '" + name + "' already exists");
    }
    push_undo("add event '" + name + "'");
    Event ev;
    ev.name = name;
    g->events.push_back(std::move(ev));
    refresh_graph_node_definition(env_, *g);
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::remove_event(const std::string& name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    for (auto it = g->events.begin(); it != g->events.end(); ++it) {
        if (it->name == name) {
            push_undo("remove event '" + name + "'");
            g->events.erase(it);
            refresh_graph_node_definition(env_, *g);
            return Result<void, std::string>::ok();
        }
    }
    return Result<void, std::string>::err("Event '" + name + "' not found");
}

Result<void, std::string> EditSession::rename_event(const std::string& old_name, const std::string& new_name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (old_name.empty() || new_name.empty()) {
        return Result<void, std::string>::err("Event names cannot be empty");
    }
    if (old_name == new_name) {
        for (auto& ev : g->events) {
            if (ev.name == old_name) return Result<void, std::string>::ok();
        }
        return Result<void, std::string>::err("Event '" + old_name + "' not found");
    }

    auto target_it = g->events.end();
    for (auto it = g->events.begin(); it != g->events.end(); ++it) {
        if (it->name == old_name) target_it = it;
        if (it->name == new_name) {
            return Result<void, std::string>::err("Event '" + new_name + "' already exists");
        }
    }
    if (target_it == g->events.end()) {
        return Result<void, std::string>::err("Event '" + old_name + "' not found");
    }

    const std::string graph_name = g->name;
    push_module_undo("rename event '" + old_name + "' to '" + new_name + "'");
    target_it->name = new_name;

    auto is_graph_node_instance = [&](const Graph& owner, const std::string& instance_name) {
        for (auto& ni : owner.node_instances) {
            if (ni.instance_name == instance_name && ni.type_name == graph_name) return true;
        }
        return false;
    };
    auto rename_endpoint = [&](Graph& owner, PinAddress& endpoint) {
        if (endpoint.pin_name == old_name && is_graph_node_instance(owner, endpoint.node_instance)) {
            endpoint.pin_name = new_name;
        }
    };
    auto rename_graph_node_pins = [&](Graph& owner, LogicBlock& block) {
        for (auto& fc : block.flow_connections) {
            rename_endpoint(owner, fc.from);
            rename_endpoint(owner, fc.to);
        }
    };
    for (auto& owner : module_.graphs) {
        for (auto& ev : owner.events) rename_graph_node_pins(owner, ev);
        for (auto& fn : owner.functions) rename_graph_node_pins(owner, fn);
    }

    refresh_graph_node_definition(env_, *g);
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::add_function(const std::string& name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    for (auto& fn : g->functions) {
        if (fn.name == name) return Result<void, std::string>::err("Function '" + name + "' already exists");
    }
    push_undo("add function '" + name + "'");
    Function fn;
    fn.name = name;
    g->functions.push_back(std::move(fn));
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::remove_function(const std::string& name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    for (auto it = g->functions.begin(); it != g->functions.end(); ++it) {
        if (it->name == name) {
            push_undo("remove function '" + name + "'");
            g->functions.erase(it);
            return Result<void, std::string>::ok();
        }
    }
    return Result<void, std::string>::err("Function '" + name + "' not found");
}

Result<void, std::string> EditSession::rename_function(const std::string& old_name, const std::string& new_name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (old_name.empty() || new_name.empty()) {
        return Result<void, std::string>::err("Function names cannot be empty");
    }
    if (old_name == new_name) {
        for (auto& fn : g->functions) {
            if (fn.name == old_name) return Result<void, std::string>::ok();
        }
        return Result<void, std::string>::err("Function '" + old_name + "' not found");
    }

    auto target_it = g->functions.end();
    for (auto it = g->functions.begin(); it != g->functions.end(); ++it) {
        if (it->name == old_name) target_it = it;
        if (it->name == new_name) {
            return Result<void, std::string>::err("Function '" + new_name + "' already exists");
        }
    }
    if (target_it == g->functions.end()) {
        return Result<void, std::string>::err("Function '" + old_name + "' not found");
    }

    push_undo("rename function '" + old_name + "' to '" + new_name + "'");
    target_it->name = new_name;
    return Result<void, std::string>::ok();
}

// ─── Connection-level ──────────────────────────────────────────────

// Validates that ref_name is a legal reference within block_name's scope.
// - function scope: only "context" and parameter names
// - event scope: "context", parameter names, and node instance names
// Returns error string if violation detected, empty string if OK.
static std::string check_block_scope(const Graph& g, const std::string& block_name, const std::string& ref_name) {
    if (ref_name == "context") return "";
    for (auto& p : g.parameters) {
        if (p.name == ref_name) return "";
    }

    bool is_function = false;
    for (auto& fn : g.functions) {
        if (fn.name == block_name) { is_function = true; break; }
    }

    if (is_function) {
        return "function '" + block_name + "': cannot reference '" + ref_name +
               "' (only context and parameters allowed)";
    }

    // Event scope: also allow node instance names
    for (auto& ni : g.node_instances) {
        if (ni.instance_name == ref_name) return "";
    }

    return "event '" + block_name + "': unknown reference '" + ref_name +
           "' (must be context, a parameter, or a node instance)";
}

Result<void, std::string> EditSession::add_flow(const std::string& block_name,
                                                 const std::string& from_node, const std::string& from_pin,
                                                 const std::string& to_node, const std::string& to_pin) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    auto* block = find_block(block_name);
    if (!block) return Result<void, std::string>::err("Logic block '" + block_name + "' not found");

    auto err1 = check_block_scope(*g, block_name, from_node);
    if (!err1.empty()) return Result<void, std::string>::err(err1);
    auto err2 = check_block_scope(*g, block_name, to_node);
    if (!err2.empty()) return Result<void, std::string>::err(err2);

    push_undo("add flow " + from_node + "." + from_pin + " -> " + to_node + "." + to_pin);
    FlowConnection fc;
    fc.from = {from_node, from_pin};
    fc.to = {to_node, to_pin};
    block->flow_connections.push_back(std::move(fc));
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::remove_flow(const std::string& block_name,
                                                    const std::string& from_node, const std::string& from_pin,
                                                    const std::string& to_node, const std::string& to_pin) {
    auto* block = find_block(block_name);
    if (!block) return Result<void, std::string>::err("Logic block '" + block_name + "' not found");

    auto& fcs = block->flow_connections;
    for (auto it = fcs.begin(); it != fcs.end(); ++it) {
        if (it->from.node_instance == from_node && it->from.pin_name == from_pin &&
            it->to.node_instance == to_node && it->to.pin_name == to_pin) {
            push_undo("remove flow " + from_node + "." + from_pin + " -> " + to_node + "." + to_pin);
            fcs.erase(it);
            return Result<void, std::string>::ok();
        }
    }
    return Result<void, std::string>::err("Flow connection not found");
}

Result<void, std::string> EditSession::add_link(const std::string& block_name,
                                                 const std::string& target_node, const std::string& target_pin,
                                                 const std::string& source_node, const std::string& source_pin) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    auto* block = find_block(block_name);
    if (!block) return Result<void, std::string>::err("Logic block '" + block_name + "' not found");

    auto err1 = check_block_scope(*g, block_name, target_node);
    if (!err1.empty()) return Result<void, std::string>::err(err1);
    auto err2 = check_block_scope(*g, block_name, source_node);
    if (!err2.empty()) return Result<void, std::string>::err(err2);

    push_undo("add link " + target_node + "." + target_pin + " = " + source_node +
              (source_pin.empty() ? "" : "." + source_pin));
    DataLink dl;
    dl.target = {target_node, target_pin};
    dl.source = {source_node, source_pin};
    block->data_links.push_back(std::move(dl));
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::remove_link(const std::string& block_name,
                                                    const std::string& target_node, const std::string& target_pin) {
    auto* block = find_block(block_name);
    if (!block) return Result<void, std::string>::err("Logic block '" + block_name + "' not found");

    auto& links = block->data_links;
    for (auto it = links.begin(); it != links.end(); ++it) {
        if (it->target.node_instance == target_node && it->target.pin_name == target_pin) {
            push_undo("remove link " + target_node + "." + target_pin);
            links.erase(it);
            return Result<void, std::string>::ok();
        }
    }
    return Result<void, std::string>::err("Data link not found");
}

// ─── Annotations (C# Attribute style) ──────────────────────────────

// Upserts an annotation on a node instance by name.
Result<void, std::string> EditSession::set_node_annotation(const std::string& instance_name, const Annotation& annot) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    for (auto& ni : g->node_instances) {
        if (ni.instance_name == instance_name) {
            push_undo("set annotation [" + annot.name + "] on " + instance_name);
            for (auto& a : ni.annotations) {
                if (a.name == annot.name) { a = annot; return Result<void, std::string>::ok(); }
            }
            ni.annotations.push_back(annot);
            return Result<void, std::string>::ok();
        }
    }
    return Result<void, std::string>::err("Node instance '" + instance_name + "' not found");
}

// Removes an annotation by name from a node instance.
Result<void, std::string> EditSession::remove_node_annotation(const std::string& instance_name, const std::string& annot_name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    for (auto& ni : g->node_instances) {
        if (ni.instance_name == instance_name) {
            auto& annots = ni.annotations;
            for (auto it = annots.begin(); it != annots.end(); ++it) {
                if (it->name == annot_name) {
                    push_undo("remove annotation [" + annot_name + "] from " + instance_name);
                    annots.erase(it);
                    return Result<void, std::string>::ok();
                }
            }
            return Result<void, std::string>::err("Annotation '" + annot_name + "' not found on " + instance_name);
        }
    }
    return Result<void, std::string>::err("Node instance '" + instance_name + "' not found");
}

// Upserts an annotation on a graph parameter by name.
Result<void, std::string> EditSession::set_param_annotation(const std::string& param_name, const Annotation& annot) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    for (auto& p : g->parameters) {
        if (p.name == param_name) {
            push_undo("set annotation [" + annot.name + "] on param " + param_name);
            for (auto& a : p.annotations) {
                if (a.name == annot.name) { a = annot; return Result<void, std::string>::ok(); }
            }
            p.annotations.push_back(annot);
            return Result<void, std::string>::ok();
        }
    }
    return Result<void, std::string>::err("Parameter '" + param_name + "' not found");
}

// Removes an annotation by name from a graph parameter.
Result<void, std::string> EditSession::remove_param_annotation(const std::string& param_name, const std::string& annot_name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    for (auto& p : g->parameters) {
        if (p.name == param_name) {
            auto& annots = p.annotations;
            for (auto it = annots.begin(); it != annots.end(); ++it) {
                if (it->name == annot_name) {
                    push_undo("remove annotation [" + annot_name + "] from param " + param_name);
                    annots.erase(it);
                    return Result<void, std::string>::ok();
                }
            }
            return Result<void, std::string>::err("Annotation '" + annot_name + "' not found on param " + param_name);
        }
    }
    return Result<void, std::string>::err("Parameter '" + param_name + "' not found");
}

Result<void, std::string> EditSession::set_block_annotation(const std::string& block_kind,
                                                            const std::string& block_name,
                                                            const Annotation& annot) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");

    auto set_on_block = [&](LogicBlock& block) -> Result<void, std::string> {
        push_undo("set annotation [" + annot.name + "] on " + block_kind + " " + block_name);
        for (auto& a : block.annotations) {
            if (a.name == annot.name) { a = annot; return Result<void, std::string>::ok(); }
        }
        block.annotations.push_back(annot);
        return Result<void, std::string>::ok();
    };

    if (block_kind == "event") {
        for (auto& ev : g->events) {
            if (ev.name == block_name) return set_on_block(ev);
        }
    } else if (block_kind == "function") {
        for (auto& fn : g->functions) {
            if (fn.name == block_name) return set_on_block(fn);
        }
    } else {
        return Result<void, std::string>::err("Logic block kind must be event or function");
    }

    return Result<void, std::string>::err("Logic block '" + block_name + "' not found");
}

Result<void, std::string> EditSession::remove_block_annotation(const std::string& block_kind,
                                                               const std::string& block_name,
                                                               const std::string& annot_name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");

    auto remove_from_block = [&](LogicBlock& block) -> Result<void, std::string> {
        auto& annots = block.annotations;
        for (auto it = annots.begin(); it != annots.end(); ++it) {
            if (it->name == annot_name) {
                push_undo("remove annotation [" + annot_name + "] from " + block_kind + " " + block_name);
                annots.erase(it);
                return Result<void, std::string>::ok();
            }
        }
        return Result<void, std::string>::err(
            "Annotation '" + annot_name + "' not found on " + block_kind + " " + block_name);
    };

    if (block_kind == "event") {
        for (auto& ev : g->events) {
            if (ev.name == block_name) return remove_from_block(ev);
        }
    } else if (block_kind == "function") {
        for (auto& fn : g->functions) {
            if (fn.name == block_name) return remove_from_block(fn);
        }
    } else {
        return Result<void, std::string>::err("Logic block kind must be event or function");
    }

    return Result<void, std::string>::err("Logic block '" + block_name + "' not found");
}

Result<void, std::string> EditSession::set_flow_annotation(const std::string& block_kind,
                                                           const std::string& block_name,
                                                           const std::string& from_node,
                                                           const std::string& from_pin,
                                                           const std::string& to_node,
                                                           const std::string& to_pin,
                                                           const Annotation& annot) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");

    auto set_on_block = [&](LogicBlock& block) -> Result<void, std::string> {
        for (auto& flow : block.flow_connections) {
            if (flow.from.node_instance == from_node && flow.from.pin_name == from_pin &&
                flow.to.node_instance == to_node && flow.to.pin_name == to_pin) {
                push_undo("set annotation [" + annot.name + "] on flow " + from_node + "." + from_pin +
                          " -> " + to_node + "." + to_pin);
                for (auto& a : flow.annotations) {
                    if (a.name == annot.name) { a = annot; return Result<void, std::string>::ok(); }
                }
                flow.annotations.push_back(annot);
                return Result<void, std::string>::ok();
            }
        }
        return Result<void, std::string>::err("Flow connection not found");
    };

    if (block_kind == "event") {
        for (auto& ev : g->events) {
            if (ev.name == block_name) return set_on_block(ev);
        }
    } else if (block_kind == "function") {
        for (auto& fn : g->functions) {
            if (fn.name == block_name) return set_on_block(fn);
        }
    } else {
        return Result<void, std::string>::err("Logic block kind must be event or function");
    }

    return Result<void, std::string>::err("Logic block '" + block_name + "' not found");
}

Result<void, std::string> EditSession::remove_flow_annotation(const std::string& block_kind,
                                                              const std::string& block_name,
                                                              const std::string& from_node,
                                                              const std::string& from_pin,
                                                              const std::string& to_node,
                                                              const std::string& to_pin,
                                                              const std::string& annot_name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");

    auto remove_from_block = [&](LogicBlock& block) -> Result<void, std::string> {
        for (auto& flow : block.flow_connections) {
            if (flow.from.node_instance == from_node && flow.from.pin_name == from_pin &&
                flow.to.node_instance == to_node && flow.to.pin_name == to_pin) {
                auto& annots = flow.annotations;
                for (auto it = annots.begin(); it != annots.end(); ++it) {
                    if (it->name == annot_name) {
                        push_undo("remove annotation [" + annot_name + "] from flow " + from_node + "." + from_pin +
                                  " -> " + to_node + "." + to_pin);
                        annots.erase(it);
                        return Result<void, std::string>::ok();
                    }
                }
                return Result<void, std::string>::err("Annotation '" + annot_name + "' not found on flow");
            }
        }
        return Result<void, std::string>::err("Flow connection not found");
    };

    if (block_kind == "event") {
        for (auto& ev : g->events) {
            if (ev.name == block_name) return remove_from_block(ev);
        }
    } else if (block_kind == "function") {
        for (auto& fn : g->functions) {
            if (fn.name == block_name) return remove_from_block(fn);
        }
    } else {
        return Result<void, std::string>::err("Logic block kind must be event or function");
    }

    return Result<void, std::string>::err("Logic block '" + block_name + "' not found");
}

Result<void, std::string> EditSession::set_link_annotation(const std::string& block_kind,
                                                           const std::string& block_name,
                                                           const std::string& target_node,
                                                           const std::string& target_pin,
                                                           const std::string& source_node,
                                                           const std::string& source_pin,
                                                           const Annotation& annot) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");

    auto source_label = source_node + (source_pin.empty() ? "" : "." + source_pin);
    auto set_on_block = [&](LogicBlock& block) -> Result<void, std::string> {
        for (auto& link : block.data_links) {
            if (link.target.node_instance == target_node && link.target.pin_name == target_pin &&
                link.source.node_instance == source_node && link.source.pin_name == source_pin) {
                push_undo("set annotation [" + annot.name + "] on link " + target_node + "." + target_pin +
                          " = " + source_label);
                for (auto& a : link.annotations) {
                    if (a.name == annot.name) { a = annot; return Result<void, std::string>::ok(); }
                }
                link.annotations.push_back(annot);
                return Result<void, std::string>::ok();
            }
        }
        return Result<void, std::string>::err("Data link not found");
    };

    if (block_kind == "event") {
        for (auto& ev : g->events) {
            if (ev.name == block_name) return set_on_block(ev);
        }
    } else if (block_kind == "function") {
        for (auto& fn : g->functions) {
            if (fn.name == block_name) return set_on_block(fn);
        }
    } else {
        return Result<void, std::string>::err("Logic block kind must be event or function");
    }

    return Result<void, std::string>::err("Logic block '" + block_name + "' not found");
}

Result<void, std::string> EditSession::remove_link_annotation(const std::string& block_kind,
                                                              const std::string& block_name,
                                                              const std::string& target_node,
                                                              const std::string& target_pin,
                                                              const std::string& source_node,
                                                              const std::string& source_pin,
                                                              const std::string& annot_name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");

    auto source_label = source_node + (source_pin.empty() ? "" : "." + source_pin);
    auto remove_from_block = [&](LogicBlock& block) -> Result<void, std::string> {
        for (auto& link : block.data_links) {
            if (link.target.node_instance == target_node && link.target.pin_name == target_pin &&
                link.source.node_instance == source_node && link.source.pin_name == source_pin) {
                auto& annots = link.annotations;
                for (auto it = annots.begin(); it != annots.end(); ++it) {
                    if (it->name == annot_name) {
                        push_undo("remove annotation [" + annot_name + "] from link " + target_node + "." +
                                  target_pin + " = " + source_label);
                        annots.erase(it);
                        return Result<void, std::string>::ok();
                    }
                }
                return Result<void, std::string>::err("Annotation '" + annot_name + "' not found on link");
            }
        }
        return Result<void, std::string>::err("Data link not found");
    };

    if (block_kind == "event") {
        for (auto& ev : g->events) {
            if (ev.name == block_name) return remove_from_block(ev);
        }
    } else if (block_kind == "function") {
        for (auto& fn : g->functions) {
            if (fn.name == block_name) return remove_from_block(fn);
        }
    } else {
        return Result<void, std::string>::err("Logic block kind must be event or function");
    }

    return Result<void, std::string>::err("Logic block '" + block_name + "' not found");
}

// Upserts an annotation on the active graph.
Result<void, std::string> EditSession::set_graph_annotation(const Annotation& annot) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    push_undo("set graph annotation [" + annot.name + "]");
    for (auto& a : g->annotations) {
        if (a.name == annot.name) { a = annot; return Result<void, std::string>::ok(); }
    }
    g->annotations.push_back(annot);
    return Result<void, std::string>::ok();
}

// Removes an annotation by name from the active graph.
Result<void, std::string> EditSession::remove_graph_annotation(const std::string& annot_name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    auto& annots = g->annotations;
    for (auto it = annots.begin(); it != annots.end(); ++it) {
        if (it->name == annot_name) {
            push_undo("remove graph annotation [" + annot_name + "]");
            annots.erase(it);
            return Result<void, std::string>::ok();
        }
    }
    return Result<void, std::string>::err("Graph annotation '" + annot_name + "' not found");
}

Result<void, std::string> EditSession::set_import_annotation(const std::string& path, const Annotation& annot) {
    auto key = import_key(path, module_base_dir(module_));
    for (auto& imp : module_.imports) {
        if (imp.path == path || import_key(imp.path, module_base_dir(module_)) == key) {
            push_module_undo("set import annotation [" + annot.name + "] on " + imp.path);
            upsert_annotation(imp.annotations, annot);
            return Result<void, std::string>::ok();
        }
    }
    return Result<void, std::string>::err("Import '" + path + "' not found");
}

Result<void, std::string> EditSession::remove_import_annotation(const std::string& path,
                                                                const std::string& annot_name) {
    auto key = import_key(path, module_base_dir(module_));
    for (auto& imp : module_.imports) {
        if (imp.path == path || import_key(imp.path, module_base_dir(module_)) == key) {
            if (!has_annotation(imp.annotations, annot_name)) {
                return Result<void, std::string>::err("Import annotation '" + annot_name + "' not found");
            }
            push_module_undo("remove import annotation [" + annot_name + "] from " + imp.path);
            erase_annotation(imp.annotations, annot_name);
            return Result<void, std::string>::ok();
        }
    }
    return Result<void, std::string>::err("Import '" + path + "' not found");
}

Result<void, std::string> EditSession::set_let_annotation(const std::string& name, const Annotation& annot) {
    for (auto& let_decl : module_.top_level_lets) {
        if (let_decl.name == name) {
            push_module_undo("set let annotation [" + annot.name + "] on " + name);
            upsert_annotation(let_decl.annotations, annot);
            return Result<void, std::string>::ok();
        }
    }
    return Result<void, std::string>::err("Let '" + name + "' not found");
}

Result<void, std::string> EditSession::remove_let_annotation(const std::string& name,
                                                             const std::string& annot_name) {
    for (auto& let_decl : module_.top_level_lets) {
        if (let_decl.name == name) {
            if (!has_annotation(let_decl.annotations, annot_name)) {
                return Result<void, std::string>::err("Let annotation '" + annot_name + "' not found");
            }
            push_module_undo("remove let annotation [" + annot_name + "] from " + name);
            erase_annotation(let_decl.annotations, annot_name);
            return Result<void, std::string>::ok();
        }
    }
    return Result<void, std::string>::err("Let '" + name + "' not found");
}

// ─── Generate block (legacy) ──────────────────────────────────────

Result<void, std::string> EditSession::add_comment(const std::string& instance_name, const std::string& text) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    push_undo("add comment '" + instance_name + "'");
    if (!g->generate) g->generate = GenerateBlock{};
    g->generate->comments.push_back({instance_name, text});
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::add_meta(const std::string& scope, const std::string& node,
                                                 const std::string& prop, const std::string& value) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    push_undo("add meta " + scope + ":" + node + "." + prop);
    if (!g->generate) g->generate = GenerateBlock{};
    g->generate->metadata.push_back({scope, node, prop, value});
    return Result<void, std::string>::ok();
}

template <typename T, typename Match>
static Result<T*, std::string> select_generate_item(std::vector<T>& items, Match match, size_t occurrence,
                                                    const std::string& description) {
    size_t matched = 0;
    T* unique = nullptr;
    for (auto& item : items) {
        if (!match(item)) continue;
        ++matched;
        if (occurrence > 0 && matched == occurrence) {
            return Result<T*, std::string>::ok(&item);
        }
        if (occurrence == 0) {
            if (unique) {
                return Result<T*, std::string>::err(
                    "Generate " + description + " is ambiguous; pass #<occurrence>");
            }
            unique = &item;
        }
    }

    if (occurrence == 0 && unique) {
        return Result<T*, std::string>::ok(unique);
    }
    if (matched == 0) {
        return Result<T*, std::string>::err("Generate " + description + " not found");
    }
    return Result<T*, std::string>::err(
        "Generate " + description + " occurrence #" + std::to_string(occurrence) + " not found");
}

template <typename T, typename Match>
static Result<size_t, std::string> select_generate_item_index(const std::vector<T>& items, Match match,
                                                              size_t occurrence,
                                                              const std::string& description) {
    size_t matched = 0;
    size_t unique_index = 0;
    bool has_unique = false;
    for (size_t i = 0; i < items.size(); ++i) {
        if (!match(items[i])) continue;
        ++matched;
        if (occurrence > 0 && matched == occurrence) {
            return Result<size_t, std::string>::ok(i);
        }
        if (occurrence == 0) {
            if (has_unique) {
                return Result<size_t, std::string>::err(
                    "Generate " + description + " is ambiguous; pass #<occurrence>");
            }
            unique_index = i;
            has_unique = true;
        }
    }

    if (occurrence == 0 && has_unique) {
        return Result<size_t, std::string>::ok(unique_index);
    }
    if (matched == 0) {
        return Result<size_t, std::string>::err("Generate " + description + " not found");
    }
    return Result<size_t, std::string>::err(
        "Generate " + description + " occurrence #" + std::to_string(occurrence) + " not found");
}

static void clear_empty_generate(Graph& graph) {
    if (graph.generate && graph.generate->comments.empty() && graph.generate->metadata.empty()) {
        graph.generate.reset();
    }
}

template <typename T>
static Result<void, std::string> move_generate_item(std::vector<T>& items, size_t index, bool move_up,
                                                    const std::string& description) {
    if (items.empty()) {
        return Result<void, std::string>::err("Generate " + description + " not found");
    }
    if (move_up) {
        if (index == 0) {
            return Result<void, std::string>::err("Generate " + description + " is already first");
        }
        std::swap(items[index], items[index - 1]);
    } else {
        if (index + 1 >= items.size()) {
            return Result<void, std::string>::err("Generate " + description + " is already last");
        }
        std::swap(items[index], items[index + 1]);
    }
    return Result<void, std::string>::ok();
}

static void upsert_annotation(std::vector<Annotation>& annotations, const Annotation& annot) {
    for (auto& existing : annotations) {
        if (existing.name == annot.name) {
            existing = annot;
            return;
        }
    }
    annotations.push_back(annot);
}

static bool erase_annotation(std::vector<Annotation>& annotations, const std::string& annot_name) {
    for (auto it = annotations.begin(); it != annotations.end(); ++it) {
        if (it->name == annot_name) {
            annotations.erase(it);
            return true;
        }
    }
    return false;
}

static bool has_annotation(const std::vector<Annotation>& annotations, const std::string& annot_name) {
    return std::any_of(annotations.begin(), annotations.end(), [&](const Annotation& annot) {
        return annot.name == annot_name;
    });
}

Result<void, std::string> EditSession::remove_comment(const std::string& instance_name,
                                                       const std::string& text,
                                                       size_t occurrence) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (!g->generate) return Result<void, std::string>::err("Generate block not found");

    auto selected = select_generate_item_index(g->generate->comments, [&](const GenerateComment& comment) {
        return comment.instance_name == instance_name && comment.text == text;
    }, occurrence, "comment '" + instance_name + "'");
    if (selected.is_err()) return Result<void, std::string>::err(selected.error());

    push_undo("remove comment '" + instance_name + "'");
    g->generate->comments.erase(g->generate->comments.begin() + static_cast<std::ptrdiff_t>(selected.value()));
    clear_empty_generate(*g);
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::remove_meta(const std::string& scope,
                                                    const std::string& node,
                                                    const std::string& prop,
                                                    const std::string& value,
                                                    size_t occurrence) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (!g->generate) return Result<void, std::string>::err("Generate block not found");

    auto selected = select_generate_item_index(g->generate->metadata, [&](const GenerateMetadata& metadata) {
        return metadata.scope == scope && metadata.node == node && metadata.property == prop && metadata.value == value;
    }, occurrence, "metadata '" + scope + ":" + node + "." + prop + "'");
    if (selected.is_err()) return Result<void, std::string>::err(selected.error());

    push_undo("remove meta " + scope + ":" + node + "." + prop);
    g->generate->metadata.erase(g->generate->metadata.begin() + static_cast<std::ptrdiff_t>(selected.value()));
    clear_empty_generate(*g);
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::move_comment(const std::string& instance_name,
                                                     const std::string& text,
                                                     size_t occurrence,
                                                     bool move_up) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (!g->generate) return Result<void, std::string>::err("Generate block not found");

    auto selected = select_generate_item_index(g->generate->comments, [&](const GenerateComment& comment) {
        return comment.instance_name == instance_name && comment.text == text;
    }, occurrence, "comment '" + instance_name + "'");
    if (selected.is_err()) return Result<void, std::string>::err(selected.error());

    const auto description = "comment '" + instance_name + "'";
    if (move_up && selected.value() == 0) {
        return Result<void, std::string>::err("Generate " + description + " is already first");
    }
    if (!move_up && selected.value() + 1 >= g->generate->comments.size()) {
        return Result<void, std::string>::err("Generate " + description + " is already last");
    }

    push_undo(std::string("move comment '") + instance_name + (move_up ? "' up" : "' down"));
    return move_generate_item(g->generate->comments, selected.value(), move_up, description);
}

Result<void, std::string> EditSession::move_meta(const std::string& scope,
                                                  const std::string& node,
                                                  const std::string& prop,
                                                  const std::string& value,
                                                  size_t occurrence,
                                                  bool move_up) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (!g->generate) return Result<void, std::string>::err("Generate block not found");

    auto selected = select_generate_item_index(g->generate->metadata, [&](const GenerateMetadata& metadata) {
        return metadata.scope == scope && metadata.node == node && metadata.property == prop && metadata.value == value;
    }, occurrence, "metadata '" + scope + ":" + node + "." + prop + "'");
    if (selected.is_err()) return Result<void, std::string>::err(selected.error());

    const auto description = "metadata '" + scope + ":" + node + "." + prop + "'";
    if (move_up && selected.value() == 0) {
        return Result<void, std::string>::err("Generate " + description + " is already first");
    }
    if (!move_up && selected.value() + 1 >= g->generate->metadata.size()) {
        return Result<void, std::string>::err("Generate " + description + " is already last");
    }

    push_undo("move meta " + scope + ":" + node + "." + prop + (move_up ? " up" : " down"));
    return move_generate_item(g->generate->metadata, selected.value(), move_up, description);
}

Result<void, std::string> EditSession::rename_comment(const std::string& instance_name,
                                                       const std::string& text,
                                                       size_t occurrence,
                                                       const std::string& new_text) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (!g->generate) return Result<void, std::string>::err("Generate block not found");

    auto selected = select_generate_item(g->generate->comments, [&](const GenerateComment& comment) {
        return comment.instance_name == instance_name && comment.text == text;
    }, occurrence, "comment '" + instance_name + "'");
    if (selected.is_err()) return Result<void, std::string>::err(selected.error());

    push_undo("rename comment '" + instance_name + "'");
    selected.value()->text = new_text;
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::rename_meta(const std::string& scope,
                                                    const std::string& node,
                                                    const std::string& prop,
                                                    const std::string& value,
                                                    size_t occurrence,
                                                    const std::string& new_value) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (!g->generate) return Result<void, std::string>::err("Generate block not found");

    auto selected = select_generate_item(g->generate->metadata, [&](const GenerateMetadata& metadata) {
        return metadata.scope == scope && metadata.node == node && metadata.property == prop && metadata.value == value;
    }, occurrence, "metadata '" + scope + ":" + node + "." + prop + "'");
    if (selected.is_err()) return Result<void, std::string>::err(selected.error());

    push_undo("rename meta " + scope + ":" + node + "." + prop);
    selected.value()->value = new_value;
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::rename_meta_ref(const std::string& scope,
                                                        const std::string& node,
                                                        const std::string& prop,
                                                        const std::string& value,
                                                        size_t occurrence,
                                                        const std::string& new_scope,
                                                        const std::string& new_node,
                                                        const std::string& new_prop) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (!g->generate) return Result<void, std::string>::err("Generate block not found");
    if (new_scope.empty() || new_node.empty() || new_prop.empty()) {
        return Result<void, std::string>::err("Generate metadata reference must be scope:node.property");
    }

    auto selected = select_generate_item(g->generate->metadata, [&](const GenerateMetadata& metadata) {
        return metadata.scope == scope && metadata.node == node && metadata.property == prop && metadata.value == value;
    }, occurrence, "metadata '" + scope + ":" + node + "." + prop + "'");
    if (selected.is_err()) return Result<void, std::string>::err(selected.error());

    push_undo("rename meta ref " + scope + ":" + node + "." + prop);
    selected.value()->scope = new_scope;
    selected.value()->node = new_node;
    selected.value()->property = new_prop;
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::set_generate_comment_annotation(const std::string& instance_name,
                                                                        const std::string& text,
                                                                        size_t occurrence,
                                                                        const Annotation& annot) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (!g->generate) return Result<void, std::string>::err("Generate block not found");

    auto selected = select_generate_item(g->generate->comments, [&](const GenerateComment& comment) {
        return comment.instance_name == instance_name && comment.text == text;
    }, occurrence, "comment '" + instance_name + "'");
    if (selected.is_err()) return Result<void, std::string>::err(selected.error());

    push_undo("set annotation [" + annot.name + "] on generate comment " + instance_name);
    upsert_annotation(selected.value()->annotations, annot);
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::remove_generate_comment_annotation(const std::string& instance_name,
                                                                           const std::string& text,
                                                                           size_t occurrence,
                                                                           const std::string& annot_name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (!g->generate) return Result<void, std::string>::err("Generate block not found");

    auto selected = select_generate_item(g->generate->comments, [&](const GenerateComment& comment) {
        return comment.instance_name == instance_name && comment.text == text;
    }, occurrence, "comment '" + instance_name + "'");
    if (selected.is_err()) return Result<void, std::string>::err(selected.error());

    if (!has_annotation(selected.value()->annotations, annot_name)) {
        return Result<void, std::string>::err(
            "Annotation '" + annot_name + "' not found on generate comment " + instance_name);
    }
    push_undo("remove annotation [" + annot_name + "] from generate comment " + instance_name);
    erase_annotation(selected.value()->annotations, annot_name);
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::set_generate_metadata_annotation(const std::string& scope,
                                                                         const std::string& node,
                                                                         const std::string& prop,
                                                                         const std::string& value,
                                                                         size_t occurrence,
                                                                         const Annotation& annot) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (!g->generate) return Result<void, std::string>::err("Generate block not found");

    auto selected = select_generate_item(g->generate->metadata, [&](const GenerateMetadata& metadata) {
        return metadata.scope == scope && metadata.node == node && metadata.property == prop && metadata.value == value;
    }, occurrence, "metadata '" + scope + ":" + node + "." + prop + "'");
    if (selected.is_err()) return Result<void, std::string>::err(selected.error());

    push_undo("set annotation [" + annot.name + "] on generate metadata " + scope + ":" + node + "." + prop);
    upsert_annotation(selected.value()->annotations, annot);
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::remove_generate_metadata_annotation(const std::string& scope,
                                                                            const std::string& node,
                                                                            const std::string& prop,
                                                                            const std::string& value,
                                                                            size_t occurrence,
                                                                            const std::string& annot_name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    if (!g->generate) return Result<void, std::string>::err("Generate block not found");

    auto selected = select_generate_item(g->generate->metadata, [&](const GenerateMetadata& metadata) {
        return metadata.scope == scope && metadata.node == node && metadata.property == prop && metadata.value == value;
    }, occurrence, "metadata '" + scope + ":" + node + "." + prop + "'");
    if (selected.is_err()) return Result<void, std::string>::err(selected.error());

    if (!has_annotation(selected.value()->annotations, annot_name)) {
        return Result<void, std::string>::err(
            "Annotation '" + annot_name + "' not found on generate metadata " + scope + ":" + node + "." + prop);
    }
    push_undo("remove annotation [" + annot_name + "] from generate metadata " + scope + ":" + node + "." + prop);
    erase_annotation(selected.value()->annotations, annot_name);
    return Result<void, std::string>::ok();
}

// ─── Undo / Redo ───────────────────────────────────────────────────

Result<std::string, std::string> EditSession::undo() {
    if (undo_stack_.empty()) return Result<std::string, std::string>::err("Nothing to undo");

    auto snapshot = std::move(undo_stack_.back());
    undo_stack_.pop_back();

    if (snapshot.scope == EditSnapshot::Scope::Module) {
        EditSnapshot redo;
        redo.scope = EditSnapshot::Scope::Module;
        redo.module = module_;
        redo.active_index = active_;
        redo.description = snapshot.description;
        redo.asset_source = asset_source_;
        redo_stack_.push_back(std::move(redo));

        module_ = std::move(snapshot.module);
        active_ = snapshot.active_index;
        asset_source_ = std::move(snapshot.asset_source);
        if (active_ >= static_cast<int>(module_.graphs.size())) active_ = module_.graphs.empty() ? -1 : 0;
        refresh_module_graph_node_definitions(env_, module_);
        dirty_ = true;
        return Result<std::string, std::string>::ok(redo_stack_.back().description);
    }

    const int index = snapshot.active_index;
    if (index < 0 || index >= static_cast<int>(module_.graphs.size())) {
        return Result<std::string, std::string>::err("Undo graph no longer exists");
    }
    asset_source_.reset();

    EditSnapshot redo;
    redo.scope = EditSnapshot::Scope::Graph;
    redo.graph = module_.graphs[index];
    redo.active_index = index;
    redo.description = snapshot.description;
    redo_stack_.push_back(std::move(redo));

    module_.graphs[index] = std::move(snapshot.graph);
    active_ = index;
    refresh_graph_node_definition(env_, module_.graphs[index]);
    dirty_ = true;
    return Result<std::string, std::string>::ok(redo_stack_.back().description);
}

Result<std::string, std::string> EditSession::redo() {
    if (redo_stack_.empty()) return Result<std::string, std::string>::err("Nothing to redo");

    auto snapshot = std::move(redo_stack_.back());
    redo_stack_.pop_back();

    if (snapshot.scope == EditSnapshot::Scope::Module) {
        EditSnapshot undo;
        undo.scope = EditSnapshot::Scope::Module;
        undo.module = module_;
        undo.active_index = active_;
        undo.description = snapshot.description;
        undo.asset_source = asset_source_;
        undo_stack_.push_back(std::move(undo));

        module_ = std::move(snapshot.module);
        active_ = snapshot.active_index;
        asset_source_ = std::move(snapshot.asset_source);
        if (active_ >= static_cast<int>(module_.graphs.size())) active_ = module_.graphs.empty() ? -1 : 0;
        refresh_module_graph_node_definitions(env_, module_);
        dirty_ = true;
        return Result<std::string, std::string>::ok(undo_stack_.back().description);
    }

    const int index = snapshot.active_index;
    if (index < 0 || index >= static_cast<int>(module_.graphs.size())) {
        return Result<std::string, std::string>::err("Redo graph no longer exists");
    }
    asset_source_.reset();

    EditSnapshot undo;
    undo.scope = EditSnapshot::Scope::Graph;
    undo.graph = module_.graphs[index];
    undo.active_index = index;
    undo.description = snapshot.description;
    undo_stack_.push_back(std::move(undo));

    module_.graphs[index] = std::move(snapshot.graph);
    active_ = index;
    refresh_graph_node_definition(env_, module_.graphs[index]);
    dirty_ = true;
    return Result<std::string, std::string>::ok(undo_stack_.back().description);
}

std::vector<std::string> EditSession::undo_history() const {
    std::vector<std::string> result;
    for (auto it = undo_stack_.rbegin(); it != undo_stack_.rend(); ++it)
        result.push_back(it->description);
    return result;
}

std::vector<std::string> EditSession::redo_history() const {
    std::vector<std::string> result;
    for (auto it = redo_stack_.rbegin(); it != redo_stack_.rend(); ++it)
        result.push_back(it->description);
    return result;
}

// ─── Query / Output ────────────────────────────────────────────────

std::string EditSession::emit() const {
    if (asset_source_) return *asset_source_;
    Emitter emitter;
    return emitter.emit(module_);
}

std::string EditSession::emit_active() const {
    auto* g = active_graph();
    if (!g) return "";
    Emitter emitter;
    return emitter.emit_graph(*g);
}

std::optional<EditGraph> EditSession::build_edit_graph() const {
    auto* g = active_graph();
    if (!g) return std::nullopt;
    return EditGraph::build(*g, env_);
}

std::vector<Diagnostic> EditSession::validate_graph(const Graph& graph) const {
    auto eg = EditGraph::build(graph, env_);
    auto diagnostics = eg.validate();

    struct DuplicateAction {
        std::string block_kind;
        std::string block_name;
        std::string connection_kind;
        std::string from_node;
        std::string from_pin;
        std::string to_node;
        std::string to_pin;
        std::string command;
    };

    std::vector<DuplicateAction> actions;

    auto collect_flow_actions = [&](const LogicBlock& block, const std::string& block_kind) {
        for (auto& a : block.flow_connections) {
            int count = 0;
            for (auto& b : block.flow_connections) {
                if (a.from.node_instance == b.from.node_instance &&
                    a.from.pin_name == b.from.pin_name &&
                    a.to.node_instance == b.to.node_instance &&
                    a.to.pin_name == b.to.pin_name) {
                    count++;
                }
            }
            if (count > 1) {
                actions.push_back({
                    block_kind,
                    block.name,
                    "exec",
                    a.from.node_instance,
                    a.from.pin_name,
                    a.to.node_instance,
                    a.to.pin_name,
                    "unflow " + a.from.node_instance + "." + a.from.pin_name +
                        " " + a.to.node_instance + "." + a.to.pin_name
                });
            }
        }
    };

    auto collect_link_actions = [&](const LogicBlock& block, const std::string& block_kind) {
        for (auto& a : block.data_links) {
            int count = 0;
            for (auto& b : block.data_links) {
                if (a.target.node_instance == b.target.node_instance &&
                    a.target.pin_name == b.target.pin_name &&
                    a.source.node_instance == b.source.node_instance &&
                    a.source.pin_name == b.source.pin_name) {
                    count++;
                }
            }
            if (count > 1) {
                actions.push_back({
                    block_kind,
                    block.name,
                    "data",
                    a.source.node_instance,
                    a.source.pin_name,
                    a.target.node_instance,
                    a.target.pin_name,
                    "unlink " + a.target.node_instance + "." + a.target.pin_name
                });
            }
        }
    };

    for (auto& ev : graph.events) {
        collect_flow_actions(ev, "event");
        collect_link_actions(ev, "event");
    }
    for (auto& fn : graph.functions) {
        collect_flow_actions(fn, "function");
        collect_link_actions(fn, "function");
    }

    for (auto& diag : diagnostics) {
        if (diag.code != "GS_GRAPH_DUPLICATE_CONNECTION") continue;
        for (auto& action : actions) {
            if (diag.target.connection_kind != action.connection_kind ||
                diag.target.node_instance != action.from_node ||
                diag.target.pin_name != action.from_pin) {
                continue;
            }
            diag.target.block_kind = action.block_kind;
            diag.target.block_name = action.block_name;
            diag.actions.push_back({
                "Remove duplicate connection",
                "quickfix",
                action.command
            });
            break;
        }
    }

    std::unordered_map<std::string, const NodeDefinition*> node_defs;
    node_defs.reserve(graph.node_instances.size());
    for (auto& ni : graph.node_instances) {
        node_defs[ni.instance_name] = env_.nodes().find(ni.type_name);
    }

    std::unordered_map<std::string, const GraphParameter*> graph_params;
    graph_params.reserve(graph.parameters.size());
    for (auto& p : graph.parameters) {
        graph_params[p.name] = &p;
    }

    const GraphSchema* graph_schema = nullptr;
    if (graph.base_type) {
        graph_schema = env_.schemas().find(*graph.base_type);
    }
    const bool strict_type_match = graph_schema && graph_schema->connection_policy.strict_type_match;

    auto endpoint_ref = [](const std::string& node, const std::string& pin) {
        return pin.empty() ? node : node + "." + pin;
    };

    auto pin_kind_name = [](PinKind kind) {
        return kind == PinKind::Exec ? "exec" : "data";
    };

    auto pin_direction_name = [](PinDirection direction) {
        return direction == PinDirection::Input ? "input" : "output";
    };

    auto add_diagnostic = [&](Severity severity,
                              const std::string& message,
                              const std::string& context,
                              const std::string& code,
                              SourceRange range,
                              const std::string& hint,
                              DiagnosticTarget target,
                              DiagnosticAction action) {
        Diagnostic diag{severity, message, context, code, range, hint, std::move(target)};
        diag.actions.push_back(std::move(action));
        diagnostics.push_back(std::move(diag));
    };

    auto find_endpoint_pin = [&](const PinAddress& endpoint) -> const PinDefinition* {
        if (endpoint.node_instance == "context") return nullptr;
        auto it = node_defs.find(endpoint.node_instance);
        if (it == node_defs.end() || !it->second) return nullptr;
        return it->second->find_pin(endpoint.pin_name);
    };

    auto endpoint_has_node_definition = [&](const PinAddress& endpoint) {
        if (endpoint.node_instance == "context") return true;
        auto it = node_defs.find(endpoint.node_instance);
        return it != node_defs.end() && it->second != nullptr;
    };

    auto report_missing_pin = [&](const std::string& block_kind,
                                  const std::string& block_name,
                                  const std::string& side,
                                  const std::string& connection_kind,
                                  const PinAddress& endpoint,
                                  SourceRange range,
                                  const std::string& command) {
        DiagnosticTarget target;
        target.graph = graph.name;
        target.block_kind = block_kind;
        target.block_name = block_name;
        target.node_instance = endpoint.node_instance;
        target.pin_name = endpoint.pin_name;
        target.reference = endpoint_ref(endpoint.node_instance, endpoint.pin_name);
        target.connection_kind = connection_kind;
        const std::string side_label = side == "source" ? "source" : "target";
        add_diagnostic(
            Severity::Error,
            "Connection references missing " + side_label + " pin",
            target.reference,
            side == "source" ? "GS_GRAPH_DANGLING_SOURCE_PIN" : "GS_GRAPH_DANGLING_TARGET_PIN",
            range,
            "Remove the dangling connection or reconnect it to an existing pin.",
            std::move(target),
            {"Remove dangling connection", "quickfix", command}
        );
    };

    auto report_missing_parameter = [&](const std::string& block_kind,
                                        const std::string& block_name,
                                        const DataLink& link) {
        DiagnosticTarget target;
        target.graph = graph.name;
        target.block_kind = block_kind;
        target.block_name = block_name;
        target.parameter_name = link.source.node_instance;
        target.reference = link.source.node_instance;
        target.connection_kind = "data";
        add_diagnostic(
            Severity::Error,
            "Data link references missing graph parameter",
            target.reference,
            "GS_GRAPH_DANGLING_PARAMETER",
            link.source_endpoint_range,
            "Remove the dangling data link or reconnect it to an existing graph parameter.",
            std::move(target),
            {"Remove dangling data link", "quickfix",
             "unlink " + link.target.node_instance + "." + link.target.pin_name}
        );
    };

    auto report_pin_kind_mismatch = [&](const std::string& block_kind,
                                        const std::string& block_name,
                                        const std::string& connection_kind,
                                        const PinAddress& endpoint,
                                        const PinDefinition& pin,
                                        PinKind expected_kind,
                                        SourceRange range,
                                        const std::string& command) {
        DiagnosticTarget target;
        target.graph = graph.name;
        target.block_kind = block_kind;
        target.block_name = block_name;
        target.node_instance = endpoint.node_instance;
        target.pin_name = endpoint.pin_name;
        target.reference = endpoint_ref(endpoint.node_instance, endpoint.pin_name);
        target.connection_kind = connection_kind;
        add_diagnostic(
            Severity::Error,
            "Connection pin kind mismatch",
            target.reference,
            "GS_GRAPH_PIN_KIND_MISMATCH",
            range,
            "Expected a " + std::string(pin_kind_name(expected_kind)) + " pin here, but found a " +
                pin_kind_name(pin.kind) + " pin.",
            std::move(target),
            {"Remove invalid connection", "quickfix", command}
        );
    };

    auto report_pin_direction_mismatch = [&](const std::string& block_kind,
                                             const std::string& block_name,
                                             const std::string& connection_kind,
                                             const PinAddress& endpoint,
                                             const PinDefinition& pin,
                                             PinDirection expected_direction,
                                             SourceRange range,
                                             const std::string& command) {
        DiagnosticTarget target;
        target.graph = graph.name;
        target.block_kind = block_kind;
        target.block_name = block_name;
        target.node_instance = endpoint.node_instance;
        target.pin_name = endpoint.pin_name;
        target.reference = endpoint_ref(endpoint.node_instance, endpoint.pin_name);
        target.connection_kind = connection_kind;
        add_diagnostic(
            Severity::Error,
            "Connection pin direction mismatch",
            target.reference,
            "GS_GRAPH_PIN_DIRECTION_MISMATCH",
            range,
            "Expected a " + std::string(pin_direction_name(expected_direction)) +
                " pin here, but found an " + pin_direction_name(pin.direction) + " pin.",
            std::move(target),
            {"Remove invalid connection", "quickfix", command}
        );
    };

    auto report_pin_type_mismatch = [&](const std::string& block_kind,
                                        const std::string& block_name,
                                        const PinAddress& target_endpoint,
                                        const std::string& source_ref,
                                        const std::string& source_type,
                                        const std::string& target_type,
                                        SourceRange range,
                                        const std::string& command) {
        DiagnosticTarget target;
        target.graph = graph.name;
        target.block_kind = block_kind;
        target.block_name = block_name;
        target.node_instance = target_endpoint.node_instance;
        target.pin_name = target_endpoint.pin_name;
        target.reference = source_ref + "->" + endpoint_ref(target_endpoint.node_instance, target_endpoint.pin_name);
        target.connection_kind = "data";
        add_diagnostic(
            Severity::Error,
            "Data link type mismatch",
            target.reference,
            "GS_GRAPH_PIN_TYPE_MISMATCH",
            range,
            "Expected type '" + target_type + "' but source has type '" + source_type + "'.",
            std::move(target),
            {"Remove invalid data link", "quickfix", command}
        );
    };

    auto validate_endpoint_pin = [&](const std::string& block_kind,
                                     const std::string& block_name,
                                     const std::string& connection_kind,
                                     const PinAddress& endpoint,
                                     const PinDefinition* pin,
                                     PinKind expected_kind,
                                     PinDirection expected_direction,
                                     SourceRange range,
                                     const std::string& command) {
        if (!pin) return false;
        if (pin->kind != expected_kind) {
            report_pin_kind_mismatch(block_kind, block_name, connection_kind, endpoint, *pin,
                                     expected_kind, range, command);
            return false;
        }
        if (pin->direction != expected_direction) {
            report_pin_direction_mismatch(block_kind, block_name, connection_kind, endpoint, *pin,
                                          expected_direction, range, command);
            return false;
        }
        return true;
    };

    auto validate_flow_block = [&](const LogicBlock& block, const std::string& block_kind) {
        for (auto& fc : block.flow_connections) {
            const std::string command = "unflow " + fc.from.node_instance + "." + fc.from.pin_name +
                                        " " + fc.to.node_instance + "." + fc.to.pin_name;
            if (fc.from.node_instance != "context" && endpoint_has_node_definition(fc.from) &&
                !find_endpoint_pin(fc.from)) {
                report_missing_pin(block_kind, block.name, "source", "exec", fc.from,
                                   fc.from_endpoint_range, command);
            }
            if (fc.to.node_instance != "context" && endpoint_has_node_definition(fc.to) &&
                !find_endpoint_pin(fc.to)) {
                report_missing_pin(block_kind, block.name, "target", "exec", fc.to,
                                   fc.to_endpoint_range, command);
            }
            if (fc.from.node_instance != "context") {
                validate_endpoint_pin(block_kind, block.name, "exec", fc.from, find_endpoint_pin(fc.from),
                                      PinKind::Exec, PinDirection::Output,
                                      fc.from_endpoint_range, command);
            }
            if (fc.to.node_instance != "context") {
                validate_endpoint_pin(block_kind, block.name, "exec", fc.to, find_endpoint_pin(fc.to),
                                      PinKind::Exec, PinDirection::Input,
                                      fc.to_endpoint_range, command);
            }
        }
    };

    auto validate_data_block = [&](const LogicBlock& block, const std::string& block_kind) {
        for (auto& dl : block.data_links) {
            const std::string unlink_command = "unlink " + dl.target.node_instance + "." + dl.target.pin_name;
            const PinDefinition* target_pin = find_endpoint_pin(dl.target);
            if (dl.target.node_instance != "context" && endpoint_has_node_definition(dl.target) &&
                !target_pin) {
                report_missing_pin(block_kind, block.name, "target", "data", dl.target,
                                   dl.target_endpoint_range, unlink_command);
            }
            const bool target_is_valid_data_input =
                dl.target.node_instance == "context" ||
                validate_endpoint_pin(block_kind, block.name, "data", dl.target, target_pin,
                                      PinKind::Data, PinDirection::Input,
                                      dl.target_endpoint_range, unlink_command);

            if (dl.source.pin_name.empty()) {
                const GraphParameter* source_param = nullptr;
                if (dl.source.node_instance != "context" &&
                    graph_params.find(dl.source.node_instance) != graph_params.end()) {
                    source_param = graph_params[dl.source.node_instance];
                }
                if (dl.source.node_instance != "context" && !source_param) {
                    report_missing_parameter(block_kind, block.name, dl);
                } else if (strict_type_match && source_param && target_pin && target_is_valid_data_input &&
                           !source_param->type_name.empty() && !target_pin->type_name.empty() &&
                           source_param->type_name != target_pin->type_name) {
                    report_pin_type_mismatch(block_kind, block.name, dl.target, dl.source.node_instance,
                                             source_param->type_name, target_pin->type_name,
                                             dl.source_endpoint_range, unlink_command);
                }
                continue;
            }

            PinAddress source_endpoint{dl.source.node_instance, dl.source.pin_name};
            const PinDefinition* source_pin = find_endpoint_pin(source_endpoint);
            if (source_endpoint.node_instance != "context" &&
                endpoint_has_node_definition(source_endpoint) &&
                !source_pin) {
                report_missing_pin(block_kind, block.name, "source", "data", source_endpoint,
                                   dl.source_endpoint_range, unlink_command);
            }
            const bool source_is_valid_data_output =
                source_endpoint.node_instance == "context" ||
                validate_endpoint_pin(block_kind, block.name, "data", source_endpoint, source_pin,
                                      PinKind::Data, PinDirection::Output,
                                      dl.source_endpoint_range, unlink_command);
            if (strict_type_match && source_pin && target_pin &&
                source_is_valid_data_output && target_is_valid_data_input &&
                !source_pin->type_name.empty() && !target_pin->type_name.empty() &&
                source_pin->type_name != target_pin->type_name) {
                report_pin_type_mismatch(block_kind, block.name, dl.target,
                                         endpoint_ref(source_endpoint.node_instance, source_endpoint.pin_name),
                                         source_pin->type_name, target_pin->type_name,
                                         dl.source_endpoint_range, unlink_command);
            }
        }
    };

    for (auto& ev : graph.events) {
        validate_flow_block(ev, "event");
        validate_data_block(ev, "event");
    }
    for (auto& fn : graph.functions) {
        validate_flow_block(fn, "function");
        validate_data_block(fn, "function");
    }

    return diagnostics;
}

std::vector<Diagnostic> EditSession::validate() const {
    auto* g = active_graph();
    if (!g) return {};
    return validate_graph(*g);
}

std::vector<Diagnostic> EditSession::validate_all() const {
    std::vector<Diagnostic> diagnostics;
    for (auto& graph : module_.graphs) {
        auto graph_diagnostics = validate_graph(graph);
        diagnostics.insert(diagnostics.end(),
                           std::make_move_iterator(graph_diagnostics.begin()),
                           std::make_move_iterator(graph_diagnostics.end()));
    }
    return diagnostics;
}

std::vector<const NodeDefinition*> EditSession::available_types() const {
    return env_.nodes().all();
}

// ─── File I/O ──────────────────────────────────────────────────────

// Reads a file into a string.
static std::string read_file_contents(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Parses source text into an AST.
static std::unique_ptr<ModuleNode> parse_text(const std::string& src) {
    Lexer lexer(src);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto result = parser.parse();
    if (result.is_err()) return nullptr;
    return std::move(result).value();
}

static std::optional<asset::Module> parse_asset_text(const std::string& source, const std::string& source_name) {
    asset::Parser parser(source, source_name);
    auto parsed = parser.parse();
    if (!parsed.diagnostics.empty()) return std::nullopt;
    return std::move(parsed.module);
}

static Result<void, std::string> lint_asset_module(const asset::Module& module) {
    asset::Linter linter;
    auto diagnostics = linter.lint(module);
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == Severity::Error) {
            return Result<void, std::string>::err("Asset lint error: " + diagnostic.message);
        }
    }
    return Result<void, std::string>::ok();
}

static bool has_asset_source_items(const asset::ItemContainer& items) {
    return !items.properties.empty() ||
           !items.consts.empty() ||
           !items.calls.empty() ||
           !items.assignments.empty() ||
           !items.directives.empty() ||
           !items.blocks.empty();
}

static bool has_asset_declaration_symbol(const asset::Module& module) {
    for (const auto& symbol : module.symbols) {
        if (symbol.kind != "export") return true;
    }
    return false;
}

static bool has_asset_declaration_import_items(const asset::Module& module) {
    if (has_asset_source_items(module.items)) return false;
    return !module.modules.empty() ||
           !module.enums.empty() ||
           !module.objects.empty() ||
           !module.block_kinds.empty() ||
           !module.commands.empty() ||
           !module.schemas.empty() ||
           !module.lints.empty() ||
           has_asset_declaration_symbol(module);
}

static std::string asset_attr_arg(const asset::Attribute& attr, const std::string& name) {
    for (const auto& arg : attr.args) {
        if (arg.name == name) return arg.value.text;
    }
    return "";
}

static bool asset_field_pin(const asset::FieldDecl& field, PinDefinition& pin) {
    pin.name = field.name;
    pin.type_name = field.type;
    pin.source_range = field.span.range;
    pin.name_range = field.name_span.range;
    pin.type_name_range = field.span.range;
    for (const auto& attr : field.attributes) {
        if (attr.name == "flow.pin") {
            pin.kind = asset_attr_arg(attr, "kind") == "exec" ? PinKind::Exec : PinKind::Data;
            pin.direction = asset_attr_arg(attr, "direction") == "out" ? PinDirection::Output : PinDirection::Input;
            return true;
        }
        if (attr.name == "flow.input") {
            pin.kind = PinKind::Data;
            pin.direction = PinDirection::Input;
            return true;
        }
        if (attr.name == "flow.output") {
            pin.kind = PinKind::Data;
            pin.direction = PinDirection::Output;
            return true;
        }
    }
    return false;
}

static std::optional<int> asset_max_exec_fan_out_value(const std::string& value) {
    if (value == "unlimited") return -1;
    try {
        size_t parsed = 0;
        int result = std::stoi(value, &parsed);
        if (parsed != value.size()) return std::nullopt;
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

static Result<void, std::string> register_asset_declarations(Environment& env,
                                                             const asset::Module& module,
                                                             const std::string& source_name) {
    std::unordered_set<std::string> local_types;
    std::unordered_set<std::string> local_objects;
    std::unordered_set<std::string> local_schemas;

    for (const auto& symbol : module.symbols) {
        if (symbol.kind != "type") continue;
        if (!local_types.insert(symbol.name).second) {
            return Result<void, std::string>::err("Duplicate asset type declaration '" + symbol.name + "'");
        }
        if (env.types().find(symbol.name)) {
            return Result<void, std::string>::err("Asset type declaration conflicts with existing type '" + symbol.name + "'");
        }
    }
    for (const auto& object : module.objects) {
        if (!local_objects.insert(object.name).second) {
            return Result<void, std::string>::err("Duplicate asset object declaration '" + object.name + "'");
        }
        if (env.nodes().find(object.name)) {
            return Result<void, std::string>::err("Asset object declaration conflicts with existing node '" + object.name + "'");
        }
    }
    for (const auto& schema_decl : module.schemas) {
        if (!local_schemas.insert(schema_decl.name).second) {
            return Result<void, std::string>::err("Duplicate asset schema declaration '" + schema_decl.name + "'");
        }
        if (env.schemas().find(schema_decl.name)) {
            return Result<void, std::string>::err("Asset schema declaration conflicts with existing schema '" + schema_decl.name + "'");
        }
        for (const auto& property : schema_decl.properties) {
            if (property.path == "max_exec_fan_out" && !asset_max_exec_fan_out_value(property.value.text)) {
                return Result<void, std::string>::err(
                    "Invalid max_exec_fan_out value in asset schema '" + schema_decl.name + "'");
            }
        }
    }

    for (const auto& symbol : module.symbols) {
        if (symbol.kind != "type") continue;
        TypeInfo info;
        info.name = symbol.name;
        info.source_range = symbol.span.range;
        info.name_range = symbol.span.range;
        info.source_file = source_name;
        env.types().register_type(std::move(info));
    }

    for (const auto& object : module.objects) {
        NodeDefinition def;
        def.type_name = object.name;
        def.is_native = true;
        def.source_range = object.span.range;
        def.name_range = object.span.range;
        def.source_file = source_name;
        for (const auto& field : object.fields) {
            PinDefinition pin;
            if (asset_field_pin(field, pin)) {
                pin.source_file = source_name;
                def.pins.push_back(std::move(pin));
                continue;
            }
            NodeFieldDefinition node_field;
            node_field.name = field.name;
            node_field.type_name = field.type;
            node_field.default_value = field.has_default ? field.default_value.text : "";
            node_field.source_range = field.span.range;
            node_field.name_range = field.name_span.range;
            node_field.type_name_range = field.span.range;
            node_field.default_value_range = field.default_value.span.range;
            node_field.source_file = source_name;
            def.fields.push_back(std::move(node_field));
        }
        env.nodes().register_node(std::move(def));
    }

    for (const auto& schema_decl : module.schemas) {
        GraphSchema schema;
        schema.name = schema_decl.name;
        schema.source_range = schema_decl.span.range;
        schema.name_range = schema_decl.span.range;
        schema.source_file = source_name;
        for (const auto& property : schema_decl.properties) {
            GraphSchemaField field;
            field.name = property.path;
            field.value = property.value.text;
            field.source_range = property.span.range;
            field.name_range = property.name_span.range;
            field.value_range = property.value_span.range;
            field.source_file = source_name;
            schema.fields.push_back(field);
            if (field.name == "max_exec_fan_out") {
                if (auto parsed = asset_max_exec_fan_out_value(field.value)) {
                    schema.connection_policy.max_exec_fan_out = *parsed;
                }
            } else if (field.name == "allow_exec_fan_in") {
                schema.connection_policy.allow_exec_fan_in = field.value == "true";
            } else if (field.name == "strict_type_match") {
                schema.connection_policy.strict_type_match = field.value == "true";
            }
        }
        env.schemas().register_schema(std::move(schema));
    }
    return Result<void, std::string>::ok();
}

static ParamDirection asset_param_direction(const std::string& direction) {
    if (direction == "out") return ParamDirection::Out;
    if (direction == "var") return ParamDirection::Var;
    return ParamDirection::In;
}

static PinAddress asset_pin_address(const std::string& endpoint) {
    auto dot = endpoint.find('.');
    if (dot == std::string::npos) return {"", endpoint};
    return {endpoint.substr(0, dot), endpoint.substr(dot + 1)};
}

static DataSource asset_data_source(const std::string& endpoint) {
    auto dot = endpoint.find('.');
    if (dot == std::string::npos) return {"", endpoint};
    return {endpoint.substr(0, dot), endpoint.substr(dot + 1)};
}

static FlowConnection asset_flow_edge(const asset::FlowEdge& edge) {
    FlowConnection flow;
    flow.from = asset_pin_address(edge.from);
    flow.to = asset_pin_address(edge.to);
    flow.source_range = edge.span.range;
    return flow;
}

static DataLink asset_data_edge(const asset::FlowDataEdge& edge) {
    DataLink link;
    link.source = asset_data_source(edge.source);
    link.target = asset_pin_address(edge.target);
    link.source_range = edge.span.range;
    return link;
}

static Graph asset_graph_to_legacy_graph(const asset::FlowGraph& flow) {
    Graph graph;
    graph.name = flow.name;
    if (!flow.schema.empty()) graph.base_type = flow.schema;
    for (const auto& param : flow.parameters) {
        GraphParameter converted;
        converted.name = param.name;
        converted.type_name = param.type;
        converted.direction = asset_param_direction(param.direction);
        converted.default_value = param.has_default ? param.default_value.text : "";
        converted.source_range = param.span.range;
        graph.parameters.push_back(std::move(converted));
    }
    for (const auto& node : flow.nodes) {
        NodeInstance instance;
        instance.type_name = node.type;
        instance.instance_name = node.alias;
        instance.source_range = node.span.range;
        for (const auto& property : node.properties) {
            InitializerField field;
            field.name = property.path;
            field.value = property.value.text;
            field.source_range = property.span.range;
            field.name_range = property.name_span.range;
            field.value_range = property.value_span.range;
            instance.initializer_fields.push_back(std::move(field));
        }
        graph.node_instances.push_back(std::move(instance));
    }
    for (const auto& block : flow.blocks) {
        if (block.kind == "function") {
            Function fn;
            fn.name = block.name;
            fn.source_range = block.span.range;
            for (const auto& edge : block.edges) fn.flow_connections.push_back(asset_flow_edge(edge));
            for (const auto& edge : block.data_edges) fn.data_links.push_back(asset_data_edge(edge));
            graph.functions.push_back(std::move(fn));
        } else {
            Event ev;
            ev.name = block.name;
            ev.source_range = block.span.range;
            for (const auto& edge : block.edges) ev.flow_connections.push_back(asset_flow_edge(edge));
            for (const auto& edge : block.data_edges) ev.data_links.push_back(asset_data_edge(edge));
            graph.events.push_back(std::move(ev));
        }
    }
    return graph;
}

static void collect_asset_graph_names(const asset::ItemContainer& items, std::vector<std::string>& names) {
    for (const auto& block : items.blocks) {
        if (block->kind == "graph") names.push_back(block->name);
        collect_asset_graph_names(block->items, names);
    }
}

static bool has_asset_declarations(const asset::Module& module) {
    return !module.imports.empty() ||
           !module.modules.empty() ||
           !module.enums.empty() ||
           !module.objects.empty() ||
           !module.block_kinds.empty() ||
           !module.commands.empty() ||
           !module.schemas.empty() ||
           !module.lints.empty() ||
           !module.symbols.empty();
}

static Result<Module, std::string> compile_asset_session_module(const asset::Module& asset_module,
                                                                const std::string& source_name) {
    Module module;
    module.file_path = source_name;
    for (const auto& import : asset_module.imports) {
        ImportDecl converted;
        converted.path = import.path;
        converted.source_range = import.span.range;
        converted.path_range = import.path_span.range;
        module.imports.push_back(std::move(converted));
    }

    std::vector<std::string> graph_names;
    collect_asset_graph_names(asset_module.items, graph_names);
    for (const auto& graph_name : graph_names) {
        auto projected = asset::FlowGraphProjector::project(asset_module, graph_name);
        if (projected.is_err()) return Result<Module, std::string>::err(projected.error());
        if (!projected.value().diagnostics.empty()) {
            return Result<Module, std::string>::err(projected.value().diagnostics.front().message);
        }
        module.graphs.push_back(asset_graph_to_legacy_graph(projected.value()));
    }
    return Result<Module, std::string>::ok(std::move(module));
}

Result<void, std::string> EditSession::load_source(const std::string& source, const std::string& source_name) {
    if (source.empty()) return Result<void, std::string>::err("Cannot load empty source");

    const std::string effective_source_name = source_name.empty() ? file_path_ : source_name;
    auto asset_module = parse_asset_text(source, effective_source_name);
    if (asset_module) {
        auto linted = lint_asset_module(*asset_module);
        if (linted.is_err()) return linted;
        auto asset_result = compile_asset_session_module(*asset_module, effective_source_name);
        if (asset_result.is_err()) return Result<void, std::string>::err(asset_result.error());
        if (asset_result.value().graphs.empty()) {
            if (has_asset_declarations(*asset_module)) {
                return Result<void, std::string>::err("Asset source must contain at least one graph");
            }
        } else {
            std::string active_graph_name;
            if (active_ >= 0 && active_ < static_cast<int>(module_.graphs.size())) {
                active_graph_name = module_.graphs[active_].name;
            }

            push_module_undo("apply asset source");
            module_ = std::move(asset_result).value();
            mark_loaded_imports();
            refresh_module_graph_node_definitions(env_, module_);
            active_ = module_.graphs.empty() ? -1 : 0;
            if (!active_graph_name.empty()) {
                for (size_t i = 0; i < module_.graphs.size(); ++i) {
                    if (module_.graphs[i].name == active_graph_name) {
                        active_ = static_cast<int>(i);
                        break;
                    }
                }
            }
            asset_source_ = source;
            dirty_ = true;
            return Result<void, std::string>::ok();
        }
    }

    auto ast = parse_text(source);
    if (!ast) return Result<void, std::string>::err("Parse error in source");

    Compiler compiler(env_);
    auto result = compiler.compile(*ast, effective_source_name);
    if (result.is_err()) return Result<void, std::string>::err("Compile error: " + result.error());

    std::string active_graph_name;
    if (active_ >= 0 && active_ < static_cast<int>(module_.graphs.size())) {
        active_graph_name = module_.graphs[active_].name;
    }

    push_module_undo("apply source");
    module_ = std::move(result).value();
    asset_source_.reset();
    mark_loaded_imports();
    refresh_module_graph_node_definitions(env_, module_);
    active_ = module_.graphs.empty() ? -1 : 0;
    if (!active_graph_name.empty()) {
        for (size_t i = 0; i < module_.graphs.size(); ++i) {
            if (module_.graphs[i].name == active_graph_name) {
                active_ = static_cast<int>(i);
                break;
            }
        }
    }
    dirty_ = true;
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::load_file(const std::string& path) {
    auto src = read_file_contents(path);
    if (src.empty()) return Result<void, std::string>::err("Cannot read file: " + path);

    auto result = load_source(src, path);
    if (result.is_err()) return result;

    undo_stack_.clear();
    redo_stack_.clear();
    file_path_ = path;
    dirty_ = false;
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::load_import(const std::string& path) {
    auto key = import_key(path);
    if (contains_key(loaded_import_keys_, key)) {
        add_import(path, true);
        return Result<void, std::string>::ok();
    }

    auto src = read_file_contents(path);
    if (src.empty()) return Result<void, std::string>::err("Cannot read file: " + path);

    auto asset_module = parse_asset_text(src, path);
    if (asset_module && has_asset_declaration_import_items(*asset_module)) {
        auto linted = lint_asset_module(*asset_module);
        if (linted.is_err()) return linted;
        auto registered = register_asset_declarations(env_, *asset_module, path);
        if (registered.is_err()) return registered;
        loaded_import_keys_.push_back(key);
        add_import(path, true);
        return Result<void, std::string>::ok();
    }

    auto ast = parse_text(src);
    if (!ast) return Result<void, std::string>::err("Parse error in: " + path);

    Compiler compiler(env_);
    auto result = compiler.compile(*ast, path);
    if (result.is_err()) return Result<void, std::string>::err("Compile error: " + result.error());

    loaded_import_keys_.push_back(key);
    add_import(path, true);
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::save_file(const std::string& path) {
    std::string out_path = path.empty() ? file_path_ : path;
    if (out_path.empty()) return Result<void, std::string>::err("No file path specified");

    std::ofstream f(out_path);
    if (!f.is_open()) return Result<void, std::string>::err("Cannot write to: " + out_path);

    f << emit();
    file_path_ = out_path;
    dirty_ = false;
    return Result<void, std::string>::ok();
}

// ─── Command Log ───────────────────────────────────────────────────

void EditSession::log_command(const std::string& cmd) {
    command_log_.push_back(cmd);
}

// ─── JSON State Export ─────────────────────────────────────────────

std::string EditSession::diagnostics_to_json() const {
    return diagnostics_to_json_array(validate());
}

std::string EditSession::state_to_json() const {
    // Module: imports
    std::vector<std::string> imports;
    auto base_dir = module_base_dir(module_);
    for (auto& imp : module_.imports) {
        imports.push_back(jobj({
            {"id", jstr(import_element_id(imp.path))},
            {"persistent_id", jstr(persistent_id_from_annotations(imp.annotations))},
            {"path", jstr(imp.path)},
            {"source_range", source_range_to_json(imp.source_range)},
            {"path_source_range", source_range_to_json(imp.path_range)},
            {"is_native", jbool(imp.is_native)},
            {"loaded", jbool(imp.loaded)},
            {"normalized_path", jstr(normalized_import_path(imp.path, base_dir))},
            {"annotations", annotations_to_json(import_element_id(imp.path), imp.annotations)}
        }));
    }

    // Module: lets
    std::vector<std::string> lets;
    for (auto& l : module_.top_level_lets)
        lets.push_back(jobj({
            {"id", jstr(let_element_id(l.name))},
            {"persistent_id", jstr(persistent_id_from_annotations(l.annotations))},
            {"name", jstr(l.name)},
            {"source_range", source_range_to_json(l.source_range)},
            {"name_source_range", source_range_to_json(l.name_range)},
            {"type_source_range", source_range_to_json(l.type_name_range)},
            {"constructor_source_range", source_range_to_json(l.constructor_range)},
            {"type", jstr(l.type_name)},
            {"arg", jstr(l.constructor_arg)},
            {"arg_source_range", source_range_to_json(l.constructor_arg_range)},
            {"annotations", annotations_to_json(let_element_id(l.name), l.annotations)}
        }));

    // Module: graphs
    std::vector<std::string> graphs;
    for (auto& g : module_.graphs)
        graphs.push_back(graph_to_json(g));

    // Node type definitions
    std::vector<std::string> declared_types;
    for (auto* t : env_.types().all())
        declared_types.push_back(type_info_to_json(*t));

    std::vector<std::string> types;
    for (auto* nd : env_.nodes().all())
        types.push_back(node_def_to_json(*nd));

    // Schemas
    std::vector<std::string> schemas;
    for (auto* s : env_.schemas().all()) {
        std::vector<std::string> fields;
        for (auto& field : s->fields) fields.push_back(schema_field_to_json(s->name, field));
        schemas.push_back(jobj({
            {"id",                jstr(declaration_schema_element_id(s->name))},
            {"name",              jstr(s->name)},
            {"source_file",       jstr(s->source_file)},
            {"source_range",      source_range_to_json(s->source_range)},
            {"name_source_range", source_range_to_json(s->name_range)},
            {"fields",            jarray(fields)},
            {"max_exec_fan_out",  jint(s->connection_policy.max_exec_fan_out)},
            {"allow_exec_fan_in", jbool(s->connection_policy.allow_exec_fan_in)},
            {"strict_type_match", jbool(s->connection_policy.strict_type_match)},
            {"persistent_id",     jstr(persistent_id_from_annotations(s->annotations))},
            {"annotations",       annotations_to_json(declaration_schema_element_id(s->name), s->annotations)}
        }));
    }

    // Command log
    std::vector<std::string> log;
    for (auto& cmd : command_log_) log.push_back(jstr(cmd));

    return jobj({
        {"file_path",    jstr(file_path_)},
        {"dirty",        jbool(dirty_)},
        {"active_graph", jint(active_)},
        {"can_undo",     jbool(can_undo())},
        {"can_redo",     jbool(can_redo())},
        {"module", jobj({
            {"imports", jarray(imports)},
            {"lets",    jarray(lets)},
            {"graphs",  jarray(graphs)}
        })},
        {"declared_types", jarray(declared_types)},
        {"types",        jarray(types)},
        {"schemas",      jarray(schemas)},
        {"diagnostics",  diagnostics_to_json_array(validate())},
        {"module_diagnostics", diagnostics_to_json_array(validate_all())},
        {"command_log",  jarray(log)}
    });
}

} // namespace gs
