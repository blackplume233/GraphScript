#include "graphscript/edit/edit_session.h"
#include "graphscript/parse/lexer.h"
#include "graphscript/parse/parser.h"
#include "graphscript/schema/schema_registry.h"
#include <fstream>
#include <sstream>
#include <algorithm>

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

static std::string pin_def_to_json(const PinDefinition& p) {
    return jobj({
        {"name",      jstr(p.name)},
        {"kind",      jstr(p.kind == PinKind::Exec ? "exec" : "data")},
        {"direction", jstr(p.direction == PinDirection::Input ? "in" : "out")},
        {"type",      jstr(p.type_name)}
    });
}

static std::string param_to_json(const GraphParameter& p) {
    std::string dir = p.direction == ParamDirection::In  ? "in" :
                      p.direction == ParamDirection::Out ? "out" : "var";
    return jobj({
        {"name",      jstr(p.name)},
        {"type",      jstr(p.type_name)},
        {"direction", jstr(dir)},
        {"default",   jstr(p.default_value)}
    });
}

static std::string node_inst_to_json(const NodeInstance& ni) {
    return jobj({
        {"type",     jstr(ni.type_name)},
        {"instance", jstr(ni.instance_name)},
        {"init",     jstr(ni.initializer)}
    });
}

static std::string flow_to_json(const FlowConnection& fc) {
    return jobj({
        {"from_node", jstr(fc.from.node_instance)},
        {"from_pin",  jstr(fc.from.pin_name)},
        {"to_node",   jstr(fc.to.node_instance)},
        {"to_pin",    jstr(fc.to.pin_name)}
    });
}

static std::string link_to_json(const DataLink& dl) {
    return jobj({
        {"target_node", jstr(dl.target.node_instance)},
        {"target_pin",  jstr(dl.target.pin_name)},
        {"source_node", jstr(dl.source.node_instance)},
        {"source_pin",  jstr(dl.source.pin_name)}
    });
}

static std::string block_to_json(const LogicBlock& b, const std::string& kind) {
    std::vector<std::string> flows, links;
    for (auto& fc : b.flow_connections) flows.push_back(flow_to_json(fc));
    for (auto& dl : b.data_links)       links.push_back(link_to_json(dl));
    return jobj({
        {"name",  jstr(b.name)},
        {"kind",  jstr(kind)},
        {"flows", jarray(flows)},
        {"links", jarray(links)}
    });
}

static std::string graph_to_json(const Graph& g) {
    std::vector<std::string> params, nodes, events, funcs;
    for (auto& p : g.parameters)     params.push_back(param_to_json(p));
    for (auto& ni : g.node_instances) nodes.push_back(node_inst_to_json(ni));
    for (auto& ev : g.events)        events.push_back(block_to_json(ev, "event"));
    for (auto& fn : g.functions)     funcs.push_back(block_to_json(fn, "function"));

    return jobj({
        {"name",       jstr(g.name)},
        {"base_type",  g.base_type ? jstr(*g.base_type) : "null"},
        {"parameters", jarray(params)},
        {"nodes",      jarray(nodes)},
        {"events",     jarray(events)},
        {"functions",  jarray(funcs)}
    });
}

static std::string node_def_to_json(const NodeDefinition& nd) {
    std::vector<std::string> pins, tags;
    for (auto& p : nd.pins) pins.push_back(pin_def_to_json(p));
    for (auto& t : nd.tags) tags.push_back(jstr(t));
    return jobj({
        {"type_name",    jstr(nd.type_name)},
        {"is_native",    jbool(nd.is_native)},
        {"source_graph", jstr(nd.source_graph)},
        {"tags",         jarray(tags)},
        {"pins",         jarray(pins)}
    });
}

EditSession::EditSession(Environment& env) : env_(env) {}

// ─── Undo helpers ──────────────────────────────────────────────────

// Saves current active graph state before a mutation.
void EditSession::push_undo(const std::string& description) {
    if (active_ < 0 || active_ >= static_cast<int>(module_.graphs.size())) return;
    undo_stack_.push_back({module_.graphs[active_], description});
    if (undo_stack_.size() > kMaxUndoDepth) {
        undo_stack_.erase(undo_stack_.begin());
    }
    redo_stack_.clear();
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
    module_.graphs.push_back(std::move(g));
    active_ = static_cast<int>(module_.graphs.size()) - 1;
    dirty_ = true;
    return Result<int, std::string>::ok(active_);
}

Result<void, std::string> EditSession::delete_graph(const std::string& name) {
    for (size_t i = 0; i < module_.graphs.size(); i++) {
        if (module_.graphs[i].name == name) {
            module_.graphs.erase(module_.graphs.begin() + i);
            if (active_ == static_cast<int>(i)) active_ = -1;
            else if (active_ > static_cast<int>(i)) active_--;
            dirty_ = true;
            return Result<void, std::string>::ok();
        }
    }
    return Result<void, std::string>::err("Graph '" + name + "' not found");
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
    for (auto& imp : module_.imports) {
        if (imp.path == path) return;
    }
    ImportDecl decl;
    decl.path = path;
    decl.is_native = (path.size() > 5 && path.substr(path.size() - 5) == ".d.gs");
    module_.imports.push_back(std::move(decl));
    dirty_ = true;
}

