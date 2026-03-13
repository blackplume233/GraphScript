#include <gtest/gtest.h>
#include "graphscript/edit/edit_graph.h"
#include "graphscript/registry/environment.h"

using namespace gs;

static Environment make_env() {
    Environment env;
    env.types().register_type({"int", false});
    env.types().register_type({"float", false});
    env.types().register_type({"FString", false});

    NodeDefinition ps;
    ps.type_name = "PrintString";
    ps.is_native = true;
    ps.tags = {"common"};
    ps.pins = {
        {"enter",   PinKind::Exec, PinDirection::Input,  ""},
        {"exit",    PinKind::Exec, PinDirection::Output, ""},
        {"message", PinKind::Data, PinDirection::Input,  "FString"},
    };
    env.nodes().register_node(std::move(ps));

    NodeDefinition delay;
    delay.type_name = "Delay";
    delay.is_native = true;
    delay.tags = {"common"};
    delay.pins = {
        {"enter",     PinKind::Exec, PinDirection::Input,  ""},
        {"completed", PinKind::Exec, PinDirection::Output, ""},
        {"duration",  PinKind::Data, PinDirection::Input,  "float"},
    };
    env.nodes().register_node(std::move(delay));

    NodeDefinition htn_move;
    htn_move.type_name = "HTN_Move";
    htn_move.is_native = true;
    htn_move.tags = {"htn_task"};
    htn_move.pins = {
        {"enter",   PinKind::Exec, PinDirection::Input,  ""},
        {"success", PinKind::Exec, PinDirection::Output, ""},
        {"fail",    PinKind::Exec, PinDirection::Output, ""},
    };
    env.nodes().register_node(std::move(htn_move));

    // Schema for HTN
    GraphSchema htn;
    htn.name = "HTNGraph";
    htn.connection_policy.max_exec_fan_out = -1;
    htn.connection_policy.allow_exec_fan_in = false;
    htn.connection_policy.strict_type_match = true;
    htn.allowed_node_tags = {"htn_task", "common"};
    env.schemas().register_schema(std::move(htn));

    // Schema for Task (fan-out = 1)
    GraphSchema task;
    task.name = "TaskGraph";
    task.connection_policy.max_exec_fan_out = 1;
    task.connection_policy.allow_exec_fan_in = true;
    task.connection_policy.strict_type_match = false;
    task.allowed_node_tags = {"task", "common"};
    env.schemas().register_schema(std::move(task));

    return env;
}

TEST(EditGraph, AddAndGetNode) {
    auto env = make_env();
    EditGraph eg("Test", &env);

    auto result = eg.add_node("PrintString", "printer");
    ASSERT_TRUE(result.is_ok()) << result.error();
    Handle h = result.value();
    EXPECT_TRUE(h.valid());
    EXPECT_EQ(eg.node_count(), 1u);

    auto* node = eg.get_node(h);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type_name, "PrintString");
    EXPECT_EQ(node->instance_name, "printer");
    EXPECT_EQ(node->pins.size(), 3u);
}

TEST(EditGraph, AddUnknownNodeFails) {
    auto env = make_env();
    EditGraph eg("Test", &env);
    auto result = eg.add_node("NonExistent", "x");
    EXPECT_TRUE(result.is_err());
}

TEST(EditGraph, RemoveNode) {
    auto env = make_env();
    EditGraph eg("Test", &env);
    auto h = eg.add_node("PrintString", "p").value();
    EXPECT_EQ(eg.node_count(), 1u);
    EXPECT_TRUE(eg.remove_node(h));
    EXPECT_EQ(eg.node_count(), 0u);
    EXPECT_EQ(eg.get_node(h), nullptr);
}

TEST(EditGraph, RemoveNodeRemovesConnections) {
    auto env = make_env();
    EditGraph eg("Test", &env);
    auto h1 = eg.add_node("PrintString", "a").value();
    auto h2 = eg.add_node("Delay", "b").value();
    eg.connect(h1, "exit", h2, "enter");
    EXPECT_EQ(eg.connection_count(), 1u);
    eg.remove_node(h1);
    EXPECT_EQ(eg.connection_count(), 0u);
}

TEST(EditGraph, ConnectExecPins) {
    auto env = make_env();
    EditGraph eg("Test", &env);
    auto h1 = eg.add_node("PrintString", "a").value();
    auto h2 = eg.add_node("Delay", "b").value();
    auto result = eg.connect(h1, "exit", h2, "enter");
    ASSERT_TRUE(result.is_ok()) << result.error();
    EXPECT_EQ(eg.connection_count(), 1u);
}

TEST(EditGraph, ConnectInvalidPinFails) {
    auto env = make_env();
    EditGraph eg("Test", &env);
    auto h1 = eg.add_node("PrintString", "a").value();
    auto h2 = eg.add_node("Delay", "b").value();
    auto result = eg.connect(h1, "nonexistent", h2, "enter");
    EXPECT_TRUE(result.is_err());
}

TEST(EditGraph, ConnectWrongDirectionFails) {
    auto env = make_env();
    EditGraph eg("Test", &env);
    auto h1 = eg.add_node("PrintString", "a").value();
    auto h2 = eg.add_node("Delay", "b").value();
    // enter is input, enter is input — should fail
    auto result = eg.connect(h1, "enter", h2, "enter");
    EXPECT_TRUE(result.is_err());
}

TEST(EditGraph, ConnectKindMismatchFails) {
    auto env = make_env();
    EditGraph eg("Test", &env);
    auto h1 = eg.add_node("PrintString", "a").value();
    auto h2 = eg.add_node("Delay", "b").value();
    // exit (exec out) → duration (data in) = kind mismatch
    auto result = eg.connect(h1, "exit", h2, "duration");
    EXPECT_TRUE(result.is_err());
}

