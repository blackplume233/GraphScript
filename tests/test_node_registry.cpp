#include <gtest/gtest.h>
#include "graphscript/core/node.h"

using namespace gs;

static NodeDefinition make_print_string() {
    NodeDefinition def;
    def.type_name = "PrintString";
    def.is_native = true;
    def.tags = {"common"};
    def.pins = {
        {"enter",   PinKind::Exec, PinDirection::Input,  ""},
        {"exit",    PinKind::Exec, PinDirection::Output, ""},
        {"message", PinKind::Data, PinDirection::Input,  "FString"},
    };
    return def;
}

TEST(NodeDefinition, FindPin) {
    auto def = make_print_string();
    auto* pin = def.find_pin("message");
    ASSERT_NE(pin, nullptr);
    EXPECT_EQ(pin->name, "message");
    EXPECT_EQ(pin->kind, PinKind::Data);
    EXPECT_EQ(pin->direction, PinDirection::Input);

    EXPECT_EQ(def.find_pin("nonexistent"), nullptr);
}

TEST(NodeDefinition, ExecInputs) {
    auto def = make_print_string();
    auto pins = def.exec_inputs();
    ASSERT_EQ(pins.size(), 1u);
    EXPECT_EQ(pins[0]->name, "enter");
}

TEST(NodeDefinition, ExecOutputs) {
    auto def = make_print_string();
    auto pins = def.exec_outputs();
    ASSERT_EQ(pins.size(), 1u);
    EXPECT_EQ(pins[0]->name, "exit");
}

TEST(NodeDefinition, DataInputs) {
    auto def = make_print_string();
    auto pins = def.data_inputs();
    ASSERT_EQ(pins.size(), 1u);
    EXPECT_EQ(pins[0]->name, "message");
}

TEST(NodeDefinition, DataOutputsEmpty) {
    auto def = make_print_string();
    EXPECT_TRUE(def.data_outputs().empty());
}

TEST(NodeRegistry, RegisterAndFind) {
    NodeRegistry reg;
    reg.register_node(make_print_string());

    auto* found = reg.find("PrintString");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->type_name, "PrintString");
    EXPECT_TRUE(found->is_native);
}

TEST(NodeRegistry, FindNonexistent) {
    NodeRegistry reg;
    EXPECT_EQ(reg.find("Nope"), nullptr);
}

TEST(NodeRegistry, RegisterGraphNode) {
    NodeRegistry reg;
    NodeDefinition def;
    def.type_name = "MySubGraph";
    def.is_native = true;
    def.source_graph = "MySubGraph";
    def.pins = {
        {"hp", PinKind::Data, PinDirection::Input, "int"},
    };

    reg.register_graph_node(std::move(def));

    auto* found = reg.find("MySubGraph");
    ASSERT_NE(found, nullptr);
    EXPECT_FALSE(found->is_native);
    EXPECT_EQ(found->source_graph, "MySubGraph");
}

TEST(NodeRegistry, UnregisterNodeOnlyRemovesNativeDefinitions) {
    NodeRegistry reg;
    reg.register_node(make_print_string());

    NodeDefinition graph_def;
    graph_def.type_name = "MySubGraph";
    graph_def.is_native = true;
    graph_def.source_graph = "MySubGraph";
    reg.register_graph_node(std::move(graph_def));

    EXPECT_TRUE(reg.unregister_node("PrintString"));
    EXPECT_EQ(reg.find("PrintString"), nullptr);

    EXPECT_FALSE(reg.unregister_node("MySubGraph"));
    auto* graph_node = reg.find("MySubGraph");
    ASSERT_NE(graph_node, nullptr);
    EXPECT_FALSE(graph_node->is_native);

    EXPECT_FALSE(reg.unregister_node("Missing"));
}

TEST(NodeRegistry, AllNodes) {
    NodeRegistry reg;
    reg.register_node(make_print_string());

    NodeDefinition delay;
    delay.type_name = "Delay";
    delay.is_native = true;
    delay.pins = {
        {"enter",     PinKind::Exec, PinDirection::Input,  ""},
        {"completed", PinKind::Exec, PinDirection::Output, ""},
        {"duration",  PinKind::Data, PinDirection::Input,  "float"},
    };
    reg.register_node(std::move(delay));

    auto all = reg.all();
    EXPECT_EQ(all.size(), 2u);
}
