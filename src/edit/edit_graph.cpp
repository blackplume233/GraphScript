#include "graphscript/edit/edit_graph.h"
#include "graphscript/registry/environment.h"
#include <algorithm>

namespace gs {

// Constructs an edit graph with optional environment and schema for validation.
EditGraph::EditGraph(const std::string& name, const Environment* env, const GraphSchema* schema)
    : name_(name), env_(env), schema_(schema) {}

// Adds a node instance; validates type exists and schema allows it (via allowed_node_tags).
Result<Handle, std::string> EditGraph::add_node(const std::string& type_name, const std::string& instance_name, const std::string& initializer) {
    if (!env_) return Result<Handle, std::string>::err("No environment set");

    auto* def = env_->nodes().find(type_name);
    if (!def) return Result<Handle, std::string>::err("Unknown node type: " + type_name);

    if (schema_ && !schema_->allowed_node_tags.empty()) {
        bool allowed = false;
        for (auto& tag : def->tags) {
            for (auto& allowed_tag : schema_->allowed_node_tags) {
                if (tag == allowed_tag) { allowed = true; break; }
            }
            if (allowed) break;
        }
        if (!allowed && !def->tags.empty()) {
            return Result<Handle, std::string>::err("Node type '" + type_name + "' not allowed by schema '" + schema_->name + "'");
        }
    }

    EditNode node;
    node.type_name = type_name;
    node.instance_name = instance_name;
    node.initializer = initializer;
    for (auto& pin_def : def->pins) {
        EditPin ep;
        ep.name = pin_def.name;
        ep.kind = pin_def.kind;
        ep.direction = pin_def.direction;
        ep.type_name = pin_def.type_name;
        node.pins.push_back(std::move(ep));
    }

    Handle h = nodes_.insert(std::move(node));
    return Result<Handle, std::string>::ok(h);
}

// Removes node and all connections involving it.
bool EditGraph::remove_node(Handle h) {
    // Remove all connections involving this node
    std::vector<Handle> to_remove;
    connections_.for_each([&](Handle ch, const EditConnection& conn) {
        if (conn.from_node == h || conn.to_node == h) {
            to_remove.push_back(ch);
        }
    });
    for (auto& ch : to_remove) {
        connections_.remove(ch);
    }
    return nodes_.remove(h);
}

EditNode* EditGraph::get_node(Handle h) { return nodes_.get(h); }
const EditNode* EditGraph::get_node(Handle h) const { return nodes_.get(h); }

void EditGraph::add_parameter(const GraphParameter& param) {
    parameters_.push_back(param);
}

Result<Handle, std::string> EditGraph::connect(Handle from_node, const std::string& from_pin, Handle to_node, const std::string& to_pin) {
    auto* fn = nodes_.get(from_node);
    if (!fn) return Result<Handle, std::string>::err("Source node not found");
    auto* tn = nodes_.get(to_node);
    if (!tn) return Result<Handle, std::string>::err("Target node not found");

    auto* fp = find_pin(*fn, from_pin);
    if (!fp) return Result<Handle, std::string>::err("Source pin '" + from_pin + "' not found on node '" + fn->instance_name + "'");
    auto* tp = find_pin(*tn, to_pin);
    if (!tp) return Result<Handle, std::string>::err("Target pin '" + to_pin + "' not found on node '" + tn->instance_name + "'");

    if (fp->kind != tp->kind) return Result<Handle, std::string>::err("Pin kind mismatch (exec vs data)");
    if (fp->direction != PinDirection::Output) return Result<Handle, std::string>::err("Source pin must be an output");
    if (tp->direction != PinDirection::Input) return Result<Handle, std::string>::err("Target pin must be an input");

    PinKind kind = fp->kind;

    if (kind == PinKind::Exec && schema_) {
        auto& policy = schema_->connection_policy;
        if (policy.max_exec_fan_out != -1) {
            int fan_out = exec_fan_out_count(from_node, from_pin);
            if (fan_out >= policy.max_exec_fan_out) {
                return Result<Handle, std::string>::err("Exec fan-out limit (" + std::to_string(policy.max_exec_fan_out) + ") exceeded");
            }
        }
        if (!policy.allow_exec_fan_in) {
            int fan_in = exec_fan_in_count(to_node, to_pin);
            if (fan_in > 0) {
                return Result<Handle, std::string>::err("Exec fan-in not allowed by schema");
            }
        }
    }

    if (kind == PinKind::Data && schema_ && schema_->connection_policy.strict_type_match) {
        if (!fp->type_name.empty() && !tp->type_name.empty() && fp->type_name != tp->type_name) {
            return Result<Handle, std::string>::err("Type mismatch: " + fp->type_name + " vs " + tp->type_name);
        }
    }

    EditConnection conn;
    conn.from_node = from_node;
    conn.from_pin = from_pin;
    conn.to_node = to_node;
    conn.to_pin = to_pin;
    conn.kind = kind;

    Handle h = connections_.insert(std::move(conn));
    return Result<Handle, std::string>::ok(h);
}

bool EditGraph::disconnect(Handle connection_handle) {
    return connections_.remove(connection_handle);
}

// Returns mutable/const connection by handle.
EditConnection* EditGraph::get_connection(Handle h) { return connections_.get(h); }
const EditConnection* EditGraph::get_connection(Handle h) const { return connections_.get(h); }

// Returns connections originating from the given node/pin.
std::vector<const EditConnection*> EditGraph::connections_from(Handle node_handle, const std::string& pin_name) const {
    std::vector<const EditConnection*> result;
    connections_.for_each([&](Handle, const EditConnection& conn) {
        if (conn.from_node == node_handle && conn.from_pin == pin_name)
            result.push_back(&conn);
    });
    return result;
}

// Returns connections targeting the given node/pin.
std::vector<const EditConnection*> EditGraph::connections_to(Handle node_handle, const std::string& pin_name) const {
    std::vector<const EditConnection*> result;
    connections_.for_each([&](Handle, const EditConnection& conn) {
        if (conn.to_node == node_handle && conn.to_pin == pin_name)
            result.push_back(&conn);
    });
    return result;
}

// Counts exec connections from this node/pin (for max_exec_fan_out policy).
int EditGraph::exec_fan_out_count(Handle node_handle, const std::string& pin_name) const {
    int count = 0;
    connections_.for_each([&](Handle, const EditConnection& conn) {
        if (conn.from_node == node_handle && conn.from_pin == pin_name && conn.kind == PinKind::Exec)
            count++;
    });
    return count;
}

// Counts exec connections to this node/pin (for allow_exec_fan_in policy).
int EditGraph::exec_fan_in_count(Handle node_handle, const std::string& pin_name) const {
    int count = 0;
    connections_.for_each([&](Handle, const EditConnection& conn) {
        if (conn.to_node == node_handle && conn.to_pin == pin_name && conn.kind == PinKind::Exec)
            count++;
    });
    return count;
}

// Returns node types available for this graph; filtered by schema allowed_node_tags when set.
std::vector<const NodeDefinition*> EditGraph::available_node_types() const {
    if (!env_) return {};
    auto all = env_->nodes().all();
    if (!schema_ || schema_->allowed_node_tags.empty()) return all;

    std::vector<const NodeDefinition*> filtered;
    for (auto* def : all) {
        if (def->tags.empty()) {
            // Nodes without tags are allowed unless schema has tag restrictions
            continue;
        }
        bool match = false;
        for (auto& tag : def->tags) {
            for (auto& allowed : schema_->allowed_node_tags) {
                if (tag == allowed) { match = true; break; }
            }
            if (match) break;
        }
        if (match) filtered.push_back(def);
    }
    return filtered;
}

// Runs validation: duplicate connections, dangling references. Required events not yet checked.
std::vector<Diagnostic> EditGraph::validate() const {
    std::vector<Diagnostic> diags;

    auto connection_kind = [](PinKind kind) {
        return kind == PinKind::Exec ? "exec" : "data";
    };

    auto handle_ref = [](Handle h) {
        return std::to_string(h.index) + ":" + std::to_string(h.generation);
    };

    auto connection_target = [&](const EditConnection& conn, bool from_side) {
        DiagnosticTarget target;
        target.graph = name_;
        target.connection_kind = connection_kind(conn.kind);
        const auto* node = from_side ? get_node(conn.from_node) : get_node(conn.to_node);
        if (node) target.node_instance = node->instance_name;
        target.pin_name = from_side ? conn.from_pin : conn.to_pin;
        target.reference = handle_ref(from_side ? conn.from_node : conn.to_node);
        return target;
    };

    // Check required events
    if (schema_) {
        for (auto& req_event : schema_->required_events) {
            bool found = false;
            // We don't track events in EditGraph directly — this would typically be checked on the source Graph
            // For now, skip this check at the EditGraph level
            (void)req_event;
            (void)found;
        }
    }

    // Check for duplicate connections
    connections_.for_each([&](Handle, const EditConnection& a) {
        int count = 0;
        connections_.for_each([&](Handle, const EditConnection& b) {
            if (a.from_node == b.from_node && a.from_pin == b.from_pin &&
                a.to_node == b.to_node && a.to_pin == b.to_pin) {
                count++;
            }
        });
        if (count > 1) {
            auto target = connection_target(a, true);
            diags.push_back({
                Severity::Error,
                "Duplicate connection detected",
                target.node_instance,
                "GS_GRAPH_DUPLICATE_CONNECTION",
                {},
                "Remove one duplicate edge from this pin.",
                target
            });
        }
    });

    // Check that all connections reference valid nodes
    connections_.for_each([&](Handle, const EditConnection& conn) {
        if (!nodes_.contains(conn.from_node)) {
            auto target = connection_target(conn, true);
            diags.push_back({
                Severity::Error,
                "Connection references non-existent source node",
                target.reference,
                "GS_GRAPH_DANGLING_SOURCE_NODE",
                {},
                "Remove the dangling connection or reconnect it to an existing node.",
                target
            });
        }
        if (!nodes_.contains(conn.to_node)) {
            auto target = connection_target(conn, false);
            diags.push_back({
                Severity::Error,
                "Connection references non-existent target node",
                target.reference,
                "GS_GRAPH_DANGLING_TARGET_NODE",
                {},
                "Remove the dangling connection or reconnect it to an existing node.",
                target
            });
        }
    });

    return diags;
}

// Static factory: converts compiled Graph to EditGraph, preserving parameters, nodes, and connections.
EditGraph EditGraph::build(const Graph& graph, const Environment& env) {
    const GraphSchema* schema = nullptr;
    if (graph.base_type) {
        schema = env.schemas().find(*graph.base_type);
    }

    EditGraph eg(graph.name, &env, schema);

    for (auto& p : graph.parameters) {
        eg.add_parameter(p);
    }

    // Add nodes: store mapping from instance_name → handle
    std::unordered_map<std::string, Handle> instance_handles;
    for (auto& ni : graph.node_instances) {
        auto result = eg.add_node(ni.type_name, ni.instance_name, ni.initializer);
        if (result.is_ok()) {
            instance_handles[ni.instance_name] = result.value();
        }
    }

    // Add connections from events and functions
    auto add_connections = [&](const LogicBlock& block) {
        for (auto& fc : block.flow_connections) {
            auto from_it = instance_handles.find(fc.from.node_instance);
            auto to_it = instance_handles.find(fc.to.node_instance);
            if (from_it != instance_handles.end() && to_it != instance_handles.end()) {
                eg.connect(from_it->second, fc.from.pin_name, to_it->second, fc.to.pin_name);
            }
        }
        for (auto& dl : block.data_links) {
            auto target_it = instance_handles.find(dl.target.node_instance);
            auto source_it = instance_handles.find(dl.source.node_instance);
            if (target_it != instance_handles.end() && source_it != instance_handles.end()) {
                eg.connect(source_it->second, dl.source.pin_name, target_it->second, dl.target.pin_name);
            }
        }
    };

    for (auto& ev : graph.events) add_connections(ev);
    for (auto& fn : graph.functions) add_connections(fn);

    return eg;
}

// Helper: finds pin by name on an edit node.
const EditPin* EditGraph::find_pin(const EditNode& node, const std::string& pin_name) const {
    for (auto& p : node.pins) {
        if (p.name == pin_name) return &p;
    }
    return nullptr;
}

} // namespace gs
