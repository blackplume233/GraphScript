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
                  << "Options:\n"
                  << "  -i, --input   Input file\n"
                  << "  -o, --output  Output file\n"
                  << "  -I, --import  Import .d.gs file (can repeat)\n"
                  << "  -f, --format  Output format (json|summary)\n"
                  << "  -p, --port    HTTP port for serve (default: 8080)\n";
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
        load_imports(opts.import_files, env);
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
