// EditSession unit tests: graph management, CRUD, undo/redo, emit round-trip

#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "graphscript/compile/compiler.h"
#include "graphscript/edit/edit_session.h"
#include "graphscript/parse/lexer.h"
#include "graphscript/parse/parser.h"

using namespace gs;

static void load_core(EditSession& s) {
    std::string path = std::string(GS_PRESETS_DIR) + "/ue_core.d.gs";
    s.load_import(path);
}

// ═══════════════════════════════════════════════════════════════════
// Graph Management
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, NewGraphAndActive) {
    Environment env;
    EditSession s(env);

    auto r = s.new_graph("TestGraph");
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 0);
    EXPECT_EQ(s.active_index(), 0);
    EXPECT_NE(s.active_graph(), nullptr);
    EXPECT_EQ(s.active_graph()->name, "TestGraph");
}

TEST(EditSession, DuplicateGraphFails) {
    Environment env;
    EditSession s(env);
    s.new_graph("A");
    auto r = s.new_graph("A");
    EXPECT_TRUE(r.is_err());
}

TEST(EditSession, MultipleGraphsSwitching) {
    Environment env;
    EditSession s(env);
    s.new_graph("First");
    s.new_graph("Second");
    EXPECT_EQ(s.active_index(), 1);

    auto r = s.set_active("First");
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(s.active_index(), 0);
    EXPECT_EQ(s.active_graph()->name, "First");
}

TEST(EditSession, DeleteGraph) {
    Environment env;
    EditSession s(env);
    s.new_graph("ToDelete");
    s.new_graph("Keep");

    auto r = s.delete_graph("ToDelete");
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(s.module().graphs.size(), 1u);
    EXPECT_EQ(s.module().graphs[0].name, "Keep");
}

TEST(EditSession, RenameGraphMigratesGraphNodeReferencesAndKeepsPersistentId) {
    Environment env;
    EditSession s(env);
    load_core(s);

    const std::string source = R"([Id("graph-child-stable")]
Graph Child {
    in msg : FString;
    event Run {
    }
}
Graph Parent {
    Child child{};
    event OnStart {
        context.start(child.Run);
    }
}
)";
    auto loaded = s.load_source(source, "rename_graph.gs");
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    ASSERT_NE(s.env().nodes().find("Child"), nullptr);

    ASSERT_TRUE(s.rename_graph("Child", "Leaf").is_ok());

    ASSERT_EQ(s.module().graphs.size(), 2u);
    EXPECT_EQ(s.module().graphs[0].name, "Leaf");
    ASSERT_EQ(s.module().graphs[0].annotations.size(), 1u);
    EXPECT_EQ(s.module().graphs[0].annotations[0].args[0].value, "graph-child-stable");
    ASSERT_EQ(s.module().graphs[1].node_instances.size(), 1u);
    EXPECT_EQ(s.module().graphs[1].node_instances[0].type_name, "Leaf");
    EXPECT_EQ(s.active_index(), 0);
    EXPECT_NE(s.env().nodes().find("Leaf"), nullptr);

    const std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"id\":\"graph:Leaf\""), std::string::npos);
    EXPECT_EQ(json.find("\"id\":\"graph:Child\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"graph-child-stable\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"node:Parent/child\""), std::string::npos);
    EXPECT_NE(json.find("\"type\":\"Leaf\""), std::string::npos);

    const std::string emitted = s.emit();
    EXPECT_NE(emitted.find("[Id(\"graph-child-stable\")]"), std::string::npos);
    EXPECT_NE(emitted.find("Graph Leaf"), std::string::npos);
    EXPECT_NE(emitted.find("Leaf child{};"), std::string::npos);
    EXPECT_EQ(emitted.find("Graph Child"), std::string::npos);
    EXPECT_EQ(emitted.find("Child child{};"), std::string::npos);

    auto undo = s.undo();
    ASSERT_TRUE(undo.is_ok()) << undo.error();
    EXPECT_EQ(s.module().graphs[0].name, "Child");
    EXPECT_EQ(s.module().graphs[1].node_instances[0].type_name, "Child");
    auto redo = s.redo();
    ASSERT_TRUE(redo.is_ok()) << redo.error();
    EXPECT_EQ(s.module().graphs[0].name, "Leaf");
    EXPECT_EQ(s.module().graphs[1].node_instances[0].type_name, "Leaf");
}

TEST(EditSession, RenameGraphDerivedPinsMigratesCrossGraphReferencesAndKeepsPersistentIds) {
    Environment env;
    EditSession s(env);
    load_core(s);

    const std::string source = R"(Graph Child {
    [Id("param-msg-stable")]
    in msg : FString;
    out reply : FString;
    [Id("event-run-stable")]
    event Run {
    }
}
Graph Parent {
    in incoming : FString;
    Child child{};
    PrintString logger{};
    event OnStart {
        context.start(child.Run);
        child.msg = incoming;
        logger.message = child.reply;
    }
}
)";
    auto loaded = s.load_source(source, "rename_graph_pins.gs");
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    ASSERT_NE(s.env().nodes().find("Child"), nullptr);
    ASSERT_NE(s.env().nodes().find("Child")->find_pin("msg"), nullptr);
    ASSERT_NE(s.env().nodes().find("Child")->find_pin("Run"), nullptr);

    ASSERT_TRUE(s.rename_param("msg", "text").is_ok());

    const auto* child_after_param = s.env().nodes().find("Child");
    ASSERT_NE(child_after_param, nullptr);
    EXPECT_NE(child_after_param->find_pin("text"), nullptr);
    EXPECT_EQ(child_after_param->find_pin("msg"), nullptr);
    const auto& parent_after_param = s.module().graphs[1];
    ASSERT_EQ(parent_after_param.events[0].data_links.size(), 2u);
    EXPECT_EQ(parent_after_param.events[0].data_links[0].target.node_instance, "child");
    EXPECT_EQ(parent_after_param.events[0].data_links[0].target.pin_name, "text");
    EXPECT_EQ(parent_after_param.events[0].data_links[1].source.node_instance, "child");
    EXPECT_EQ(parent_after_param.events[0].data_links[1].source.pin_name, "reply");

    ASSERT_TRUE(s.rename_event("Run", "Execute").is_ok());

    const auto* child_after_event = s.env().nodes().find("Child");
    ASSERT_NE(child_after_event, nullptr);
    EXPECT_NE(child_after_event->find_pin("Execute"), nullptr);
    EXPECT_EQ(child_after_event->find_pin("Run"), nullptr);
    const auto& child = s.module().graphs[0];
    EXPECT_EQ(child.parameters[0].name, "text");
    EXPECT_EQ(child.parameters[0].annotations[0].args[0].value, "param-msg-stable");
    EXPECT_EQ(child.events[0].name, "Execute");
    EXPECT_EQ(child.events[0].annotations[0].args[0].value, "event-run-stable");
    const auto& parent = s.module().graphs[1];
    ASSERT_EQ(parent.events[0].flow_connections.size(), 1u);
    EXPECT_EQ(parent.events[0].flow_connections[0].to.node_instance, "child");
    EXPECT_EQ(parent.events[0].flow_connections[0].to.pin_name, "Execute");

    const std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"persistent_id\":\"param-msg-stable\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"event-run-stable\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"link:Parent/event/OnStart/incoming->child.text\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"flow:Parent/event/OnStart/context.start->child.Execute\""), std::string::npos);

    const std::string emitted = s.emit();
    EXPECT_NE(emitted.find("in text : FString;"), std::string::npos);
    EXPECT_NE(emitted.find("event Execute {"), std::string::npos);
    EXPECT_NE(emitted.find("context.start(child.Execute);"), std::string::npos);
    EXPECT_NE(emitted.find("child.text = incoming;"), std::string::npos);
    EXPECT_EQ(emitted.find("child.msg = incoming;"), std::string::npos);
    EXPECT_EQ(emitted.find("child.Run"), std::string::npos);

    auto undo_event = s.undo();
    ASSERT_TRUE(undo_event.is_ok()) << undo_event.error();
    EXPECT_EQ(s.module().graphs[0].events[0].name, "Run");
    EXPECT_EQ(s.module().graphs[1].events[0].flow_connections[0].to.pin_name, "Run");
    EXPECT_EQ(s.module().graphs[0].parameters[0].name, "text");
    auto undo_param = s.undo();
    ASSERT_TRUE(undo_param.is_ok()) << undo_param.error();
    EXPECT_EQ(s.module().graphs[0].parameters[0].name, "msg");
    EXPECT_EQ(s.module().graphs[1].events[0].data_links[0].target.pin_name, "msg");
}

TEST(EditSession, GraphInterfaceEditsRefreshGraphDerivedNodeDefinition) {
    Environment env;
    EditSession s(env);
    load_core(s);

    ASSERT_TRUE(s.new_graph("Child").is_ok());
    const auto* child_type = s.env().nodes().find("Child");
    ASSERT_NE(child_type, nullptr);
    EXPECT_TRUE(child_type->pins.empty());

    ASSERT_TRUE(s.add_param(ParamDirection::In, "msg", "FString").is_ok());
    ASSERT_TRUE(s.add_event("Run").is_ok());
    child_type = s.env().nodes().find("Child");
    ASSERT_NE(child_type, nullptr);
    EXPECT_NE(child_type->find_pin("msg"), nullptr);
    EXPECT_NE(child_type->find_pin("Run"), nullptr);

    ASSERT_TRUE(s.new_graph("Parent").is_ok());
    ASSERT_TRUE(s.add_node("Child", "child").is_ok());
    ASSERT_EQ(s.active_graph()->node_instances.size(), 1u);
    EXPECT_EQ(s.active_graph()->node_instances[0].type_name, "Child");

    ASSERT_TRUE(s.set_active("Child").is_ok());
    ASSERT_TRUE(s.remove_param("msg").is_ok());
    ASSERT_TRUE(s.remove_event("Run").is_ok());
    child_type = s.env().nodes().find("Child");
    ASSERT_NE(child_type, nullptr);
    EXPECT_EQ(child_type->find_pin("msg"), nullptr);
    EXPECT_EQ(child_type->find_pin("Run"), nullptr);

    auto undo_event = s.undo();
    ASSERT_TRUE(undo_event.is_ok()) << undo_event.error();
    child_type = s.env().nodes().find("Child");
    ASSERT_NE(child_type, nullptr);
    EXPECT_EQ(child_type->find_pin("msg"), nullptr);
    EXPECT_NE(child_type->find_pin("Run"), nullptr);

    auto undo_param = s.undo();
    ASSERT_TRUE(undo_param.is_ok()) << undo_param.error();
    child_type = s.env().nodes().find("Child");
    ASSERT_NE(child_type, nullptr);
    EXPECT_NE(child_type->find_pin("msg"), nullptr);
    EXPECT_NE(child_type->find_pin("Run"), nullptr);

    auto redo_param = s.redo();
    ASSERT_TRUE(redo_param.is_ok()) << redo_param.error();
    child_type = s.env().nodes().find("Child");
    ASSERT_NE(child_type, nullptr);
    EXPECT_EQ(child_type->find_pin("msg"), nullptr);
    EXPECT_NE(child_type->find_pin("Run"), nullptr);

    auto redo_event = s.redo();
    ASSERT_TRUE(redo_event.is_ok()) << redo_event.error();
    child_type = s.env().nodes().find("Child");
    ASSERT_NE(child_type, nullptr);
    EXPECT_EQ(child_type->find_pin("msg"), nullptr);
    EXPECT_EQ(child_type->find_pin("Run"), nullptr);
}

TEST(EditSession, NewGraphWithSchema) {
    Environment env;
    EditSession s(env);
    auto r = s.new_graph("HTNPlan", "HTNGraph");
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(s.active_graph()->base_type.has_value());
    EXPECT_EQ(*s.active_graph()->base_type, "HTNGraph");
}

// ═══════════════════════════════════════════════════════════════════
// Parameters
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, AddRemoveParams) {
    Environment env;
    EditSession s(env);
    s.new_graph("G");

    auto r1 = s.add_param(ParamDirection::In, "health", "int", "100");
    ASSERT_TRUE(r1.is_ok());
    auto r2 = s.add_param(ParamDirection::Out, "result", "bool");
    ASSERT_TRUE(r2.is_ok());
    EXPECT_EQ(s.active_graph()->parameters.size(), 2u);
    EXPECT_EQ(s.active_graph()->parameters[0].default_value, "100");

    auto r3 = s.remove_param("health");
    ASSERT_TRUE(r3.is_ok());
    EXPECT_EQ(s.active_graph()->parameters.size(), 1u);
    EXPECT_EQ(s.active_graph()->parameters[0].name, "result");
}

TEST(EditSession, DuplicateParamFails) {
    Environment env;
    EditSession s(env);
    s.new_graph("G");
    s.add_param(ParamDirection::In, "x", "int");
    auto r = s.add_param(ParamDirection::In, "x", "float");
    EXPECT_TRUE(r.is_err());
}

TEST(EditSession, SetParamDefaultUpdatesAndClearsValue) {
    Environment env;
    EditSession s(env);
    ASSERT_TRUE(s.new_graph("Defaults").is_ok());
    ASSERT_TRUE(s.add_param(ParamDirection::In, "speed", "float", "1.0").is_ok());

    auto set_default = s.set_param_default("speed", "2.5");
    ASSERT_TRUE(set_default.is_ok()) << set_default.error();
    ASSERT_EQ(s.active_graph()->parameters.size(), 1u);
    EXPECT_EQ(s.active_graph()->parameters[0].default_value, "2.5");
    EXPECT_NE(s.emit_active().find("in speed : float = 2.5;"), std::string::npos);

    auto non_constructor_arg = s.set_param_default_constructor_argument("speed", "3.5");
    EXPECT_TRUE(non_constructor_arg.is_err());

    auto set_constructor = s.set_param_default_constructor("speed", "SoftFloat", "3.5");
    ASSERT_TRUE(set_constructor.is_ok()) << set_constructor.error();
    EXPECT_EQ(s.active_graph()->parameters[0].default_value, "SoftFloat(3.5)");
    EXPECT_NE(s.emit_active().find("in speed : float = SoftFloat(3.5);"), std::string::npos);

    auto set_constructor_arg = s.set_param_default_constructor_argument("speed", "4.5");
    ASSERT_TRUE(set_constructor_arg.is_ok()) << set_constructor_arg.error();
    EXPECT_EQ(s.active_graph()->parameters[0].default_value, "SoftFloat(4.5)");

    auto set_constructor_type = s.set_param_default_constructor_type("speed", "PreciseFloat");
    ASSERT_TRUE(set_constructor_type.is_ok()) << set_constructor_type.error();
    EXPECT_EQ(s.active_graph()->parameters[0].default_value, "PreciseFloat(4.5)");
    EXPECT_NE(s.emit_active().find("in speed : float = PreciseFloat(4.5);"), std::string::npos);

    auto clear_default = s.set_param_default("speed");
    ASSERT_TRUE(clear_default.is_ok()) << clear_default.error();
    EXPECT_TRUE(s.active_graph()->parameters[0].default_value.empty());
    EXPECT_NE(s.emit_active().find("in speed : float;"), std::string::npos);

    auto invalid_type = s.set_param_default_constructor("speed", "123Bad", "1.0");
    EXPECT_TRUE(invalid_type.is_err());
    auto invalid_update_type = s.set_param_default_constructor_type("speed", "123Bad");
    EXPECT_TRUE(invalid_update_type.is_err());

    auto missing = s.set_param_default("missing", "1.0");
    EXPECT_TRUE(missing.is_err());
}

TEST(EditSession, SetParamTypeUpdatesGraphAndDerivedNodePin) {
    Environment env;
    EditSession s(env);
    ASSERT_TRUE(s.new_graph("TypedParam").is_ok());
    ASSERT_TRUE(s.add_param(ParamDirection::In, "speed", "float", "1.0").is_ok());

    auto set_type = s.set_param_type("speed", "double");
    ASSERT_TRUE(set_type.is_ok()) << set_type.error();
    ASSERT_EQ(s.active_graph()->parameters.size(), 1u);
    EXPECT_EQ(s.active_graph()->parameters[0].type_name, "double");
    EXPECT_NE(s.emit_active().find("in speed : double = 1.0;"), std::string::npos);

    const auto* graph_node = s.env().nodes().find("TypedParam");
    ASSERT_NE(graph_node, nullptr);
    const auto* speed_pin = graph_node->find_pin("speed");
    ASSERT_NE(speed_pin, nullptr);
    EXPECT_EQ(speed_pin->type_name, "double");

    auto no_op = s.set_param_type("speed", "double");
    EXPECT_TRUE(no_op.is_ok());
    auto invalid = s.set_param_type("speed", "123Bad");
    EXPECT_TRUE(invalid.is_err());
    auto missing = s.set_param_type("missing", "float");
    EXPECT_TRUE(missing.is_err());
}

// ═══════════════════════════════════════════════════════════════════
// Nodes
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, AddRemoveNodes) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("G");

    auto r1 = s.add_node("PrintString", "logger");
    ASSERT_TRUE(r1.is_ok());
    auto r2 = s.add_node("Delay", "timer");
    ASSERT_TRUE(r2.is_ok());
    EXPECT_EQ(s.active_graph()->node_instances.size(), 2u);

    auto r3 = s.remove_node("logger");
    ASSERT_TRUE(r3.is_ok());
    EXPECT_EQ(s.active_graph()->node_instances.size(), 1u);
}

