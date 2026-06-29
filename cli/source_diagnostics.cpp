#include "source_diagnostics.h"

#include "graphscript/compile/compiler.h"
#include "graphscript/diagnostic/diagnostic.h"
#include "graphscript/parse/lexer.h"
#include "graphscript/parse/parser.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace gs {
namespace {

struct ResolvedDeclaration {
    std::string path;
    std::string normalized_path;
    std::string content_hash;
    std::string status;
    std::string message;
    std::string command;
    std::string parent_path;
    std::string parent_normalized_path;
    std::string import_chain;
    size_t depth = 0;
};

struct ResolvedEnvironment {
    Environment env;
    std::vector<ResolvedDeclaration> declarations;
    std::vector<Diagnostic> diagnostics;
};

std::string esc(const std::string& s) {
    std::string r;
    for (char c : s) {
        switch (c) {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default:   r += c;
        }
    }
    return r;
}

std::string json_str(const std::string& s) {
    return "\"" + esc(s) + "\"";
}

std::string source_hash(const std::string& source) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : source) {
        hash ^= c;
        hash *= 1099511628211ull;
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

std::string json_location(const SourceLocation& loc) {
    return "{\"line\":" + std::to_string(loc.line) +
           ",\"column\":" + std::to_string(loc.column) + "}";
}

std::string json_range(const SourceRange& range) {
    return "{\"start\":" + json_location(range.start) +
           ",\"end\":" + json_location(range.end) + "}";
}

std::string json_target(const DiagnosticTarget& target) {
    return "{\"graph\":" + json_str(target.graph) +
           ",\"block_kind\":" + json_str(target.block_kind) +
           ",\"block_name\":" + json_str(target.block_name) +
           ",\"node_instance\":" + json_str(target.node_instance) +
           ",\"pin_name\":" + json_str(target.pin_name) +
           ",\"parameter_name\":" + json_str(target.parameter_name) +
           ",\"reference\":" + json_str(target.reference) +
           ",\"connection_kind\":" + json_str(target.connection_kind) + "}";
}

std::string append_occurrence_suffix(std::unordered_map<std::string, size_t>& seen, const std::string& base_id) {
    size_t& count = seen[base_id];
    const size_t occurrence = count++;
    if (occurrence == 0) return base_id;
    return base_id + "#" + std::to_string(occurrence + 1);
}

std::string diagnostic_id_base(const Diagnostic& diag) {
    const std::string severity = diag.severity == Severity::Error ? "error" : "warning";
    return "diagnostic:" + diag.code + "/" + severity + "/" +
           diag.context + "/" +
           std::to_string(diag.range.start.line) + ":" + std::to_string(diag.range.start.column) + "-" +
           std::to_string(diag.range.end.line) + ":" + std::to_string(diag.range.end.column) + "/" +
           diag.target.graph + "/" + diag.target.block_kind + "/" + diag.target.block_name + "/" +
           diag.target.node_instance + "/" + diag.target.pin_name + "/" +
           diag.target.parameter_name + "/" + diag.target.reference + "/" + diag.target.connection_kind;
}

std::string diagnostic_action_id_base(const std::string& diagnostic_id, const DiagnosticAction& action) {
    return "diagnostic-action:" + diagnostic_id + "/" +
           action.kind + "/" + action.command + "/" + action.title;
}

std::string json_actions(const std::string& diagnostic_id, const std::vector<DiagnosticAction>& actions) {
    std::string json = "[";
    std::unordered_map<std::string, size_t> seen_ids;
    for (size_t i = 0; i < actions.size(); ++i) {
        if (i > 0) json += ",";
        const auto action_id = append_occurrence_suffix(seen_ids, diagnostic_action_id_base(diagnostic_id, actions[i]));
        json += "{\"id\":" + json_str(action_id) +
                ",\"title\":" + json_str(actions[i].title) +
                ",\"kind\":" + json_str(actions[i].kind) +
                ",\"command\":" + json_str(actions[i].command) +
                ",\"edit_range\":" + json_range(actions[i].edit_range) +
                ",\"replacement\":" + json_str(actions[i].replacement) + "}";
    }
    json += "]";
    return json;
}

std::string json_diagnostic(const std::string& diagnostic_id, const Diagnostic& diag) {
    return "{\"id\":" + json_str(diagnostic_id) +
           ",\"severity\":" + json_str(diag.severity == Severity::Error ? "error" : "warning") +
           ",\"message\":" + json_str(diag.message) +
           ",\"context\":" + json_str(diag.context) +
           ",\"code\":" + json_str(diag.code) +
           ",\"range\":" + json_range(diag.range) +
           ",\"hint\":" + json_str(diag.hint) +
           ",\"target\":" + json_target(diag.target) +
           ",\"actions\":" + json_actions(diagnostic_id, diag.actions) + "}";
}

std::string json_diagnostics(const std::vector<Diagnostic>& diagnostics) {
    std::string json = "[";
    std::unordered_map<std::string, size_t> seen_ids;
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        if (i > 0) json += ",";
        const auto diagnostic_id = append_occurrence_suffix(seen_ids, diagnostic_id_base(diagnostics[i]));
        json += json_diagnostic(diagnostic_id, diagnostics[i]);
    }
    json += "]";
    return json;
}

