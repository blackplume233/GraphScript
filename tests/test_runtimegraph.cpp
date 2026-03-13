#include <gtest/gtest.h>
#include "graphscript/runtime/runtime_graph.h"
#include "graphscript/edit/edit_graph.h"
#include "graphscript/registry/environment.h"

using namespace gs;

static Environment make_env() {
    Environment env;
    env.types().register_type({"FString", false});
    env.types().register_type({"float", false});

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

    GraphSchema task;
    task.name = "TaskGraph";
    task.connection_policy.max_exec_fan_out = 1;
    env.schemas().register_schema(std::move(task));

    return env;
}

TEST(RuntimeGraph, BakeEmpty) {
    auto env = make_env();
    EditGraph eg("Empty", &env);
    auto rt = RuntimeGraph::bake(eg);
    EXPECT_EQ(rt.name(), "Empty");
    EXPECT_EQ(rt.node_count(), 0u);
    EXPECT_EQ(rt.flow_edge_count(), 0u);
    EXPECT_EQ(rt.data_edge_count(), 0u);
}

TEST(RuntimeGraph, BakeWithNodes) {
    auto env = make_env();
    EditGraph eg("Test", &env);
    eg.add_node("PrintString", "a");
    eg.add_node("Delay", "b");

    auto rt = RuntimeGraph::bake(eg);
    EXPECT_EQ(rt.node_count(), 2u);
    EXPECT_EQ(rt.pins().size(), 6u); // 3 + 3
}

TEST(RuntimeGraph, BakePreservesNodeData) {
    auto env = make_env();
    EditGraph eg("Test", &env);
    eg.add_node("PrintString", "printer");

    auto rt = RuntimeGraph::bake(eg);
    auto* rn = rt.find_node("printer");
    ASSERT_NE(rn, nullptr);
    EXPECT_EQ(rn->type_name, "PrintString");
    EXPECT_EQ(rn->instance_name, "printer");
    EXPECT_EQ(rn->pin_count, 3u);
}

TEST(RuntimeGraph, BakeFlowEdges) {
    auto env = make_env();
    EditGraph eg("Test", &env);
    auto h1 = eg.add_node("PrintString", "a").value();
    auto h2 = eg.add_node("Delay", "b").value();
    eg.connect(h1, "exit", h2, "enter");

    auto rt = RuntimeGraph::bake(eg);
    EXPECT_EQ(rt.flow_edge_count(), 1u);
    auto& fe = rt.flow_edges()[0];
    EXPECT_EQ(rt.nodes()[fe.from_node].instance_name, "a");
    EXPECT_EQ(rt.nodes()[fe.to_node].instance_name, "b");
}

TEST(RuntimeGraph, BakeDataEdges) {
    auto env = make_env();

    NodeDefinition src;
    src.type_name = "DataSource";
    src.is_native = true;
    src.pins = {{"value", PinKind::Data, PinDirection::Output, "FString"}};
    env.nodes().register_node(std::move(src));

    EditGraph eg("Test", &env);
    auto h1 = eg.add_node("DataSource", "src").value();
    auto h2 = eg.add_node("PrintString", "printer").value();
    eg.connect(h1, "value", h2, "message");

    auto rt = RuntimeGraph::bake(eg);
    EXPECT_EQ(rt.data_edge_count(), 1u);
    auto& de = rt.data_edges()[0];
    EXPECT_EQ(rt.nodes()[de.source_node].instance_name, "src");
    EXPECT_EQ(rt.nodes()[de.target_node].instance_name, "printer");
}

TEST(RuntimeGraph, BakeWithSchema) {
    auto env = make_env();
    auto* schema = env.schemas().find("TaskGraph");
    EditGraph eg("TaskTest", &env, schema);
    eg.add_node("PrintString", "a");

    auto rt = RuntimeGraph::bake(eg);
    EXPECT_EQ(rt.domain_name(), "TaskGraph");
}

TEST(RuntimeGraph, FindNode) {
    auto env = make_env();
    EditGraph eg("Test", &env);
    eg.add_node("PrintString", "p");
    eg.add_node("Delay", "d");

    auto rt = RuntimeGraph::bake(eg);
    EXPECT_NE(rt.find_node("p"), nullptr);
    EXPECT_NE(rt.find_node("d"), nullptr);
    EXPECT_EQ(rt.find_node("nonexistent"), nullptr);
}

TEST(RuntimeGraph, FindPinIndex) {
    auto env = make_env();
    EditGraph eg("Test", &env);
    eg.add_node("PrintString", "p");

    auto rt = RuntimeGraph::bake(eg);
    auto* rn = rt.find_node("p");
    ASSERT_NE(rn, nullptr);
    uint32_t idx = static_cast<uint32_t>(rn - &rt.nodes()[0]);

    auto enter_idx = rt.find_pin_index(idx, "enter");
    EXPECT_NE(enter_idx, 0xFF);

    auto bad_idx = rt.find_pin_index(idx, "nonexistent");
    EXPECT_EQ(bad_idx, 0xFF);
}

TEST(RuntimeGraph, PinData) {
    auto env = make_env();
    EditGraph eg("Test", &env);
    eg.add_node("PrintString", "p");

    auto rt = RuntimeGraph::bake(eg);
    auto& pins = rt.pins();
    ASSERT_EQ(pins.size(), 3u);
    EXPECT_EQ(pins[0].name, "enter");
    EXPECT_EQ(pins[0].kind, 0);  // Exec
    EXPECT_EQ(pins[0].direction, 0);  // Input
    EXPECT_EQ(pins[1].name, "exit");
    EXPECT_EQ(pins[1].direction, 1);  // Output
    EXPECT_EQ(pins[2].name, "message");
    EXPECT_EQ(pins[2].kind, 1);  // Data
}