TEST(EditGraph, Disconnect) {
    auto env = make_env();
    EditGraph eg("Test", &env);
    auto h1 = eg.add_node("PrintString", "a").value();
    auto h2 = eg.add_node("Delay", "b").value();
    auto ch = eg.connect(h1, "exit", h2, "enter").value();
    EXPECT_EQ(eg.connection_count(), 1u);
    EXPECT_TRUE(eg.disconnect(ch));
    EXPECT_EQ(eg.connection_count(), 0u);
}

TEST(EditGraph, TaskSchemaFanOutLimit) {
    auto env = make_env();
    auto* schema = env.schemas().find("TaskGraph");
    EditGraph eg("Test", &env, schema);

    auto h1 = eg.add_node("PrintString", "a").value();
    auto h2 = eg.add_node("Delay", "b").value();
    auto h3 = eg.add_node("PrintString", "c").value();

    auto r1 = eg.connect(h1, "exit", h2, "enter");
    ASSERT_TRUE(r1.is_ok()) << r1.error();

    // Second connection from same exec out should fail (fan-out = 1)
    auto r2 = eg.connect(h1, "exit", h3, "enter");
    EXPECT_TRUE(r2.is_err());
}

TEST(EditGraph, HTNSchemaUnlimitedFanOut) {
    auto env = make_env();
    auto* schema = env.schemas().find("HTNGraph");
    EditGraph eg("Test", &env, schema);

    auto h1 = eg.add_node("HTN_Move", "m").value();
    auto h2 = eg.add_node("PrintString", "a").value();
    auto h3 = eg.add_node("Delay", "b").value();

    auto r1 = eg.connect(h1, "success", h2, "enter");
    ASSERT_TRUE(r1.is_ok()) << r1.error();

    auto r2 = eg.connect(h1, "success", h3, "enter");
    ASSERT_TRUE(r2.is_ok()) << r2.error();
}

TEST(EditGraph, HTNSchemaNoExecFanIn) {
    auto env = make_env();
    auto* schema = env.schemas().find("HTNGraph");
    EditGraph eg("Test", &env, schema);

    auto h1 = eg.add_node("HTN_Move", "m1").value();
    auto h2 = eg.add_node("HTN_Move", "m2").value();
    auto h3 = eg.add_node("PrintString", "target").value();

    auto r1 = eg.connect(h1, "success", h3, "enter");
    ASSERT_TRUE(r1.is_ok()) << r1.error();

    // Fan-in not allowed: second connection to same exec input
    auto r2 = eg.connect(h2, "success", h3, "enter");
    EXPECT_TRUE(r2.is_err());
}

TEST(EditGraph, StrictTypeMatchEnforced) {
    auto env = make_env();
    auto* schema = env.schemas().find("HTNGraph");

    // Add nodes with differently-typed data pins
    NodeDefinition typed_out;
    typed_out.type_name = "IntSource";
    typed_out.is_native = true;
    typed_out.tags = {"common"};
    typed_out.pins = {{"value", PinKind::Data, PinDirection::Output, "int"}};
    env.nodes().register_node(std::move(typed_out));

    NodeDefinition typed_in;
    typed_in.type_name = "FloatSink";
    typed_in.is_native = true;
    typed_in.tags = {"common"};
    typed_in.pins = {{"value", PinKind::Data, PinDirection::Input, "float"}};
    env.nodes().register_node(std::move(typed_in));

    EditGraph eg("Test", &env, schema);
    auto h1 = eg.add_node("IntSource", "src").value();
    auto h2 = eg.add_node("FloatSink", "sink").value();

    auto result = eg.connect(h1, "value", h2, "value");
    EXPECT_TRUE(result.is_err());
}

TEST(EditGraph, NodeFilterBySchema) {
    auto env = make_env();
    auto* schema = env.schemas().find("HTNGraph");
    EditGraph eg("Test", &env, schema);

    auto available = eg.available_node_types();
    // Should include HTN_Move (htn_task) and PrintString/Delay (common)
    bool found_htn = false, found_ps = false;
    for (auto* n : available) {
        if (n->type_name == "HTN_Move") found_htn = true;
        if (n->type_name == "PrintString") found_ps = true;
    }
    EXPECT_TRUE(found_htn);
    EXPECT_TRUE(found_ps);
}

TEST(EditGraph, ValidateEmpty) {
    auto env = make_env();
    EditGraph eg("Test", &env);
    auto diags = eg.validate();
    EXPECT_TRUE(diags.empty());
}

TEST(EditGraph, Parameters) {
    auto env = make_env();
    EditGraph eg("Test", &env);
    eg.add_parameter({"hp", "int", ParamDirection::In, ""});
    eg.add_parameter({"result", "int", ParamDirection::Out, ""});
    EXPECT_EQ(eg.parameters().size(), 2u);
}

TEST(EditGraph, ForEachNode) {
    auto env = make_env();
    EditGraph eg("Test", &env);
    eg.add_node("PrintString", "a");
    eg.add_node("Delay", "b");

    int count = 0;
    eg.for_each_node([&](Handle, const EditNode&) { count++; });
    EXPECT_EQ(count, 2);
}

TEST(EditGraph, ConnectionQuery) {
    auto env = make_env();
    EditGraph eg("Test", &env);
    auto h1 = eg.add_node("PrintString", "a").value();
    auto h2 = eg.add_node("Delay", "b").value();
    eg.connect(h1, "exit", h2, "enter");

    auto from = eg.connections_from(h1, "exit");
    ASSERT_EQ(from.size(), 1u);
    EXPECT_EQ(from[0]->to_node, h2);

    auto to = eg.connections_to(h2, "enter");
    ASSERT_EQ(to.size(), 1u);
    EXPECT_EQ(to[0]->from_node, h1);
}
