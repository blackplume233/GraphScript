#include "graphscript/graph/runtime_ir.h"

#include <unordered_map>

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

} // namespace

const RuntimeIRNode* GraphRuntimeIR::find_node(const std::string& instance_name) const {
    for (const auto& n : nodes_) {
        if (n.instance_name == instance_name) return &n;
    }
    return nullptr;
}

uint8_t GraphRuntimeIR::find_pin_index(uint32_t node_index, const std::string& pin_name) const {
    if (node_index >= nodes_.size()) return 0xFF;
    auto& node = nodes_[node_index];
    for (uint32_t i = node.first_pin; i < node.first_pin + node.pin_count; i++) {
        if (pins_[i].name == pin_name) return static_cast<uint8_t>(i - node.first_pin);
    }
    return 0xFF;
}

GraphRuntimeIR GraphRuntimeIR::bake(const asset::FlowGraph& graph) {
    GraphRuntimeIR rt;
    rt.name_ = graph.name;
    rt.domain_name_ = graph.schema;

    std::unordered_map<std::string, uint32_t> alias_to_index;

    for (uint32_t node_idx = 0; node_idx < graph.nodes.size(); ++node_idx) {
        const auto& en = graph.nodes[node_idx];
        RuntimeIRNode rn;
        rn.type_name = en.type;
        rn.instance_name = en.alias;
        rn.first_pin = static_cast<uint32_t>(rt.pins_.size());
        rn.pin_count = static_cast<uint32_t>(en.pins.size());

        for (uint32_t pin_idx = 0; pin_idx < en.pins.size(); ++pin_idx) {
            const auto& ep = en.pins[pin_idx];
            RuntimeIRPin rp;
            rp.node_index = node_idx;
            rp.pin_index = static_cast<uint8_t>(pin_idx);
            rp.kind = (ep.kind == "exec") ? 0 : 1;
            rp.direction = (ep.direction == "in") ? 0 : 1;
            rp.name = ep.name;
            rp.type_name = ep.type;
            rt.pins_.push_back(std::move(rp));
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
        rt.flow_edges_.push_back({from_idx, rt.find_pin_index(from_idx, from.pin), to_idx, rt.find_pin_index(to_idx, to.pin)});
    }

    for (const auto& edge : graph.data_edges) {
        const auto source = parse_endpoint(edge.source);
        const auto target = parse_endpoint(edge.target);
        const auto source_it = alias_to_index.find(source.node);
        const auto target_it = alias_to_index.find(target.node);
        if (source_it == alias_to_index.end() || target_it == alias_to_index.end()) continue;
        const uint32_t source_idx = source_it->second;
        const uint32_t target_idx = target_it->second;
        rt.data_edges_.push_back({source_idx, rt.find_pin_index(source_idx, source.pin), target_idx, rt.find_pin_index(target_idx, target.pin)});
    }

    return rt;
}

} // namespace gs
