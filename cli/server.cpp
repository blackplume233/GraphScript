#include "server.h"
#include "editor.h"
#include "source_diagnostics.h"
#include <httplib.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <optional>

namespace gs {

WebServer::WebServer(EditSession& session, CLIEditor& cli, int port)
    : session_(session), cli_(cli), port_(port) {}

// Reads a file into a string. Returns empty on failure.
static std::string read_file(const std::string& path, std::ios::openmode mode = std::ios::in) {
    std::ifstream f(path, mode);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Escapes a string for JSON output.
static std::string esc(const std::string& s) {
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

static std::string json_str(const std::string& s) {
    return "\"" + esc(s) + "\"";
}

static bool has_suffix(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string content_type_for_path(const std::string& path) {
    if (has_suffix(path, ".html")) return "text/html";
    if (has_suffix(path, ".js")) return "text/javascript";
    if (has_suffix(path, ".css")) return "text/css";
    if (has_suffix(path, ".svg")) return "image/svg+xml";
    if (has_suffix(path, ".png")) return "image/png";
    if (has_suffix(path, ".jpg") || has_suffix(path, ".jpeg")) return "image/jpeg";
    if (has_suffix(path, ".webp")) return "image/webp";
    if (has_suffix(path, ".woff2")) return "font/woff2";
    return "application/octet-stream";
}

static bool is_safe_web_relative_path(const std::string& path) {
    if (path.empty()) return false;
    const auto fs_path = std::filesystem::path(path);
    if (fs_path.is_absolute()) return false;
    for (const auto& part : fs_path) {
        if (part == "..") return false;
    }
    return true;
}

static bool serve_web_file(const std::string& web_dir, const std::string& relative_path, httplib::Response& res) {
    if (web_dir.empty() || !is_safe_web_relative_path(relative_path)) {
        res.status = 404;
        res.set_content("Not found", "text/plain");
        return false;
    }

    const auto path = (std::filesystem::path(web_dir) / std::filesystem::path(relative_path)).lexically_normal();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || !std::filesystem::is_regular_file(path, ec)) {
        res.status = 404;
        res.set_content("Not found", "text/plain");
        return false;
    }

    const std::string content = read_file(path.string(), std::ios::in | std::ios::binary);
    if (content.empty() && std::filesystem::file_size(path, ec) > 0) {
        res.status = 500;
        res.set_content("Cannot read file", "text/plain");
        return false;
    }

    res.set_content(content, content_type_for_path(path.string()));
    return true;
}

static std::string base64_encode(const std::string& input) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int value = 0;
    int bits = -6;
    for (unsigned char c : input) {
        value = (value << 8) + c;
        bits += 8;
        while (bits >= 0) {
            out.push_back(table[(value >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) {
        out.push_back(table[((value << 8) >> (bits + 8)) & 0x3F]);
    }
    while (out.size() % 4 != 0) out.push_back('=');
    return out;
}

static std::string source_hash(const std::string& source) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : source) {
        hash ^= c;
        hash *= 1099511628211ull;
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

static std::string command_arg(const std::string& value) {
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

static std::string json_location(const SourceLocation& loc) {
    return "{\"line\":" + std::to_string(loc.line) +
           ",\"column\":" + std::to_string(loc.column) + "}";
}

static std::string json_range(const SourceRange& range) {
    return "{\"start\":" + json_location(range.start) +
           ",\"end\":" + json_location(range.end) + "}";
}

static std::string json_target(const DiagnosticTarget& target) {
    return "{\"graph\":" + json_str(target.graph) +
           ",\"block_kind\":" + json_str(target.block_kind) +
           ",\"block_name\":" + json_str(target.block_name) +
           ",\"node_instance\":" + json_str(target.node_instance) +
           ",\"pin_name\":" + json_str(target.pin_name) +
           ",\"parameter_name\":" + json_str(target.parameter_name) +
           ",\"reference\":" + json_str(target.reference) +
           ",\"connection_kind\":" + json_str(target.connection_kind) + "}";
}

static std::string append_occurrence_suffix(const std::string& base_id, std::unordered_map<std::string, size_t>& seen_ids) {
    size_t& seen = seen_ids[base_id];
    ++seen;
    if (seen == 1) return base_id;
    return base_id + "#" + std::to_string(seen);
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

static std::string json_actions(const std::string& diagnostic_id, const std::vector<DiagnosticAction>& actions) {
    std::string json = "[";
    std::unordered_map<std::string, size_t> seen_action_ids;
    for (size_t i = 0; i < actions.size(); ++i) {
        if (i > 0) json += ",";
        const auto action_id = append_occurrence_suffix(diagnostic_action_id_base(diagnostic_id, actions[i]), seen_action_ids);
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

static std::string json_diagnostic(const std::string& diagnostic_id, const Diagnostic& diag) {
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

static std::string json_diagnostics(const std::vector<Diagnostic>& diagnostics) {
    std::string json = "[";
    std::unordered_map<std::string, size_t> seen_diagnostic_ids;
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        if (i > 0) json += ",";
        const auto diagnostic_id = append_occurrence_suffix(diagnostic_id_base(diagnostics[i]), seen_diagnostic_ids);
        json += json_diagnostic(diagnostic_id, diagnostics[i]);
    }
    json += "]";
    return json;
}

static std::string unescape_json_string(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out += s[i];
            continue;
        }
        char next = s[++i];
        switch (next) {
            case '"':  out += '"'; break;
            case '\\': out += '\\'; break;
            case '/':  out += '/'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            default:   out += next; break;
        }
    }
    return out;
}

static std::string extract_json_source(const std::string& body) {
    auto key = body.find("\"source\"");
    if (key == std::string::npos) return body;
    auto colon = body.find(':', key);
    if (colon == std::string::npos) return body;
    auto q1 = body.find('"', colon + 1);
    if (q1 == std::string::npos) return body;

    std::string raw;
    bool escaped = false;
    for (size_t i = q1 + 1; i < body.size(); ++i) {
        char c = body[i];
        if (escaped) {
            raw += '\\';
            raw += c;
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') return unescape_json_string(raw);
        raw += c;
    }
    return body;
}

static std::optional<std::string> extract_json_string_field(const std::string& body, const std::string& key) {
    auto key_pos = body.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return std::nullopt;
    auto colon = body.find(':', key_pos);
    if (colon == std::string::npos) return std::nullopt;
    auto q1 = body.find('"', colon + 1);
    if (q1 == std::string::npos) return std::nullopt;

    std::string raw;
    bool escaped = false;
    for (size_t i = q1 + 1; i < body.size(); ++i) {
        char c = body[i];
        if (escaped) {
            raw += '\\';
            raw += c;
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') return unescape_json_string(raw);
        raw += c;
    }
    return std::nullopt;
}

static std::optional<uint32_t> extract_json_u32_field(const std::string& body, const std::string& key) {
    auto key_pos = body.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return std::nullopt;
    auto colon = body.find(':', key_pos);
    if (colon == std::string::npos) return std::nullopt;
    auto begin = colon + 1;
    while (begin < body.size() && std::isspace(static_cast<unsigned char>(body[begin]))) ++begin;
    auto end = begin;
    while (end < body.size() && std::isdigit(static_cast<unsigned char>(body[end]))) ++end;
    if (begin == end) return std::nullopt;

    uint32_t value = 0;
    auto result = std::from_chars(body.data() + begin, body.data() + end, value);
    if (result.ec != std::errc() || result.ptr != body.data() + end || value == 0) return std::nullopt;
    return value;
}

static std::optional<bool> extract_json_bool_field(const std::string& body, const std::string& key) {
    auto key_pos = body.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return std::nullopt;
    auto colon = body.find(':', key_pos);
    if (colon == std::string::npos) return std::nullopt;
    auto begin = colon + 1;
    while (begin < body.size() && std::isspace(static_cast<unsigned char>(body[begin]))) ++begin;
    if (body.compare(begin, 4, "true") == 0) return true;
    if (body.compare(begin, 5, "false") == 0) return false;
    return std::nullopt;
}

static SourceDiagnosticsOptions source_diagnostics_options_from_json(const std::string& body) {
    SourceDiagnosticsOptions options;
    if (auto resolve_imports = extract_json_bool_field(body, "resolve_imports")) {
        options.resolve_imports = *resolve_imports;
    }
    if (auto base_dir = extract_json_string_field(body, "base_dir")) {
        options.base_dir = *base_dir;
    } else if (auto source_path = extract_json_string_field(body, "source_path")) {
        auto slash = source_path->find_last_of("/\\");
        if (slash != std::string::npos) options.base_dir = source_path->substr(0, slash);
    }
    return options;
}

static std::optional<std::string> source_environment_guard_error(
    const std::string& source,
    const std::optional<std::string>& expected_hash,
    const Environment& env,
    const SourceDiagnosticsOptions& options) {
    if (!expected_hash || expected_hash->empty()) return std::nullopt;

    auto actual = source_diagnostics_environment_hash(source, env, options);
    if (actual.is_err()) return actual.error();
    if (actual.value() != *expected_hash) {
        return "Source environment hash mismatch";
    }
    return std::nullopt;
}

static std::string source_environment_guard_args(
    const std::optional<std::string>& environment_hash,
    const SourceDiagnosticsOptions& options) {
    if (!environment_hash || environment_hash->empty()) return "";

    std::string args = " --env-hash " + command_arg(*environment_hash);
    if (options.resolve_imports) args += " --resolve-imports";
    if (!options.base_dir.empty()) args += " --base-dir " + command_arg(options.base_dir);
    return args;
}

static std::string source_replay_source_name(const SourceDiagnosticsOptions& options) {
    if (options.base_dir.empty()) return "";
    return (std::filesystem::path(options.base_dir) / "__source_replay__.gs").string();
}

static std::string source_patch_command(uint32_t start_line, uint32_t start_column,
                                        uint32_t end_line, uint32_t end_column,
                                        const std::string& replacement,
                                        const std::string& base_hash = "",
                                        const std::optional<std::string>& environment_hash = std::nullopt,
                                        const SourceDiagnosticsOptions& options = {}) {
    std::string cmd = "apply_source_patch " +
        std::to_string(start_line) + " " +
        std::to_string(start_column) + " " +
        std::to_string(end_line) + " " +
        std::to_string(end_column);
    std::string encoded = base64_encode(replacement);
    if (!encoded.empty()) {
        cmd += " " + encoded;
    } else if (!base_hash.empty()) {
        cmd += " -";
    }
    if (!base_hash.empty()) cmd += " " + base_hash;
    cmd += source_environment_guard_args(environment_hash, options);
    return cmd;
}

int WebServer::run() {
    httplib::Server svr;

    // ── Serve web UI (Vite build output from webapp/dist) ─────────
    if (!web_dir_.empty()) {
        svr.set_mount_point("/", web_dir_);
    }

    // set_mount_point only succeeds when the directory exists at server startup.
    // These dynamic routes let a running dev server pick up a freshly built dist.
    svr.Get(R"(/assets/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        serve_web_file(web_dir_, "assets/" + std::string(req.matches[1]), res);
    });
    svr.Get(R"(/(favicon\.svg|icons\.svg))", [this](const httplib::Request& req, httplib::Response& res) {
        serve_web_file(web_dir_, std::string(req.matches[1]), res);
    });

    // Fallback: serve index.html for SPA client-side routing
    svr.Get("/", [this](const httplib::Request&, httplib::Response& res) {
        std::string html = read_file(web_dir_ + "/index.html");
        if (html.empty()) {
            res.set_content("<h1>GraphScript Editor</h1>"
                "<p>webapp/dist/index.html not found at: " + web_dir_ + "</p>",
                "text/html");
        } else {
            res.set_content(html, "text/html");
        }
    });

    // ── GET /api/state  → full JSON state ─────────────────────────
    svr.Get("/api/state", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex_);
        res.set_content(session_.state_to_json(), "application/json");
    });

    // ── GET /api/diagnostics → active-graph validation diagnostics ─
    svr.Get("/api/diagnostics", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string json = "{\"ok\":true,\"stage\":\"session\",\"diagnostics\":" +
                           session_.diagnostics_to_json() + "}";
        res.set_content(json, "application/json");
    });

    // ── POST /api/diagnostics → parse/compile diagnostics for source
    svr.Post("/api/diagnostics", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string source = req.body;
        SourceDiagnosticsOptions options;
        if (!source.empty() && source[0] == '{') {
            source = extract_json_source(req.body);
            options = source_diagnostics_options_from_json(req.body);
        }
        if (source.empty()) source = session_.emit();
        res.set_content(source_diagnostics_to_json(source, session_.env(), options), "application/json");
    });

    // ── POST /api/source → replace session module from source text ─
    svr.Post("/api/source", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string source = req.body;
        SourceDiagnosticsOptions options;
        std::optional<std::string> environment_hash;
        if (!source.empty() && source[0] == '{') {
            source = extract_json_source(req.body);
            options = source_diagnostics_options_from_json(req.body);
            environment_hash = extract_json_string_field(req.body, "environment_hash");
        }
        if (source.empty()) {
            res.set_content("{\"ok\":false,\"error\":\"Empty source\",\"state\":" +
                            session_.state_to_json() + "}", "application/json");
            return;
        }

        if (auto guard_error = source_environment_guard_error(source, environment_hash, session_.env(), options)) {
            res.set_content("{\"ok\":false,\"error\":" + json_str(*guard_error) +
                            ",\"state\":" + session_.state_to_json() + "}", "application/json");
            return;
        }

        auto result = session_.load_source(source, source_replay_source_name(options));
        if (result.is_err()) {
            res.set_content("{\"ok\":false,\"error\":" + json_str(result.error()) +
                            ",\"state\":" + session_.state_to_json() + "}", "application/json");
            return;
        }

        session_.log_command("apply_source_b64 " + base64_encode(source) +
                             source_environment_guard_args(environment_hash, options));
        res.set_content("{\"ok\":true,\"state\":" + session_.state_to_json() + "}", "application/json");
    });

    // ── POST /api/source_patch → apply a source range edit through CLI replay ─
    svr.Post("/api/source_patch", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto start_line = extract_json_u32_field(req.body, "start_line");
        auto start_column = extract_json_u32_field(req.body, "start_column");
        auto end_line = extract_json_u32_field(req.body, "end_line");
        auto end_column = extract_json_u32_field(req.body, "end_column");
        auto replacement = extract_json_string_field(req.body, "replacement");
        auto base_hash = extract_json_string_field(req.body, "base_hash");
        auto fallback_source = extract_json_string_field(req.body, "fallback_source");
        auto environment_hash = extract_json_string_field(req.body, "environment_hash");
        auto options = source_diagnostics_options_from_json(req.body);

        if (!start_line || !start_column || !end_line || !end_column || !replacement) {
            res.set_content("{\"ok\":false,\"error\":\"Invalid source patch request\",\"state\":" +
                            session_.state_to_json() + "}", "application/json");
            return;
        }

        if (environment_hash && (!fallback_source || fallback_source->empty())) {
            res.set_content("{\"ok\":false,\"error\":\"Source environment hash requires fallback_source\",\"state\":" +
                            session_.state_to_json() + "}", "application/json");
            return;
        }
        if (fallback_source) {
            if (auto guard_error = source_environment_guard_error(*fallback_source, environment_hash, session_.env(), options)) {
                res.set_content("{\"ok\":false,\"error\":" + json_str(*guard_error) +
                                ",\"state\":" + session_.state_to_json() + "}", "application/json");
                return;
            }
        }

        std::string current_source = session_.emit();
        if (base_hash && *base_hash != source_hash(current_source)) {
            if (!fallback_source || fallback_source->empty()) {
                res.set_content("{\"ok\":false,\"error\":\"Source patch base hash mismatch\",\"state\":" +
                                session_.state_to_json() + "}", "application/json");
                return;
            }

            auto result = session_.load_source(*fallback_source, source_replay_source_name(options));
            if (result.is_err()) {
                res.set_content("{\"ok\":false,\"error\":" + json_str(result.error()) +
                                ",\"state\":" + session_.state_to_json() + "}", "application/json");
                return;
            }

            std::string cmd = "apply_source_b64 " + base64_encode(*fallback_source) +
                              source_environment_guard_args(environment_hash, options);
            session_.log_command(cmd);
            res.set_content("{\"ok\":true,\"command\":" + json_str(cmd) +
                            ",\"fallback\":true,\"state\":" + session_.state_to_json() + "}", "application/json");
            return;
        }

        std::string cmd = source_patch_command(*start_line, *start_column, *end_line, *end_column,
                                               *replacement, base_hash ? *base_hash : "",
                                               environment_hash, options);
        std::ostringstream capture;
        auto* old_buf = std::cout.rdbuf(capture.rdbuf());
        cli_.execute(cmd);
        std::cout.rdbuf(old_buf);

        std::string output = capture.str();
        if (!cli_.last_command_succeeded()) {
            res.set_content("{\"ok\":false,\"error\":" + json_str(output) +
                            ",\"state\":" + session_.state_to_json() + "}", "application/json");
            return;
        }

        session_.log_command(cmd);
        res.set_content("{\"ok\":true,\"command\":" + json_str(cmd) +
                        ",\"state\":" + session_.state_to_json() + "}", "application/json");
    });

    // ── POST /api/exec  → execute CLI command ─────────────────────
    svr.Post("/api/exec", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Parse command from body (plain text or JSON { "command": "..." })
        std::string cmd = req.body;
        if (!cmd.empty() && cmd[0] == '{') {
            auto start = cmd.find("\"command\"");
            if (start != std::string::npos) {
                auto q1 = cmd.find('"', start + 9);
                if (q1 != std::string::npos) {
                    q1++;
                    auto q2 = cmd.find('"', q1);
                    if (q2 != std::string::npos) cmd = cmd.substr(q1, q2 - q1);
                }
            }
        }

        if (cmd.empty()) {
            res.set_content("{\"ok\":false,\"error\":\"Empty command\"}", "application/json");
            return;
        }

        // Capture stdout for the command output
        session_.log_command(cmd);
        std::ostringstream capture;
        auto* old_buf = std::cout.rdbuf(capture.rdbuf());
        cli_.execute(cmd);
        std::cout.rdbuf(old_buf);

        std::string output = capture.str();
        bool ok = cli_.last_command_succeeded();
        std::string json = "{\"ok\":" + std::string(ok ? "true" : "false") +
                           ",\"command\":\"" + esc(cmd) +
                           "\",\"output\":\"" + esc(output) + "\"";
        if (!ok) json += ",\"error\":\"" + esc(output) + "\"";
        json += ",\"state\":" + session_.state_to_json() + "}";
        res.set_content(json, "application/json");
    });

    // ── POST /api/undo ────────────────────────────────────────────
    svr.Post("/api/undo", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex_);
        session_.log_command("undo");
        auto r = session_.undo();
        std::string desc = r.is_ok() ? r.value() : r.error();
        std::string json = "{\"ok\":" + std::string(r.is_ok() ? "true" : "false") +
                           ",\"description\":\"" + esc(desc) +
                           "\",\"state\":" + session_.state_to_json() + "}";
        res.set_content(json, "application/json");
    });

