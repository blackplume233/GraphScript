#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "graphscript/edit/edit_session.h"
#include "graphscript/registry/environment.h"
#include "graphscript/asset/language.h"
#include "editor.h"
#include "server.h"

// Reads file contents into string.
static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// CLI argument container.
struct CLIOptions {
    std::string command;
    std::string input_file;
    std::string output_file;
    std::string format = "json";
    std::vector<std::string> import_files;
    std::string patch_op;
    std::string graph_name;
    std::string alias;
    std::string type_name;
    std::string from_ref;
    std::string to_ref;
    std::string property_path;
    std::string value;
    std::string new_alias;
    std::string add_import_path;
    std::string parent_block_name;
    std::string block_kind;
    std::string block_name;
    std::string target_name;
    std::string attribute_source;
    int port = 8080;
};

// Parses command-line arguments into CLIOptions.
static CLIOptions parse_args(int argc, char* argv[]) {
    CLIOptions opts;
    if (argc >= 2) opts.command = argv[1];
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-i" || arg == "--input") && i + 1 < argc)  opts.input_file = argv[++i];
        else if ((arg == "-o" || arg == "--output") && i + 1 < argc) opts.output_file = argv[++i];
        else if ((arg == "-f" || arg == "--format") && i + 1 < argc) opts.format = argv[++i];
        else if ((arg == "-I" || arg == "--import") && i + 1 < argc) opts.import_files.push_back(argv[++i]);
        else if ((arg == "-p" || arg == "--port") && i + 1 < argc) opts.port = std::atoi(argv[++i]);
        else if (arg == "--op" && i + 1 < argc) opts.patch_op = argv[++i];
        else if (arg == "--graph" && i + 1 < argc) opts.graph_name = argv[++i];
        else if (arg == "--alias" && i + 1 < argc) opts.alias = argv[++i];
        else if (arg == "--type" && i + 1 < argc) opts.type_name = argv[++i];
        else if (arg == "--from" && i + 1 < argc) opts.from_ref = argv[++i];
        else if (arg == "--to" && i + 1 < argc) opts.to_ref = argv[++i];
        else if (arg == "--property" && i + 1 < argc) opts.property_path = argv[++i];
        else if (arg == "--value" && i + 1 < argc) opts.value = argv[++i];
        else if (arg == "--new-alias" && i + 1 < argc) opts.new_alias = argv[++i];
        else if (arg == "--add-import" && i + 1 < argc) opts.add_import_path = argv[++i];
        else if (arg == "--parent-block" && i + 1 < argc) opts.parent_block_name = argv[++i];
        else if (arg == "--block-kind" && i + 1 < argc) opts.block_kind = argv[++i];
        else if (arg == "--block-name" && i + 1 < argc) opts.block_name = argv[++i];
        else if (arg == "--target" && i + 1 < argc) opts.target_name = argv[++i];
        else if (arg == "--attribute" && i + 1 < argc) opts.attribute_source = argv[++i];
    }
    return opts;
}

static void load_session_imports(const std::vector<std::string>& import_files, gs::EditSession& session) {
    for (auto& path : import_files) {
        auto loaded = session.load_import(path);
        if (loaded.is_err()) {
            std::cerr << "Warning: " << loaded.error() << "\n";
        }
    }
}

static std::vector<std::string> default_preset_imports() {
    std::vector<std::string> imports;
#ifdef GS_PRESETS_DIR
    const std::string dir = GS_PRESETS_DIR;
    const std::vector<std::string> candidates = {
        dir + "/ue_core.d.gs",
        dir + "/ue_blueprint.d.gs",
        dir + "/task_nodes.d.gs",
        dir + "/levelscript_nodes.d.gs",
        dir + "/htn_nodes.d.gs",
    };
    for (const auto& path : candidates) {
        if (!read_file(path).empty()) imports.push_back(path);
    }
#endif
    return imports;
}

static std::vector<std::string> serve_imports(const CLIOptions& opts) {
    if (!opts.import_files.empty()) return opts.import_files;
    return default_preset_imports();
}