TEST(EditSession, SetNodeInitializerFieldUpdatesAndAppendsFields) {
    Environment env;
    EditSession s(env);
    load_core(s);
    ASSERT_TRUE(s.new_graph("InitEdit").is_ok());
    ASSERT_TRUE(s.add_node("PrintString", "logger", "message = old").is_ok());

    auto update = s.set_node_initializer_field("logger", "message", "PreviewValue(\"updated\")");
    ASSERT_TRUE(update.is_ok()) << update.error();
    auto append = s.set_node_initializer_field("logger", "asset", "payload");
    ASSERT_TRUE(append.is_ok()) << append.error();
    auto set_constructor = s.set_node_initializer_constructor_field("logger", "asset", "SoftObjectPath", "payload");
    ASSERT_TRUE(set_constructor.is_ok()) << set_constructor.error();
    auto set_constructor_arg = s.set_node_initializer_constructor_argument("logger", "asset", "\"/Game/Asset\"");
    ASSERT_TRUE(set_constructor_arg.is_ok()) << set_constructor_arg.error();
    auto set_constructor_type = s.set_node_initializer_constructor_type("logger", "asset", "AssetRef");
    ASSERT_TRUE(set_constructor_type.is_ok()) << set_constructor_type.error();

    ASSERT_NE(s.active_graph(), nullptr);
    ASSERT_EQ(s.active_graph()->node_instances.size(), 1u);
    const auto& node = s.active_graph()->node_instances[0];
    EXPECT_EQ(node.initializer, "message = PreviewValue(\"updated\"), asset = AssetRef(\"/Game/Asset\")");
    ASSERT_EQ(node.initializer_fields.size(), 2u);
    EXPECT_EQ(node.initializer_fields[0].name, "message");
    EXPECT_EQ(node.initializer_fields[0].value, "PreviewValue(\"updated\")");
    EXPECT_EQ(node.initializer_fields[1].name, "asset");
    EXPECT_EQ(node.initializer_fields[1].value, "AssetRef(\"/Game/Asset\")");
    EXPECT_NE(s.emit().find("PrintString logger{message = PreviewValue(\"updated\"), asset = AssetRef(\"/Game/Asset\")};"), std::string::npos);
    EXPECT_TRUE(s.set_node_initializer_constructor_field("logger", "asset", "123Bad", "payload").is_err());
    EXPECT_TRUE(s.set_node_initializer_constructor_argument("logger", "missing", "seed").is_err());
    EXPECT_TRUE(s.set_node_initializer_constructor_type("logger", "asset", "123Bad").is_err());

    auto rename_message = s.rename_node_initializer_field("logger", "message", "text");
    ASSERT_TRUE(rename_message.is_ok()) << rename_message.error();
    EXPECT_EQ(node.initializer, "text = PreviewValue(\"updated\"), asset = AssetRef(\"/Game/Asset\")");
    ASSERT_EQ(node.initializer_fields.size(), 2u);
    EXPECT_EQ(node.initializer_fields[0].name, "text");
    EXPECT_EQ(node.initializer_fields[0].value, "PreviewValue(\"updated\")");
    EXPECT_TRUE(s.rename_node_initializer_field("logger", "text", "asset").is_err());
    EXPECT_TRUE(s.rename_node_initializer_field("logger", "missing", "other").is_err());

    auto remove_message = s.remove_node_initializer_field("logger", "text");
    ASSERT_TRUE(remove_message.is_ok()) << remove_message.error();
    EXPECT_EQ(node.initializer, "asset = AssetRef(\"/Game/Asset\")");
    ASSERT_EQ(node.initializer_fields.size(), 1u);
    EXPECT_EQ(node.initializer_fields[0].name, "asset");

    auto remove_asset = s.remove_node_initializer_field("logger", "asset");
    ASSERT_TRUE(remove_asset.is_ok()) << remove_asset.error();
    EXPECT_TRUE(node.initializer.empty());
    EXPECT_TRUE(node.initializer_fields.empty());
    EXPECT_TRUE(s.remove_node_initializer_field("logger", "missing").is_err());

    auto set_raw = s.set_node_initializer("logger", "Factory(seed)");
    ASSERT_TRUE(set_raw.is_ok()) << set_raw.error();
    EXPECT_EQ(node.initializer, "Factory(seed)");
    EXPECT_TRUE(node.initializer_fields.empty());
    EXPECT_NE(s.emit().find("PrintString logger{Factory(seed)};"), std::string::npos);

    auto set_assignment_list = s.set_node_initializer("logger", "message = restored");
    ASSERT_TRUE(set_assignment_list.is_ok()) << set_assignment_list.error();
    EXPECT_EQ(node.initializer, "message = restored");
    ASSERT_EQ(node.initializer_fields.size(), 1u);
    EXPECT_EQ(node.initializer_fields[0].name, "message");
    EXPECT_EQ(node.initializer_fields[0].value, "restored");

    auto clear = s.set_node_initializer("logger");
    ASSERT_TRUE(clear.is_ok()) << clear.error();
    EXPECT_TRUE(node.initializer.empty());
    EXPECT_TRUE(node.initializer_fields.empty());
    EXPECT_TRUE(s.set_node_initializer("missing", "Factory(seed)").is_err());
}

TEST(EditSession, UnknownNodeTypeFails) {
    Environment env;
    EditSession s(env);
    s.new_graph("G");
    auto r = s.add_node("NonExistent", "x");
    EXPECT_TRUE(r.is_err());
}

TEST(EditSession, RemoveNodeClearsConnections) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("G");
    s.add_node("PrintString", "p1");
    s.add_node("Delay", "d1");
    s.add_event("Ev");
    s.add_flow("Ev", "p1", "exit", "d1", "enter");
    s.add_link("Ev", "d1", "duration", "p1", "message");

    EXPECT_EQ(s.active_graph()->events[0].flow_connections.size(), 1u);
    EXPECT_EQ(s.active_graph()->events[0].data_links.size(), 1u);

    s.remove_node("p1");
    EXPECT_EQ(s.active_graph()->events[0].flow_connections.size(), 0u);
    EXPECT_EQ(s.active_graph()->events[0].data_links.size(), 0u);
}

// ═══════════════════════════════════════════════════════════════════
// Events & Functions
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, AddRemoveEventsAndFunctions) {
    Environment env;
    EditSession s(env);
    s.new_graph("G");

    s.add_event("OnStart");
    s.add_event("OnEnd");
    s.add_function("Calculate");
    EXPECT_EQ(s.active_graph()->events.size(), 2u);
    EXPECT_EQ(s.active_graph()->functions.size(), 1u);

    s.remove_event("OnEnd");
    EXPECT_EQ(s.active_graph()->events.size(), 1u);
    EXPECT_EQ(s.active_graph()->events[0].name, "OnStart");

    s.remove_function("Calculate");
    EXPECT_EQ(s.active_graph()->functions.size(), 0u);
}

// ═══════════════════════════════════════════════════════════════════
// Flow & Link
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, FlowAndLinkOperations) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("G");
    s.add_param(ParamDirection::In, "msg", "FString");
    s.add_node("PrintString", "p");
    s.add_node("Delay", "d");
    s.add_event("Ev");

    auto r1 = s.add_flow("Ev", "p", "exit", "d", "enter");
    ASSERT_TRUE(r1.is_ok());
    EXPECT_EQ(s.active_graph()->events[0].flow_connections.size(), 1u);

    auto r2 = s.add_link("Ev", "p", "message", "msg");
    ASSERT_TRUE(r2.is_ok());
    EXPECT_EQ(s.active_graph()->events[0].data_links.size(), 1u);
    EXPECT_TRUE(s.active_graph()->events[0].data_links[0].source.pin_name.empty());

    auto r3 = s.remove_flow("Ev", "p", "exit", "d", "enter");
    ASSERT_TRUE(r3.is_ok());
    EXPECT_EQ(s.active_graph()->events[0].flow_connections.size(), 0u);

    auto r4 = s.remove_link("Ev", "p", "message");
    ASSERT_TRUE(r4.is_ok());
    EXPECT_EQ(s.active_graph()->events[0].data_links.size(), 0u);
}

TEST(EditSession, FlowOnMissingBlockFails) {
    Environment env;
    EditSession s(env);
    s.new_graph("G");
    auto r = s.add_flow("NoSuchBlock", "a", "b", "c", "d");
    EXPECT_TRUE(r.is_err());
}

TEST(EditSession, FunctionCannotReferenceGraphNodes) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("G");
    s.add_param(ParamDirection::In, "health", "int");
    s.add_node("PrintString", "logger");
    s.add_function("MyFunc");

    // Function referencing a graph-level node instance must fail
    auto r1 = s.add_flow("MyFunc", "context", "start", "logger", "enter");
    EXPECT_TRUE(r1.is_err());
    EXPECT_NE(r1.error().find("cannot reference"), std::string::npos);

    auto r2 = s.add_link("MyFunc", "logger", "message", "health");
    EXPECT_TRUE(r2.is_err());
    EXPECT_NE(r2.error().find("cannot reference"), std::string::npos);

    // Function referencing context and params must succeed
    auto r3 = s.add_flow("MyFunc", "context", "start", "context", "done");
    EXPECT_TRUE(r3.is_ok());

    auto r4 = s.add_link("MyFunc", "context", "result", "health");
    EXPECT_TRUE(r4.is_ok());

    // Events can still reference graph nodes
    s.add_event("OnStart");
    auto r5 = s.add_flow("OnStart", "context", "start", "logger", "enter");
    EXPECT_TRUE(r5.is_ok());

    auto r6 = s.add_link("OnStart", "logger", "message", "health");
    EXPECT_TRUE(r6.is_ok());
}

TEST(EditSession, EventCannotReferenceDanglingNames) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("G");
    s.add_param(ParamDirection::In, "health", "int");
    s.add_node("PrintString", "logger");
    s.add_event("OnStart");

    // Event referencing valid entities: context, params, nodes → OK
    auto r1 = s.add_flow("OnStart", "context", "start", "logger", "enter");
    EXPECT_TRUE(r1.is_ok());

    auto r2 = s.add_link("OnStart", "logger", "message", "health");
    EXPECT_TRUE(r2.is_ok());

    // Event referencing a name that doesn't exist anywhere → fail
    auto r3 = s.add_flow("OnStart", "logger", "exit", "nonexistent", "enter");
    EXPECT_TRUE(r3.is_err());
    EXPECT_NE(r3.error().find("unknown reference"), std::string::npos);

    auto r4 = s.add_link("OnStart", "logger", "message", "ghost_param");
    EXPECT_TRUE(r4.is_err());
    EXPECT_NE(r4.error().find("unknown reference"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════
// Undo / Redo
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, UndoRedoBasic) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("G");
    s.add_node("PrintString", "p1");
    EXPECT_EQ(s.active_graph()->node_instances.size(), 1u);
    EXPECT_TRUE(s.can_undo());

    auto r1 = s.undo();
    ASSERT_TRUE(r1.is_ok());
    EXPECT_EQ(s.active_graph()->node_instances.size(), 0u);
    EXPECT_TRUE(s.can_redo());

    auto r2 = s.redo();
    ASSERT_TRUE(r2.is_ok());
    EXPECT_EQ(s.active_graph()->node_instances.size(), 1u);
}

TEST(EditSession, UndoRedoMultipleSteps) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("G");

    s.add_param(ParamDirection::In, "x", "int");
    s.add_node("PrintString", "p1");
    s.add_node("Delay", "d1");
    s.add_event("OnStart");
    EXPECT_EQ(s.active_graph()->node_instances.size(), 2u);
    EXPECT_EQ(s.active_graph()->events.size(), 1u);
    EXPECT_EQ(s.undo_history().size(), 4u);

    // Undo all 4 operations
    s.undo(); // undo add_event
    EXPECT_EQ(s.active_graph()->events.size(), 0u);
    s.undo(); // undo add_node d1
    EXPECT_EQ(s.active_graph()->node_instances.size(), 1u);
    s.undo(); // undo add_node p1
    EXPECT_EQ(s.active_graph()->node_instances.size(), 0u);
    s.undo(); // undo add_param
    EXPECT_EQ(s.active_graph()->parameters.size(), 0u);

    EXPECT_EQ(s.redo_history().size(), 4u);

    // Redo first 2
    s.redo(); // redo add_param
    EXPECT_EQ(s.active_graph()->parameters.size(), 1u);
    s.redo(); // redo add_node p1
    EXPECT_EQ(s.active_graph()->node_instances.size(), 1u);
}

TEST(EditSession, UndoEmptyFails) {
    Environment env;
    EditSession s(env);
    auto r = s.undo();
    EXPECT_TRUE(r.is_err());
}

TEST(EditSession, NewOperationClearsRedoStack) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("G");
    s.add_node("PrintString", "p1");
    s.undo();
    EXPECT_TRUE(s.can_redo());

    s.add_node("Delay", "d1");
    EXPECT_FALSE(s.can_redo());
}

// ═══════════════════════════════════════════════════════════════════
// Emit + Round-trip
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, EmitRoundTrip) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("Player");
    s.add_param(ParamDirection::In, "health", "int", "100");
    s.add_param(ParamDirection::Out, "alive", "bool");
    s.add_node("PrintString", "log");
    s.add_node("Delay", "wait");
    s.add_event("OnDamage");
    s.add_flow("OnDamage", "context", "start", "log", "enter");
    s.add_flow("OnDamage", "log", "exit", "wait", "enter");
    s.add_link("OnDamage", "log", "message", "health");

    std::string emitted = s.emit();
    ASSERT_FALSE(emitted.empty());
    EXPECT_NE(emitted.find("Graph Player"), std::string::npos);
    EXPECT_NE(emitted.find("in health : int = 100"), std::string::npos);
    EXPECT_NE(emitted.find("PrintString log"), std::string::npos);
    EXPECT_NE(emitted.find("event OnDamage"), std::string::npos);
    EXPECT_NE(emitted.find("log.message = health"), std::string::npos);

    // Re-parse the emitted text
    Lexer lexer(emitted);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto result = parser.parse();
    ASSERT_TRUE(result.is_ok()) << "Re-parse failed";

    Environment env2;
    EditSession s2(env2);
    load_core(s2);
    Compiler compiler(env2);
    auto mod2 = compiler.compile(*result.value());
    ASSERT_TRUE(mod2.is_ok());
    EXPECT_EQ(mod2.value().graphs.size(), 1u);
    EXPECT_EQ(mod2.value().graphs[0].name, "Player");
}

// ═══════════════════════════════════════════════════════════════════
// Generate block
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, GenerateBlock) {
    Environment env;
    EditSession s(env);
    s.new_graph("G");

    s.add_comment("desc", "Test graph");
    s.add_meta("position", "myNode", "x", "100");
    s.add_meta("position", "myNode", "y", "200");

    auto* g = s.active_graph();
    ASSERT_TRUE(g->generate.has_value());
    EXPECT_EQ(g->generate->comments.size(), 1u);
    EXPECT_EQ(g->generate->metadata.size(), 2u);
}

TEST(EditSession, StateJsonExportsGenerateSourceRanges) {
    Environment env;
    EditSession s(env);
    load_core(s);

    const std::string source = R"(Graph GenerateSpan {
    PrintString logger{};
    generate {
        Comment logger = "Legacy note";
        position:logger.x(SoftObjectPath("Generated"));
    }
}
)";
    auto loaded = s.load_source(source, "generate_span.gs");
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    ASSERT_EQ(s.module().graphs.size(), 1u);

    const auto& graph = s.module().graphs[0];
    ASSERT_TRUE(graph.generate.has_value());
    EXPECT_EQ(graph.generate->source_range.start.line, 3u);
    EXPECT_EQ(graph.generate->source_range.start.column, 5u);
    ASSERT_EQ(graph.generate->comments.size(), 1u);
    ASSERT_EQ(graph.generate->metadata.size(), 1u);
    EXPECT_EQ(graph.generate->comments[0].source_range.start.line, 4u);
    EXPECT_EQ(graph.generate->comments[0].source_range.start.column, 9u);
    EXPECT_EQ(graph.generate->comments[0].instance_name_range.start.line, 4u);
    EXPECT_EQ(graph.generate->comments[0].instance_name_range.start.column, 17u);
    EXPECT_EQ(graph.generate->comments[0].text_range.start.line, 4u);
    EXPECT_EQ(graph.generate->comments[0].text_range.start.column, 26u);
    EXPECT_EQ(graph.generate->metadata[0].source_range.start.line, 5u);
    EXPECT_EQ(graph.generate->metadata[0].source_range.start.column, 9u);
    EXPECT_EQ(graph.generate->metadata[0].scope_range.start.line, 5u);
    EXPECT_EQ(graph.generate->metadata[0].scope_range.start.column, 9u);
    EXPECT_EQ(graph.generate->metadata[0].node_range.start.line, 5u);
    EXPECT_EQ(graph.generate->metadata[0].node_range.start.column, 18u);
    EXPECT_EQ(graph.generate->metadata[0].property_range.start.line, 5u);
    EXPECT_EQ(graph.generate->metadata[0].property_range.start.column, 25u);
    EXPECT_EQ(graph.generate->metadata[0].value_range.start.line, 5u);
    EXPECT_EQ(graph.generate->metadata[0].value_range.start.column, 27u);
    EXPECT_EQ(graph.generate->metadata[0].value, "SoftObjectPath(\"Generated\")");
    EXPECT_EQ(graph.generate->metadata[0].value_constructor_range.start.line, 5u);
    EXPECT_EQ(graph.generate->metadata[0].value_constructor_range.start.column, 27u);
    EXPECT_EQ(graph.generate->metadata[0].value_constructor_type_range.start.line, 5u);
    EXPECT_EQ(graph.generate->metadata[0].value_constructor_type_range.start.column, 27u);
    EXPECT_EQ(graph.generate->metadata[0].value_constructor_arg_range.start.line, 5u);
    EXPECT_EQ(graph.generate->metadata[0].value_constructor_arg_range.start.column, 42u);

    std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"generate\":{\"source_range\":{\"start\":{\"line\":3,\"column\":5}"), std::string::npos);
    EXPECT_NE(json.find("\"comments\":[{\"id\":\"generate-comment:GenerateSpan/logger/Legacy note\",\"persistent_id\":\"\",\"instance\":\"logger\",\"text\":\"Legacy note\",\"source_range\":{\"start\":{\"line\":4,\"column\":9}"), std::string::npos);
    EXPECT_NE(json.find("\"value\":\"SoftObjectPath(\\\"Generated\\\")\""), std::string::npos);
    EXPECT_NE(json.find("\"value_constructor_source_range\":{\"start\":{\"line\":5,\"column\":27}"), std::string::npos);
    EXPECT_NE(json.find("\"value_constructor_type_source_range\":{\"start\":{\"line\":5,\"column\":27}"), std::string::npos);
    EXPECT_NE(json.find("\"value_constructor_arg_source_range\":{\"start\":{\"line\":5,\"column\":42}"), std::string::npos);
    EXPECT_NE(json.find("\"instance_source_range\":{\"start\":{\"line\":4,\"column\":17}"), std::string::npos);
    EXPECT_NE(json.find("\"text_source_range\":{\"start\":{\"line\":4,\"column\":26}"), std::string::npos);
    EXPECT_NE(json.find("\"metadata\":[{\"id\":\"generate-metadata:GenerateSpan/position/logger/x/SoftObjectPath(\\\"Generated\\\")\",\"persistent_id\":\"\",\"scope\":\"position\",\"node\":\"logger\",\"property\":\"x\",\"value\":\"SoftObjectPath(\\\"Generated\\\")\",\"source_range\":{\"start\":{\"line\":5,\"column\":9}"), std::string::npos);
    EXPECT_NE(json.find("\"scope_source_range\":{\"start\":{\"line\":5,\"column\":9}"), std::string::npos);
    EXPECT_NE(json.find("\"node_source_range\":{\"start\":{\"line\":5,\"column\":18}"), std::string::npos);
    EXPECT_NE(json.find("\"property_source_range\":{\"start\":{\"line\":5,\"column\":25}"), std::string::npos);
    EXPECT_NE(json.find("\"value_source_range\":{\"start\":{\"line\":5,\"column\":27}"), std::string::npos);
}

TEST(EditSession, GenerateAnnotationsRoundTripAndExportPersistentIds) {
    Environment env;
    EditSession s(env);
    load_core(s);

    const std::string source = R"(Graph GenerateAnnotated {
    PrintString logger{};
    generate {
        [Id("gen-comment-001")]
        Comment logger = "Legacy note";
        [PersistentId("gen-meta-001")]
        position:logger.x(100);
    }
}
)";
    auto loaded = s.load_source(source, "generate_annotated.gs");
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    ASSERT_EQ(s.module().graphs.size(), 1u);

    const auto& graph = s.module().graphs[0];
    ASSERT_TRUE(graph.generate.has_value());
    ASSERT_EQ(graph.generate->comments.size(), 1u);
    ASSERT_EQ(graph.generate->metadata.size(), 1u);
    ASSERT_EQ(graph.generate->comments[0].annotations.size(), 1u);
    EXPECT_EQ(graph.generate->comments[0].annotations[0].name, "Id");
    ASSERT_EQ(graph.generate->metadata[0].annotations.size(), 1u);
    EXPECT_EQ(graph.generate->metadata[0].annotations[0].name, "PersistentId");

    const std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"id\":\"generate-comment:GenerateAnnotated/logger/Legacy note\",\"persistent_id\":\"gen-comment-001\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"generate-metadata:GenerateAnnotated/position/logger/x/100\",\"persistent_id\":\"gen-meta-001\""), std::string::npos);
    EXPECT_NE(json.find("\"annotations\":[{\"name\":\"Id\""), std::string::npos);
    EXPECT_NE(json.find("\"annotations\":[{\"name\":\"PersistentId\""), std::string::npos);

    const std::string emitted = s.emit();
    EXPECT_NE(emitted.find("        [Id(\"gen-comment-001\")]"), std::string::npos);
    EXPECT_NE(emitted.find("        [PersistentId(\"gen-meta-001\")]"), std::string::npos);

    EditSession reparsed(env);
    load_core(reparsed);
    auto reloaded = reparsed.load_source(emitted, "generate_annotated_roundtrip.gs");
    ASSERT_TRUE(reloaded.is_ok()) << reloaded.error();
    const std::string reparsed_json = reparsed.state_to_json();
    EXPECT_NE(reparsed_json.find("\"persistent_id\":\"gen-comment-001\""), std::string::npos);
    EXPECT_NE(reparsed_json.find("\"persistent_id\":\"gen-meta-001\""), std::string::npos);
}

