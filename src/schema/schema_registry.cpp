#include "graphscript/schema/schema_registry.h"

namespace gs {

// Registers a graph schema by name; overwrites if name already exists.
void SchemaRegistry::register_schema(GraphSchema schema) {
    auto name = schema.name;
    schemas_.emplace(std::move(name), std::move(schema));
}

// Looks up schema by name; returns nullptr if not found.
const GraphSchema* SchemaRegistry::find(std::string_view name) const {
    auto it = schemas_.find(std::string(name));
    if (it == schemas_.end()) return nullptr;
    return &it->second;
}

// Returns all registered schemas.
std::vector<const GraphSchema*> SchemaRegistry::all() const {
    std::vector<const GraphSchema*> result;
    result.reserve(schemas_.size());
    for (auto& [_, s] : schemas_) {
        result.push_back(&s);
    }
    return result;
}

} // namespace gs
