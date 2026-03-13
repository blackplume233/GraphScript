#include "editor.h"
#include "graphscript/runtime/runtime_graph.h"
#include <iostream>
#include <sstream>
#include <algorithm>

namespace gs {

CLIEditor::CLIEditor(EditSession& session) : session_(session) {}

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
    auto args = tokenize(line);
    if (args.empty()) return true;

    auto& cmd = args[0];

    if (cmd == "help" || cmd == "?")       cmd_help();
    else if (cmd == "status")              cmd_status();
    else if (cmd == "new")                 cmd_new(args);
    else if (cmd == "open")                cmd_open(args);
    else if (cmd == "delete")              cmd_delete_graph(args);
    else if (cmd == "graphs")              cmd_graphs();
    else if (cmd == "import")              cmd_import(args);
    else if (cmd == "let")                 cmd_let(args);
    else if (cmd == "param")               cmd_param(args);
    else if (cmd == "params")              cmd_params();
    else if (cmd == "add")                 cmd_add(args);
    else if (cmd == "rm")                  cmd_rm(args);
    else if (cmd == "nodes")               cmd_nodes();
    else if (cmd == "info")                cmd_info(args);
    else if (cmd == "pins")                cmd_pins(args);
    else if (cmd == "event")               cmd_event(args);
    else if (cmd == "fn")                  cmd_fn(args);
    else if (cmd == "flow")                cmd_flow(args);
    else if (cmd == "link")                cmd_link(args);
    else if (cmd == "unflow")              cmd_unflow(args);
    else if (cmd == "unlink")              cmd_unlink(args);
    else if (cmd == "done")                cmd_done();
    else if (cmd == "connections" || cmd == "conns") cmd_connections();
    else if (cmd == "comment")             cmd_comment(args);
    else if (cmd == "meta")                cmd_meta(args);
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

void CLIEditor::print_error(const std::string& msg) { std::cout << "  [ERROR] " << msg << "\n"; }
void CLIEditor::print_ok(const std::string& msg) { std::cout << "  " << msg << "\n"; }

// ─── Commands ──────────────────────────────────────────────────────

void CLIEditor::cmd_help() {
    std::cout <<
        "\n=== Graph Management ===\n"
        "  new <name> [: schema]    Create new graph\n"
        "  open <name>              Switch active graph\n"
        "  delete <name>            Delete a graph\n"
        "  graphs                   List all graphs\n"
        "  import <file.d.gs>       Load declarations\n"
        "  let <name> <Type> [arg]  Add let declaration\n"
        "\n=== Parameters ===\n"
        "  param <in|out|var> <name> <type> [= default]\n"
        "  param rm <name>          Remove parameter\n"
        "  params                   List parameters\n"
        "\n=== Nodes ===\n"
        "  add <Type> <name>        Add node instance\n"
        "  rm <name>                Remove node instance\n"
        "  nodes                    List node instances\n"
        "  info <name>              Show node details\n"
        "  pins <name>              Show node pins\n"
        "  types                    List available node types\n"
        "\n=== Events / Functions ===\n"
        "  event <name>             Create/enter event block\n"
        "  fn <name>                Create/enter function block\n"
        "  done                     Exit event/function context\n"
        "\n=== Connections (inside event/fn) ===\n"
        "  flow <from.pin> <to.pin> Add flow connection\n"
        "  link <target.pin> <src>  Add data link (src = node.pin or param)\n"
        "  unflow <from.pin> <to.pin>\n"
        "  unlink <target.pin>      Remove data link\n"
        "  connections              List all connections\n"
        "\n=== Generate ===\n"
        "  comment <name> <text>    Add generate comment\n"
        "  meta <scope:node.prop> <val>  Add metadata\n"
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
    auto r = session_.load_import(args[1]);
    if (r.is_err()) print_error(r.error());
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

void CLIEditor::cmd_rm(const std::vector<std::string>& args) {
    if (args.size() < 2) { print_error("Usage: rm <instance_name>"); return; }
    auto r = session_.remove_node(args[1]);
    if (r.is_err()) print_error(r.error());
    else print_ok("Removed '" + args[1] + "'");
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

void CLIEditor::cmd_comment(const std::vector<std::string>& args) {
    if (args.size() < 3) { print_error("Usage: comment <name> <text>"); return; }
    // Join remaining args as text
    std::string text;
    for (size_t i = 2; i < args.size(); i++) { if (i > 2) text += " "; text += args[i]; }
    auto r = session_.add_comment(args[1], text);
    if (r.is_err()) print_error(r.error());
    else print_ok("Comment " + args[1] + " = \"" + text + "\"");
}

void CLIEditor::cmd_meta(const std::vector<std::string>& args) {
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

} // namespace gs
