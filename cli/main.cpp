#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "graphscript/parse/lexer.h"
#include "graphscript/parse/parser.h"
#include "graphscript/compile/compiler.h"
#include "graphscript/emit/emitter.h"
#include "graphscript/edit/edit_graph.h"
#include "graphscript/edit/edit_session.h"
#include "graphscript/runtime/runtime_graph.h"
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
    std::string parent_scope_name;
    std::string scope_kind;
    std::string scope_name;
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
        else if (arg == "--parent-scope" && i + 1 < argc) opts.parent_scope_name = argv[++i];
        else if (arg == "--scope-kind" && i + 1 < argc) opts.scope_kind = argv[++i];
        else if (arg == "--scope-name" && i + 1 < argc) opts.scope_name = argv[++i];
        else if (arg == "--target" && i + 1 < argc) opts.target_name = argv[++i];
        else if (arg == "--attribute" && i + 1 < argc) opts.attribute_source = argv[++i];
    }
    return opts;
}

static std::unique_ptr<gs::ModuleNode> parse_source(const std::string& src) {
    gs::Lexer lexer(src);
    auto tokens = lexer.tokenize();
    gs::Parser parser(std::move(tokens));
    auto result = parser.parse();
    if (result.is_err()) {
        std::cerr << "Parse error: " << result.error() << "\n";
        return nullptr;
    }
    return std::move(result).value();
}

