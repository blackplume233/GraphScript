#include "graphscript/graph/runtime_ir.h"

#include "graphscript/registry/environment.h"

#include <unordered_map>
#include <unordered_set>

namespace gs {

namespace {

struct Endpoint {
    std::string node;
    std::string pin;
};

Endpoint parse_endpoint(const std::string& text) {
    const auto dot = text.find('.');
    if (dot == std::string::npos) return {text, ""};
    return {text.substr(0, dot), text.substr(dot + 1)};
}

uint8_t pin_kind_value(const std::string& kind) {
    return kind == "exec" ? 0 : 1;
}

uint8_t pin_direction_value(const std::string& direction) {
    return direction == "in" ? 0 : 1;
}

uint8_t pin_kind_value(PinKind kind) {
    return kind == PinKind::Exec ? 0 : 1;
}

uint8_t pin_direction_value(PinDirection direction) {
    return direction == PinDirection::Input ? 0 : 1;
}

} // namespace

const RuntimeIRNode* GraphRuntimeIR::find_node(const std::string& instance_name) const {
    for (const auto& n : nodes_) {
        if (n.instance_name == instance_name) return &n;
    }
    return nullptr;
}

uint32_t GraphRuntimeIR::find_pin_index(uint32_t node_index, const std::string& pin_name) const {
    if (node_index >= nodes_.size()) return invalid_pin_index;
    auto& node = nodes_[node_index];
    for (uint32_t i = node.first_pin; i < node.first_pin + node.pin_count; i++) {
        if (pins_[i].name == pin_name) return i - node.first_pin;
    }
    return invalid_pin_index;
}

GraphRuntimeIR GraphRuntimeIR::bake_impl(const asset::FlowGraph& graph, const Environment* env) {
    GraphRuntimeIR rt;
    rt.name_ = graph.name;
    rt.domain_name_ = graph.schema;

    std::unordered_map<std::string, uint32_t> alias_to_index;
    auto append_pin = [&](uint32_t node_idx,
                          uint32_t pin_idx,
                          uint8_t kind,
                          uint8_t direction,
                          const std::string& name,
                          const std::string& type_name) {
        RuntimeIRPin rp;
        rp.node_index = node_idx;
        rp.pin_index = pin_idx;
        rp.kind = kind;
        rp.direction = direction;
        rp.name = name;
        rp.type_name = type_name;
        rt.pins_.push_back(std::move(rp));
    };

    for (uint32_t node_idx = 0; node_idx < graph.nodes.size(); ++node_idx) {
        const auto& en = graph.nodes[node_idx];
        RuntimeIRNode rn;
        rn.type_name = en.type;
        rn.instance_name = en.alias;
        rn.first_pin = static_cast<uint32_t>(rt.pins_.size());
        rn.pin_count = 0;

        std::unordered_set<std::string> projected_pin_names;
        for (const auto& ep : en.pins) {
            const auto pin_idx = rn.pin_count++;
            projected_pin_names.insert(ep.name);
            append_pin(node_idx,
                       pin_idx,
                       pin_kind_value(ep.kind),
                       pin_direction_value(ep.direction),
                       ep.name,
                       ep.type);
        }

        if (env) {
            if (const auto* def = env->nodes().find(en.type)) {
                for (const auto& ep : def->pins) {
                    if (projected_pin_names.count(ep.name)) continue;
                    const auto pin_idx = rn.pin_count++;
                    append_pin(node_idx,
                               pin_idx,
                               pin_kind_value(ep.kind),
                               pin_direction_value(ep.direction),
                               ep.name,
                               ep.type_name);
                }
            }
        }

        alias_to_index[en.alias] = node_idx;
        rt.nodes_.push_back(std::move(rn));
    }

    for (const auto& edge : graph.edges) {
        const auto from = parse_endpoint(edge.from);
        const auto to = parse_endpoint(edge.to);
        const auto from_it = alias_to_index.find(from.node);
        const auto to_it = alias_to_index.find(to.node);
        if (from_it == alias_to_index.end() || to_it == alias_to_index.end()) continue;
        const uint32_t from_idx = from_it->second;
        const uint32_t to_idx = to_it->second;
        const uint32_t from_pin = rt.find_pin_index(from_idx, from.pin);
        const uint32_t to_pin = rt.find_pin_index(to_idx, to.pin);
        if (from_pin == invalid_pin_index || to_pin == invalid_pin_index) continue;
        rt.flow_edges_.push_back({from_idx, from_pin, to_idx, to_pin});
    }

    for (const auto& edge : graph.data_edges) {
        const auto source = parse_endpoint(edge.source);
        const auto target = parse_endpoint(edge.target);
        const auto source_it = alias_to_index.find(source.node);
        const auto target_it = alias_to_index.find(target.node);
        if (source_it == alias_to_index.end() || target_it == alias_to_index.end()) continue;
        const uint32_t source_idx = source_it->second;
        const uint32_t target_idx = target_it->second;
        const uint32_t source_pin = rt.find_pin_index(source_idx, source.pin);
        const uint32_t target_pin = rt.find_pin_index(target_idx, target.pin);
        if (source_pin == invalid_pin_index || target_pin == invalid_pin_index) continue;
        rt.data_edges_.push_back({source_idx, source_pin, target_idx, target_pin});
    }

    return rt;
}

GraphRuntimeIR GraphRuntimeIR::bake(const asset::FlowGraph& graph) {
    return bake_impl(graph, nullptr);
}

GraphRuntimeIR GraphRuntimeIR::bake(const asset::FlowGraph& graph, const Environment& env) {
    return bake_impl(graph, &env);
}

} // namespace gs