TEST(EditSession, GenerateAnnotationEditApiUpsertsRemovesAndEmits) {
    Environment env;
    EditSession s(env);
    load_core(s);
    ASSERT_TRUE(s.new_graph("GenerateAnnotationEdit").is_ok());
    ASSERT_TRUE(s.add_node("PrintString", "logger").is_ok());
    ASSERT_TRUE(s.add_comment("logger", "legacy note").is_ok());
    ASSERT_TRUE(s.add_meta("position", "logger", "x", "100").is_ok());

    ASSERT_TRUE(s.set_generate_comment_annotation("logger", "legacy note", 0,
        {"Id", {{"", "comment-a"}}}).is_ok());
    ASSERT_TRUE(s.set_generate_metadata_annotation("position", "logger", "x", "100", 0,
        {"PersistentId", {{"", "meta-a"}}}).is_ok());

    std::string text = s.emit();
    EXPECT_NE(text.find("[Id(\"comment-a\")]"), std::string::npos);
    EXPECT_NE(text.find("[PersistentId(\"meta-a\")]"), std::string::npos);

    std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"persistent_id\":\"comment-a\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"meta-a\""), std::string::npos);

    ASSERT_TRUE(s.set_generate_comment_annotation("logger", "legacy note", 0,
        {"Id", {{"", "comment-b"}}}).is_ok());
    text = s.emit();
    EXPECT_EQ(text.find("[Id(\"comment-a\")]"), std::string::npos);
    EXPECT_NE(text.find("[Id(\"comment-b\")]"), std::string::npos);

    ASSERT_TRUE(s.remove_generate_metadata_annotation("position", "logger", "x", "100", 0, "PersistentId").is_ok());
    text = s.emit();
    EXPECT_EQ(text.find("[PersistentId(\"meta-a\")]"), std::string::npos);
    ASSERT_TRUE(s.undo().is_ok());
    EXPECT_NE(s.emit().find("[PersistentId(\"meta-a\")]"), std::string::npos);

    ASSERT_TRUE(s.add_comment("logger", "legacy note").is_ok());
    EXPECT_TRUE(s.set_generate_comment_annotation("logger", "legacy note", 0,
        {"Tag", {{"", "ambiguous"}}}).is_err());
    ASSERT_TRUE(s.set_generate_comment_annotation("logger", "legacy note", 2,
        {"Id", {{"", "comment-b-duplicate"}}}).is_ok());
    json = s.state_to_json();
    EXPECT_NE(json.find("\"persistent_id\":\"comment-b\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"comment-b-duplicate\""), std::string::npos);
}

TEST(EditSession, GenerateItemRemoveApiSupportsOccurrenceAndUndo) {
    Environment env;
    EditSession s(env);
    load_core(s);
    ASSERT_TRUE(s.new_graph("GenerateRemove").is_ok());
    ASSERT_TRUE(s.add_node("PrintString", "logger").is_ok());
    ASSERT_TRUE(s.add_comment("logger", "same note").is_ok());
    ASSERT_TRUE(s.add_comment("logger", "same note").is_ok());
    ASSERT_TRUE(s.add_meta("position", "logger", "x", "100").is_ok());
    ASSERT_TRUE(s.add_meta("position", "logger", "x", "100").is_ok());

    EXPECT_TRUE(s.remove_comment("logger", "same note", 0).is_err());
    ASSERT_TRUE(s.remove_comment("logger", "same note", 2).is_ok());
    ASSERT_TRUE(s.active_graph()->generate.has_value());
    EXPECT_EQ(s.active_graph()->generate->comments.size(), 1u);

    ASSERT_TRUE(s.undo().is_ok());
    ASSERT_TRUE(s.active_graph()->generate.has_value());
    EXPECT_EQ(s.active_graph()->generate->comments.size(), 2u);

    EXPECT_TRUE(s.remove_meta("position", "logger", "x", "100", 0).is_err());
    ASSERT_TRUE(s.remove_meta("position", "logger", "x", "100", 1).is_ok());
    ASSERT_TRUE(s.active_graph()->generate.has_value());
    EXPECT_EQ(s.active_graph()->generate->metadata.size(), 1u);

    ASSERT_TRUE(s.remove_comment("logger", "same note", 1).is_ok());
    ASSERT_TRUE(s.remove_comment("logger", "same note", 1).is_ok());
    ASSERT_TRUE(s.remove_meta("position", "logger", "x", "100", 1).is_ok());
    EXPECT_FALSE(s.active_graph()->generate.has_value());

    ASSERT_TRUE(s.undo().is_ok());
    ASSERT_TRUE(s.active_graph()->generate.has_value());
    EXPECT_EQ(s.active_graph()->generate->metadata.size(), 1u);
}

TEST(EditSession, GenerateItemMoveApiSupportsOccurrenceAndUndo) {
    Environment env;
    EditSession s(env);
    load_core(s);
    ASSERT_TRUE(s.new_graph("GenerateMove").is_ok());
    ASSERT_TRUE(s.add_node("PrintString", "logger").is_ok());
    ASSERT_TRUE(s.add_comment("logger", "first").is_ok());
    ASSERT_TRUE(s.add_comment("logger", "second").is_ok());
    ASSERT_TRUE(s.add_comment("logger", "second").is_ok());
    ASSERT_TRUE(s.add_meta("position", "logger", "x", "100").is_ok());
    ASSERT_TRUE(s.add_meta("position", "logger", "y", "200").is_ok());

    ASSERT_TRUE(s.active_graph()->generate.has_value());
    EXPECT_TRUE(s.move_comment("logger", "first", 0, true).is_err());
    EXPECT_TRUE(s.move_comment("logger", "second", 0, true).is_err());
    ASSERT_TRUE(s.move_comment("logger", "second", 1, true).is_ok());
    EXPECT_EQ(s.active_graph()->generate->comments[0].text, "second");
    EXPECT_EQ(s.active_graph()->generate->comments[1].text, "first");
    EXPECT_EQ(s.active_graph()->generate->comments[2].text, "second");

    ASSERT_TRUE(s.undo().is_ok());
    EXPECT_EQ(s.active_graph()->generate->comments[0].text, "first");
    EXPECT_EQ(s.active_graph()->generate->comments[1].text, "second");

    ASSERT_TRUE(s.move_comment("logger", "first", 0, false).is_ok());
    EXPECT_EQ(s.active_graph()->generate->comments[1].text, "first");

    EXPECT_TRUE(s.move_meta("position", "logger", "y", "200", 0, false).is_err());
    ASSERT_TRUE(s.move_meta("position", "logger", "x", "100", 0, false).is_ok());
    EXPECT_EQ(s.active_graph()->generate->metadata[0].property, "y");
    EXPECT_EQ(s.active_graph()->generate->metadata[1].property, "x");
}

TEST(EditSession, GenerateItemRenameApiSupportsOccurrenceAndUndo) {
    Environment env;
    EditSession s(env);
    load_core(s);
    ASSERT_TRUE(s.new_graph("GenerateRename").is_ok());
    ASSERT_TRUE(s.add_node("PrintString", "logger").is_ok());
    ASSERT_TRUE(s.add_comment("logger", "same note").is_ok());
    ASSERT_TRUE(s.add_comment("logger", "same note").is_ok());
    ASSERT_TRUE(s.set_generate_comment_annotation("logger", "same note", 2,
        {"Id", {{"", "comment-2"}}}).is_ok());
    ASSERT_TRUE(s.add_meta("position", "logger", "x", "100").is_ok());
    ASSERT_TRUE(s.add_meta("position", "logger", "x", "100").is_ok());
    ASSERT_TRUE(s.set_generate_metadata_annotation("position", "logger", "x", "100", 1,
        {"PersistentId", {{"", "meta-1"}}}).is_ok());

    ASSERT_TRUE(s.active_graph()->generate.has_value());
    EXPECT_TRUE(s.rename_comment("logger", "same note", 0, "renamed note").is_err());
    ASSERT_TRUE(s.rename_comment("logger", "same note", 2, "renamed note").is_ok());
    EXPECT_EQ(s.active_graph()->generate->comments[1].text, "renamed note");
    ASSERT_EQ(s.active_graph()->generate->comments[1].annotations.size(), 1u);
    EXPECT_EQ(s.active_graph()->generate->comments[1].annotations[0].args[0].value, "comment-2");

    auto text = s.emit();
    EXPECT_NE(text.find("[Id(\"comment-2\")]"), std::string::npos);
    EXPECT_NE(text.find("Comment logger = \"renamed note\";"), std::string::npos);

    ASSERT_TRUE(s.undo().is_ok());
    EXPECT_EQ(s.active_graph()->generate->comments[1].text, "same note");

    EXPECT_TRUE(s.rename_meta("position", "logger", "x", "100", 0, "150").is_err());
    ASSERT_TRUE(s.rename_meta("position", "logger", "x", "100", 1, "150").is_ok());
    EXPECT_EQ(s.active_graph()->generate->metadata[0].value, "150");
    ASSERT_EQ(s.active_graph()->generate->metadata[0].annotations.size(), 1u);
    EXPECT_EQ(s.active_graph()->generate->metadata[0].annotations[0].args[0].value, "meta-1");

    text = s.emit();
    EXPECT_NE(text.find("[PersistentId(\"meta-1\")]"), std::string::npos);
    EXPECT_NE(text.find("position:logger.x(150);"), std::string::npos);

    ASSERT_TRUE(s.undo().is_ok());
    EXPECT_EQ(s.active_graph()->generate->metadata[0].value, "100");

    EXPECT_TRUE(s.rename_meta_ref("position", "logger", "x", "100", 0,
        "layout", "logger", "y").is_err());
    ASSERT_TRUE(s.rename_meta_ref("position", "logger", "x", "100", 1,
        "layout", "logger", "y").is_ok());
    EXPECT_EQ(s.active_graph()->generate->metadata[0].scope, "layout");
    EXPECT_EQ(s.active_graph()->generate->metadata[0].node, "logger");
    EXPECT_EQ(s.active_graph()->generate->metadata[0].property, "y");
    EXPECT_EQ(s.active_graph()->generate->metadata[0].value, "100");
    ASSERT_EQ(s.active_graph()->generate->metadata[0].annotations.size(), 1u);
    EXPECT_EQ(s.active_graph()->generate->metadata[0].annotations[0].args[0].value, "meta-1");

    text = s.emit();
    EXPECT_NE(text.find("[PersistentId(\"meta-1\")]"), std::string::npos);
    EXPECT_NE(text.find("layout:logger.y(100);"), std::string::npos);

    ASSERT_TRUE(s.undo().is_ok());
    EXPECT_EQ(s.active_graph()->generate->metadata[0].scope, "position");
    EXPECT_EQ(s.active_graph()->generate->metadata[0].property, "x");
}

TEST(EditSession, StateJsonExportsAnnotations) {
    Environment env;
    EditSession s(env);
    load_core(s);
    ASSERT_TRUE(s.new_graph("Annotated").is_ok());

    auto* g = s.active_graph();
    ASSERT_NE(g, nullptr);
    g->annotations.push_back({"Comment", {{"", "title"}, {"", "Graph note"}}});

    ASSERT_TRUE(s.add_param(ParamDirection::In, "name", "FString").is_ok());
    g->parameters[0].annotations.push_back({"Tooltip", {{"", "Player name"}}});

    ASSERT_TRUE(s.add_node("PrintString", "logger").is_ok());
    g->node_instances[0].annotations.push_back({"Position", {{"X", "100"}, {"Y", "200"}}});

    std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"annotations\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"Comment\""), std::string::npos);
    EXPECT_NE(json.find("\"value\":\"Graph note\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"Tooltip\""), std::string::npos);
    EXPECT_NE(json.find("\"value\":\"Player name\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"Position\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"X\",\"value\":\"100\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"Y\",\"value\":\"200\""), std::string::npos);
}

TEST(EditSession, StateJsonExportsStableElementIds) {
    Environment env;
    EditSession s(env);
    load_core(s);
    ASSERT_TRUE(s.new_graph("StableIds").is_ok());
    ASSERT_TRUE(s.add_param(ParamDirection::In, "msg", "FString").is_ok());
    ASSERT_TRUE(s.add_node("PrintString", "logger").is_ok());
    ASSERT_TRUE(s.add_node("Delay", "wait").is_ok());
    ASSERT_TRUE(s.add_event("OnStart").is_ok());
    ASSERT_TRUE(s.add_function("Compute").is_ok());
    ASSERT_TRUE(s.add_flow("OnStart", "logger", "exit", "wait", "enter").is_ok());
    ASSERT_TRUE(s.add_link("OnStart", "logger", "message", "msg").is_ok());
    ASSERT_TRUE(s.add_comment("logger", "generated note").is_ok());
    ASSERT_TRUE(s.add_meta("position", "logger", "x", "120").is_ok());
    ASSERT_TRUE(s.set_graph_annotation({"Id", {{"", "graph-001"}}}).is_ok());
    ASSERT_TRUE(s.set_param_annotation("msg", {"Id", {{"", "param-msg-001"}}}).is_ok());
    ASSERT_TRUE(s.set_node_annotation("logger", {"Id", {{"", "node-logger-001"}}}).is_ok());

    std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"id\":\"graph:StableIds\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"graph-001\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"param:StableIds/msg\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"param-msg-001\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"node:StableIds/logger\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"node-logger-001\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"node:StableIds/wait\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"block:StableIds/event/OnStart\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"block:StableIds/function/Compute\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"flow:StableIds/event/OnStart/logger.exit->wait.enter\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"link:StableIds/event/OnStart/msg->logger.message\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"generate-comment:StableIds/logger/generated note\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"generate-metadata:StableIds/position/logger/x/120\""), std::string::npos);

    Environment top_level_env;
    EditSession top_level(top_level_env);
    load_core(top_level);
    const std::string source = R"(import "ue_core.d.gs";
let spawn_point = SoftObjectPath("spawn");
Graph TopLevelIds {
}
)";
    auto loaded = top_level.load_source(source, "stable_top_level.gs");
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    std::string top_level_json = top_level.state_to_json();
    EXPECT_NE(top_level_json.find("\"id\":\"import:ue_core.d.gs\""), std::string::npos);
    EXPECT_NE(top_level_json.find("\"id\":\"let:spawn_point\""), std::string::npos);
}

TEST(EditSession, StateJsonDisambiguatesDuplicateGenerateElementIds) {
    Environment env;
    EditSession s(env);
    load_core(s);
    ASSERT_TRUE(s.new_graph("DuplicateGenerateIds").is_ok());
    ASSERT_TRUE(s.add_comment("logger", "same note").is_ok());
    ASSERT_TRUE(s.add_comment("logger", "same note").is_ok());
    ASSERT_TRUE(s.add_meta("position", "logger", "x", "120").is_ok());
    ASSERT_TRUE(s.add_meta("position", "logger", "x", "120").is_ok());

    const std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"id\":\"generate-comment:DuplicateGenerateIds/logger/same note\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"generate-comment:DuplicateGenerateIds/logger/same note#2\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"generate-metadata:DuplicateGenerateIds/position/logger/x/120\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"generate-metadata:DuplicateGenerateIds/position/logger/x/120#2\""), std::string::npos);
}

TEST(EditSession, RenameNodeMigratesReferencesAndKeepsPersistentId) {
    Environment env;
    EditSession s(env);
    load_core(s);
    ASSERT_TRUE(s.new_graph("RenameIds").is_ok());
    ASSERT_TRUE(s.add_param(ParamDirection::In, "msg", "FString").is_ok());
    ASSERT_TRUE(s.add_node("PrintString", "logger").is_ok());
    ASSERT_TRUE(s.add_node("Delay", "wait").is_ok());
    ASSERT_TRUE(s.add_event("OnStart").is_ok());
    ASSERT_TRUE(s.add_flow("OnStart", "logger", "exit", "wait", "enter").is_ok());
    ASSERT_TRUE(s.add_link("OnStart", "logger", "message", "msg").is_ok());
    ASSERT_TRUE(s.set_node_annotation("logger", {"Id", {{"", "node-logger-stable"}}}).is_ok());
    ASSERT_TRUE(s.add_comment("logger", "generated note").is_ok());
    ASSERT_TRUE(s.add_meta("position", "logger", "x", "120").is_ok());

    ASSERT_TRUE(s.rename_node_instance("logger", "writer").is_ok());

    const auto& graph = s.module().graphs[0];
    ASSERT_EQ(graph.node_instances.size(), 2u);
    EXPECT_EQ(graph.node_instances[0].instance_name, "writer");
    ASSERT_EQ(graph.node_instances[0].annotations.size(), 1u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[0].value, "node-logger-stable");
    ASSERT_EQ(graph.events.size(), 1u);
    ASSERT_EQ(graph.events[0].flow_connections.size(), 1u);
    EXPECT_EQ(graph.events[0].flow_connections[0].from.node_instance, "writer");
    ASSERT_EQ(graph.events[0].data_links.size(), 1u);
    EXPECT_EQ(graph.events[0].data_links[0].target.node_instance, "writer");
    ASSERT_TRUE(graph.generate.has_value());
    EXPECT_EQ(graph.generate->comments[0].instance_name, "writer");
    EXPECT_EQ(graph.generate->metadata[0].node, "writer");

    const std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"id\":\"node:RenameIds/writer\""), std::string::npos);
    EXPECT_EQ(json.find("\"id\":\"node:RenameIds/logger\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"node-logger-stable\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"flow:RenameIds/event/OnStart/writer.exit->wait.enter\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"link:RenameIds/event/OnStart/msg->writer.message\""), std::string::npos);

    const std::string emitted = s.emit();
    EXPECT_NE(emitted.find("[Id(\"node-logger-stable\")]"), std::string::npos);
    EXPECT_NE(emitted.find("PrintString writer{};"), std::string::npos);
    EXPECT_NE(emitted.find("writer.exit(wait.enter);"), std::string::npos);
    EXPECT_NE(emitted.find("writer.message = msg;"), std::string::npos);

    auto undo = s.undo();
    ASSERT_TRUE(undo.is_ok()) << undo.error();
    ASSERT_EQ(s.module().graphs[0].node_instances.size(), 2u);
    EXPECT_EQ(s.module().graphs[0].node_instances[0].instance_name, "logger");
    EXPECT_EQ(s.module().graphs[0].events[0].flow_connections[0].from.node_instance, "logger");
    EXPECT_EQ(s.module().graphs[0].events[0].data_links[0].target.node_instance, "logger");
}

