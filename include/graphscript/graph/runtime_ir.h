#pragma once

#include "graphscript/asset/language.h"

#include <string>
#include <vector>
#include <cstdint>

namespace gs {

/// Runtime pin descriptor; indices refer into nodes_ and pins_ arrays.
struct RuntimeIRPin {
    uint32_t    node_index;
    uint8_t     pin_index;
    uint8_t     kind;       // 0=Exec, 1=Data
    uint8_t     direction;  // 0=Input, 1=Output
    std::string name;
    std::string type_name;
};

/// Runtime node descriptor; pins are stored contiguously from first_pin.
struct RuntimeIRNode {
    std::string type_name;
    std::string instance_name;
    uint32_t    first_pin;
    uint32_t    pin_count;
};

/// Exec flow edge (control flow between nodes).
struct RuntimeIRFlowEdge {
    uint32_t from_node;
    uint8_t  from_pin;
    uint32_t to_node;
    uint8_t  to_pin;
};

/// Data dependency edge.
struct RuntimeIRDataEdge {
    uint32_t source_node;
    uint8_t  source_pin;
    uint32_t target_node;
    uint8_t  target_pin;
};

/// Immutable graph runtime IR: flat arrays for fast traversal.
class GraphRuntimeIR {
public:
    const std::string& name() const { return name_; }
    const std::string& domain_name() const { return domain_name_; }

    const std::vector<RuntimeIRNode>&     nodes()      const { return nodes_; }
    const std::vector<RuntimeIRPin>&      pins()       const { return pins_; }
    const std::vector<RuntimeIRFlowEdge>& flow_edges() const { return flow_edges_; }
    const std::vector<RuntimeIRDataEdge>& data_edges() const { return data_edges_; }

    uint32_t node_count() const { return static_cast<uint32_t>(nodes_.size()); }
    uint32_t flow_edge_count() const { return static_cast<uint32_t>(flow_edges_.size()); }
    uint32_t data_edge_count() const { return static_cast<uint32_t>(data_edges_.size()); }

    /// Returns node by instance name, or nullptr if not found.
    const RuntimeIRNode* find_node(const std::string& instance_name) const;
    /// Returns pin index within node, or invalid index if not found.
    uint8_t find_pin_index(uint32_t node_index, const std::string& pin_name) const;

    /// Builds runtime IR from an asset graph projection.
    static GraphRuntimeIR bake(const asset::FlowGraph& graph);

private:
    std::string                         name_;
    std::string                         domain_name_;
    std::vector<RuntimeIRNode>          nodes_;
    std::vector<RuntimeIRPin>           pins_;
    std::vector<RuntimeIRFlowEdge>      flow_edges_;
    std::vector<RuntimeIRDataEdge>      data_edges_;
    std::vector<uint8_t>                domain_metadata_;
};

} // namespace gs