static std::string json_escape(const std::string& value) {
    std::string out;
    for (char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

static gs::asset::ParseResult parse_asset_source(const std::string& source, const std::string& source_name) {
    gs::asset::Parser parser(source, source_name);
    return parser.parse();
}

static gs::Diagnostic make_cli_diagnostic(const std::string& code,
                                          const std::string& message,
                                          const std::string& context,
                                          const std::string& hint) {
    gs::Diagnostic diagnostic;
    diagnostic.severity = gs::Severity::Error;
    diagnostic.code = code;
    diagnostic.message = message;
    diagnostic.context = context;
    diagnostic.hint = hint;
    return diagnostic;
}

static void merge_asset_declarations(gs::asset::Module& target, gs::asset::Module&& source) {
    for (auto& module : source.modules) target.modules.push_back(std::move(module));
    for (auto& symbol : source.symbols) target.symbols.push_back(std::move(symbol));
    for (auto& object : source.objects) target.objects.push_back(std::move(object));
}

static int cmd_asset_parse(const CLIOptions& opts) {
    auto source = read_file(opts.input_file);
    if (source.empty()) { std::cerr << "Error: Cannot read '" << opts.input_file << "'\n"; return 1; }
    auto parsed = parse_asset_source(source, opts.input_file);
    std::cout << "{\n";
    std::cout << "  \"imports\": " << parsed.module.imports.size() << ",\n";
    std::cout << "  \"modules\": " << parsed.module.modules.size() << ",\n";
    std::cout << "  \"enums\": " << parsed.module.enums.size() << ",\n";
    std::cout << "  \"objects\": " << parsed.module.objects.size() << ",\n";
    std::cout << "  \"block_kinds\": " << parsed.module.block_kinds.size() << ",\n";
    std::cout << "  \"commands\": " << parsed.module.commands.size() << ",\n";
    std::cout << "  \"schemas\": " << parsed.module.schemas.size() << ",\n";
    std::cout << "  \"lints\": " << parsed.module.lints.size() << ",\n";
    std::cout << "  \"symbols\": " << parsed.module.symbols.size() << ",\n";
    std::cout << "  \"blocks\": " << parsed.module.items.blocks.size() << ",\n";
    std::cout << "  \"directives\": " << parsed.module.items.directives.size() << ",\n";
    std::cout << "  \"assignments\": " << parsed.module.items.assignments.size() << ",\n";
    std::cout << "  \"diagnostics\": " << parsed.diagnostics.size() << "\n";
    std::cout << "}\n";
    return parsed.diagnostics.empty() ? 0 : 1;
}

static int cmd_asset_lint(const CLIOptions& opts) {
    auto source = read_file(opts.input_file);
    if (source.empty()) { std::cerr << "Error: Cannot read '" << opts.input_file << "'\n"; return 1; }
    auto parsed = parse_asset_source(source, opts.input_file);
    gs::asset::ModuleGraph graph;
    auto diagnostics = gs::asset::Linter::lint(parsed.module, &graph);
    diagnostics.insert(diagnostics.begin(), parsed.diagnostics.begin(), parsed.diagnostics.end());
    std::cout << "{\n";
    std::cout << "  \"module_id\": \"" << json_escape(graph.module_id) << "\",\n";
    std::cout << "  \"module_id_inferred\": " << (graph.module_id_inferred ? "true" : "false") << ",\n";
    std::cout << "  \"imports\": " << graph.imports.size() << ",\n";
    std::cout << "  \"exports\": " << graph.exports.size() << ",\n";
    std::cout << "  \"diagnostics\": [";
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << "{\"code\":\"" << json_escape(diagnostics[i].code)
                  << "\",\"message\":\"" << json_escape(diagnostics[i].message) << "\"}";
    }
    std::cout << "]\n}\n";
    return diagnostics.empty() ? 0 : 1;
}

static int cmd_asset_project(const CLIOptions& opts) {
    auto source = read_file(opts.input_file);
    if (source.empty()) { std::cerr << "Error: Cannot read '" << opts.input_file << "'\n"; return 1; }
    auto parsed = parse_asset_source(source, opts.input_file);
    std::vector<gs::Diagnostic> diagnostics = parsed.diagnostics;
    for (const auto& import_path : opts.import_files) {
        auto import_source = read_file(import_path);
        if (import_source.empty()) {
            diagnostics.push_back(make_cli_diagnostic("GS_IMPORT_READ_FAILED",
                                                      "Cannot read import file '" + import_path + "'",
                                                      import_path,
                                                      "Check the -I path before projecting the graph."));
            continue;
        }
        auto import_parsed = parse_asset_source(import_source, import_path);
        diagnostics.insert(diagnostics.end(), import_parsed.diagnostics.begin(), import_parsed.diagnostics.end());
        merge_asset_declarations(parsed.module, std::move(import_parsed.module));
    }
    auto projected = gs::asset::FlowGraphProjector::project(parsed.module, opts.graph_name);
    if (projected.is_err()) {
        std::cerr << "Projection error: " << projected.error() << "\n";
        return 1;
    }
    const auto& graph = projected.value();
    diagnostics.insert(diagnostics.end(), graph.diagnostics.begin(), graph.diagnostics.end());
    std::cout << "{\n";
    std::cout << "  \"name\": \"" << json_escape(graph.name) << "\",\n";
    std::cout << "  \"schema\": \"" << json_escape(graph.schema) << "\",\n";
    std::cout << "  \"parameters\": " << graph.parameters.size() << ",\n";
    std::cout << "  \"nodes\": " << graph.nodes.size() << ",\n";
    std::cout << "  \"edges\": " << graph.edges.size() << ",\n";
    std::cout << "  \"data_edges\": " << graph.data_edges.size() << ",\n";
    std::cout << "  \"blocks\": " << graph.blocks.size() << ",\n";
    std::cout << "  \"diagnostics\": " << diagnostics.size() << "\n";
    std::cout << "}\n";
    return diagnostics.empty() ? 0 : 1;
}

static int cmd_asset_patch(const CLIOptions& opts) {
    auto source = read_file(opts.input_file);
    if (source.empty()) { std::cerr << "Error: Cannot read '" << opts.input_file << "'\n"; return 1; }
    auto parsed = parse_asset_source(source, opts.input_file);

    gs::asset::TextPatch patch;
    if (opts.patch_op == "add-import") {
        if (opts.add_import_path.empty()) { std::cerr << "Error: --add-import required\n"; return 1; }
        patch = gs::asset::Patcher::add_import(source, opts.add_import_path);
    } else if (opts.patch_op == "add-node") {
        auto result = gs::asset::Patcher::add_node(source, parsed.module, opts.graph_name, opts.alias, opts.type_name);
        if (result.is_err()) { std::cerr << "Patch error: " << result.error() << "\n"; return 1; }
        patch = result.value();
    } else if (opts.patch_op == "add-block") {
        auto result = gs::asset::Patcher::add_block(source, parsed.module, opts.parent_block_name, opts.block_kind, opts.block_name, opts.type_name);
        if (result.is_err()) { std::cerr << "Patch error: " << result.error() << "\n"; return 1; }
        patch = result.value();
    } else if (opts.patch_op == "add-attribute") {
        auto result = gs::asset::Patcher::add_attribute(source, parsed.module, opts.target_name, opts.attribute_source);
        if (result.is_err()) { std::cerr << "Patch error: " << result.error() << "\n"; return 1; }
        patch = result.value();
    } else if (opts.patch_op == "connect") {
        auto result = gs::asset::Patcher::connect(source, parsed.module, opts.graph_name, opts.from_ref, opts.to_ref);
        if (result.is_err()) { std::cerr << "Patch error: " << result.error() << "\n"; return 1; }
        patch = result.value();
    } else if (opts.patch_op == "disconnect") {
        auto result = gs::asset::Patcher::disconnect(source, parsed.module, opts.graph_name, opts.from_ref, opts.to_ref);
        if (result.is_err()) { std::cerr << "Patch error: " << result.error() << "\n"; return 1; }
        patch = result.value();
    } else if (opts.patch_op == "rename-node") {
        auto result = gs::asset::Patcher::rename_node(source, parsed.module, opts.graph_name, opts.alias, opts.new_alias);
        if (result.is_err()) { std::cerr << "Patch error: " << result.error() << "\n"; return 1; }
        patch = result.value();
    } else if (opts.patch_op == "rename-block") {
        auto result = gs::asset::Patcher::rename_block(source, parsed.module, opts.block_name, opts.new_alias);
        if (result.is_err()) { std::cerr << "Patch error: " << result.error() << "\n"; return 1; }
        patch = result.value();
    } else if (opts.patch_op == "set-property") {
        auto result = gs::asset::Patcher::set_property(source, parsed.module, opts.alias, opts.property_path, opts.value);
        if (result.is_err()) { std::cerr << "Patch error: " << result.error() << "\n"; return 1; }
        patch = result.value();
    } else {
        std::cerr << "Error: --op add-import|add-node|add-block|add-attribute|connect|disconnect|rename-node|rename-block|set-property required\n";
        return 1;
    }

    auto applied = gs::asset::Patcher::apply(source, patch);
    if (applied.is_err()) { std::cerr << "Patch error: " << applied.error() << "\n"; return 1; }
    if (opts.output_file.empty()) {
        std::cout << applied.value();
    } else {
        std::ofstream out(opts.output_file);
        if (!out.is_open()) { std::cerr << "Error: Cannot write '" << opts.output_file << "'\n"; return 1; }
        out << applied.value();
        std::cout << "Patched: " << opts.output_file << "\n";
    }
    return 0;
}

// Entry point: dispatches to subcommand handlers.
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "GraphScript CLI (gs) v0.1.0\n"
                  << "Usage: gs <command> [options]\n\n"
                  << "Commands:\n"
                  << "  edit      Interactive graph editor (CLI REPL)\n"
                  << "  serve     Start web editor (GUI + HTTP server)\n"
                  << "  parse     Parse .gs/.d.gs asset syntax and output syntax summary\n"
                  << "  lint      Lint .gs/.d.gs asset syntax and output diagnostics JSON\n"
                  << "  project   Project .gs graph block into FlowGraph JSON summary\n"
                  << "  patch     Apply tree-sitter-aware .gs text patch operation\n\n"
                  << "Options:\n"
                  << "  -i, --input   Input file\n"
                  << "  -o, --output  Output file\n"
                  << "  -I, --import  Import .d.gs file (can repeat)\n"
                  << "  -f, --format  Output format (json|summary)\n"
                  << "  -p, --port    HTTP port for serve (default: 8080)\n"
                  << "  --op          patch op: add-import|add-node|add-block|add-attribute|connect|disconnect|rename-node|rename-block|set-property\n"
                  << "  --graph       Graph block name for project/patch\n"
                  << "  --alias       Node alias for patch\n"
                  << "  --new-alias   New node alias for rename-node\n"
                  << "  --parent-block Parent block name for add-block\n"
                  << "  --block-kind  Block kind for add-block\n"
                  << "  --block-name  Block name for add-block/rename-block\n"
                  << "  --target      Target block/object name for add-attribute\n"
                  << "  --attribute   Attribute source for add-attribute\n"
                  << "  --from/--to   Connection endpoints for connect/disconnect\n"
                  << "  --property    Property path for set-property\n"
                  << "  --value       Replacement value for set-property\n";
        return 0;
    }

    auto opts = parse_args(argc, argv);

    // Interactive editor — special handling before input check
    if (opts.command == "edit") {
        gs::Environment env;
        gs::EditSession session(env);
        load_session_imports(opts.import_files, session);
        if (!opts.input_file.empty()) {
            auto r = session.load_file(opts.input_file);
            if (r.is_err()) { std::cerr << "Error: " << r.error() << "\n"; return 1; }
        }
        gs::CLIEditor editor(session);
        return editor.run();
    }

    // Web editor server
    if (opts.command == "serve") {
        gs::Environment env;
        gs::EditSession session(env);
        load_session_imports(serve_imports(opts), session);
        if (!opts.input_file.empty()) {
            auto r = session.load_file(opts.input_file);
            if (r.is_err()) { std::cerr << "Error: " << r.error() << "\n"; return 1; }
        }
        gs::CLIEditor editor(session);
        gs::WebServer server(session, editor, opts.port);
#ifdef GS_WEB_DIR
        server.set_web_dir(GS_WEB_DIR);
#endif
        return server.run();
    }

    const bool asset_command = opts.command == "parse" || opts.command == "lint" || opts.command == "project" || opts.command == "patch";
    if (!asset_command) {
        std::cerr << "Unknown command: " << opts.command << "\n";
        return 1;
    }

    if (opts.input_file.empty()) {
        std::cerr << "Error: -i <input_file> required\n";
        return 1;
    }

    if (opts.command == "parse")    return cmd_asset_parse(opts);
    if (opts.command == "lint")     return cmd_asset_lint(opts);
    if (opts.command == "project")  return cmd_asset_project(opts);
    if (opts.command == "patch")    return cmd_asset_patch(opts);

    return 1;
}