std::string json_declarations(const std::vector<ResolvedDeclaration>& declarations) {
    std::string json = "[";
    for (size_t i = 0; i < declarations.size(); ++i) {
        if (i > 0) json += ",";
        json += "{\"path\":" + json_str(declarations[i].path) +
                ",\"normalized_path\":" + json_str(declarations[i].normalized_path) +
                ",\"content_hash\":" + json_str(declarations[i].content_hash) +
                ",\"status\":" + json_str(declarations[i].status) +
                ",\"message\":" + json_str(declarations[i].message) +
                ",\"command\":" + json_str(declarations[i].command) +
                ",\"parent_path\":" + json_str(declarations[i].parent_path) +
                ",\"parent_normalized_path\":" + json_str(declarations[i].parent_normalized_path) +
                ",\"import_chain\":" + json_str(declarations[i].import_chain) +
                ",\"depth\":" + std::to_string(declarations[i].depth) + "}";
    }
    json += "]";
    return json;
}

std::string command_arg(const std::string& value) {
    bool needs_quotes = value.empty();
    for (char c : value) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) return value;

    std::string quoted = "\"";
    for (char c : value) {
        if (c != '"') quoted += c;
    }
    quoted += "\"";
    return quoted;
}

Diagnostic import_diagnostic(const std::string& message,
                             const std::string& context,
                             const std::string& code,
                             SourceRange range,
                             const std::string& hint) {
    Diagnostic diag;
    diag.severity = Severity::Error;
    diag.message = message;
    diag.context = context;
    diag.code = code;
    diag.range = range;
    diag.hint = hint;
    return diag;
}

bool has_dgs_extension(const std::string& path) {
    constexpr const char* suffix = ".d.gs";
    if (path.size() < 5) return false;
    return path.compare(path.size() - 5, 5, suffix) == 0;
}

bool has_url_scheme(const std::string& path) {
    auto pos = path.find(':');
    if (pos == std::string::npos) return false;
    for (size_t i = 0; i < pos; ++i) {
        if (!std::isalpha(static_cast<unsigned char>(path[i]))) return false;
    }
    return pos > 0 && pos + 2 < path.size() && path[pos + 1] == '/' && path[pos + 2] == '/';
}

std::filesystem::path normalized_root(const std::string& base_dir) {
    std::error_code ec;
    std::filesystem::path root = base_dir.empty()
        ? std::filesystem::current_path(ec)
        : std::filesystem::path(base_dir);
    if (ec) root = std::filesystem::path(".");
    root = std::filesystem::absolute(root, ec);
    if (ec) root = std::filesystem::path(base_dir.empty() ? "." : base_dir);
    auto canonical = std::filesystem::weakly_canonical(root, ec);
    return (ec ? root.lexically_normal() : canonical.lexically_normal());
}

std::filesystem::path normalized_candidate(const std::filesystem::path& path) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec) return canonical.lexically_normal();
    auto absolute = std::filesystem::absolute(path, ec);
    if (!ec) return absolute.lexically_normal();
    return path.lexically_normal();
}

std::string comparable_path(const std::filesystem::path& path) {
    std::string value = path.generic_string();
#ifdef _WIN32
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
#endif
    while (value.size() > 1 && value.back() == '/') value.pop_back();
    return value;
}

bool is_within_root(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    std::string root_value = comparable_path(root);
    std::string candidate_value = comparable_path(candidate);
    return candidate_value == root_value ||
           (candidate_value.size() > root_value.size() &&
            candidate_value.compare(0, root_value.size(), root_value) == 0 &&
            candidate_value[root_value.size()] == '/');
}

