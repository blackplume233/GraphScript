#pragma once

#include <cstdint>

namespace gs {

/// 1-based line and column in source text.
struct SourceLocation {
    uint32_t line = 1;
    uint32_t column = 1;
};

/// Span from start to end location (inclusive).
struct SourceRange {
    SourceLocation start;
    SourceLocation end;
};

} // namespace gs
