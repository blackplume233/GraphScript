#include "graphscript/core/node.h"

namespace gs {

// Returns pin definition by name, or nullptr if not found.
const PinDefinition* NodeDefinition::find_pin(std::string_view name) const {
    for (auto& p : pins) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

// Returns all exec pins with input direction.
std::vector<const PinDefinition*> NodeDefinition::exec_inputs() const {
    std::vector<const PinDefinition*> result;
    for (auto& p : pins) {
        if (p.kind == PinKind::Exec && p.direction == PinDirection::Input)
            result.push_back(&p);
    }
    return result;
}

// Returns all exec pins with output direction.
std::vector<const PinDefinition*> NodeDefinition::exec_outputs() const {
    std::vector<const PinDefinition*> result;
    for (auto& p : pins) {
        if (p.kind == PinKind::Exec && p.direction == PinDirection::Output)
            result.push_back(&p);
    }
    return result;
}

// Returns all data pins with input direction.
std::vector<const PinDefinition*> NodeDefinition::data_inputs() const {
    std::vector<const PinDefinition*> result;
    for (auto& p : pins) {
        if (p.kind == PinKind::Data && p.direction == PinDirection::Input)
            result.push_back(&p);
    }
    return result;
}

// Returns all data pins with output direction.
std::vector<const PinDefinition*> NodeDefinition::data_outputs() const {
    std::vector<const PinDefinition*> result;
    for (auto& p : pins) {
        if (p.kind == PinKind::Data && p.direction == PinDirection::Output)
            result.push_back(&p);
    }
    return result;
}

// Registers a native node definition (is_native remains true).
void NodeRegistry::register_node(NodeDefinition def) {
    auto name = def.type_name;
    nodes_.emplace(std::move(name), std::move(def));
}

// Registers a graph-derived node; forces is_native = false.
void NodeRegistry::register_graph_node(NodeDefinition def) {
    def.is_native = false;
    auto name = def.type_name;
    auto it = nodes_.find(name);
    if (it != nodes_.end()) {
        if (!it->second.is_native) {
            it->second = std::move(def);
        }
        return;
    }
    nodes_.emplace(std::move(name), std::move(def));
}

// Unregisters a graph-derived node definition without touching native nodes.
void NodeRegistry::unregister_graph_node(std::string_view type_name) {
    auto it = nodes_.find(std::string(type_name));
    if (it != nodes_.end() && !it->second.is_native) {
        nodes_.erase(it);
    }
}

// Unregisters a native node definition without touching graph-derived nodes.
bool NodeRegistry::unregister_node(std::string_view type_name) {
    auto it = nodes_.find(std::string(type_name));
    if (it != nodes_.end() && it->second.is_native) {
        nodes_.erase(it);
        return true;
    }
    return false;
}

// Looks up node definition by type name; returns nullptr if not found.
const NodeDefinition* NodeRegistry::find(std::string_view type_name) const {
    auto it = nodes_.find(std::string(type_name));
    if (it == nodes_.end()) return nullptr;
    return &it->second;
}

// Returns all registered node definitions.
std::vector<const NodeDefinition*> NodeRegistry::all() const {
    std::vector<const NodeDefinition*> result;
    result.reserve(nodes_.size());
    for (auto& [_, def] : nodes_) {
        result.push_back(&def);
    }
    return result;
}

} // namespace gs