TEST(EditSession, RenameParamMigratesBareReferencesAndKeepsPersistentId) {
    Environment env;
    EditSession s(env);
    load_core(s);
    ASSERT_TRUE(s.new_graph("RenameParamIds").is_ok());
    ASSERT_TRUE(s.add_param(ParamDirection::In, "msg", "FString").is_ok());
    ASSERT_TRUE(s.add_param(ParamDirection::In, "locator", "AActor").is_ok());
    ASSERT_TRUE(s.add_node("PrintString", "logger").is_ok());
    ASSERT_TRUE(s.add_node("GetActorLocation", "locator").is_ok());
    ASSERT_TRUE(s.add_event("OnStart").is_ok());
    ASSERT_TRUE(s.add_link("OnStart", "logger", "message", "msg").is_ok());
    ASSERT_TRUE(s.add_link("OnStart", "logger", "message", "locator", "location").is_ok());
    ASSERT_TRUE(s.set_param_annotation("msg", {"Id", {{"", "param-msg-stable"}}}).is_ok());

    ASSERT_TRUE(s.rename_param("msg", "text").is_ok());

    const auto& graph = s.module().graphs[0];
    ASSERT_EQ(graph.parameters.size(), 2u);
    EXPECT_EQ(graph.parameters[0].name, "text");
    ASSERT_EQ(graph.parameters[0].annotations.size(), 1u);
    EXPECT_EQ(graph.parameters[0].annotations[0].args[0].value, "param-msg-stable");
    ASSERT_EQ(graph.events.size(), 1u);
    ASSERT_EQ(graph.events[0].data_links.size(), 2u);
    EXPECT_EQ(graph.events[0].data_links[0].source.node_instance, "text");
    EXPECT_TRUE(graph.events[0].data_links[0].source.pin_name.empty());
    EXPECT_EQ(graph.events[0].data_links[1].source.node_instance, "locator");
    EXPECT_EQ(graph.events[0].data_links[1].source.pin_name, "location");

    const std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"id\":\"param:RenameParamIds/text\""), std::string::npos);
    EXPECT_EQ(json.find("\"id\":\"param:RenameParamIds/msg\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"param-msg-stable\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"link:RenameParamIds/event/OnStart/text->logger.message\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"link:RenameParamIds/event/OnStart/locator.location->logger.message\""), std::string::npos);

    const std::string emitted = s.emit();
    EXPECT_NE(emitted.find("[Id(\"param-msg-stable\")]"), std::string::npos);
    EXPECT_NE(emitted.find("in text : FString;"), std::string::npos);
    EXPECT_NE(emitted.find("logger.message = text;"), std::string::npos);
    EXPECT_NE(emitted.find("logger.message = locator.location;"), std::string::npos);

    auto undo = s.undo();
    ASSERT_TRUE(undo.is_ok()) << undo.error();
    ASSERT_EQ(s.module().graphs[0].parameters.size(), 2u);
    EXPECT_EQ(s.module().graphs[0].parameters[0].name, "msg");
    EXPECT_EQ(s.module().graphs[0].events[0].data_links[0].source.node_instance, "msg");
    EXPECT_TRUE(s.module().graphs[0].events[0].data_links[0].source.pin_name.empty());
}

TEST(EditSession, RenameLogicBlocksKeepPersistentIdsAndConnections) {
    Environment env;
    EditSession s(env);
    load_core(s);
    ASSERT_TRUE(s.new_graph("RenameBlocks").is_ok());
    ASSERT_TRUE(s.add_param(ParamDirection::In, "msg", "FString").is_ok());
    ASSERT_TRUE(s.add_node("PrintString", "logger").is_ok());
    ASSERT_TRUE(s.add_node("Delay", "wait").is_ok());
    ASSERT_TRUE(s.add_event("OnStart").is_ok());
    ASSERT_TRUE(s.add_flow("OnStart", "logger", "exit", "wait", "enter").is_ok());
    ASSERT_TRUE(s.add_link("OnStart", "logger", "message", "msg").is_ok());
    ASSERT_TRUE(s.add_function("Compute").is_ok());
    ASSERT_TRUE(s.add_flow("Compute", "context", "start", "context", "done").is_ok());
    ASSERT_TRUE(s.add_link("Compute", "context", "result", "msg").is_ok());
    s.module_mut().graphs[0].events[0].annotations.push_back({"Id", {{"", "event-stable"}}});
    s.module_mut().graphs[0].functions[0].annotations.push_back({"PersistentId", {{"", "function-stable"}}});

    ASSERT_TRUE(s.rename_event("OnStart", "Begin").is_ok());
    ASSERT_TRUE(s.rename_function("Compute", "Evaluate").is_ok());

    const auto& graph = s.module().graphs[0];
    ASSERT_EQ(graph.events.size(), 1u);
    EXPECT_EQ(graph.events[0].name, "Begin");
    ASSERT_EQ(graph.events[0].annotations.size(), 1u);
    EXPECT_EQ(graph.events[0].annotations[0].args[0].value, "event-stable");
    ASSERT_EQ(graph.events[0].flow_connections.size(), 1u);
    EXPECT_EQ(graph.events[0].flow_connections[0].from.node_instance, "logger");
    ASSERT_EQ(graph.events[0].data_links.size(), 1u);
    EXPECT_EQ(graph.events[0].data_links[0].source.node_instance, "msg");
    ASSERT_EQ(graph.functions.size(), 1u);
    EXPECT_EQ(graph.functions[0].name, "Evaluate");
    ASSERT_EQ(graph.functions[0].annotations.size(), 1u);
    EXPECT_EQ(graph.functions[0].annotations[0].args[0].value, "function-stable");
    ASSERT_EQ(graph.functions[0].flow_connections.size(), 1u);
    EXPECT_EQ(graph.functions[0].flow_connections[0].from.node_instance, "context");
    ASSERT_EQ(graph.functions[0].data_links.size(), 1u);
    EXPECT_EQ(graph.functions[0].data_links[0].target.node_instance, "context");

    const std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"id\":\"block:RenameBlocks/event/Begin\""), std::string::npos);
    EXPECT_EQ(json.find("\"id\":\"block:RenameBlocks/event/OnStart\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"event-stable\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"flow:RenameBlocks/event/Begin/logger.exit->wait.enter\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"link:RenameBlocks/event/Begin/msg->logger.message\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"block:RenameBlocks/function/Evaluate\""), std::string::npos);
    EXPECT_EQ(json.find("\"id\":\"block:RenameBlocks/function/Compute\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"function-stable\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"flow:RenameBlocks/function/Evaluate/context.start->context.done\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"link:RenameBlocks/function/Evaluate/msg->context.result\""), std::string::npos);

    const std::string emitted = s.emit();
    EXPECT_NE(emitted.find("[Id(\"event-stable\")]"), std::string::npos);
    EXPECT_NE(emitted.find("event Begin {"), std::string::npos);
    EXPECT_NE(emitted.find("logger.exit(wait.enter);"), std::string::npos);
    EXPECT_NE(emitted.find("logger.message = msg;"), std::string::npos);
    EXPECT_NE(emitted.find("[PersistentId(\"function-stable\")]"), std::string::npos);
    EXPECT_NE(emitted.find("function Evaluate {"), std::string::npos);
    EXPECT_NE(emitted.find("context.start(context.done);"), std::string::npos);
    EXPECT_NE(emitted.find("context.result = msg;"), std::string::npos);

    auto undo_function = s.undo();
    ASSERT_TRUE(undo_function.is_ok()) << undo_function.error();
    EXPECT_EQ(s.module().graphs[0].events[0].name, "Begin");
    EXPECT_EQ(s.module().graphs[0].functions[0].name, "Compute");
    auto undo_event = s.undo();
    ASSERT_TRUE(undo_event.is_ok()) << undo_event.error();
    EXPECT_EQ(s.module().graphs[0].events[0].name, "OnStart");
    EXPECT_EQ(s.module().graphs[0].functions[0].name, "Compute");
}

TEST(EditSession, LogicBlockAnnotationsRoundTripAndExportPersistentIds) {
    Environment env;
    EditSession s(env);

    const std::string source = R"(Graph AnnotatedBlocks {
    [Id("event-start")]
    event OnStart {
    }
    [PersistentId("function-compute")]
    function Compute {
    }
}
)";
    auto loaded = s.load_source(source, "annotated_blocks.gs");
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    ASSERT_EQ(s.module().graphs.size(), 1u);
    const auto& graph = s.module().graphs[0];
    ASSERT_EQ(graph.events.size(), 1u);
    ASSERT_EQ(graph.functions.size(), 1u);
    ASSERT_EQ(graph.events[0].annotations.size(), 1u);
    ASSERT_EQ(graph.functions[0].annotations.size(), 1u);
    EXPECT_EQ(graph.events[0].annotations[0].name, "Id");
    ASSERT_EQ(graph.events[0].annotations[0].args.size(), 1u);
    EXPECT_EQ(graph.events[0].annotations[0].args[0].value, "event-start");
    EXPECT_EQ(graph.functions[0].annotations[0].name, "PersistentId");
    ASSERT_EQ(graph.functions[0].annotations[0].args.size(), 1u);
    EXPECT_EQ(graph.functions[0].annotations[0].args[0].value, "function-compute");

    std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"id\":\"block:AnnotatedBlocks/event/OnStart\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"event-start\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"block:AnnotatedBlocks/function/Compute\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"function-compute\""), std::string::npos);
    EXPECT_NE(json.find("\"annotations\":[{\"name\":\"Id\""), std::string::npos);
    EXPECT_NE(json.find("\"annotations\":[{\"name\":\"PersistentId\""), std::string::npos);

    std::string emitted = s.emit();
    EXPECT_NE(emitted.find("[Id(\"event-start\")]"), std::string::npos);
    EXPECT_NE(emitted.find("[PersistentId(\"function-compute\")]"), std::string::npos);

    EditSession reparsed(env);
    auto reloaded = reparsed.load_source(emitted, "annotated_blocks_roundtrip.gs");
    ASSERT_TRUE(reloaded.is_ok()) << reloaded.error();
    ASSERT_EQ(reparsed.module().graphs.size(), 1u);
    EXPECT_EQ(reparsed.module().graphs[0].events[0].annotations[0].args[0].value, "event-start");
    EXPECT_EQ(reparsed.module().graphs[0].functions[0].annotations[0].args[0].value, "function-compute");

    ASSERT_TRUE(reparsed.set_block_annotation("event", "OnStart", {"Id", {{"", "event-updated"}}}).is_ok());
    ASSERT_TRUE(reparsed.set_block_annotation("function", "Compute", {"Id", {{"", "function-updated"}}}).is_ok());
    ASSERT_EQ(reparsed.module().graphs[0].events[0].annotations[0].name, "Id");
    EXPECT_EQ(reparsed.module().graphs[0].events[0].annotations[0].args[0].value, "event-updated");
    ASSERT_EQ(reparsed.module().graphs[0].functions[0].annotations.size(), 2u);
    ASSERT_TRUE(reparsed.remove_block_annotation("function", "Compute", "PersistentId").is_ok());
    ASSERT_EQ(reparsed.module().graphs[0].functions[0].annotations.size(), 1u);
    EXPECT_EQ(reparsed.module().graphs[0].functions[0].annotations[0].name, "Id");
    EXPECT_TRUE(reparsed.remove_block_annotation("event", "OnStart", "Missing").is_err());
}

TEST(EditSession, ConnectionAnnotationsRoundTripAndExportPersistentIds) {
    Environment env;
    EditSession s(env);
    load_core(s);

    const std::string source = R"(Graph AnnotatedConnections {
    in msg : FString;
    PrintString logger{};
    Delay wait{};
    [Id("event-start")]
    event OnStart {
        [Id("flow-start")]
        logger.exit(wait.enter);
        [PersistentId("link-message")]
        logger.message = msg;
    }
    function Compute {
        [Id("flow-context")]
        context.start(context.done);
        [PersistentId("link-result")]
        context.result = msg;
    }
}
)";
    auto loaded = s.load_source(source, "annotated_connections.gs");
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    ASSERT_EQ(s.module().graphs.size(), 1u);
    const auto& graph = s.module().graphs[0];
    ASSERT_EQ(graph.events.size(), 1u);
    ASSERT_EQ(graph.functions.size(), 1u);
    ASSERT_EQ(graph.events[0].flow_connections.size(), 1u);
    ASSERT_EQ(graph.events[0].data_links.size(), 1u);
    ASSERT_EQ(graph.functions[0].flow_connections.size(), 1u);
    ASSERT_EQ(graph.functions[0].data_links.size(), 1u);
    ASSERT_EQ(graph.events[0].flow_connections[0].annotations.size(), 1u);
    ASSERT_EQ(graph.events[0].data_links[0].annotations.size(), 1u);
    ASSERT_EQ(graph.functions[0].flow_connections[0].annotations.size(), 1u);
    ASSERT_EQ(graph.functions[0].data_links[0].annotations.size(), 1u);
    EXPECT_EQ(graph.events[0].flow_connections[0].annotations[0].args[0].value, "flow-start");
    EXPECT_EQ(graph.events[0].data_links[0].annotations[0].args[0].value, "link-message");
    EXPECT_EQ(graph.functions[0].flow_connections[0].annotations[0].args[0].value, "flow-context");
    EXPECT_EQ(graph.functions[0].data_links[0].annotations[0].args[0].value, "link-result");

    std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"id\":\"flow:AnnotatedConnections/event/OnStart/logger.exit->wait.enter\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"flow-start\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"link:AnnotatedConnections/event/OnStart/msg->logger.message\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"link-message\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"flow:AnnotatedConnections/function/Compute/context.start->context.done\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"flow-context\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"link:AnnotatedConnections/function/Compute/msg->context.result\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"link-result\""), std::string::npos);
    EXPECT_NE(json.find("\"annotations\":[{\"name\":\"Id\""), std::string::npos);
    EXPECT_NE(json.find("\"annotations\":[{\"name\":\"PersistentId\""), std::string::npos);

    std::string emitted = s.emit();
    EXPECT_NE(emitted.find("[Id(\"flow-start\")]"), std::string::npos);
    EXPECT_NE(emitted.find("[PersistentId(\"link-message\")]"), std::string::npos);
    EXPECT_NE(emitted.find("[Id(\"flow-context\")]"), std::string::npos);
    EXPECT_NE(emitted.find("[PersistentId(\"link-result\")]"), std::string::npos);

    EditSession reparsed(env);
    auto reloaded = reparsed.load_source(emitted, "annotated_connections_roundtrip.gs");
    ASSERT_TRUE(reloaded.is_ok()) << reloaded.error();
    ASSERT_EQ(reparsed.module().graphs.size(), 1u);
    const auto& roundtrip = reparsed.module().graphs[0];
    EXPECT_EQ(roundtrip.events[0].flow_connections[0].annotations[0].args[0].value, "flow-start");
    EXPECT_EQ(roundtrip.events[0].data_links[0].annotations[0].args[0].value, "link-message");
    EXPECT_EQ(roundtrip.functions[0].flow_connections[0].annotations[0].args[0].value, "flow-context");
    EXPECT_EQ(roundtrip.functions[0].data_links[0].annotations[0].args[0].value, "link-result");

    ASSERT_TRUE(reparsed.set_flow_annotation("event", "OnStart", "logger", "exit", "wait", "enter",
                                             {"PersistentId", {{"", "flow-updated"}}}).is_ok());
    ASSERT_TRUE(reparsed.set_link_annotation("event", "OnStart", "logger", "message", "msg", "",
                                             {"Id", {{"", "link-updated"}}}).is_ok());
    ASSERT_EQ(reparsed.module().graphs[0].events[0].flow_connections[0].annotations.size(), 2u);
    ASSERT_EQ(reparsed.module().graphs[0].events[0].data_links[0].annotations.size(), 2u);
    EXPECT_EQ(reparsed.module().graphs[0].events[0].flow_connections[0].annotations[1].name, "PersistentId");
    EXPECT_EQ(reparsed.module().graphs[0].events[0].data_links[0].annotations[1].name, "Id");

    ASSERT_TRUE(reparsed.remove_flow_annotation("event", "OnStart", "logger", "exit", "wait", "enter", "Id").is_ok());
    ASSERT_TRUE(reparsed.remove_link_annotation("event", "OnStart", "logger", "message", "msg", "", "PersistentId").is_ok());
    ASSERT_EQ(reparsed.module().graphs[0].events[0].flow_connections[0].annotations.size(), 1u);
    ASSERT_EQ(reparsed.module().graphs[0].events[0].data_links[0].annotations.size(), 1u);
    EXPECT_EQ(reparsed.module().graphs[0].events[0].flow_connections[0].annotations[0].name, "PersistentId");
    EXPECT_EQ(reparsed.module().graphs[0].events[0].data_links[0].annotations[0].name, "Id");
    EXPECT_TRUE(reparsed.remove_flow_annotation("event", "OnStart", "logger", "exit", "wait", "enter", "Missing").is_err());
    EXPECT_TRUE(reparsed.set_link_annotation("event", "OnStart", "logger", "message", "missing", "",
                                            {"Id", {{"", "missing"}}}).is_err());
}

TEST(EditSession, TopLevelAnnotationsRoundTripAndExportPersistentIds) {
    Environment env;
    EditSession s(env);
    load_core(s);

    const std::string source = R"([Id("import-core")]
import "ue_core.d.gs";
[PersistentId("let-spawn")]
let spawn_point = SoftObjectPath("spawn");
Graph TopLevelAnnotated {
}
)";
    auto loaded = s.load_source(source, "annotated_top_level.gs");
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    ASSERT_EQ(s.module().imports.size(), 1u);
    ASSERT_EQ(s.module().top_level_lets.size(), 1u);
    ASSERT_EQ(s.module().imports[0].annotations.size(), 1u);
    ASSERT_EQ(s.module().top_level_lets[0].annotations.size(), 1u);
    EXPECT_EQ(s.module().imports[0].annotations[0].name, "Id");
    ASSERT_EQ(s.module().imports[0].annotations[0].args.size(), 1u);
    EXPECT_EQ(s.module().imports[0].annotations[0].args[0].value, "import-core");
    EXPECT_EQ(s.module().top_level_lets[0].annotations[0].name, "PersistentId");
    ASSERT_EQ(s.module().top_level_lets[0].annotations[0].args.size(), 1u);
    EXPECT_EQ(s.module().top_level_lets[0].annotations[0].args[0].value, "let-spawn");

    std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"id\":\"import:ue_core.d.gs\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"import-core\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"let:spawn_point\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"let-spawn\""), std::string::npos);
    EXPECT_NE(json.find("\"annotations\":[{\"name\":\"Id\""), std::string::npos);
    EXPECT_NE(json.find("\"annotations\":[{\"name\":\"PersistentId\""), std::string::npos);

    std::string emitted = s.emit();
    EXPECT_NE(emitted.find("[Id(\"import-core\")]"), std::string::npos);
    EXPECT_NE(emitted.find("import \"ue_core.d.gs\";"), std::string::npos);
    EXPECT_NE(emitted.find("[PersistentId(\"let-spawn\")]"), std::string::npos);
    EXPECT_NE(emitted.find("let spawn_point = SoftObjectPath(\"spawn\");"), std::string::npos);

    EditSession reparsed(env);
    load_core(reparsed);
    auto reloaded = reparsed.load_source(emitted, "annotated_top_level_roundtrip.gs");
    ASSERT_TRUE(reloaded.is_ok()) << reloaded.error();
    ASSERT_EQ(reparsed.module().imports.size(), 1u);
    ASSERT_EQ(reparsed.module().top_level_lets.size(), 1u);
    EXPECT_EQ(reparsed.module().imports[0].annotations[0].args[0].value, "import-core");
    EXPECT_EQ(reparsed.module().top_level_lets[0].annotations[0].args[0].value, "let-spawn");
}

TEST(EditSession, CanEditTopLevelImportAndLetAnnotations) {
    Environment env;
    EditSession s(env);

    s.add_import("custom.d.gs");
    s.add_let("cached", "SoftObjectPath", "seed");

    auto import_result = s.set_import_annotation("custom.d.gs", {"Id", {{"", "import-cli"}}});
    ASSERT_TRUE(import_result.is_ok()) << import_result.error();
    auto let_result = s.set_let_annotation("cached", {"PersistentId", {{"", "let-cli"}}});
    ASSERT_TRUE(let_result.is_ok()) << let_result.error();

    ASSERT_EQ(s.module().imports[0].annotations.size(), 1u);
    EXPECT_EQ(s.module().imports[0].annotations[0].args[0].value, "import-cli");
    ASSERT_EQ(s.module().top_level_lets[0].annotations.size(), 1u);
    EXPECT_EQ(s.module().top_level_lets[0].annotations[0].args[0].value, "let-cli");

    auto emitted = s.emit();
    EXPECT_NE(emitted.find("[Id(\"import-cli\")]"), std::string::npos);
    EXPECT_NE(emitted.find("[PersistentId(\"let-cli\")]"), std::string::npos);

    auto remove_import = s.remove_import_annotation("custom.d.gs", "Id");
    ASSERT_TRUE(remove_import.is_ok()) << remove_import.error();
    auto remove_let = s.remove_let_annotation("cached", "PersistentId");
    ASSERT_TRUE(remove_let.is_ok()) << remove_let.error();
    EXPECT_TRUE(s.module().imports[0].annotations.empty());
    EXPECT_TRUE(s.module().top_level_lets[0].annotations.empty());
}

