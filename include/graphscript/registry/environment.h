#pragma once

#include "graphscript/core/types.h"
#include "graphscript/core/node.h"
#include "graphscript/schema/schema_registry.h"

namespace gs {

/// Central registry for types, node definitions, and schemas.
class Environment {
public:
    TypeRegistry&   types()   { return types_; }
    NodeRegistry&   nodes()   { return nodes_; }
    SchemaRegistry& schemas() { return schemas_; }

    const TypeRegistry&   types()   const { return types_; }
    const NodeRegistry&   nodes()   const { return nodes_; }
    const SchemaRegistry& schemas() const { return schemas_; }

private:
    TypeRegistry   types_;
    NodeRegistry   nodes_;
    SchemaRegistry schemas_;
};

} // namespace gs
