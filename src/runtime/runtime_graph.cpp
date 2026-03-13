#include "graphscript/runtime/runtime_graph.h"
#include "graphscript/edit/edit_graph.h"
#include <unordered_map>

namespace gs {

// Looks up runtime node by instance name; returns nullptr if not found.
const RNode* RuntimeGraph::find_node(const std::string& instance_name) const {
    for (auto& n : nodes_) {
        if (n.instance_name == instance_name) return &n;
    }
    return nullptr;
}

// Returns pin index within node (0..pin_count-1), or 0xFF if not found.
uint8_t RuntimeGraph::find_pin_index(uint32_t node_index, const std::string& pin_name) const {
    if (node_index >= nodes_.size()) return 0xFF;
    auto& node = nodes_[node_index];
    for (uint32_t i = node.first_pin; i < node.first_pin + node.pin_count; i++) {
        if (pins_[i].name == pin_name) return static_cast<uint8_t>(i - node.first_pin);
    }
    return 0xFF;
}

// Converts EditGraph to flat RuntimeGraph; maps handles to indices, bakes flow and data edges.
RuntimeGraph RuntimeGraph::bake(const EditGraph& edit_graph) {
    RuntimeGraph rt;
    rt.name_ = edit_graph.name();

    if (edit_graph.schema()) {
        rt.domain_name_ = edit_graph.schema()->name;
    }

    // Map from Handle → node index
    std::unordered_map<uint32_t, uint32_t> handle_to_index;
    std::unordered_map<uint32_t, uint32_t> handle_gen;

    uint32_t node_idx = 0;
    edit_graph.for_each_node([&](Handle h, const EditNode& en) {
        RNode rn;
        rn.type_name = en.type_name;
        rn.instance_name = en.instance_name;
        rn.first_pin = static_cast<uint32_t>(rt.pins_.size());
        rn.pin_count = static_cast<uint32_t>(en.pins.size());

        for (auto& ep : en.pins) {
            RPin rp;
            rp.node_index = node_idx;
            rp.pin_index = static_cast<uint8_t>(&ep - &en.pins[0]);
            rp.kind = (ep.kind == PinKind::Exec) ? 0 : 1;
            rp.direction = (ep.direction == PinDirection::Input) ? 0 : 1;
            rp.name = ep.name;
            rp.type_name = ep.type_name;
            rt.pins_.push_back(std::move(rp));
        }

        handle_to_index[h.index] = node_idx;
        handle_gen[h.index] = h.generation;
        rt.nodes_.push_back(std::move(rn));
        node_idx++;
    });

    // Bake connections
    edit_graph.for_each_connection([&](Handle, const EditConnection& ec) {
        auto from_it = handle_to_index.find(ec.from_node.index);
        auto to_it = handle_to_index.find(ec.to_node.index);
        if (from_it == handle_to_index.end() || to_it == handle_to_index.end()) return;
        if (handle_gen[ec.from_node.index] != ec.from_node.generation) return;
        if (handle_gen[ec.to_node.index] != ec.to_node.generation) return;

        uint32_t from_idx = from_it->second;
        uint32_t to_idx = to_it->second;
        uint8_t from_pin = rt.find_pin_index(from_idx, ec.from_pin);
        uint8_t to_pin = rt.find_pin_index(to_idx, ec.to_pin);

        if (ec.kind == PinKind::Exec) {
            rt.flow_edges_.push_back({from_idx, from_pin, to_idx, to_pin});
        } else {
            rt.data_edges_.push_back({from_idx, from_pin, to_idx, to_pin});
        }
    });

    return rt;
}

} // namespace gs