std::optional<std::string> read_limited_file(const std::filesystem::path& path,
                                             size_t max_file_bytes,
                                             std::string& error) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        error = "Import file not found";
        return std::nullopt;
    }
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        error = "Import path is not a regular file";
        return std::nullopt;
    }
    auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        error = "Cannot stat import file";
        return std::nullopt;
    }
    if (size > max_file_bytes) {
        error = "Import file exceeds max_file_bytes";
        return std::nullopt;
    }

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        error = "Cannot read import file";
        return std::nullopt;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string environment_hash(const Environment& env,
                             const std::vector<ResolvedDeclaration>& declarations) {
    std::string material =
        "types=" + std::to_string(env.types().all().size()) +
        ";nodes=" + std::to_string(env.nodes().all().size()) +
        ";schemas=" + std::to_string(env.schemas().all().size());
    for (const auto& declaration : declarations) {
        material += ";" + declaration.status + ":" +
                    declaration.normalized_path + ":" + declaration.content_hash;
    }
    return source_hash(material);
}

std::string environment_json(const std::string& mode,
                             const Environment& env,
                             const std::vector<ResolvedDeclaration>& declarations,
                             const SourceDiagnosticsOptions& options) {
    return "{\"mode\":" + json_str(mode) +
           ",\"declarations\":" + json_declarations(declarations) +
           ",\"environment_hash\":" + json_str(environment_hash(env, declarations)) +
           ",\"type_count\":" + std::to_string(env.types().all().size()) +
           ",\"node_type_count\":" + std::to_string(env.nodes().all().size()) +
           ",\"schema_count\":" + std::to_string(env.schemas().all().size()) +
           ",\"limits\":{\"max_imports\":" + std::to_string(options.max_imports) +
           ",\"max_import_depth\":" + std::to_string(options.max_import_depth) +
           ",\"max_file_bytes\":" + std::to_string(options.max_file_bytes) +
           ",\"max_total_bytes\":" + std::to_string(options.max_total_bytes) + "}}";
}

std::string diagnostics_response(bool ok,
                                 const std::string& stage,
                                 const std::vector<Diagnostic>& diagnostics,
                                 const std::string& environment = "") {
    std::string json = "{\"ok\":" + std::string(ok ? "true" : "false") +
                       ",\"stage\":" + json_str(stage) +
                       ",\"diagnostics\":" + json_diagnostics(diagnostics);
    if (!environment.empty()) json += ",\"environment\":" + environment;
    json += "}";
    return json;
}

std::string compile_with_environment_to_json(const ModuleNode& ast,
                                             Environment& env,
                                             const std::string& environment = "") {
    Compiler compiler(env);
    auto compiled = compiler.compile(ast);
    if (compiled.is_err()) {
        return diagnostics_response(false, "compiler", compiler.diagnostics(), environment);
    }
    return diagnostics_response(true, "compiler", {}, environment);
}

bool declaration_limit_reached(ResolvedEnvironment& resolved,
                               const ImportNode& import,
                               const SourceDiagnosticsOptions& options,
                               SourceRange report_range) {
    if (resolved.declarations.size() < options.max_imports) return false;
    resolved.diagnostics.push_back(import_diagnostic(
        "Too many source imports to resolve",
        import.path,
        "GS_IMPORT_LIMIT_EXCEEDED",
        report_range,
        "Reduce the number of source imports or increase the resolver limit."));
    return true;
}

std::string readable_import_chain(const std::vector<std::string>& chain,
                                  const std::string& import_path) {
    std::string result = "source";
    for (const auto& part : chain) {
        result += " -> " + part;
    }
    result += " -> " + import_path;
    return result;
}

