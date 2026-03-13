#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>

#include "graphscript/schema/connection_policy.h"

namespace gs {

struct Diagnostic;
class EditGraph;

/// Schema defining validation rules and constraints for a graph.
struct GraphSchema {
    std::string                   name;
    ConnectionPolicy              connection_policy;
    std::vector<std::string>      allowed_node_tags;
    std::vector<std::string>      required_events;
};

} // namespace gs
