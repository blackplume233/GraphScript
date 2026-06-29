#include "editor.h"
#include "source_diagnostics.h"
#include "graphscript/compile/compiler.h"
#include "graphscript/parse/lexer.h"
#include "graphscript/parse/parser.h"
#include "graphscript/runtime/runtime_graph.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gs {

CLIEditor::CLIEditor(EditSession& session) : session_(session) {}

static int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static std::optional<std::string> base64_decode(const std::string& encoded) {
    std::string out;
    int value = 0;
    int bits = -8;
    bool padding = false;

    for (char c : encoded) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        if (c == '=') {
            padding = true;
            continue;
        }
        if (padding) return std::nullopt;
        int decoded = base64_value(c);
        if (decoded < 0) return std::nullopt;
        value = (value << 6) + decoded;
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<char>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }

    return out;
}

static std::optional<uint32_t> parse_u32(const std::string& text) {
    uint32_t value = 0;
    auto* begin = text.data();
    auto* end = text.data() + text.size();
    auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc() || result.ptr != end || value == 0) return std::nullopt;
    return value;
}

static std::optional<size_t> offset_for_location(const std::string& source, const SourceLocation& location) {
    if (location.line == 0 || location.column == 0) return std::nullopt;

    uint32_t line = 1;
    uint32_t column = 1;
    for (size_t index = 0; index < source.size(); ++index) {
        if (line == location.line && column == location.column) return index;

        char c = source[index];
        if (c == '\r') {
            if (index + 1 < source.size() && source[index + 1] == '\n') ++index;
            ++line;
            column = 1;
            continue;
        }
        if (c == '\n') {
            ++line;
            column = 1;
            continue;
        }
        ++column;
    }

    if (line == location.line && column == location.column) return source.size();
    return std::nullopt;
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

static std::optional<std::string> read_text_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return std::nullopt;
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

static Result<void, std::string> write_text_file(const std::string& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return Result<void, std::string>::err("Cannot write file: " + path);
    file << text;
    if (!file.good()) return Result<void, std::string>::err("Failed to write file: " + path);
    return Result<void, std::string>::ok();
}

struct SourceReplayGuard {
    std::optional<std::string> environment_hash;
    SourceDiagnosticsOptions options;
};

struct SourcePatchRecord {
    SourceRange range;
    std::string replacement;
};

struct ResolvedSourcePatch {
    size_t start = 0;
    size_t end = 0;
    std::string replacement;
};

static Result<SourceReplayGuard, std::string> parse_source_replay_guard(
    const std::vector<std::string>& args,
    size_t start) {
    SourceReplayGuard guard;
    for (size_t i = start; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg == "--env-hash") {
            if (i + 1 >= args.size() || args[i + 1].empty()) {
                return Result<SourceReplayGuard, std::string>::err("Usage: --env-hash <environment-hash>");
            }
            guard.environment_hash = args[++i];
        } else if (arg == "--resolve-imports") {
            guard.options.resolve_imports = true;
        } else if (arg == "--base-dir") {
            if (i + 1 >= args.size()) {
                return Result<SourceReplayGuard, std::string>::err("Usage: --base-dir <directory>");
            }
            guard.options.base_dir = args[++i];
        } else {
            return Result<SourceReplayGuard, std::string>::err("Unknown source replay option: " + arg);
        }
    }
    return Result<SourceReplayGuard, std::string>::ok(guard);
}

static Result<void, std::string> verify_source_replay_guard(
    const std::string& source,
    const Environment& env,
    const SourceReplayGuard& guard) {
    if (!guard.environment_hash || guard.environment_hash->empty()) {
        return Result<void, std::string>::ok();
    }

    auto actual = source_diagnostics_environment_hash(source, env, guard.options);
    if (actual.is_err()) {
        return Result<void, std::string>::err(actual.error());
    }
    if (actual.value() != *guard.environment_hash) {
        return Result<void, std::string>::err("Source environment hash mismatch");
    }
    return Result<void, std::string>::ok();
}

static std::string source_replay_source_name(const SourceReplayGuard& guard) {
    if (guard.options.base_dir.empty()) return "";
    return (std::filesystem::path(guard.options.base_dir) / "__source_replay__.gs").string();
}

static Result<std::vector<SourcePatchRecord>, std::string> parse_source_patch_records(const std::string& text) {
    std::vector<SourcePatchRecord> records;
    std::istringstream input(text);
    std::string line;
    size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty()) continue;

        std::istringstream row(line);
        std::string start_line_text;
        std::string start_column_text;
        std::string end_line_text;
        std::string end_column_text;
        std::string replacement_text;
        std::string extra;
        if (!(row >> start_line_text >> start_column_text >> end_line_text >> end_column_text >> replacement_text) ||
            (row >> extra)) {
            return Result<std::vector<SourcePatchRecord>, std::string>::err(
                "Invalid source patch record on line " + std::to_string(line_number));
        }

        auto start_line = parse_u32(start_line_text);
        auto start_column = parse_u32(start_column_text);
        auto end_line = parse_u32(end_line_text);
        auto end_column = parse_u32(end_column_text);
        if (!start_line || !start_column || !end_line || !end_column) {
            return Result<std::vector<SourcePatchRecord>, std::string>::err(
                "Invalid source patch range on line " + std::to_string(line_number));
        }

        SourcePatchRecord record;
        record.range.start.line = *start_line;
        record.range.start.column = *start_column;
        record.range.end.line = *end_line;
        record.range.end.column = *end_column;
        if (replacement_text != "-") {
            auto replacement = base64_decode(replacement_text);
            if (!replacement) {
                return Result<std::vector<SourcePatchRecord>, std::string>::err(
                    "Invalid source patch replacement on line " + std::to_string(line_number));
            }
            record.replacement = *replacement;
        }
        records.push_back(record);
    }

    if (records.empty()) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Source patch list is empty");
    }
    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::string, std::string> apply_source_patch_records(
    const std::string& source,
    const std::vector<SourcePatchRecord>& records) {
    std::vector<ResolvedSourcePatch> resolved;
    resolved.reserve(records.size());

    for (const auto& record : records) {
        auto start = offset_for_location(source, record.range.start);
        auto end = offset_for_location(source, record.range.end);
        if (!start || !end || *start > *end) {
            return Result<std::string, std::string>::err("Source patch range is outside current source");
        }
        resolved.push_back({*start, *end, record.replacement});
    }

    std::sort(resolved.begin(), resolved.end(), [](const ResolvedSourcePatch& a, const ResolvedSourcePatch& b) {
        if (a.start != b.start) return a.start > b.start;
        return a.end > b.end;
    });
    for (size_t i = 1; i < resolved.size(); ++i) {
        const auto& later = resolved[i - 1];
        const auto& earlier = resolved[i];
        if (earlier.end > later.start) {
            return Result<std::string, std::string>::err("Source patch ranges overlap");
        }
    }

    std::string patched = source;
    for (const auto& patch : resolved) {
        patched.replace(patch.start, patch.end - patch.start, patch.replacement);
    }
    return Result<std::string, std::string>::ok(patched);
}

static SourceLocation identifier_end_location(const Token& token) {
    SourceLocation end = token.location;
    end.column += static_cast<uint32_t>(token.text.size());
    return end;
}

static bool is_identifier_text(const std::string& text) {
    auto tokens = Lexer(text).tokenize();
    return tokens.size() == 2 &&
           tokens[0].type == TokenType::Identifier &&
           tokens[0].text == text &&
           tokens[1].type == TokenType::EndOfFile;
}