TEST(EditSession, StateJsonExportsElementSourceRanges) {
    Environment env;
    EditSession s(env);
    load_core(s);

    const std::string source = R"(import "ue_core.d.gs";
let spawn_point = SoftObjectPath("spawn");

[Comment("title", "Graph span")]
Graph SpanGraph : TraceGraph {
    in msg : FString = SoftObjectPath("Hello");
    [Position(X = 10, Y = 20, Asset = SoftObjectPath("Meta"))]
    PrintString logger{message = msg, count = 42, asset = SoftObjectPath("Asset")};
    PrintString raw{SoftObjectPath("Raw")}; Delay wait{};
    event OnStart {
        logger.exit(wait.enter);
        link logger.message = msg;
    }
    function Compute {
    }
}
)";
    auto loaded = s.load_source(source, "span_graph.gs");
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    ASSERT_EQ(s.module().imports.size(), 1u);
    ASSERT_EQ(s.module().top_level_lets.size(), 1u);
    ASSERT_EQ(s.module().graphs.size(), 1u);

    EXPECT_EQ(s.module().imports[0].source_range.start.line, 1u);
    EXPECT_EQ(s.module().imports[0].source_range.start.column, 1u);
    EXPECT_EQ(s.module().imports[0].path_range.start.line, 1u);
    EXPECT_EQ(s.module().imports[0].path_range.start.column, 8u);
    EXPECT_EQ(s.module().top_level_lets[0].source_range.start.line, 2u);
    EXPECT_EQ(s.module().top_level_lets[0].source_range.start.column, 1u);
    EXPECT_EQ(s.module().top_level_lets[0].name_range.start.line, 2u);
    EXPECT_EQ(s.module().top_level_lets[0].name_range.start.column, 5u);
    EXPECT_EQ(s.module().top_level_lets[0].type_name_range.start.line, 2u);
    EXPECT_EQ(s.module().top_level_lets[0].type_name_range.start.column, 19u);
    EXPECT_EQ(s.module().top_level_lets[0].constructor_range.start.line, 2u);
    EXPECT_EQ(s.module().top_level_lets[0].constructor_range.start.column, 19u);
    EXPECT_EQ(s.module().top_level_lets[0].constructor_arg_range.start.line, 2u);
    EXPECT_EQ(s.module().top_level_lets[0].constructor_arg_range.start.column, 34u);

    const auto& graph = s.module().graphs[0];
    ASSERT_EQ(graph.annotations.size(), 1u);
    ASSERT_EQ(graph.parameters.size(), 1u);
    ASSERT_EQ(graph.node_instances.size(), 3u);
    ASSERT_EQ(graph.node_instances[0].annotations.size(), 1u);
    ASSERT_EQ(graph.events.size(), 1u);
    ASSERT_EQ(graph.functions.size(), 1u);
    ASSERT_EQ(graph.events[0].flow_connections.size(), 1u);
    ASSERT_EQ(graph.events[0].data_links.size(), 1u);

    EXPECT_EQ(graph.annotations[0].source_range.start.line, 4u);
    EXPECT_EQ(graph.annotations[0].source_range.start.column, 2u);
    EXPECT_EQ(graph.annotations[0].name_range.start.line, 4u);
    EXPECT_EQ(graph.annotations[0].name_range.start.column, 2u);
    EXPECT_EQ(graph.annotations[0].args[0].source_range.start.line, 4u);
    EXPECT_EQ(graph.annotations[0].args[0].source_range.start.column, 10u);
    EXPECT_EQ(graph.annotations[0].args[0].value_range.start.line, 4u);
    EXPECT_EQ(graph.annotations[0].args[0].value_range.start.column, 10u);
    EXPECT_EQ(graph.source_range.start.line, 5u);
    EXPECT_EQ(graph.source_range.start.column, 1u);
    EXPECT_EQ(graph.name_range.start.line, 5u);
    EXPECT_EQ(graph.name_range.start.column, 7u);
    ASSERT_TRUE(graph.base_type.has_value());
    EXPECT_EQ(graph.base_type_range.start.line, 5u);
    EXPECT_EQ(graph.base_type_range.start.column, 19u);
    EXPECT_EQ(graph.parameters[0].source_range.start.line, 6u);
    EXPECT_EQ(graph.parameters[0].source_range.start.column, 5u);
    EXPECT_EQ(graph.parameters[0].name_range.start.line, 6u);
    EXPECT_EQ(graph.parameters[0].name_range.start.column, 8u);
    EXPECT_EQ(graph.parameters[0].type_name_range.start.line, 6u);
    EXPECT_EQ(graph.parameters[0].type_name_range.start.column, 14u);
    EXPECT_EQ(graph.parameters[0].default_value_range.start.line, 6u);
    EXPECT_EQ(graph.parameters[0].default_value_range.start.column, 24u);
    EXPECT_EQ(graph.parameters[0].default_constructor_range.start.line, 6u);
    EXPECT_EQ(graph.parameters[0].default_constructor_range.start.column, 24u);
    EXPECT_EQ(graph.parameters[0].default_constructor_type_range.start.line, 6u);
    EXPECT_EQ(graph.parameters[0].default_constructor_type_range.start.column, 24u);
    EXPECT_EQ(graph.parameters[0].default_constructor_arg_range.start.line, 6u);
    EXPECT_EQ(graph.parameters[0].default_constructor_arg_range.start.column, 39u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].source_range.start.line, 7u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].source_range.start.column, 6u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].name_range.start.line, 7u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].name_range.start.column, 6u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[0].source_range.start.line, 7u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[0].source_range.start.column, 15u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[0].name_range.start.line, 7u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[0].name_range.start.column, 15u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[0].value_range.start.line, 7u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[0].value_range.start.column, 19u);
    ASSERT_EQ(graph.node_instances[0].annotations[0].args.size(), 3u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[2].name, "Asset");
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[2].value, "SoftObjectPath(\"Meta\")");
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[2].source_range.start.line, 7u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[2].source_range.start.column, 31u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[2].name_range.start.line, 7u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[2].name_range.start.column, 31u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[2].value_range.start.line, 7u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[2].value_range.start.column, 39u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[2].value_constructor_range.start.line, 7u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[2].value_constructor_range.start.column, 39u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[2].value_constructor_type_range.start.line, 7u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[2].value_constructor_type_range.start.column, 39u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[2].value_constructor_arg_range.start.line, 7u);
    EXPECT_EQ(graph.node_instances[0].annotations[0].args[2].value_constructor_arg_range.start.column, 54u);
    EXPECT_EQ(graph.node_instances[0].source_range.start.line, 8u);
    EXPECT_EQ(graph.node_instances[0].source_range.start.column, 5u);
    EXPECT_EQ(graph.node_instances[0].type_name_range.start.line, 8u);
    EXPECT_EQ(graph.node_instances[0].type_name_range.start.column, 5u);
    EXPECT_EQ(graph.node_instances[0].instance_name_range.start.line, 8u);
    EXPECT_EQ(graph.node_instances[0].instance_name_range.start.column, 17u);
    EXPECT_EQ(graph.node_instances[0].initializer_range.start.line, 8u);
    EXPECT_EQ(graph.node_instances[0].initializer_range.start.column, 24u);
    ASSERT_EQ(graph.node_instances[0].initializer_fields.size(), 3u);
    EXPECT_EQ(graph.node_instances[0].initializer_fields[0].name, "message");
    EXPECT_EQ(graph.node_instances[0].initializer_fields[0].value, "msg");
    EXPECT_EQ(graph.node_instances[0].initializer_fields[0].name_range.start.line, 8u);
    EXPECT_EQ(graph.node_instances[0].initializer_fields[0].name_range.start.column, 24u);
    EXPECT_EQ(graph.node_instances[0].initializer_fields[0].value_range.start.line, 8u);
    EXPECT_EQ(graph.node_instances[0].initializer_fields[0].value_range.start.column, 34u);
    EXPECT_EQ(graph.node_instances[0].initializer_fields[1].name, "count");
    EXPECT_EQ(graph.node_instances[0].initializer_fields[1].value, "42");
    EXPECT_EQ(graph.node_instances[0].initializer_fields[1].name_range.start.line, 8u);
    EXPECT_EQ(graph.node_instances[0].initializer_fields[1].name_range.start.column, 39u);
    EXPECT_EQ(graph.node_instances[0].initializer_fields[1].value_range.start.line, 8u);
    EXPECT_EQ(graph.node_instances[0].initializer_fields[1].value_range.start.column, 47u);
    EXPECT_EQ(graph.node_instances[0].initializer_fields[2].name, "asset");
    EXPECT_EQ(graph.node_instances[0].initializer_fields[2].name_range.start.line, 8u);
    EXPECT_EQ(graph.node_instances[0].initializer_fields[2].name_range.start.column, 51u);
    EXPECT_EQ(graph.node_instances[0].initializer_fields[2].value_range.start.line, 8u);
    EXPECT_EQ(graph.node_instances[0].initializer_fields[2].value_range.start.column, 59u);
    EXPECT_EQ(graph.node_instances[0].initializer_fields[2].value_constructor_range.start.line, 8u);
    EXPECT_EQ(graph.node_instances[0].initializer_fields[2].value_constructor_range.start.column, 59u);
    EXPECT_EQ(graph.node_instances[0].initializer_fields[2].value_constructor_type_range.start.line, 8u);
    EXPECT_EQ(graph.node_instances[0].initializer_fields[2].value_constructor_type_range.start.column, 59u);
    EXPECT_EQ(graph.node_instances[0].initializer_fields[2].value_constructor_arg_range.start.line, 8u);
    EXPECT_EQ(graph.node_instances[0].initializer_fields[2].value_constructor_arg_range.start.column, 74u);
    EXPECT_EQ(graph.node_instances[1].instance_name, "raw");
    EXPECT_EQ(graph.node_instances[1].source_range.start.line, 9u);
    EXPECT_EQ(graph.node_instances[1].source_range.start.column, 5u);
    EXPECT_EQ(graph.node_instances[1].initializer_range.start.line, 9u);
    EXPECT_EQ(graph.node_instances[1].initializer_range.start.column, 21u);
    EXPECT_EQ(graph.node_instances[1].initializer_constructor_range.start.line, 9u);
    EXPECT_EQ(graph.node_instances[1].initializer_constructor_range.start.column, 21u);
    EXPECT_EQ(graph.node_instances[1].initializer_constructor_type_range.start.line, 9u);
    EXPECT_EQ(graph.node_instances[1].initializer_constructor_type_range.start.column, 21u);
    EXPECT_EQ(graph.node_instances[1].initializer_constructor_arg_range.start.line, 9u);
    EXPECT_EQ(graph.node_instances[1].initializer_constructor_arg_range.start.column, 36u);
    EXPECT_TRUE(graph.node_instances[1].initializer_fields.empty());
    EXPECT_EQ(graph.events[0].source_range.start.line, 10u);
    EXPECT_EQ(graph.events[0].source_range.start.column, 5u);
    EXPECT_EQ(graph.events[0].name_range.start.line, 10u);
    EXPECT_EQ(graph.events[0].name_range.start.column, 11u);
    EXPECT_EQ(graph.events[0].flow_connections[0].source_range.start.line, 11u);
    EXPECT_EQ(graph.events[0].flow_connections[0].source_range.start.column, 9u);
    EXPECT_EQ(graph.events[0].flow_connections[0].from_endpoint_range.start.line, 11u);
    EXPECT_EQ(graph.events[0].flow_connections[0].from_endpoint_range.start.column, 9u);
    EXPECT_EQ(graph.events[0].flow_connections[0].to_endpoint_range.start.line, 11u);
    EXPECT_EQ(graph.events[0].flow_connections[0].to_endpoint_range.start.column, 21u);
    EXPECT_EQ(graph.events[0].flow_connections[0].from_node_range.start.line, 11u);
    EXPECT_EQ(graph.events[0].flow_connections[0].from_node_range.start.column, 9u);
    EXPECT_EQ(graph.events[0].flow_connections[0].from_pin_range.start.line, 11u);
    EXPECT_EQ(graph.events[0].flow_connections[0].from_pin_range.start.column, 16u);
    EXPECT_EQ(graph.events[0].flow_connections[0].to_node_range.start.line, 11u);
    EXPECT_EQ(graph.events[0].flow_connections[0].to_node_range.start.column, 21u);
    EXPECT_EQ(graph.events[0].flow_connections[0].to_pin_range.start.line, 11u);
    EXPECT_EQ(graph.events[0].flow_connections[0].to_pin_range.start.column, 26u);
    EXPECT_EQ(graph.events[0].data_links[0].source_range.start.line, 12u);
    EXPECT_EQ(graph.events[0].data_links[0].source_range.start.column, 9u);
    EXPECT_EQ(graph.events[0].data_links[0].target_endpoint_range.start.line, 12u);
    EXPECT_EQ(graph.events[0].data_links[0].target_endpoint_range.start.column, 14u);
    EXPECT_EQ(graph.events[0].data_links[0].source_endpoint_range.start.line, 12u);
    EXPECT_EQ(graph.events[0].data_links[0].source_endpoint_range.start.column, 31u);
    EXPECT_EQ(graph.events[0].data_links[0].target_node_range.start.line, 12u);
    EXPECT_EQ(graph.events[0].data_links[0].target_node_range.start.column, 14u);
    EXPECT_EQ(graph.events[0].data_links[0].target_pin_range.start.line, 12u);
    EXPECT_EQ(graph.events[0].data_links[0].target_pin_range.start.column, 21u);
    EXPECT_EQ(graph.events[0].data_links[0].source_node_range.start.line, 12u);
    EXPECT_EQ(graph.events[0].data_links[0].source_node_range.start.column, 31u);
    EXPECT_EQ(graph.functions[0].source_range.start.line, 14u);
    EXPECT_EQ(graph.functions[0].source_range.start.column, 5u);
    EXPECT_EQ(graph.functions[0].name_range.start.line, 14u);
    EXPECT_EQ(graph.functions[0].name_range.start.column, 14u);

    std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"path\":\"ue_core.d.gs\",\"source_range\":{\"start\":{\"line\":1,\"column\":1}"), std::string::npos);
    EXPECT_NE(json.find("\"path_source_range\":{\"start\":{\"line\":1,\"column\":8}"), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"spawn_point\",\"source_range\":{\"start\":{\"line\":2,\"column\":1}"), std::string::npos);
    EXPECT_NE(json.find("\"name_source_range\":{\"start\":{\"line\":2,\"column\":5}"), std::string::npos);
    EXPECT_NE(json.find("\"type_source_range\":{\"start\":{\"line\":2,\"column\":19}"), std::string::npos);
    EXPECT_NE(json.find("\"constructor_source_range\":{\"start\":{\"line\":2,\"column\":19}"), std::string::npos);
    EXPECT_NE(json.find("\"arg_source_range\":{\"start\":{\"line\":2,\"column\":34}"), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"Comment\",\"source_range\":{\"start\":{\"line\":4,\"column\":2}"), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"Comment\",\"source_range\":{\"start\":{\"line\":4,\"column\":2}"), std::string::npos);
    EXPECT_NE(json.find("\"name_source_range\":{\"start\":{\"line\":4,\"column\":2}"), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"annotation:graph:SpanGraph/Comment\""), std::string::npos);
    EXPECT_NE(json.find("\"value\":\"title\",\"source_range\":{\"start\":{\"line\":4,\"column\":10}"), std::string::npos);
    EXPECT_NE(json.find("\"value_source_range\":{\"start\":{\"line\":4,\"column\":10}"), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"annotation-arg:annotation:graph:SpanGraph/Comment/$1\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"Position\",\"source_range\":{\"start\":{\"line\":7,\"column\":6}"), std::string::npos);
    EXPECT_NE(json.find("\"name_source_range\":{\"start\":{\"line\":7,\"column\":6}"), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"annotation:node:SpanGraph/logger/Position\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"X\",\"value\":\"10\",\"source_range\":{\"start\":{\"line\":7,\"column\":15}"), std::string::npos);
    EXPECT_NE(json.find("\"name_source_range\":{\"start\":{\"line\":7,\"column\":15}"), std::string::npos);
    EXPECT_NE(json.find("\"value_source_range\":{\"start\":{\"line\":7,\"column\":19}"), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"annotation-arg:annotation:node:SpanGraph/logger/Position/X\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"Asset\",\"value\":\"SoftObjectPath(\\\"Meta\\\")\",\"source_range\":{\"start\":{\"line\":7,\"column\":31}"), std::string::npos);
    EXPECT_NE(json.find("\"value_constructor_source_range\":{\"start\":{\"line\":7,\"column\":39}"), std::string::npos);
    EXPECT_NE(json.find("\"value_constructor_type_source_range\":{\"start\":{\"line\":7,\"column\":39}"), std::string::npos);
    EXPECT_NE(json.find("\"value_constructor_arg_source_range\":{\"start\":{\"line\":7,\"column\":54}"), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"annotation-arg:annotation:node:SpanGraph/logger/Position/Asset\""), std::string::npos);
    EXPECT_NE(json.find("\"source_range\":{\"start\":{\"line\":5,\"column\":1}"), std::string::npos);
    EXPECT_NE(json.find("\"name_source_range\":{\"start\":{\"line\":5,\"column\":7}"), std::string::npos);
    EXPECT_NE(json.find("\"base_type_source_range\":{\"start\":{\"line\":5,\"column\":19}"), std::string::npos);
    EXPECT_NE(json.find("\"source_range\":{\"start\":{\"line\":6,\"column\":5}"), std::string::npos);
    EXPECT_NE(json.find("\"name_source_range\":{\"start\":{\"line\":6,\"column\":8}"), std::string::npos);
    EXPECT_NE(json.find("\"type_source_range\":{\"start\":{\"line\":6,\"column\":14}"), std::string::npos);
    EXPECT_NE(json.find("\"default_source_range\":{\"start\":{\"line\":6,\"column\":24}"), std::string::npos);
    EXPECT_NE(json.find("\"default_constructor_source_range\":{\"start\":{\"line\":6,\"column\":24}"), std::string::npos);
    EXPECT_NE(json.find("\"default_constructor_type_source_range\":{\"start\":{\"line\":6,\"column\":24}"), std::string::npos);
    EXPECT_NE(json.find("\"default_constructor_arg_source_range\":{\"start\":{\"line\":6,\"column\":39}"), std::string::npos);
    EXPECT_NE(json.find("\"source_range\":{\"start\":{\"line\":8,\"column\":5}"), std::string::npos);
    EXPECT_NE(json.find("\"type_source_range\":{\"start\":{\"line\":8,\"column\":5}"), std::string::npos);
    EXPECT_NE(json.find("\"instance_source_range\":{\"start\":{\"line\":8,\"column\":17}"), std::string::npos);
    EXPECT_NE(json.find("\"init_source_range\":{\"start\":{\"line\":8,\"column\":24}"), std::string::npos);
    EXPECT_NE(json.find("\"initializer_fields\":[{\"id\":\"initializer-field:SpanGraph/logger/message\",\"name\":\"message\",\"value\":\"msg\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"initializer-field:SpanGraph/logger/count\",\"name\":\"count\",\"value\":\"42\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"initializer-field:SpanGraph/logger/asset\",\"name\":\"asset\""), std::string::npos);
    EXPECT_NE(json.find("\"name_source_range\":{\"start\":{\"line\":8,\"column\":24}"), std::string::npos);
    EXPECT_NE(json.find("\"value_source_range\":{\"start\":{\"line\":8,\"column\":34}"), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"count\",\"value\":\"42\""), std::string::npos);
    EXPECT_NE(json.find("\"value_source_range\":{\"start\":{\"line\":8,\"column\":47}"), std::string::npos);
    EXPECT_NE(json.find("\"value_constructor_source_range\":{\"start\":{\"line\":8,\"column\":59}"), std::string::npos);
    EXPECT_NE(json.find("\"value_constructor_type_source_range\":{\"start\":{\"line\":8,\"column\":59}"), std::string::npos);
    EXPECT_NE(json.find("\"value_constructor_arg_source_range\":{\"start\":{\"line\":8,\"column\":74}"), std::string::npos);
    EXPECT_NE(json.find("\"instance\":\"raw\""), std::string::npos);
    EXPECT_NE(json.find("\"init\":\"SoftObjectPath(\\\"Raw\\\")\""), std::string::npos);
    EXPECT_NE(json.find("\"init_constructor_source_range\":{\"start\":{\"line\":9,\"column\":21}"), std::string::npos);
    EXPECT_NE(json.find("\"init_constructor_type_source_range\":{\"start\":{\"line\":9,\"column\":21}"), std::string::npos);
    EXPECT_NE(json.find("\"init_constructor_arg_source_range\":{\"start\":{\"line\":9,\"column\":36}"), std::string::npos);
    EXPECT_NE(json.find("\"source_range\":{\"start\":{\"line\":10,\"column\":5}"), std::string::npos);
    EXPECT_NE(json.find("\"name_source_range\":{\"start\":{\"line\":10,\"column\":11}"), std::string::npos);
    EXPECT_NE(json.find("\"source_range\":{\"start\":{\"line\":11,\"column\":9}"), std::string::npos);
    EXPECT_NE(json.find("\"from_endpoint_source_range\":{\"start\":{\"line\":11,\"column\":9}"), std::string::npos);
    EXPECT_NE(json.find("\"to_endpoint_source_range\":{\"start\":{\"line\":11,\"column\":21}"), std::string::npos);
    EXPECT_NE(json.find("\"from_node_source_range\":{\"start\":{\"line\":11,\"column\":9}"), std::string::npos);
    EXPECT_NE(json.find("\"from_pin_source_range\":{\"start\":{\"line\":11,\"column\":16}"), std::string::npos);
    EXPECT_NE(json.find("\"to_node_source_range\":{\"start\":{\"line\":11,\"column\":21}"), std::string::npos);
    EXPECT_NE(json.find("\"to_pin_source_range\":{\"start\":{\"line\":11,\"column\":26}"), std::string::npos);
    EXPECT_NE(json.find("\"source_range\":{\"start\":{\"line\":12,\"column\":9}"), std::string::npos);
    EXPECT_NE(json.find("\"target_endpoint_source_range\":{\"start\":{\"line\":12,\"column\":14}"), std::string::npos);
    EXPECT_NE(json.find("\"source_endpoint_source_range\":{\"start\":{\"line\":12,\"column\":31}"), std::string::npos);
    EXPECT_NE(json.find("\"target_node_source_range\":{\"start\":{\"line\":12,\"column\":14}"), std::string::npos);
    EXPECT_NE(json.find("\"target_pin_source_range\":{\"start\":{\"line\":12,\"column\":21}"), std::string::npos);
    EXPECT_NE(json.find("\"source_node_source_range\":{\"start\":{\"line\":12,\"column\":31}"), std::string::npos);
    EXPECT_NE(json.find("\"name_source_range\":{\"start\":{\"line\":14,\"column\":14}"), std::string::npos);
}