    // ── POST /api/redo ────────────────────────────────────────────
    svr.Post("/api/redo", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex_);
        session_.log_command("redo");
        auto r = session_.redo();
        std::string desc = r.is_ok() ? r.value() : r.error();
        std::string json = "{\"ok\":" + std::string(r.is_ok() ? "true" : "false") +
                           ",\"description\":\"" + esc(desc) +
                           "\",\"state\":" + session_.state_to_json() + "}";
        res.set_content(json, "application/json");
    });

    // ── GET /api/emit → .gs text ──────────────────────────────────
    svr.Get("/api/emit", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex_);
        res.set_content(session_.emit(), "text/plain");
    });

    // ── GET /api/declaration_source?path=<file.d.gs> → declaration text ──
    svr.Get("/api/declaration_source", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("path")) {
            res.status = 400;
            res.set_content("{\"ok\":false,\"error\":\"Missing declaration path\"}", "application/json");
            return;
        }

        const std::string path = req.get_param_value("path");
        if (!has_suffix(path, ".d.gs")) {
            res.status = 400;
            res.set_content("{\"ok\":false,\"error\":\"Only .d.gs declaration files can be previewed\"}", "application/json");
            return;
        }

        std::error_code ec;
        const auto fs_path = std::filesystem::path(path);
        if (!std::filesystem::exists(fs_path, ec) || !std::filesystem::is_regular_file(fs_path, ec)) {
            res.status = 404;
            res.set_content("{\"ok\":false,\"error\":\"Declaration file not found\"}", "application/json");
            return;
        }

        const auto size = std::filesystem::file_size(fs_path, ec);
        if (ec) {
            res.status = 500;
            res.set_content("{\"ok\":false,\"error\":\"Cannot stat declaration file\"}", "application/json");
            return;
        }
        if (size > 1024 * 1024) {
            res.status = 413;
            res.set_content("{\"ok\":false,\"error\":\"Declaration file is too large to preview\"}", "application/json");
            return;
        }

        const std::string source = read_file(path);
        if (source.empty() && size > 0) {
            res.status = 500;
            res.set_content("{\"ok\":false,\"error\":\"Cannot read declaration file\"}", "application/json");
            return;
        }

        const auto normalized = std::filesystem::weakly_canonical(fs_path, ec);
        const std::string normalized_path = ec ? path : normalized.string();
        const std::string json = "{\"ok\":true,\"path\":" + json_str(path) +
            ",\"normalized_path\":" + json_str(normalized_path) +
            ",\"content_hash\":" + json_str(source_hash(source)) +
            ",\"source\":" + json_str(source) + "}";
        res.set_content(json, "application/json");
    });

    // ── CORS headers for local development ────────────────────────
    svr.set_pre_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        return httplib::Server::HandlerResponse::Unhandled;
    });

    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.set_content("", "text/plain");
    });

    std::cout << "GraphScript Editor Server running at http://localhost:" << port_ << "\n";
    std::cout << "Press Ctrl+C to stop.\n";

    if (!svr.listen("0.0.0.0", port_)) {
        std::cerr << "Failed to start server on port " << port_ << "\n";
        return 1;
    }
    return 0;
}

} // namespace gs