static void load_imports(const std::vector<std::string>& import_files, gs::Environment& env) {
    for (auto& path : import_files) {
        auto src = read_file(path);
        if (src.empty()) { std::cerr << "Warning: Cannot read import file '" << path << "'\n"; continue; }
        auto ast = parse_source(src);
        if (!ast) continue;
        gs::Compiler compiler(env);
        compiler.compile(*ast, path);
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

static void merge_asset_declarations(gs::asset::Module& target, gs::asset::Module&& source) {
    for (auto& module : source.modules) target.modules.push_back(std::move(module));
    for (auto& symbol : source.symbols) target.symbols.push_back(std::move(symbol));
    for (auto& object : source.objects) target.objects.push_back(std::move(object));
}

static int cmd_sc_parse(const CLIOptions& opts) {
    auto source = read_file(opts.input_file);
    if (source.empty()) { std::cerr << "Error: Cannot read '" << opts.input_file << "'\n"; return 1; }
    auto parsed = parse_asset_source(source, opts.input_file);
    std::cout << "{\n";
    std::cout << "  \"imports\": " << parsed.module.imports.size() << ",\n";
    std::cout << "  \"modules\": " << parsed.module.modules.size() << ",\n";
    std::cout << "  \"enums\": " << parsed.module.enums.size() << ",\n";
    std::cout << "  \"objects\": " << parsed.module.objects.size() << ",\n";
    std::cout << "  \"scope_kinds\": " << parsed.module.scope_kinds.size() << ",\n";
    std::cout << "  \"commands\": " << parsed.module.commands.size() << ",\n";
    std::cout << "  \"schemas\": " << parsed.module.schemas.size() << ",\n";
    std::cout << "  \"lints\": " << parsed.module.lints.size() << ",\n";
    std::cout << "  \"symbols\": " << parsed.module.symbols.size() << ",\n";
    std::cout << "  \"scopes\": " << parsed.module.items.scopes.size() << ",\n";
    std::cout << "  \"directives\": " << parsed.module.items.directives.size() << ",\n";
    std::cout << "  \"assignments\": " << parsed.module.items.assignments.size() << ",\n";
    std::cout << "  \"diagnostics\": " << parsed.diagnostics.size() << "\n";
    std::cout << "}\n";
    return parsed.diagnostics.empty() ? 0 : 1;
}

static int cmd_sc_lint(const CLIOptions& opts) {
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

static int cmd_sc_project(const CLIOptions& opts) {
    auto source = read_file(opts.input_file);
    if (source.empty()) { std::cerr << "Error: Cannot read '" << opts.input_file << "'\n"; return 1; }
    auto parsed = parse_asset_source(source, opts.input_file);
    for (const auto& import_path : opts.import_files) {
        auto import_source = read_file(import_path);
        if (import_source.empty()) { std::cerr << "Warning: Cannot read import file '" << import_path << "'\n"; continue; }
        auto import_parsed = parse_asset_source(import_source, import_path);
        merge_asset_declarations(parsed.module, std::move(import_parsed.module));
    }
    auto projected = gs::asset::FlowGraphProjector::project(parsed.module, opts.graph_name);
    if (projected.is_err()) {
        std::cerr << "Projection error: " << projected.error() << "\n";
        return 1;
    }
    const auto& graph = projected.value();
    std::cout << "{\n";
    std::cout << "  \"name\": \"" << json_escape(graph.name) << "\",\n";
    std::cout << "  \"schema\": \"" << json_escape(graph.schema) << "\",\n";
    std::cout << "  \"nodes\": " << graph.nodes.size() << ",\n";
    std::cout << "  \"edges\": " << graph.edges.size() << ",\n";
    std::cout << "  \"diagnostics\": " << graph.diagnostics.size() << "\n";
    std::cout << "}\n";
    return graph.diagnostics.empty() ? 0 : 1;
}

static int cmd_sc_patch(const CLIOptions& opts) {
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
    } else if (opts.patch_op == "add-scope") {
        auto result = gs::asset::Patcher::add_scope(source, parsed.module, opts.parent_scope_name, opts.scope_kind, opts.scope_name, opts.type_name);
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
    } else if (opts.patch_op == "rename-scope") {
        auto result = gs::asset::Patcher::rename_scope(source, parsed.module, opts.scope_name, opts.new_alias);
        if (result.is_err()) { std::cerr << "Patch error: " << result.error() << "\n"; return 1; }
        patch = result.value();
    } else if (opts.patch_op == "set-property") {
        auto result = gs::asset::Patcher::set_property(source, parsed.module, opts.alias, opts.property_path, opts.value);
        if (result.is_err()) { std::cerr << "Patch error: " << result.error() << "\n"; return 1; }
        patch = result.value();
    } else {
        std::cerr << "Error: --op add-import|add-node|add-scope|add-attribute|connect|disconnect|rename-node|rename-scope|set-property required\n";
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

static int cmd_parse(const CLIOptions& opts) {
    auto source = read_file(opts.input_file);
    if (source.empty()) { std::cerr << "Error: Cannot read '" << opts.input_file << "'\n"; return 1; }
    auto ast = parse_source(source);
    if (!ast) return 1;

    std::cout << "{\n";
    std::cout << "  \"imports\": " << ast->imports.size() << ",\n";
    std::cout << "  \"declare_types\": " << ast->declare_types.size() << ",\n";
    std::cout << "  \"declare_nodes\": " << ast->declare_nodes.size() << ",\n";
    std::cout << "  \"declare_schemas\": " << ast->declare_schemas.size() << ",\n";
    std::cout << "  \"let_decls\": " << ast->let_decls.size() << ",\n";
    std::cout << "  \"graphs\": " << ast->graphs.size() << "\n";
    std::cout << "}\n";
    return 0;
}

static int cmd_compile(const CLIOptions& opts) {
    gs::Environment env;
    load_imports(opts.import_files, env);

    auto source = read_file(opts.input_file);
    if (source.empty()) { std::cerr << "Error: Cannot read '" << opts.input_file << "'\n"; return 1; }
    auto ast = parse_source(source);
    if (!ast) return 1;

    gs::Compiler compiler(env);
    auto result = compiler.compile(*ast, opts.input_file);
    if (result.is_err()) { std::cerr << "Compile error: " << result.error() << "\n"; return 1; }

    auto& mod = result.value();
    std::cout << "Compiled: " << opts.input_file << "\n";
    std::cout << "  Graphs: " << mod.graphs.size() << "\n";
    for (auto& g : mod.graphs) {
        std::cout << "    - " << g.name;
        if (g.base_type) std::cout << " : " << *g.base_type;
        std::cout << " (" << g.parameters.size() << " params, "
                  << g.node_instances.size() << " nodes, "
                  << g.events.size() << " events, "
                  << g.functions.size() << " functions)\n";
    }
    return 0;
}

static int cmd_validate(const CLIOptions& opts) {
    gs::Environment env;
    load_imports(opts.import_files, env);

    auto source = read_file(opts.input_file);
    if (source.empty()) { std::cerr << "Error: Cannot read '" << opts.input_file << "'\n"; return 1; }
    auto ast = parse_source(source);
    if (!ast) return 1;

    gs::Compiler compiler(env);
    auto result = compiler.compile(*ast, opts.input_file);
    if (result.is_err()) { std::cerr << "Compile error: " << result.error() << "\n"; return 1; }

    int total_issues = 0;
    for (auto& g : result.value().graphs) {
        auto eg = gs::EditGraph::build(g, env);
        auto diags = eg.validate();
        if (!diags.empty()) {
            std::cout << "Graph '" << g.name << "':\n";
            for (auto& d : diags) {
                std::cout << "  [" << (d.severity == gs::Severity::Error ? "ERROR" : "WARN") << "] " << d.message << "\n";
                total_issues++;
            }
        }
    }
    if (total_issues == 0) std::cout << "Validation passed: 0 issues\n";
    return (total_issues > 0) ? 1 : 0;
}

// Emit command: reconstructs .gs text from compiled module.
static int cmd_emit(const CLIOptions& opts) {
    gs::Environment env;
    load_imports(opts.import_files, env);

    auto source = read_file(opts.input_file);
    if (source.empty()) { std::cerr << "Error: Cannot read '" << opts.input_file << "'\n"; return 1; }
    auto ast = parse_source(source);
    if (!ast) return 1;

    gs::Compiler compiler(env);
    auto result = compiler.compile(*ast, opts.input_file);
    if (result.is_err()) { std::cerr << "Compile error: " << result.error() << "\n"; return 1; }

    gs::Emitter emitter;
    auto output = emitter.emit(result.value());

    if (opts.output_file.empty()) {
        std::cout << output;
    } else {
        std::ofstream f(opts.output_file);
        if (!f.is_open()) { std::cerr << "Error: Cannot write '" << opts.output_file << "'\n"; return 1; }
        f << output;
        std::cout << "Emitted to: " << opts.output_file << "\n";
    }
    return 0;
}

// Bake command: converts EditGraph to RuntimeGraph.
static int cmd_bake(const CLIOptions& opts) {
    gs::Environment env;
    load_imports(opts.import_files, env);

    auto source = read_file(opts.input_file);
    if (source.empty()) { std::cerr << "Error: Cannot read '" << opts.input_file << "'\n"; return 1; }
    auto ast = parse_source(source);
    if (!ast) return 1;

    gs::Compiler compiler(env);
    auto result = compiler.compile(*ast, opts.input_file);
    if (result.is_err()) { std::cerr << "Compile error: " << result.error() << "\n"; return 1; }

    for (auto& g : result.value().graphs) {
        auto eg = gs::EditGraph::build(g, env);
        auto rt = gs::RuntimeGraph::bake(eg);
        std::cout << "RuntimeGraph '" << rt.name() << "':\n";
        if (!rt.domain_name().empty()) std::cout << "  Domain: " << rt.domain_name() << "\n";
        std::cout << "  Nodes: " << rt.node_count() << "\n";
        std::cout << "  Pins: " << rt.pins().size() << "\n";
        std::cout << "  Flow edges: " << rt.flow_edge_count() << "\n";
        std::cout << "  Data edges: " << rt.data_edge_count() << "\n";
    }
    return 0;
}

// Info command: dumps environment registry contents.
static int cmd_info(const CLIOptions& opts) {
    gs::Environment env;
    load_imports(opts.import_files, env);

    if (!opts.input_file.empty()) {
        auto source = read_file(opts.input_file);
        if (!source.empty()) {
            auto ast = parse_source(source);
            if (ast) {
                gs::Compiler compiler(env);
                compiler.compile(*ast, opts.input_file);
            }
        }
    }

    std::cout << "Types: " << env.types().all().size() << "\n";
    for (auto* t : env.types().all()) {
        std::cout << "  - " << t->name << (t->constructible ? " (constructible)" : "") << "\n";
    }

    std::cout << "\nNodes: " << env.nodes().all().size() << "\n";
    for (auto* n : env.nodes().all()) {
        std::cout << "  - " << n->type_name;
        if (!n->is_native) std::cout << " [graph]";
        std::cout << " (" << n->pins.size() << " pins)\n";
    }

    std::cout << "\nSchemas: " << env.schemas().all().size() << "\n";
    for (auto* s : env.schemas().all()) {
        std::cout << "  - " << s->name << "\n";
    }
    return 0;
}

// Schema command: lists registered schemas.
static int cmd_schema(const CLIOptions& opts) {
    gs::Environment env;
    load_imports(opts.import_files, env);

    auto schemas = env.schemas().all();
    if (schemas.empty()) {
        std::cout << "No schemas registered.\n";
        return 0;
    }

    for (auto* s : schemas) {
        std::cout << "Schema: " << s->name << "\n";
        std::cout << "  max_exec_fan_out: " << (s->connection_policy.max_exec_fan_out == -1 ? "unlimited" : std::to_string(s->connection_policy.max_exec_fan_out)) << "\n";
        std::cout << "  allow_exec_fan_in: " << (s->connection_policy.allow_exec_fan_in ? "true" : "false") << "\n";
        std::cout << "  strict_type_match: " << (s->connection_policy.strict_type_match ? "true" : "false") << "\n";
        if (!s->allowed_node_tags.empty()) {
            std::cout << "  allowed_node_tags: [";
            for (size_t i = 0; i < s->allowed_node_tags.size(); i++) {
                if (i > 0) std::cout << ", ";
                std::cout << "\"" << s->allowed_node_tags[i] << "\"";
            }
            std::cout << "]\n";
        }
        if (!s->required_events.empty()) {
            std::cout << "  required_events: [";
            for (size_t i = 0; i < s->required_events.size(); i++) {
                if (i > 0) std::cout << ", ";
                std::cout << "\"" << s->required_events[i] << "\"";
            }
            std::cout << "]\n";
        }
        std::cout << "\n";
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
                  << "  parse     Parse .gs/.d.gs file and output AST summary\n"
                  << "  compile   Compile .gs files into Graph structures\n"
                  << "  validate  Validate graph against schema\n"
                  << "  emit      Emit .gs text from compiled graph\n"
                  << "  diagram   Generate Mermaid flowchart from graph\n"
                  << "  bake      Bake EditGraph into RuntimeGraph\n"
                  << "  info      Show type/node/schema registry info\n"
                  << "  schema    List registered schemas\n\n"
                  << "  sc-parse  Parse .sc/.d.sc and output syntax summary\n"
                  << "  sc-lint   Lint .sc/.d.sc and output diagnostics JSON\n"
                  << "  sc-project Project .sc graph scope into FlowGraph JSON summary\n"
                  << "  sc-patch  Apply .sc text patch operation\n\n"
                  << "Options:\n"
                  << "  -i, --input   Input file\n"
                  << "  -o, --output  Output file\n"
                  << "  -I, --import  Import .d.gs file (can repeat)\n"
                  << "  -f, --format  Output format (json|summary)\n"
                  << "  -p, --port    HTTP port for serve (default: 8080)\n"
                  << "  --op          sc-patch op: add-import|add-node|add-scope|add-attribute|connect|disconnect|rename-node|rename-scope|set-property\n"
                  << "  --graph       Scope graph name for sc-project/sc-patch\n"
                  << "  --alias       Node alias for sc-patch\n"
                  << "  --new-alias   New node alias for rename-node\n"
                  << "  --parent-scope Parent scope name for add-scope\n"
                  << "  --scope-kind  Scope kind for add-scope\n"
                  << "  --scope-name  Scope name for add-scope/rename-scope\n"
                  << "  --target      Target scope/object name for add-attribute\n"
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
        load_imports(opts.import_files, env);
        gs::EditSession session(env);
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
        load_imports(serve_imports(opts), env);
        gs::EditSession session(env);
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

    if (opts.input_file.empty() && opts.command != "schema" && opts.command != "info") {
        std::cerr << "Error: -i <input_file> required\n";
        return 1;
    }

    if (opts.command == "sc-parse")   return cmd_sc_parse(opts);
    if (opts.command == "sc-lint")    return cmd_sc_lint(opts);
    if (opts.command == "sc-project") return cmd_sc_project(opts);
    if (opts.command == "sc-patch")   return cmd_sc_patch(opts);
    if (opts.command == "parse")    return cmd_parse(opts);
    if (opts.command == "compile")  return cmd_compile(opts);
    if (opts.command == "validate") return cmd_validate(opts);
    if (opts.command == "emit")     return cmd_emit(opts);
    if (opts.command == "diagram") {
        auto src = read_file(opts.input_file);
        auto ast = parse_source(src);
        if (!ast) return 1;
        gs::Environment env;
        load_imports(opts.import_files, env);
        gs::Compiler compiler(env);
        auto result = compiler.compile(*ast, opts.input_file);
        if (result.is_err()) { std::cerr << "Compile error: " << result.error() << "\n"; return 1; }
        gs::Emitter emitter;
        std::string md = emitter.emit_diagram(result.value());
        if (!opts.output_file.empty()) {
            std::ofstream f(opts.output_file);
            f << md;
            std::cout << "Diagram written to " << opts.output_file << "\n";
        } else {
            std::cout << md;
        }
        return 0;
    }
    if (opts.command == "bake")     return cmd_bake(opts);
    if (opts.command == "info")     return cmd_info(opts);
    if (opts.command == "schema")   return cmd_schema(opts);

    std::cerr << "Unknown command: " << opts.command << "\n";
    return 1;
}