void resolve_import_node(const ImportNode& import,
                         Environment& env,
                         const std::filesystem::path& root,
                         const std::filesystem::path& current_dir,
                         const SourceDiagnosticsOptions& options,
                         ResolvedEnvironment& resolved,
                         size_t& total_bytes,
                         std::vector<std::string>& resolved_keys,
                         std::vector<std::string>& active_stack,
                         std::vector<std::string>& chain,
                         const std::string& parent_path,
                         const std::string& parent_normalized_path,
                         size_t depth,
                         SourceRange report_range) {
    if (declaration_limit_reached(resolved, import, options, report_range)) return;

    ResolvedDeclaration declaration;
    declaration.path = import.path;
    declaration.parent_path = parent_path;
    declaration.parent_normalized_path = parent_normalized_path;
    declaration.import_chain = readable_import_chain(chain, import.path);
    declaration.depth = depth;

    auto finish_with_diagnostic = [&](const std::string& status,
                                      const std::string& message,
                                      const std::string& code,
                                      const std::string& hint) {
        declaration.status = status;
        declaration.message = message;
        resolved.diagnostics.push_back(import_diagnostic(message, import.path, code, report_range, hint));
        resolved.declarations.push_back(std::move(declaration));
    };

    if (import.path.empty() || has_url_scheme(import.path)) {
        finish_with_diagnostic(
            "blocked",
            "Blocked source import '" + import.path + "'",
            "GS_IMPORT_PATH_BLOCKED",
            "Use a relative .d.gs path inside the configured source root.");
        return;
    }

    std::filesystem::path relative(import.path);
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
        finish_with_diagnostic(
            "blocked",
            "Blocked absolute source import '" + import.path + "'",
            "GS_IMPORT_PATH_BLOCKED",
            "Use a relative .d.gs path inside the configured source root.");
        return;
    }

    if (!has_dgs_extension(import.path)) {
        finish_with_diagnostic(
            "unsupported",
            "Unsupported source import '" + import.path + "'",
            "GS_IMPORT_UNSUPPORTED_EXTENSION",
            "Only .d.gs declaration files are resolved by the source diagnostics dry-run.");
        return;
    }

    if (depth > options.max_import_depth) {
        finish_with_diagnostic(
            "too_deep",
            "Source import depth exceeds max_import_depth",
            "GS_IMPORT_DEPTH_EXCEEDED",
            "Reduce nested declaration imports or increase the resolver depth limit.");
        return;
    }

    auto candidate = normalized_candidate(current_dir / relative);
    declaration.normalized_path = candidate.string();
    const std::string candidate_key = comparable_path(candidate);
    if (!is_within_root(root, candidate)) {
        finish_with_diagnostic(
            "blocked",
            "Blocked source import outside root '" + import.path + "'",
            "GS_IMPORT_PATH_BLOCKED",
            "Keep source imports inside the configured base_dir.");
        return;
    }

    if (std::find(active_stack.begin(), active_stack.end(), candidate_key) != active_stack.end()) {
        finish_with_diagnostic(
            "cycle",
            "Source import cycle detected at '" + import.path + "'",
            "GS_IMPORT_CYCLE",
            "Break the declaration import cycle before using import-aware diagnostics.");
        return;
    }

    if (std::find(resolved_keys.begin(), resolved_keys.end(), candidate_key) != resolved_keys.end()) {
        declaration.status = "skipped";
        declaration.message = "Declaration already resolved in dry-run environment";
        declaration.command = "import " + command_arg(declaration.normalized_path);
        resolved.declarations.push_back(std::move(declaration));
        return;
    }

    std::string read_error;
    auto declaration_source = read_limited_file(candidate, options.max_file_bytes, read_error);
    if (!declaration_source) {
        declaration.status = (read_error.find("exceeds") != std::string::npos) ? "too_large" : "missing";
        declaration.message = read_error;
        resolved.diagnostics.push_back(import_diagnostic(
            "Cannot resolve source import '" + import.path + "': " + read_error,
            import.path,
            declaration.status == "too_large" ? "GS_IMPORT_TOO_LARGE" : "GS_IMPORT_READ_FAILED",
            report_range,
            "Check the import path and source diagnostics resolver limits."));
        resolved.declarations.push_back(std::move(declaration));
        return;
    }

    total_bytes += declaration_source->size();
    if (total_bytes > options.max_total_bytes) {
        finish_with_diagnostic(
            "too_large",
            "Resolved source imports exceed max_total_bytes",
            "GS_IMPORT_LIMIT_EXCEEDED",
            "Reduce imported declaration size or increase the resolver limit.");
        return;
    }

    declaration.content_hash = source_hash(*declaration_source);
    Lexer lexer(*declaration_source);
    Parser parser(lexer.tokenize());
    auto parsed_import = parser.parse();
    if (parsed_import.is_err()) {
        finish_with_diagnostic(
            "parse_error",
            "Parse error in source import '" + import.path + "'",
            "GS_IMPORT_PARSE_ERROR",
            "Fix the imported declaration file before using import-aware diagnostics.");
        return;
    }

    active_stack.push_back(candidate_key);
    chain.push_back(import.path);
    size_t diagnostics_before_nested = resolved.diagnostics.size();
    for (const auto& nested_import : parsed_import.value()->imports) {
        resolve_import_node(*nested_import,
                            env,
                            root,
                            candidate.parent_path(),
                            options,
                            resolved,
                            total_bytes,
                            resolved_keys,
                            active_stack,
                            chain,
                            import.path,
                            declaration.normalized_path,
                            depth + 1,
                            report_range);
    }
    chain.pop_back();
    active_stack.pop_back();

    if (resolved.diagnostics.size() != diagnostics_before_nested) {
        declaration.status = "dependency_error";
        declaration.message = "Nested imports failed";
        resolved.declarations.push_back(std::move(declaration));
        return;
    }

    Compiler compiler(env);
    auto compiled_import = compiler.compile(*parsed_import.value(), candidate.string());
    if (compiled_import.is_err()) {
        declaration.status = "compile_error";
        declaration.message = compiled_import.error();
        resolved.diagnostics.push_back(import_diagnostic(
            "Compile error in source import '" + import.path + "'",
            import.path,
            "GS_IMPORT_COMPILE_ERROR",
            report_range,
            "Fix the imported declaration file before using import-aware diagnostics."));
        resolved.declarations.push_back(std::move(declaration));
        return;
    }

    declaration.status = "loaded";
    declaration.message = "Loaded declaration into dry-run environment";
    declaration.command = "import " + command_arg(declaration.normalized_path);
    resolved_keys.push_back(candidate_key);
    resolved.declarations.push_back(std::move(declaration));
}