void EditSession::add_let(const std::string& name, const std::string& type_name, const std::string& ctor_arg) {
    LetDecl decl;
    decl.name = name;
    decl.type_name = type_name;
    decl.constructor_arg = ctor_arg;
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
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::remove_param(const std::string& name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    for (auto it = g->parameters.begin(); it != g->parameters.end(); ++it) {
        if (it->name == name) {
            push_undo("remove param '" + name + "'");
            g->parameters.erase(it);
            return Result<void, std::string>::ok();
        }
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
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::remove_event(const std::string& name) {
    auto* g = active_graph();
    if (!g) return Result<void, std::string>::err("No active graph");
    for (auto it = g->events.begin(); it != g->events.end(); ++it) {
        if (it->name == name) {
            push_undo("remove event '" + name + "'");
            g->events.erase(it);
            return Result<void, std::string>::ok();
        }
    }
    return Result<void, std::string>::err("Event '" + name + "' not found");
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

// ─── Connection-level ──────────────────────────────────────────────

Result<void, std::string> EditSession::add_flow(const std::string& block_name,
                                                 const std::string& from_node, const std::string& from_pin,
                                                 const std::string& to_node, const std::string& to_pin) {
    auto* block = find_block(block_name);
    if (!block) return Result<void, std::string>::err("Logic block '" + block_name + "' not found");

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
    auto* block = find_block(block_name);
    if (!block) return Result<void, std::string>::err("Logic block '" + block_name + "' not found");

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

// ─── Generate block ────────────────────────────────────────────────

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

// ─── Undo / Redo ───────────────────────────────────────────────────

Result<std::string, std::string> EditSession::undo() {
    if (undo_stack_.empty()) return Result<std::string, std::string>::err("Nothing to undo");
    if (active_ < 0) return Result<std::string, std::string>::err("No active graph");

    auto snapshot = std::move(undo_stack_.back());
    undo_stack_.pop_back();

    // Push current state to redo
    redo_stack_.push_back({module_.graphs[active_], snapshot.description});

    module_.graphs[active_] = std::move(snapshot.graph);
    dirty_ = true;
    return Result<std::string, std::string>::ok(redo_stack_.back().description);
}

Result<std::string, std::string> EditSession::redo() {
    if (redo_stack_.empty()) return Result<std::string, std::string>::err("Nothing to redo");
    if (active_ < 0) return Result<std::string, std::string>::err("No active graph");

    auto snapshot = std::move(redo_stack_.back());
    redo_stack_.pop_back();

    // Push current state to undo
    undo_stack_.push_back({module_.graphs[active_], snapshot.description});

    module_.graphs[active_] = std::move(snapshot.graph);
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

std::vector<Diagnostic> EditSession::validate() const {
    auto eg = build_edit_graph();
    if (!eg) return {};
    return eg->validate();
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

Result<void, std::string> EditSession::load_file(const std::string& path) {
    auto src = read_file_contents(path);
    if (src.empty()) return Result<void, std::string>::err("Cannot read file: " + path);

    auto ast = parse_text(src);
    if (!ast) return Result<void, std::string>::err("Parse error in: " + path);

    Compiler compiler(env_);
    auto result = compiler.compile(*ast, path);
    if (result.is_err()) return Result<void, std::string>::err("Compile error: " + result.error());

    module_ = std::move(result).value();
    active_ = module_.graphs.empty() ? -1 : 0;
    undo_stack_.clear();
    redo_stack_.clear();
    file_path_ = path;
    dirty_ = false;
    return Result<void, std::string>::ok();
}

Result<void, std::string> EditSession::load_import(const std::string& path) {
    auto src = read_file_contents(path);
    if (src.empty()) return Result<void, std::string>::err("Cannot read file: " + path);

    auto ast = parse_text(src);
    if (!ast) return Result<void, std::string>::err("Parse error in: " + path);

    Compiler compiler(env_);
    auto result = compiler.compile(*ast, path);
    if (result.is_err()) return Result<void, std::string>::err("Compile error: " + result.error());

    add_import(path);
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

std::string EditSession::state_to_json() const {
    // Module: imports
    std::vector<std::string> imports;
    for (auto& imp : module_.imports)
        imports.push_back(jobj({{"path", jstr(imp.path)}, {"is_native", jbool(imp.is_native)}}));

    // Module: lets
    std::vector<std::string> lets;
    for (auto& l : module_.top_level_lets)
        lets.push_back(jobj({{"name", jstr(l.name)}, {"type", jstr(l.type_name)}, {"arg", jstr(l.constructor_arg)}}));

    // Module: graphs
    std::vector<std::string> graphs;
    for (auto& g : module_.graphs)
        graphs.push_back(graph_to_json(g));

    // Node type definitions
    std::vector<std::string> types;
    for (auto* nd : env_.nodes().all())
        types.push_back(node_def_to_json(*nd));

    // Schemas
    std::vector<std::string> schemas;
    for (auto* s : env_.schemas().all()) {
        schemas.push_back(jobj({
            {"name",              jstr(s->name)},
            {"max_exec_fan_out",  jint(s->connection_policy.max_exec_fan_out)},
            {"allow_exec_fan_in", jbool(s->connection_policy.allow_exec_fan_in)},
            {"strict_type_match", jbool(s->connection_policy.strict_type_match)}
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
        {"types",        jarray(types)},
        {"schemas",      jarray(schemas)},
        {"command_log",  jarray(log)}
    });
}

} // namespace gs
