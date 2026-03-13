#pragma once

#include <functional>
#include <string>
#include <optional>

namespace gs {

/// Connection rules for graph edges (exec and data pins).
struct ConnectionPolicy {
    /// Max exec edges from one output pin. -1 = unlimited.
    int  max_exec_fan_out   = 1;     // -1 = unlimited
    /// If true, multiple exec edges may target the same input pin.
    bool allow_exec_fan_in  = true;
    /// If true, data links require matching pin type names.
    bool strict_type_match  = false;
};

} // namespace gs