TEST(EditSession, StateJsonExportsDeclarationSourceRanges) {
    Environment env;
    EditSession s(env);

    const std::string source =
        "declare type DeclValue : constructible;\n"
        "declare Node SpanDeclaredNode {\n"
        "    exec in enter;\n"
        "    data in value : DeclValue;\n"
        "    exec out done;\n"
        "}\n"
        "declare Schema SpanDeclaredSchema {\n"
        "    max_exec_fan_out: 1;\n"
        "    allow_exec_fan_in: true;\n"
        "    strict_type_match: false;\n"
        "    default_payload: DeclValue(\"schema\");\n"
        "}\n";
    const auto path = std::filesystem::temp_directory_path() / "graphscript_decl_source_ranges_090.d.gs";
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << source;
    }

    auto loaded = s.load_import(path.string());
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();

    auto* type = env.types().find("DeclValue");
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->source_range.start.line, 1u);
    EXPECT_EQ(type->source_range.start.column, 1u);
    EXPECT_EQ(type->name_range.start.line, 1u);
    EXPECT_EQ(type->name_range.start.column, 14u);
    EXPECT_EQ(type->source_file, path.string());

    auto* node = env.nodes().find("SpanDeclaredNode");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->source_range.start.line, 2u);
    EXPECT_EQ(node->source_range.start.column, 1u);
    EXPECT_EQ(node->name_range.start.line, 2u);
    EXPECT_EQ(node->name_range.start.column, 14u);
    EXPECT_EQ(node->source_file, path.string());
    ASSERT_EQ(node->pins.size(), 3u);
    EXPECT_EQ(node->pins[0].source_range.start.line, 3u);
    EXPECT_EQ(node->pins[0].source_range.start.column, 5u);
    EXPECT_EQ(node->pins[0].name_range.start.line, 3u);
    EXPECT_EQ(node->pins[0].name_range.start.column, 13u);
    EXPECT_EQ(node->pins[0].source_file, path.string());
    EXPECT_EQ(node->pins[1].source_range.start.line, 4u);
    EXPECT_EQ(node->pins[1].source_range.start.column, 5u);
    EXPECT_EQ(node->pins[1].name_range.start.line, 4u);
    EXPECT_EQ(node->pins[1].name_range.start.column, 13u);
    EXPECT_EQ(node->pins[1].type_name_range.start.line, 4u);
    EXPECT_EQ(node->pins[1].type_name_range.start.column, 21u);

    auto* schema = env.schemas().find("SpanDeclaredSchema");
    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(schema->source_range.start.line, 7u);
    EXPECT_EQ(schema->source_range.start.column, 1u);
    EXPECT_EQ(schema->name_range.start.line, 7u);
    EXPECT_EQ(schema->name_range.start.column, 16u);
    EXPECT_EQ(schema->source_file, path.string());
    ASSERT_EQ(schema->fields.size(), 4u);
    EXPECT_EQ(schema->fields[0].name, "max_exec_fan_out");
    EXPECT_EQ(schema->fields[0].value, "1");
    EXPECT_EQ(schema->fields[0].source_range.start.line, 8u);
    EXPECT_EQ(schema->fields[0].source_range.start.column, 5u);
    EXPECT_EQ(schema->fields[0].name_range.start.line, 8u);
    EXPECT_EQ(schema->fields[0].name_range.start.column, 5u);
    EXPECT_EQ(schema->fields[0].value_range.start.line, 8u);
    EXPECT_EQ(schema->fields[0].value_range.start.column, 23u);
    EXPECT_EQ(schema->fields[0].source_file, path.string());
    EXPECT_EQ(schema->fields[3].name, "default_payload");
    EXPECT_EQ(schema->fields[3].value, "DeclValue(\"schema\")");
    EXPECT_EQ(schema->fields[3].value_constructor_range.start.line, 11u);
    EXPECT_EQ(schema->fields[3].value_constructor_range.start.column, 22u);
    EXPECT_EQ(schema->fields[3].value_constructor_type_range.start.line, 11u);
    EXPECT_EQ(schema->fields[3].value_constructor_type_range.start.column, 22u);
    EXPECT_EQ(schema->fields[3].value_constructor_arg_range.start.line, 11u);
    EXPECT_EQ(schema->fields[3].value_constructor_arg_range.start.column, 32u);

    std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"declared_types\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"decl-type:DeclValue\""), std::string::npos);
    EXPECT_NE(json.find("\"source_file\""), std::string::npos);
    EXPECT_NE(json.find(path.filename().string()), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"DeclValue\""), std::string::npos);
    EXPECT_NE(json.find("\"source_range\":{\"start\":{\"line\":1,\"column\":1}"), std::string::npos);
    EXPECT_NE(json.find("\"name_source_range\":{\"start\":{\"line\":1,\"column\":14}"), std::string::npos);
    EXPECT_NE(json.find("\"constructible\":true"), std::string::npos);
    EXPECT_NE(json.find("\"value\":\"DeclValue(\\\"schema\\\")\""), std::string::npos);
    EXPECT_NE(json.find("\"value_constructor_source_range\":{\"start\":{\"line\":11,\"column\":22}"), std::string::npos);
    EXPECT_NE(json.find("\"value_constructor_type_source_range\":{\"start\":{\"line\":11,\"column\":22}"), std::string::npos);
    EXPECT_NE(json.find("\"value_constructor_arg_source_range\":{\"start\":{\"line\":11,\"column\":32}"), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"decl-node:SpanDeclaredNode\""), std::string::npos);
    EXPECT_NE(json.find("\"type_name\":\"SpanDeclaredNode\""), std::string::npos);
    EXPECT_NE(json.find("\"source_range\":{\"start\":{\"line\":2,\"column\":1}"), std::string::npos);
    EXPECT_NE(json.find("\"name_source_range\":{\"start\":{\"line\":2,\"column\":14}"), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"decl-pin:SpanDeclaredNode/enter\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"enter\""), std::string::npos);
    EXPECT_NE(json.find("\"source_range\":{\"start\":{\"line\":3,\"column\":5}"), std::string::npos);
    EXPECT_NE(json.find("\"name_source_range\":{\"start\":{\"line\":3,\"column\":13}"), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"decl-pin:SpanDeclaredNode/value\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"value\""), std::string::npos);
    EXPECT_NE(json.find("\"source_range\":{\"start\":{\"line\":4,\"column\":5}"), std::string::npos);
    EXPECT_NE(json.find("\"name_source_range\":{\"start\":{\"line\":4,\"column\":13}"), std::string::npos);
    EXPECT_NE(json.find("\"type_source_range\":{\"start\":{\"line\":4,\"column\":21}"), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"decl-schema:SpanDeclaredSchema\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"SpanDeclaredSchema\""), std::string::npos);
    EXPECT_NE(json.find("\"source_range\":{\"start\":{\"line\":7,\"column\":1}"), std::string::npos);
    EXPECT_NE(json.find("\"name_source_range\":{\"start\":{\"line\":7,\"column\":16}"), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"decl-schema-field:SpanDeclaredSchema/max_exec_fan_out\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"max_exec_fan_out\""), std::string::npos);
    EXPECT_NE(json.find("\"value\":\"1\""), std::string::npos);
    EXPECT_NE(json.find("\"source_range\":{\"start\":{\"line\":8,\"column\":5}"), std::string::npos);
    EXPECT_NE(json.find("\"name_source_range\":{\"start\":{\"line\":8,\"column\":5}"), std::string::npos);
    EXPECT_NE(json.find("\"value_source_range\":{\"start\":{\"line\":8,\"column\":23}"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(EditSession, DeclarationAnnotationsExportPersistentIds) {
    Environment env;
    EditSession s(env);

    const std::string source =
        "[Id(\"type-decl\")]\n"
        "declare type DeclValue : constructible;\n"
        "[PersistentId(\"node-decl\")]\n"
        "declare Node AnnotatedDeclaredNode {\n"
        "    [Id(\"pin-enter\")]\n"
        "    exec in enter;\n"
        "    [PersistentId(\"pin-value\")]\n"
        "    data in value : DeclValue;\n"
        "}\n"
        "[Id(\"schema-decl\")]\n"
        "declare Schema AnnotatedDeclaredSchema {\n"
        "    [PersistentId(\"field-fanout\")]\n"
        "    max_exec_fan_out: 1;\n"
        "    allow_exec_fan_in: true;\n"
        "}\n";
    const auto path = std::filesystem::temp_directory_path() / "graphscript_decl_annotations_110.d.gs";
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << source;
    }

    auto loaded = s.load_import(path.string());
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();

    auto* type = env.types().find("DeclValue");
    ASSERT_NE(type, nullptr);
    ASSERT_EQ(type->annotations.size(), 1u);
    EXPECT_EQ(type->annotations[0].name, "Id");
    EXPECT_EQ(type->annotations[0].args[0].value, "type-decl");

    auto* node = env.nodes().find("AnnotatedDeclaredNode");
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->annotations.size(), 1u);
    EXPECT_EQ(node->annotations[0].name, "PersistentId");
    EXPECT_EQ(node->annotations[0].args[0].value, "node-decl");
    ASSERT_EQ(node->pins.size(), 2u);
    ASSERT_EQ(node->pins[0].annotations.size(), 1u);
    ASSERT_EQ(node->pins[1].annotations.size(), 1u);
    EXPECT_EQ(node->pins[0].annotations[0].args[0].value, "pin-enter");
    EXPECT_EQ(node->pins[1].annotations[0].args[0].value, "pin-value");

    auto* schema = env.schemas().find("AnnotatedDeclaredSchema");
    ASSERT_NE(schema, nullptr);
    ASSERT_EQ(schema->annotations.size(), 1u);
    EXPECT_EQ(schema->annotations[0].args[0].value, "schema-decl");
    ASSERT_EQ(schema->fields.size(), 2u);
    ASSERT_EQ(schema->fields[0].annotations.size(), 1u);
    EXPECT_EQ(schema->fields[0].annotations[0].args[0].value, "field-fanout");

    std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"name\":\"DeclValue\""), std::string::npos);
    EXPECT_NE(json.find("\"source_range\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"type-decl\""), std::string::npos);
    EXPECT_NE(json.find("\"type_name\":\"AnnotatedDeclaredNode\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"node-decl\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"enter\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"pin-enter\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"value\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"pin-value\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"AnnotatedDeclaredSchema\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"schema-decl\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"max_exec_fan_out\",\"value\":\"1\""), std::string::npos);
    EXPECT_NE(json.find("\"persistent_id\":\"field-fanout\""), std::string::npos);
    EXPECT_NE(json.find("\"annotations\":[{\"name\":\"Id\""), std::string::npos);
    EXPECT_NE(json.find("\"annotations\":[{\"name\":\"PersistentId\""), std::string::npos);

    std::filesystem::remove(path);
}

TEST(EditSession, StateJsonExportsDiagnostics) {
    Environment env;
    EditSession s(env);
    load_core(s);
    ASSERT_TRUE(s.new_graph("Diagnostics").is_ok());
    ASSERT_TRUE(s.add_node("PrintString", "logger").is_ok());
    ASSERT_TRUE(s.add_node("Delay", "wait").is_ok());
    ASSERT_TRUE(s.add_event("OnStart").is_ok());
    ASSERT_TRUE(s.add_flow("OnStart", "logger", "exit", "wait", "enter").is_ok());
    ASSERT_TRUE(s.add_flow("OnStart", "logger", "exit", "wait", "enter").is_ok());

    auto diags = s.validate();
    ASSERT_FALSE(diags.empty());
    auto duplicate = std::find_if(diags.begin(), diags.end(), [](const Diagnostic& diag) {
        return diag.code == "GS_GRAPH_DUPLICATE_CONNECTION";
    });
    ASSERT_NE(duplicate, diags.end());
    EXPECT_EQ(duplicate->target.block_kind, "event");
    EXPECT_EQ(duplicate->target.block_name, "OnStart");
    ASSERT_FALSE(duplicate->actions.empty());
    EXPECT_EQ(duplicate->actions[0].title, "Remove duplicate connection");
    EXPECT_EQ(duplicate->actions[0].kind, "quickfix");
    EXPECT_EQ(duplicate->actions[0].command, "unflow logger.exit wait.enter");

    std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"diagnostics\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"diagnostic:GS_GRAPH_DUPLICATE_CONNECTION"), std::string::npos);
    EXPECT_NE(json.find("\"severity\":\"warning\""), std::string::npos);
    EXPECT_NE(json.find("\"message\":\"Duplicate connection detected\""), std::string::npos);
    EXPECT_NE(json.find("\"code\":\"GS_GRAPH_DUPLICATE_CONNECTION\""), std::string::npos);
    EXPECT_NE(json.find("\"range\":{\"start\":{\"line\":1,\"column\":1},\"end\":{\"line\":1,\"column\":1}}"), std::string::npos);
    EXPECT_NE(json.find("\"target\""), std::string::npos);
    EXPECT_NE(json.find("\"graph\":\"Diagnostics\""), std::string::npos);
    EXPECT_NE(json.find("\"block_kind\":\"event\""), std::string::npos);
    EXPECT_NE(json.find("\"block_name\":\"OnStart\""), std::string::npos);
    EXPECT_NE(json.find("\"node_instance\":\"logger\""), std::string::npos);
    EXPECT_NE(json.find("\"pin_name\":\"exit\""), std::string::npos);
    EXPECT_NE(json.find("\"connection_kind\":\"exec\""), std::string::npos);
    EXPECT_NE(json.find("\"actions\""), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"diagnostic-action:diagnostic:GS_GRAPH_DUPLICATE_CONNECTION"), std::string::npos);
    EXPECT_NE(json.find("\"title\":\"Remove duplicate connection\""), std::string::npos);
    EXPECT_NE(json.find("\"kind\":\"quickfix\""), std::string::npos);
    EXPECT_NE(json.find("\"command\":\"unflow logger.exit wait.enter\""), std::string::npos);

    std::string diagnostics = s.diagnostics_to_json();
    EXPECT_NE(diagnostics.find("\"id\":\"diagnostic:GS_GRAPH_DUPLICATE_CONNECTION"), std::string::npos);
    EXPECT_NE(diagnostics.find("\"message\":\"Duplicate connection detected\""), std::string::npos);
    EXPECT_NE(diagnostics.find("\"node_instance\":\"logger\""), std::string::npos);
    EXPECT_NE(diagnostics.find("\"actions\""), std::string::npos);
}

TEST(EditSession, ValidateReportsDanglingGraphInterfacePinsAfterRemoval) {
    Environment env;
    EditSession s(env);
    load_core(s);

    ASSERT_TRUE(s.new_graph("Child").is_ok());
    ASSERT_TRUE(s.add_param(ParamDirection::In, "msg", "FString").is_ok());
    ASSERT_TRUE(s.add_event("Run").is_ok());

    ASSERT_TRUE(s.new_graph("Parent").is_ok());
    ASSERT_TRUE(s.add_param(ParamDirection::In, "incoming", "FString").is_ok());
    ASSERT_TRUE(s.add_node("Child", "child").is_ok());
    ASSERT_TRUE(s.add_event("OnStart").is_ok());
    ASSERT_TRUE(s.add_flow("OnStart", "context", "start", "child", "Run").is_ok());
    ASSERT_TRUE(s.add_link("OnStart", "child", "msg", "incoming").is_ok());

    ASSERT_TRUE(s.set_active("Child").is_ok());
    ASSERT_TRUE(s.remove_param("msg").is_ok());
    ASSERT_TRUE(s.remove_event("Run").is_ok());
    ASSERT_TRUE(s.set_active("Parent").is_ok());

    auto diags = s.validate();
    auto missing_event_pin = std::find_if(diags.begin(), diags.end(), [](const Diagnostic& diag) {
        return diag.code == "GS_GRAPH_DANGLING_TARGET_PIN" &&
               diag.target.connection_kind == "exec" &&
               diag.target.node_instance == "child" &&
               diag.target.pin_name == "Run";
    });
    ASSERT_NE(missing_event_pin, diags.end());
    EXPECT_EQ(missing_event_pin->message, "Connection references missing target pin");
    EXPECT_EQ(missing_event_pin->target.block_kind, "event");
    EXPECT_EQ(missing_event_pin->target.block_name, "OnStart");
    EXPECT_EQ(missing_event_pin->target.reference, "child.Run");
    ASSERT_FALSE(missing_event_pin->actions.empty());
    EXPECT_EQ(missing_event_pin->actions[0].command, "unflow context.start child.Run");

    auto missing_param_pin = std::find_if(diags.begin(), diags.end(), [](const Diagnostic& diag) {
        return diag.code == "GS_GRAPH_DANGLING_TARGET_PIN" &&
               diag.target.connection_kind == "data" &&
               diag.target.node_instance == "child" &&
               diag.target.pin_name == "msg";
    });
    ASSERT_NE(missing_param_pin, diags.end());
    EXPECT_EQ(missing_param_pin->target.reference, "child.msg");
    ASSERT_FALSE(missing_param_pin->actions.empty());
    EXPECT_EQ(missing_param_pin->actions[0].command, "unlink child.msg");

    const std::string json = s.diagnostics_to_json();
    EXPECT_NE(json.find("\"code\":\"GS_GRAPH_DANGLING_TARGET_PIN\""), std::string::npos);
    EXPECT_NE(json.find("\"reference\":\"child.Run\""), std::string::npos);
    EXPECT_NE(json.find("\"reference\":\"child.msg\""), std::string::npos);
    EXPECT_NE(json.find("\"command\":\"unflow context.start child.Run\""), std::string::npos);
    EXPECT_NE(json.find("\"command\":\"unlink child.msg\""), std::string::npos);
}

