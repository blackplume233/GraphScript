#include <gtest/gtest.h>
#include "graphscript/schema/schema_registry.h"

using namespace gs;

static GraphSchema make_htn_schema() {
    GraphSchema s;
    s.name = "HTNGraph";
    s.connection_policy.max_exec_fan_out  = -1;
    s.connection_policy.allow_exec_fan_in = false;
    s.connection_policy.strict_type_match = true;
    s.allowed_node_tags = {"htn_task", "htn_decorator", "htn_service", "common"};
    s.required_events = {"OnPlan"};
    return s;
}

static GraphSchema make_task_schema() {
    GraphSchema s;
    s.name = "TaskGraph";
    s.connection_policy.max_exec_fan_out  = 1;
    s.connection_policy.allow_exec_fan_in = true;
    s.connection_policy.strict_type_match = false;
    s.allowed_node_tags = {"task", "common"};
    return s;
}

TEST(SchemaRegistry, RegisterAndFind) {
    SchemaRegistry reg;
    reg.register_schema(make_htn_schema());

    auto* found = reg.find("HTNGraph");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "HTNGraph");
    EXPECT_EQ(found->connection_policy.max_exec_fan_out, -1);
    EXPECT_FALSE(found->connection_policy.allow_exec_fan_in);
}

TEST(SchemaRegistry, FindNonexistent) {
    SchemaRegistry reg;
    EXPECT_EQ(reg.find("Nope"), nullptr);
}

TEST(SchemaRegistry, UnregisterSchemaRemovesRegisteredSchema) {
    SchemaRegistry reg;
    reg.register_schema(make_htn_schema());

    EXPECT_TRUE(reg.unregister_schema("HTNGraph"));
    EXPECT_EQ(reg.find("HTNGraph"), nullptr);
    EXPECT_FALSE(reg.unregister_schema("HTNGraph"));
}

TEST(SchemaRegistry, MultipleSchemas) {
    SchemaRegistry reg;
    reg.register_schema(make_htn_schema());
    reg.register_schema(make_task_schema());

    auto all = reg.all();
    EXPECT_EQ(all.size(), 2u);

    EXPECT_NE(reg.find("HTNGraph"), nullptr);
    EXPECT_NE(reg.find("TaskGraph"), nullptr);
}

TEST(SchemaRegistry, AllowedNodeTags) {
    SchemaRegistry reg;
    reg.register_schema(make_htn_schema());

    auto* s = reg.find("HTNGraph");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->allowed_node_tags.size(), 4u);
    EXPECT_EQ(s->allowed_node_tags[0], "htn_task");
}

TEST(SchemaRegistry, RequiredEvents) {
    SchemaRegistry reg;
    reg.register_schema(make_htn_schema());

    auto* s = reg.find("HTNGraph");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->required_events.size(), 1u);
    EXPECT_EQ(s->required_events[0], "OnPlan");
}
