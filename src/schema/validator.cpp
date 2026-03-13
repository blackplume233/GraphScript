#include "graphscript/schema/validator.h"

namespace gs {

// Placeholder: common validation (orphan nodes, cycles, etc.). Phase 6.
std::vector<Diagnostic> validate_common(const EditGraph& /*graph*/) {
    // Placeholder — will be implemented in Phase 6
    return {};
}

// Placeholder: schema-specific validation (required events, tags, etc.). Phase 6.
std::vector<Diagnostic> validate_schema(const EditGraph& /*graph*/, const GraphSchema& /*schema*/) {
    // Placeholder — will be implemented in Phase 6
    return {};
}

} // namespace gs