ResolvedEnvironment resolve_imports(const ModuleNode& ast,
                                    const Environment& session_env,
                                    const SourceDiagnosticsOptions& options) {
    ResolvedEnvironment resolved;
    resolved.env = session_env;
    std::filesystem::path root = normalized_root(options.base_dir);
    size_t total_bytes = 0;
    std::vector<std::string> resolved_keys;
    std::vector<std::string> active_stack;
    std::vector<std::string> chain;

    for (const auto& import : ast.imports) {
        resolve_import_node(*import,
                            resolved.env,
                            root,
                            root,
                            options,
                            resolved,
                            total_bytes,
                            resolved_keys,
                            active_stack,
                            chain,
                            "",
                            "",
                            1,
                            import->range);
    }

    return resolved;
}

std::string resolve_imports_and_compile_to_json(const ModuleNode& ast,
                                                const Environment& session_env,
                                                const SourceDiagnosticsOptions& options) {
    auto resolved = resolve_imports(ast, session_env, options);
    auto env_json = environment_json("resolved", resolved.env, resolved.declarations, options);
    if (!resolved.diagnostics.empty()) {
        return diagnostics_response(false, "resolver", resolved.diagnostics, env_json);
    }
    return compile_with_environment_to_json(ast, resolved.env, env_json);
}

} // namespace

std::string source_diagnostics_to_json(const std::string& source,
                                       const Environment& session_env,
                                       const SourceDiagnosticsOptions& options) {
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto parsed = parser.parse();
    if (parsed.is_err()) {
        std::string env_json;
        if (options.resolve_imports) {
            env_json = environment_json("resolved", session_env, {}, options);
        }
        return diagnostics_response(false, "parser", parser.diagnostics(), env_json);
    }

    if (options.resolve_imports) {
        return resolve_imports_and_compile_to_json(*parsed.value(), session_env, options);
    }

    Environment session_copy = session_env;
    return compile_with_environment_to_json(*parsed.value(), session_copy);
}

Result<std::string, std::string> source_diagnostics_environment_hash(
    const std::string& source,
    const Environment& session_env,
    const SourceDiagnosticsOptions& options) {
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto parsed = parser.parse();
    if (parsed.is_err()) {
        return Result<std::string, std::string>::err("Parse error in source");
    }

    if (options.resolve_imports) {
        auto resolved = resolve_imports(*parsed.value(), session_env, options);
        if (!resolved.diagnostics.empty()) {
            return Result<std::string, std::string>::err(resolved.diagnostics.front().message);
        }
        return Result<std::string, std::string>::ok(
            environment_hash(resolved.env, resolved.declarations));
    }

    Environment session_copy = session_env;
    return Result<std::string, std::string>::ok(environment_hash(session_copy, {}));
}

} // namespace gs