static Result<std::vector<SourcePatchRecord>, std::string> build_identifier_rename_patches(
    const std::string& source,
    const std::string& old_name,
    const std::string& new_name) {
    if (!is_identifier_text(old_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Old source identifier is invalid");
    }
    if (!is_identifier_text(new_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("New source identifier is invalid");
    }
    if (old_name == new_name) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Source identifier rename must change the name");
    }

    std::vector<SourcePatchRecord> records;
    auto tokens = Lexer(source).tokenize();
    for (const auto& token : tokens) {
        if (token.type == TokenType::Error) {
            return Result<std::vector<SourcePatchRecord>, std::string>::err("Cannot rename identifiers in invalid source");
        }
        if (token.type != TokenType::Identifier || token.text != old_name) continue;

        SourcePatchRecord record;
        record.range.start = token.location;
        record.range.end = identifier_end_location(token);
        record.replacement = new_name;
        records.push_back(record);
    }

    if (records.empty()) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Source identifier rename found no matches");
    }
    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static bool source_range_matches_text(const std::string& source, const SourceRange& range, const std::string& text) {
    auto start = offset_for_location(source, range.start);
    auto end = offset_for_location(source, range.end);
    if (!start || !end || *start > *end) return false;
    return source.compare(*start, *end - *start, text) == 0;
}

static SourceRange token_range_to_patch_range(SourceRange range) {
    ++range.end.column;
    return range;
}

static Result<void, std::string> add_checked_source_rename_patch(
    const std::string& source,
    const SourceRange& range,
    const std::string& old_name,
    const std::string& new_name,
    const std::string& label,
    std::vector<SourcePatchRecord>& records) {
    SourceRange patch_range = token_range_to_patch_range(range);
    if (!source_range_matches_text(source, patch_range, old_name)) {
        return Result<void, std::string>::err("Source range for " + label + " does not match '" + old_name + "'");
    }

    SourcePatchRecord record;
    record.range = patch_range;
    record.replacement = new_name;
    records.push_back(record);
    return Result<void, std::string>::ok();
}

static bool is_default_source_range(const SourceRange& range) {
    return range.start.line == 1 && range.start.column == 1 &&
           range.end.line == 1 && range.end.column == 1;
}

static Result<void, std::string> add_optional_type_reference_patch(
    const std::string& source,
    const SourceRange& range,
    const std::string& old_name,
    const std::string& new_name,
    const std::string& label,
    std::vector<SourcePatchRecord>& records) {
    if (is_default_source_range(range)) {
        return Result<void, std::string>::ok();
    }
    auto patch_range = token_range_to_patch_range(range);
    if (!source_range_matches_text(source, patch_range, old_name)) {
        return Result<void, std::string>::ok();
    }
    return add_checked_source_rename_patch(source, range, old_name, new_name, label, records);
}

static Result<void, std::string> add_annotation_constructor_type_rename_patches(
    const std::string& source,
    const std::vector<Annotation>& annotations,
    const std::string& old_name,
    const std::string& new_name,
    const std::string& label,
    std::vector<SourcePatchRecord>& records) {
    for (const auto& annotation : annotations) {
        for (const auto& arg : annotation.args) {
            auto added = add_optional_type_reference_patch(
                source,
                arg.value_constructor_type_range,
                old_name,
                new_name,
                label + " annotation constructor type reference",
                records);
            if (added.is_err()) return added;
        }
    }
    return Result<void, std::string>::ok();
}

static Result<std::vector<SourcePatchRecord>, std::string> build_active_graph_param_rename_patches(
    const std::string& source,
    const Module& module,
    const Graph& graph,
    const std::string& old_name,
    const std::string& new_name) {
    if (!is_identifier_text(old_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Old parameter identifier is invalid");
    }
    if (!is_identifier_text(new_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("New parameter identifier is invalid");
    }
    if (old_name == new_name) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Source parameter rename must change the name");
    }

    const GraphParameter* target = nullptr;
    for (const auto& param : graph.parameters) {
        if (param.name == new_name) {
            return Result<std::vector<SourcePatchRecord>, std::string>::err("Parameter '" + new_name + "' already exists");
        }
        if (param.name == old_name) target = &param;
    }
    if (!target) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Parameter '" + old_name + "' not found");
    }

    std::vector<SourcePatchRecord> records;
    auto added_decl = add_checked_source_rename_patch(
        source, target->name_range, old_name, new_name, "parameter declaration", records);
    if (added_decl.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added_decl.error());

    auto add_block_refs = [&](const LogicBlock& block) -> Result<void, std::string> {
        for (const auto& link : block.data_links) {
            if (!link.source.pin_name.empty() || link.source.node_instance != old_name) continue;
            auto added = add_checked_source_rename_patch(
                source, link.source_node_range, old_name, new_name, "bare parameter reference", records);
            if (added.is_err()) return added;
        }
        return Result<void, std::string>::ok();
    };
    for (const auto& event : graph.events) {
        auto added = add_block_refs(event);
        if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
    }
    for (const auto& function : graph.functions) {
        auto added = add_block_refs(function);
        if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
    }

    auto is_graph_node_instance = [&](const Graph& owner, const std::string& instance_name) {
        for (const auto& node : owner.node_instances) {
            if (node.instance_name == instance_name && node.type_name == graph.name) return true;
        }
        return false;
    };
    auto add_graph_node_pin_refs = [&](const Graph& owner, const LogicBlock& block) -> Result<void, std::string> {
        for (const auto& link : block.data_links) {
            if (link.target.pin_name == old_name && is_graph_node_instance(owner, link.target.node_instance)) {
                auto added = add_checked_source_rename_patch(
                    source, link.target_pin_range, old_name, new_name, "graph-node target pin reference", records);
                if (added.is_err()) return added;
            }
            if (link.source.pin_name == old_name && is_graph_node_instance(owner, link.source.node_instance)) {
                auto added = add_checked_source_rename_patch(
                    source, link.source_pin_range, old_name, new_name, "graph-node source pin reference", records);
                if (added.is_err()) return added;
            }
        }
        return Result<void, std::string>::ok();
    };
    for (const auto& owner : module.graphs) {
        for (const auto& event : owner.events) {
            auto added = add_graph_node_pin_refs(owner, event);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
        for (const auto& function : owner.functions) {
            auto added = add_graph_node_pin_refs(owner, function);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
    }

    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::vector<SourcePatchRecord>, std::string> build_optional_graph_param_reference_rename_patches(
    const std::string& source,
    const Module& module,
    const std::string& graph_name,
    const std::string& old_name,
    const std::string& new_name) {
    if (!is_identifier_text(graph_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Graph identifier is invalid");
    }
    if (!is_identifier_text(old_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Old parameter identifier is invalid");
    }
    if (!is_identifier_text(new_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("New parameter identifier is invalid");
    }
    if (old_name == new_name) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("File graph parameter rename must change the name");
    }

    const Graph* target_graph = nullptr;
    const GraphParameter* target_param = nullptr;
    bool has_new_param = false;
    for (const auto& graph : module.graphs) {
        if (graph.name != graph_name) continue;
        target_graph = &graph;
        for (const auto& param : graph.parameters) {
            if (param.name == old_name) target_param = &param;
            if (param.name == new_name) has_new_param = true;
        }
        break;
    }
    if (target_param && has_new_param) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err(
            "Parameter '" + new_name + "' already exists on graph '" + graph_name + "'");
    }

    std::vector<SourcePatchRecord> records;
    if (target_graph && target_param) {
        auto added_decl = add_checked_source_rename_patch(
            source, target_param->name_range, old_name, new_name, "graph parameter declaration", records);
        if (added_decl.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added_decl.error());

        auto add_bare_param_refs = [&](const LogicBlock& block) -> Result<void, std::string> {
            for (const auto& link : block.data_links) {
                if (!link.source.pin_name.empty() || link.source.node_instance != old_name) continue;
                auto added = add_checked_source_rename_patch(
                    source, link.source_node_range, old_name, new_name, "bare graph parameter reference", records);
                if (added.is_err()) return added;
            }
            return Result<void, std::string>::ok();
        };
        for (const auto& event : target_graph->events) {
            auto added = add_bare_param_refs(event);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
        for (const auto& function : target_graph->functions) {
            auto added = add_bare_param_refs(function);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
    }

    auto is_graph_node_instance = [&](const Graph& owner, const std::string& instance_name) {
        for (const auto& node : owner.node_instances) {
            if (node.instance_name == instance_name && node.type_name == graph_name) return true;
        }
        return false;
    };
    auto add_graph_node_pin_refs = [&](const Graph& owner, const LogicBlock& block) -> Result<void, std::string> {
        for (const auto& link : block.data_links) {
            if (link.target.pin_name == old_name && is_graph_node_instance(owner, link.target.node_instance)) {
                auto added = add_checked_source_rename_patch(
                    source, link.target_pin_range, old_name, new_name, "graph-node target data pin reference", records);
                if (added.is_err()) return added;
            }
            if (link.source.pin_name == old_name && is_graph_node_instance(owner, link.source.node_instance)) {
                auto added = add_checked_source_rename_patch(
                    source, link.source_pin_range, old_name, new_name, "graph-node source data pin reference", records);
                if (added.is_err()) return added;
            }
        }
        return Result<void, std::string>::ok();
    };
    for (const auto& owner : module.graphs) {
        for (const auto& event : owner.events) {
            auto added = add_graph_node_pin_refs(owner, event);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
        for (const auto& function : owner.functions) {
            auto added = add_graph_node_pin_refs(owner, function);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
    }

    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::vector<SourcePatchRecord>, std::string> build_active_graph_event_rename_patches(
    const std::string& source,
    const Module& module,
    const Graph& graph,
    const std::string& old_name,
    const std::string& new_name) {
    if (!is_identifier_text(old_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Old event identifier is invalid");
    }
    if (!is_identifier_text(new_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("New event identifier is invalid");
    }
    if (old_name == new_name) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Source event rename must change the name");
    }

    const Event* target = nullptr;
    for (const auto& event : graph.events) {
        if (event.name == new_name) {
            return Result<std::vector<SourcePatchRecord>, std::string>::err("Event '" + new_name + "' already exists");
        }
        if (event.name == old_name) target = &event;
    }
    if (!target) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Event '" + old_name + "' not found");
    }

    std::vector<SourcePatchRecord> records;
    auto added_decl = add_checked_source_rename_patch(
        source, target->name_range, old_name, new_name, "event declaration", records);
    if (added_decl.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added_decl.error());

    auto is_graph_node_instance = [&](const Graph& owner, const std::string& instance_name) {
        for (const auto& node : owner.node_instances) {
            if (node.instance_name == instance_name && node.type_name == graph.name) return true;
        }
        return false;
    };
    auto add_graph_node_exec_refs = [&](const Graph& owner, const LogicBlock& block) -> Result<void, std::string> {
        for (const auto& flow : block.flow_connections) {
            if (flow.from.pin_name == old_name && is_graph_node_instance(owner, flow.from.node_instance)) {
                auto added = add_checked_source_rename_patch(
                    source, flow.from_pin_range, old_name, new_name, "graph-node source exec pin reference", records);
                if (added.is_err()) return added;
            }
            if (flow.to.pin_name == old_name && is_graph_node_instance(owner, flow.to.node_instance)) {
                auto added = add_checked_source_rename_patch(
                    source, flow.to_pin_range, old_name, new_name, "graph-node target exec pin reference", records);
                if (added.is_err()) return added;
            }
        }
        return Result<void, std::string>::ok();
    };
    for (const auto& owner : module.graphs) {
        for (const auto& event : owner.events) {
            auto added = add_graph_node_exec_refs(owner, event);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
        for (const auto& function : owner.functions) {
            auto added = add_graph_node_exec_refs(owner, function);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
    }

    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::vector<SourcePatchRecord>, std::string> build_optional_graph_event_reference_rename_patches(
    const std::string& source,
    const Module& module,
    const std::string& graph_name,
    const std::string& old_name,
    const std::string& new_name) {
    if (!is_identifier_text(graph_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Graph identifier is invalid");
    }
    if (!is_identifier_text(old_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Old event identifier is invalid");
    }
    if (!is_identifier_text(new_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("New event identifier is invalid");
    }
    if (old_name == new_name) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("File graph event rename must change the name");
    }

    const Graph* target_graph = nullptr;
    const Event* target_event = nullptr;
    bool has_new_event = false;
    for (const auto& graph : module.graphs) {
        if (graph.name != graph_name) continue;
        target_graph = &graph;
        for (const auto& event : graph.events) {
            if (event.name == old_name) target_event = &event;
            if (event.name == new_name) has_new_event = true;
        }
        break;
    }
    if (target_event && has_new_event) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err(
            "Event '" + new_name + "' already exists on graph '" + graph_name + "'");
    }

    std::vector<SourcePatchRecord> records;
    if (target_graph && target_event) {
        auto added_decl = add_checked_source_rename_patch(
            source, target_event->name_range, old_name, new_name, "graph event declaration", records);
        if (added_decl.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added_decl.error());
    }

    auto is_graph_node_instance = [&](const Graph& owner, const std::string& instance_name) {
        for (const auto& node : owner.node_instances) {
            if (node.instance_name == instance_name && node.type_name == graph_name) return true;
        }
        return false;
    };
    auto add_graph_node_exec_refs = [&](const Graph& owner, const LogicBlock& block) -> Result<void, std::string> {
        for (const auto& flow : block.flow_connections) {
            if (flow.from.pin_name == old_name && is_graph_node_instance(owner, flow.from.node_instance)) {
                auto added = add_checked_source_rename_patch(
                    source, flow.from_pin_range, old_name, new_name, "graph-node source exec pin reference", records);
                if (added.is_err()) return added;
            }
            if (flow.to.pin_name == old_name && is_graph_node_instance(owner, flow.to.node_instance)) {
                auto added = add_checked_source_rename_patch(
                    source, flow.to_pin_range, old_name, new_name, "graph-node target exec pin reference", records);
                if (added.is_err()) return added;
            }
        }
        return Result<void, std::string>::ok();
    };
    for (const auto& owner : module.graphs) {
        for (const auto& event : owner.events) {
            auto added = add_graph_node_exec_refs(owner, event);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
        for (const auto& function : owner.functions) {
            auto added = add_graph_node_exec_refs(owner, function);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
    }

    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::vector<SourcePatchRecord>, std::string> build_active_graph_function_rename_patches(
    const std::string& source,
    const Module& module,
    const Graph& graph,
    const std::string& old_name,
    const std::string& new_name) {
    if (!is_identifier_text(old_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Old function identifier is invalid");
    }
    if (!is_identifier_text(new_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("New function identifier is invalid");
    }
    if (old_name == new_name) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Source function rename must change the name");
    }

    const Function* target = nullptr;
    for (const auto& function : graph.functions) {
        if (function.name == new_name) {
            return Result<std::vector<SourcePatchRecord>, std::string>::err("Function '" + new_name + "' already exists");
        }
        if (function.name == old_name) target = &function;
    }
    if (!target) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Function '" + old_name + "' not found");
    }

    std::vector<SourcePatchRecord> records;
    auto added_decl = add_checked_source_rename_patch(
        source, target->name_range, old_name, new_name, "function declaration", records);
    if (added_decl.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added_decl.error());

    auto is_graph_node_instance = [&](const Graph& owner, const std::string& instance_name) {
        for (const auto& node : owner.node_instances) {
            if (node.instance_name == instance_name && node.type_name == graph.name) return true;
        }
        return false;
    };
    auto add_graph_node_exec_refs = [&](const Graph& owner, const LogicBlock& block) -> Result<void, std::string> {
        for (const auto& flow : block.flow_connections) {
            if (flow.from.pin_name == old_name && is_graph_node_instance(owner, flow.from.node_instance)) {
                auto added = add_checked_source_rename_patch(
                    source, flow.from_pin_range, old_name, new_name, "graph-node source function reference", records);
                if (added.is_err()) return added;
            }
            if (flow.to.pin_name == old_name && is_graph_node_instance(owner, flow.to.node_instance)) {
                auto added = add_checked_source_rename_patch(
                    source, flow.to_pin_range, old_name, new_name, "graph-node target function reference", records);
                if (added.is_err()) return added;
            }
        }
        return Result<void, std::string>::ok();
    };
    for (const auto& owner : module.graphs) {
        for (const auto& event : owner.events) {
            auto added = add_graph_node_exec_refs(owner, event);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
        for (const auto& function : owner.functions) {
            auto added = add_graph_node_exec_refs(owner, function);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
    }

    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::vector<SourcePatchRecord>, std::string> build_optional_graph_function_declaration_rename_patches(
    const std::string& source,
    const Module& module,
    const std::string& graph_name,
    const std::string& old_name,
    const std::string& new_name) {
    if (!is_identifier_text(graph_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Graph identifier is invalid");
    }
    if (!is_identifier_text(old_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Old function identifier is invalid");
    }
    if (!is_identifier_text(new_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("New function identifier is invalid");
    }
    if (old_name == new_name) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("File graph function rename must change the name");
    }

    const Graph* target_graph = nullptr;
    const Function* target_function = nullptr;
    bool has_new_function = false;
    for (const auto& graph : module.graphs) {
        if (graph.name != graph_name) continue;
        target_graph = &graph;
        for (const auto& function : graph.functions) {
            if (function.name == old_name) target_function = &function;
            if (function.name == new_name) has_new_function = true;
        }
        break;
    }
    if (target_function && has_new_function) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err(
            "Function '" + new_name + "' already exists on graph '" + graph_name + "'");
    }

    std::vector<SourcePatchRecord> records;
    if (target_graph && target_function) {
        auto added_decl = add_checked_source_rename_patch(
            source, target_function->name_range, old_name, new_name, "graph function declaration", records);
        if (added_decl.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added_decl.error());
    }

    auto is_graph_node_instance = [&](const Graph& owner, const std::string& instance_name) {
        for (const auto& node : owner.node_instances) {
            if (node.instance_name == instance_name && node.type_name == graph_name) return true;
        }
        return false;
    };
    auto add_graph_node_exec_refs = [&](const Graph& owner, const LogicBlock& block) -> Result<void, std::string> {
        for (const auto& flow : block.flow_connections) {
            if (flow.from.pin_name == old_name && is_graph_node_instance(owner, flow.from.node_instance)) {
                auto added = add_checked_source_rename_patch(
                    source, flow.from_pin_range, old_name, new_name, "graph-node source function reference", records);
                if (added.is_err()) return added;
            }
            if (flow.to.pin_name == old_name && is_graph_node_instance(owner, flow.to.node_instance)) {
                auto added = add_checked_source_rename_patch(
                    source, flow.to_pin_range, old_name, new_name, "graph-node target function reference", records);
                if (added.is_err()) return added;
            }
        }
        return Result<void, std::string>::ok();
    };
    for (const auto& owner : module.graphs) {
        for (const auto& event : owner.events) {
            auto added = add_graph_node_exec_refs(owner, event);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
        for (const auto& function : owner.functions) {
            auto added = add_graph_node_exec_refs(owner, function);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
    }

    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::vector<SourcePatchRecord>, std::string> build_active_graph_node_rename_patches(
    const std::string& source,
    const Graph& graph,
    const std::string& old_name,
    const std::string& new_name) {
    if (!is_identifier_text(old_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Old node identifier is invalid");
    }
    if (!is_identifier_text(new_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("New node identifier is invalid");
    }
    if (old_name == new_name) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Source node rename must change the name");
    }

    const NodeInstance* target = nullptr;
    for (const auto& node : graph.node_instances) {
        if (node.instance_name == new_name) {
            return Result<std::vector<SourcePatchRecord>, std::string>::err("Node '" + new_name + "' already exists");
        }
        if (node.instance_name == old_name) target = &node;
    }
    if (!target) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Node '" + old_name + "' not found");
    }

    std::vector<SourcePatchRecord> records;
    auto added_decl = add_checked_source_rename_patch(
        source, target->instance_name_range, old_name, new_name, "node declaration", records);
    if (added_decl.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added_decl.error());

    auto add_block_refs = [&](const LogicBlock& block) -> Result<void, std::string> {
        for (const auto& flow : block.flow_connections) {
            if (flow.from.node_instance == old_name) {
                auto added = add_checked_source_rename_patch(
                    source, flow.from_node_range, old_name, new_name, "source flow node reference", records);
                if (added.is_err()) return added;
            }
            if (flow.to.node_instance == old_name) {
                auto added = add_checked_source_rename_patch(
                    source, flow.to_node_range, old_name, new_name, "target flow node reference", records);
                if (added.is_err()) return added;
            }
        }
        for (const auto& link : block.data_links) {
            if (link.target.node_instance == old_name) {
                auto added = add_checked_source_rename_patch(
                    source, link.target_node_range, old_name, new_name, "target data node reference", records);
                if (added.is_err()) return added;
            }
            if (!link.source.pin_name.empty() && link.source.node_instance == old_name) {
                auto added = add_checked_source_rename_patch(
                    source, link.source_node_range, old_name, new_name, "source data node reference", records);
                if (added.is_err()) return added;
            }
        }
        return Result<void, std::string>::ok();
    };
    for (const auto& event : graph.events) {
        auto added = add_block_refs(event);
        if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
    }
    for (const auto& function : graph.functions) {
        auto added = add_block_refs(function);
        if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
    }

    if (graph.generate) {
        for (const auto& comment : graph.generate->comments) {
            if (comment.instance_name == old_name) {
                auto added = add_checked_source_rename_patch(
                    source, comment.instance_name_range, old_name, new_name, "generate comment node reference", records);
                if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
            }
        }
        for (const auto& metadata : graph.generate->metadata) {
            if (metadata.node == old_name) {
                auto added = add_checked_source_rename_patch(
                    source, metadata.node_range, old_name, new_name, "generate metadata node reference", records);
                if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
            }
        }
    }

    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::vector<SourcePatchRecord>, std::string> build_graph_rename_patches(
    const std::string& source,
    const Module& module,
    const std::string& old_name,
    const std::string& new_name) {
    if (!is_identifier_text(old_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Old graph identifier is invalid");
    }
    if (!is_identifier_text(new_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("New graph identifier is invalid");
    }
    if (old_name == new_name) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Source graph rename must change the name");
    }

    const Graph* target = nullptr;
    for (const auto& graph : module.graphs) {
        if (graph.name == new_name) {
            return Result<std::vector<SourcePatchRecord>, std::string>::err("Graph '" + new_name + "' already exists");
        }
        if (graph.name == old_name) target = &graph;
    }
    if (!target) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Graph '" + old_name + "' not found");
    }

    std::vector<SourcePatchRecord> records;
    auto added_decl = add_checked_source_rename_patch(
        source, target->name_range, old_name, new_name, "graph declaration", records);
    if (added_decl.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added_decl.error());

    for (const auto& graph : module.graphs) {
        for (const auto& node : graph.node_instances) {
            if (node.type_name != old_name) continue;
            auto added = add_checked_source_rename_patch(
                source, node.type_name_range, old_name, new_name, "graph-node type reference", records);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
    }

    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::vector<SourcePatchRecord>, std::string> build_optional_graph_reference_rename_patches(
    const std::string& source,
    const Module& module,
    const std::string& old_name,
    const std::string& new_name) {
    if (!is_identifier_text(old_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Old graph identifier is invalid");
    }
    if (!is_identifier_text(new_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("New graph identifier is invalid");
    }
    if (old_name == new_name) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("File graph rename must change the name");
    }

    const Graph* target = nullptr;
    for (const auto& graph : module.graphs) {
        if (graph.name == new_name) {
            return Result<std::vector<SourcePatchRecord>, std::string>::err("Graph '" + new_name + "' already exists");
        }
        if (graph.name == old_name) target = &graph;
    }

    std::vector<SourcePatchRecord> records;
    if (target) {
        auto added_decl = add_checked_source_rename_patch(
            source, target->name_range, old_name, new_name, "graph declaration", records);
        if (added_decl.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added_decl.error());
    }

    for (const auto& graph : module.graphs) {
        for (const auto& node : graph.node_instances) {
            if (node.type_name != old_name) continue;
            auto added = add_checked_source_rename_patch(
                source, node.type_name_range, old_name, new_name, "graph-node type reference", records);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
    }

    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::vector<SourcePatchRecord>, std::string> build_node_type_rename_patches(
    const std::string& source,
    const Module& module,
    const Environment& env,
    const std::string& old_name,
    const std::string& new_name) {
    if (!is_identifier_text(old_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Old node type identifier is invalid");
    }
    if (!is_identifier_text(new_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("New node type identifier is invalid");
    }
    if (old_name == new_name) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Source node type rename must change the name");
    }
    if (!env.nodes().find(old_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Node type '" + old_name + "' is not declared");
    }
    if (!env.nodes().find(new_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Node type '" + new_name + "' is not declared");
    }

    std::vector<SourcePatchRecord> records;
    for (const auto& graph : module.graphs) {
        for (const auto& node : graph.node_instances) {
            if (node.type_name != old_name) continue;
            auto added = add_checked_source_rename_patch(
                source, node.type_name_range, old_name, new_name, "node type reference", records);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
    }

    if (records.empty()) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Node type '" + old_name + "' has no source references");
    }
    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::vector<SourcePatchRecord>, std::string> build_optional_node_type_reference_rename_patches(
    const std::string& source,
    const Module& module,
    const std::string& old_name,
    const std::string& new_name) {
    std::vector<SourcePatchRecord> records;
    for (const auto& graph : module.graphs) {
        for (const auto& node : graph.node_instances) {
            if (node.type_name != old_name) continue;
            auto added = add_checked_source_rename_patch(
                source, node.type_name_range, old_name, new_name, "node type reference", records);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
    }

    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::vector<SourcePatchRecord>, std::string> build_declaration_node_rename_patches(
    const std::string& source,
    const std::string& old_name,
    const std::string& new_name) {
    if (!is_identifier_text(old_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Old declaration node identifier is invalid");
    }
    if (!is_identifier_text(new_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("New declaration node identifier is invalid");
    }
    if (old_name == new_name) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Declaration node rename must change the name");
    }

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto parsed = parser.parse();
    if (parsed.is_err()) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Parse error in declaration source");
    }

    const DeclareNodeNode* target = nullptr;
    for (const auto& node : parsed.value()->declare_nodes) {
        if (node->name == new_name) {
            return Result<std::vector<SourcePatchRecord>, std::string>::err("Declaration node '" + new_name + "' already exists");
        }
        if (node->name == old_name) target = node.get();
    }
    if (!target) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Declaration node '" + old_name + "' not found");
    }

    std::vector<SourcePatchRecord> records;
    auto added = add_checked_source_rename_patch(
        source, target->name_range, old_name, new_name, "declaration node definition", records);
    if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::vector<SourcePatchRecord>, std::string> build_declaration_schema_rename_patches(
    const std::string& source,
    const std::string& old_name,
    const std::string& new_name) {
    if (!is_identifier_text(old_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Old declaration schema identifier is invalid");
    }
    if (!is_identifier_text(new_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("New declaration schema identifier is invalid");
    }
    if (old_name == new_name) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Declaration schema rename must change the name");
    }

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto parsed = parser.parse();
    if (parsed.is_err()) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Parse error in declaration source");
    }

    const DeclareSchemaNode* target = nullptr;
    for (const auto& schema : parsed.value()->declare_schemas) {
        if (schema->name == new_name) {
            return Result<std::vector<SourcePatchRecord>, std::string>::err("Declaration schema '" + new_name + "' already exists");
        }
        if (schema->name == old_name) target = schema.get();
    }
    if (!target) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Declaration schema '" + old_name + "' not found");
    }

    std::vector<SourcePatchRecord> records;
    auto added = add_checked_source_rename_patch(
        source, target->name_range, old_name, new_name, "declaration schema definition", records);
    if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::vector<SourcePatchRecord>, std::string> build_declaration_schema_field_rename_patches(
    const std::string& source,
    const std::string& schema_name,
    const std::string& old_name,
    const std::string& new_name) {
    if (!is_identifier_text(schema_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Declaration schema identifier is invalid");
    }
    if (!is_identifier_text(old_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Old declaration schema field identifier is invalid");
    }
    if (!is_identifier_text(new_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("New declaration schema field identifier is invalid");
    }
    if (old_name == new_name) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Declaration schema field rename must change the name");
    }

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto parsed = parser.parse();
    if (parsed.is_err()) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Parse error in declaration source");
    }

    const DeclareSchemaNode* target_schema = nullptr;
    for (const auto& schema : parsed.value()->declare_schemas) {
        if (schema->name == schema_name) {
            target_schema = schema.get();
            break;
        }
    }
    if (!target_schema) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Declaration schema '" + schema_name + "' not found");
    }

    const SchemaFieldNode* target_field = nullptr;
    for (const auto& field : target_schema->fields) {
        if (field->name == new_name) {
            return Result<std::vector<SourcePatchRecord>, std::string>::err(
                "Declaration schema field '" + new_name + "' already exists on schema '" + schema_name + "'");
        }
        if (field->name == old_name) target_field = field.get();
    }
    if (!target_field) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err(
            "Declaration schema field '" + old_name + "' not found on schema '" + schema_name + "'");
    }

    std::vector<SourcePatchRecord> records;
    auto added = add_checked_source_rename_patch(
        source, target_field->name_range, old_name, new_name, "declaration schema field definition", records);
    if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::vector<SourcePatchRecord>, std::string> build_declaration_type_rename_patches(
    const std::string& source,
    const std::string& old_name,
    const std::string& new_name) {
    if (!is_identifier_text(old_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Old declaration type identifier is invalid");
    }
    if (!is_identifier_text(new_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("New declaration type identifier is invalid");
    }
    if (old_name == new_name) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Declaration type rename must change the name");
    }

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto parsed = parser.parse();
    if (parsed.is_err()) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Parse error in declaration source");
    }

    const DeclareTypeNode* target = nullptr;
    for (const auto& type : parsed.value()->declare_types) {
        if (type->name == new_name) {
            return Result<std::vector<SourcePatchRecord>, std::string>::err("Declaration type '" + new_name + "' already exists");
        }
        if (type->name == old_name) target = type.get();
    }
    if (!target) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Declaration type '" + old_name + "' not found");
    }

    std::vector<SourcePatchRecord> records;
    auto added = add_checked_source_rename_patch(
        source, target->name_range, old_name, new_name, "declaration type definition", records);
    if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());

    for (const auto& type : parsed.value()->declare_types) {
        auto annot_added = add_annotation_constructor_type_rename_patches(
            source, type->annotations, old_name, new_name, "declaration type", records);
        if (annot_added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(annot_added.error());
    }
    for (const auto& node : parsed.value()->declare_nodes) {
        auto node_annot_added = add_annotation_constructor_type_rename_patches(
            source, node->annotations, old_name, new_name, "declaration node", records);
        if (node_annot_added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(node_annot_added.error());
        for (const auto& pin : node->pins) {
            auto pin_annot_added = add_annotation_constructor_type_rename_patches(
                source, pin->annotations, old_name, new_name, "declaration pin", records);
            if (pin_annot_added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(pin_annot_added.error());
            if (pin->type_name != old_name) continue;
            auto pin_added = add_checked_source_rename_patch(
                source, pin->type_name_range, old_name, new_name, "declaration pin type reference", records);
            if (pin_added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(pin_added.error());
        }
    }
    for (const auto& schema : parsed.value()->declare_schemas) {
        auto schema_annot_added = add_annotation_constructor_type_rename_patches(
            source, schema->annotations, old_name, new_name, "declaration schema", records);
        if (schema_annot_added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(schema_annot_added.error());
        for (const auto& field : schema->fields) {
            auto field_annot_added = add_annotation_constructor_type_rename_patches(
                source, field->annotations, old_name, new_name, "declaration schema field", records);
            if (field_annot_added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(field_annot_added.error());
            auto field_value_added = add_optional_type_reference_patch(
                source,
                field->value_constructor_type_range,
                old_name,
                new_name,
                "declaration schema field constructor type reference",
                records);
            if (field_value_added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(field_value_added.error());
        }
    }

    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::vector<SourcePatchRecord>, std::string> build_declaration_node_pin_rename_patches(
    const std::string& source,
    const std::string& node_type,
    const std::string& old_name,
    const std::string& new_name) {
    if (!is_identifier_text(node_type)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Declaration node identifier is invalid");
    }
    if (!is_identifier_text(old_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Old declaration pin identifier is invalid");
    }
    if (!is_identifier_text(new_name)) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("New declaration pin identifier is invalid");
    }
    if (old_name == new_name) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Declaration pin rename must change the name");
    }

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto parsed = parser.parse();
    if (parsed.is_err()) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Parse error in declaration source");
    }

    const DeclareNodeNode* target_node = nullptr;
    for (const auto& node : parsed.value()->declare_nodes) {
        if (node->name == node_type) {
            target_node = node.get();
            break;
        }
    }
    if (!target_node) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err("Declaration node '" + node_type + "' not found");
    }

    const PinDeclNode* target_pin = nullptr;
    for (const auto& pin : target_node->pins) {
        if (pin->name == new_name) {
            return Result<std::vector<SourcePatchRecord>, std::string>::err(
                "Declaration pin '" + new_name + "' already exists on node '" + node_type + "'");
        }
        if (pin->name == old_name) target_pin = pin.get();
    }
    if (!target_pin) {
        return Result<std::vector<SourcePatchRecord>, std::string>::err(
            "Declaration pin '" + old_name + "' not found on node '" + node_type + "'");
    }

    std::vector<SourcePatchRecord> records;
    auto added = add_checked_source_rename_patch(
        source, target_pin->name_range, old_name, new_name, "declaration pin definition", records);
    if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::vector<SourcePatchRecord>, std::string> build_optional_node_pin_reference_rename_patches(
    const std::string& source,
    const Module& module,
    const std::string& node_type,
    const std::string& old_name,
    const std::string& new_name) {
    auto instance_has_type = [&](const Graph& graph, const std::string& instance_name) {
        for (const auto& node : graph.node_instances) {
            if (node.instance_name == instance_name && node.type_name == node_type) return true;
        }
        return false;
    };

    std::vector<SourcePatchRecord> records;
    for (const auto& graph : module.graphs) {
        auto add_block_refs = [&](const LogicBlock& block) -> Result<void, std::string> {
            for (const auto& flow : block.flow_connections) {
                if (flow.from.pin_name == old_name && instance_has_type(graph, flow.from.node_instance)) {
                    auto added = add_checked_source_rename_patch(
                        source, flow.from_pin_range, old_name, new_name, "source flow pin reference", records);
                    if (added.is_err()) return added;
                }
                if (flow.to.pin_name == old_name && instance_has_type(graph, flow.to.node_instance)) {
                    auto added = add_checked_source_rename_patch(
                        source, flow.to_pin_range, old_name, new_name, "target flow pin reference", records);
                    if (added.is_err()) return added;
                }
            }
            for (const auto& link : block.data_links) {
                if (link.target.pin_name == old_name && instance_has_type(graph, link.target.node_instance)) {
                    auto added = add_checked_source_rename_patch(
                        source, link.target_pin_range, old_name, new_name, "target data pin reference", records);
                    if (added.is_err()) return added;
                }
                if (link.source.pin_name == old_name && instance_has_type(graph, link.source.node_instance)) {
                    auto added = add_checked_source_rename_patch(
                        source, link.source_pin_range, old_name, new_name, "source data pin reference", records);
                    if (added.is_err()) return added;
                }
            }
            return Result<void, std::string>::ok();
        };

        for (const auto& event : graph.events) {
            auto added = add_block_refs(event);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
        for (const auto& function : graph.functions) {
            auto added = add_block_refs(function);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
    }

    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::vector<SourcePatchRecord>, std::string> build_optional_schema_reference_rename_patches(
    const std::string& source,
    const Module& module,
    const std::string& old_name,
    const std::string& new_name) {
    std::vector<SourcePatchRecord> records;
    for (const auto& graph : module.graphs) {
        if (!graph.base_type || *graph.base_type != old_name) continue;
        auto added = add_checked_source_rename_patch(
            source, graph.base_type_range, old_name, new_name, "graph base schema reference", records);
        if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
    }

    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static Result<std::vector<SourcePatchRecord>, std::string> build_optional_type_reference_rename_patches(
    const std::string& source,
    const Module& module,
    const std::string& old_name,
    const std::string& new_name) {
    std::vector<SourcePatchRecord> records;
    auto add_optional_type_reference = [&](const SourceRange& range,
                                           const std::string& label) -> Result<void, std::string> {
        return add_optional_type_reference_patch(source, range, old_name, new_name, label, records);
    };
    auto add_annotations = [&](const std::vector<Annotation>& annotations,
                               const std::string& label) -> Result<void, std::string> {
        return add_annotation_constructor_type_rename_patches(
            source, annotations, old_name, new_name, label, records);
    };
    auto add_logic_annotations = [&](const LogicBlock& block,
                                     const std::string& label) -> Result<void, std::string> {
        auto block_added = add_annotations(block.annotations, label);
        if (block_added.is_err()) return block_added;
        for (const auto& flow : block.flow_connections) {
            auto flow_added = add_annotations(flow.annotations, label + " flow");
            if (flow_added.is_err()) return flow_added;
        }
        for (const auto& link : block.data_links) {
            auto link_added = add_annotations(link.annotations, label + " link");
            if (link_added.is_err()) return link_added;
        }
        return Result<void, std::string>::ok();
    };

    for (const auto& import : module.imports) {
        auto added = add_annotations(import.annotations, "import");
        if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
    }
    for (const auto& let : module.top_level_lets) {
        auto annot_added = add_annotations(let.annotations, "let");
        if (annot_added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(annot_added.error());
        if (let.type_name != old_name) continue;
        auto added = add_checked_source_rename_patch(
            source, let.type_name_range, old_name, new_name, "let type reference", records);
        if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
    }
    for (const auto& graph : module.graphs) {
        auto graph_annot_added = add_annotations(graph.annotations, "graph");
        if (graph_annot_added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(graph_annot_added.error());
        for (const auto& param : graph.parameters) {
            auto annot_added = add_annotations(param.annotations, "graph parameter");
            if (annot_added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(annot_added.error());
            if (param.type_name != old_name) continue;
            auto added = add_checked_source_rename_patch(
                source, param.type_name_range, old_name, new_name, "graph parameter type reference", records);
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
        for (const auto& param : graph.parameters) {
            auto added = add_optional_type_reference(
                param.default_constructor_type_range,
                "graph parameter default constructor type reference");
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
        for (const auto& node : graph.node_instances) {
            auto annot_added = add_annotations(node.annotations, "node instance");
            if (annot_added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(annot_added.error());
            auto raw_added = add_optional_type_reference(
                node.initializer_constructor_type_range,
                "node initializer constructor type reference");
            if (raw_added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(raw_added.error());

            for (const auto& field : node.initializer_fields) {
                auto field_added = add_optional_type_reference(
                    field.value_constructor_type_range,
                    "node initializer field constructor type reference");
                if (field_added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(field_added.error());
            }
        }
        for (const auto& event : graph.events) {
            auto added = add_logic_annotations(event, "event");
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
        for (const auto& function : graph.functions) {
            auto added = add_logic_annotations(function, "function");
            if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
        }
        if (graph.generate) {
            for (const auto& comment : graph.generate->comments) {
                auto added = add_annotations(comment.annotations, "generate comment");
                if (added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(added.error());
            }
            for (const auto& metadata : graph.generate->metadata) {
                auto annot_added = add_annotations(metadata.annotations, "generate metadata");
                if (annot_added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(annot_added.error());
                auto value_added = add_optional_type_reference(
                    metadata.value_constructor_type_range,
                    "generate metadata constructor type reference");
                if (value_added.is_err()) return Result<std::vector<SourcePatchRecord>, std::string>::err(value_added.error());
            }
        }
    }

    return Result<std::vector<SourcePatchRecord>, std::string>::ok(records);
}

static void unregister_declared_native_nodes(Environment& env, const ModuleNode& declaration_ast) {
    for (const auto& node : declaration_ast.declare_nodes) {
        env.nodes().unregister_node(node->name);
    }
}

static Result<Module, std::string> compile_source_for_ranges(
    const std::string& source,
    Environment& env,
    const std::string& source_name) {
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto ast = parser.parse();
    if (ast.is_err()) {
        return Result<Module, std::string>::err("Parse error in emitted source");
    }

    Compiler compiler(env);
    auto compiled = compiler.compile(*ast.value(), source_name);
    if (compiled.is_err()) {
        return Result<Module, std::string>::err("Compile error in emitted source: " + compiled.error());
    }
    return compiled;
}

static const Graph* find_graph_by_name(const Module& module, const std::string& name) {
    for (const auto& graph : module.graphs) {
        if (graph.name == name) return &graph;
    }
    return nullptr;
}

// ─── REPL ──────────────────────────────────────────────────────────

int CLIEditor::run() {
    std::cout << "GraphScript Editor v0.1.0  (type 'help' for commands)\n";
    std::string line;
    while (running_) {
        std::cout << prompt();
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        execute(line);
    }
    return 0;
}

bool CLIEditor::execute(const std::string& line) {
    last_command_ok_ = true;
    auto args = tokenize(line);
    if (args.empty()) return true;

    auto& cmd = args[0];

    if (cmd == "help" || cmd == "?")       cmd_help();
    else if (cmd == "status")              cmd_status();
    else if (cmd == "new" || cmd == "create_graph") cmd_new(args);
    else if (cmd == "open")                cmd_open(args);
    else if (cmd == "rename_graph")        cmd_rename_graph(args);
    else if (cmd == "switch_graph")        cmd_switch_graph(args);
    else if (cmd == "delete" || cmd == "delete_graph") cmd_delete_graph(args);
    else if (cmd == "graphs")              cmd_graphs();
    else if (cmd == "import")              cmd_import(args);
    else if (cmd == "let")                 cmd_let(args);
    else if (cmd == "param")               cmd_param(args);
    else if (cmd == "rename_param")        cmd_rename_param(args);
    else if (cmd == "set_param_type")      cmd_set_param_type(args);
    else if (cmd == "set_param_default")   cmd_set_param_default(args);
    else if (cmd == "set_param_default_ctor") cmd_set_param_default_ctor(args);
    else if (cmd == "set_param_default_ctor_arg") cmd_set_param_default_ctor_arg(args);
    else if (cmd == "set_param_default_ctor_type") cmd_set_param_default_ctor_type(args);
    else if (cmd == "params")              cmd_params();
    else if (cmd == "add" || cmd == "add_node") cmd_add(args);
    else if (cmd == "set_init_expr")       cmd_set_init_expr(args);
    else if (cmd == "set_init")            cmd_set_init(args);
    else if (cmd == "set_init_ctor")       cmd_set_init_ctor(args);
    else if (cmd == "set_init_ctor_arg")   cmd_set_init_ctor_arg(args);
    else if (cmd == "set_init_ctor_type")  cmd_set_init_ctor_type(args);
    else if (cmd == "unset_init")          cmd_unset_init(args);
    else if (cmd == "rename_init")         cmd_rename_init(args);
    else if (cmd == "rm" || cmd == "remove_node") cmd_rm(args);
    else if (cmd == "rename_node")       cmd_rename_node(args);
    else if (cmd == "nodes")               cmd_nodes();
    else if (cmd == "info")                cmd_info(args);
    else if (cmd == "pins")                cmd_pins(args);
    else if (cmd == "event")               cmd_event(args);
    else if (cmd == "fn")                  cmd_fn(args);
    else if (cmd == "rename_event")        cmd_rename_event(args);
    else if (cmd == "rename_function")     cmd_rename_function(args);
    else if (cmd == "delete_event")        cmd_delete_event(args);
    else if (cmd == "delete_function")     cmd_delete_function(args);
    else if (cmd == "flow")                cmd_flow(args);
    else if (cmd == "link")                cmd_link(args);
    else if (cmd == "unflow")              cmd_unflow(args);
    else if (cmd == "unlink")              cmd_unlink(args);
    else if (cmd == "done")                cmd_done();
    else if (cmd == "connections" || cmd == "conns") cmd_connections();
    else if (cmd == "annotate")            cmd_annotate(args);
    else if (cmd == "unannotate")          cmd_unannotate(args);
    else if (cmd == "comment")             cmd_comment(args);
    else if (cmd == "meta")                cmd_meta(args);
    else if (cmd == "remove_comment")      cmd_remove_comment(args);
    else if (cmd == "remove_meta")         cmd_remove_meta(args);
    else if (cmd == "move_comment")        cmd_move_comment(args);
    else if (cmd == "move_meta")           cmd_move_meta(args);
    else if (cmd == "rename_comment")      cmd_rename_comment(args);
    else if (cmd == "rename_meta")         cmd_rename_meta(args);
    else if (cmd == "rename_meta_ref")     cmd_rename_meta_ref(args);
    else if (cmd == "undo")                cmd_undo();
    else if (cmd == "redo")                cmd_redo();
    else if (cmd == "history")             cmd_history();
    else if (cmd == "emit")                cmd_emit();
    else if (cmd == "diagram")             cmd_diagram();
    else if (cmd == "validate")            cmd_validate();
    else if (cmd == "bake")                cmd_bake();
    else if (cmd == "types")               cmd_types();
    else if (cmd == "schemas")             cmd_schemas();
    else if (cmd == "load")                cmd_load(args);
    else if (cmd == "save")                cmd_save(args);
    else if (cmd == "apply_source_b64")    cmd_apply_source_b64(args);
    else if (cmd == "apply_source_patch")  cmd_apply_source_patch(args);
    else if (cmd == "apply_source_patches_b64") cmd_apply_source_patches_b64(args);
    else if (cmd == "apply_source_identifier_rename") cmd_apply_source_identifier_rename(args);
    else if (cmd == "apply_source_param_rename") cmd_apply_source_param_rename(args);
    else if (cmd == "apply_source_event_rename") cmd_apply_source_event_rename(args);
    else if (cmd == "apply_source_function_rename") cmd_apply_source_function_rename(args);
    else if (cmd == "apply_source_node_rename") cmd_apply_source_node_rename(args);
    else if (cmd == "apply_source_graph_rename") cmd_apply_source_graph_rename(args);
    else if (cmd == "apply_source_node_type_rename") cmd_apply_source_node_type_rename(args);
    else if (cmd == "apply_source_node_pin_rename") cmd_apply_source_node_pin_rename(args);
    else if (cmd == "apply_source_schema_rename") cmd_apply_source_schema_rename(args);
    else if (cmd == "apply_source_type_rename") cmd_apply_source_type_rename(args);
    else if (cmd == "apply_import_node_rename") cmd_apply_import_node_rename(args);
    else if (cmd == "apply_import_node_pin_rename") cmd_apply_import_node_pin_rename(args);
    else if (cmd == "apply_import_schema_rename") cmd_apply_import_schema_rename(args);
    else if (cmd == "apply_import_schema_field_rename") cmd_apply_import_schema_field_rename(args);
    else if (cmd == "apply_import_type_rename") cmd_apply_import_type_rename(args);
    else if (cmd == "apply_files_graph_rename") cmd_apply_files_graph_rename(args);
    else if (cmd == "apply_files_graph_param_rename") cmd_apply_files_graph_param_rename(args);
    else if (cmd == "apply_files_graph_event_rename") cmd_apply_files_graph_event_rename(args);
    else if (cmd == "apply_files_graph_function_rename") cmd_apply_files_graph_function_rename(args);
    else if (cmd == "apply_files_node_type_rename") cmd_apply_files_node_type_rename(args);
    else if (cmd == "apply_files_node_pin_rename") cmd_apply_files_node_pin_rename(args);
    else if (cmd == "apply_files_schema_rename") cmd_apply_files_schema_rename(args);
    else if (cmd == "apply_files_type_rename") cmd_apply_files_type_rename(args);
    else if (cmd == "quit" || cmd == "exit") { running_ = false; }
    else print_error("Unknown command: " + cmd + " (type 'help')");

    return running_;
}

std::string CLIEditor::prompt() const {
    std::string p = "gs";
    auto* g = session_.active_graph();
    if (g) {
        p += "/" + g->name;
        if (!current_block_.empty()) p += "/" + current_block_;
    }
    p += "> ";
    return p;
}

std::vector<std::string> CLIEditor::tokenize(const std::string& line) const {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quote = false;

    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (c == '"') {
            in_quote = !in_quote;
        } else if (c == ' ' && !in_quote) {
            if (!current.empty()) { tokens.push_back(current); current.clear(); }
        } else {
            current += c;
        }
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

std::pair<std::string, std::string> CLIEditor::parse_pin_ref(const std::string& ref) const {
    auto dot = ref.find('.');
    if (dot == std::string::npos) return {ref, ""};
    return {ref.substr(0, dot), ref.substr(dot + 1)};
}

static bool parse_occurrence_arg(const std::string& token, size_t& occurrence) {
    if (token.size() < 2 || token[0] != '#') return false;
    size_t value = 0;
    auto begin = token.data() + 1;
    auto end = token.data() + token.size();
    auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc() || result.ptr != end || value == 0) return false;
    occurrence = value;
    return true;
}

static bool parse_generate_meta_ref(const std::string& ref, std::string& scope, std::string& node, std::string& prop) {
    auto colon = ref.find(':');
    if (colon == std::string::npos) return false;
    std::string rest = ref.substr(colon + 1);
    auto dot = rest.find('.');
    if (dot == std::string::npos) return false;
    scope = ref.substr(0, colon);
    node = rest.substr(0, dot);
    prop = rest.substr(dot + 1);
    return !scope.empty() && !node.empty() && !prop.empty();
}

static std::optional<bool> parse_move_direction(const std::string& token) {
    if (token == "up") return true;
    if (token == "down") return false;
    return std::nullopt;
}

Annotation CLIEditor::parse_annotation_args(const std::vector<std::string>& args, size_t start) const {
    Annotation annot;
    if (start >= args.size()) return annot;
    annot.name = args[start];

    for (size_t i = start + 1; i < args.size(); ++i) {
        const auto& token = args[i];
        auto eq = token.find('=');
        if (eq != std::string::npos) {
            annot.args.push_back({token.substr(0, eq), token.substr(eq + 1)});
        } else if (i + 2 < args.size() && args[i + 1] == "=") {
            annot.args.push_back({token, args[i + 2]});
            i += 2;
        } else {
            annot.args.push_back({"", token});
        }
    }
    return annot;
}

void CLIEditor::print_error(const std::string& msg) {
    last_command_ok_ = false;
    std::cout << "  [ERROR] " << msg << "\n";
}
void CLIEditor::print_ok(const std::string& msg) { std::cout << "  " << msg << "\n"; }

// ─── Commands ──────────────────────────────────────────────────────

void CLIEditor::cmd_help() {
    std::cout <<
        "\n=== Graph Management ===\n"
        "  new <name> [: schema]    Create new graph (alias: create_graph)\n"
        "  open <name>              Switch active graph by name\n"
        "  rename_graph <old> <new> Rename graph and migrate graph-node references\n"
        "  switch_graph <index|name> Switch active graph by index or name\n"
        "  delete <name>            Delete a graph (alias: delete_graph)\n"
        "  graphs                   List all graphs\n"
        "  import <file.d.gs>       Load declarations\n"
        "  let <name> <Type> [arg]  Add let declaration\n"
        "\n=== Parameters ===\n"
        "  param <in|out|var> <name> <type> [= default]\n"
        "  param rm <name>          Remove parameter\n"
        "  rename_param <old> <new> Rename parameter and migrate bare references\n"
        "  set_param_type <name> <Type> Set parameter type\n"
        "  set_param_default <name> [value] Set or clear parameter default value\n"
        "  set_param_default_ctor <name> <Type> [arg] Set parameter default to Type(arg)\n"
        "  set_param_default_ctor_arg <name> [arg] Set parameter default constructor argument\n"
        "  set_param_default_ctor_type <name> <Type> Set parameter default constructor type\n"
        "  params                   List parameters\n"
        "\n=== Nodes ===\n"
        "  add <Type> <name>        Add node instance (alias: add_node)\n"
        "  set_init_expr <node> [expr] Set or clear raw initializer expression\n"
        "  set_init <node> <field> <value> Set or append initializer field\n"
        "  set_init_ctor <node> <field> <Type> [arg] Set initializer field to Type(arg)\n"
        "  set_init_ctor_arg <node> <field> [arg] Set initializer constructor argument\n"
        "  set_init_ctor_type <node> <field> <Type> Set initializer constructor type\n"
        "  unset_init <node> <field> Remove initializer field\n"
        "  rename_init <node> <old_field> <new_field> Rename initializer field\n"
        "  rm <name>                Remove node instance (alias: remove_node)\n"
        "  rename_node <old> <new>  Rename node instance and migrate references\n"
        "  nodes                    List node instances\n"
        "  info <name>              Show node details\n"
        "  pins <name>              Show node pins\n"
        "  types                    List available node types\n"
        "\n=== Events / Functions ===\n"
        "  event <name>             Create/enter event block\n"
        "  event rm <name>          Remove event block (alias: delete_event)\n"
        "  rename_event <old> <new> Rename event block\n"
        "  fn <name>                Create/enter function block\n"
        "  fn rm <name>             Remove function block (alias: delete_function)\n"
        "  rename_function <old> <new> Rename function block\n"
        "  done                     Exit event/function context\n"
        "\n=== Connections (inside event/fn) ===\n"
        "  flow <from.pin> <to.pin> Add flow connection\n"
        "  link <target.pin> <src>  Add data link (src = node.pin or param)\n"
        "  unflow <from.pin> <to.pin>\n"
        "  unlink <target.pin>      Remove data link\n"
        "  connections              List all connections\n"
        "\n=== Annotations ===\n"
        "  annotate graph <Name> [args...]           Set graph annotation\n"
        "  annotate import <path> <Name> [args...]   Set import annotation\n"
        "  annotate let <name> <Name> [args...]      Set top-level let annotation\n"
        "  annotate node <inst> <Name> [args...]     Set node annotation\n"
        "  annotate param <name> <Name> [args...]    Set parameter annotation\n"
        "  annotate generate-comment <inst> <text> [#N] <Name> [args...]\n"
        "  annotate generate-meta <scope:node.prop> <val> [#N] <Name> [args...]\n"
        "  unannotate graph <Name>                   Remove graph annotation\n"
        "  unannotate import <path> <Name>           Remove import annotation\n"
        "  unannotate let <name> <Name>              Remove top-level let annotation\n"
        "  unannotate node <inst> <Name>             Remove node annotation\n"
        "  unannotate param <name> <Name>            Remove parameter annotation\n"
        "  unannotate generate-comment <inst> <text> [#N] <Name>\n"
        "  unannotate generate-meta <scope:node.prop> <val> [#N] <Name>\n"
        "\n=== Generate ===\n"
        "  comment <name> <text>    Add generate comment\n"
        "  comment rm <name> <text> [#N]  Remove generate comment\n"
        "  comment move <name> <text> [#N] <up|down>\n"
        "  comment rename <name> <old_text> [#N] <new_text>\n"
        "  remove_comment <name> <text> [#N]\n"
        "  move_comment <name> <text> [#N] <up|down>\n"
        "  rename_comment <name> <old_text> [#N] <new_text>\n"
        "  meta <scope:node.prop> <val>  Add metadata\n"
        "  meta rm <scope:node.prop> <val> [#N]  Remove metadata\n"
        "  meta move <scope:node.prop> <val> [#N] <up|down>\n"
        "  meta rename <scope:node.prop> <old_val> [#N] <new_val>\n"
        "  meta rename_ref <scope:node.prop> <val> [#N] <new_scope:node.prop>\n"
        "  remove_meta <scope:node.prop> <val> [#N]\n"
        "  move_meta <scope:node.prop> <val> [#N] <up|down>\n"
        "  rename_meta <scope:node.prop> <old_val> [#N] <new_val>\n"
        "  rename_meta_ref <scope:node.prop> <val> [#N] <new_scope:node.prop>\n"
        "\n=== Tools ===\n"
        "  emit                     Print .gs text\n"
        "  diagram                  Print Mermaid flowchart\n"
        "  validate                 Run validation\n"
        "  bake                     Show RuntimeGraph stats\n"
        "  schemas                  List schemas\n"
        "  status                   Show session summary\n"
        "\n=== Undo/Redo ===\n"
        "  undo / redo              Undo/redo last operation\n"
        "  history                  Show undo/redo stacks\n"
        "\n=== File ===\n"
        "  load <file.gs>           Load .gs file\n"
        "  save [file.gs]           Save to file\n"
        "  apply_source_b64 <text> [--env-hash hash --resolve-imports --base-dir dir]\n"
        "  apply_source_patch <sl> <sc> <el> <ec> [b64|-] [hash] [--env-hash hash --resolve-imports --base-dir dir]\n"
        "  apply_source_patches_b64 <patches-b64> [hash] [--env-hash hash --resolve-imports --base-dir dir]\n"
        "  apply_source_identifier_rename <old> <new> [hash] [--env-hash hash --resolve-imports --base-dir dir]\n"
        "  apply_source_param_rename <old> <new> [hash] [--env-hash hash --resolve-imports --base-dir dir]\n"
        "  apply_source_event_rename <old> <new> [hash] [--env-hash hash --resolve-imports --base-dir dir]\n"
        "  apply_source_function_rename <old> <new> [hash] [--env-hash hash --resolve-imports --base-dir dir]\n"
        "  apply_source_node_rename <old> <new> [hash] [--env-hash hash --resolve-imports --base-dir dir]\n"
        "  apply_source_graph_rename <old> <new> [hash] [--env-hash hash --resolve-imports --base-dir dir]\n"
        "  apply_source_node_type_rename <old> <new> [hash] [--env-hash hash --resolve-imports --base-dir dir]\n"
        "  apply_source_node_pin_rename <node-type> <old-pin> <new-pin> [hash] [--env-hash hash --resolve-imports --base-dir dir]\n"
        "  apply_source_schema_rename <old> <new> [hash] [--env-hash hash --resolve-imports --base-dir dir]\n"
        "  apply_source_type_rename <old> <new> [hash] [--env-hash hash --resolve-imports --base-dir dir]\n"
        "  apply_import_node_rename <file.d.gs> <old> <new> [declaration-source-hash]\n"
        "  apply_import_node_pin_rename <file.d.gs> <node-type> <old-pin> <new-pin> [declaration-source-hash]\n"
        "  apply_import_schema_rename <file.d.gs> <old> <new> [declaration-source-hash]\n"
        "  apply_import_schema_field_rename <file.d.gs> <schema> <old-field> <new-field> [declaration-source-hash]\n"
        "  apply_import_type_rename <file.d.gs> <old> <new> [declaration-source-hash]\n"
        "  apply_files_graph_rename <old> <new> <file.gs>... [--hash <file.gs> <hash>]...\n"
        "  apply_files_graph_param_rename <graph> <old> <new> <file.gs>... [--hash <file.gs> <hash>]...\n"
        "  apply_files_graph_event_rename <graph> <old> <new> <file.gs>... [--hash <file.gs> <hash>]...\n"
        "  apply_files_graph_function_rename <graph> <old> <new> <file.gs>... [--hash <file.gs> <hash>]...\n"
        "  apply_files_node_type_rename <old> <new> <file.gs>... [--hash <file.gs> <hash>]...\n"
        "  apply_files_node_pin_rename <node-type> <old-pin> <new-pin> <file.gs>... [--hash <file.gs> <hash>]...\n"
        "  apply_files_schema_rename <old> <new> <file.gs>... [--hash <file.gs> <hash>]...\n"
        "  apply_files_type_rename <old> <new> <file.gs>... [--hash <file.gs> <hash>]...\n"
        "  quit / exit              Exit editor\n\n";
}

void CLIEditor::cmd_status() {
    auto& mod = session_.module();
    std::cout << "  File: " << (session_.file_path().empty() ? "(unsaved)" : session_.file_path())
              << (session_.dirty() ? " [modified]" : "") << "\n";
    std::cout << "  Imports: " << mod.imports.size() << "\n";
    std::cout << "  Lets: " << mod.top_level_lets.size() << "\n";
    std::cout << "  Graphs: " << mod.graphs.size() << "\n";
    for (size_t i = 0; i < mod.graphs.size(); i++) {
        auto& g = mod.graphs[i];
        std::cout << "    " << (static_cast<int>(i) == session_.active_index() ? "* " : "  ")
                  << g.name;
        if (g.base_type) std::cout << " : " << *g.base_type;
        std::cout << " (" << g.parameters.size() << "P "
                  << g.node_instances.size() << "N "
                  << g.events.size() << "E "
                  << g.functions.size() << "F)\n";
    }
    std::cout << "  Types: " << session_.env().types().all().size()
              << "  Nodes: " << session_.env().nodes().all().size()
              << "  Schemas: " << session_.env().schemas().all().size() << "\n";
    if (!current_block_.empty()) std::cout << "  Editing block: " << current_block_ << "\n";
}

void CLIEditor::cmd_new(const std::vector<std::string>& args) {
    if (args.size() < 2) { print_error("Usage: new <name> [: schema]"); return; }
    std::string base;
    if (args.size() >= 4 && args[2] == ":") base = args[3];
    auto r = session_.new_graph(args[1], base);
    if (r.is_err()) print_error(r.error());
    else { print_ok("Created graph '" + args[1] + "'"); current_block_.clear(); }
}

void CLIEditor::cmd_open(const std::vector<std::string>& args) {
    if (args.size() < 2) { print_error("Usage: open <name>"); return; }
    auto r = session_.set_active(args[1]);
    if (r.is_err()) print_error(r.error());
    else { print_ok("Switched to '" + args[1] + "'"); current_block_.clear(); }
}

void CLIEditor::cmd_rename_graph(const std::vector<std::string>& args) {
    if (args.size() < 3) { print_error("Usage: rename_graph <old_name> <new_name>"); return; }
    auto r = session_.rename_graph(args[1], args[2]);
    if (r.is_err()) print_error(r.error());
    else print_ok("Renamed graph '" + args[1] + "' to '" + args[2] + "'");
}

void CLIEditor::cmd_switch_graph(const std::vector<std::string>& args) {
    if (args.size() < 2) { print_error("Usage: switch_graph <index|name>"); return; }

    const std::string& target = args[1];
    bool is_index = !target.empty() && std::all_of(target.begin(), target.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });

    Result<void, std::string> r = is_index
        ? session_.set_active(std::stoi(target))
        : session_.set_active(target);

    if (r.is_err()) print_error(r.error());
    else { print_ok("Switched graph to '" + target + "'"); current_block_.clear(); }
}

void CLIEditor::cmd_delete_graph(const std::vector<std::string>& args) {
    if (args.size() < 2) { print_error("Usage: delete <name>"); return; }
    auto r = session_.delete_graph(args[1]);
    if (r.is_err()) print_error(r.error());
    else print_ok("Deleted graph '" + args[1] + "'");
}

void CLIEditor::cmd_graphs() {
    auto& mod = session_.module();
    if (mod.graphs.empty()) { print_ok("(no graphs)"); return; }
    for (size_t i = 0; i < mod.graphs.size(); i++) {
        auto& g = mod.graphs[i];
        std::cout << "  " << (static_cast<int>(i) == session_.active_index() ? "* " : "  ")
                  << g.name;
        if (g.base_type) std::cout << " : " << *g.base_type;
        std::cout << "\n";
    }
}

void CLIEditor::cmd_import(const std::vector<std::string>& args) {
    if (args.size() < 2) { print_error("Usage: import <file.d.gs>"); return; }
    bool already_loaded = session_.is_import_loaded(args[1]);
    auto r = session_.load_import(args[1]);
    if (r.is_err()) print_error(r.error());
    else if (already_loaded) print_ok("Already loaded: " + args[1]);
    else print_ok("Loaded: " + args[1]);
}

void CLIEditor::cmd_let(const std::vector<std::string>& args) {
    if (args.size() < 3) { print_error("Usage: let <name> <Type> [arg]"); return; }
    std::string ctor_arg = args.size() >= 4 ? args[3] : "";
    session_.add_let(args[1], args[2], ctor_arg);
    print_ok("let " + args[1] + " = " + args[2] + "(" + ctor_arg + ")");
}

void CLIEditor::cmd_param(const std::vector<std::string>& args) {
    if (args.size() >= 2 && args[1] == "rm") {
        if (args.size() < 3) { print_error("Usage: param rm <name>"); return; }
        auto r = session_.remove_param(args[2]);
        if (r.is_err()) print_error(r.error());
        else print_ok("Removed param '" + args[2] + "'");
        return;
    }
    if (args.size() < 4) { print_error("Usage: param <in|out|var> <name> <type> [= default]"); return; }
    ParamDirection dir;
    if (args[1] == "in") dir = ParamDirection::In;
    else if (args[1] == "out") dir = ParamDirection::Out;
    else if (args[1] == "var") dir = ParamDirection::Var;
    else { print_error("Direction must be: in, out, var"); return; }

    std::string def;
    if (args.size() >= 6 && args[4] == "=") def = args[5];

    auto r = session_.add_param(dir, args[2], args[3], def);
    if (r.is_err()) print_error(r.error());
    else print_ok(args[1] + " " + args[2] + " : " + args[3] + (def.empty() ? "" : " = " + def));
}

void CLIEditor::cmd_rename_param(const std::vector<std::string>& args) {
    if (args.size() < 3) { print_error("Usage: rename_param <old_name> <new_name>"); return; }
    auto r = session_.rename_param(args[1], args[2]);
    if (r.is_err()) print_error(r.error());
    else print_ok("Renamed param '" + args[1] + "' to '" + args[2] + "'");
}

void CLIEditor::cmd_set_param_type(const std::vector<std::string>& args) {
    if (args.size() < 3) { print_error("Usage: set_param_type <name> <Type>"); return; }
    auto r = session_.set_param_type(args[1], args[2]);
    if (r.is_err()) print_error(r.error());
    else print_ok("Set type for param '" + args[1] + "'");
}

void CLIEditor::cmd_set_param_default(const std::vector<std::string>& args) {
    if (args.size() < 2) { print_error("Usage: set_param_default <name> [value]"); return; }
    const std::string value = args.size() >= 3 ? args[2] : "";
    auto r = session_.set_param_default(args[1], value);
    if (r.is_err()) print_error(r.error());
    else print_ok("Set default for param '" + args[1] + "'");
}

void CLIEditor::cmd_set_param_default_ctor(const std::vector<std::string>& args) {
    if (args.size() < 3) { print_error("Usage: set_param_default_ctor <name> <Type> [arg]"); return; }
    const std::string argument = args.size() >= 4 ? args[3] : "";
    auto r = session_.set_param_default_constructor(args[1], args[2], argument);
    if (r.is_err()) print_error(r.error());
    else print_ok("Set default constructor for param '" + args[1] + "'");
}

void CLIEditor::cmd_set_param_default_ctor_arg(const std::vector<std::string>& args) {
    if (args.size() < 2) { print_error("Usage: set_param_default_ctor_arg <name> [arg]"); return; }
    const std::string argument = args.size() >= 3 ? args[2] : "";
    auto r = session_.set_param_default_constructor_argument(args[1], argument);
    if (r.is_err()) print_error(r.error());
    else print_ok("Set default constructor argument for param '" + args[1] + "'");
}

void CLIEditor::cmd_set_param_default_ctor_type(const std::vector<std::string>& args) {
    if (args.size() < 3) { print_error("Usage: set_param_default_ctor_type <name> <Type>"); return; }
    auto r = session_.set_param_default_constructor_type(args[1], args[2]);
    if (r.is_err()) print_error(r.error());
    else print_ok("Set default constructor type for param '" + args[1] + "'");
}

void CLIEditor::cmd_params() {
    auto* g = session_.active_graph();
    if (!g) { print_error("No active graph"); return; }
    if (g->parameters.empty()) { print_ok("(no parameters)"); return; }
    for (auto& p : g->parameters) {
        std::string dir = p.direction == ParamDirection::In ? "in" :
                          p.direction == ParamDirection::Out ? "out" : "var";
        std::cout << "  " << dir << " " << p.name << " : " << p.type_name;
        if (!p.default_value.empty()) std::cout << " = " << p.default_value;
        std::cout << "\n";
    }
}

void CLIEditor::cmd_add(const std::vector<std::string>& args) {
    if (args.size() < 3) { print_error("Usage: add <Type> <instance_name>"); return; }
    auto r = session_.add_node(args[1], args[2], args.size() >= 4 ? args[3] : "");
    if (r.is_err()) print_error(r.error());
    else print_ok("Added " + args[1] + " " + args[2]);
}

void CLIEditor::cmd_set_init_expr(const std::vector<std::string>& args) {
    if (args.size() < 2) { print_error("Usage: set_init_expr <node> [expr]"); return; }
    const std::string initializer = args.size() >= 3 ? args[2] : "";
    auto r = session_.set_node_initializer(args[1], initializer);
    if (r.is_err()) print_error(r.error());
    else print_ok("Set initializer expression on node '" + args[1] + "'");
}

void CLIEditor::cmd_set_init(const std::vector<std::string>& args) {
    if (args.size() < 4) { print_error("Usage: set_init <node> <field> <value>"); return; }
    auto r = session_.set_node_initializer_field(args[1], args[2], args[3]);
    if (r.is_err()) print_error(r.error());
    else print_ok("Set initializer field '" + args[2] + "' on node '" + args[1] + "'");
}

void CLIEditor::cmd_set_init_ctor(const std::vector<std::string>& args) {
    if (args.size() < 4) { print_error("Usage: set_init_ctor <node> <field> <Type> [arg]"); return; }
    const std::string argument = args.size() >= 5 ? args[4] : "";
    auto r = session_.set_node_initializer_constructor_field(args[1], args[2], args[3], argument);
    if (r.is_err()) print_error(r.error());
    else print_ok("Set initializer field '" + args[2] + "' to constructor '" + args[3] + "' on node '" + args[1] + "'");
}

void CLIEditor::cmd_set_init_ctor_arg(const std::vector<std::string>& args) {
    if (args.size() < 3) { print_error("Usage: set_init_ctor_arg <node> <field> [arg]"); return; }
    const std::string argument = args.size() >= 4 ? args[3] : "";
    auto r = session_.set_node_initializer_constructor_argument(args[1], args[2], argument);
    if (r.is_err()) print_error(r.error());
    else print_ok("Set initializer constructor argument for field '" + args[2] + "' on node '" + args[1] + "'");
}

void CLIEditor::cmd_set_init_ctor_type(const std::vector<std::string>& args) {
    if (args.size() < 4) { print_error("Usage: set_init_ctor_type <node> <field> <Type>"); return; }
    auto r = session_.set_node_initializer_constructor_type(args[1], args[2], args[3]);
    if (r.is_err()) print_error(r.error());
    else print_ok("Set initializer constructor type for field '" + args[2] + "' on node '" + args[1] + "'");
}

void CLIEditor::cmd_unset_init(const std::vector<std::string>& args) {
    if (args.size() < 3) { print_error("Usage: unset_init <node> <field>"); return; }
    auto r = session_.remove_node_initializer_field(args[1], args[2]);
    if (r.is_err()) print_error(r.error());
    else print_ok("Removed initializer field '" + args[2] + "' from node '" + args[1] + "'");
}

void CLIEditor::cmd_rename_init(const std::vector<std::string>& args) {
    if (args.size() < 4) { print_error("Usage: rename_init <node> <old_field> <new_field>"); return; }
    auto r = session_.rename_node_initializer_field(args[1], args[2], args[3]);
    if (r.is_err()) print_error(r.error());
    else print_ok("Renamed initializer field '" + args[2] + "' to '" + args[3] + "' on node '" + args[1] + "'");
}

void CLIEditor::cmd_rm(const std::vector<std::string>& args) {
    if (args.size() < 2) { print_error("Usage: rm <instance_name>"); return; }
    auto r = session_.remove_node(args[1]);
    if (r.is_err()) print_error(r.error());
    else print_ok("Removed '" + args[1] + "'");
}

void CLIEditor::cmd_rename_node(const std::vector<std::string>& args) {
    if (args.size() < 3) { print_error("Usage: rename_node <old_name> <new_name>"); return; }
    auto r = session_.rename_node_instance(args[1], args[2]);
    if (r.is_err()) print_error(r.error());
    else print_ok("Renamed node '" + args[1] + "' to '" + args[2] + "'");
}

void CLIEditor::cmd_nodes() {
    auto* g = session_.active_graph();
    if (!g) { print_error("No active graph"); return; }
    if (g->node_instances.empty()) { print_ok("(no nodes)"); return; }
    for (auto& ni : g->node_instances) {
        std::cout << "  " << ni.type_name << " " << ni.instance_name;
        if (!ni.initializer.empty()) std::cout << "{" << ni.initializer << "}";
        std::cout << "\n";
    }
}

void CLIEditor::cmd_info(const std::vector<std::string>& args) {
    if (args.size() < 2) { print_error("Usage: info <instance_name>"); return; }
    auto* g = session_.active_graph();
    if (!g) { print_error("No active graph"); return; }
    for (auto& ni : g->node_instances) {
        if (ni.instance_name == args[1]) {
            auto* def = session_.env().nodes().find(ni.type_name);
            std::cout << "  " << ni.type_name << " " << ni.instance_name << "\n";
            if (def) {
                auto ei = def->exec_inputs();
                auto eo = def->exec_outputs();
                auto di = def->data_inputs();
                auto dout = def->data_outputs();
                if (!ei.empty()) { std::cout << "  Exec In: "; for (auto* p : ei) std::cout << p->name << " "; std::cout << "\n"; }
                if (!eo.empty()) { std::cout << "  Exec Out: "; for (auto* p : eo) std::cout << p->name << " "; std::cout << "\n"; }
                if (!di.empty()) { std::cout << "  Data In: "; for (auto* p : di) std::cout << p->name << ":" << p->type_name << " "; std::cout << "\n"; }
                if (!dout.empty()) { std::cout << "  Data Out: "; for (auto* p : dout) std::cout << p->name << ":" << p->type_name << " "; std::cout << "\n"; }
            }
            // Show connections
            for (auto& ev : g->events) {
                for (auto& fc : ev.flow_connections) {
                    if (fc.from.node_instance == args[1])
                        std::cout << "  [" << ev.name << "] " << fc.from.node_instance << "." << fc.from.pin_name << " -> " << fc.to.node_instance << "." << fc.to.pin_name << "\n";
                    if (fc.to.node_instance == args[1])
                        std::cout << "  [" << ev.name << "] " << fc.from.node_instance << "." << fc.from.pin_name << " -> " << fc.to.node_instance << "." << fc.to.pin_name << "\n";
                }
                for (auto& dl : ev.data_links) {
                    if (dl.target.node_instance == args[1] || dl.source.node_instance == args[1])
                        std::cout << "  [" << ev.name << "] " << dl.target.node_instance << "." << dl.target.pin_name << " = "
                                  << dl.source.node_instance << (dl.source.pin_name.empty() ? "" : "." + dl.source.pin_name) << "\n";
                }
            }
            for (auto& fn : g->functions) {
                for (auto& fc : fn.flow_connections) {
                    if (fc.from.node_instance == args[1] || fc.to.node_instance == args[1])
                        std::cout << "  [" << fn.name << "] " << fc.from.node_instance << "." << fc.from.pin_name << " -> " << fc.to.node_instance << "." << fc.to.pin_name << "\n";
                }
                for (auto& dl : fn.data_links) {
                    if (dl.target.node_instance == args[1] || dl.source.node_instance == args[1])
                        std::cout << "  [" << fn.name << "] " << dl.target.node_instance << "." << dl.target.pin_name << " = "
                                  << dl.source.node_instance << (dl.source.pin_name.empty() ? "" : "." + dl.source.pin_name) << "\n";
                }
            }
            return;
        }
    }
    print_error("Node '" + args[1] + "' not found");
}

void CLIEditor::cmd_pins(const std::vector<std::string>& args) {
    if (args.size() < 2) { print_error("Usage: pins <Type_or_instance>"); return; }
    // Try instance name first
    auto* g = session_.active_graph();
    std::string type_name = args[1];
    if (g) {
        for (auto& ni : g->node_instances) {
            if (ni.instance_name == args[1]) { type_name = ni.type_name; break; }
        }
    }
    auto* def = session_.env().nodes().find(type_name);
    if (!def) { print_error("Unknown type/instance: " + args[1]); return; }
    for (auto& p : def->pins) {
        std::string k = (p.kind == PinKind::Exec ? "exec" : "data");
        std::string d = (p.direction == PinDirection::Input ? "in" : "out");
        std::cout << "  " << k << " " << d << " " << p.name;
        if (!p.type_name.empty()) std::cout << " : " << p.type_name;
        std::cout << "\n";
    }
}

void CLIEditor::cmd_event(const std::vector<std::string>& args) {
    if (args.size() < 2) { print_error("Usage: event <name>"); return; }
    if (args[1] == "rm") {
        cmd_delete_event(args);
        return;
    }
    auto* g = session_.active_graph();
    if (!g) { print_error("No active graph"); return; }
    // If event doesn't exist, create it
    bool exists = false;
    for (auto& ev : g->events) {
        if (ev.name == args[1]) { exists = true; break; }
    }
    if (!exists) {
        auto r = session_.add_event(args[1]);
        if (r.is_err()) { print_error(r.error()); return; }
        print_ok("Created event '" + args[1] + "'");
    }
    current_block_ = args[1];
    print_ok("Editing event '" + args[1] + "'");
}

void CLIEditor::cmd_fn(const std::vector<std::string>& args) {
    if (args.size() < 2) { print_error("Usage: fn <name>"); return; }
    if (args[1] == "rm") {
        cmd_delete_function(args);
        return;
    }
    auto* g = session_.active_graph();
    if (!g) { print_error("No active graph"); return; }
    bool exists = false;
    for (auto& fn : g->functions) {
        if (fn.name == args[1]) { exists = true; break; }
    }
    if (!exists) {
        auto r = session_.add_function(args[1]);
        if (r.is_err()) { print_error(r.error()); return; }
        print_ok("Created function '" + args[1] + "'");
    }
    current_block_ = args[1];
    print_ok("Editing function '" + args[1] + "'");
}

void CLIEditor::cmd_rename_event(const std::vector<std::string>& args) {
    if (args.size() < 3) { print_error("Usage: rename_event <old_name> <new_name>"); return; }
    auto r = session_.rename_event(args[1], args[2]);
    if (r.is_err()) print_error(r.error());
    else {
        if (current_block_ == args[1]) current_block_ = args[2];
        print_ok("Renamed event '" + args[1] + "' to '" + args[2] + "'");
    }
}

void CLIEditor::cmd_rename_function(const std::vector<std::string>& args) {
    if (args.size() < 3) { print_error("Usage: rename_function <old_name> <new_name>"); return; }
    auto r = session_.rename_function(args[1], args[2]);
    if (r.is_err()) print_error(r.error());
    else {
        if (current_block_ == args[1]) current_block_ = args[2];
        print_ok("Renamed function '" + args[1] + "' to '" + args[2] + "'");
    }
}

void CLIEditor::cmd_delete_event(const std::vector<std::string>& args) {
    bool nested = args.size() >= 2 && args[1] == "rm";
    size_t name_index = nested ? 2 : 1;
    if (args.size() <= name_index) {
        print_error(nested ? "Usage: event rm <name>" : "Usage: delete_event <name>");
        return;
    }
    auto r = session_.remove_event(args[name_index]);
    if (r.is_err()) print_error(r.error());
    else {
        if (current_block_ == args[name_index]) current_block_.clear();
        print_ok("Removed event '" + args[name_index] + "'");
    }
}

void CLIEditor::cmd_delete_function(const std::vector<std::string>& args) {
    bool nested = args.size() >= 2 && args[1] == "rm";
    size_t name_index = nested ? 2 : 1;
    if (args.size() <= name_index) {
        print_error(nested ? "Usage: fn rm <name>" : "Usage: delete_function <name>");
        return;
    }
    auto r = session_.remove_function(args[name_index]);
    if (r.is_err()) print_error(r.error());
    else {
        if (current_block_ == args[name_index]) current_block_.clear();
        print_ok("Removed function '" + args[name_index] + "'");
    }
}

void CLIEditor::cmd_flow(const std::vector<std::string>& args) {
    if (args.size() < 3) { print_error("Usage: flow <from.pin> <to.pin>"); return; }
    if (current_block_.empty()) { print_error("Enter an event/fn first (event <name> or fn <name>)"); return; }
    auto [fn, fp] = parse_pin_ref(args[1]);
    auto [tn, tp] = parse_pin_ref(args[2]);
    if (fp.empty() || tp.empty()) { print_error("Format: node.pin"); return; }
    auto r = session_.add_flow(current_block_, fn, fp, tn, tp);
    if (r.is_err()) print_error(r.error());
    else print_ok(fn + "." + fp + " -> " + tn + "." + tp);
}

void CLIEditor::cmd_link(const std::vector<std::string>& args) {
    if (args.size() < 3) { print_error("Usage: link <target.pin> <source[.pin]>"); return; }
    if (current_block_.empty()) { print_error("Enter an event/fn first"); return; }
    auto [tn, tp] = parse_pin_ref(args[1]);
    auto [sn, sp] = parse_pin_ref(args[2]);
    if (tp.empty()) { print_error("Target must be node.pin"); return; }
    auto r = session_.add_link(current_block_, tn, tp, sn, sp);
    if (r.is_err()) print_error(r.error());
    else print_ok(tn + "." + tp + " = " + sn + (sp.empty() ? "" : "." + sp));
}

void CLIEditor::cmd_unflow(const std::vector<std::string>& args) {
    if (args.size() < 3) { print_error("Usage: unflow <from.pin> <to.pin>"); return; }
    if (current_block_.empty()) { print_error("Enter an event/fn first"); return; }
    auto [fn, fp] = parse_pin_ref(args[1]);
    auto [tn, tp] = parse_pin_ref(args[2]);
    auto r = session_.remove_flow(current_block_, fn, fp, tn, tp);
    if (r.is_err()) print_error(r.error());
    else print_ok("Removed flow");
}

void CLIEditor::cmd_unlink(const std::vector<std::string>& args) {
    if (args.size() < 2) { print_error("Usage: unlink <target.pin>"); return; }
    if (current_block_.empty()) { print_error("Enter an event/fn first"); return; }
    auto [tn, tp] = parse_pin_ref(args[1]);
    auto r = session_.remove_link(current_block_, tn, tp);
    if (r.is_err()) print_error(r.error());
    else print_ok("Removed link");
}

void CLIEditor::cmd_done() {
    if (current_block_.empty()) { print_ok("Already at graph level"); return; }
    print_ok("Back to graph level");
    current_block_.clear();
}

void CLIEditor::cmd_connections() {
    auto* g = session_.active_graph();
    if (!g) { print_error("No active graph"); return; }

    auto print_block = [](const LogicBlock& b) {
        if (b.flow_connections.empty() && b.data_links.empty()) return;
        std::cout << "  [" << b.name << "]\n";
        for (auto& fc : b.flow_connections)
            std::cout << "    flow " << fc.from.node_instance << "." << fc.from.pin_name
                      << " -> " << fc.to.node_instance << "." << fc.to.pin_name << "\n";
        for (auto& dl : b.data_links)
            std::cout << "    link " << dl.target.node_instance << "." << dl.target.pin_name
                      << " = " << dl.source.node_instance
                      << (dl.source.pin_name.empty() ? "" : "." + dl.source.pin_name) << "\n";
    };

    for (auto& ev : g->events) print_block(ev);
    for (auto& fn : g->functions) print_block(fn);
}

void CLIEditor::cmd_annotate(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        print_error("Usage: annotate <graph|import|let|node|param|event|function|flow|link|generate-comment|generate-meta> [target] <Annotation> [arg|key=value]...");
        return;
    }

    Result<void, std::string> r = Result<void, std::string>::err("Invalid annotation target");
    if (args[1] == "graph") {
        auto annot = parse_annotation_args(args, 2);
        if (annot.name.empty()) { print_error("Usage: annotate graph <Annotation> [args...]"); return; }
        r = session_.set_graph_annotation(annot);
        if (r.is_ok()) print_ok("Annotated graph [" + annot.name + "]");
    } else if (args[1] == "import") {
        if (args.size() < 4) { print_error("Usage: annotate import <path> <Annotation> [args...]"); return; }
        auto annot = parse_annotation_args(args, 3);
        if (annot.name.empty()) { print_error("Usage: annotate import <path> <Annotation> [args...]"); return; }
        r = session_.set_import_annotation(args[2], annot);
        if (r.is_ok()) print_ok("Annotated import '" + args[2] + "' [" + annot.name + "]");
    } else if (args[1] == "let") {
        if (args.size() < 4) { print_error("Usage: annotate let <name> <Annotation> [args...]"); return; }
        auto annot = parse_annotation_args(args, 3);
        if (annot.name.empty()) { print_error("Usage: annotate let <name> <Annotation> [args...]"); return; }
        r = session_.set_let_annotation(args[2], annot);
        if (r.is_ok()) print_ok("Annotated let '" + args[2] + "' [" + annot.name + "]");
    } else if (args[1] == "node") {
        if (args.size() < 4) { print_error("Usage: annotate node <instance> <Annotation> [args...]"); return; }
        auto annot = parse_annotation_args(args, 3);
        if (annot.name.empty()) { print_error("Usage: annotate node <instance> <Annotation> [args...]"); return; }
        r = session_.set_node_annotation(args[2], annot);
        if (r.is_ok()) print_ok("Annotated node '" + args[2] + "' [" + annot.name + "]");
    } else if (args[1] == "param") {
        if (args.size() < 4) { print_error("Usage: annotate param <name> <Annotation> [args...]"); return; }
        auto annot = parse_annotation_args(args, 3);
        if (annot.name.empty()) { print_error("Usage: annotate param <name> <Annotation> [args...]"); return; }
        r = session_.set_param_annotation(args[2], annot);
        if (r.is_ok()) print_ok("Annotated param '" + args[2] + "' [" + annot.name + "]");
    } else if (args[1] == "event" || args[1] == "function") {
        if (args.size() < 4) {
            print_error("Usage: annotate <event|function> <name> <Annotation> [args...]");
            return;
        }
        auto annot = parse_annotation_args(args, 3);
        if (annot.name.empty()) {
            print_error("Usage: annotate <event|function> <name> <Annotation> [args...]");
            return;
        }
        r = session_.set_block_annotation(args[1], args[2], annot);
        if (r.is_ok()) print_ok("Annotated " + args[1] + " '" + args[2] + "' [" + annot.name + "]");
    } else if (args[1] == "flow") {
        if (args.size() < 7) {
            print_error("Usage: annotate flow <event|function> <block> <from.pin> <to.pin> <Annotation> [args...]");
            return;
        }
        auto [from_node, from_pin] = parse_pin_ref(args[4]);
        auto [to_node, to_pin] = parse_pin_ref(args[5]);
        if (from_pin.empty() || to_pin.empty()) {
            print_error("Flow endpoints must be node.pin");
            return;
        }
        auto annot = parse_annotation_args(args, 6);
        if (annot.name.empty()) {
            print_error("Usage: annotate flow <event|function> <block> <from.pin> <to.pin> <Annotation> [args...]");
            return;
        }
        r = session_.set_flow_annotation(args[2], args[3], from_node, from_pin, to_node, to_pin, annot);
        if (r.is_ok()) print_ok("Annotated flow '" + args[4] + " -> " + args[5] + "' [" + annot.name + "]");
    } else if (args[1] == "link") {
        if (args.size() < 7) {
            print_error("Usage: annotate link <event|function> <block> <target.pin> <source> <Annotation> [args...]");
            return;
        }
        auto [target_node, target_pin] = parse_pin_ref(args[4]);
        auto [source_node, source_pin] = parse_pin_ref(args[5]);
        if (target_pin.empty()) {
            print_error("Link target must be node.pin");
            return;
        }
        auto annot = parse_annotation_args(args, 6);
        if (annot.name.empty()) {
            print_error("Usage: annotate link <event|function> <block> <target.pin> <source> <Annotation> [args...]");
            return;
        }
        r = session_.set_link_annotation(args[2], args[3], target_node, target_pin, source_node, source_pin, annot);
        if (r.is_ok()) print_ok("Annotated link '" + args[4] + " = " + args[5] + "' [" + annot.name + "]");
    } else if (args[1] == "generate-comment") {
        if (args.size() < 5) {
            print_error("Usage: annotate generate-comment <instance> <text> [#N] <Annotation> [args...]");
            return;
        }
        size_t occurrence = 0;
        size_t annotation_start = 4;
        if (args[4][0] == '#') {
            if (!parse_occurrence_arg(args[4], occurrence)) {
                print_error("Occurrence must be #<positive-number>");
                return;
            }
            annotation_start = 5;
        }
        auto annot = parse_annotation_args(args, annotation_start);
        if (annot.name.empty()) {
            print_error("Usage: annotate generate-comment <instance> <text> [#N] <Annotation> [args...]");
            return;
        }
        r = session_.set_generate_comment_annotation(args[2], args[3], occurrence, annot);
        if (r.is_ok()) print_ok("Annotated generate comment '" + args[2] + "' [" + annot.name + "]");
    } else if (args[1] == "generate-meta" || args[1] == "generate-metadata") {
        if (args.size() < 5) {
            print_error("Usage: annotate generate-meta <scope:node.prop> <value> [#N] <Annotation> [args...]");
            return;
        }
        std::string scope, node, prop;
        if (!parse_generate_meta_ref(args[2], scope, node, prop)) {
            print_error("Format: scope:node.prop");
            return;
        }
        size_t occurrence = 0;
        size_t annotation_start = 4;
        if (args[4][0] == '#') {
            if (!parse_occurrence_arg(args[4], occurrence)) {
                print_error("Occurrence must be #<positive-number>");
                return;
            }
            annotation_start = 5;
        }
        auto annot = parse_annotation_args(args, annotation_start);
        if (annot.name.empty()) {
            print_error("Usage: annotate generate-meta <scope:node.prop> <value> [#N] <Annotation> [args...]");
            return;
        }
        r = session_.set_generate_metadata_annotation(scope, node, prop, args[3], occurrence, annot);
        if (r.is_ok()) print_ok("Annotated generate metadata '" + args[2] + "' [" + annot.name + "]");
    } else {
        print_error("Annotation target must be: graph, import, let, node, param, event, function, flow, link, generate-comment, generate-meta");
        return;
    }

    if (r.is_err()) print_error(r.error());
}

void CLIEditor::cmd_unannotate(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        print_error("Usage: unannotate <graph|import|let|node|param|event|function|flow|link|generate-comment|generate-meta> [target] <Annotation>");
        return;
    }

    Result<void, std::string> r = Result<void, std::string>::err("Invalid annotation target");
    if (args[1] == "graph") {
        r = session_.remove_graph_annotation(args[2]);
        if (r.is_ok()) print_ok("Removed graph annotation [" + args[2] + "]");
    } else if (args[1] == "import") {
        if (args.size() < 4) { print_error("Usage: unannotate import <path> <Annotation>"); return; }
        r = session_.remove_import_annotation(args[2], args[3]);
        if (r.is_ok()) print_ok("Removed import annotation '" + args[2] + "' [" + args[3] + "]");
    } else if (args[1] == "let") {
        if (args.size() < 4) { print_error("Usage: unannotate let <name> <Annotation>"); return; }
        r = session_.remove_let_annotation(args[2], args[3]);
        if (r.is_ok()) print_ok("Removed let annotation '" + args[2] + "' [" + args[3] + "]");
    } else if (args[1] == "node") {
        if (args.size() < 4) { print_error("Usage: unannotate node <instance> <Annotation>"); return; }
        r = session_.remove_node_annotation(args[2], args[3]);
        if (r.is_ok()) print_ok("Removed node annotation '" + args[2] + "' [" + args[3] + "]");
    } else if (args[1] == "param") {
        if (args.size() < 4) { print_error("Usage: unannotate param <name> <Annotation>"); return; }
        r = session_.remove_param_annotation(args[2], args[3]);
        if (r.is_ok()) print_ok("Removed param annotation '" + args[2] + "' [" + args[3] + "]");
    } else if (args[1] == "event" || args[1] == "function") {
        if (args.size() < 4) { print_error("Usage: unannotate <event|function> <name> <Annotation>"); return; }
        r = session_.remove_block_annotation(args[1], args[2], args[3]);
        if (r.is_ok()) print_ok("Removed " + args[1] + " annotation '" + args[2] + "' [" + args[3] + "]");
    } else if (args[1] == "flow") {
        if (args.size() < 7) {
            print_error("Usage: unannotate flow <event|function> <block> <from.pin> <to.pin> <Annotation>");
            return;
        }
        auto [from_node, from_pin] = parse_pin_ref(args[4]);
        auto [to_node, to_pin] = parse_pin_ref(args[5]);
        if (from_pin.empty() || to_pin.empty()) {
            print_error("Flow endpoints must be node.pin");
            return;
        }
        r = session_.remove_flow_annotation(args[2], args[3], from_node, from_pin, to_node, to_pin, args[6]);
        if (r.is_ok()) print_ok("Removed flow annotation '" + args[4] + " -> " + args[5] + "' [" + args[6] + "]");
    } else if (args[1] == "link") {
        if (args.size() < 7) {
            print_error("Usage: unannotate link <event|function> <block> <target.pin> <source> <Annotation>");
            return;
        }
        auto [target_node, target_pin] = parse_pin_ref(args[4]);
        auto [source_node, source_pin] = parse_pin_ref(args[5]);
        if (target_pin.empty()) {
            print_error("Link target must be node.pin");
            return;
        }
        r = session_.remove_link_annotation(args[2], args[3], target_node, target_pin, source_node, source_pin, args[6]);
        if (r.is_ok()) print_ok("Removed link annotation '" + args[4] + " = " + args[5] + "' [" + args[6] + "]");
    } else if (args[1] == "generate-comment") {
        if (args.size() < 5) {
            print_error("Usage: unannotate generate-comment <instance> <text> [#N] <Annotation>");
            return;
        }
        size_t occurrence = 0;
        size_t annotation_name_index = 4;
        if (args[4][0] == '#') {
            if (!parse_occurrence_arg(args[4], occurrence)) {
                print_error("Occurrence must be #<positive-number>");
                return;
            }
            annotation_name_index = 5;
        }
        if (annotation_name_index >= args.size()) {
            print_error("Usage: unannotate generate-comment <instance> <text> [#N] <Annotation>");
            return;
        }
        r = session_.remove_generate_comment_annotation(args[2], args[3], occurrence, args[annotation_name_index]);
        if (r.is_ok()) print_ok("Removed generate comment annotation '" + args[2] + "' [" + args[annotation_name_index] + "]");
    } else if (args[1] == "generate-meta" || args[1] == "generate-metadata") {
        if (args.size() < 5) {
            print_error("Usage: unannotate generate-meta <scope:node.prop> <value> [#N] <Annotation>");
            return;
        }
        std::string scope, node, prop;
        if (!parse_generate_meta_ref(args[2], scope, node, prop)) {
            print_error("Format: scope:node.prop");
            return;
        }
        size_t occurrence = 0;
        size_t annotation_name_index = 4;
        if (args[4][0] == '#') {
            if (!parse_occurrence_arg(args[4], occurrence)) {
                print_error("Occurrence must be #<positive-number>");
                return;
            }
            annotation_name_index = 5;
        }
        if (annotation_name_index >= args.size()) {
            print_error("Usage: unannotate generate-meta <scope:node.prop> <value> [#N] <Annotation>");
            return;
        }
        r = session_.remove_generate_metadata_annotation(scope, node, prop, args[3], occurrence, args[annotation_name_index]);
        if (r.is_ok()) print_ok("Removed generate metadata annotation '" + args[2] + "' [" + args[annotation_name_index] + "]");
    } else {
        print_error("Annotation target must be: graph, import, let, node, param, event, function, flow, link, generate-comment, generate-meta");
        return;
    }

    if (r.is_err()) print_error(r.error());
}

void CLIEditor::cmd_comment(const std::vector<std::string>& args) {
    if (args.size() >= 2 && args[1] == "rm") {
        cmd_remove_comment(args);
        return;
    }
    if (args.size() >= 2 && args[1] == "move") {
        cmd_move_comment(args);
        return;
    }
    if (args.size() >= 2 && args[1] == "rename") {
        cmd_rename_comment(args);
        return;
    }
    if (args.size() < 3) { print_error("Usage: comment <name> <text>"); return; }
    // Join remaining args as text
    std::string text;
    for (size_t i = 2; i < args.size(); i++) { if (i > 2) text += " "; text += args[i]; }
    auto r = session_.add_comment(args[1], text);
    if (r.is_err()) print_error(r.error());
    else print_ok("Comment " + args[1] + " = \"" + text + "\"");
}

void CLIEditor::cmd_meta(const std::vector<std::string>& args) {
    if (args.size() >= 2 && args[1] == "rm") {
        cmd_remove_meta(args);
        return;
    }
    if (args.size() >= 2 && args[1] == "move") {
        cmd_move_meta(args);
        return;
    }
    if (args.size() >= 2 && args[1] == "rename") {
        cmd_rename_meta(args);
        return;
    }
    if (args.size() >= 2 && args[1] == "rename_ref") {
        cmd_rename_meta_ref(args);
        return;
    }
    if (args.size() < 3) { print_error("Usage: meta <scope:node.prop> <value>"); return; }
    // Parse scope:node.prop
    auto& ref = args[1];
    auto colon = ref.find(':');
    if (colon == std::string::npos) { print_error("Format: scope:node.prop"); return; }
    std::string scope = ref.substr(0, colon);
    std::string rest = ref.substr(colon + 1);
    auto dot = rest.find('.');
    if (dot == std::string::npos) { print_error("Format: scope:node.prop"); return; }
    std::string node = rest.substr(0, dot);
    std::string prop = rest.substr(dot + 1);

    auto r = session_.add_meta(scope, node, prop, args[2]);
    if (r.is_err()) print_error(r.error());
    else print_ok(ref + "(" + args[2] + ")");
}

void CLIEditor::cmd_remove_comment(const std::vector<std::string>& args) {
    const bool nested = args.size() >= 2 && args[0] == "comment" && args[1] == "rm";
    const size_t instance_index = nested ? 2 : 1;
    const size_t text_start = instance_index + 1;
    if (args.size() <= text_start) {
        print_error(nested ? "Usage: comment rm <name> <text> [#N]" : "Usage: remove_comment <name> <text> [#N]");
        return;
    }

    size_t occurrence = 0;
    size_t text_end = args.size();
    if (parse_occurrence_arg(args.back(), occurrence)) {
        text_end -= 1;
    } else if (!args.back().empty() && args.back()[0] == '#') {
        print_error("Occurrence must be #<positive-number>");
        return;
    }
    if (text_start >= text_end) {
        print_error(nested ? "Usage: comment rm <name> <text> [#N]" : "Usage: remove_comment <name> <text> [#N]");
        return;
    }

    std::string text;
    for (size_t i = text_start; i < text_end; ++i) {
        if (i > text_start) text += " ";
        text += args[i];
    }

    auto r = session_.remove_comment(args[instance_index], text, occurrence);
    if (r.is_err()) print_error(r.error());
    else print_ok("Removed comment " + args[instance_index] + " = \"" + text + "\"");
}

void CLIEditor::cmd_remove_meta(const std::vector<std::string>& args) {
    const bool nested = args.size() >= 2 && args[0] == "meta" && args[1] == "rm";
    const size_t ref_index = nested ? 2 : 1;
    const size_t value_index = ref_index + 1;
    if (args.size() <= value_index) {
        print_error(nested ? "Usage: meta rm <scope:node.prop> <value> [#N]" : "Usage: remove_meta <scope:node.prop> <value> [#N]");
        return;
    }

    size_t occurrence = 0;
    if (args.size() > value_index + 1) {
        if (args.size() != value_index + 2 || !parse_occurrence_arg(args[value_index + 1], occurrence)) {
            print_error("Usage: meta rm <scope:node.prop> <value> [#N]");
            return;
        }
    }

    std::string scope, node, prop;
    if (!parse_generate_meta_ref(args[ref_index], scope, node, prop)) {
        print_error("Format: scope:node.prop");
        return;
    }

    auto r = session_.remove_meta(scope, node, prop, args[value_index], occurrence);
    if (r.is_err()) print_error(r.error());
    else print_ok("Removed metadata " + args[ref_index] + "(" + args[value_index] + ")");
}

void CLIEditor::cmd_move_comment(const std::vector<std::string>& args) {
    const bool nested = args.size() >= 2 && args[0] == "comment" && args[1] == "move";
    const size_t instance_index = nested ? 2 : 1;
    const size_t text_start = instance_index + 1;
    if (args.size() <= text_start + 1) {
        print_error(nested ? "Usage: comment move <name> <text> [#N] <up|down>" : "Usage: move_comment <name> <text> [#N] <up|down>");
        return;
    }

    auto direction = parse_move_direction(args.back());
    if (!direction) {
        print_error("Direction must be up or down");
        return;
    }

    size_t occurrence = 0;
    size_t text_end = args.size() - 1;
    if (parse_occurrence_arg(args[text_end - 1], occurrence)) {
        text_end -= 1;
    } else if (!args[text_end - 1].empty() && args[text_end - 1][0] == '#') {
        print_error("Occurrence must be #<positive-number>");
        return;
    }
    if (text_start >= text_end) {
        print_error(nested ? "Usage: comment move <name> <text> [#N] <up|down>" : "Usage: move_comment <name> <text> [#N] <up|down>");
        return;
    }

    std::string text;
    for (size_t i = text_start; i < text_end; ++i) {
        if (i > text_start) text += " ";
        text += args[i];
    }

    auto r = session_.move_comment(args[instance_index], text, occurrence, *direction);
    if (r.is_err()) print_error(r.error());
    else print_ok("Moved comment " + args[instance_index] + " " + args.back());
}

void CLIEditor::cmd_move_meta(const std::vector<std::string>& args) {
    const bool nested = args.size() >= 2 && args[0] == "meta" && args[1] == "move";
    const size_t ref_index = nested ? 2 : 1;
    const size_t value_index = ref_index + 1;
    if (args.size() <= value_index + 1) {
        print_error(nested ? "Usage: meta move <scope:node.prop> <value> [#N] <up|down>" : "Usage: move_meta <scope:node.prop> <value> [#N] <up|down>");
        return;
    }

    auto direction = parse_move_direction(args.back());
    if (!direction) {
        print_error("Direction must be up or down");
        return;
    }

    size_t occurrence = 0;
    if (args.size() > value_index + 3) {
        print_error(nested ? "Usage: meta move <scope:node.prop> <value> [#N] <up|down>" : "Usage: move_meta <scope:node.prop> <value> [#N] <up|down>");
        return;
    }
    if (args.size() == value_index + 3 && !parse_occurrence_arg(args[value_index + 1], occurrence)) {
        if (!args[value_index + 1].empty() && args[value_index + 1][0] == '#') {
            print_error("Occurrence must be #<positive-number>");
        } else {
            print_error(nested ? "Usage: meta move <scope:node.prop> <value> [#N] <up|down>" : "Usage: move_meta <scope:node.prop> <value> [#N] <up|down>");
        }
        return;
    }

    std::string scope, node, prop;
    if (!parse_generate_meta_ref(args[ref_index], scope, node, prop)) {
        print_error("Format: scope:node.prop");
        return;
    }

    auto r = session_.move_meta(scope, node, prop, args[value_index], occurrence, *direction);
    if (r.is_err()) print_error(r.error());
    else print_ok("Moved metadata " + args[ref_index] + " " + args.back());
}

void CLIEditor::cmd_rename_comment(const std::vector<std::string>& args) {
    const bool nested = args.size() >= 2 && args[0] == "comment" && args[1] == "rename";
    const size_t instance_index = nested ? 2 : 1;
    const size_t old_text_index = instance_index + 1;
    if (args.size() != old_text_index + 2 && args.size() != old_text_index + 3) {
        print_error(nested ? "Usage: comment rename <name> <old_text> [#N] <new_text>" : "Usage: rename_comment <name> <old_text> [#N] <new_text>");
        return;
    }

    size_t occurrence = 0;
    size_t new_text_index = old_text_index + 1;
    if (args.size() == old_text_index + 3) {
        if (!parse_occurrence_arg(args[old_text_index + 1], occurrence)) {
            print_error("Occurrence must be #<positive-number>");
            return;
        }
        new_text_index = old_text_index + 2;
    }

    auto r = session_.rename_comment(args[instance_index], args[old_text_index], occurrence, args[new_text_index]);
    if (r.is_err()) print_error(r.error());
    else print_ok("Renamed comment " + args[instance_index]);
}

void CLIEditor::cmd_rename_meta(const std::vector<std::string>& args) {
    const bool nested = args.size() >= 2 && args[0] == "meta" && args[1] == "rename";
    const size_t ref_index = nested ? 2 : 1;
    const size_t old_value_index = ref_index + 1;
    if (args.size() != old_value_index + 2 && args.size() != old_value_index + 3) {
        print_error(nested ? "Usage: meta rename <scope:node.prop> <old_value> [#N] <new_value>" : "Usage: rename_meta <scope:node.prop> <old_value> [#N] <new_value>");
        return;
    }

    size_t occurrence = 0;
    size_t new_value_index = old_value_index + 1;
    if (args.size() == old_value_index + 3) {
        if (!parse_occurrence_arg(args[old_value_index + 1], occurrence)) {
            print_error("Occurrence must be #<positive-number>");
            return;
        }
        new_value_index = old_value_index + 2;
    }

    std::string scope, node, prop;
    if (!parse_generate_meta_ref(args[ref_index], scope, node, prop)) {
        print_error("Format: scope:node.prop");
        return;
    }

    auto r = session_.rename_meta(scope, node, prop, args[old_value_index], occurrence, args[new_value_index]);
    if (r.is_err()) print_error(r.error());
    else print_ok("Renamed metadata " + args[ref_index]);
}

void CLIEditor::cmd_rename_meta_ref(const std::vector<std::string>& args) {
    const bool nested = args.size() >= 2 && args[0] == "meta" && args[1] == "rename_ref";
    const size_t ref_index = nested ? 2 : 1;
    const size_t value_index = ref_index + 1;
    if (args.size() != value_index + 2 && args.size() != value_index + 3) {
        print_error(nested
            ? "Usage: meta rename_ref <scope:node.prop> <value> [#N] <new_scope:node.prop>"
            : "Usage: rename_meta_ref <scope:node.prop> <value> [#N] <new_scope:node.prop>");
        return;
    }

    size_t occurrence = 0;
    size_t new_ref_index = value_index + 1;
    if (args.size() == value_index + 3) {
        if (!parse_occurrence_arg(args[value_index + 1], occurrence)) {
            print_error("Occurrence must be #<positive-number>");
            return;
        }
        new_ref_index = value_index + 2;
    }

    std::string scope, node, prop;
    if (!parse_generate_meta_ref(args[ref_index], scope, node, prop)) {
        print_error("Format: scope:node.prop");
        return;
    }

    std::string new_scope, new_node, new_prop;
    if (!parse_generate_meta_ref(args[new_ref_index], new_scope, new_node, new_prop)) {
        print_error("Format: scope:node.prop");
        return;
    }

    auto r = session_.rename_meta_ref(scope, node, prop, args[value_index], occurrence, new_scope, new_node, new_prop);
    if (r.is_err()) print_error(r.error());
    else print_ok("Renamed metadata reference " + args[ref_index] + " to " + args[new_ref_index]);
}

void CLIEditor::cmd_undo() {
    auto r = session_.undo();
    if (r.is_err()) print_error(r.error());
    else print_ok("Undone: " + r.value());
}

void CLIEditor::cmd_redo() {
    auto r = session_.redo();
    if (r.is_err()) print_error(r.error());
    else print_ok("Redone: " + r.value());
}

void CLIEditor::cmd_history() {
    auto undo = session_.undo_history();
    auto redo = session_.redo_history();
    std::cout << "  Undo stack (" << undo.size() << "):\n";
    for (size_t i = 0; i < undo.size(); i++)
        std::cout << "    " << (i + 1) << ". " << undo[i] << "\n";
    std::cout << "  Redo stack (" << redo.size() << "):\n";
    for (size_t i = 0; i < redo.size(); i++)
        std::cout << "    " << (i + 1) << ". " << redo[i] << "\n";
}

void CLIEditor::cmd_emit() {
    auto text = session_.emit();
    if (text.empty()) { print_ok("(empty module)"); return; }
    std::cout << "\n" << text << "\n";
}

void CLIEditor::cmd_diagram() {
    Emitter emitter;
    auto* g = session_.active_graph();
    if (!g) { print_ok("(no active graph)"); return; }
    std::cout << "\n" << emitter.emit_graph_diagram(*g) << "\n";
}

void CLIEditor::cmd_validate() {
    auto diags = session_.validate();
    if (diags.empty()) { print_ok("Validation passed: 0 issues"); return; }
    for (auto& d : diags) {
        std::cout << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                  << d.message << "\n";
    }
}

void CLIEditor::cmd_bake() {
    auto eg = session_.build_edit_graph();
    if (!eg) { print_error("No active graph to bake"); return; }
    auto rt = RuntimeGraph::bake(*eg);
    std::cout << "  RuntimeGraph '" << rt.name() << "'\n";
    if (!rt.domain_name().empty()) std::cout << "  Domain: " << rt.domain_name() << "\n";
    std::cout << "  Nodes: " << rt.node_count() << "\n";
    std::cout << "  Pins: " << rt.pins().size() << "\n";
    std::cout << "  Flow edges: " << rt.flow_edge_count() << "\n";
    std::cout << "  Data edges: " << rt.data_edge_count() << "\n";
}

void CLIEditor::cmd_types() {
    auto types = session_.available_types();
    if (types.empty()) { print_ok("(no types registered)"); return; }
    for (auto* t : types) {
        std::cout << "  " << t->type_name;
        if (!t->is_native) std::cout << " [graph]";
        std::cout << " (" << t->pins.size() << " pins)\n";
    }
}

void CLIEditor::cmd_schemas() {
    auto schemas = session_.env().schemas().all();
    if (schemas.empty()) { print_ok("(no schemas)"); return; }
    for (auto* s : schemas) {
        std::cout << "  " << s->name << "  fan-out:"
                  << (s->connection_policy.max_exec_fan_out == -1 ? "unlimited" : std::to_string(s->connection_policy.max_exec_fan_out))
                  << "  fan-in:" << (s->connection_policy.allow_exec_fan_in ? "yes" : "no")
                  << "  strict:" << (s->connection_policy.strict_type_match ? "yes" : "no") << "\n";
    }
}

void CLIEditor::cmd_load(const std::vector<std::string>& args) {
    if (args.size() < 2) { print_error("Usage: load <file.gs>"); return; }
    auto r = session_.load_file(args[1]);
    if (r.is_err()) print_error(r.error());
    else {
        print_ok("Loaded: " + args[1]);
        cmd_status();
    }
}

void CLIEditor::cmd_save(const std::vector<std::string>& args) {
    std::string path = args.size() >= 2 ? args[1] : "";
    auto r = session_.save_file(path);
    if (r.is_err()) print_error(r.error());
    else print_ok("Saved: " + session_.file_path());
}

void CLIEditor::cmd_apply_source_b64(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        print_error("Usage: apply_source_b64 <base64-source> [--env-hash <environment-hash>] [--resolve-imports] [--base-dir <directory>]");
        return;
    }
    auto source = base64_decode(args[1]);
    if (!source) {
        print_error("Invalid base64 source snapshot");
        return;
    }

    auto guard = parse_source_replay_guard(args, 2);
    if (guard.is_err()) {
        print_error(guard.error());
        return;
    }
    auto guard_result = verify_source_replay_guard(*source, session_.env(), guard.value());
    if (guard_result.is_err()) {
        print_error(guard_result.error());
        return;
    }

    auto r = session_.load_source(*source, source_replay_source_name(guard.value()));
    if (r.is_err()) print_error(r.error());
    else {
        current_block_.clear();
        print_ok("Applied source snapshot");
    }
}

void CLIEditor::cmd_apply_source_patch(const std::vector<std::string>& args) {
    if (args.size() < 5) {
        print_error("Usage: apply_source_patch <start-line> <start-column> <end-line> <end-column> [base64-replacement|-] [base-source-hash] [--env-hash <environment-hash>] [--resolve-imports] [--base-dir <directory>]");
        return;
    }

    auto start_line = parse_u32(args[1]);
    auto start_column = parse_u32(args[2]);
    auto end_line = parse_u32(args[3]);
    auto end_column = parse_u32(args[4]);
    if (!start_line || !start_column || !end_line || !end_column) {
        print_error("Invalid source patch range");
        return;
    }

    size_t option_start = 5;
    while (option_start < args.size() && args[option_start].rfind("--", 0) != 0) {
        ++option_start;
    }
    if (option_start > 7) {
        print_error("Usage: apply_source_patch <start-line> <start-column> <end-line> <end-column> [base64-replacement|-] [base-source-hash] [--env-hash <environment-hash>] [--resolve-imports] [--base-dir <directory>]");
        return;
    }

    std::string replacement;
    if (option_start >= 6) {
        if (args[5] != "-") {
            auto decoded = base64_decode(args[5]);
            if (!decoded) {
                print_error("Invalid base64 source patch replacement");
                return;
            }
            replacement = *decoded;
        }
    }

    const std::string source = session_.emit();
    if (option_start >= 7 && args[6] != source_hash(source)) {
        print_error("Source patch base hash mismatch");
        return;
    }

    SourceRange range;
    range.start.line = *start_line;
    range.start.column = *start_column;
    range.end.line = *end_line;
    range.end.column = *end_column;

    auto start = offset_for_location(source, range.start);
    auto end = offset_for_location(source, range.end);
    if (!start || !end || *start > *end) {
        print_error("Source patch range is outside current source");
        return;
    }

    std::string patched = source.substr(0, *start) + replacement + source.substr(*end);
    auto guard = parse_source_replay_guard(args, option_start);
    if (guard.is_err()) {
        print_error(guard.error());
        return;
    }
    auto guard_result = verify_source_replay_guard(patched, session_.env(), guard.value());
    if (guard_result.is_err()) {
        print_error(guard_result.error());
        return;
    }

    auto r = session_.load_source(patched, source_replay_source_name(guard.value()));
    if (r.is_err()) {
        print_error(r.error());
        return;
    }

    current_block_.clear();
    print_ok("Applied source patch");
}

void CLIEditor::cmd_apply_source_patches_b64(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        print_error("Usage: apply_source_patches_b64 <base64-patch-lines> [base-source-hash] [--env-hash <environment-hash>] [--resolve-imports] [--base-dir <directory>]");
        return;
    }

    auto patch_text = base64_decode(args[1]);
    if (!patch_text) {
        print_error("Invalid base64 source patch list");
        return;
    }

    size_t option_start = 2;
    std::optional<std::string> base_hash;
    if (option_start < args.size() && args[option_start].rfind("--", 0) != 0) {
        base_hash = args[option_start++];
    }

    const std::string source = session_.emit();
    if (base_hash && *base_hash != source_hash(source)) {
        print_error("Source patch base hash mismatch");
        return;
    }

    auto records = parse_source_patch_records(*patch_text);
    if (records.is_err()) {
        print_error(records.error());
        return;
    }

    auto patched = apply_source_patch_records(source, records.value());
    if (patched.is_err()) {
        print_error(patched.error());
        return;
    }

    auto guard = parse_source_replay_guard(args, option_start);
    if (guard.is_err()) {
        print_error(guard.error());
        return;
    }
    auto guard_result = verify_source_replay_guard(patched.value(), session_.env(), guard.value());
    if (guard_result.is_err()) {
        print_error(guard_result.error());
        return;
    }

    auto r = session_.load_source(patched.value(), source_replay_source_name(guard.value()));
    if (r.is_err()) {
        print_error(r.error());
        return;
    }

    current_block_.clear();
    print_ok("Applied source patches");
}

void CLIEditor::cmd_apply_source_identifier_rename(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        print_error("Usage: apply_source_identifier_rename <old> <new> [base-source-hash] [--env-hash <environment-hash>] [--resolve-imports] [--base-dir <directory>]");
        return;
    }

    size_t option_start = 3;
    std::optional<std::string> base_hash;
    if (option_start < args.size() && args[option_start].rfind("--", 0) != 0) {
        base_hash = args[option_start++];
    }

    const std::string source = session_.emit();
    if (base_hash && *base_hash != source_hash(source)) {
        print_error("Source patch base hash mismatch");
        return;
    }

    auto records = build_identifier_rename_patches(source, args[1], args[2]);
    if (records.is_err()) {
        print_error(records.error());
        return;
    }

    auto patched = apply_source_patch_records(source, records.value());
    if (patched.is_err()) {
        print_error(patched.error());
        return;
    }

    auto guard = parse_source_replay_guard(args, option_start);
    if (guard.is_err()) {
        print_error(guard.error());
        return;
    }
    auto guard_result = verify_source_replay_guard(patched.value(), session_.env(), guard.value());
    if (guard_result.is_err()) {
        print_error(guard_result.error());
        return;
    }

    auto r = session_.load_source(patched.value(), source_replay_source_name(guard.value()));
    if (r.is_err()) {
        print_error(r.error());
        return;
    }

    current_block_.clear();
    print_ok("Applied source identifier rename");
}

void CLIEditor::cmd_apply_source_param_rename(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        print_error("Usage: apply_source_param_rename <old> <new> [base-source-hash] [--env-hash <environment-hash>] [--resolve-imports] [--base-dir <directory>]");
        return;
    }

    const auto* active_graph = session_.active_graph();
    if (!active_graph) {
        print_error("No active graph");
        return;
    }
    const std::string active_graph_name = active_graph->name;

    size_t option_start = 3;
    std::optional<std::string> base_hash;
    if (option_start < args.size() && args[option_start].rfind("--", 0) != 0) {
        base_hash = args[option_start++];
    }

    const std::string source = session_.emit();
    if (base_hash && *base_hash != source_hash(source)) {
        print_error("Source patch base hash mismatch");
        return;
    }

    auto range_module = compile_source_for_ranges(source, session_.env(), session_.file_path());
    if (range_module.is_err()) {
        print_error(range_module.error());
        return;
    }
    const Graph* graph = find_graph_by_name(range_module.value(), active_graph_name);
    if (!graph) {
        print_error("Active graph not found in emitted source");
        return;
    }

    auto records = build_active_graph_param_rename_patches(source, range_module.value(), *graph, args[1], args[2]);
    if (records.is_err()) {
        print_error(records.error());
        return;
    }

    auto patched = apply_source_patch_records(source, records.value());
    if (patched.is_err()) {
        print_error(patched.error());
        return;
    }

    auto guard = parse_source_replay_guard(args, option_start);
    if (guard.is_err()) {
        print_error(guard.error());
        return;
    }
    auto guard_result = verify_source_replay_guard(patched.value(), session_.env(), guard.value());
    if (guard_result.is_err()) {
        print_error(guard_result.error());
        return;
    }

    auto r = session_.load_source(patched.value(), source_replay_source_name(guard.value()));
    if (r.is_err()) {
        print_error(r.error());
        return;
    }

    current_block_.clear();
    print_ok("Applied source parameter rename");
}

void CLIEditor::cmd_apply_source_event_rename(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        print_error("Usage: apply_source_event_rename <old> <new> [base-source-hash] [--env-hash <environment-hash>] [--resolve-imports] [--base-dir <directory>]");
        return;
    }

    const auto* active_graph = session_.active_graph();
    if (!active_graph) {
        print_error("No active graph");
        return;
    }
    const std::string active_graph_name = active_graph->name;

    size_t option_start = 3;
    std::optional<std::string> base_hash;
    if (option_start < args.size() && args[option_start].rfind("--", 0) != 0) {
        base_hash = args[option_start++];
    }

    const std::string source = session_.emit();
    if (base_hash && *base_hash != source_hash(source)) {
        print_error("Source patch base hash mismatch");
        return;
    }

    auto range_module = compile_source_for_ranges(source, session_.env(), session_.file_path());
    if (range_module.is_err()) {
        print_error(range_module.error());
        return;
    }
    const Graph* graph = find_graph_by_name(range_module.value(), active_graph_name);
    if (!graph) {
        print_error("Active graph not found in emitted source");
        return;
    }

    auto records = build_active_graph_event_rename_patches(source, range_module.value(), *graph, args[1], args[2]);
    if (records.is_err()) {
        print_error(records.error());
        return;
    }

    auto patched = apply_source_patch_records(source, records.value());
    if (patched.is_err()) {
        print_error(patched.error());
        return;
    }

    auto guard = parse_source_replay_guard(args, option_start);
    if (guard.is_err()) {
        print_error(guard.error());
        return;
    }
    auto guard_result = verify_source_replay_guard(patched.value(), session_.env(), guard.value());
    if (guard_result.is_err()) {
        print_error(guard_result.error());
        return;
    }

    auto r = session_.load_source(patched.value(), source_replay_source_name(guard.value()));
    if (r.is_err()) {
        print_error(r.error());
        return;
    }

    current_block_.clear();
    print_ok("Applied source event rename");
}

void CLIEditor::cmd_apply_source_function_rename(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        print_error("Usage: apply_source_function_rename <old> <new> [base-source-hash] [--env-hash <environment-hash>] [--resolve-imports] [--base-dir <directory>]");
        return;
    }

    const auto* active_graph = session_.active_graph();
    if (!active_graph) {
        print_error("No active graph");
        return;
    }
    const std::string active_graph_name = active_graph->name;

    size_t option_start = 3;
    std::optional<std::string> base_hash;
    if (option_start < args.size() && args[option_start].rfind("--", 0) != 0) {
        base_hash = args[option_start++];
    }

    const std::string source = session_.emit();
    if (base_hash && *base_hash != source_hash(source)) {
        print_error("Source patch base hash mismatch");
        return;
    }

    auto range_module = compile_source_for_ranges(source, session_.env(), session_.file_path());
    if (range_module.is_err()) {
        print_error(range_module.error());
        return;
    }
    const Graph* graph = find_graph_by_name(range_module.value(), active_graph_name);
    if (!graph) {
        print_error("Active graph not found in emitted source");
        return;
    }

    auto records = build_active_graph_function_rename_patches(source, range_module.value(), *graph, args[1], args[2]);
    if (records.is_err()) {
        print_error(records.error());
        return;
    }

    auto patched = apply_source_patch_records(source, records.value());
    if (patched.is_err()) {
        print_error(patched.error());
        return;
    }

    auto guard = parse_source_replay_guard(args, option_start);
    if (guard.is_err()) {
        print_error(guard.error());
        return;
    }
    auto guard_result = verify_source_replay_guard(patched.value(), session_.env(), guard.value());
    if (guard_result.is_err()) {
        print_error(guard_result.error());
        return;
    }

    auto r = session_.load_source(patched.value(), source_replay_source_name(guard.value()));
    if (r.is_err()) {
        print_error(r.error());
        return;
    }

    current_block_.clear();
    print_ok("Applied source function rename");
}

void CLIEditor::cmd_apply_source_node_rename(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        print_error("Usage: apply_source_node_rename <old> <new> [base-source-hash] [--env-hash <environment-hash>] [--resolve-imports] [--base-dir <directory>]");
        return;
    }

    const auto* active_graph = session_.active_graph();
    if (!active_graph) {
        print_error("No active graph");
        return;
    }
    const std::string active_graph_name = active_graph->name;

    size_t option_start = 3;
    std::optional<std::string> base_hash;
    if (option_start < args.size() && args[option_start].rfind("--", 0) != 0) {
        base_hash = args[option_start++];
    }

    const std::string source = session_.emit();
    if (base_hash && *base_hash != source_hash(source)) {
        print_error("Source patch base hash mismatch");
        return;
    }

    auto range_module = compile_source_for_ranges(source, session_.env(), session_.file_path());
    if (range_module.is_err()) {
        print_error(range_module.error());
        return;
    }
    const Graph* graph = find_graph_by_name(range_module.value(), active_graph_name);
    if (!graph) {
        print_error("Active graph not found in emitted source");
        return;
    }

    auto records = build_active_graph_node_rename_patches(source, *graph, args[1], args[2]);
    if (records.is_err()) {
        print_error(records.error());
        return;
    }

    auto patched = apply_source_patch_records(source, records.value());
    if (patched.is_err()) {
        print_error(patched.error());
        return;
    }

    auto guard = parse_source_replay_guard(args, option_start);
    if (guard.is_err()) {
        print_error(guard.error());
        return;
    }
    auto guard_result = verify_source_replay_guard(patched.value(), session_.env(), guard.value());
    if (guard_result.is_err()) {
        print_error(guard_result.error());
        return;
    }

    auto r = session_.load_source(patched.value(), source_replay_source_name(guard.value()));
    if (r.is_err()) {
        print_error(r.error());
        return;
    }

    current_block_.clear();
    print_ok("Applied source node rename");
}

void CLIEditor::cmd_apply_source_graph_rename(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        print_error("Usage: apply_source_graph_rename <old> <new> [base-source-hash] [--env-hash <environment-hash>] [--resolve-imports] [--base-dir <directory>]");
        return;
    }

    size_t option_start = 3;
    std::optional<std::string> base_hash;
    if (option_start < args.size() && args[option_start].rfind("--", 0) != 0) {
        base_hash = args[option_start++];
    }

    const std::string source = session_.emit();
    if (base_hash && *base_hash != source_hash(source)) {
        print_error("Source patch base hash mismatch");
        return;
    }

    auto range_module = compile_source_for_ranges(source, session_.env(), session_.file_path());
    if (range_module.is_err()) {
        print_error(range_module.error());
        return;
    }

    auto records = build_graph_rename_patches(source, range_module.value(), args[1], args[2]);
    if (records.is_err()) {
        print_error(records.error());
        return;
    }

    auto patched = apply_source_patch_records(source, records.value());
    if (patched.is_err()) {
        print_error(patched.error());
        return;
    }

    auto guard = parse_source_replay_guard(args, option_start);
    if (guard.is_err()) {
        print_error(guard.error());
        return;
    }
    auto guard_result = verify_source_replay_guard(patched.value(), session_.env(), guard.value());
    if (guard_result.is_err()) {
        print_error(guard_result.error());
        return;
    }

    auto r = session_.load_source(patched.value(), source_replay_source_name(guard.value()));
    if (r.is_err()) {
        print_error(r.error());
        return;
    }

    current_block_.clear();
    print_ok("Applied source graph rename");
}

void CLIEditor::cmd_apply_source_node_type_rename(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        print_error("Usage: apply_source_node_type_rename <old> <new> [base-source-hash] [--env-hash <environment-hash>] [--resolve-imports] [--base-dir <directory>]");
        return;
    }

    size_t option_start = 3;
    std::optional<std::string> base_hash;
    if (option_start < args.size() && args[option_start].rfind("--", 0) != 0) {
        base_hash = args[option_start++];
    }

    const std::string source = session_.emit();
    if (base_hash && *base_hash != source_hash(source)) {
        print_error("Source patch base hash mismatch");
        return;
    }

    auto range_module = compile_source_for_ranges(source, session_.env(), session_.file_path());
    if (range_module.is_err()) {
        print_error(range_module.error());
        return;
    }

    auto records = build_node_type_rename_patches(source, range_module.value(), session_.env(), args[1], args[2]);
    if (records.is_err()) {
        print_error(records.error());
        return;
    }

    auto patched = apply_source_patch_records(source, records.value());
    if (patched.is_err()) {
        print_error(patched.error());
        return;
    }

    auto guard = parse_source_replay_guard(args, option_start);
    if (guard.is_err()) {
        print_error(guard.error());
        return;
    }
    auto guard_result = verify_source_replay_guard(patched.value(), session_.env(), guard.value());
    if (guard_result.is_err()) {
        print_error(guard_result.error());
        return;
    }

    auto r = session_.load_source(patched.value(), source_replay_source_name(guard.value()));
    if (r.is_err()) {
        print_error(r.error());
        return;
    }

    current_block_.clear();
    print_ok("Applied source node type rename");
}

void CLIEditor::cmd_apply_source_node_pin_rename(const std::vector<std::string>& args) {
    if (args.size() < 4) {
        print_error("Usage: apply_source_node_pin_rename <node-type> <old-pin> <new-pin> [base-source-hash] [--env-hash <environment-hash>] [--resolve-imports] [--base-dir <directory>]");
        return;
    }

    const std::string& node_type = args[1];
    const std::string& old_name = args[2];
    const std::string& new_name = args[3];
    if (!is_identifier_text(node_type)) {
        print_error("Node type identifier is invalid");
        return;
    }
    if (!is_identifier_text(old_name)) {
        print_error("Old pin identifier is invalid");
        return;
    }
    if (!is_identifier_text(new_name)) {
        print_error("New pin identifier is invalid");
        return;
    }
    if (old_name == new_name) {
        print_error("Source node pin rename must change the name");
        return;
    }
    const NodeDefinition* node = session_.env().nodes().find(node_type);
    if (!node) {
        print_error("Node type '" + node_type + "' is not declared");
        return;
    }
    if (!node->find_pin(old_name)) {
        print_error("Pin '" + old_name + "' is not declared on node type '" + node_type + "'");
        return;
    }
    if (!node->find_pin(new_name)) {
        print_error("Pin '" + new_name + "' is not declared on node type '" + node_type + "'");
        return;
    }

    size_t option_start = 4;
    std::optional<std::string> base_hash;
    if (option_start < args.size() && args[option_start].rfind("--", 0) != 0) {
        base_hash = args[option_start++];
    }

    const std::string source = session_.emit();
    if (base_hash && *base_hash != source_hash(source)) {
        print_error("Source patch base hash mismatch");
        return;
    }

    auto range_module = compile_source_for_ranges(source, session_.env(), session_.file_path());
    if (range_module.is_err()) {
        print_error(range_module.error());
        return;
    }

    auto records = build_optional_node_pin_reference_rename_patches(
        source, range_module.value(), node_type, old_name, new_name);
    if (records.is_err()) {
        print_error(records.error());
        return;
    }
    if (records.value().empty()) {
        print_error("Pin '" + old_name + "' on node type '" + node_type + "' has no references in current source");
        return;
    }

    auto patched = apply_source_patch_records(source, records.value());
    if (patched.is_err()) {
        print_error(patched.error());
        return;
    }

    auto guard = parse_source_replay_guard(args, option_start);
    if (guard.is_err()) {
        print_error(guard.error());
        return;
    }
    auto guard_result = verify_source_replay_guard(patched.value(), session_.env(), guard.value());
    if (guard_result.is_err()) {
        print_error(guard_result.error());
        return;
    }

    auto r = session_.load_source(patched.value(), source_replay_source_name(guard.value()));
    if (r.is_err()) {
        print_error(r.error());
        return;
    }

    current_block_.clear();
    print_ok("Applied source node pin rename");
}

void CLIEditor::cmd_apply_source_schema_rename(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        print_error("Usage: apply_source_schema_rename <old> <new> [base-source-hash] [--env-hash <environment-hash>] [--resolve-imports] [--base-dir <directory>]");
        return;
    }

    const std::string& old_name = args[1];
    const std::string& new_name = args[2];
    if (!is_identifier_text(old_name)) {
        print_error("Old schema identifier is invalid");
        return;
    }
    if (!is_identifier_text(new_name)) {
        print_error("New schema identifier is invalid");
        return;
    }
    if (old_name == new_name) {
        print_error("Source schema rename must change the name");
        return;
    }
    if (!session_.env().schemas().find(new_name)) {
        print_error("Schema '" + new_name + "' is not declared");
        return;
    }

    size_t option_start = 3;
    std::optional<std::string> base_hash;
    if (option_start < args.size() && args[option_start].rfind("--", 0) != 0) {
        base_hash = args[option_start++];
    }

    const std::string source = session_.emit();
    if (base_hash && *base_hash != source_hash(source)) {
        print_error("Source patch base hash mismatch");
        return;
    }

    auto range_module = compile_source_for_ranges(source, session_.env(), session_.file_path());
    if (range_module.is_err()) {
        print_error(range_module.error());
        return;
    }

    auto records = build_optional_schema_reference_rename_patches(source, range_module.value(), old_name, new_name);
    if (records.is_err()) {
        print_error(records.error());
        return;
    }
    if (records.value().empty()) {
        print_error("Schema '" + old_name + "' has no references in current source");
        return;
    }

    auto patched = apply_source_patch_records(source, records.value());
    if (patched.is_err()) {
        print_error(patched.error());
        return;
    }

    auto guard = parse_source_replay_guard(args, option_start);
    if (guard.is_err()) {
        print_error(guard.error());
        return;
    }
    auto guard_result = verify_source_replay_guard(patched.value(), session_.env(), guard.value());
    if (guard_result.is_err()) {
        print_error(guard_result.error());
        return;
    }

    auto r = session_.load_source(patched.value(), source_replay_source_name(guard.value()));
    if (r.is_err()) {
        print_error(r.error());
        return;
    }

    current_block_.clear();
    print_ok("Applied source schema rename");
}

void CLIEditor::cmd_apply_source_type_rename(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        print_error("Usage: apply_source_type_rename <old> <new> [base-source-hash] [--env-hash <environment-hash>] [--resolve-imports] [--base-dir <directory>]");
        return;
    }

    const std::string& old_name = args[1];
    const std::string& new_name = args[2];
    if (!is_identifier_text(old_name)) {
        print_error("Old type identifier is invalid");
        return;
    }
    if (!is_identifier_text(new_name)) {
        print_error("New type identifier is invalid");
        return;
    }
    if (old_name == new_name) {
        print_error("Source type rename must change the name");
        return;
    }
    if (!session_.env().types().find(new_name)) {
        print_error("Type '" + new_name + "' is not declared");
        return;
    }

    size_t option_start = 3;
    std::optional<std::string> base_hash;
    if (option_start < args.size() && args[option_start].rfind("--", 0) != 0) {
        base_hash = args[option_start++];
    }

    const std::string source = session_.emit();
    if (base_hash && *base_hash != source_hash(source)) {
        print_error("Source patch base hash mismatch");
        return;
    }

    auto range_module = compile_source_for_ranges(source, session_.env(), session_.file_path());
    if (range_module.is_err()) {
        print_error(range_module.error());
        return;
    }

    auto records = build_optional_type_reference_rename_patches(source, range_module.value(), old_name, new_name);
    if (records.is_err()) {
        print_error(records.error());
        return;
    }
    if (records.value().empty()) {
        print_error("Type '" + old_name + "' has no references in current source");
        return;
    }

    auto patched = apply_source_patch_records(source, records.value());
    if (patched.is_err()) {
        print_error(patched.error());
        return;
    }

    auto guard = parse_source_replay_guard(args, option_start);
    if (guard.is_err()) {
        print_error(guard.error());
        return;
    }
    auto guard_result = verify_source_replay_guard(patched.value(), session_.env(), guard.value());
    if (guard_result.is_err()) {
        print_error(guard_result.error());
        return;
    }

    auto r = session_.load_source(patched.value(), source_replay_source_name(guard.value()));
    if (r.is_err()) {
        print_error(r.error());
        return;
    }

    current_block_.clear();
    print_ok("Applied source type rename");
}

void CLIEditor::cmd_apply_import_node_rename(const std::vector<std::string>& args) {
    if (args.size() < 4) {
        print_error("Usage: apply_import_node_rename <file.d.gs> <old> <new> [declaration-source-hash]");
        return;
    }

    if (args.size() > 5) {
        print_error("Usage: apply_import_node_rename <file.d.gs> <old> <new> [declaration-source-hash]");
        return;
    }

    const std::string& path = args[1];
    const std::string& old_name = args[2];
    const std::string& new_name = args[3];

    const NodeDefinition* old_node = session_.env().nodes().find(old_name);
    if (!old_node || !old_node->is_native) {
        print_error("Declaration node '" + old_name + "' is not loaded");
        return;
    }
    if (session_.env().nodes().find(new_name)) {
        print_error("Node type '" + new_name + "' already exists");
        return;
    }

    auto declaration_source = read_text_file(path);
    if (!declaration_source) {
        print_error("Cannot read declaration file: " + path);
        return;
    }
    if (args.size() == 5 && args[4] != source_hash(*declaration_source)) {
        print_error("Declaration source hash mismatch");
        return;
    }

    auto declaration_records = build_declaration_node_rename_patches(*declaration_source, old_name, new_name);
    if (declaration_records.is_err()) {
        print_error(declaration_records.error());
        return;
    }

    auto patched_declaration = apply_source_patch_records(*declaration_source, declaration_records.value());
    if (patched_declaration.is_err()) {
        print_error(patched_declaration.error());
        return;
    }

    Lexer declaration_lexer(patched_declaration.value());
    Parser declaration_parser(declaration_lexer.tokenize());
    auto patched_declaration_ast = declaration_parser.parse();
    if (patched_declaration_ast.is_err()) {
        print_error("Parse error in patched declaration source");
        return;
    }

    Environment dry_import_env;
    Compiler dry_import_compiler(dry_import_env);
    auto dry_import = dry_import_compiler.compile(*patched_declaration_ast.value(), path);
    if (dry_import.is_err()) {
        print_error("Compile error in patched declaration source: " + dry_import.error());
        return;
    }

    const std::string module_source = session_.emit();
    auto range_module = compile_source_for_ranges(module_source, session_.env(), session_.file_path());
    if (range_module.is_err()) {
        print_error(range_module.error());
        return;
    }

    auto module_records = build_optional_node_type_reference_rename_patches(
        module_source, range_module.value(), old_name, new_name);
    if (module_records.is_err()) {
        print_error(module_records.error());
        return;
    }

    std::string patched_module_source = module_source;
    if (!module_records.value().empty()) {
        auto patched_module = apply_source_patch_records(module_source, module_records.value());
        if (patched_module.is_err()) {
            print_error(patched_module.error());
            return;
        }
        patched_module_source = patched_module.value();
    }

    Environment replay_env = session_.env();
    Compiler replay_import_compiler(replay_env);
    auto replay_import = replay_import_compiler.compile(*patched_declaration_ast.value(), path);
    if (replay_import.is_err()) {
        print_error("Compile error in replay declaration environment: " + replay_import.error());
        return;
    }
    auto replay_module = compile_source_for_ranges(patched_module_source, replay_env, session_.file_path());
    if (replay_module.is_err()) {
        print_error(replay_module.error());
        return;
    }

    auto written = write_text_file(path, patched_declaration.value());
    if (written.is_err()) {
        print_error(written.error());
        return;
    }

    Compiler real_import_compiler(session_.env());
    auto real_import = real_import_compiler.compile(*patched_declaration_ast.value(), path);
    if (real_import.is_err()) {
        print_error("Compile error while applying declaration rename: " + real_import.error());
        return;
    }

    if (!module_records.value().empty()) {
        auto loaded = session_.load_source(patched_module_source, source_replay_source_name(SourceReplayGuard{}));
        if (loaded.is_err()) {
            print_error(loaded.error());
            return;
        }
    }
    session_.env().nodes().unregister_node(old_name);

    current_block_.clear();
    print_ok("Applied import node rename");
}

void CLIEditor::cmd_apply_import_node_pin_rename(const std::vector<std::string>& args) {
    if (args.size() < 5) {
        print_error("Usage: apply_import_node_pin_rename <file.d.gs> <node-type> <old-pin> <new-pin> [declaration-source-hash]");
        return;
    }

    if (args.size() > 6) {
        print_error("Usage: apply_import_node_pin_rename <file.d.gs> <node-type> <old-pin> <new-pin> [declaration-source-hash]");
        return;
    }

    const std::string& path = args[1];
    const std::string& node_type = args[2];
    const std::string& old_name = args[3];
    const std::string& new_name = args[4];

    const NodeDefinition* node = session_.env().nodes().find(node_type);
    if (!node || !node->is_native) {
        print_error("Declaration node '" + node_type + "' is not loaded");
        return;
    }
    if (!node->find_pin(old_name)) {
        print_error("Declaration pin '" + old_name + "' not found on node '" + node_type + "'");
        return;
    }
    if (node->find_pin(new_name)) {
        print_error("Declaration pin '" + new_name + "' already exists on node '" + node_type + "'");
        return;
    }

    auto declaration_source = read_text_file(path);
    if (!declaration_source) {
        print_error("Cannot read declaration file: " + path);
        return;
    }
    if (args.size() == 6 && args[5] != source_hash(*declaration_source)) {
        print_error("Declaration source hash mismatch");
        return;
    }

    auto declaration_records = build_declaration_node_pin_rename_patches(
        *declaration_source, node_type, old_name, new_name);
    if (declaration_records.is_err()) {
        print_error(declaration_records.error());
        return;
    }

    auto patched_declaration = apply_source_patch_records(*declaration_source, declaration_records.value());
    if (patched_declaration.is_err()) {
        print_error(patched_declaration.error());
        return;
    }

    Lexer declaration_lexer(patched_declaration.value());
    Parser declaration_parser(declaration_lexer.tokenize());
    auto patched_declaration_ast = declaration_parser.parse();
    if (patched_declaration_ast.is_err()) {
        print_error("Parse error in patched declaration source");
        return;
    }

    Environment dry_import_env;
    Compiler dry_import_compiler(dry_import_env);
    auto dry_import = dry_import_compiler.compile(*patched_declaration_ast.value(), path);
    if (dry_import.is_err()) {
        print_error("Compile error in patched declaration source: " + dry_import.error());
        return;
    }

    const std::string module_source = session_.emit();
    auto range_module = compile_source_for_ranges(module_source, session_.env(), session_.file_path());
    if (range_module.is_err()) {
        print_error(range_module.error());
        return;
    }

    auto module_records = build_optional_node_pin_reference_rename_patches(
        module_source, range_module.value(), node_type, old_name, new_name);
    if (module_records.is_err()) {
        print_error(module_records.error());
        return;
    }

    std::string patched_module_source = module_source;
    if (!module_records.value().empty()) {
        auto patched_module = apply_source_patch_records(module_source, module_records.value());
        if (patched_module.is_err()) {
            print_error(patched_module.error());
            return;
        }
        patched_module_source = patched_module.value();
    }

    Environment replay_env = session_.env();
    replay_env.nodes().unregister_node(node_type);
    Compiler replay_import_compiler(replay_env);
    auto replay_import = replay_import_compiler.compile(*patched_declaration_ast.value(), path);
    if (replay_import.is_err()) {
        print_error("Compile error in replay declaration environment: " + replay_import.error());
        return;
    }
    auto replay_module = compile_source_for_ranges(patched_module_source, replay_env, session_.file_path());
    if (replay_module.is_err()) {
        print_error(replay_module.error());
        return;
    }

    auto written = write_text_file(path, patched_declaration.value());
    if (written.is_err()) {
        print_error(written.error());
        return;
    }

    session_.env().nodes().unregister_node(node_type);
    Compiler real_import_compiler(session_.env());
    auto real_import = real_import_compiler.compile(*patched_declaration_ast.value(), path);
    if (real_import.is_err()) {
        print_error("Compile error while applying declaration pin rename: " + real_import.error());
        return;
    }

    if (!module_records.value().empty()) {
        auto loaded = session_.load_source(patched_module_source, source_replay_source_name(SourceReplayGuard{}));
        if (loaded.is_err()) {
            print_error(loaded.error());
            return;
        }
    }

    current_block_.clear();
    print_ok("Applied import node pin rename");
}

void CLIEditor::cmd_apply_import_schema_rename(const std::vector<std::string>& args) {
    if (args.size() < 4) {
        print_error("Usage: apply_import_schema_rename <file.d.gs> <old> <new> [declaration-source-hash]");
        return;
    }

    if (args.size() > 5) {
        print_error("Usage: apply_import_schema_rename <file.d.gs> <old> <new> [declaration-source-hash]");
        return;
    }

    const std::string& path = args[1];
    const std::string& old_name = args[2];
    const std::string& new_name = args[3];

    if (!session_.env().schemas().find(old_name)) {
        print_error("Declaration schema '" + old_name + "' is not loaded");
        return;
    }
    if (session_.env().schemas().find(new_name)) {
        print_error("Schema '" + new_name + "' already exists");
        return;
    }

    auto declaration_source = read_text_file(path);
    if (!declaration_source) {
        print_error("Cannot read declaration file: " + path);
        return;
    }
    if (args.size() == 5 && args[4] != source_hash(*declaration_source)) {
        print_error("Declaration source hash mismatch");
        return;
    }

    auto declaration_records = build_declaration_schema_rename_patches(*declaration_source, old_name, new_name);
    if (declaration_records.is_err()) {
        print_error(declaration_records.error());
        return;
    }

    auto patched_declaration = apply_source_patch_records(*declaration_source, declaration_records.value());
    if (patched_declaration.is_err()) {
        print_error(patched_declaration.error());
        return;
    }

    Lexer declaration_lexer(patched_declaration.value());
    Parser declaration_parser(declaration_lexer.tokenize());
    auto patched_declaration_ast = declaration_parser.parse();
    if (patched_declaration_ast.is_err()) {
        print_error("Parse error in patched declaration source");
        return;
    }

    Environment dry_import_env;
    Compiler dry_import_compiler(dry_import_env);
    auto dry_import = dry_import_compiler.compile(*patched_declaration_ast.value(), path);
    if (dry_import.is_err()) {
        print_error("Compile error in patched declaration source: " + dry_import.error());
        return;
    }

    const std::string module_source = session_.emit();
    auto range_module = compile_source_for_ranges(module_source, session_.env(), session_.file_path());
    if (range_module.is_err()) {
        print_error(range_module.error());
        return;
    }

    auto module_records = build_optional_schema_reference_rename_patches(
        module_source, range_module.value(), old_name, new_name);
    if (module_records.is_err()) {
        print_error(module_records.error());
        return;
    }

    std::string patched_module_source = module_source;
    if (!module_records.value().empty()) {
        auto patched_module = apply_source_patch_records(module_source, module_records.value());
        if (patched_module.is_err()) {
            print_error(patched_module.error());
            return;
        }
        patched_module_source = patched_module.value();
    }

    Environment replay_env = session_.env();
    replay_env.schemas().unregister_schema(old_name);
    Compiler replay_import_compiler(replay_env);
    auto replay_import = replay_import_compiler.compile(*patched_declaration_ast.value(), path);
    if (replay_import.is_err()) {
        print_error("Compile error in replay declaration environment: " + replay_import.error());
        return;
    }
    auto replay_module = compile_source_for_ranges(patched_module_source, replay_env, session_.file_path());
    if (replay_module.is_err()) {
        print_error(replay_module.error());
        return;
    }

    auto written = write_text_file(path, patched_declaration.value());
    if (written.is_err()) {
        print_error(written.error());
        return;
    }

    session_.env().schemas().unregister_schema(old_name);
    Compiler real_import_compiler(session_.env());
    auto real_import = real_import_compiler.compile(*patched_declaration_ast.value(), path);
    if (real_import.is_err()) {
        print_error("Compile error while applying declaration schema rename: " + real_import.error());
        return;
    }

    if (!module_records.value().empty()) {
        auto loaded = session_.load_source(patched_module_source, source_replay_source_name(SourceReplayGuard{}));
        if (loaded.is_err()) {
            print_error(loaded.error());
            return;
        }
    }

    current_block_.clear();
    print_ok("Applied import schema rename");
}

void CLIEditor::cmd_apply_import_schema_field_rename(const std::vector<std::string>& args) {
    if (args.size() < 5) {
        print_error("Usage: apply_import_schema_field_rename <file.d.gs> <schema> <old-field> <new-field> [declaration-source-hash]");
        return;
    }

    if (args.size() > 6) {
        print_error("Usage: apply_import_schema_field_rename <file.d.gs> <schema> <old-field> <new-field> [declaration-source-hash]");
        return;
    }

    const std::string& path = args[1];
    const std::string& schema_name = args[2];
    const std::string& old_name = args[3];
    const std::string& new_name = args[4];

    const GraphSchema* schema = session_.env().schemas().find(schema_name);
    if (!schema) {
        print_error("Declaration schema '" + schema_name + "' is not loaded");
        return;
    }
    auto field_exists = [&](const std::string& field_name) {
        return std::any_of(schema->fields.begin(), schema->fields.end(), [&](const GraphSchemaField& field) {
            return field.name == field_name;
        });
    };
    if (!field_exists(old_name)) {
        print_error("Declaration schema field '" + old_name + "' not found on schema '" + schema_name + "'");
        return;
    }
    if (field_exists(new_name)) {
        print_error("Declaration schema field '" + new_name + "' already exists on schema '" + schema_name + "'");
        return;
    }

    auto declaration_source = read_text_file(path);
    if (!declaration_source) {
        print_error("Cannot read declaration file: " + path);
        return;
    }
    if (args.size() == 6 && args[5] != source_hash(*declaration_source)) {
        print_error("Declaration source hash mismatch");
        return;
    }

    auto declaration_records = build_declaration_schema_field_rename_patches(
        *declaration_source, schema_name, old_name, new_name);
    if (declaration_records.is_err()) {
        print_error(declaration_records.error());
        return;
    }

    auto patched_declaration = apply_source_patch_records(*declaration_source, declaration_records.value());
    if (patched_declaration.is_err()) {
        print_error(patched_declaration.error());
        return;
    }

    Lexer declaration_lexer(patched_declaration.value());
    Parser declaration_parser(declaration_lexer.tokenize());
    auto patched_declaration_ast = declaration_parser.parse();
    if (patched_declaration_ast.is_err()) {
        print_error("Parse error in patched declaration source");
        return;
    }

    Environment dry_import_env;
    Compiler dry_import_compiler(dry_import_env);
    auto dry_import = dry_import_compiler.compile(*patched_declaration_ast.value(), path);
    if (dry_import.is_err()) {
        print_error("Compile error in patched declaration source: " + dry_import.error());
        return;
    }

    Environment replay_env = session_.env();
    replay_env.schemas().unregister_schema(schema_name);
    Compiler replay_import_compiler(replay_env);
    auto replay_import = replay_import_compiler.compile(*patched_declaration_ast.value(), path);
    if (replay_import.is_err()) {
        print_error("Compile error in replay declaration environment: " + replay_import.error());
        return;
    }

    auto written = write_text_file(path, patched_declaration.value());
    if (written.is_err()) {
        print_error(written.error());
        return;
    }

    session_.env().schemas().unregister_schema(schema_name);
    Compiler real_import_compiler(session_.env());
    auto real_import = real_import_compiler.compile(*patched_declaration_ast.value(), path);
    if (real_import.is_err()) {
        print_error("Compile error while applying declaration schema field rename: " + real_import.error());
        return;
    }

    current_block_.clear();
    print_ok("Applied import schema field rename");
}

void CLIEditor::cmd_apply_import_type_rename(const std::vector<std::string>& args) {
    if (args.size() < 4) {
        print_error("Usage: apply_import_type_rename <file.d.gs> <old> <new> [declaration-source-hash]");
        return;
    }

    if (args.size() > 5) {
        print_error("Usage: apply_import_type_rename <file.d.gs> <old> <new> [declaration-source-hash]");
        return;
    }

    const std::string& path = args[1];
    const std::string& old_name = args[2];
    const std::string& new_name = args[3];

    if (!session_.env().types().find(old_name)) {
        print_error("Declaration type '" + old_name + "' is not loaded");
        return;
    }
    if (session_.env().types().find(new_name)) {
        print_error("Type '" + new_name + "' already exists");
        return;
    }

    auto declaration_source = read_text_file(path);
    if (!declaration_source) {
        print_error("Cannot read declaration file: " + path);
        return;
    }
    if (args.size() == 5 && args[4] != source_hash(*declaration_source)) {
        print_error("Declaration source hash mismatch");
        return;
    }

    auto declaration_records = build_declaration_type_rename_patches(*declaration_source, old_name, new_name);
    if (declaration_records.is_err()) {
        print_error(declaration_records.error());
        return;
    }

    auto patched_declaration = apply_source_patch_records(*declaration_source, declaration_records.value());
    if (patched_declaration.is_err()) {
        print_error(patched_declaration.error());
        return;
    }

    Lexer declaration_lexer(patched_declaration.value());
    Parser declaration_parser(declaration_lexer.tokenize());
    auto patched_declaration_ast = declaration_parser.parse();
    if (patched_declaration_ast.is_err()) {
        print_error("Parse error in patched declaration source");
        return;
    }

    Environment dry_import_env;
    Compiler dry_import_compiler(dry_import_env);
    auto dry_import = dry_import_compiler.compile(*patched_declaration_ast.value(), path);
    if (dry_import.is_err()) {
        print_error("Compile error in patched declaration source: " + dry_import.error());
        return;
    }

    const std::string module_source = session_.emit();
    auto range_module = compile_source_for_ranges(module_source, session_.env(), session_.file_path());
    if (range_module.is_err()) {
        print_error(range_module.error());
        return;
    }

    auto module_records = build_optional_type_reference_rename_patches(
        module_source, range_module.value(), old_name, new_name);
    if (module_records.is_err()) {
        print_error(module_records.error());
        return;
    }

    std::string patched_module_source = module_source;
    if (!module_records.value().empty()) {
        auto patched_module = apply_source_patch_records(module_source, module_records.value());
        if (patched_module.is_err()) {
            print_error(patched_module.error());
            return;
        }
        patched_module_source = patched_module.value();
    }

    Environment replay_env = session_.env();
    replay_env.types().unregister_type(old_name);
    unregister_declared_native_nodes(replay_env, *patched_declaration_ast.value());
    Compiler replay_import_compiler(replay_env);
    auto replay_import = replay_import_compiler.compile(*patched_declaration_ast.value(), path);
    if (replay_import.is_err()) {
        print_error("Compile error in replay declaration environment: " + replay_import.error());
        return;
    }
    auto replay_module = compile_source_for_ranges(patched_module_source, replay_env, session_.file_path());
    if (replay_module.is_err()) {
        print_error(replay_module.error());
        return;
    }

    auto written = write_text_file(path, patched_declaration.value());
    if (written.is_err()) {
        print_error(written.error());
        return;
    }

    session_.env().types().unregister_type(old_name);
    unregister_declared_native_nodes(session_.env(), *patched_declaration_ast.value());
    Compiler real_import_compiler(session_.env());
    auto real_import = real_import_compiler.compile(*patched_declaration_ast.value(), path);
    if (real_import.is_err()) {
        print_error("Compile error while applying declaration type rename: " + real_import.error());
        return;
    }

    if (!module_records.value().empty()) {
        auto loaded = session_.load_source(patched_module_source, source_replay_source_name(SourceReplayGuard{}));
        if (loaded.is_err()) {
            print_error(loaded.error());
            return;
        }
    }

    current_block_.clear();
    print_ok("Applied import type rename");
}

void CLIEditor::cmd_apply_files_graph_rename(const std::vector<std::string>& args) {
    if (args.size() < 4) {
        print_error("Usage: apply_files_graph_rename <old> <new> <file.gs>... [--hash <file.gs> <hash>]...");
        return;
    }

    const std::string& old_name = args[1];
    const std::string& new_name = args[2];
    if (!is_identifier_text(old_name)) {
        print_error("Old graph identifier is invalid");
        return;
    }
    if (!is_identifier_text(new_name)) {
        print_error("New graph identifier is invalid");
        return;
    }
    if (old_name == new_name) {
        print_error("File graph rename must change the name");
        return;
    }

    std::vector<std::string> paths;
    std::unordered_map<std::string, std::string> expected_hashes;
    std::unordered_set<std::string> seen_paths;
    size_t i = 3;
    for (; i < args.size(); ++i) {
        if (args[i] == "--hash") break;
        if (args[i].rfind("--", 0) == 0) {
            print_error("Unknown file rename option: " + args[i]);
            return;
        }
        if (!seen_paths.insert(args[i]).second) {
            print_error("Duplicate file path: " + args[i]);
            return;
        }
        paths.push_back(args[i]);
    }
    if (paths.empty()) {
        print_error("Usage: apply_files_graph_rename <old> <new> <file.gs>... [--hash <file.gs> <hash>]...");
        return;
    }

    while (i < args.size()) {
        if (args[i] != "--hash" || i + 2 >= args.size()) {
            print_error("Usage: --hash <file.gs> <hash>");
            return;
        }
        const std::string& path = args[i + 1];
        const std::string& hash = args[i + 2];
        if (!seen_paths.count(path)) {
            print_error("Hash guard references unknown file: " + path);
            return;
        }
        expected_hashes[path] = hash;
        i += 3;
    }

    struct FilePlan {
        std::string path;
        std::string patched_source;
        size_t patch_count = 0;
    };

    std::vector<FilePlan> plans;
    size_t total_patches = 0;
    for (const auto& path : paths) {
        auto source = read_text_file(path);
        if (!source) {
            print_error("Cannot read source file: " + path);
            return;
        }
        auto hash_it = expected_hashes.find(path);
        if (hash_it != expected_hashes.end() && hash_it->second != source_hash(*source)) {
            print_error("Source file hash mismatch: " + path);
            return;
        }

        Environment range_env = session_.env();
        auto range_module = compile_source_for_ranges(*source, range_env, path);
        if (range_module.is_err()) {
            print_error("Compile error in source file '" + path + "': " + range_module.error());
            return;
        }

        auto records = build_optional_graph_reference_rename_patches(
            *source, range_module.value(), old_name, new_name);
        if (records.is_err()) {
            print_error(records.error());
            return;
        }
        if (records.value().empty()) {
            continue;
        }

        auto patched = apply_source_patch_records(*source, records.value());
        if (patched.is_err()) {
            print_error("Patch error in source file '" + path + "': " + patched.error());
            return;
        }

        Environment replay_env = session_.env();
        auto replay_module = compile_source_for_ranges(patched.value(), replay_env, path);
        if (replay_module.is_err()) {
            print_error("Compile error in patched source file '" + path + "': " + replay_module.error());
            return;
        }

        total_patches += records.value().size();
        plans.push_back({path, patched.value(), records.value().size()});
    }

    if (plans.empty()) {
        print_error("Graph '" + old_name + "' has no declarations or graph-node references in the provided files");
        return;
    }

    for (const auto& plan : plans) {
        auto written = write_text_file(plan.path, plan.patched_source);
        if (written.is_err()) {
            print_error(written.error());
            return;
        }
    }

    current_block_.clear();
    print_ok("Applied file graph rename to " + std::to_string(plans.size()) +
             " file(s), " + std::to_string(total_patches) + " reference(s)");
}

void CLIEditor::cmd_apply_files_graph_param_rename(const std::vector<std::string>& args) {
    if (args.size() < 5) {
        print_error("Usage: apply_files_graph_param_rename <graph> <old> <new> <file.gs>... [--hash <file.gs> <hash>]...");
        return;
    }

    const std::string& graph_name = args[1];
    const std::string& old_name = args[2];
    const std::string& new_name = args[3];
    if (!is_identifier_text(graph_name)) {
        print_error("Graph identifier is invalid");
        return;
    }
    if (!is_identifier_text(old_name)) {
        print_error("Old parameter identifier is invalid");
        return;
    }
    if (!is_identifier_text(new_name)) {
        print_error("New parameter identifier is invalid");
        return;
    }
    if (old_name == new_name) {
        print_error("File graph parameter rename must change the name");
        return;
    }

    std::vector<std::string> paths;
    std::unordered_map<std::string, std::string> expected_hashes;
    std::unordered_set<std::string> seen_paths;
    size_t i = 4;
    for (; i < args.size(); ++i) {
        if (args[i] == "--hash") break;
        if (args[i].rfind("--", 0) == 0) {
            print_error("Unknown file rename option: " + args[i]);
            return;
        }
        if (!seen_paths.insert(args[i]).second) {
            print_error("Duplicate file path: " + args[i]);
            return;
        }
        paths.push_back(args[i]);
    }
    if (paths.empty()) {
        print_error("Usage: apply_files_graph_param_rename <graph> <old> <new> <file.gs>... [--hash <file.gs> <hash>]...");
        return;
    }

    while (i < args.size()) {
        if (args[i] != "--hash" || i + 2 >= args.size()) {
            print_error("Usage: --hash <file.gs> <hash>");
            return;
        }
        const std::string& path = args[i + 1];
        const std::string& hash = args[i + 2];
        if (!seen_paths.count(path)) {
            print_error("Hash guard references unknown file: " + path);
            return;
        }
        expected_hashes[path] = hash;
        i += 3;
    }

    struct FilePlan {
        std::string path;
        std::string patched_source;
        size_t patch_count = 0;
    };

    std::vector<FilePlan> plans;
    size_t total_patches = 0;
    for (const auto& path : paths) {
        auto source = read_text_file(path);
        if (!source) {
            print_error("Cannot read source file: " + path);
            return;
        }
        auto hash_it = expected_hashes.find(path);
        if (hash_it != expected_hashes.end() && hash_it->second != source_hash(*source)) {
            print_error("Source file hash mismatch: " + path);
            return;
        }

        Environment range_env = session_.env();
        auto range_module = compile_source_for_ranges(*source, range_env, path);
        if (range_module.is_err()) {
            print_error("Compile error in source file '" + path + "': " + range_module.error());
            return;
        }

        auto records = build_optional_graph_param_reference_rename_patches(
            *source, range_module.value(), graph_name, old_name, new_name);
        if (records.is_err()) {
            print_error(records.error());
            return;
        }
        if (records.value().empty()) {
            continue;
        }

        auto patched = apply_source_patch_records(*source, records.value());
        if (patched.is_err()) {
            print_error("Patch error in source file '" + path + "': " + patched.error());
            return;
        }

        Environment replay_env = session_.env();
        auto replay_module = compile_source_for_ranges(patched.value(), replay_env, path);
        if (replay_module.is_err()) {
            print_error("Compile error in patched source file '" + path + "': " + replay_module.error());
            return;
        }

        total_patches += records.value().size();
        plans.push_back({path, patched.value(), records.value().size()});
    }

    if (plans.empty()) {
        print_error("Graph parameter '" + old_name + "' on graph '" + graph_name + "' has no declarations or references in the provided files");
        return;
    }

    for (const auto& plan : plans) {
        auto written = write_text_file(plan.path, plan.patched_source);
        if (written.is_err()) {
            print_error(written.error());
            return;
        }
    }

    current_block_.clear();
    print_ok("Applied file graph parameter rename to " + std::to_string(plans.size()) +
             " file(s), " + std::to_string(total_patches) + " reference(s)");
}

void CLIEditor::cmd_apply_files_graph_event_rename(const std::vector<std::string>& args) {
    if (args.size() < 5) {
        print_error("Usage: apply_files_graph_event_rename <graph> <old> <new> <file.gs>... [--hash <file.gs> <hash>]...");
        return;
    }

    const std::string& graph_name = args[1];
    const std::string& old_name = args[2];
    const std::string& new_name = args[3];
    if (!is_identifier_text(graph_name)) {
        print_error("Graph identifier is invalid");
        return;
    }
    if (!is_identifier_text(old_name)) {
        print_error("Old event identifier is invalid");
        return;
    }
    if (!is_identifier_text(new_name)) {
        print_error("New event identifier is invalid");
        return;
    }
    if (old_name == new_name) {
        print_error("File graph event rename must change the name");
        return;
    }

    std::vector<std::string> paths;
    std::unordered_map<std::string, std::string> expected_hashes;
    std::unordered_set<std::string> seen_paths;
    size_t i = 4;
    for (; i < args.size(); ++i) {
        if (args[i] == "--hash") break;
        if (args[i].rfind("--", 0) == 0) {
            print_error("Unknown file rename option: " + args[i]);
            return;
        }
        if (!seen_paths.insert(args[i]).second) {
            print_error("Duplicate file path: " + args[i]);
            return;
        }
        paths.push_back(args[i]);
    }
    if (paths.empty()) {
        print_error("Usage: apply_files_graph_event_rename <graph> <old> <new> <file.gs>... [--hash <file.gs> <hash>]...");
        return;
    }

    while (i < args.size()) {
        if (args[i] != "--hash" || i + 2 >= args.size()) {
            print_error("Usage: --hash <file.gs> <hash>");
            return;
        }
        const std::string& path = args[i + 1];
        const std::string& hash = args[i + 2];
        if (!seen_paths.count(path)) {
            print_error("Hash guard references unknown file: " + path);
            return;
        }
        expected_hashes[path] = hash;
        i += 3;
    }

    struct FilePlan {
        std::string path;
        std::string patched_source;
        size_t patch_count = 0;
    };

    std::vector<FilePlan> plans;
    size_t total_patches = 0;
    for (const auto& path : paths) {
        auto source = read_text_file(path);
        if (!source) {
            print_error("Cannot read source file: " + path);
            return;
        }
        auto hash_it = expected_hashes.find(path);
        if (hash_it != expected_hashes.end() && hash_it->second != source_hash(*source)) {
            print_error("Source file hash mismatch: " + path);
            return;
        }

        Environment range_env = session_.env();
        auto range_module = compile_source_for_ranges(*source, range_env, path);
        if (range_module.is_err()) {
            print_error("Compile error in source file '" + path + "': " + range_module.error());
            return;
        }

        auto records = build_optional_graph_event_reference_rename_patches(
            *source, range_module.value(), graph_name, old_name, new_name);
        if (records.is_err()) {
            print_error(records.error());
            return;
        }
        if (records.value().empty()) {
            continue;
        }

        auto patched = apply_source_patch_records(*source, records.value());
        if (patched.is_err()) {
            print_error("Patch error in source file '" + path + "': " + patched.error());
            return;
        }

        Environment replay_env = session_.env();
        auto replay_module = compile_source_for_ranges(patched.value(), replay_env, path);
        if (replay_module.is_err()) {
            print_error("Compile error in patched source file '" + path + "': " + replay_module.error());
            return;
        }

        total_patches += records.value().size();
        plans.push_back({path, patched.value(), records.value().size()});
    }

    if (plans.empty()) {
        print_error("Graph event '" + old_name + "' on graph '" + graph_name + "' has no declarations or references in the provided files");
        return;
    }

    for (const auto& plan : plans) {
        auto written = write_text_file(plan.path, plan.patched_source);
        if (written.is_err()) {
            print_error(written.error());
            return;
        }
    }

    current_block_.clear();
    print_ok("Applied file graph event rename to " + std::to_string(plans.size()) +
             " file(s), " + std::to_string(total_patches) + " reference(s)");
}

void CLIEditor::cmd_apply_files_graph_function_rename(const std::vector<std::string>& args) {
    if (args.size() < 5) {
        print_error("Usage: apply_files_graph_function_rename <graph> <old> <new> <file.gs>... [--hash <file.gs> <hash>]...");
        return;
    }

    const std::string& graph_name = args[1];
    const std::string& old_name = args[2];
    const std::string& new_name = args[3];
    if (!is_identifier_text(graph_name)) {
        print_error("Graph identifier is invalid");
        return;
    }
    if (!is_identifier_text(old_name)) {
        print_error("Old function identifier is invalid");
        return;
    }
    if (!is_identifier_text(new_name)) {
        print_error("New function identifier is invalid");
        return;
    }
    if (old_name == new_name) {
        print_error("File graph function rename must change the name");
        return;
    }

    std::vector<std::string> paths;
    std::unordered_map<std::string, std::string> expected_hashes;
    std::unordered_set<std::string> seen_paths;
    size_t i = 4;
    for (; i < args.size(); ++i) {
        if (args[i] == "--hash") break;
        if (args[i].rfind("--", 0) == 0) {
            print_error("Unknown file rename option: " + args[i]);
            return;
        }
        if (!seen_paths.insert(args[i]).second) {
            print_error("Duplicate file path: " + args[i]);
            return;
        }
        paths.push_back(args[i]);
    }
    if (paths.empty()) {
        print_error("Usage: apply_files_graph_function_rename <graph> <old> <new> <file.gs>... [--hash <file.gs> <hash>]...");
        return;
    }

    while (i < args.size()) {
        if (args[i] != "--hash" || i + 2 >= args.size()) {
            print_error("Usage: --hash <file.gs> <hash>");
            return;
        }
        const std::string& path = args[i + 1];
        const std::string& hash = args[i + 2];
        if (!seen_paths.count(path)) {
            print_error("Hash guard references unknown file: " + path);
            return;
        }
        expected_hashes[path] = hash;
        i += 3;
    }

    struct FilePlan {
        std::string path;
        std::string patched_source;
        size_t patch_count = 0;
    };

    std::vector<FilePlan> plans;
    size_t total_patches = 0;
    for (const auto& path : paths) {
        auto source = read_text_file(path);
        if (!source) {
            print_error("Cannot read source file: " + path);
            return;
        }
        auto hash_it = expected_hashes.find(path);
        if (hash_it != expected_hashes.end() && hash_it->second != source_hash(*source)) {
            print_error("Source file hash mismatch: " + path);
            return;
        }

        Environment range_env = session_.env();
        auto range_module = compile_source_for_ranges(*source, range_env, path);
        if (range_module.is_err()) {
            print_error("Compile error in source file '" + path + "': " + range_module.error());
            return;
        }

        auto records = build_optional_graph_function_declaration_rename_patches(
            *source, range_module.value(), graph_name, old_name, new_name);
        if (records.is_err()) {
            print_error(records.error());
            return;
        }
        if (records.value().empty()) {
            continue;
        }

        auto patched = apply_source_patch_records(*source, records.value());
        if (patched.is_err()) {
            print_error("Patch error in source file '" + path + "': " + patched.error());
            return;
        }

        Environment replay_env = session_.env();
        auto replay_module = compile_source_for_ranges(patched.value(), replay_env, path);
        if (replay_module.is_err()) {
            print_error("Compile error in patched source file '" + path + "': " + replay_module.error());
            return;
        }

        total_patches += records.value().size();
        plans.push_back({path, patched.value(), records.value().size()});
    }

    if (plans.empty()) {
        print_error("Graph function '" + old_name + "' on graph '" + graph_name + "' has no declarations in the provided files");
        return;
    }

    for (const auto& plan : plans) {
        auto written = write_text_file(plan.path, plan.patched_source);
        if (written.is_err()) {
            print_error(written.error());
            return;
        }
    }

    current_block_.clear();
    print_ok("Applied file graph function rename to " + std::to_string(plans.size()) +
             " file(s), " + std::to_string(total_patches) + " patch(es)");
}

void CLIEditor::cmd_apply_files_node_type_rename(const std::vector<std::string>& args) {
    if (args.size() < 4) {
        print_error("Usage: apply_files_node_type_rename <old> <new> <file.gs>... [--hash <file.gs> <hash>]...");
        return;
    }

    const std::string& old_name = args[1];
    const std::string& new_name = args[2];
    if (!is_identifier_text(old_name)) {
        print_error("Old node type identifier is invalid");
        return;
    }
    if (!is_identifier_text(new_name)) {
        print_error("New node type identifier is invalid");
        return;
    }
    if (old_name == new_name) {
        print_error("File node type rename must change the name");
        return;
    }
    if (!session_.env().nodes().find(new_name)) {
        print_error("Node type '" + new_name + "' is not declared");
        return;
    }

    std::vector<std::string> paths;
    std::unordered_map<std::string, std::string> expected_hashes;
    std::unordered_set<std::string> seen_paths;
    size_t i = 3;
    for (; i < args.size(); ++i) {
        if (args[i] == "--hash") break;
        if (args[i].rfind("--", 0) == 0) {
            print_error("Unknown file rename option: " + args[i]);
            return;
        }
        if (!seen_paths.insert(args[i]).second) {
            print_error("Duplicate file path: " + args[i]);
            return;
        }
        paths.push_back(args[i]);
    }
    if (paths.empty()) {
        print_error("Usage: apply_files_node_type_rename <old> <new> <file.gs>... [--hash <file.gs> <hash>]...");
        return;
    }

    while (i < args.size()) {
        if (args[i] != "--hash" || i + 2 >= args.size()) {
            print_error("Usage: --hash <file.gs> <hash>");
            return;
        }
        const std::string& path = args[i + 1];
        const std::string& hash = args[i + 2];
        if (!seen_paths.count(path)) {
            print_error("Hash guard references unknown file: " + path);
            return;
        }
        expected_hashes[path] = hash;
        i += 3;
    }

    struct FilePlan {
        std::string path;
        std::string patched_source;
        size_t patch_count = 0;
    };

    std::vector<FilePlan> plans;
    size_t total_patches = 0;
    for (const auto& path : paths) {
        auto source = read_text_file(path);
        if (!source) {
            print_error("Cannot read source file: " + path);
            return;
        }
        auto hash_it = expected_hashes.find(path);
        if (hash_it != expected_hashes.end() && hash_it->second != source_hash(*source)) {
            print_error("Source file hash mismatch: " + path);
            return;
        }

        Environment range_env = session_.env();
        auto range_module = compile_source_for_ranges(*source, range_env, path);
        if (range_module.is_err()) {
            print_error("Compile error in source file '" + path + "': " + range_module.error());
            return;
        }

        auto records = build_optional_node_type_reference_rename_patches(
            *source, range_module.value(), old_name, new_name);
        if (records.is_err()) {
            print_error(records.error());
            return;
        }
        if (records.value().empty()) {
            continue;
        }

        auto patched = apply_source_patch_records(*source, records.value());
        if (patched.is_err()) {
            print_error("Patch error in source file '" + path + "': " + patched.error());
            return;
        }

        Environment replay_env = session_.env();
        auto replay_module = compile_source_for_ranges(patched.value(), replay_env, path);
        if (replay_module.is_err()) {
            print_error("Compile error in patched source file '" + path + "': " + replay_module.error());
            return;
        }

        total_patches += records.value().size();
        plans.push_back({path, patched.value(), records.value().size()});
    }

    if (plans.empty()) {
        print_error("Node type '" + old_name + "' has no references in the provided files");
        return;
    }

    for (const auto& plan : plans) {
        auto written = write_text_file(plan.path, plan.patched_source);
        if (written.is_err()) {
            print_error(written.error());
            return;
        }
    }

    current_block_.clear();
    print_ok("Applied file node type rename to " + std::to_string(plans.size()) +
             " file(s), " + std::to_string(total_patches) + " reference(s)");
}

void CLIEditor::cmd_apply_files_node_pin_rename(const std::vector<std::string>& args) {
    if (args.size() < 5) {
        print_error("Usage: apply_files_node_pin_rename <node-type> <old-pin> <new-pin> <file.gs>... [--hash <file.gs> <hash>]...");
        return;
    }

    const std::string& node_type = args[1];
    const std::string& old_name = args[2];
    const std::string& new_name = args[3];
    if (!is_identifier_text(node_type)) {
        print_error("Node type identifier is invalid");
        return;
    }
    if (!is_identifier_text(old_name)) {
        print_error("Old pin identifier is invalid");
        return;
    }
    if (!is_identifier_text(new_name)) {
        print_error("New pin identifier is invalid");
        return;
    }
    if (old_name == new_name) {
        print_error("File node pin rename must change the name");
        return;
    }
    const NodeDefinition* node = session_.env().nodes().find(node_type);
    if (!node) {
        print_error("Node type '" + node_type + "' is not declared");
        return;
    }
    if (!node->find_pin(old_name)) {
        print_error("Pin '" + old_name + "' is not declared on node type '" + node_type + "'");
        return;
    }
    if (!node->find_pin(new_name)) {
        print_error("Pin '" + new_name + "' is not declared on node type '" + node_type + "'");
        return;
    }

    std::vector<std::string> paths;
    std::unordered_map<std::string, std::string> expected_hashes;
    std::unordered_set<std::string> seen_paths;
    size_t i = 4;
    for (; i < args.size(); ++i) {
        if (args[i] == "--hash") break;
        if (args[i].rfind("--", 0) == 0) {
            print_error("Unknown file rename option: " + args[i]);
            return;
        }
        if (!seen_paths.insert(args[i]).second) {
            print_error("Duplicate file path: " + args[i]);
            return;
        }
        paths.push_back(args[i]);
    }
    if (paths.empty()) {
        print_error("Usage: apply_files_node_pin_rename <node-type> <old-pin> <new-pin> <file.gs>... [--hash <file.gs> <hash>]...");
        return;
    }

    while (i < args.size()) {
        if (args[i] != "--hash" || i + 2 >= args.size()) {
            print_error("Usage: --hash <file.gs> <hash>");
            return;
        }
        const std::string& path = args[i + 1];
        const std::string& hash = args[i + 2];
        if (!seen_paths.count(path)) {
            print_error("Hash guard references unknown file: " + path);
            return;
        }
        expected_hashes[path] = hash;
        i += 3;
    }

    struct FilePlan {
        std::string path;
        std::string patched_source;
        size_t patch_count = 0;
    };

    std::vector<FilePlan> plans;
    size_t total_patches = 0;
    for (const auto& path : paths) {
        auto source = read_text_file(path);
        if (!source) {
            print_error("Cannot read source file: " + path);
            return;
        }
        auto hash_it = expected_hashes.find(path);
        if (hash_it != expected_hashes.end() && hash_it->second != source_hash(*source)) {
            print_error("Source file hash mismatch: " + path);
            return;
        }

        Environment range_env = session_.env();
        auto range_module = compile_source_for_ranges(*source, range_env, path);
        if (range_module.is_err()) {
            print_error("Compile error in source file '" + path + "': " + range_module.error());
            return;
        }

        auto records = build_optional_node_pin_reference_rename_patches(
            *source, range_module.value(), node_type, old_name, new_name);
        if (records.is_err()) {
            print_error(records.error());
            return;
        }
        if (records.value().empty()) {
            continue;
        }

        auto patched = apply_source_patch_records(*source, records.value());
        if (patched.is_err()) {
            print_error("Patch error in source file '" + path + "': " + patched.error());
            return;
        }

        Environment replay_env = session_.env();
        auto replay_module = compile_source_for_ranges(patched.value(), replay_env, path);
        if (replay_module.is_err()) {
            print_error("Compile error in patched source file '" + path + "': " + replay_module.error());
            return;
        }

        total_patches += records.value().size();
        plans.push_back({path, patched.value(), records.value().size()});
    }

    if (plans.empty()) {
        print_error("Pin '" + old_name + "' on node type '" + node_type + "' has no references in the provided files");
        return;
    }

    for (const auto& plan : plans) {
        auto written = write_text_file(plan.path, plan.patched_source);
        if (written.is_err()) {
            print_error(written.error());
            return;
        }
    }

    current_block_.clear();
    print_ok("Applied file node pin rename to " + std::to_string(plans.size()) +
             " file(s), " + std::to_string(total_patches) + " reference(s)");
}

void CLIEditor::cmd_apply_files_schema_rename(const std::vector<std::string>& args) {
    if (args.size() < 4) {
        print_error("Usage: apply_files_schema_rename <old> <new> <file.gs>... [--hash <file.gs> <hash>]...");
        return;
    }

    const std::string& old_name = args[1];
    const std::string& new_name = args[2];
    if (!is_identifier_text(old_name)) {
        print_error("Old schema identifier is invalid");
        return;
    }
    if (!is_identifier_text(new_name)) {
        print_error("New schema identifier is invalid");
        return;
    }
    if (old_name == new_name) {
        print_error("File schema rename must change the name");
        return;
    }
    if (!session_.env().schemas().find(new_name)) {
        print_error("Schema '" + new_name + "' is not declared");
        return;
    }

    std::vector<std::string> paths;
    std::unordered_map<std::string, std::string> expected_hashes;
    std::unordered_set<std::string> seen_paths;
    size_t i = 3;
    for (; i < args.size(); ++i) {
        if (args[i] == "--hash") break;
        if (args[i].rfind("--", 0) == 0) {
            print_error("Unknown file rename option: " + args[i]);
            return;
        }
        if (!seen_paths.insert(args[i]).second) {
            print_error("Duplicate file path: " + args[i]);
            return;
        }
        paths.push_back(args[i]);
    }
    if (paths.empty()) {
        print_error("Usage: apply_files_schema_rename <old> <new> <file.gs>... [--hash <file.gs> <hash>]...");
        return;
    }

    while (i < args.size()) {
        if (args[i] != "--hash" || i + 2 >= args.size()) {
            print_error("Usage: --hash <file.gs> <hash>");
            return;
        }
        const std::string& path = args[i + 1];
        const std::string& hash = args[i + 2];
        if (!seen_paths.count(path)) {
            print_error("Hash guard references unknown file: " + path);
            return;
        }
        expected_hashes[path] = hash;
        i += 3;
    }

    struct FilePlan {
        std::string path;
        std::string patched_source;
        size_t patch_count = 0;
    };

    std::vector<FilePlan> plans;
    size_t total_patches = 0;
    for (const auto& path : paths) {
        auto source = read_text_file(path);
        if (!source) {
            print_error("Cannot read source file: " + path);
            return;
        }
        auto hash_it = expected_hashes.find(path);
        if (hash_it != expected_hashes.end() && hash_it->second != source_hash(*source)) {
            print_error("Source file hash mismatch: " + path);
            return;
        }

        Environment range_env = session_.env();
        auto range_module = compile_source_for_ranges(*source, range_env, path);
        if (range_module.is_err()) {
            print_error("Compile error in source file '" + path + "': " + range_module.error());
            return;
        }

        auto records = build_optional_schema_reference_rename_patches(
            *source, range_module.value(), old_name, new_name);
        if (records.is_err()) {
            print_error(records.error());
            return;
        }
        if (records.value().empty()) {
            continue;
        }

        auto patched = apply_source_patch_records(*source, records.value());
        if (patched.is_err()) {
            print_error("Patch error in source file '" + path + "': " + patched.error());
            return;
        }

        Environment replay_env = session_.env();
        auto replay_module = compile_source_for_ranges(patched.value(), replay_env, path);
        if (replay_module.is_err()) {
            print_error("Compile error in patched source file '" + path + "': " + replay_module.error());
            return;
        }

        total_patches += records.value().size();
        plans.push_back({path, patched.value(), records.value().size()});
    }

    if (plans.empty()) {
        print_error("Schema '" + old_name + "' has no references in the provided files");
        return;
    }

    for (const auto& plan : plans) {
        auto written = write_text_file(plan.path, plan.patched_source);
        if (written.is_err()) {
            print_error(written.error());
            return;
        }
    }

    current_block_.clear();
    print_ok("Applied file schema rename to " + std::to_string(plans.size()) +
             " file(s), " + std::to_string(total_patches) + " reference(s)");
}

void CLIEditor::cmd_apply_files_type_rename(const std::vector<std::string>& args) {
    if (args.size() < 4) {
        print_error("Usage: apply_files_type_rename <old> <new> <file.gs>... [--hash <file.gs> <hash>]...");
        return;
    }

    const std::string& old_name = args[1];
    const std::string& new_name = args[2];
    if (!is_identifier_text(old_name)) {
        print_error("Old type identifier is invalid");
        return;
    }
    if (!is_identifier_text(new_name)) {
        print_error("New type identifier is invalid");
        return;
    }
    if (old_name == new_name) {
        print_error("File type rename must change the name");
        return;
    }
    if (!session_.env().types().find(new_name)) {
        print_error("Type '" + new_name + "' is not declared");
        return;
    }

    std::vector<std::string> paths;
    std::unordered_map<std::string, std::string> expected_hashes;
    std::unordered_set<std::string> seen_paths;
    size_t i = 3;
    for (; i < args.size(); ++i) {
        if (args[i] == "--hash") break;
        if (args[i].rfind("--", 0) == 0) {
            print_error("Unknown file rename option: " + args[i]);
            return;
        }
        if (!seen_paths.insert(args[i]).second) {
            print_error("Duplicate file path: " + args[i]);
            return;
        }
        paths.push_back(args[i]);
    }
    if (paths.empty()) {
        print_error("Usage: apply_files_type_rename <old> <new> <file.gs>... [--hash <file.gs> <hash>]...");
        return;
    }

    while (i < args.size()) {
        if (args[i] != "--hash" || i + 2 >= args.size()) {
            print_error("Usage: --hash <file.gs> <hash>");
            return;
        }
        const std::string& path = args[i + 1];
        const std::string& hash = args[i + 2];
        if (!seen_paths.count(path)) {
            print_error("Hash guard references unknown file: " + path);
            return;
        }
        expected_hashes[path] = hash;
        i += 3;
    }

    struct FilePlan {
        std::string path;
        std::string patched_source;
        size_t patch_count = 0;
    };

    std::vector<FilePlan> plans;
    size_t total_patches = 0;
    for (const auto& path : paths) {
        auto source = read_text_file(path);
        if (!source) {
            print_error("Cannot read source file: " + path);
            return;
        }
        auto hash_it = expected_hashes.find(path);
        if (hash_it != expected_hashes.end() && hash_it->second != source_hash(*source)) {
            print_error("Source file hash mismatch: " + path);
            return;
        }

        Environment range_env = session_.env();
        auto range_module = compile_source_for_ranges(*source, range_env, path);
        if (range_module.is_err()) {
            print_error("Compile error in source file '" + path + "': " + range_module.error());
            return;
        }

        auto records = build_optional_type_reference_rename_patches(
            *source, range_module.value(), old_name, new_name);
        if (records.is_err()) {
            print_error(records.error());
            return;
        }
        if (records.value().empty()) {
            continue;
        }

        auto patched = apply_source_patch_records(*source, records.value());
        if (patched.is_err()) {
            print_error("Patch error in source file '" + path + "': " + patched.error());
            return;
        }

        Environment replay_env = session_.env();
        auto replay_module = compile_source_for_ranges(patched.value(), replay_env, path);
        if (replay_module.is_err()) {
            print_error("Compile error in patched source file '" + path + "': " + replay_module.error());
            return;
        }

        total_patches += records.value().size();
        plans.push_back({path, patched.value(), records.value().size()});
    }

    if (plans.empty()) {
        print_error("Type '" + old_name + "' has no references in the provided files");
        return;
    }

    for (const auto& plan : plans) {
        auto written = write_text_file(plan.path, plan.patched_source);
        if (written.is_err()) {
            print_error(written.error());
            return;
        }
    }

    current_block_.clear();
    print_ok("Applied file type rename to " + std::to_string(plans.size()) +
             " file(s), " + std::to_string(total_patches) + " reference(s)");
}

} // namespace gs