TEST(EditSession, ValidateAllReportsInactiveGraphDiagnostics) {
    Environment env;
    EditSession s(env);
    load_core(s);

    ASSERT_TRUE(s.new_graph("Child").is_ok());
    ASSERT_TRUE(s.add_param(ParamDirection::In, "msg", "FString").is_ok());
    ASSERT_TRUE(s.add_event("Run").is_ok());

    ASSERT_TRUE(s.new_graph("Parent").is_ok());
    ASSERT_TRUE(s.add_param(ParamDirection::In, "incoming", "FString").is_ok());
    ASSERT_TRUE(s.add_node("Child", "child").is_ok());
    ASSERT_TRUE(s.add_event("OnStart").is_ok());
    ASSERT_TRUE(s.add_flow("OnStart", "context", "start", "child", "Run").is_ok());
    ASSERT_TRUE(s.add_link("OnStart", "child", "msg", "incoming").is_ok());

    ASSERT_TRUE(s.set_active("Child").is_ok());
    ASSERT_TRUE(s.remove_param("msg").is_ok());
    ASSERT_TRUE(s.remove_event("Run").is_ok());

    const auto active_diags = s.validate();
    EXPECT_EQ(std::find_if(active_diags.begin(), active_diags.end(), [](const Diagnostic& diag) {
        return diag.target.graph == "Parent" &&
               (diag.target.reference == "child.Run" || diag.target.reference == "child.msg");
    }), active_diags.end());

    const auto module_diags = s.validate_all();
    auto missing_event_pin = std::find_if(module_diags.begin(), module_diags.end(), [](const Diagnostic& diag) {
        return diag.code == "GS_GRAPH_DANGLING_TARGET_PIN" &&
               diag.target.graph == "Parent" &&
               diag.target.connection_kind == "exec" &&
               diag.target.reference == "child.Run";
    });
    ASSERT_NE(missing_event_pin, module_diags.end());
    ASSERT_FALSE(missing_event_pin->actions.empty());
    EXPECT_EQ(missing_event_pin->actions[0].command, "unflow context.start child.Run");

    auto missing_param_pin = std::find_if(module_diags.begin(), module_diags.end(), [](const Diagnostic& diag) {
        return diag.code == "GS_GRAPH_DANGLING_TARGET_PIN" &&
               diag.target.graph == "Parent" &&
               diag.target.connection_kind == "data" &&
               diag.target.reference == "child.msg";
    });
    ASSERT_NE(missing_param_pin, module_diags.end());
    ASSERT_FALSE(missing_param_pin->actions.empty());
    EXPECT_EQ(missing_param_pin->actions[0].command, "unlink child.msg");

    const std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"module_diagnostics\""), std::string::npos);
    EXPECT_NE(json.find("\"reference\":\"child.Run\""), std::string::npos);
    EXPECT_NE(json.find("\"reference\":\"child.msg\""), std::string::npos);
}

TEST(EditSession, ValidateReportsDanglingBareParameterAfterRemoval) {
    Environment env;
    EditSession s(env);
    load_core(s);

    ASSERT_TRUE(s.new_graph("BareParamDangling").is_ok());
    ASSERT_TRUE(s.add_param(ParamDirection::In, "msg", "FString").is_ok());
    ASSERT_TRUE(s.add_node("PrintString", "logger").is_ok());
    ASSERT_TRUE(s.add_event("OnStart").is_ok());
    ASSERT_TRUE(s.add_link("OnStart", "logger", "message", "msg").is_ok());

    ASSERT_TRUE(s.remove_param("msg").is_ok());

    auto diags = s.validate();
    auto missing_param = std::find_if(diags.begin(), diags.end(), [](const Diagnostic& diag) {
        return diag.code == "GS_GRAPH_DANGLING_PARAMETER" &&
               diag.target.parameter_name == "msg";
    });
    ASSERT_NE(missing_param, diags.end());
    EXPECT_EQ(missing_param->message, "Data link references missing graph parameter");
    EXPECT_EQ(missing_param->target.graph, "BareParamDangling");
    EXPECT_EQ(missing_param->target.block_kind, "event");
    EXPECT_EQ(missing_param->target.block_name, "OnStart");
    EXPECT_EQ(missing_param->target.reference, "msg");
    EXPECT_EQ(missing_param->target.connection_kind, "data");
    ASSERT_FALSE(missing_param->actions.empty());
    EXPECT_EQ(missing_param->actions[0].command, "unlink logger.message");

    const std::string json = s.state_to_json();
    EXPECT_NE(json.find("\"code\":\"GS_GRAPH_DANGLING_PARAMETER\""), std::string::npos);
    EXPECT_NE(json.find("\"parameter_name\":\"msg\""), std::string::npos);
    EXPECT_NE(json.find("\"command\":\"unlink logger.message\""), std::string::npos);
}

TEST(EditSession, ValidateReportsPinKindAndDirectionMismatches) {
    Environment env;
    NodeDefinition mixed;
    mixed.type_name = "MixedPins";
    mixed.is_native = true;
    mixed.pins = {
        {"execIn", PinKind::Exec, PinDirection::Input, ""},
        {"execOut", PinKind::Exec, PinDirection::Output, ""},
        {"dataIn", PinKind::Data, PinDirection::Input, "FString"},
        {"dataOut", PinKind::Data, PinDirection::Output, "FString"}
    };
    env.nodes().register_node(std::move(mixed));

    EditSession s(env);
    ASSERT_TRUE(s.new_graph("PinMismatches").is_ok());
    ASSERT_TRUE(s.add_node("MixedPins", "a").is_ok());
    ASSERT_TRUE(s.add_node("MixedPins", "b").is_ok());
    ASSERT_TRUE(s.add_event("OnStart").is_ok());
    ASSERT_TRUE(s.add_flow("OnStart", "a", "dataIn", "b", "execIn").is_ok());
    ASSERT_TRUE(s.add_flow("OnStart", "a", "execIn", "b", "execIn").is_ok());
    ASSERT_TRUE(s.add_link("OnStart", "a", "dataIn", "b", "execOut").is_ok());
    ASSERT_TRUE(s.add_link("OnStart", "a", "dataOut", "b", "dataOut").is_ok());

    const auto diags = s.validate();
    auto flow_kind = std::find_if(diags.begin(), diags.end(), [](const Diagnostic& diag) {
        return diag.code == "GS_GRAPH_PIN_KIND_MISMATCH" &&
               diag.target.connection_kind == "exec" &&
               diag.target.node_instance == "a" &&
               diag.target.pin_name == "dataIn";
    });
    ASSERT_NE(flow_kind, diags.end());
    EXPECT_EQ(flow_kind->message, "Connection pin kind mismatch");
    EXPECT_EQ(flow_kind->target.reference, "a.dataIn");
    ASSERT_FALSE(flow_kind->actions.empty());
    EXPECT_EQ(flow_kind->actions[0].command, "unflow a.dataIn b.execIn");

    auto flow_direction = std::find_if(diags.begin(), diags.end(), [](const Diagnostic& diag) {
        return diag.code == "GS_GRAPH_PIN_DIRECTION_MISMATCH" &&
               diag.target.connection_kind == "exec" &&
               diag.target.node_instance == "a" &&
               diag.target.pin_name == "execIn";
    });
    ASSERT_NE(flow_direction, diags.end());
    EXPECT_EQ(flow_direction->target.reference, "a.execIn");
    ASSERT_FALSE(flow_direction->actions.empty());
    EXPECT_EQ(flow_direction->actions[0].command, "unflow a.execIn b.execIn");

    auto link_kind = std::find_if(diags.begin(), diags.end(), [](const Diagnostic& diag) {
        return diag.code == "GS_GRAPH_PIN_KIND_MISMATCH" &&
               diag.target.connection_kind == "data" &&
               diag.target.node_instance == "b" &&
               diag.target.pin_name == "execOut";
    });
    ASSERT_NE(link_kind, diags.end());
    ASSERT_FALSE(link_kind->actions.empty());
    EXPECT_EQ(link_kind->actions[0].command, "unlink a.dataIn");

    auto link_direction = std::find_if(diags.begin(), diags.end(), [](const Diagnostic& diag) {
        return diag.code == "GS_GRAPH_PIN_DIRECTION_MISMATCH" &&
               diag.target.connection_kind == "data" &&
               diag.target.node_instance == "a" &&
               diag.target.pin_name == "dataOut";
    });
    ASSERT_NE(link_direction, diags.end());
    ASSERT_FALSE(link_direction->actions.empty());
    EXPECT_EQ(link_direction->actions[0].command, "unlink a.dataOut");
}

TEST(EditSession, ValidateReportsStrictDataTypeMismatches) {
    Environment env;

    GraphSchema strict;
    strict.name = "StrictGraph";
    strict.connection_policy.strict_type_match = true;
    env.schemas().register_schema(std::move(strict));

    NodeDefinition int_source;
    int_source.type_name = "IntSource";
    int_source.is_native = true;
    int_source.pins = {{"value", PinKind::Data, PinDirection::Output, "int"}};
    env.nodes().register_node(std::move(int_source));

    NodeDefinition float_sink;
    float_sink.type_name = "FloatSink";
    float_sink.is_native = true;
    float_sink.pins = {{"value", PinKind::Data, PinDirection::Input, "float"}};
    env.nodes().register_node(std::move(float_sink));

    EditSession s(env);
    ASSERT_TRUE(s.new_graph("TypedMismatches", "StrictGraph").is_ok());
    ASSERT_TRUE(s.add_param(ParamDirection::In, "count", "int").is_ok());
    ASSERT_TRUE(s.add_node("IntSource", "src").is_ok());
    ASSERT_TRUE(s.add_node("FloatSink", "sink").is_ok());
    ASSERT_TRUE(s.add_node("FloatSink", "paramSink").is_ok());
    ASSERT_TRUE(s.add_event("OnStart").is_ok());
    ASSERT_TRUE(s.add_link("OnStart", "sink", "value", "src", "value").is_ok());
    ASSERT_TRUE(s.add_link("OnStart", "paramSink", "value", "count").is_ok());

    const auto diags = s.validate();
    auto node_type_mismatch = std::find_if(diags.begin(), diags.end(), [](const Diagnostic& diag) {
        return diag.code == "GS_GRAPH_PIN_TYPE_MISMATCH" &&
               diag.target.node_instance == "sink" &&
               diag.target.pin_name == "value" &&
               diag.target.reference == "src.value->sink.value";
    });
    ASSERT_NE(node_type_mismatch, diags.end());
    EXPECT_EQ(node_type_mismatch->message, "Data link type mismatch");
    ASSERT_FALSE(node_type_mismatch->actions.empty());
    EXPECT_EQ(node_type_mismatch->actions[0].command, "unlink sink.value");

    auto param_type_mismatch = std::find_if(diags.begin(), diags.end(), [](const Diagnostic& diag) {
        return diag.code == "GS_GRAPH_PIN_TYPE_MISMATCH" &&
               diag.target.node_instance == "paramSink" &&
               diag.target.pin_name == "value" &&
               diag.target.reference == "count->paramSink.value";
    });
    ASSERT_NE(param_type_mismatch, diags.end());
    ASSERT_FALSE(param_type_mismatch->actions.empty());
    EXPECT_EQ(param_type_mismatch->actions[0].command, "unlink paramSink.value");

    const std::string json = s.diagnostics_to_json();
    EXPECT_NE(json.find("\"code\":\"GS_GRAPH_PIN_TYPE_MISMATCH\""), std::string::npos);
    EXPECT_NE(json.find("\"reference\":\"src.value->sink.value\""), std::string::npos);
    EXPECT_NE(json.find("\"reference\":\"count->paramSink.value\""), std::string::npos);
}

TEST(EditSession, AnnotationEditApiUpsertsRemovesAndEmits) {
    Environment env;
    EditSession s(env);
    load_core(s);
    ASSERT_TRUE(s.new_graph("Annotated").is_ok());
    ASSERT_TRUE(s.add_param(ParamDirection::In, "name", "FString").is_ok());
    ASSERT_TRUE(s.add_node("PrintString", "logger").is_ok());

    ASSERT_TRUE(s.set_graph_annotation({"Comment", {{"", "title"}, {"", "Graph note"}}}).is_ok());
    ASSERT_TRUE(s.set_param_annotation("name", {"Tooltip", {{"", "Player name"}}}).is_ok());
    ASSERT_TRUE(s.set_node_annotation("logger", {"Position", {{"X", "100"}, {"Y", "200"}}}).is_ok());

    auto text = s.emit();
    EXPECT_NE(text.find("[Comment(\"title\", \"Graph note\")]"), std::string::npos);
    EXPECT_NE(text.find("[Tooltip(\"Player name\")]"), std::string::npos);
    EXPECT_NE(text.find("[Position(X = 100, Y = 200)]"), std::string::npos);

    ASSERT_TRUE(s.set_node_annotation("logger", {"Position", {{"X", "300"}, {"Y", "400"}}}).is_ok());
    text = s.emit();
    EXPECT_EQ(text.find("[Position(X = 100, Y = 200)]"), std::string::npos);
    EXPECT_NE(text.find("[Position(X = 300, Y = 400)]"), std::string::npos);

    ASSERT_TRUE(s.remove_node_annotation("logger", "Position").is_ok());
    ASSERT_TRUE(s.remove_param_annotation("name", "Tooltip").is_ok());
    ASSERT_TRUE(s.remove_graph_annotation("Comment").is_ok());
    text = s.emit();
    EXPECT_EQ(text.find("[Position("), std::string::npos);
    EXPECT_EQ(text.find("[Tooltip("), std::string::npos);
    EXPECT_EQ(text.find("[Comment("), std::string::npos);

    ASSERT_TRUE(s.undo().is_ok());
    text = s.emit();
    EXPECT_NE(text.find("[Comment(\"title\", \"Graph note\")]"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════
// Import + Module
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, ImportAndModuleState) {
    Environment env;
    EditSession s(env);

    // add_import only adds the declaration (no compile), so start fresh
    s.add_import("ue_core.d.gs");
    EXPECT_EQ(s.module().imports.size(), 1u);
    EXPECT_TRUE(s.module().imports[0].is_native);
    EXPECT_FALSE(s.module().imports[0].loaded);

    // Duplicate import is ignored
    s.add_import("ue_core.d.gs");
    EXPECT_EQ(s.module().imports.size(), 1u);

    // load_import compiles + adds import; total should remain 1 due to dedup
    std::string path = std::string(GS_PRESETS_DIR) + "/ue_core.d.gs";
    s.load_import(path);
    EXPECT_GE(s.module().imports.size(), 1u);
}

TEST(EditSession, LoadImportDeduplicatesAndMarksLoaded) {
    Environment env;
    EditSession s(env);

    std::filesystem::path path = std::filesystem::path(GS_PRESETS_DIR) / "ue_core.d.gs";
    std::filesystem::path equivalent = path.parent_path() / "." / path.filename();

    auto loaded = s.load_import(equivalent.string());
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    ASSERT_EQ(s.module().imports.size(), 1u);
    EXPECT_TRUE(s.module().imports[0].loaded);
    EXPECT_TRUE(s.is_import_loaded(path.string()));

    auto loaded_again = s.load_import(path.string());
    ASSERT_TRUE(loaded_again.is_ok()) << loaded_again.error();
    EXPECT_EQ(s.module().imports.size(), 1u);

    auto json = s.state_to_json();
    EXPECT_NE(json.find("\"loaded\":true"), std::string::npos);
    EXPECT_NE(json.find("\"normalized_path\":"), std::string::npos);
}

TEST(EditSession, SourceImportDeclarationIsNotLoadedUntilImportCommand) {
    Environment env;
    EditSession s(env);

    std::filesystem::path preset_dir = std::filesystem::path(GS_PRESETS_DIR);
    std::filesystem::path import_path = preset_dir / "ue_core.d.gs";
    std::filesystem::path source_name = preset_dir / "module_with_import.gs";
    const std::string source = R"(import "ue_core.d.gs";
graph Imported {
}
)";

    auto loaded_source = s.load_source(source, source_name.string());
    ASSERT_TRUE(loaded_source.is_ok()) << loaded_source.error();
    ASSERT_EQ(s.module().imports.size(), 1u);
    EXPECT_FALSE(s.module().imports[0].loaded);

    auto loaded_import = s.load_import(import_path.string());
    ASSERT_TRUE(loaded_import.is_ok()) << loaded_import.error();
    ASSERT_EQ(s.module().imports.size(), 1u);
    EXPECT_TRUE(s.module().imports[0].loaded);
}

TEST(EditSession, LoadSourceReplacesModuleAtomically) {
    Environment env;
    EditSession s(env);
    load_core(s);
    ASSERT_TRUE(s.new_graph("Original").is_ok());
    ASSERT_TRUE(s.add_node("PrintString", "logger").is_ok());

    const std::string valid = R"(Graph Synced {
    in speed : float;
    PrintString printer{};
}
)";
    auto loaded = s.load_source(valid);
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    ASSERT_EQ(s.module().graphs.size(), 1u);
    EXPECT_EQ(s.module().graphs[0].name, "Synced");
    EXPECT_EQ(s.active_index(), 0);
    EXPECT_TRUE(s.dirty());
    EXPECT_TRUE(s.can_undo());
    EXPECT_FALSE(s.can_redo());
    EXPECT_NE(s.emit().find("in speed : float"), std::string::npos);

    auto undone = s.undo();
    ASSERT_TRUE(undone.is_ok()) << undone.error();
    EXPECT_EQ(undone.value(), "apply source");
    ASSERT_EQ(s.module().graphs.size(), 1u);
    EXPECT_EQ(s.module().graphs[0].name, "Original");
    EXPECT_NE(s.emit().find("PrintString logger"), std::string::npos);
    EXPECT_TRUE(s.can_redo());

    auto redone = s.redo();
    ASSERT_TRUE(redone.is_ok()) << redone.error();
    EXPECT_EQ(redone.value(), "apply source");
    ASSERT_EQ(s.module().graphs.size(), 1u);
    EXPECT_EQ(s.module().graphs[0].name, "Synced");
    EXPECT_NE(s.emit().find("in speed : float"), std::string::npos);

    const std::string invalid = R"(Graph Broken {
    in speed float;
}
)";
    auto failed = s.load_source(invalid);
    ASSERT_TRUE(failed.is_err());
    EXPECT_EQ(s.module().graphs[0].name, "Synced");
    EXPECT_NE(s.emit().find("Graph Synced"), std::string::npos);
}

TEST(EditSession, LetDeclaration) {
    Environment env;
    EditSession s(env);
    s.add_let("spawn_point", "FVector", "0,0,100");
    EXPECT_EQ(s.module().top_level_lets.size(), 1u);
    EXPECT_EQ(s.module().top_level_lets[0].name, "spawn_point");
}

