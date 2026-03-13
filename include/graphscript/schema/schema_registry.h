#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <string_view>

#include "graphscript/schema/graph_schema.h"

namespace gs {

/// Registry of named GraphSchema instances.
class SchemaRegistry {
public:
    /// Registers a schema by name. Overwrites if name already exists.
    void register_schema(GraphSchema schema);
    /// Returns the schema with the given name, or nullptr if not found.
    const GraphSchema* find(std::string_view name) const;
    /// Returns all registered schemas in unspecified order.
    std::vector<const GraphSchema*> all() const;

private:
    std::unordered_map<std::string, GraphSchema> schemas_;
};

} // namespace gs
