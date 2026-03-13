#include <gtest/gtest.h>
#include "graphscript/schema/connection_policy.h"

using namespace gs;

TEST(ConnectionPolicy, DefaultValues) {
    ConnectionPolicy policy;
    EXPECT_EQ(policy.max_exec_fan_out, 1);
    EXPECT_TRUE(policy.allow_exec_fan_in);
    EXPECT_FALSE(policy.strict_type_match);
}

TEST(ConnectionPolicy, HTNPolicy) {
    ConnectionPolicy htn;
    htn.max_exec_fan_out  = -1;  // unlimited
    htn.allow_exec_fan_in = false;
    htn.strict_type_match = true;

    EXPECT_EQ(htn.max_exec_fan_out, -1);
    EXPECT_FALSE(htn.allow_exec_fan_in);
    EXPECT_TRUE(htn.strict_type_match);
}

TEST(ConnectionPolicy, TaskPolicy) {
    ConnectionPolicy task;
    task.max_exec_fan_out  = 1;
    task.allow_exec_fan_in = true;
    task.strict_type_match = false;

    EXPECT_EQ(task.max_exec_fan_out, 1);
    EXPECT_TRUE(task.allow_exec_fan_in);
    EXPECT_FALSE(task.strict_type_match);
}

TEST(ConnectionPolicy, LevelScriptPolicy) {
    ConnectionPolicy ls;
    ls.max_exec_fan_out  = -1;
    ls.allow_exec_fan_in = true;
    ls.strict_type_match = false;

    EXPECT_EQ(ls.max_exec_fan_out, -1);
    EXPECT_TRUE(ls.allow_exec_fan_in);
    EXPECT_FALSE(ls.strict_type_match);
}
