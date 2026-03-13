#include <gtest/gtest.h>
#include "graphscript/schema/validator.h"

using namespace gs;

TEST(Diagnostic, SeverityValues) {
    Diagnostic warn{Severity::Warning, "unused node", "node_1"};
    EXPECT_EQ(warn.severity, Severity::Warning);
    EXPECT_EQ(warn.message, "unused node");

    Diagnostic err{Severity::Error, "type mismatch", "connection_3"};
    EXPECT_EQ(err.severity, Severity::Error);
}

TEST(Validator, PlaceholderReturnsEmpty) {
    // validate_common/validate_schema are placeholder stubs for now
    // They will be fully tested in Phase 6 when EditGraph is ready
}