// ═══════════════════════════════════════════════════════════════════
// Build + Validate + Bake via session
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, BuildEditGraphAndBake) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("Simple");
    s.add_node("PrintString", "p");

    auto eg = s.build_edit_graph();
    ASSERT_TRUE(eg.has_value());
    EXPECT_EQ(eg->name(), "Simple");
    EXPECT_EQ(eg->node_count(), 1u);

    auto diags = s.validate();
    EXPECT_TRUE(diags.empty());
}

// ═══════════════════════════════════════════════════════════════════
// Full workflow simulation
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, FullWorkflowSimulation) {
    Environment env;
    EditSession s(env);
    load_core(s);

    // Step 1: Create graph with params
    s.new_graph("GameplayAbility");
    s.add_param(ParamDirection::In, "caster", "AActor");
    s.add_param(ParamDirection::In, "damage", "float", "50.0");
    s.add_param(ParamDirection::Out, "success", "bool");

    // Step 2: Add nodes
    s.add_node("PrintString", "dmgLog");
    s.add_node("Delay", "cooldown");

    // Step 3: Create event with connections
    s.add_event("OnActivate");
    s.add_flow("OnActivate", "context", "start", "dmgLog", "enter");
    s.add_flow("OnActivate", "dmgLog", "exit", "cooldown", "enter");
    s.add_link("OnActivate", "dmgLog", "message", "damage");
    s.add_link("OnActivate", "cooldown", "duration", "damage");

    // Step 4: Create function (functions can only reference context and params)
    s.add_function("Reset");
    s.add_flow("Reset", "context", "start", "context", "done");

    // Step 5: Add generate metadata
    s.add_comment("desc", "Gameplay ability with cooldown");
    s.add_meta("position", "dmgLog", "x", "200");
    s.add_meta("position", "dmgLog", "y", "100");

    // Verify structure
    auto* g = s.active_graph();
    EXPECT_EQ(g->parameters.size(), 3u);
    EXPECT_EQ(g->node_instances.size(), 2u);
    EXPECT_EQ(g->events.size(), 1u);
    EXPECT_EQ(g->functions.size(), 1u);
    EXPECT_TRUE(g->generate.has_value());
    EXPECT_EQ(g->events[0].flow_connections.size(), 2u);
    EXPECT_EQ(g->events[0].data_links.size(), 2u);

    // Step 6: Emit and verify
    auto text = s.emit();
    EXPECT_NE(text.find("GameplayAbility"), std::string::npos);
    EXPECT_NE(text.find("in damage : float = 50.0"), std::string::npos);
    EXPECT_NE(text.find("event OnActivate"), std::string::npos);
    EXPECT_NE(text.find("function Reset"), std::string::npos);
    EXPECT_NE(text.find("Comment desc"), std::string::npos);

    // Step 7: Undo the function, verify it's gone
    s.undo(); // undo add_meta
    s.undo(); // undo add_meta
    s.undo(); // undo add_comment
    s.undo(); // undo add_flow in Reset
    s.undo(); // undo add_function Reset
    EXPECT_EQ(s.active_graph()->functions.size(), 0u);

    // Step 8: Redo it back
    s.redo(); // redo add_function
    EXPECT_EQ(s.active_graph()->functions.size(), 1u);
    EXPECT_EQ(s.active_graph()->functions[0].name, "Reset");
}

TEST(EditSession, LoadAssetSourceProjectsGraphAndPreservesSourceEmit) {
    Environment env;
    EditSession s(env);

    const std::string source = R"(graph Execute {
    schema AbilityGraph;
    @graph.input
    param target: Actor;

    node log {
        type PrintString;
        message: "done";
    }

    event Start {
        connect(context.start, log.enter);
        bind(target, log.message);
    }
}
)";

    auto loaded = s.load_source(source, "asset_session.gs");
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    ASSERT_NE(s.active_graph(), nullptr);
    EXPECT_EQ(s.active_graph()->name, "Execute");
    ASSERT_EQ(s.active_graph()->parameters.size(), 1u);
    EXPECT_EQ(s.active_graph()->parameters[0].name, "target");
    EXPECT_EQ(s.active_graph()->parameters[0].direction, ParamDirection::In);
    ASSERT_EQ(s.active_graph()->node_instances.size(), 1u);
    EXPECT_EQ(s.active_graph()->node_instances[0].instance_name, "log");
    EXPECT_EQ(s.active_graph()->node_instances[0].type_name, "PrintString");
    ASSERT_EQ(s.active_graph()->events.size(), 1u);
    EXPECT_EQ(s.active_graph()->events[0].name, "Start");
    EXPECT_EQ(s.active_graph()->events[0].flow_connections.size(), 1u);
    EXPECT_EQ(s.active_graph()->events[0].data_links.size(), 1u);
    EXPECT_EQ(s.emit(), source);

    auto add_param = s.add_param(ParamDirection::In, "amount", "float");
    ASSERT_TRUE(add_param.is_ok()) << add_param.error();
    EXPECT_EQ(s.emit().find("Graph Execute"), 0u);
}

TEST(EditSession, LoadAssetImportRejectsInvalidSchemaPolicyValue) {
    auto dir = std::filesystem::temp_directory_path() / "graphscript_asset_import_bad_policy";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    auto path = dir / "bad_schema.d.gs";
    {
        std::ofstream out(path, std::ios::binary);
        out << "export declare schema AbilityGraph: FlowGraphSchema {\n"
               "    max_exec_fan_out: bad;\n"
               "}\n";
    }

    Environment env;
    EditSession s(env);
    auto loaded = s.load_import(path.string());
    ASSERT_TRUE(loaded.is_err());
    EXPECT_NE(loaded.error().find("Invalid max_exec_fan_out"), std::string::npos);
}

TEST(EditSession, LoadAssetSourceRequiresProjectedGraph) {
    Environment env;
    EditSession s(env);

    const std::string source =
        "import \"types.d.gs\";\n"
        "export declare type Actor;\n";

    auto loaded = s.load_source(source, "declarations_only.gs");
    EXPECT_TRUE(loaded.is_err());
    EXPECT_EQ(s.active_graph(), nullptr);
}

TEST(EditSession, LoadAssetSourceRejectsImportOnlyWithoutProjectedGraph) {
    Environment env;
    EditSession s(env);

    const std::string source = "import \"types.d.gs\";\n";

    auto loaded = s.load_source(source, "import_only.gs");
    EXPECT_TRUE(loaded.is_err());
    EXPECT_EQ(s.active_graph(), nullptr);
}

TEST(EditSession, LoadAssetSourceRejectsDeclarationOnlyWithoutProjectedGraph) {
    Environment env;
    EditSession s(env);

    const std::string source = "export declare type Actor;\n";

    auto loaded = s.load_source(source, "declaration_only.gs");
    EXPECT_TRUE(loaded.is_err());
    EXPECT_EQ(s.active_graph(), nullptr);
}

TEST(EditSession, LoadAssetImportRejectsInvalidMaxExecFanOutValue) {
    Environment env;
    EditSession s(env);

    const std::string source =
        "export declare schema BadPolicy: FlowGraphSchema {\n"
        "    max_exec_fan_out: nope;\n"
        "    allow_exec_fan_in: true;\n"
        "}\n";
    const auto path = std::filesystem::temp_directory_path() / "graphscript_asset_bad_policy_090.d.gs";
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << source;
    }

    auto loaded = s.load_import(path.string());
    ASSERT_TRUE(loaded.is_err());
    EXPECT_NE(loaded.error().find("Invalid max_exec_fan_out"), std::string::npos);
    EXPECT_EQ(env.schemas().find("BadPolicy"), nullptr);
    EXPECT_FALSE(s.is_import_loaded(path.string()));
}

TEST(EditSession, LoadAssetImportAcceptsDeclarationOnlyMetadataKinds) {
    struct Case {
        const char* name;
        const char* source;
    };

    const Case cases[] = {
        {"module_only", "declare module \"meta.only\" {\n    version: \"1\";\n}\n"},
        {"enum_only", "export declare enum Mode {\n    A;\n}\n"},
        {"kind_only", "export declare kind block dialog;\n"},
        {"block_only", "export declare block dialog {\n    allows property title;\n}\n"},
        {"command_only", "export declare command connect(from: PinRef, to: PinRef): EdgeRef;\n"},
        {"lint_only", "export declare lint DialogLint for DialogGraph;\n"},
    };

    for (const auto& item : cases) {
        Environment env;
        EditSession s(env);
        const auto path = std::filesystem::temp_directory_path() /
                          ("graphscript_asset_decl_only_" + std::string(item.name) + ".d.gs");
        {
            std::ofstream out(path, std::ios::binary);
            ASSERT_TRUE(out.is_open());
            out << item.source;
        }

        auto loaded = s.load_import(path.string());
        ASSERT_TRUE(loaded.is_ok()) << item.name << ": " << loaded.error();
        ASSERT_EQ(s.module().imports.size(), 1u) << item.name;
        EXPECT_TRUE(s.module().imports[0].loaded) << item.name;
    }
}

TEST(EditSession, LoadAssetImportRejectsMalformedAssetDeclarationWithoutLegacyFallback) {
    const auto path = std::filesystem::temp_directory_path() / "graphscript_asset_malformed_decl_012.d.gs";
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "export declare object Broken {\n"
               "    @flow.input\n"
               "    target: Actor\n"
               "}\n";
    }

    Environment env;
    EditSession s(env);
    auto loaded = s.load_import(path.string());
    ASSERT_TRUE(loaded.is_err());
    EXPECT_NE(loaded.error().find("Asset parse error in:"), std::string::npos);
    EXPECT_FALSE(s.is_import_loaded(path.string()));
    EXPECT_TRUE(s.module().imports.empty());
    EXPECT_TRUE(env.nodes().all().empty());
}

TEST(EditSession, LoadAssetImportRejectsGraphSourceWithImportOrExportList) {
    struct Case {
        const char* name;
        const char* source;
    };

    const Case cases[] = {
        {"with_import", "import \"core.d.gs\";\ngraph Execute {\n}\n"},
        {"with_export_list", "export { Execute };\ngraph Execute {\n}\n"},
    };

    for (const auto& item : cases) {
        Environment env;
        EditSession s(env);
        const auto path = std::filesystem::temp_directory_path() /
                          ("graphscript_asset_graph_source_import_" + std::string(item.name) + ".d.gs");
        {
            std::ofstream out(path, std::ios::binary);
            ASSERT_TRUE(out.is_open());
            out << item.source;
        }

        auto loaded = s.load_import(path.string());
        ASSERT_TRUE(loaded.is_err()) << item.name;
        EXPECT_FALSE(s.is_import_loaded(path.string())) << item.name;
        EXPECT_TRUE(s.module().imports.empty()) << item.name;
    }
}

TEST(EditSession, LoadAssetSourceRejectsLintErrors) {
    Environment env;
    EditSession s(env);

    const std::string source = R"(graph Bad {
    node log {
        type PrintString;
    }
    node log {
        type PrintString;
    }
}
)";

    auto loaded = s.load_source(source, "duplicate_alias.gs");
    ASSERT_TRUE(loaded.is_err());
    EXPECT_EQ(s.active_graph(), nullptr);
}

TEST(EditSession, LoadAssetSourceRejectsLintErrorsWithoutMutatingSession) {
    Environment env;
    EditSession s(env);

    const std::string good_source = R"(graph Execute {
    schema AbilityGraph;

    node log {
        type PrintString;
        message: "done";
    }
}
)";
    auto good = s.load_source(good_source, "good_asset.gs");
    ASSERT_TRUE(good.is_ok()) << good.error();
    ASSERT_NE(s.active_graph(), nullptr);
    EXPECT_EQ(s.active_graph()->name, "Execute");
    EXPECT_EQ(s.emit(), good_source);

    const std::string bad_source = R"(graph Bad {
    node log {
        type PrintString;
    }
    node log {
        type PrintString;
    }
}
)";
    auto loaded = s.load_source(bad_source, "duplicate_alias.gs");
    ASSERT_TRUE(loaded.is_err());
    EXPECT_NE(loaded.error().find("Asset lint error"), std::string::npos);
    ASSERT_NE(s.active_graph(), nullptr);
    EXPECT_EQ(s.active_graph()->name, "Execute");
    EXPECT_EQ(s.emit(), good_source);
}

TEST(EditSession, LoadAssetSourceRejectsProjectionErrorsWithoutMutatingSession) {
    Environment env;
    EditSession s(env);

    const std::string good_source = R"(graph Execute {
    schema AbilityGraph;

    node log {
        type PrintString;
        message: "done";
    }
}
)";
    auto good = s.load_source(good_source, "good_asset.gs");
    ASSERT_TRUE(good.is_ok()) << good.error();
    ASSERT_NE(s.active_graph(), nullptr);
    EXPECT_EQ(s.active_graph()->name, "Execute");
    EXPECT_EQ(s.emit(), good_source);

    const std::string bad_source = R"(graph Broken {
    schema AbilityGraph;

    node log {
        type PrintString;
    }

    bind(missing.value, log.message);
}
)";
    auto loaded = s.load_source(bad_source, "bad_projection.gs");
    ASSERT_TRUE(loaded.is_err());
    EXPECT_NE(loaded.error().find("Unknown source in data link"), std::string::npos);
    ASSERT_NE(s.active_graph(), nullptr);
    EXPECT_EQ(s.active_graph()->name, "Execute");
    EXPECT_EQ(s.emit(), good_source);
}

TEST(EditSession, LoadAssetImportRejectsLintErrorsWithoutRegistering) {
    Environment env;
    EditSession s(env);

    const std::string source =
        "export declare type Actor;\n"
        "export declare type Actor;\n";
    const auto path = std::filesystem::temp_directory_path() / "graphscript_asset_duplicate_type_090.d.gs";
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << source;
    }

    auto loaded = s.load_import(path.string());
    ASSERT_TRUE(loaded.is_err());
    EXPECT_EQ(env.types().find("Actor"), nullptr);
    EXPECT_FALSE(s.is_import_loaded(path.string()));
}

TEST(EditSession, LoadAssetImportRejectsEnvironmentConflictsWithoutMarkingLoaded) {
    auto dir = std::filesystem::temp_directory_path() / "graphscript_asset_import_conflict";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    auto first = dir / "first.d.gs";
    auto second = dir / "second.d.gs";
    {
        std::ofstream out(first, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "export declare type Actor;\n";
    }
    {
        std::ofstream out(second, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "export declare type Actor;\n";
    }

    Environment env;
    EditSession s(env);
    auto first_loaded = s.load_import(first.string());
    ASSERT_TRUE(first_loaded.is_ok()) << first_loaded.error();
    auto second_loaded = s.load_import(second.string());
    ASSERT_TRUE(second_loaded.is_err());
    EXPECT_NE(second_loaded.error().find("conflicts"), std::string::npos);
    EXPECT_TRUE(s.is_import_loaded(first.string()));
    EXPECT_FALSE(s.is_import_loaded(second.string()));
}

TEST(EditSession, LoadAssetImportAcceptsDeclarationMetadataOnlyFile) {
    const auto path = std::filesystem::temp_directory_path() / "graphscript_asset_declaration_metadata_only_090.d.gs";
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "declare module \"ability.core\" {\n"
               "    package: \"Game.Ability\";\n"
               "}\n"
               "export declare enum DamageType {\n"
               "    Fire;\n"
               "}\n"
               "export declare kind block graph;\n"
               "export declare kind command connect;\n"
               "export declare block graph {\n"
               "    allows command connect;\n"
               "}\n"
               "export declare command connect(from: PinRef, to: PinRef): EdgeRef;\n"
               "export declare lint AbilityLint for AbilityGraph;\n";
    }

    Environment env;
    EditSession s(env);
    auto loaded = s.load_import(path.string());
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    EXPECT_TRUE(s.is_import_loaded(path.string()));
    EXPECT_EQ(env.types().all().size(), 0u);
    EXPECT_EQ(env.nodes().all().size(), 0u);
    EXPECT_EQ(env.schemas().all().size(), 0u);
}

TEST(EditSession, LoadAssetImportAcceptsImportOnlyDeclarationFile) {
    const auto path = std::filesystem::temp_directory_path() / "graphscript_asset_import_only_011.d.gs";
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import \"core.d.gs\";\n";
    }

    Environment env;
    EditSession s(env);
    auto loaded = s.load_import(path.string());
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    EXPECT_TRUE(s.is_import_loaded(path.string()));
    EXPECT_EQ(s.module().imports.size(), 1u);
    EXPECT_EQ(env.types().all().size(), 0u);
    EXPECT_EQ(env.nodes().all().size(), 0u);
    EXPECT_EQ(env.schemas().all().size(), 0u);
}

TEST(EditSession, LoadAssetImportRegistersTypeObjectAndSchemaDeclarations) {
    const auto path = std::filesystem::temp_directory_path() / "graphscript_asset_registered_declarations_011.d.gs";
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "export declare type Actor;\n"
               "export declare object PrintString {\n"
               "    @flow.input\n"
               "    message: string;\n"
               "}\n"
               "export declare schema AbilityGraph: FlowGraphSchema {\n"
               "    max_exec_fan_out: 2;\n"
               "    allow_exec_fan_in: true;\n"
               "}\n";
    }

    Environment env;
    EditSession s(env);
    auto loaded = s.load_import(path.string());
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    EXPECT_TRUE(s.is_import_loaded(path.string()));

    auto* actor = env.types().find("Actor");
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(actor->source_file, path.string());

    auto* node = env.nodes().find("PrintString");
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->pins.size(), 1u);
    EXPECT_EQ(node->pins[0].name, "message");
    EXPECT_EQ(node->pins[0].direction, PinDirection::Input);

    auto* schema = env.schemas().find("AbilityGraph");
    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(schema->connection_policy.max_exec_fan_out, 2);
    EXPECT_TRUE(schema->connection_policy.allow_exec_fan_in);
}

TEST(EditSession, UndoRedoAssetSourceLoadRestoresSourceCache) {
    Environment env;
    EditSession s(env);
    ASSERT_TRUE(s.new_graph("Legacy").is_ok());

    const std::string source = R"(graph Execute {
    schema AbilityGraph;

    node log {
        type PrintString;
    }
}
)";

    auto loaded = s.load_source(source, "asset_session.gs");
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    EXPECT_EQ(s.emit(), source);

    auto undone = s.undo();
    ASSERT_TRUE(undone.is_ok()) << undone.error();
    ASSERT_NE(s.active_graph(), nullptr);
    EXPECT_EQ(s.active_graph()->name, "Legacy");
    EXPECT_NE(s.emit(), source);
    EXPECT_NE(s.emit().find("Graph Legacy"), std::string::npos);

    auto redone = s.redo();
    ASSERT_TRUE(redone.is_ok()) << redone.error();
    ASSERT_NE(s.active_graph(), nullptr);
    EXPECT_EQ(s.active_graph()->name, "Execute");
    EXPECT_EQ(s.emit(), source);
}

TEST(EditSession, UndoAssetToAssetSourceLoadRestoresPreviousSourceCache) {
    Environment env;
    EditSession s(env);

    const std::string first = R"(graph First {
    node log {
        type PrintString;
    }
}
)";
    const std::string second = R"(graph Second {
    node log {
        type PrintString;
    }
}
)";

    auto first_loaded = s.load_source(first, "first.gs");
    ASSERT_TRUE(first_loaded.is_ok()) << first_loaded.error();
    auto second_loaded = s.load_source(second, "second.gs");
    ASSERT_TRUE(second_loaded.is_ok()) << second_loaded.error();
    EXPECT_EQ(s.emit(), second);

    auto undone = s.undo();
    ASSERT_TRUE(undone.is_ok()) << undone.error();
    ASSERT_NE(s.active_graph(), nullptr);
    EXPECT_EQ(s.active_graph()->name, "First");
    EXPECT_EQ(s.emit(), first);

    auto redone = s.redo();
    ASSERT_TRUE(redone.is_ok()) << redone.error();
    ASSERT_NE(s.active_graph(), nullptr);
    EXPECT_EQ(s.active_graph()->name, "Second");
    EXPECT_EQ(s.emit(), second);
}
